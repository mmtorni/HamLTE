/*
   Implement user data multiplexing part of 3GPP LTE RLC AM
*/
/*
  TODO: Only 10 bit sequence numbers and 15 bit segment offsets are implemented
  TODO: Only 11 bit length fields are implemented
  TODO: Retransmit and poll timers
 */
#include <cstdio>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <getopt.h>
#include <vector>
#include <climits>
#include <queue>
#include <list>
#include <set>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <boost/foreach.hpp>
#include <boost/range/adaptor/sliced.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/fill.hpp>
#include <boost/range/adaptor/map.hpp>

#include <boost/range/numeric.hpp>
#include <boost/assign.hpp>

/* C external interface */
#include "rlc.h"

/* My C++ helpers */
#include "cpp_socket.hh"
#include "math.hh"
#include "bitfield.hh"


using boost::adaptors::sliced;
using boost::adaptors::map_values;
using std::vector;
using std::cerr;
using std::endl;

#define RLC_AM
#define MAX_SDU_SIZE 9000
#define RLC_AM_HEADER_SIZE 16
#define RLC_AM_RESEG_HEADER_SIZE 32
#define RLC_AM_HEADER_CONTINUE_SIZE 12
#define RLC_AM_LENGTH_FIELD_SIZE 11
#define RLC_AM_STATUS_BEGIN_SIZE 15
#define RLC_AM_STATUS_CONTINUE_NACK_SIZE 12
#define RLC_AM_STATUS_CONTINUE_NACK_SEGMENT_SIZE 42
#define RLC_AM_SEQUENCE_NUMBER_FIELD_SIZE 10
#define RLC_AM_SEGMENT_OFFSET_SIZE 15
#define RLC_AM_WINDOW_SIZE  (1<<(RLC_AM_SEQUENCE_NUMBER_FIELD_SIZE-1))






typedef sequence_number<RLC_AM_SEQUENCE_NUMBER_FIELD_SIZE> rlc_am_sn;


struct timer {
  unsigned time_in_ms;
  bool m_running;
  bool m_ringing;
  timer() : m_running(false), m_ringing(false) {}
  sequence_number<31> expiry_at_time_in_ms;
  void start_timeout(unsigned timeout_in_ms) { m_running = true; expiry_at_time_in_ms = time_in_ms + timeout_in_ms; }
  void stop() { m_running = false; }
  void reset() { m_ringing = false; }
  void update(unsigned time_in_ms) { if(m_running && ((expiry_at_time_in_ms - time_in_ms) >= 0)) { m_ringing = true; stop(); } }
  bool running() { update(time_in_ms); return m_running; }
  bool ringing() { update(time_in_ms); return m_ringing; }
};



static packet
packet_slice(const vector<uint8_t> &pkt, int start = 0, int end = INT_MAX) {
  start = clamp(start, 0, pkt.size());
  end = clamp(end, 0, pkt.size());
  return vector<uint8_t>(&pkt[start], &pkt[end]);
}

static packet
make_mux_am_header(rlc_am_sn next_sequence_number, unsigned fi, const vector<packet> &output_queue) {
  bits header(NULL);
  if(output_queue.empty()) return vector<uint8_t>();
  // Mandatory header
  header += f<1>(1) + f<1>(0) + f<1>(0) + f<2>(fi) + f<1>(output_queue.size() > 1);
  header.push_bits(RLC_AM_SEQUENCE_NUMBER_FIELD_SIZE, next_sequence_number.value);

  for(size_t i = 0; output_queue.size() - i > 1; ++i) {
    header += f<1>(output_queue.size() - i > 2);
    assert(output_queue[i].size() < (1 << RLC_AM_LENGTH_FIELD_SIZE));
    header.push_bits(RLC_AM_LENGTH_FIELD_SIZE, output_queue[i].size());
  }
  // Last sdu continues to end of packet and has implicit length
  // Pad to octet boundary
  bits_pad_to_octet(header);
  return packet(&header.data[0], &header.data[bits_to_bytes(header.write_offset)]);
}

template <class Container, typename T>
static auto
sum(const Container&c, T init) {
  return std::accumulate(c.begin(), c.end(), init);
}

typedef vector<packet> packetq;

struct rx_pdu_incomplete {
  packet data;
  packet known_bytes;
  bool length_is_known;
  rx_pdu_incomplete() : length_is_known(false) {}
  bool is_complete() const { return (length_is_known && sum(known_bytes, 0u) == known_bytes.size()); }
  bool add(size_t segment_offset, bool end_is_included, const packet &segment);
  bool add(const packet &pdu) { assert(!"not implemented"); }
  std::pair<size_t, size_t> next_unknown_range(size_t segment_offset = 0) const;
};

std::pair<size_t, size_t>
rx_pdu_incomplete::next_unknown_range(size_t segment_offset) const {
  size_t start;
  for(start = segment_offset; start < known_bytes.size(); ++start) {
    if (!known_bytes[start])
      break;
  }
  size_t end;
  for(end = start; end < known_bytes.size(); ++end) {
    if (known_bytes[end])
      break;
  }
  if (start == end && length_is_known)
    return std::pair<size_t, size_t>(-1, -1);
  if (end == known_bytes.size())
    end = -1;
  return std::pair<size_t, size_t>(start, end);
}

