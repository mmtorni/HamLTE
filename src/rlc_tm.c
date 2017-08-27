#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "rlc.h"

typedef struct rlc_am_state *STATE;

struct rlc_am_state {
  void *arg;
  rlc_sdu_send_opportunity_fn sdu_send;
  rlc_sdu_received_fn sdu_recv;
  rlc_radio_link_failure_fn rlf;
};

STATE rlc_init() {
  return calloc(1, sizeof(struct rlc_am_state));
}

void
rlc_free(STATE state) {
  free(state);
}

int
rlc_set_mode(STATE state, int mode) {
  if(mode != RLC_MODE_TM) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

int
rlc_get_mode(STATE state) {
  return RLC_MODE_TM;
}

int
rlc_set_parameters(STATE state, struct rlc_am_parameters *parameters) {
  return 0;
}

void
rlc_get_parameters(STATE state, struct rlc_am_parameters *parameters) {
  memset(parameters, 0, sizeof(*parameters));
}

ssize_t
rlc_pdu_send_opportunity(struct rlc_am_state *state,unsigned time_in_ms, void *buffer, size_t size) {
  return state->sdu_send(state->arg, time_in_ms, buffer, size);
}

void
rlc_pdu_received(struct rlc_am_state *state, unsigned time_in_ms, void *buffer, size_t size) {
  return state->sdu_recv(state->arg, time_in_ms, buffer, size);
}

void
rlc_am_set_callbacks(struct rlc_am_state *state, void *arg, rlc_sdu_send_opportunity_fn sdu_send, rlc_sdu_received_fn sdu_recv, rlc_radio_link_failure_fn rlf) {
  state->sdu_send = sdu_send;
  state->sdu_recv = sdu_recv;
  state->rlf = rlf;
}

