#include <stdio.h>
#include <stdlib.h>
#include "iperf.h"
#include "relayer.h"
#include "logger.h"
#include "iperf_pkt.h"
#include "thread.h"
#include "ztimer.h"
#include "msg.h"

#include "net/ipv6/hdr.h"
#include "net/ipv6/addr.h"

extern uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];
extern IperfConfig_s config;

static msg_t _msg_queue[IPERF_MSG_QUEUE_SIZE];
static volatile kernel_pid_t relayerPid = KERNEL_PID_UNDEF;
static uint8_t *cacheBuffer = (uint8_t *) &rxtxBuffer;


static void initRelayer(void)
{
  relayerPid = thread_getpid();
}

static void deinitRelayer(void)
{
  relayerPid = KERNEL_PID_UNDEF;
}

void *Iperf_RelayerThread(void *arg)
{
  (void) arg;
  msg_t msg, reply;
  msg_init_queue(_msg_queue, IPERF_MSG_QUEUE_SIZE);

  initRelayer();

  loginfo("Starting Relayer Thread. Sitting Idle. Pid %d\n", relayerPid);

  bool running = true;
  do {
    msg_receive(&msg);
    logdebug("IPC Message type %x\n", msg.type);
    switch (msg.type) {
      IPERF_IPC_MSG_RELAY_RESPOND: // IF relayer needs to do something instead of simply forwarding
      {
        loginfo("RELAYER RESPONSE\n");
        break;
      }
    IPERF_IPC_MSG_STOP:
      {
        running = false;
        break;
      }
      default:
        logverbose("received something unexpected %d\n", msg.type);
        break;
    }
  } while (running);
  deinitRelayer();
  loginfo("Relayer thread exiting\n");
  return NULL;
}

bool Iperf_RelayerIntercept(gnrc_pktsnip_t *snip)
{
  bool keepForwrding = false;

  // We care about IPv6 and UNDEF snips. 
  gnrc_pktsnip_t *ipv6 = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_IPV6);
  gnrc_pktsnip_t *undef = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_UNDEF);
  
  if (ipv6)
  {
    printf("[IPV6]\n");
    ipv6_hdr_print(ipv6->data);
  }

  if (undef)
  {
    printf("[UNDEF]\n");
    for (int i = 0; i < undef->size; i++)
    {
      char data = * (char *) (undef->data + i);
      printf("%02x %c %s", data, data, (i % 8 == 0 && i > 0) ? "\n" : "");
    }
    printf("\n");
  }

  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) (undef->data + 8); // JON TODO HACK //  Idk why I need this 8 byte offset but i do
  ipv6_hdr_t *ipv6header = (ipv6_hdr_t *) ipv6->data;
  
  printf("\n PL:%s\n", iperfPkt->payload);

  /*if (strncmp(iperfPkt->payload, "asdqwe", 6) == 0)*/
  {
    printf("TEST PAYLOAD RECEIVED\n");
    msg_t ipc;
    ipc.type = IPERF_IPC_MSG_RELAY_RESPOND;
    msg_send(&ipc, relayerPid);
    return true; 
  }

  if (ipv6)
  {
    printf("[IPV6]\n");
    ipv6_hdr_print(ipv6->data);
  }

  return true;
}
