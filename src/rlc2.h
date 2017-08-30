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

#if 1
/*********************************************************************
 ** EXAMPLE
 **/

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <argz.h>
#include <envz.h>

/*********************************************************************
 ** CALLBACK FUNCTIONS
 **/

static int
sdu_send_opportunity(void *arg, unsigned time_in_ms, void *buffer, int size) {
  printf("%s[ms=%04d]: %d byte send opportunity\n", arg, time_in_ms % 10000, size);
  return -1;
}
static void
sdu_received(void *arg, unsigned time_in_ms, void *buffer, int size) {
  printf("%s[ms=%04d]: %d byte packet received\n", arg, time_in_ms % 10000, size);
}
static void
sdu_delivered(void *arg, unsigned time_in_ms, void *buffer, int size) {
  printf("%s[ms=%04d]: %d byte packet acknowledged\n", arg, time_in_ms % 10000, size);
}
static void
radio_link_failure(void *arg, unsigned time_in_ms) {
  printf("%s[ms=%04d]: radio link failure (=ECONNRESET)\n", arg, time_in_ms % 10000);
}

/*********************************************************************
 ** HOW TO USE: parameter passing to nested function
 **/


int
rlc_example(RLC *rlc, char *upstream_envz, size_t upstream_envz_len) {
  /* Set parameters. Demonstrating use of convenient glibc argz_* and envz_* functions */
  char *envz = NULL; size_t envz_len = 0;
    /* Envz from command line parameters */
  char *rlc_argv[] = { "pollPDU=54", "maxRetxThreshold=8", NULL };
  argz_create(rlc_argv, &envz, &envz_len);
  /* Envz from string delimited by user-specified separator */
  argz_add_sep(&envz, &envz_len, "pollByte=64000 SN-FieldLength.tx=5 rx-SN-FieldLength.rx=5", ' ');
  /* Add one by one programmatically */
  envz_add(&envz, &envz_len, "t-StatusProhibit", "35");
  envz_add(&envz, &envz_len, "t-Reordering", "35");
  envz_add(&envz, &envz_len, "t-PollRetransmit", "5");
  /* Add in upstream parameter overrides. "true" means override from upstream_envz */
  envz_merge(&envz, &envz_len, upstream_envz, upstream_envz_len, true);

  /* PARAMETER SETUP */
  rlc->set_parameters(rlc, envz, envz_len);

  /* CALLBACK SETUP */
  rlc->arg = "LCID 0x01: ";
  rlc->sdu_send_opportunity = sdu_send_opportunity;
  rlc->sdu_received = sdu_received;
  rlc->sdu_delivered = sdu_delivered;
  rlc->cb_radio_link_failure = radio_link_failure;

  /* PROTOCOL USE */
  unsigned time_in_ms = 0u;
  rlc->timer_tick(rlc, time_in_ms);
  rlc->send_opportunity(rlc, time_in_ms, NULL, 0);
  rlc->received(rlc, time_in_ms, NULL, 0);
  ++time_in_ms;
  rlc->timer_tick(rlc, time_in_ms);
}

static char *const defaults[] = {
  "protocol=LTE-RLC-AM",
  NULL
};

/*********************************************************************
 ** Declarations of protocol instance constructors to be defined elsewhere
 **/

RLC *rlc_am_create();
RLC *rlc_tejeez_create();
RLC *rlc_ax25_create();
RLC *rlc_tm_create();


/*********************************************************************
 ** HOW TO USE: create protocol instance and get parameters from command line
 **/

int
main(int argc, char **argv) {
  char *envz = NULL; size_t envz_len = 0;
  argz_create(argv + 1, &envz, &envz_len);
  char *defaults_envz = NULL; size_t defaults_envz_len = 0;
  argz_create(defaults, &defaults_envz, &defaults_envz_len);
  envz_merge(&envz, &envz_len,  defaults_envz, defaults_envz_len, true);

  RLC *rlc;
  char *proto = envz_get(envz, envz_len, "protocol");
  if(strcmp(proto, "lte-rlc-tm") == 0) {
    rlc = rlc_tm_create();
  else if(strcmp(proto, "lte-rlc-am") == 0) {
    rlc = rlc_am_create();
  else if(strcmp(proto, "lte-rlc-um") == 0) {
    rlc = rlc_am_create();
    // rlc_am could also read this setting from "protocol" itself
    envz_add(&envz, &envz_len, "mode", "UM");
  } else if(strcmp("proto", "tejeez") == 0) {
    rlc = rlc_tejeez_create();
  } else if(strcmp("proto", "ax25") == 0) {
    rlc = rlc_ax25_create();
  } else {
    fprintf(stderr, "Invalid protocol specified: %s\n", proto);
    return 1;
  }
  /* CREATE PROTOCOL INSTANCE, passing given parameters on command line plus defaults */
  int retval = rlc_example(rlc, envz, envz_len);
  /* FREE PROTOCOL INSTANCE */
  rlc->free(rlc);
  rlc = NULL;

  return retval;
}
#endif // example