struct tx_pdu_state {
  packet data;
  size_t retx_count;

};

struct rlc_am_nack {
  rlc_am_sn sn;
  bool reseg;
  std::pair<size_t, size_t> segment;
  rlc_am_nack(rlc_am_sn sn_) : sn(sn_), reseg(false) {}
  rlc_am_nack(rlc_am_sn sn_, size_t start, size_t end) : sn(sn_), reseg(true), segment(start, end) {}
};
struct rlc_am_rx_state;

struct rlc_am_tx_pdu_contents {
  rlc_am_sn sn;
  bool poll;
  bool f0, f1;
  vector<packet> sdus;
  
  /* These two fields are not filled in when decoding */
  packet first_partial_sdu; // if(f0) First full packet, wire part of packet in sdus[0]
  size_t first_partial_sdu_offset; // Part where wire part started in this PDU

  rlc_am_tx_pdu_contents() : sn(0), poll(false), f0(false), f1(false), first_partial_sdu_offset(0), max_size(0), segment_offset(-1), last_segment(false) {}
  void start(std::pair<size_t, packet> &initial_state, rlc_am_sn sn, size_t max_size);
  packet resegment(size_t max_size, std::pair<size_t, size_t> range) const;
  void add_sdu(packet sdu);
  void finalize(std::pair<size_t, packet> &initial_state);
  packet encode() const;
  size_t payload_size() const { return boost::accumulate(sdus, (size_t)0, [](auto i, auto &v) { return i + v.size(); }); }
  size_t header_size(size_t with_extra_packet_count=0) const {
    size_t pieces = sdus.size() + with_extra_packet_count;
    if(pieces == 0)
      return 0;
    return bits_to_bytes(16+16*(segment_offset!=-1)+12*(max(0u,pieces-1)));
  }
  size_t total_size() const { return header_size() + payload_size(); }
  bool room_for_more() {
    if(max_size < 2) return false;
    if(f1) return false;
    if(sdus.size() == 0) return true;
    if(sdus.back().size() > 2047) return false;
    return (payload_size() + 1 + header_size(1) <= max_size);
  }
  void decode(packet &pdu);

 private:
  size_t max_size;
  ssize_t segment_offset;
  bool last_segment;
};



 
struct rlc_am_tx_pdu_state {
  rlc_am_tx_pdu_contents pdu;
  size_t retx_count;
  bool delivered;
  bool retx_requested;
  std::list<std::pair<size_t, size_t> > retx_ranges;
  rlc_am_tx_pdu_state() : retx_count(0), delivered(false), retx_requested(false) {}
  void request_retx() { if (!delivered) retx_requested = true; }
};

struct rlc_am_tx_state {
  unsigned time_in_ms;
  bool status_requested;
  rlc_am_sn status_requested_sn;
  size_t bytes_without_poll;
  size_t pdu_without_poll;
  
  rlc_am_sn last_poll_sn;
  timer timer_poll_retransmit;

  /* Single fragmented SDU in progress */
  std::pair<size_t, packet> sdu_in_progress; // Packet and offset

  /* Transmission window */
  rlc_am_sn next_sequence_number;
  rlc_am_sn lowest_unacknowledged_sequence_number;

  /* Configurable parameters */
  size_t am_max_retx_threshold;
  size_t max_bytes_without_poll;
  size_t max_pdu_without_poll;
  size_t t_PollRetransmit;
  size_t t_StatusProhibit;

  bool can_send_new_packet() const { return next_sequence_number - lowest_unacknowledged_sequence_number >= 0; }

  /* Retransmission */
  std::unordered_map<rlc_am_sn, rlc_am_tx_pdu_state> in_flight;

  /* Delivery indication required and then done! */
  std::queue<packet> delivered_sdus;

  bool have_radio_link_failure() const {
    return boost::accumulate(in_flight | map_values, false,
			     [this](bool fail, const auto &s) {
			       return fail || s.retx_count == 1+am_max_retx_threshold;
			     });
  }

  /* Helper functions to interpret state and parameters listed above */
  bool want_poll() const {
    return (pdu_without_poll >= max_pdu_without_poll ||
	    bytes_without_poll >= max_bytes_without_poll)
      && !have_data_to_send();
  }
  bool have_data_to_send() const {
    return
      (sdu_in_progress.first != 0) ||
      boost::accumulate(in_flight | map_values, false,
			[this](bool fail, const auto &s) {
			  return fail || s.retx_requested;
			});
    //TODO: Really need this for performance      || sdu_peek_buffer();
  }

  /* Boilerplate */

  bool need_retransmission() const {
    return boost::accumulate(in_flight | map_values, false,
			     [this](bool need, const auto &s) {
			       return need || s.retx_requested;
			     });
  }
  rlc_am_tx_state() : status_requested(false) {}
  rlc_am_rx_state *rx_state;

