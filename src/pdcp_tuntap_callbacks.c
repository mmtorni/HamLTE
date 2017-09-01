#include "rlc.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <argz.h>
#include <envz.h>

static int tun_alloc(char *dev, int *flags) {

  struct ifreq ifr; memset(&ifr, 0, sizeof(ifr));
  int fd, err;
  char *clonedev = "/dev/net/tun";

  if( (fd = open(clonedev, O_RDWR)) < 0 ) { return fd; }
  if (*dev) { strncpy(ifr.ifr_name, dev, IFNAMSIZ); }

  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
    ifr.ifr_flags = IFF_TAP;
    if( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
      close(fd); return err;
    }
  }
  strcpy(dev, ifr.ifr_name);
  return fd;
}

struct pdcp_tun {
  int fd;
  unsigned send_sn;
  size_t max_sdu_size;
  size_t pdcp_sn_size;
  size_t pdcp_data_header_in_bytes;
};

static size_t min(size_t a, size_t b) { return (b < a) ? b : a; }

static void
pdcp_tun_send(void *arg, unsigned time_in_ms, const void *buffer, size_t size) {
  struct pdcp_tun *s = (struct pdcp_tun *)arg;
  const uint8_t *p = buffer;
  p += s->pdcp_data_header_in_bytes;
  int retval = send(s->fd, buffer, size - s->pdcp_data_header_in_bytes, MSG_DONTWAIT);
  if (retval == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    // Just drop the packet
  } else {
    // Handle errors or quietly ignore?
  }
}

static int
pdcp_tun_recv(void *arg, unsigned time_in_ms, void *buffer, size_t size) {
  struct pdcp_tun *s = (struct pdcp_tun *)arg;
  size_t h = s->pdcp_data_header_in_bytes;
  int retval = recv(s->fd, buffer + h, min(size - h, s->max_sdu_size), MSG_DONTWAIT);
  if (retval == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    return -1;
  } else if (retval == -1) {
    // Handle errors or quietly ignore?
    return -1;
  } else {
    /* Add PDCP data header */
    uint8_t *p = buffer;
    memset(buffer, 0, h);
    unsigned sn = s->send_sn = (s->send_sn + 1) % (1<<s->pdcp_sn_size);
    for(size_t i = 0; i < s->pdcp_data_header_in_bytes; ++i) {
      p[s->pdcp_data_header_in_bytes-1-i] = sn;
      sn >>= 8;
    }
    p[0] |= 0x80;
  }
  return retval + s->pdcp_data_header_in_bytes;
}

/*
 * dev must be IFNAMSIZ bytes long and will receive the used device name
 *
 * It will try opening it as a TUN first and TAP second. This works great to
 * autodetect persistent devices. In other cases it will create a TUN device.
 */

//static const char *rlc_pdcp_tun_defaults = "pdcp-SN-Size=7 cipheringAlgorithm=eea0 integrityProtAlgorithm=eia2";

int
rlc_pdcp_tun(RLC *state, char *dev, char *envz_more, size_t envz_more_len) {
  int flags;
  int fd = tun_alloc(dev, &flags);
  if (fd < 0)
    return fd;
  struct pdcp_tun *s = calloc(1, sizeof(*s)); // TODO: Leaks a tiny bit of memory
  s->fd = fd;
  s->max_sdu_size = atoi(envz_get(envz_more, envz_more_len, "pdcp-max-SDU-Size")) || 8188;
  s->pdcp_sn_size = atoi(envz_get(envz_more, envz_more_len, "pdcp-SN-Size")) || 7;
  s->pdcp_data_header_in_bytes = (1 + s->pdcp_sn_size + 7) / 8;
  
  rlc_am_set_callbacks(state, s, pdcp_tun_recv, pdcp_tun_send, NULL, NULL);
  return fd;
}

