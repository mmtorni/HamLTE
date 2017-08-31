/*
   Implement user data multiplexing part of 3GPP LTE RLC AM
*/
/*
  TODO: Only 10 bit sequence numbers and 15 bit segment offsets are implemented
  TODO: Only 11 bit length fields are implemented
  TODO: Retransmit and poll timers
  TODO: Sequence numbers sometimes get stuck at 1023 (highest value)
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


static bool rlc_debug = false;



typedef sequence_number<RLC_AM_SEQUENCE_NUMBER_FIELD_SIZE> rlc_am_sn;


struct timer {
  const char *name;
  timer(const char *name_ = NULL) : name(name_), time_in_ms(0), m_running(false), m_ringing(false), timeout_in_ms(0) {}
  void start() {
    if (timeout_in_ms) {
      m_running = true; started_at_time_in_ms = time_in_ms;
      m_ringing = false;
      if (rlc_debug & 2) {
	cerr << "Timer(" << (name?:"") << "=" << timeout_in_ms << "ms) started" << endl;
      }
    } else {
    }
  }
  void stop() {
    m_running = false;
    if (rlc_debug & 2) {
      cerr << "Timer(" << (name?:"") << "=" << timeout_in_ms << "ms) stopped" << endl;
    }
  }
  void reset() {
    m_ringing = false;
    if (rlc_debug & 2) {
      cerr << "Timer(" << (name?:"") << "=" << timeout_in_ms << "ms) reset" << endl;
    }
  }
  void update(unsigned time_in_ms_) {
    time_in_ms = time_in_ms_;
    if(m_running && (time_in_ms - started_at_time_in_ms >= (int)timeout_in_ms)) {
      if (rlc_debug & 2) {

	cerr << "Timer(" << (name?:"") << "=" << timeout_in_ms << "ms) expired" << endl;
      }
      m_ringing = true;
      m_running = false;
    }
  }
  bool running() const { return m_running; }
  bool ringing() const { return m_ringing; }

  void set_timeout(unsigned timeout_in_ms) { this->timeout_in_ms = timeout_in_ms; }
protected:
  sequence_number<31> time_in_ms;
  bool m_running;
  bool m_ringing;
  unsigned timeout_in_ms;
  sequence_number<31> started_at_time_in_ms;
};

struct rx_pdu_incomplete {
  packet data;
  packet known_bytes;
  packet sdu_boundaries;
  bool length_is_known;
  rx_pdu_incomplete() : length_is_known(false) {}
  bool is_complete() const { return (length_is_known && sum(known_bytes, 0u) == known_bytes.size()); }
  bool add(packet &pdu);
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

  rlc_am_tx_pdu_contents() : sn(0), poll(false), f0(false), f1(false), first_partial_sdu_offset(0), segment_offset(-1), max_size(0), last_segment(false) {}
  void start(std::pair<size_t, packet> &initial_state, rlc_am_sn sn, size_t max_size);
  rlc_am_tx_pdu_contents
       resegment(size_t max_size, std::pair<size_t, size_t> range) const;
  void add_sdu(packet sdu);
  void finalize(std::pair<size_t, packet> &initial_state);
  packet encode() const;
  size_t payload_size() const { return boost::accumulate(sdus, (size_t)0, [](auto i, auto &v) { return i + v.size(); }); }
  size_t header_size(size_t with_extra_packet_count=0) const {
    size_t pieces = sdus.size() + with_extra_packet_count;
    if(pieces == 0)
      return 0;
    return bits_to_bytes(16+16*(segment_offset!=-1)+12*(max((size_t)0,pieces-1)));
  }
  size_t total_size() const { return header_size() + payload_size(); }
  bool room_for_more() {
    if(f1) return false;
    if(!sdus.empty() && sdus.back().size() > 2047) return false;
    return (payload_size() + 1 + header_size(1) <= max_size);
  }
  void decode(packet &pdu);

  ssize_t segment_offset;
  size_t max_size;
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
  timer t_PollRetransmit = ("t-PollRetransmit");

  /* Single fragmented SDU in progress */
  std::pair<size_t, packet> sdu_in_progress; // Packet and offset

  /* Transmission window */
  rlc_am_sn next_sequence_number;
  rlc_am_sn lowest_unacknowledged_sequence_number;

  /* Configurable parameters */
  size_t am_max_retx_threshold;
  size_t max_bytes_without_poll;
  size_t max_pdu_without_poll;
  size_t am_window_size;

  bool is_window_full() const { return next_sequence_number - lowest_unacknowledged_sequence_number >= (int)am_window_size; }

  /* Retransmission */
  std::map<rlc_am_sn, rlc_am_tx_pdu_state> in_flight;

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
    if (is_window_full() || t_PollRetransmit.ringing())
      return true;
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
  void poll_sent(rlc_am_sn sn) {
    pdu_without_poll = 0;
    bytes_without_poll = 0;
    last_poll_sn = sn;
    t_PollRetransmit.reset();
    t_PollRetransmit.start();
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
  rlc_am_sn VT_MS() { return VT_A() + am_window_size; }
  rlc_am_sn VT_S() { return next_sequence_number; }
  rlc_am_sn POLL_SN() { return last_poll_sn; }

};

