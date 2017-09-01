#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "rlc.h"

#include "rlc_parameters.h"

struct rlc_state {
  void *arg;
  rlc_sdu_send_opportunity_fn sdu_send;
  rlc_sdu_received_fn sdu_recv;
  rlc_radio_link_failure_fn rlf;
};

RLC *rlc_init() {
  return (RLC *)calloc(1, sizeof(RLC));
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
rlc_pdu_send_opportunity(RLC *rlc,unsigned time_in_ms, void *buffer, int size) {
  return rlc->sdu_send(rlc->arg, time_in_ms, buffer, size);
}

void
rlc_pdu_received(RLC *rlc, unsigned time_in_ms, const void *buffer, int size) {
  return rlc->sdu_recv(rlc->arg, time_in_ms, buffer, size);
}

void
rlc_am_set_callbacks(RLC *rlc, void *arg, rlc_sdu_send_opportunity_fn sdu_send, rlc_sdu_received_fn sdu_recv, rlc_sdu_received_fn sdu_delivered, rlc_radio_link_failure_fn rlf) {
  rlc->arg = arg;
  rlc->sdu_send = sdu_send;
  rlc->sdu_recv = sdu_recv;
  rlc->rlf = rlf;
}

void
rlc_timer_tick(RLC *state, unsigned time_in_ms) {
  
}