  /* Support same notation as 3GPP LTE RLC specification */
  rlc_am_sn VT_A() { return lowest_unacknowledged_sequence_number; }
  rlc_am_sn VT_MS() { return VT_A() + RLC_AM_WINDOW_SIZE; }
  rlc_am_sn VT_S() { return next_sequence_number; }
  rlc_am_sn POLL_SN() { return last_poll_sn; }
  
};

struct rlc_am_rx_state {
  /* Reordering queue */
  unsigned window_size;
  rlc_am_sn lowest_sequence_number; // == VR(R)
  std::unordered_map<rlc_am_sn, packet> reordering_queue;
  timer timer_reordering;
  rlc_am_sn highest_seen_plus_1; // VR(H)
  rlc_am_sn timer_reordering_trigger_plus_1; // VR(X)

  /* Resegmentation queue */
  std::unordered_map<rlc_am_sn, rx_pdu_incomplete> resegmentation_queue;

  /* Status feedback */
  timer timer_status_prohibit;
  rlc_am_tx_state *tx_state;

  /* Fragmentation state */
  packet partial_packet;

  /* Everything ready */
  std::queue<packet> sdus;

  rlc_am_rx_state() {}
  // This function calculates VR(R) <= SN  < VR(MR)
  bool in_receive_window(rlc_am_sn sn) const { return sn - lowest_sequence_number >= 0; }

  /* Support same notation as 3GPP LTE RLC specification */
  rlc_am_sn VR_R() { return lowest_sequence_number; }
  rlc_am_sn VR_MR() { return VR_R() + RLC_AM_WINDOW_SIZE; }
  rlc_am_sn VR_X() { return timer_reordering_trigger_plus_1; }

  rlc_am_sn VR_MS() { assert(!"not implemented"); } // "highest possible value of the SN which can be indicated by “ACK_SN” when a STATUS PDU needs to be constructed."
  rlc_am_sn VR_H() { return highest_seen_plus_1; }
};

struct rlc_am_state {
  rlc_am_tx_state tx;
  rlc_am_rx_state rx;
  rlc_am_state() {
    tx.rx_state = &rx;
    rx.tx_state = &tx;
    //TODO: Set parameters
  }
  void set_time(unsigned time_in_ms) {
    rx.timer_reordering.update(time_in_ms);
    rx.timer_status_prohibit.update(time_in_ms);
    tx.timer_poll_retransmit.update(time_in_ms);
  }
};



bool
rx_pdu_incomplete::add(size_t segment_offset, bool end_is_included, const packet &segment) {
  size_t size = clamp(segment_offset + segment.size(), data.size(), INT_MAX);
  length_is_known |= end_is_included;
  if(!length_is_known) {
    data.resize(size);
    known_bytes.resize(size);
  }
  unsigned orig = boost::accumulate(known_bytes, 0u);
  boost::copy(segment, data.begin() + segment_offset);
  boost::fill(known_bytes | sliced(segment_offset, segment_offset + segment.size()), true);
  return (orig != boost::accumulate(known_bytes, 0u));
}

void
rlc_am_tx_pdu_contents::start(std::pair<size_t, packet> &initial_state, rlc_am_sn sn, size_t max_size) {
  this->sn = sn;
  this->max_size = max_size;
  if (initial_state.first) {
    f0 = true;
    auto &sdu = initial_state.second;
    first_partial_sdu = sdu;
    first_partial_sdu_offset = initial_state.first;
    sdus.push_back(packet(sdu.begin() + first_partial_sdu_offset, sdu.end()));
    size_t len = sdus.back().size();
    if (max_size - header_size() < len) {
      f1 = true;
      sdus[0].erase(sdus[0].begin() + len, sdus[0].end());
    }
  }
}

packet
rlc_am_tx_pdu_contents::resegment(size_t max_size, std::pair<size_t, size_t> range) const {
  rlc_am_tx_pdu_contents pdu;
  pdu.segment_offset = range.first;
  range.second = min(payload_size(), range.second);
  const size_t segment_size = range.second - range.first;
  size_t skip = range.first;
  bool initialized = false;
  std::pair<size_t, packet> initial_state;
  // Skip to beginning of range
  auto sdup = sdus.begin();
  for(sdup = sdus.begin(); sdup != sdus.end(); ++sdup) {
    const auto &sdu = *sdup;
    if (skip >= sdu.size()) {
      skip -= sdu.size();
      continue;
    }
    if (!initialized) {
      initialized = true;
      initial_state.first = skip;
      initial_state.second = sdu;
      pdu.start(initial_state, sn, max_size);
    } else {
      pdu.add_sdu(sdu);
    }
    if (!pdu.room_for_more())
      break;
    if (pdu.payload_size() >= segment_size) {
      pdu.max_size = pdu.header_size() + segment_size;
    }
  }
  pdu.finalize(initial_state);
  pdu.last_segment = (range.first + pdu.payload_size() == payload_size());
  return pdu.encode();
}

