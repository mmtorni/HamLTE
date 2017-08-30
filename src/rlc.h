// 3GPP LTE RLC: 4G Radio Link Control protocol interface

#include <stddef.h> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

  /******** RLC API *********/
  struct rlc_state;
  typedef struct rlc_state RLC;

  const char *const rlc_parameter_names[];
  RLC *   rlc_init();
  void    rlc_free(RLC *state);
  // rlc_reset resets all protocol state like calling rlc_init()
  // but leaves parameters and callbacks untouched
  // Returns -1 if parameters are invalid and sets errno. Mode is then undefined
  int     rlc_reset(RLC *state);
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
  typedef int (*rlc_sdu_send_opportunity_fn)(void *arg, unsigned time_in_ms, void *buffer, size_t size);
  typedef void (*rlc_sdu_received_fn)(void *arg, unsigned time_in_ms, void *buffer, size_t size);
  typedef void (*rlc_sdu_delivered_fn)(void *arg, unsigned time_in_ms, void *buffer, size_t size);
  typedef void (*rlc_radio_link_failure_fn)(void *arg, unsigned time_in_ms);

  // If rlf is not given, retransmits will continue forever
  void    rlc_am_set_callbacks(RLC *state, void *arg,
			       rlc_sdu_send_opportunity_fn sdu_send,
			       rlc_sdu_received_fn sdu_recv,
			       rlc_sdu_delivered_fn sdu_delivered,
			       rlc_radio_link_failure_fn rlf);

#ifdef __cplusplus
}
#endif			 