struct rlc_am_rx_state {
  /* Reordering queue */
  unsigned am_window_size;
  rlc_am_sn lowest_sequence_number; // == VR(R)
  std::map<rlc_am_sn, packet> reordering_queue;
  timer t_Reordering = "t-Reordering"; // Configurable
  rlc_am_sn highest_seen_plus_1; // VR(H)
  rlc_am_sn timer_reordering_trigger_plus_1; // VR(X)

  /* ation queue */
  std::unordered_map<rlc_am_sn, rx_pdu_incomplete> resegmentation_queue;

  /* Status feedback */
  rlc_am_tx_state *tx_state;
  timer t_StatusProhibit = "t-StatusProhibit";    // Configurable

  /* Fragmentation state */
  packet partial_packet;

  /* Everything ready */
  std::queue<packet> sdus;


  rlc_am_rx_state() {}
  // This function calculates VR(R) <= SN  < VR(MR)
  bool in_receive_window(rlc_am_sn sn) const { return sn - lowest_sequence_number >= 0; }

  /* Support same notation as 3GPP LTE RLC specification */
  rlc_am_sn VR_R() { return lowest_sequence_number; }
  rlc_am_sn VR_MR() { return VR_R() + am_window_size; }
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
    rx.t_Reordering.update(time_in_ms);
    rx.t_StatusProhibit.update(time_in_ms);
    tx.t_PollRetransmit.update(time_in_ms);
  }
};



bool
rx_pdu_incomplete::add(packet &pdu_) {
  //TODO: Piece together SDU boundaries
  rlc_am_tx_pdu_contents pdu;
  unsigned known_before = boost::accumulate(known_bytes, 0u);
  pdu.decode(pdu_);
  size_t sostart = pdu.segment_offset;
  size_t soend = pdu.segment_offset + pdu.payload_size();
  size_t size = clamp(soend, data.size(), INT_MAX);
  data.resize(size);
  known_bytes.resize(size);
  sdu_boundaries.resize(size + 1);

  if (pdu.last_segment)
    length_is_known = true;

  // Mark known bytes
  for(size_t i = sostart; i != soend; ++i) {
    known_bytes[i] = true;
  }
  // Mark known boundaries
  size_t ofs = sostart + pdu.f0 * pdu.sdus[0].size();
  for(size_t i = pdu.f0; i != pdu.sdus.size(); ++i) {
    sdu_boundaries[ofs] = true;
    ofs += pdu.sdus[i].size();
  }
  if (pdu.f1 == false) {
    sdu_boundaries[ofs] = true;
  }
  return known_before != boost::accumulate(known_bytes, 0u);
}

void
rlc_am_tx_pdu_contents::start(std::pair<size_t, packet> &initial_state, rlc_am_sn sn, size_t max_size) {
  this->sn = sn;
  this->max_size = max_size;
  if (initial_state.first) {
    if (!room_for_more())
      return;
    f0 = true;
    auto &sdu = initial_state.second;
    first_partial_sdu = sdu;
    first_partial_sdu_offset = initial_state.first;
    sdus.push_back(packet(sdu.begin() + first_partial_sdu_offset, sdu.end()));
    size_t len = sdus.back().size();
    if (max_size - header_size() < len) {
      f1 = true;
    }
  }
}