void
rlc_am_tx_pdu_contents::add_sdu(packet sdu) {
  sdus.push_back(sdu);
}

void
rlc_am_tx_pdu_contents::finalize(std::pair<size_t, packet> &initial_state) {
  if(total_size() > max_size) {
    if (sdus.size() == 1 && first_partial_sdu.empty()) {
      first_partial_sdu = sdus[0];
    }
    size_t overflow = total_size() - max_size;
    sdus[0].erase(sdus[0].end() - overflow, sdus[0].end());
    f1 = true;
  }
  size_t end_offset = 0;
  if (f1) {
    end_offset = sdus.back().size();
    if (sdus.size() == 1) {
      end_offset += first_partial_sdu_offset;
      initial_state = make_pair(end_offset, first_partial_sdu);
    } else {
      initial_state = make_pair(end_offset, sdus.back());
    }
  } else {
    initial_state = make_pair((size_t)0, empty_packet);
  }
}


/**************************************************************
 **
 ** rlc_am_mux_packets
 **
 **  Will continue a previous fragmentation (in initial_state)
 **  and pull new packets from the upper layers as they fit.
 **  The last piece is often fragmented to create a maximal PDU.
 **
 **/
static rlc_am_tx_pdu_contents
rlc_am_mux_sdus(std::pair<size_t, packet> &initial_state, rlc_am_sn sn, size_t requested_bytes, std::function<packet(size_t)> pull_sdu) {
  rlc_am_tx_pdu_contents pdu;
  pdu.start(initial_state, sn, requested_bytes);
  while (pdu.room_for_more()) {
    auto sdu = pull_sdu(MAX_SDU_SIZE);
      if(sdu.empty())
	break;
      pdu.add_sdu(sdu);
  }
  pdu.finalize(initial_state);
  return pdu;
}

/**************************************************************
 **
 ** encode RLC AM DATA PDU
 **
 **  Create an RLC AM header which describes the given packets.
 **  We assume the data packets are correctly chosen.
 **  fi means Fragmentation Information an it must be calculated
 **  correctly. The two bits tell whether the first and last are
 **  fragments instead of full packets.
 **
 **/
packet
rlc_am_tx_pdu_contents::encode() const {
  assert(!sdus.empty());
  packet pdu(total_size());
  bits header(pdu.data());

  bool reseg = (segment_offset != -1);
  auto fi = f<1>(f0) + f<1>(f1);
  
  // Mandatory header
  header += f<1>(1) + f<1>(reseg) + f<1>(poll) + fi + f<1>(sdus.size() > 1);
  header.push_bits(RLC_AM_SEQUENCE_NUMBER_FIELD_SIZE, sn.value);

  if (reseg) {
    header += f<1>(last_segment) + f<15>((unsigned)segment_offset);
  }

  for(size_t i = 0; sdus.size() - i > 1; ++i) {
    bool ext = sdus.size() - i > 2;
    header += f<1>(ext);
    assert(sdus[i].size() < (1 << RLC_AM_LENGTH_FIELD_SIZE));
    header.push_bits(RLC_AM_LENGTH_FIELD_SIZE, sdus[i].size());
  }
  // Last sdu continues to end of packet and has implicit length
  // Pad to octet boundary
  bits_pad_to_octet(header);

  // Encode data
  uint8_t *data = pdu.data() + header_size();
  BOOST_FOREACH(auto &sdu, sdus) {
    memcpy(data, sdu.data(), sdu.size());
    data += sdu.size();
  }

  return pdu;
}

// Next packet in sequence must be processed
static void
rlc_am_rx_process_in_sequence(rlc_am_rx_state &rx, packet &pdu_) {
  rlc_am_tx_pdu_contents pdu;
  pdu.decode(pdu_);

  if (pdu.f0 && pdu.f1 && pdu.sdus.size() == 1) {
    boost::copy(pdu.sdus.back(), std::back_inserter(rx.partial_packet));
    return;
  }
  if (pdu.f0) {
    boost::copy(pdu.sdus.back(), std::back_inserter(rx.partial_packet));
    rx.sdus.push(rx.partial_packet);
    rx.partial_packet = packet();
  }
  if (pdu.f0 + pdu.f1 > (int)pdu.sdus.size()) {
    std::for_each(pdu.sdus.begin() + pdu.f0, pdu.sdus.end() - pdu.f1,
		  [&rx](auto &a) { rx.sdus.push(a); });
  }

  if (pdu.f1) {
    rx.partial_packet = pdu.sdus.back();
  }
}

// New data has been added to state. Update state machine and construct PDUs
static void
rlc_am_rx_new_data(rlc_am_rx_state &rx) {
  for(rlc_am_sn sn = rx.lowest_sequence_number; sn != rx.highest_seen_plus_1; sn++) {
    auto p = rx.reordering_queue.find(sn);
    if(p == rx.reordering_queue.end())
      break;

    rlc_am_rx_process_in_sequence(rx, p->second);
    rx.reordering_queue.erase(p);
    ++rx.lowest_sequence_number;
  }
}

