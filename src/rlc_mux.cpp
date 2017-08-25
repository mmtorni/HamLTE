/*
   Implement user data multiplexing part of 3GPP LTE RLC AM
*/
#include <cstdio>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <climits>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

using std::vector;
using std::cerr;
using std::endl;

#define MAX_SDU_SIZE 9000 /* Arbitrary size */
#define MAX_PDU_SIZE 9000 /* Arbitrary size */
#define RLC_AM

#ifdef RLC_AM
#define HEADER_SIZE_VARIABLES 
#define header_bits_with_new_packet_of_length(length) 12
#define header_bits_for_first_packet
#endif

int verbose;
int sdu_line_output;
int sduout_fd = STDOUT_FILENO;
int sduin_fd = STDIN_FILENO;
int pdustream_fd = 4;
int send_opportunity_fd = 3;
FILE *send_opportunity_stream;
FILE *sdu_line_input_stream;

void cpp_send(int fd, vector<uint8_t> pkt) {
  int retval = send(fd, pkt.data(), pkt.size(), 0);
  if (retval == -1) {
    perror("cpp_send:send");
    exit(1);
  }
}
void cpp_write(int fd, vector<uint8_t> pkt) {
  int retval = write(fd, pkt.data(), pkt.size());
  if (retval == -1) {
    perror("cpp_write:write");
    exit(1);
  } else if ((size_t)retval != pkt.size()) {
    perror("cpp_write:write returned short write %d");
    exit(1);
  }
}
vector<uint8_t> cpp_recv_dontwait(int fd, size_t max_size) {
  auto pkt = vector<uint8_t>(max_size);
  int retval = recv(fd, pkt.data(), max_size, MSG_DONTWAIT);
  if(retval == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
    retval = 0;
  else if (retval == -1) {
    perror("recv");
  }
  pkt.erase(pkt.begin() + retval, pkt.end());
  return pkt;
}

vector<uint8_t> cpp_read_nonblock(int fd, size_t max_size) {
  auto pkt = vector<uint8_t>(max_size);
  fcntl(fd, F_SETFL, O_NONBLOCK);
  int retval = read(fd, pkt.data(), max_size);
  if(retval == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    retval = 0;
  } else if (retval == -1) {
    perror("recv");
  }
  pkt.erase(pkt.begin() + retval, pkt.end());
  return pkt;
}


#ifdef SDU_INPUT_DUMMY_DATA
vector<uint8_t>
sdu_recv(size_t max_size) {
  static const char *dummy_data[] = { "ax", "________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________"
				      "________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________",
    "A", "Bcd", "Defgh", "Ijklm", NULL };
  static int i = 0;
  if(dummy_data[i] == NULL) return vector<uint8_t>();
  cerr << "Read dummy data: " << dummy_data[i] << endl;
  int old_i = i++;
  return vector<uint8_t>(dummy_data[old_i], dummy_data[old_i] + strlen(dummy_data[old_i]));
}
#else
vector<uint8_t> sdu_recv(size_t max_size) {
  if(sdu_line_input_stream) {
    char *line = NULL;
    size_t n = 0;
    int retval = getline(&line, &n, sdu_line_input_stream);
    
    if(retval == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return vector<uint8_t>();
    } else if(feof(sdu_line_input_stream)) {
      cerr << "End of SDU input stream. Exiting." << endl;
      exit(0);
      }
    else if(errno == -1) {
      perror("sdu_recv:getline");
      exit(1);
    }
    if(line == NULL) {
      cerr << "End of SDU input stream. Exiting." << endl;
      exit(0);
    }
    vector<uint8_t> sdu(line, line + strlen(line));
    free(line); line = NULL;
    if(sdu.back() == '\n') sdu.pop_back();
    if(sdu.back() == '\r') sdu.pop_back();
    return sdu;
  }
  else
  {
    return cpp_read_nonblock(sduin_fd, 2048);
  }
}
#endif
vector<uint8_t> pdu_recv() { return cpp_recv_dontwait(pdustream_fd, MAX_PDU_SIZE); }
void sdu_send(vector<uint8_t> pkt) {
  cpp_write(sduout_fd, pkt);
  if(sdu_line_output)
    cpp_write(sduout_fd, vector<uint8_t> {'\n'});
}
void pdu_send(vector<uint8_t> pkt) { cpp_write(pdustream_fd, pkt); }

size_t get_requested_bytes() {
  int result;
  int retval = fscanf(send_opportunity_stream, "%d", &result);
  if(retval == 0 || retval == EOF) {
    fprintf(stderr, "Zero length packet received on fd %d, exiting.\n", send_opportunity_fd);
    exit(0);
  }
  /*
  char buf[6];
    recv(send_opportunity_fd, buf, sizeof(buf));
  if(retval == -1) {
    perror("get_requested_bytes:recv");
    exit(1);
  } else if (retval == 0) {
    fprintf(stderr, "Zero length packet received on fd %d, exiting.\n", send_opportunity_fd);
    exit(0);
  }
  auto result = atoi(buf);
  */
  //cerr << "Request of " << result << " bytes" << endl;
  return result;
}


struct bits {
  size_t length_in_bits;
  uint8_t data[2048 + sizeof(long)]; // No packet may exceed this
  bits();
};
void bits_clear(struct bits &self) { memset(&self, 0, sizeof(self)); }

bits::bits() { bits_clear(*this); }

void bits_add_bit(struct bits &self, bool bit) {
  self.data[self.length_in_bits/8] |= (!!bit)<<((8-self.length_in_bits%8)-1);
  ++self.length_in_bits;
}
void bits_add_int(struct bits &self, size_t n_bits, int value) {
  unsigned i = (unsigned)value;
  while(n_bits) {
    bits_add_bit(self, (i >> --n_bits)&1);
  }
}

static int clamp(int number, int low, int high) {
  assert(low<=high);
  if(number < low) return low;
  if(number > high) return high;
  return number;
}

vector<uint8_t> packet_slice(const vector<uint8_t> &pkt, int start = 0, int end = INT_MAX) {
  start = clamp(start, 0, pkt.size());
  end = clamp(end, 0, pkt.size());
  return vector<uint8_t>(&pkt[start], &pkt[end]);
}

const vector<uint8_t> empty_packet;

static int bits_to_bytes(int bits) { return (bits + 7) / 8; }

size_t total_queue_size(vector<vector<uint8_t> > output_queue) {
  size_t result = 0;
  for(auto pkt = output_queue.begin(); pkt != output_queue.end(); ++pkt)
    result += pkt->size();
  return result;
}

#define max_packet_length (requested_bytes)
#define max_remaining_payload clamp(max_packet_length - packet_length_with(empty_packet), 0, INT_MAX)
#define packet_count (output_queue.size())
#define total_output_queue_length total_queue_size(output_queue)
#define are_in_middle !state.empty()
#define fragment_info (2*!initial_state.empty() + are_in_middle)

#define queue_possible_partial_packet()			  \
  ({							  \
    size_t _nbytes = clamp(max_remaining_payload, 0, state.size());	\
    if (_nbytes) { \
      output_queue.push_back(packet_slice(state, 0, _nbytes)); \
      state.erase(state.begin(), state.begin() + _nbytes);  \
    } \
  })

#define RLC_AM_HEADER_SIZE 16
#define RLC_AM_HEADER_CONTINUE_SIZE 12
#define RLC_AM_LENGTH_FIELD_SIZE 11

#define packet_length_with(pkt)						\
  (bits_to_bytes(RLC_AM_HEADER_SIZE + packet_count * RLC_AM_HEADER_CONTINUE_SIZE) \
   + total_output_queue_length \
   + pkt.size())
				 
struct rlc_am_mux_packets_result {
  vector<vector<uint8_t> > output_queue;
  vector<uint8_t> state;
  int fi;
};
  
rlc_am_mux_packets_result
rlc_am_mux_packets(const vector<uint8_t> initial_state, size_t requested_bytes) {
  vector<vector<uint8_t> > output_queue;
  vector<uint8_t> state(initial_state);

  if(are_in_middle) {
    queue_possible_partial_packet();
  }
  while(max_remaining_payload) {
    // Special case: only implicit length field packets can exceed >11 bits
    // so we need to end with this packet if the length is too high
    if(!output_queue.empty() && output_queue.back().size() >= (1<<RLC_AM_LENGTH_FIELD_SIZE))
      break;
    state = sdu_recv(max_remaining_payload);
    if(!are_in_middle) // Nothing to send as of now
      break;
    queue_possible_partial_packet();
  }
  return (rlc_am_mux_packets_result){output_queue, state, fragment_info};
}

struct rlc_am_tx_state {
  unsigned next_sequence_number;
  vector<uint8_t> in_progress;
  rlc_am_tx_state() { memset(this, 0, sizeof(*this)); }
};

vector<uint8_t>
make_mux_am_header(unsigned next_sequence_number, unsigned fi, vector<vector<uint8_t> > output_queue) {
  bits header;
  if(output_queue.empty()) return vector<uint8_t>();
  // Mandatory header
  bits_add_int(header, 1, 1);
  bits_add_int(header, 1, 0);
  bits_add_int(header, 1, 0);
  bits_add_int(header, 2, fi);
  bits_add_int(header, 1, output_queue.size() > 1);
  bits_add_int(header, 10, next_sequence_number);
  size_t i = 0;
  while(output_queue.size() - i > 1) {
    bits_add_int(header, 1, output_queue.size() - i > 2);
    assert(output_queue[i].size() < (1 << RLC_AM_LENGTH_FIELD_SIZE));
    bits_add_int(header, RLC_AM_LENGTH_FIELD_SIZE, output_queue[i].size());
    ++i;
  }
  // Last sdu continues to end of packet and has implicit length
  // Pad to octet boundary
  bits_add_int(header, (8 - header.length_in_bits % 8) % 8, 0);
  return vector<uint8_t>(&header.data[0], &header.data[bits_to_bytes(header.length_in_bits)]);
}

vector<uint8_t>
rlc_am_make_packet(rlc_am_tx_state &state, size_t requested_bytes) {
  auto result = rlc_am_mux_packets(state.in_progress, requested_bytes);
  if(result.output_queue.empty()) {
    // Nothing to send
    return empty_packet;
  }
  state.in_progress.swap(result.state);
  auto pdu = make_mux_am_header(state.next_sequence_number++, result.fi, result.output_queue);
  for(auto sdu = result.output_queue.begin(); sdu != result.output_queue.end(); ++sdu) {
    pdu.insert(pdu.end(), sdu->begin(), sdu->end());
  }
  return pdu;
}
  
void test_send_only() {
  rlc_am_tx_state state;
  for(;;) {
    size_t requested_bytes = get_requested_bytes();
    if(requested_bytes == 0) {
      pdu_send(empty_packet);
    } else {
      auto pdu = rlc_am_make_packet(state, requested_bytes);
      pdu_send(pdu);
    
    }
  }
}

static char short_options[] = "vhs";
static struct option long_options[] = {
  {"sdu-line-input-file",   required_argument, 0, 1 },
  {"sdu-stdio",             no_argument,       0, 2 },
  {"send",                  no_argument,       0, 's'},
  {"verbose",               no_argument,       &verbose, 'v' },
  {"help",                  no_argument,       0, 'h' },
  {0,                       0,                 0, 0 }
};

void usage() {
  cerr <<
    "Usage: rlc_mux <send | receive>\n"
    "Perform 3GPP LTE RLC Acknowledged mode multiplexing, fragmentation\n"
    "and reassembly in case of no packet loss or reordering\n"
    "Does not support ACK/NACK or retransmissions\n"
    "File descriptor 3 receives MAC requests for packets. Sizes are in ASCII integers\n"
    "separated by whitespace\n"
    "The program sends and received PDUs at STDOUT (fd 1)\n"
    "The program sends and receives SDUs at STDIN (fd 0)\n"
    "  -h, --help                       Show this help and exit\n"
    "  -v, --verbose                    Show debugging output\n"
    "      --sdu-line-input-file=FILE   Reads newline separated SDUs from FILE\n"
    "      --sdu-stdio                  Use newlines as packet delimiters and communicate\n"
    "                                   through STDIN and STDOUT\n"
    "  -s, --send-only                  Only send without reception\n"
    ;exit(1);
}

int main(int argc, char **argv) {
  for(;;) {
    int option_index;
    int c = getopt_long(argc, argv, short_options, long_options, &option_index);
    if(c == -1) break;
    switch (c) {
    case -1: cerr << "Error parsing command line arguments" << endl;
      usage();
    case '?': usage();
    case 'h': usage();
    case 1:
      sdu_line_input_stream = fopen(optarg, "rt");
      if(!sdu_line_input_stream) {	perror("sdu-line-input-file:fopen"); exit(1); }
      cerr << "Reading SDUs from " << optarg << " with line separators" << endl;
      break;
    case 2:
      sdu_line_input_stream = fdopen(STDIN_FILENO, "rt");
      if(!sdu_line_input_stream) {	perror("sdu-stdio:fdopen"); exit(1); }
      setvbuf(sdu_line_input_stream, NULL, _IOLBF, 32768);
      sdu_line_output = 1;
      cerr << "Reading and writing SDUs from STDIO with line separators" << endl;
      break;
    case 's':
      break;
    }
      
  }
  send_opportunity_stream = fdopen(send_opportunity_fd, "r");
  if(!send_opportunity_stream) {
    perror("fdopen(send_opportunity_fd=3");
    cerr << "You must provide requested packet sizes on fd 3" << endl;
    return 1;
  }
  test_send_only();
  return 0;
}

  
