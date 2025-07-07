#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
extern ztimer_t intervalTimer;
extern msg_t ipcMsg;

static msg_t _msg_queue[IPERF_MSG_QUEUE_SIZE];
static volatile kernel_pid_t relayerPid = KERNEL_PID_UNDEF;
static uint8_t *cacheBuffer = (uint8_t *) &rxtxBuffer;

static gnrc_netreg_entry_t udpServer = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF); // TODO JON this can be generic, in iperf.c

static void initRelayer(void)
{
  relayerPid = thread_getpid();
  Iperf_StartUdpServer(&udpServer, relayerPid);
}

static void deinitRelayer(void)
{
  Iperf_StopUdpServer(&udpServer);
  relayerPid = KERNEL_PID_UNDEF;
}

static int sendPayload(void)
{
  char buf[sizeof(IperfUdpPkt_t) + 8];
  IperfUdpPkt_t *fakeEchoResp = (IperfUdpPkt_t *) &buf;
  fakeEchoResp->msgType = IPERF_ECHO_RESP;
  fakeEchoResp->seqNo = 0;
  strncpy(fakeEchoResp->payload, "ASDQWE", 6);
  return Iperf_SocklessUdpSendToSrc((char *) buf, sizeof(buf));
}

static bool diceRoll(uint8_t percent)
{
  if (rand() % 100 < percent)
  {
    return true;
  }
  return false;
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
    logverbose("IPC Message type %x\n", msg.type);
    switch (msg.type) {
      case IPERF_IPC_MSG_RELAY_RESPOND: // IF relayer needs to do something instead of simply forwarding
      {
        loginfo("RELAYER RESPONSE\n");
        sendPayload();
        break;
      }
      case IPERF_IPC_MSG_STOP:
      {
        running = false;
        break;
      }
      default:
        logdebug("IPC received something unexpected %x\n", msg.type);
        break;
    }
  } while (running);
  deinitRelayer();
  loginfo("Relayer thread exiting\n");
  return NULL;
}

// Will return true if the packet should keep going
bool Iperf_RelayerIntercept(gnrc_pktsnip_t *snip)
{
  bool shouldForward = false;

  // We care about IPv6 and UNDEF snips. 
  gnrc_pktsnip_t *ipv6 = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_IPV6);
  gnrc_pktsnip_t *undef = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_UNDEF);
  
  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) (undef->data + 8); // JON TODO HACK //  Idk why I need this 8 byte offset but i do
  ipv6_hdr_t *ipv6header = (ipv6_hdr_t *) ipv6->data;
  
  // FILTERS
  if (strncmp(iperfPkt->payload, "asdqwe", 6) == 0)
  {
    msg_t ipc;
    ipc.type = IPERF_IPC_MSG_RELAY_RESPOND;
    msg_send(&ipc, relayerPid);
    shouldForward = false;
  }
  else if (iperfPkt->msgType == IPERF_PAYLOAD)
  {
    if (config.cache)
    {
      
    }
    // TODO code
    shouldForward = true;
  }
  else if (iperfPkt->msgType == IPERF_PKT_REQ)
  {
    //
    if (config.cache)
    {

    }
    shouldForward = true;
  }
  else if (iperfPkt->msgType == IPERF_PKT_BULK_REQ)
  {
    //
    shouldForward = true;
  }
  return shouldForward;
}