static bool rlc_am_is_control(const packet &pdu) { return !(pdu[0] & 0x80); }
static bool rlc_am_is_reseg(const packet &pdu) { return !rlc_am_is_control(pdu) && !!(pdu[0] & 0x40); }
static rlc_am_sn rlc_am_get_sn(const packet &pdu) { return ((pdu[0]<<8)|pdu[1]) & ((1<<rlc_am_sn::width)-1); }
static bool rlc_am_get_poll(const packet &pdu) { return !rlc_am_is_control(pdu) && !!(pdu[0] & 0x20); }


// Parse pdu segment and update rx.resegmentation_queue.
// Add whole pdus to rx.reordering_queue
static bool
rlc_am_parse_resegmented(rlc_am_rx_state &rx, packet &pdu_resegmented) {
  return false;
}

// Parse pdu and add to rx.reordering_queue
static bool
rlc_am_parse_full(rlc_am_rx_state &rx, packet &pdu_full) {
  return true;
}



// Parse RLC status feedback
static rlc_am_sn
rlc_am_parse_status(packet &pdu_status, vector<rlc_am_nack> &nacks) {
  bits header = pdu_status.data();
  header/4;
  rlc_am_sn ack_sn = header/rlc_am_sn::width;
  bool ext = header/1;
  while (ext) {
    rlc_am_sn nack_sn = header/rlc_am_sn::width;
    ext = header/1;
    if (header/1) {
      nacks.push_back(rlc_am_nack(nack_sn,
				  header/RLC_AM_SEGMENT_OFFSET_SIZE,
				  header/RLC_AM_SEGMENT_OFFSET_SIZE));
    }
  }
  return ack_sn;
}

// Handle RLC status feedback
//NOTE! There's an awful corner case: if a segment was NACKed, there
//might be more segments that didn't fit in the status report.
//
//There's two possible interpretations:
//Consistent ACK_SN field: ACK_SN should be the NACK_SN as that wasn't fully
//reported
//However, resending the segment means we need to retransmit anyway so might
//as well increase ACK_SN past the partial NACK.
//
//This case can only appear when the NACK was the last in the STATUS PDU
//In this case the ACK_SN should be the same as the NACKed, as delivering
//the indicated segment is only partial. If on the other hand that was
//the only segment, ACK_SN will be larger than the segment's sequence number
static bool
rlc_am_handle_status(rlc_am_tx_state &tx, packet &pdu_status) {
  vector<rlc_am_nack> nacks;
  rlc_am_sn ack_sn = rlc_am_parse_status(pdu_status, nacks);

  std::set<rlc_am_sn> all_nacks;
  BOOST_FOREACH(auto nack, nacks) { all_nacks.insert(nack.sn); }

  // Now we got the NACKs and ACKs sorted out, update tx state

  // Process all ACKs
  for(auto sn = tx.lowest_unacknowledged_sequence_number; sn != ack_sn; ++sn) {
    // Is it a NACK or ACK?
    if(!has_key(all_nacks, sn)) {
      // It's an ACK
      tx.in_flight[sn].retx_requested = false;
      tx.in_flight[sn].delivered = true;
    }
  }
  // Indicate delivery of all in-sequence ACKed packets
  for(auto sn = tx.lowest_unacknowledged_sequence_number; tx.in_flight[sn].delivered; ++sn) {
    auto &pdu = tx.in_flight[sn].pdu;
    if (pdu.f0 && !(pdu.f1 && pdu.sdus.size() == 1)) {
      tx.delivered_sdus.push(pdu.first_partial_sdu);
    }
    for(int i = pdu.f0; i < (int)pdu.sdus.size() - pdu.f1; ++i) {
      tx.delivered_sdus.push(pdu.sdus[i]);
    }
    tx.in_flight.erase(sn);
    tx.lowest_unacknowledged_sequence_number = sn;
  }
  //Add retransmit requests
  BOOST_FOREACH(rlc_am_sn sn, all_nacks) {
    tx.in_flight[sn].request_retx();
    tx.in_flight[sn].retx_ranges.clear();
  }
  // Add retransmit byte ranges requested
  BOOST_FOREACH(rlc_am_nack nack, nacks) {
    if (nack.reseg)
      tx.in_flight[nack.sn].retx_ranges.push_back(nack.segment);
  }
  // Do we have a reply to our latest POLL?
  if(tx.last_poll_sn < ack_sn || has_key(all_nacks, tx.last_poll_sn)) {
    tx.timer_poll_retransmit.stop();
  }
  return true;
}

