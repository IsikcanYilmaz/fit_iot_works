#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include "iperf.h"
#include "logger.h"
#include "iperf_pkt.h"
#include "thread.h"
#include "ztimer.h"
#include "msg.h"
#include "jammer.h"

#include "net/gnrc.h" // some of this may be redundant
#include "net/gnrc/ipv6.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/netif/hdr.h"
#include "net/gnrc/pktdump.h"
#include "net/gnrc/udp.h"
#include "net/utils.h"
#include "shell.h"
#include "timex.h"
#include "utlist.h"

kernel_pid_t jammerPid;

static msg_t _msg_queue[IPERF_MSG_QUEUE_SIZE];
extern IperfJammerConfig_s jammerConfig;
static char *dummyAddr = "fe80::0:0:0:0";
static uint16_t dummyPort = 666;
static netif_t *netif;
static ipv6_addr_t addr;
static bool initialized = false;
extern ztimer_t intervalTimer;

// Yanked out of RIOT/sys/shell/cmds/gnrc_udp.c
static void _send(const char *data, uint16_t num, uint16_t delayMs)
{
  uint16_t port = dummyPort;

  while (num--)
  {
    gnrc_pktsnip_t *payload, *udp, *ip;
    unsigned payload_size;

    /* allocate payload */
    payload = gnrc_pktbuf_add(NULL, data, strlen(data), GNRC_NETTYPE_UNDEF);
    if (payload == NULL) {
      logerror("Error: unable to copy data to packet buffer\n");
      return;
    }
    /* store size for output */
    payload_size = (unsigned)payload->size;
    /* allocate UDP header, set source port := destination port */
    udp = gnrc_udp_hdr_build(payload, port, port);
    if (udp == NULL) {
      logerror("Error: unable to allocate UDP header\n");
      gnrc_pktbuf_release(payload);
      return;
    }
    /* allocate IPv6 header */
    ip = gnrc_ipv6_hdr_build(udp, NULL, &addr);
    if (ip == NULL) {
      logerror("Error: unable to allocate IPv6 header\n");
      gnrc_pktbuf_release(udp);
      return;
    }
    /* add netif header, if interface was given */
    if (netif != NULL) {
      gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, NULL, 0);
      if (netif_hdr == NULL) {
        printf("Error: unable to allocate netif header\n");
        gnrc_pktbuf_release(ip);
        return;
      }
      gnrc_netif_hdr_set_netif(netif_hdr->data,
                               container_of(netif, gnrc_netif_t, netif));
      ip = gnrc_pkt_prepend(ip, netif_hdr);
    }
    /* send packet */
    if (!gnrc_netapi_dispatch_send(GNRC_NETTYPE_UDP,
                                   GNRC_NETREG_DEMUX_CTX_ALL, ip)) {
      logerror("Error: unable to locate UDP thread\n");
      gnrc_pktbuf_release(ip);
      return;
    }
    
    logdebug("Sent %u byte(s) to [%s]:%u\n", payload_size, dummyAddr, port);

    if (num)
    {
      ztimer_sleep(ZTIMER_USEC, delayMs * 1000);
    }
  }
}

static void initJammer(void)
{
  jammerPid = thread_getpid();

  /* parse destination address */
  if (netutils_get_ipv6(&addr, &netif, dummyAddr) < 0) {
    logerror("Error: unable to parse destination address\n");
    return;
  }

  initialized = true;
}

static void deinitJammer(void)
{
  // needed?
}

// Send out $burstMax many messages, that are $payloadSizeBytes big
// Wait for $burstDelayMsMin-$burstDelayMsMax in between every transmission.
// after the burst, wait for $sleepDelayMsMin-$sleepDelayMsMax
static void chatter(void)
{
  uint16_t num = 1 + (rand() % (jammerConfig.burstMax - 1));
  uint16_t delayMs = jammerConfig.burstDelayMsMin + (rand() % (jammerConfig.burstDelayMsMax - jammerConfig.burstDelayMsMin));
  char payload[jammerConfig.payloadSizeBytes];
  memset(payload, 0x31, sizeof(payload));
  loginfo("Chatter for %d times with %d ms in between.\n", num, delayMs);
  _send(payload, num, delayMs);
}

void *Iperf_JammerThread(void *arg)
{
  (void) arg;
  msg_t msg, reply;
  msg_init_queue(_msg_queue, IPERF_MSG_QUEUE_SIZE);
  initJammer();
  loginfo("Starting Jammer Thread. Sitting Idle. Pid %d\n", jammerPid);
  bool running = true;
  msg_t ipc = (msg_t) {.type = IPERF_IPC_MSG_JAM};
  ztimer_set_msg(ZTIMER_USEC, &intervalTimer, 0, &ipc, jammerPid);
  do {
    msg_receive(&msg);
    logdebug("IPC Message type %x\n", msg.type);
    switch (msg.type)
    {
      case IPERF_IPC_MSG_JAM:
      {
        chatter();

        // Calculate next time we do this:
        uint16_t sleepMs = jammerConfig.sleepDelayMsMin + (rand() % (jammerConfig.sleepDelayMsMax - jammerConfig.sleepDelayMsMin));
        loginfo("Sleeping for %d ms\n", sleepMs);
        ztimer_set_msg(ZTIMER_USEC, &intervalTimer, sleepMs * 1000, &ipc, jammerPid);
        break;
      }
      case IPERF_IPC_MSG_STOP:
      {
        running = false;
        break;
      }
      default:
        break;
    }
    uint16_t num = rand() % jammerConfig.burstMax;
  } while (running);
  deinitJammer();
  loginfo("Jammer thread exiting\n");
}
