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
#include "simple_queue.h"

#include "receiver.h"

typedef enum
{
  SENDER_STOPPED,
  SENDER_IDLE,
  SENDER_SENDING,
  SENDER_STATE_MAX
} IperfSenderState_e;

extern IperfResults_s results;
extern IperfConfig_s config;
extern uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];
extern char dstGlobalIpAddr[25];
extern char srcGlobalIpAddr[25];
extern msg_t ipcMsg;
extern ztimer_t intervalTimer;

extern uint16_t pktReqQueueBuffer[];
extern SimpleQueue_t pktReqQueue;

static uint8_t *txBuffer = (uint8_t *) &rxtxBuffer;
static msg_t _msg_queue[IPERF_MSG_QUEUE_SIZE];

static IperfSenderState_e senderState = SENDER_STOPPED;
static kernel_pid_t senderPid = KERNEL_PID_UNDEF;
static gnrc_netreg_entry_t udpServer = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF);


static bool isTransferDone(void)
{
  bool ret = false;
  if (config.mode == IPERF_MODE_SIZE)
  {
    ret = (results.numSentBytes >= config.transferSizeBytes);
  }
  else if (config.mode == IPERF_MODE_TIMED)
  {
    ret = (ztimer_now(ZTIMER_USEC) - results.startTimestamp >= config.transferTimeUs);
  }
  else if (config.mode == IPERF_MODE_CACHING_BIDIRECTIONAL)
  {
    ret = (results.lastPktSeqNo == config.numPktsToTransfer-1) && (SimpleQueue_IsEmpty(&pktReqQueue));
  }
  return ret;
}

static int senderHandleIperfPacket(gnrc_pktsnip_t *pkt)
{
  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) pkt;
  if (iperfPkt->msgType >= IPERF_CTRL_MSG_MAX)
  {
    logerror("Sender received bad msg_type %x\n", iperfPkt->msgType);
    return 1;
  }

  /*logverbose("Received seq no %d\nPayload %s\nLosses %d\nDups %d\n", iperfPkt->seqNo, iperfPkt->payload, results.pktLossCounter, results.numDuplicates);*/
  logverbose("Received Iperf Pkt: Type %d\n", iperfPkt->msgType);
  switch (iperfPkt->msgType)
  {
    case IPERF_PAYLOAD:
      {
        loginfo("Sender received PAYLOAD packet. Shouldnt have...\n");
        break;
      }
    case IPERF_PKT_REQ:
      {
        IperfPacketRequest_t *reqPl = (IperfPacketRequest_t *) iperfPkt->payload;
        loginfo("Sender received PKT_REQ for packet seq no %d\n", reqPl->seqNo);
        SimpleQueue_Push(&pktReqQueue, reqPl->seqNo);
        if (senderState == SENDER_IDLE)
        {
          ipcMsg.type = IPERF_IPC_MSG_SEND_FILE;
          ztimer_set_msg(ZTIMER_USEC, &intervalTimer, config.delayUs, &ipcMsg, senderPid); // Start immediately
        }
        break;
      }
    case IPERF_ECHO_CALL:
      {
        loginfo("Echo CALL Received %s\n", iperfPkt->payload);
        break;
      }
    case IPERF_ECHO_RESP:
      {
        loginfo("Echo RESP Received %s\n", iperfPkt->payload);
        break;
      }
    case IPERF_CONFIG_SYNC:
      {
        /*Iperf_HandleConfigSync(iperfPkt);*/
        break;
      }
    default:
      {
        logerror("Bad iperf packet type %d\n", iperfPkt->msgType);
        break;
      }
  }
  return 0;
}

static void initSender(void)
{
  senderPid = thread_getpid();
  memset(txBuffer, 0x01, sizeof(IperfUdpPkt_t) + config.payloadSizeBytes);
  IperfUdpPkt_t *payloadPkt = (IperfUdpPkt_t *) txBuffer;
  payloadPkt->plSize = config.payloadSizeBytes;
  payloadPkt->msgType = IPERF_PAYLOAD;
  payloadPkt->seqNo = 0;
  strncpy((char *) &payloadPkt->payload, IperfMessage_GetPointer(0), config.payloadSizeBytes);
  Iperf_StartUdpServer(&udpServer, senderPid);
}

static void deinitSender(void)
{
  Iperf_StopUdpServer(&udpServer);
}

static int sendPayload(uint16_t chunkIdx, bool serveRequest)
{
  IperfUdpPkt_t *payloadPkt = (IperfUdpPkt_t *) txBuffer;
  uint16_t charIdx = chunkIdx * config.payloadSizeBytes;
  strncpy((char *) &payloadPkt->payload, IperfMessage_GetPointer(charIdx), config.payloadSizeBytes);
  logverbose("Sending payload. Seq no %d\n", payloadPkt->seqNo);
  return Iperf_SocklessUdpSendToDst((char *) txBuffer, config.payloadSizeBytes + sizeof(IperfUdpPkt_t));
}

