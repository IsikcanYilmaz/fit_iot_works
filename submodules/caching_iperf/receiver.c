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
  RECEIVER_RUNNING,
  RECEIVER_STATE_MAX
} IperfReceiverState_e;

extern IperfResults_s results;
extern IperfConfig_s config;
extern uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];
extern bool receivedPktIds[IPERF_TOTAL_TRANSMISSION_SIZE_MAX];
extern char receiveFileBuffer[IPERF_TOTAL_TRANSMISSION_SIZE_MAX];

static uint8_t *txBuffer = (uint8_t *) &rxtxBuffer;
static msg_t _msg_queue[IPERF_MSG_QUEUE_SIZE];

static IperfReceiverState_e receiverState = RECEIVER_STOPPED;
static kernel_pid_t receiverPid = KERNEL_PID_UNDEF;
static gnrc_netreg_entry_t udpServer = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF);

static int receiverHandleIperfPacket(gnrc_pktsnip_t *pkt)
{
  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) pkt;

  if (iperfPkt->msgType >= IPERF_CTRL_MSG_MAX)
  {
    logerror("Receiver received bad msg_type %x\n", iperfPkt->msgType);
    return 1;
  }

  /*logverbose("Received seq no %d\nPayload %s\nLosses %d\nDups %d\n", iperfPkt->seqNo, iperfPkt->payload, results.pktLossCounter, results.numDuplicates);*/
  logverbose("Received Iperf Pkt: Type %d\n", iperfPkt->msgType);
  
  switch (iperfPkt->msgType)
  {
    case IPERF_PAYLOAD:
      {
        if (results.lastPktSeqNo == iperfPkt->seqNo - 1)
        {
          // NO LOSS 
          results.lastPktSeqNo = iperfPkt->seqNo;
          logverbose("RX %d\n", iperfPkt->seqNo);
        }
        else if (receivedPktIds[iperfPkt->seqNo])
        {
          // Dup
          results.numDuplicates++; 
          logverbose("DUP %d\n", iperfPkt->seqNo);
        }
        else if (results.lastPktSeqNo < iperfPkt->seqNo)
        {
          // Loss happened
          uint16_t lostPkts = (iperfPkt->seqNo - results.lastPktSeqNo);
          results.pktLossCounter += lostPkts;
          results.lastPktSeqNo = iperfPkt->seqNo;
          logverbose("LOSS %d pkts \n", lostPkts);
        }

        receivedPktIds[iperfPkt->seqNo] = true;

        results.numReceivedPkts++;
        results.endTimestamp = ztimer_now(ZTIMER_USEC);
        if (results.numReceivedPkts == 1)
        {
          results.startTimestamp = results.endTimestamp;
        }
      }
    case IPERF_PKT_REQ:
      {
        break;
      }
    case IPERF_ECHO_CALL:
      {
        loginfo("Echo Received %s\n", iperfPkt->payload);
        break;
      }
    case IPERF_ECHO_RESP:
      {
        break;
      }
    default:
      {
        logerror("Bad iperf packet type %d\n", iperfPkt->msgType);
        break;
      }
  }
}

void *Iperf_ReceiverThread(void *arg)
{
  (void) arg;
  msg_t msg, reply;
  msg_init_queue(_msg_queue, IPERF_MSG_QUEUE_SIZE);

  reply.content.value = (uint32_t)(-ENOTSUP);
  reply.type = GNRC_NETAPI_MSG_TYPE_ACK;

  receiverPid = thread_getpid();
  Iperf_StartUdpServer(&udpServer, receiverPid);
  loginfo("Starting Receiver Thread. Pid %d\n", receiverPid);
  receiverState = RECEIVER_RUNNING;

  while (receiverState > RECEIVER_STOPPED) {
    msg_receive(&msg);
    logdebug("IPC Message type %d\n", msg.type);
    switch (msg.type) {
      case GNRC_NETAPI_MSG_TYPE_RCV:
        logdebug("Data received\n");
        Iperf_PacketHandler(msg.content.ptr, (void *) receiverHandleIperfPacket);
        break;
      case GNRC_NETAPI_MSG_TYPE_SND:
        logdebug("Data to send\n");
        Iperf_PacketHandler(msg.content.ptr, (void *) receiverHandleIperfPacket);
        break;
      case GNRC_NETAPI_MSG_TYPE_GET:
      case GNRC_NETAPI_MSG_TYPE_SET:
        msg_reply(&msg, &reply);
        break;
      case IPERF_IPC_MSG_STOP:
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