rlc_am_tx_pdu_contents
rlc_am_tx_pdu_contents::resegment(size_t max_size, std::pair<size_t, size_t> range) const {
  rlc_am_tx_pdu_contents pdu;
  pdu.segment_offset = range.first; // Set segment_offset the first thing
  pdu.max_size = max_size;
  if(!pdu.room_for_more())
    return pdu;

  range.second = min(payload_size(), range.second);
  const size_t segment_size = range.second - range.first;
  size_t skip = range.first;
  bool initialized = false;
  // Skip to beginning of range
  BOOST_FOREACH(const auto &sdu, sdus) {
    if (skip >= sdu.size()) {
      skip -= sdu.size();
      continue;
    }
    if (!pdu.room_for_more())
      break;

    if (!initialized) {
      initialized = true;
      if (skip) {
	std::pair<size_t, packet> initial_state;
	initial_state.first = skip;
	initial_state.second = sdu;
	pdu.start(initial_state, sn, max_size);
      } else {
	std::pair<size_t, packet> initial_state;
	initial_state.first = 0;
	initial_state.second = empty_packet;
	pdu.start(initial_state, sn, max_size);
	pdu.add_sdu(sdu);
      }
    } else {
      pdu.add_sdu(sdu);
    }
    if (pdu.payload_size() >= segment_size) {
      break;
    }
  }
  std::pair<size_t, packet> initial_state;
  pdu.finalize(initial_state);
  pdu.last_segment = (range.first + pdu.payload_size() == payload_size());
  return pdu;
}

void
rlc_am_tx_pdu_contents::add_sdu(packet sdu) {
  sdus.push_back(sdu);
}