static void handleFileSending(void)
{
  IperfUdpPkt_t *payloadPkt = (IperfUdpPkt_t *) txBuffer;

  if (SimpleQueue_IsEmpty(&pktReqQueue)) // If nothing in the request queue, send off the next chunk in our file transfer
  {
    payloadPkt->msgType = IPERF_PAYLOAD;
    payloadPkt->seqNo = results.lastPktSeqNo + 1;
    payloadPkt->plSize = config.payloadSizeBytes;
    uint16_t charIdx = payloadPkt->seqNo * config.payloadSizeBytes;
    strncpy((char *) &payloadPkt->payload, IperfMessage_GetPointer(charIdx), config.payloadSizeBytes);
    logverbose("Sending payload. Seq no %d\n", payloadPkt->seqNo);
    Iperf_SocklessUdpSendToDst((char *) txBuffer, config.payloadSizeBytes + sizeof(IperfUdpPkt_t));
    results.lastPktSeqNo = payloadPkt->seqNo;
  }
  else // If there's something in the req queue, send that now
  {
    uint16_t reqSeq = 0;
    int ret = SimpleQueue_Pop(&pktReqQueue, &reqSeq);
    payloadPkt->msgType = IPERF_PKT_RESP;
    payloadPkt->seqNo = reqSeq;
    payloadPkt->plSize = config.payloadSizeBytes;
    uint16_t charIdx = payloadPkt->seqNo * config.payloadSizeBytes;
    strncpy((char *) &payloadPkt->payload, IperfMessage_GetPointer(charIdx), config.payloadSizeBytes);
    loginfo("Serving request. Seq no %d\n", payloadPkt->seqNo);
    Iperf_SocklessUdpSendToDst((char *) txBuffer, config.payloadSizeBytes + sizeof(IperfUdpPkt_t));
  }

  results.numSentBytes += config.payloadSizeBytes;// + sizeof(IperfUdpPkt_t); // todo should or should not include metadata in our size sum? (4 bytes)
  if (isTransferDone())
  {
    loginfo("Sender done sending %d packets\n", config.numPktsToTransfer);
    if (config.mode < IPERF_MODE_CACHING_BIDIRECTIONAL)
    {
      senderState = SENDER_STOPPED;
      results.endTimestamp = ztimer_now(ZTIMER_USEC);
      loginfo("Stopping iperf\n");
    }
    else if (config.mode == IPERF_MODE_CACHING_BIDIRECTIONAL)
    {
      senderState = SENDER_IDLE;
      loginfo("Sitting idle\n");
    }
  }
  else
  {
    /*payloadPkt->seqNo++;*/
    ztimer_set_msg(ZTIMER_USEC, &intervalTimer, config.delayUs, &ipcMsg, senderPid);
  }
}

void *Iperf_SenderThread(void *arg)
{
  (void) arg;
  msg_t msg, reply;
  msg_init_queue(_msg_queue, IPERF_MSG_QUEUE_SIZE);

  reply.content.value = (uint32_t)(-ENOTSUP);
  reply.type = GNRC_NETAPI_MSG_TYPE_ACK;

  initSender();
  loginfo("Starting Sender Thread. Sitting Idle. Pid %d\n", senderPid);

  senderState = SENDER_IDLE;

  ipcMsg.type = IPERF_IPC_MSG_SEND_FILE;

  do {
    msg_receive(&msg);
    logdebug("Received type %d\n", msg.type);
    switch (msg.type) {
      case GNRC_NETAPI_MSG_TYPE_RCV:
        {
          logdebug("Data received\n");
          Iperf_PacketHandler(msg.content.ptr, (void *) senderHandleIperfPacket);
          break;
        }
      case GNRC_NETAPI_MSG_TYPE_SND:
        {
          logdebug("Data to send\n");
          /*Iperf_PacketHandler(msg.content.ptr, senderHandleIperfPacket);*/
          break;
        }
      case GNRC_NETAPI_MSG_TYPE_GET:
      case GNRC_NETAPI_MSG_TYPE_SET:
        {
          msg_reply(&msg, &reply);
          break;
        }
      case IPERF_IPC_MSG_START: // TODO am i adding complexity for no reason? 
        {
          loginfo("Sender received START command. Commencing iperf\n");
          senderState = SENDER_SENDING;
          results.startTimestamp = ztimer_now(ZTIMER_USEC);
          ztimer_set_msg(ZTIMER_USEC, &intervalTimer, 0, &ipcMsg, senderPid); // Start immediately
          break;
        }
      case IPERF_IPC_MSG_SEND_FILE:
        {
          handleFileSending();
          break;
        }
      case IPERF_IPC_MSG_STOP:
        {
          results.endTimestamp = ztimer_now(ZTIMER_USEC);
          senderState = SENDER_STOPPED;
          break;
        }
      default:
        logverbose("received something unexpected");
        break;
    }
  } while (senderState > SENDER_STOPPED);
  deinitSender();
  loginfo("Sender thread exiting\n");
  return NULL;
}
