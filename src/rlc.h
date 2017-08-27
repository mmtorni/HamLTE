#ifdef __cplusplus
extern "C" {
#endif
  struct rlc_am_state;
  struct rlc_am_parameters {
    /******** RLC AM parameters *********/

    //LTE name: maxRetxThreshold range: 1-32s
    // [1s] Report failure if unable to deliver a packet in this time.
    size_t am_max_retx_threshold;

    //LTE name: pollPDU range: 4-16384 or infinity
    // [64] Send poll after every am_poll_pdu pdus. Set to zero if unwanted.
    size_t am_poll_pdu;

    //LTE name: pollByte range: 1kB-40MB or infinity
    // [64kB] Send poll after every am_poll_pdu_bytes. Set to zero if unwanted.
    uint32_t am_poll_pdu_bytes;

    //LTE name: t-Reordering range: 5-1600ms or off
    // [35ms] Reordering time window before sending STATUS.
    size_t timeout_reordering;

    //LTE name: t-StatusProhibit range: 5-2400ms or off
    // [35ms] Do not send status messages more often than this (in ms).
    size_t timeout_status_prohibit;

    //LTE name: t-PollRetransmit range: 5-4000ms
    // [5ms] How long to wait for response to poll before retransmitting
    size_t timeout_poll_retransmit;


    /******** RLC UM parameters *********/

    // Valid choices are 5 (default) and 10
    size_t um_sequence_number_field_length;
  };

  /******** RLC mode choice *********/

  /*
    Three modes: 0=Acknowledged Mode, 1=Unacknowledged Mode, 2=Transparent Mode
    0: in-order, reliable packet stream, like SOCK_SEQPACKET
    1: in-order, unreliable packet stream, like unreliable SOCK_SEQPACKET
    2: out-of-order, unreliable packet stream, bit like SOCK_DGRAM. Packets go straight through and no fragmentation is done; packet size is restricted to that given by rlc_pdu_send_opportunity
  */


  /******** RLC API *********/
  void *  rlc_init() { return new rlc_am_state(); }
  void    rlc_free(struct rlc_am_state *state) { delete state; }
  // rlc_set resets all protocol state, but leaves parameters and callbacks untouched
  // Default mode is 0=Acknowledged Mode
  void    rlc_set_mode(int mode);
  // Parameters may be changed at any time except for mode.
  int     rlc_set_parameters(struct rlc_am_parameters *parameters) { return 0; }
  ssize_t rlc_pdu_send_opportunity(unsigned time_in_ms, void *buffer, size_t size) { return -1; }
  void    rlc_pdu_received(unsigned time_in_ms, void *buffer, size_t size) { }

  /******** Callbacks *********/

  // Implement these upper layer callbacks.

  /* Return -1 if don't want to send a packet */
  typedef ssize_t (*rlc_sdu_send_opportunity_fn)(void *arg, unsigned time_in_ms, void *buffer, size_t size);
  typedef void (*rlc_sdu_received_fn)(void *arg, unsigned time_in_ms, void *buffer, size_t size);
  typedef void (*rlc_radio_link_failure_fn)(void *arg, unsigned time_in_ms);

  // If rlf is not given, retransmits will continue forever
  void    rlc_am_set_callbacks(struct rlc_am_state *state, void *arg,
			       rlc_sdu_send_opportunity_fn sdu_send,
			       rlc_sdu_received_fn sdu_recv,
			       rlc_radio_link_failure_fn rlf);
#ifdef __cplusplus
}
#endif			 
