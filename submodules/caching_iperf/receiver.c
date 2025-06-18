#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "thread.h"
#include "ztimer.h"
#include "msg.h"

#include "macros/utils.h"
#include "net/gnrc.h"
#include "net/sock/udp.h"
#include "net/gnrc/udp.h"
#include "net/sock/tcp.h"
#include "net/gnrc/nettype.h"
#include "net/utils.h"
#include "sema_inv.h"
#include "test_utils/benchmark_udp.h"
#include "ztimer.h"
#include "shell.h"
#include "od.h"
#include "net/netstats.h"

#include "iperf.h"
#include "iperf_pkt.h"
#include "message.h"
#include "logger.h"

#include "receiver.h"

typedef enum
{
  RECEIVER_STOPPED,
  RECEIVER_IDLE,
   



  RECEIVER_STATE_MAX
} IperfReceiverState_e;

static msg_t _msg_queue[IPERF_RECEIVER_MSG_QUEUE_SIZE];

static IperfReceiverState_e receiverState = RECEIVER_IDLE;
static char receiveFileBuffer[IPERF_TOTAL_TRANSMISSION_SIZE_MAX];

static gnrc_netreg_entry_t udpServer = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF);

static kernel_pid_t receiverPid = KERNEL_PID_UNDEF;

extern IperfResults_s results;
extern bool receivedPktIds[IPERF_TOTAL_TRANSMISSION_SIZE_MAX];

static int receiverHandleIperfPayload(gnrc_pktsnip_t *pkt)
{
  iperf_udp_pkt_t *iperfPl = (iperf_udp_pkt_t *) pkt;
  logverbose("Received seq no %d\nPayload %s\nLosses %d\nDups %d\n", iperfPl->seq_no, iperfPl->payload, results.pktLossCounter, results.numDuplicates);

  if (results.lastPktSeqNo == iperfPl->seq_no - 1)
  {
    // NO LOSS 
    results.lastPktSeqNo = iperfPl->seq_no;
    logverbose("RX %d\n", iperfPl->seq_no);
  }
  else if (receivedPktIds[iperfPl->seq_no])
  {
    // Dup
    results.numDuplicates++;
    logverbose("DUP %d\n", iperfPl->seq_no);
  }
  else if (results.lastPktSeqNo < iperfPl->seq_no)
  {
    // Loss happened
    uint16_t lostPkts = (iperfPl->seq_no - results.lastPktSeqNo);
    results.pktLossCounter += lostPkts;
    results.lastPktSeqNo = iperfPl->seq_no;
    logverbose("LOSS %d pkts \n", lostPkts);
  }

  receivedPktIds[iperfPl->seq_no] = true;

  results.numReceivedPkts++;
  results.endTimestamp = ztimer_now(ZTIMER_USEC);
  if (results.numReceivedPkts == 1)
  {
    results.startTimestamp = results.endTimestamp;
  }
}

void *Iperf_ReceiverThread(void *arg)
{
  (void) arg;
  msg_t msg, reply;
  msg_init_queue(_msg_queue, IPERF_RECEIVER_MSG_QUEUE_SIZE);

  reply.content.value = (uint32_t)(-ENOTSUP);
  reply.type = GNRC_NETAPI_MSG_TYPE_ACK;

  receiverPid = thread_getpid();
  Iperf_StartUdpServer(&udpServer, receiverPid);
  loginfo("Starting Receiver Thread. Pid %d\n", receiverPid);
  receiverState = RECEIVER_IDLE;

  while (receiverState > RECEIVER_STOPPED) {
    msg_receive(&msg);
    logdebug("Received type %d\n", msg.type);
    switch (msg.type) {
      case GNRC_NETAPI_MSG_TYPE_RCV:
        logdebug("Data received\n");
        Iperf_PacketHandler(msg.content.ptr, NULL);
        break;
      case GNRC_NETAPI_MSG_TYPE_SND:
        logdebug("Data to send");
        Iperf_PacketHandler(msg.content.ptr, NULL);
        break;
      case GNRC_NETAPI_MSG_TYPE_GET:
      case GNRC_NETAPI_MSG_TYPE_SET:
        msg_reply(&msg, &reply);
        break;
      case IPERF_MSG_TYPE_DONE:
        receiverState = RECEIVER_STOPPED;
        break;
      default:
        /*loginfo("received something unexpected");*/
        break;
    }
  }
  Iperf_StopUdpServer(&udpServer);
  loginfo("Receiver thread exiting\n");
}