static void
rlc_am_rx_new_packet(rlc_am_rx_state &rx, packet &pdu) {
  if (rlc_am_is_control(pdu)) {
    rlc_am_handle_status(*rx.tx_state, pdu);
    return;
  }
  auto sn = rlc_am_get_sn(pdu);
  bool poll = rlc_am_get_poll(pdu);
  if (poll) {
    // We can't reply immediately to status request
    // as we need to be asked to produce data. The tx path will handle it for us
    rx.tx_state->status_requested = true;
  }
  if (!rx.in_receive_window(sn)) {
    // Ignore packet. We might already have it or it's past the window.
    return;
  }
  if (rlc_am_is_reseg(pdu)) {
    if (has_key(rx.reordering_queue, sn)) {
      // We already have all segments
      return;
    }
    //TODO: Process resegmentation queue to reordering_queue
    rx_pdu_incomplete &ipdu = rx.resegmentation_queue[sn];
    ipdu.add(pdu);
    if(ipdu.is_complete()) {
      rx.reordering_queue[sn] = ipdu.data;
      rx.highest_seen_plus_1 = sn + 1;
      rx.resegmentation_queue.erase(sn);
    }
  } else {
    rx.reordering_queue[sn] = pdu;
    rx.highest_seen_plus_1 = sn + 1;
  }
  // Look through reordering queue if we can make progress
  rlc_am_rx_new_data(rx);

  // Any new SDUs are now waiting in rx.sdus
}

static vector<uint8_t>
operator+(const vector<uint8_t> &lhs, const vector<uint8_t> &rhs) {
  vector<uint8_t> dest(lhs.size() + rhs.size());
  dest.insert(dest.end(), lhs.begin(), lhs.end());
  dest.insert(dest.end(), rhs.begin(), rhs.end());
  return dest;
}
static void
operator+=(vector<uint8_t> &lhs, const vector<uint8_t> &rhs) {
  lhs.insert(lhs.end(), rhs.begin(), rhs.end());
}

static void
am_status_continue_nack(bits &header, rlc_am_sn nack_sequence_number, bool ext) {
  // continue_nack
  header += nack_sequence_number;
  header += f<1>(ext) + f<1>(0);
}

static void
am_status_continue_nack_segment(bits &header, rlc_am_sn nack_sequence_number, int segment_offset_start, int segment_offset_end, bool ext) {
  // continue_nack_segment
  header += nack_sequence_number;
  header += f<1>(ext) + f<1>(1);
  header += f<RLC_AM_SEGMENT_OFFSET_SIZE>(segment_offset_start);
  header += f<RLC_AM_SEGMENT_OFFSET_SIZE>(segment_offset_end);
}

static void
am_status_begin(bits &header, rlc_am_sn ack_sequence_number, bool ext) {
  header += f<1>(0) + f<3>(0);
  header += ack_sequence_number;
  header += f<1>(ext);
}

/**************************************************************
 **
 ** rlc_am_make_status_pdu
 **
 **  Here we pack as much ACK/NACK information as fits in the
 **  packet and truncate nicely when space runs out.
 **
 **  We can report either a ACK, NACK for a sequence number
 **  or do a partial ACK/NACK for segmentation
 **/
static packet
rlc_am_make_status_pdu(rlc_am_rx_state &rx, size_t requested_bytes) {
  std::vector<rlc_am_nack> nacks;
  size_t total_size = RLC_AM_STATUS_BEGIN_SIZE;
  assert(requested_bytes >= bits_to_bytes(total_size));

  rlc_am_sn sn;
  for(sn = rx.lowest_sequence_number; sn != rx.highest_seen_plus_1; ++sn) {
    if(has_key(rx.resegmentation_queue, sn)) {
      const auto &pdu_incomplete = rx.resegmentation_queue[sn];
      for(size_t segment_offset = 0; /**/; /**/) {
	if (bits_to_bytes(RLC_AM_STATUS_CONTINUE_NACK_SEGMENT_SIZE - total_size) > requested_bytes) {
	  goto no_more_room;
	}
	auto segment = pdu_incomplete.next_unknown_range(segment_offset);
	nacks.push_back(rlc_am_nack(sn, segment.first, segment.second));
	if (segment.second == (size_t)-1)
	  break;
      }
    } else if(has_key(rx.reordering_queue, sn)) {
      if (bits_to_bytes(RLC_AM_STATUS_CONTINUE_NACK_SIZE - total_size) > requested_bytes) {
	goto no_more_room;
      }
      nacks.push_back(rlc_am_nack(sn));
    }
  }
 no_more_room:
  // Calculate ACK_SN
  for(/**/; sn != rx.highest_seen_plus_1; sn++) {
    if(has_key(rx.resegmentation_queue, sn))
      break;
    if(!has_key(rx.reordering_queue, sn))
      break;
  }
  rlc_am_sn ack_point = sn;
  rx.tx_state->status_requested = false;

  // Now encode the status packet
  packet pdu(bits_to_bytes(total_size));
  bits header(pdu.data());
  am_status_begin(header, ack_point, !nacks.empty());
  for(auto nackp = nacks.begin(); nackp != nacks.end(); ++nackp) {
    if (nackp->reseg)
      am_status_continue_nack_segment(header,
				      nackp->sn,
				      nackp->segment.first, nackp->segment.second,
				      (nackp+1) != nacks.end());
    else
      am_status_continue_nack(header,
			      nackp->sn,
			      (nackp+1) != nacks.end());
  }
  //TODO:am_status_continue_nack_segment(header, nack, start, end);
  bits_pad_to_octet(header);
  return pdu;
}