void
rlc_am_tx_pdu_contents::finalize(std::pair<size_t, packet> &initial_state) {
  if (sdus.size() == 0)
    return;
  int overflow = max((int)total_size() - (int)max_size, 0);
  f1 = overflow > 0;
  auto &last_sdu = sdus.back();
  assert(overflow < (int)last_sdu.size());
  // Special case for single fragment
  if (sdus.size() == 1 && f0 && f1) {
    initial_state.first += last_sdu.size() - overflow;
    last_sdu = packet(last_sdu.begin(), last_sdu.end() - overflow);
    return;
  }
  if (f1) {
    initial_state = make_pair(last_sdu.size() - overflow, last_sdu);
    last_sdu = packet(last_sdu.begin(), last_sdu.end() - overflow);
  } else {
    initial_state = make_pair(0, empty_packet);
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
  pdu.max_size = requested_bytes;
  if (!pdu.room_for_more()) {
    return pdu;
  }
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
  assert(pdu.payload_size() > 0); // For debugging, might allow 0-length SDUs later

  if (pdu.f0 && pdu.f1 && pdu.sdus.size() == 1) {
    boost::copy(pdu.sdus.front(), std::back_inserter(rx.partial_packet));
    return;
  }
  if (pdu.f0) {
    boost::copy(pdu.sdus.front(), std::back_inserter(rx.partial_packet));
    rx.sdus.push(rx.partial_packet);
    rx.partial_packet = empty_packet;
  }
  for(int i = pdu.f0; i < (int)pdu.sdus.size() - pdu.f1; ++i) {
    rx.sdus.push(pdu.sdus[i]);
  }

  if (pdu.f1) {
    rx.partial_packet = pdu.sdus.back();
  }
}

// New data has been added to state. Update state machine and construct SDUs
static void
rlc_am_rx_new_data(rlc_am_rx_state &rx) {
  for(rlc_am_sn sn = rx.lowest_sequence_number; sn != rx.highest_seen_plus_1; ++sn) {
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
      size_t start = header/RLC_AM_SEGMENT_OFFSET_SIZE;
      size_t end = header/RLC_AM_SEGMENT_OFFSET_SIZE;
      nacks.push_back(rlc_am_nack(nack_sn, start, end));
    } else {
      nacks.push_back(nack_sn);
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

  // Figure out which PDUs in transit were ACKed
  std::set<rlc_am_sn> acks;
  for(rlc_am_sn sn = tx.lowest_unacknowledged_sequence_number; sn < ack_sn; ++sn) {
    if (!has_key(all_nacks, sn)
	&& has_key(tx.in_flight, sn)
	&& !tx.in_flight[sn].delivered)
      acks.insert(sn);
  }
  // Now we got the NACKs and ACKs sorted out, update delivery status
  std::set<rlc_am_sn> just_delivered;
  BOOST_FOREACH(auto &sn, acks) {
    tx.in_flight[sn].retx_requested = false;
    tx.in_flight[sn].delivered = true;
    just_delivered.insert(sn);
  }
  // Indicate delivery of all in-sequence ACKed packets
  // This involves figuring out which SDUs were completely
  // transferred by that PDU.
  rlc_am_sn sn;
  for(sn = tx.lowest_unacknowledged_sequence_number; sn < ack_sn; ++sn) {
    if (!has_key(tx.in_flight, sn)) break;
    if (!tx.in_flight[sn].delivered)  break;
    auto &pdu = tx.in_flight[sn].pdu;
    if (pdu.f0 && !(pdu.f1 && pdu.sdus.size() == 1)) {
      tx.delivered_sdus.push(pdu.first_partial_sdu);
    }
    for(int i = pdu.f0; i < (int)pdu.sdus.size() - pdu.f1; ++i) {
      tx.delivered_sdus.push(pdu.sdus[i]);
    }
    tx.in_flight.erase(sn);
  }
  // Update lower edge of tx window
  tx.lowest_unacknowledged_sequence_number = sn;
  //Add retransmit requests
  BOOST_FOREACH(rlc_am_sn sn, all_nacks) {
    if (has_key(tx.in_flight, sn)) {
      tx.in_flight[sn].request_retx();
      tx.in_flight[sn].retx_ranges.clear();
    }
  }
  // Add retransmit byte ranges requested
  BOOST_FOREACH(rlc_am_nack nack, nacks) {
    if (nack.reseg && has_key(tx.in_flight, nack.sn))
      tx.in_flight[nack.sn].retx_ranges.push_back(nack.segment);
  }
  // Print debugging output
  if (rlc_debug) {
    cerr << "\e[1mSTATUS RECEIVED: (raw ACK_SN=" << ack_sn.value << ") ACK=";
    BOOST_FOREACH(auto sn, acks) { cerr << sn.value << " "; }
    cerr << "NACK=\e[32m";
    BOOST_FOREACH(auto nack, nacks) {
      assert(nack.segment.first < 30000);
      cerr << nack.sn.value;
      if (nack.reseg) {
	cerr << ":" << nack.segment.first << "-";
	if (nack.segment.second != 32767) { //TODO: change this once SO==16bits
	  cerr << nack.segment.second;
	}
      }
      cerr << " ";
    }
    cerr << "\e[0m" << endl;
  }
  // Do we have a reply to our latest POLL?
  if(tx.last_poll_sn < ack_sn || has_key(all_nacks, tx.last_poll_sn)) {
    tx.t_PollRetransmit.stop();
  }
  // Did we pass our t-Reordering water mark
  auto &rx = *tx.rx_state;
  if(rx.VR_X() == rx.VR_R() || (!rx.in_receive_window(rx.VR_X()) && rx.VR_X() != rx.VR_MR())) {
    tx.rx_state->t_Reordering.stop();
    tx.rx_state->t_Reordering.reset();
  }
  // Do we need to start t-Reordering?
  if(rx.VR_H() > rx.VR_R()) {
    tx.rx_state->t_Reordering.stop();
    tx.rx_state->t_Reordering.reset();
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
      rx.resegmentation_queue.erase(sn);
    }
  } else {
    rx.reordering_queue[sn] = pdu;
  }
  if (rx.highest_seen_plus_1 < sn + 1)
    rx.highest_seen_plus_1 = sn + 1;

  if (rx.timer_reordering_trigger_plus_1 == rx.lowest_sequence_number ||
      (!rx.in_receive_window(rx.timer_reordering_trigger_plus_1) &&
       rx.timer_reordering_trigger_plus_1 != rx.VR_MR())) {
    rx.t_Reordering.stop();
    rx.t_Reordering.reset();
  }
  if (!rx.t_Reordering.running()) {
    if (rx.lowest_sequence_number < rx.highest_seen_plus_1) {
      rx.t_Reordering.start();
      rx.timer_reordering_trigger_plus_1 = rx.highest_seen_plus_1;
    }
  }

  // Look through reordering queue if we can make progress
  rlc_am_rx_new_data(rx);

  // Any new SDUs are now waiting in rx.sdus
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
  assert((unsigned)segment_offset_start < 30000u);

  header += f<RLC_AM_SEGMENT_OFFSET_SIZE>(segment_offset_start);
  header += f<RLC_AM_SEGMENT_OFFSET_SIZE>(segment_offset_end);
}

static void
am_status_begin(bits &header, rlc_am_sn ack_sequence_number, bool ext) {
  //assert(ack_sequence_number != 1023);
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
	if (bits_to_bytes(RLC_AM_STATUS_CONTINUE_NACK_SEGMENT_SIZE + total_size) > requested_bytes) {
	  goto no_more_room;
	}
	total_size += RLC_AM_STATUS_CONTINUE_NACK_SEGMENT_SIZE;
	auto segment = pdu_incomplete.next_unknown_range(segment_offset);
	if (segment.first == (size_t)-1)
	  break;

	nacks.push_back(rlc_am_nack(sn, segment.first, segment.second));
	if (segment.second == (size_t)-1)
	  break;
	segment_offset = segment.second;
      }
    } else if(!has_key(rx.reordering_queue, sn)) {
      if (bits_to_bytes(RLC_AM_STATUS_CONTINUE_NACK_SIZE + total_size) > requested_bytes) {
	goto no_more_room;
      }
      total_size += RLC_AM_STATUS_CONTINUE_NACK_SIZE;
      nacks.push_back(sn);
    }
  }
 no_more_room:
  // Calculate ACK_SN
  for(/**/; sn != rx.highest_seen_plus_1; ++sn) {
    if(has_key(rx.resegmentation_queue, sn))
      break;
    if(!has_key(rx.reordering_queue, sn))
      break;
  }
  rlc_am_sn ack_point = sn;
  assert(!(ack_point < rx.lowest_sequence_number));
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
  BOOST_FOREACH(uint8_t byte, pdu) {
    if (byte == 0xff) {
      cerr << "byte == 0xff" << endl;
    }
  }
  return pdu;
}

