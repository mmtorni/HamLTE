// 3GPP LTE RLC: 4G Radio Link Control protocol interface
// Parameters for RLC AM


const char *const rlc_parameter_names[] =
{
  /* RLC mode AM/UM/TM */
  "rlc/mode",
  /* AM */
  "maxRetxThreshold",
  "pollPDU",
  "pollByte",
  "t-StatusProhibit",
  "t-PollRetransmit",
  /* AM & UM */
  "t-Reordering",
  /* UM */
  "SN-FieldLength.rx",
  "SN-FieldLength.tx",
  /* PDCP */
  "headerCompression",
  "pdcp-SN-Size",
  "statusReportRequired",
  "discardTimer",
  "maxCID",
  "profiles",
  "pdcp/t-Reordering",
  "",
  NULL
};


  const char *const rlc_parameter_names[];
  
  /******** RLC API *********/
  typedef struct rlc_state RLC;
  RLC *   rlc_init();
  void    rlc_free(RLC *state);
  // rlc_set_mode resets all protocol state, but leaves parameters and callbacks untouched
  // Returns -1 if parameters are invalid and sets errno. Mode is then undefined
  int     rlc_reset();
  int     rlc_set_parameters(RLC *state, const char *envz, size_t envz_len);
  /* Returns -1 if doesn't want to or can't send a packet */
  int     rlc_pdu_send_opportunity(RLC *state, unsigned time_in_ms, void *buffer, int size);
  void    rlc_pdu_received(RLC *state, unsigned time_in_ms, void *buffer, int size);
  // rlc_timer_tick: Use this to do slow work. Call radio_link_failure
  // callback for example or shuffle buffers
  void    rlc_timer_tick(RLC *state, unsigned time_in_ms);

  /******** Callbacks *********/

  // Implement these upper layer callbacks.

  /* Return -1 if don't want to send a packet */
  int  rlc_sdu_send_opportunity(void *arg, unsigned time_in_ms, void *buffer, size_t size);
  void rlc_sdu_received(void *arg, unsigned time_in_ms, void *buffer, size_t size);
  void rlc_sdu_delivered(void *arg, unsigned time_in_ms, void *buffer, size_t size);
  void rlc_sdu_radio_link_failure(void *arg, unsigned time_in_ms);

#ifdef __cplusplus
}
#endif			 
