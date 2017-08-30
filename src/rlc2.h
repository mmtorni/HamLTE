// 3GPP LTE RLC: 4G Radio Link Control protocol interface
//
// TODO: add callbacks for confirming succesful delivery of whole SDU.
//       That would mean some sort of ID tracking is necessary or
//       would need to give the whole SDU on callback

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
  struct rlc_instance;
  typedef struct rlc_instance RLC;
  struct rlc_instance {
    /* BOILERPLATE */
    /* Create a similar copy of this instance, with a new arg */
    RLC *(*clone)(RLC *rlc, void *new_arg);
    /* Use this to free this instance */
    void (*free)(RLC *rlc);

    /* INTERFACE */

    int     (*set_parameters)(RLC *rlc, const char *envz, size_t envz_len);
    void    (*get_parameters)(RLC *rlc, char **envz, size_t *envz_len);
    int     (*send_opportunity)(RLC *state, unsigned time_in_ms, void *buffer, size_t size);
    void    (*received)(RLC *state, unsigned time_in_ms, void *buffer, size_t size);
    void    (*timer_tick)(RLC *state, unsigned time_in_ms);
    /* Fill these in yourself. They will be called from the above functions */

    /* CALLBACKS
     *
     * Any of these callbacks may be NULL
     * For a time instance, callbacks will be called in the order:
     * sdu_send_opportunity(time) (time-critical)
     * sdu_received / sdu_delivered (non-time-critical)
     * sdu_timer_tick(time+1)
     */
    void    *arg;

    /* Return -1 if don't want to send a packet */
    int  (*sdu_send_opportunity)(void *arg, unsigned time_in_ms, void *buffer, size_t size);
    void (*sdu_received)(void *arg, unsigned time_in_ms, void *buffer, size_t size);
    void (*sdu_delivered)(void *arg, unsigned time_in_ms, void *buffer, size_t size);
    void (*cb_radio_link_failure)(void *arg, unsigned time_in_ms);
    void (*cb_timer_tick)(void *arg, unsigned time_in_ms);

    void *reserved[16]; /* Space for future callbacks */
  };

#ifdef __cplusplus
}
#endif