static packet
rlc_am_mux_retransmit(rlc_am_tx_state &state, size_t requested_bytes) {
  // Start
  BOOST_FOREACH(auto &pdupair, state.in_flight) {
    auto &pdu = pdupair.second;
    if (pdu.retx_requested) {
      if (pdu.retx_ranges.empty()) {
	if (pdu.pdu.total_size() <= requested_bytes) {
	  // Simple case: retransmit as-is
	  pdu.retx_requested = false;
	  //TODO: Add POLL flag?
	  if (state.want_poll()) {
	    pdu.pdu.poll = true;
	    state.poll_sent(pdu.pdu.sn);
	  }
	  return pdu.pdu.encode();
	}
	// Resegmentation case
	pdu.retx_ranges.push_back(std::pair<size_t, size_t>(0, -1));
	//FALLTHROUGH
      }
      auto range = pdu.retx_ranges.front();
      pdu.retx_ranges.pop_front();

      //TODO: Add POLL flag?
      auto rpdu = pdu.pdu.resegment(requested_bytes, range);
      if (rpdu.total_size() == 0) {
	// requested_bytes is too small for a segmented PDU
	return empty_packet;
      }
      if (rpdu.payload_size() < (range.second - range.first)) {
	pdu.retx_ranges.push_front(std::make_pair(range.first + rpdu.payload_size(), range.second));
      }
      if (pdu.retx_ranges.empty()) {
	pdu.retx_requested = false;
      }
      if (state.want_poll()) {
	rpdu.poll = true;
	state.poll_sent(rpdu.sn);
      }
      return rpdu.encode();
    }
  }
  //NOTREACHED
  return empty_packet;
}