static packet
rlc_am_mux_retransmit(rlc_am_tx_state &state, size_t requested_bytes) {
  BOOST_FOREACH(auto &pdupair, state.in_flight) {
    auto &pdu = pdupair.second;
    if (pdu.retx_requested) {
      if (pdu.retx_ranges.empty()) {
	if (pdu.pdu.total_size() <= requested_bytes) {
	  // Simple case: retransmit as-is
	  pdu.retx_requested = false;
	  //TODO: Add POLL flag?
	  return pdu.pdu.encode();
	}
	// Resegmentation case
	pdu.retx_ranges.push_back(std::pair<size_t, size_t>(0, -1));
	//FALLTHROUGH
      }
      auto range = pdu.retx_ranges.front();
      pdu.retx_ranges.pop_front();

      //TODO: Add POLL flag?
      return pdu.pdu.resegment(requested_bytes, range);
    }
  }
  //NOTREACHED
  return empty_packet;
}

static packet
rlc_am_mux_transmit(rlc_am_tx_state &state, size_t requested_bytes, std::function<packet(size_t)> pull_sdu) {
  auto pdu = rlc_am_mux_sdus(state.sdu_in_progress,
			     state.next_sequence_number++,
			     requested_bytes,
			     pull_sdu);
  state.pdu_without_poll += 1;
  state.bytes_without_poll += pdu.payload_size();

  if (state.want_poll()) {
    //TODO: Set POLL flag?
    pdu.poll = true;
    state.pdu_without_poll = 0;
    state.bytes_without_poll = 0;
    state.last_poll_sn = pdu.sn;
    state.timer_poll_retransmit.start_timeout(state.t_PollRetransmit);
  }

  if(pdu.total_size() == 0)
    return empty_packet;
  state.in_flight[pdu.sn].pdu = pdu;
  return pdu.encode();
}
/**************************************************************
 **
 ** rlc_am_make_packet
 **
 **  This is called when we receive a request to send data.
 **  We must produce a packet, but no more than requested_bytes.
 **
 **/
static packet
rlc_am_make_packet(rlc_am_tx_state &state, size_t requested_bytes, std::function<packet(size_t)> pull_sdu) {
  // Priority 1: STATUS REPORTS
  if (state.status_requested && !state.rx_state->timer_status_prohibit.running()) {
    state.rx_state->timer_status_prohibit.start_timeout(state.t_StatusProhibit);
    state.status_requested = false;
    return rlc_am_make_status_pdu(*state.rx_state, requested_bytes);
  }
  // Priority 2: RETRANSMISSIONS
  if (state.need_retransmission()) {
    return rlc_am_mux_retransmit(state, requested_bytes);
  }
  // Priority 3: NEW DATA
  // Check if we have space in send window
  if (state.can_send_new_packet()) {
    return rlc_am_mux_transmit(state, requested_bytes, pull_sdu);
  }
  // Nothing to send
  return empty_packet;
}

/**************************************************************
 **
 ** RLC AM decoding
 **
 **/

static rlc_am_sn
rlc_am_get_sequence_number(packet &pdu) {
  bits header = pdu.data();
  assert(header/2 == 2);
  header/1; // skip status request bit
  return header/rlc_am_sn::width;
}

struct decoded_data_pdu {
  rlc_am_sn sn;
  bool poll;
  bool f0, f1;
  ssize_t segment_offset;
  bool last_segment;
  vector<packet> sdus;
  decoded_data_pdu(packet &pdu);
};

void
rlc_am_tx_pdu_contents::decode(packet &pdu) {
  bits header = pdu.data();
  header/1;
  bool reseg = header/1;
  poll = header/1;
  f0 = header/1; f1 = header/1;
  bool ext = header/1;
  sn = header/rlc_am_sn::width;
  if (reseg) {
    last_segment = header/1;
    segment_offset = header/RLC_AM_SEGMENT_OFFSET_SIZE;
  }
  
  while(ext) {
    ext = header/1;
    sdus.push_back(packet(header/RLC_AM_LENGTH_FIELD_SIZE));
  }
  size_t data_offset = bits_to_bytes(header.read_offset);
  size_t implicit_length = pdu.size() - data_offset -
    boost::accumulate(sdus, 0, [](size_t a, auto &b) { return a + b.size(); });
  sdus.push_back(packet(implicit_length));
  uint8_t *p = pdu.data() + data_offset;
  BOOST_FOREACH(auto &sdu, sdus) {
    memcpy(sdu.data(), p, sdu.size());
    p += sdu.size();
  }
}

