// 3GPP LTE RLC: 4G Radio Link Control protocol interface

#include <stddef.h> // For size_t

#if defined _WIN32 || defined __CYGWIN__
  #ifdef BUILDING_DLL
    #ifdef __GNUC__
      #define DLL_PUBLIC __attribute__ ((dllexport))
    #else
      #define DLL_PUBLIC __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #else
    #ifdef __GNUC__
      #define DLL_PUBLIC __attribute__ ((dllimport))
    #else
      #define DLL_PUBLIC __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #endif
  #define DLL_LOCAL
#else
  #if __GNUC__ >= 4
    #define DLL_PUBLIC __attribute__ ((visibility ("default")))
    #define DLL_LOCAL  __attribute__ ((visibility ("hidden")))
  #else
    #define DLL_PUBLIC
    #define DLL_LOCAL
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

  /******** RLC API *********/
  struct rlc_state;
  typedef struct rlc_state RLC;

  extern DLL_PUBLIC const char *const rlc_parameter_names[];
  DLL_PUBLIC RLC *   rlc_init();
  DLL_PUBLIC void    rlc_free(RLC *state);
  // rlc_reset resets all protocol state like calling rlc_init()
  // but leaves parameters and callbacks untouched
  DLL_PUBLIC void    rlc_reset(RLC *state);
  // Returns -1 if parameters are invalid and sets errno. State is then undefined
  // until the next call to rlc_set_parameters
  DLL_PUBLIC int     rlc_set_parameters(RLC *state, const char *envz, size_t envz_len);
  /* Returns -1 if doesn't want to or can't send a packet */
  DLL_PUBLIC int     rlc_pdu_send_opportunity(RLC *state, unsigned time_in_ms, void *buffer, int size);
  DLL_PUBLIC void    rlc_pdu_received(RLC *state, unsigned time_in_ms, const void *buffer, int size);
  // rlc_timer_tick: Use this to do slow work. Call radio_link_failure
  // callback for example or shuffle buffers
  DLL_PUBLIC void    rlc_timer_tick(RLC *state, unsigned time_in_ms);

  /******** Callbacks *********/

  // Implement these upper layer callbacks.

  /* Return -1 if don't want to send a packet */
  typedef int (*rlc_sdu_send_opportunity_fn)(void *arg, unsigned time_in_ms, void *buffer, size_t size);
  typedef void (*rlc_sdu_received_fn)(void *arg, unsigned time_in_ms, const void *buffer, size_t size);
  typedef void (*rlc_sdu_delivered_fn)(void *arg, unsigned time_in_ms, const void *buffer, size_t size);
  typedef void (*rlc_radio_link_failure_fn)(void *arg, unsigned time_in_ms);

  // If rlf is not given, retransmits will continue forever
  DLL_PUBLIC void    rlc_am_set_callbacks(RLC *state, void *arg,
			       rlc_sdu_send_opportunity_fn sdu_send,
			       rlc_sdu_received_fn sdu_recv,
			       rlc_sdu_delivered_fn sdu_delivered,
			       rlc_radio_link_failure_fn rlf);

#ifdef __cplusplus
}
#endif			 