static packet
rlc_am_mux_transmit(rlc_am_tx_state &state, size_t requested_bytes, std::function<packet(size_t)> pull_sdu) {
  auto pdu = rlc_am_mux_sdus(state.sdu_in_progress,
			     state.next_sequence_number,
			     requested_bytes,
			     pull_sdu);
  if(pdu.total_size() == 0)
    return empty_packet;

  assert(pdu.payload_size() > 0); // For debugging. Might allow 0-length SDUs later

  ++state.next_sequence_number;

  state.pdu_without_poll += 1;
  state.bytes_without_poll += pdu.payload_size();

  if (state.want_poll()) {
    //TODO: Set POLL flag?
    state.poll_sent(pdu.sn);
    pdu.poll = true;
  }

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
  //TODO: Do housekeeping; Update rx reordering timer
  
  // Priority 1: STATUS REPORTS
  auto &rx = *state.rx_state;
  if (!rx.t_StatusProhibit.running()) {
    if (state.status_requested || rx.t_Reordering.ringing()) {
      rx.t_StatusProhibit.start();
      if (rx.t_Reordering.ringing())
	rx.t_Reordering.start();
      state.status_requested = false;
      return rlc_am_make_status_pdu(rx, requested_bytes);
    }
  }
  // Priority 2: RETRANSMISSIONS
  if (state.need_retransmission()) {
    return rlc_am_mux_retransmit(state, requested_bytes);
  }
  // Priority 3: NEW DATA
  // Check if we have space in send window
  if (!state.is_window_full()) {
    return rlc_am_mux_transmit(state, requested_bytes, pull_sdu);
  } else {
    // Window is full... Either wait for ACK or retransmit a packet with POLL
    if (state.t_PollRetransmit.ringing()) {
      //TODO: FIND A PACKET FOR POLL
      BOOST_REVERSE_FOREACH(auto &pdu, state.in_flight | map_values) {
	//TODO: Probably should find a packet which doesn't require resegmentation
	if(!pdu.delivered) {
	  pdu.retx_requested = true;
	  return rlc_am_mux_retransmit(state, requested_bytes);
	}
      }
      assert(state.lowest_unacknowledged_sequence_number == state.next_sequence_number);
    }
  }
  // Nothing to send
  return empty_packet;
}

/**************************************************************
 **
 ** RLC AM decoding
 **
 **/

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
    boost::accumulate(sdus, 0, [](int a, auto &b) { return a + (int)b.size(); });
  sdus.push_back(packet(implicit_length));
  uint8_t *p = pdu.data() + data_offset;
  BOOST_FOREACH(auto &sdu, sdus) {
    memcpy(sdu.data(), p, sdu.size());
    p += sdu.size();
  }
}

/********************************************************************/
/********************************************************************/
/********************************************************************/
/********************************************************************/

#include <argz.h>
#include <envz.h>
#include <cstdlib>
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
static const char *default_parameters = ""
"rlc/mode=AM rlc/debug=0 maxRetxThreshold=4 pollPDU=8 pollByte=1024 t-Reordering=35"
" t-StatusProhibit=5 t-PollRetransmit=5";

#define ENVZ_INT(name) atoi(envz_get(envz, envz_len, name))
#define ENVZ_SET_INT(name, i) (sprintf(intbuf, "%d", (i)), envz_add(&envz, &envz_len, name, intbuf))

int
rlc_set_parameters(RLC *rlc, const char *envz_more, size_t envz_more_len) {
  char *envz = NULL; size_t envz_len = 0;
  char intbuf[32];
  argz_create_sep(default_parameters, ' ', &envz, &envz_len);
  ENVZ_SET_INT("amWindowSize", RLC_AM_WINDOW_SIZE);
  envz_merge(&envz, &envz_len, envz_more, envz_more_len, true);
  rlc->state.tx.max_pdu_without_poll = ENVZ_INT("pollPDU");
  rlc->state.tx.max_bytes_without_poll = ENVZ_INT("pollByte");
  rlc->state.tx.am_max_retx_threshold = ENVZ_INT("maxRetxThreshold");
  rlc->state.tx.am_window_size = ENVZ_INT("amWindowSize");
  rlc->state.rx.am_window_size = ENVZ_INT("amWindowSize");
  rlc->state.tx.t_PollRetransmit.set_timeout(ENVZ_INT("t-PollRetransmit"));
  rlc->state.rx.t_StatusProhibit.set_timeout(ENVZ_INT("t-StatusProhibit"));

  rlc->state.rx.t_Reordering.set_timeout(ENVZ_INT("t-Reordering"));
  rlc_debug = ENVZ_INT("rlc/debug");
  return 0;
}

int
rlc_pdu_send_opportunity(RLC *rlc, unsigned time_in_ms, void *buffer, int size) {
  rlc->state.set_time(time_in_ms);
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
  rlc->state.set_time(time_in_ms);
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
      rlc->sdu_delivered(rlc->arg, time_in_ms, sdu.data(), sdu.size());
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
rlc_timer_tick(RLC *rlc, unsigned time_in_ms) {
  rlc->state.set_time(time_in_ms);
}