static void
rlc_am_decode_packet(rlc_am_rx_state &rx, packet &pdu) {
  std::ostream &cdbg = cerr;

  bits header = pdu.data();
  bool is_control = header/1;
  if (is_control) {
    // DATA PDU
    bool reseg = header/1;
    bool poll = header/1;
    if (poll) {
      rx.tx_state->status_requested = poll;
    }
    bool f0 = header/1, f1 = header/1, ext = header/1;
    rlc_am_sn sequence_number = header/rlc_am_sn::width;
    cdbg << "Recv: SN=" << sequence_number.value;

    vector<size_t> lengths;
    while(ext) {
      ext = header/1;
      lengths.push_back(header/RLC_AM_LENGTH_FIELD_SIZE);
    }
    size_t data_offset = bits_to_bytes(header.read_offset);
    size_t implicit_length = pdu.size() - data_offset - std::accumulate(lengths.begin(), lengths.end(), 0);
    lengths.push_back(implicit_length);

    if (!reseg) {
      // NORMAL DATA
      for(size_t i = 0; i < lengths.size(); ++i) {
	cdbg << " " << ((f0 && i == 0)?"+":"") << lengths[i] << ((f1 && i == lengths.size()-1)?"+":"");
      }
      cdbg << endl;
    } else {
      // RESEGMENTED PACKET
      bool end_of_pdu_included = header/1;
      size_t segment_offset = header/RLC_AM_SEGMENT_OFFSET_SIZE;
      cdbg << ":" << segment_offset << "-" << (segment_offset + implicit_length - 1);
      if(!end_of_pdu_included) {
        cdbg << "+";
      }
      cdbg << " " << endl;
    }
  }
  else
  {
    //CONTROL PDU
    unsigned cpt = header/3;
    if(cpt != 0x0) {
      // Undefined at the time of writing. Must be discarded per specification
      cdbg << "Recv: Discarded unknown CONTROL PDU: cpt=" << cpt << endl;
    } else {
      cdbg << "Recv: STATUS";
      rlc_am_sn ack_sn = header/rlc_am_sn::width;
      cdbg << " ACK=" << ack_sn.value;
      bool e1 = header/1;
      bool e2 = false;
      while(e1) {
	rlc_am_sn nack_sn = header/rlc_am_sn::width;
	cdbg << " NACK=" << nack_sn.value;
	e1 = header/1;
	e2 = header/1;
	if (e2) {
	  size_t segment_offset_start = header/15;
	  size_t segment_offset_end = header/15;
	  cdbg << ":" << segment_offset_start << "-" << segment_offset_end;
	}
      }
      cdbg << endl;
    }
  }
}

/********************************************************************/
/********************************************************************/
/********************************************************************/
/********************************************************************/

#include "rlc_parameters.h"

struct rlc_state {
  void *arg;
  rlc_sdu_send_opportunity_fn sdu_send;
  rlc_sdu_received_fn sdu_recv;
  rlc_sdu_delivered_fn sdu_delivered;
  rlc_radio_link_failure_fn rlf;
  rlc_am_state state;
};

RLC *rlc_init() {
  return new RLC();
}

void
rlc_reset(RLC *state) {
}

void
rlc_free(RLC *rlc) {
  free(rlc);
}

int
rlc_set_parameters(RLC *rlc, const char *envz, size_t envz_len) {
  return 0;
}

int
rlc_pdu_send_opportunity(RLC *rlc, unsigned time_in_ms, void *buffer, int size) {
  auto pull = [=](size_t max_size) {
    packet sdu(max_size);
    int size = rlc->sdu_send(rlc->arg, time_in_ms, sdu.data(), max_size);
    sdu.resize((size==-1)?0:size);
    return sdu;
  };
  packet pkt = rlc_am_make_packet(rlc->state.tx, size, pull);
  if (pkt.size()) {
    boost::copy(pkt, (uint8_t *)buffer);
    return pkt.size();
  }
  else
    return -1;
}

void
rlc_pdu_received(RLC *rlc, unsigned time_in_ms, const void *buffer, int size) {
  uint8_t *buf = (uint8_t *)buffer;
  packet pdu(buf, buf + size);
  rlc_am_rx_new_packet(rlc->state.rx, pdu);

  {
    // Deliver any new SDUs
    auto &sdus = rlc->state.rx.sdus;
    while (!sdus.empty()) {
      const auto &sdu = sdus.front();
      rlc->sdu_recv(rlc->arg, time_in_ms, sdu.data(), sdu.size());
      sdus.pop();
    }
  }
  {
    // Indicate delivery of any acknowledged SDUs
    auto &sdus = rlc->state.tx.delivered_sdus;
    while (!sdus.empty()) {
      const auto &sdu = sdus.front();
      rlc->sdu_recv(rlc->arg, time_in_ms, sdu.data(), sdu.size());
      sdus.pop();
    }
  }
}

void
rlc_am_set_callbacks(RLC *rlc, void *arg, rlc_sdu_send_opportunity_fn sdu_send, rlc_sdu_received_fn sdu_recv, rlc_sdu_received_fn sdu_delivered, rlc_radio_link_failure_fn rlf) {
  rlc->arg = arg;
  rlc->sdu_send = sdu_send;
  rlc->sdu_recv = sdu_recv;
  rlc->sdu_delivered = sdu_delivered;
  rlc->rlf = rlf;
}

void
rlc_timer_tick(RLC *state, unsigned time_in_ms) {
  
}
