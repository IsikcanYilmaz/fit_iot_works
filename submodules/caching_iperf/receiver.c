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
#include "simple_queue.h"

#include "receiver.h"

typedef enum
{
  RECEIVER_STOPPED,
  RECEIVER_IDLE,
  RECEIVER_RECEIVING,
  RECEIVER_STATE_MAX
} IperfReceiverState_e;

// TODO clean all this. there'll be a version 3 deffo

extern IperfResults_s results;
extern IperfConfig_s config;
extern uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];
extern bool receivedPktIds[IPERF_TOTAL_TRANSMISSION_SIZE_MAX];
extern char receiveFileBuffer[IPERF_TOTAL_TRANSMISSION_SIZE_MAX];
extern char dstGlobalIpAddr[25];
extern char srcGlobalIpAddr[25];
extern msg_t ipcMsg;
extern ztimer_t intervalTimer;

static msg_t expectationMsg;
static msg_t interestMsg;
static ztimer_t expectationTimer;
static ztimer_t interestTimer;

extern uint16_t pktReqQueueBuffer[];
extern SimpleQueue_t pktReqQueue;

static uint8_t *txBuffer = (uint8_t *) &rxtxBuffer;
static msg_t _msg_queue[IPERF_MSG_QUEUE_SIZE];

static IperfReceiverState_e receiverState = RECEIVER_STOPPED;
static kernel_pid_t receiverPid = KERNEL_PID_UNDEF;
static gnrc_netreg_entry_t udpServer = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF);

static int requestPacket(uint16_t seqNo)
{
  char rawPkt[20];
  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) &rawPkt;
  IperfPacketRequest_t *pktReqPl = (IperfPacketRequest_t *) &iperfPkt->payload;
  memset(&rawPkt, 0x00, sizeof(rawPkt));
  iperfPkt->msgType = IPERF_PKT_REQ;
  pktReqPl->seqNo = seqNo;
  return Iperf_SocklessUdpSendToSrc((char *) &rawPkt, sizeof(rawPkt));
}

static bool isTransferDone(void)
{
  return (config.numPktsToTransfer == results.receivedUniqueChunks);
}

// TODO below functions can be macros or consolidated
static void startExpectationTimer(uint32_t timeoutUs)
{
  if (ztimer_is_set(ZTIMER_USEC, &expectationTimer))
  {
    logdebug("Expectation timer already set\n");
    return;
  }
  expectationMsg.type = IPERF_IPC_MSG_EXPECTATION_TIMEOUT;
  ztimer_set_msg(ZTIMER_USEC, &expectationTimer, timeoutUs, &expectationMsg, receiverPid);
}

static void stopExpectationTimer(void)
{
  if (ztimer_is_set(ZTIMER_USEC, &expectationTimer))
  {
    ztimer_remove(ZTIMER_USEC, &expectationTimer);
  }
}

static void startInterestTimer(uint32_t timeoutUs)
{
  if (ztimer_is_set(ZTIMER_USEC, &interestTimer))
  {
    logdebug("Interest timer already set\n");
    return;
  }
  interestMsg.type = IPERF_IPC_MSG_INTEREST_TIMER_TIMEOUT;
  ztimer_set_msg(ZTIMER_USEC, &interestTimer, timeoutUs, &interestMsg, receiverPid);
}

static void stopInterestTimer(void)
{
  if (ztimer_is_set(ZTIMER_USEC, &interestTimer))
  {
    ztimer_remove(ZTIMER_USEC, &interestTimer);
  }
}
//

static bool checkForCompletionAndTransition(void)
{
  loginfo("Receiver checking for completion. Expecting %d Received %d packets\n", \
          config.numPktsToTransfer, results.receivedUniqueChunks);
  if (config.numPktsToTransfer == results.receivedUniqueChunks)
  {
    // We're done. send done message
    msg_t m;
    m.type = IPERF_IPC_MSG_STOP;
    msg_send(&m, receiverPid);
    return true;
  }
  else
  {
    // We're not done
    return false;
  }
}

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
        // If it's the first payload packet we receive, ////
        if (receiverState == RECEIVER_IDLE)
        {
          logverbose("Received first packet. State RECEIVER_RECEIVING\n");
          receiverState = RECEIVER_RECEIVING; // TODO see if this logic is needed
          
          // Start our expectation timer
          startExpectationTimer(1000000);
        }
        
        // handle packet seq no
        if (results.lastPktSeqNo == iperfPkt->seqNo - 1)
        {
          // GOOD RX //////////////////////////////////////
          results.lastPktSeqNo = iperfPkt->seqNo;
          logverbose("RX %d\n", iperfPkt->seqNo);
          results.receivedUniqueChunks++;
        }
        else if (receivedPktIds[iperfPkt->seqNo])
        {
          // DUPLICATE PKT ////////////////////////////////
          results.numDuplicates++; 
          logverbose("DUP %d\n", iperfPkt->seqNo);
        }
        else if (results.lastPktSeqNo < iperfPkt->seqNo)
        {
          // OUT OF ORDER (LOSS) //////////////////////////
          uint16_t lostPkts = (iperfPkt->seqNo - results.lastPktSeqNo);
          if (config.mode == IPERF_MODE_CACHING_BIDIRECTIONAL)
          {
            for (uint16_t i = results.lastPktSeqNo + 1; i < iperfPkt->seqNo; i++)
            {
              loginfo("Lost packet %d adding to request queue\n", i);
              SimpleQueue_Push(&pktReqQueue, i);
            }
            startInterestTimer(1000000);
          }
          results.pktLossCounter += lostPkts;
          results.lastPktSeqNo = iperfPkt->seqNo;
          results.receivedUniqueChunks++;
          logverbose("LOSS %d pkts. Current Last Pkt %d \n", lostPkts, iperfPkt->seqNo);
        }

        // Tally
        if (iperfPkt->seqNo < IPERF_TOTAL_TRANSMISSION_SIZE_MAX)
        {
          receivedPktIds[iperfPkt->seqNo] = true;
        }
        else
        {
          logerror("Out of bounds sequence number!! %d\n", iperfPkt->seqNo);
        }

        results.numReceivedPkts++;
        results.endTimestamp = ztimer_now(ZTIMER_USEC);
        if (results.numReceivedPkts == 1)
        {
          results.startTimestamp = results.endTimestamp;
        }

        checkForCompletionAndTransition();
        break;
      }
    case IPERF_PKT_REQ:
      {
        logerror("PKT_REQ %d received. Wrong role!\n", iperfPkt->seqNo);
        break;
      }
    case IPERF_PKT_RESP:
      {
        loginfo("PKT_RESP %d received\n", iperfPkt->seqNo);
        if (iperfPkt->seqNo < IPERF_TOTAL_TRANSMISSION_SIZE_MAX)
        {
          receivedPktIds[iperfPkt->seqNo] = true;
          results.receivedUniqueChunks++;
          Iperf_PrintFileTransferStatus();
        }
        else
        {
          logerror("Out of bounds sequence number!! %d\n", iperfPkt->seqNo);
        }
        checkForCompletionAndTransition();
        break;
      }
    case IPERF_ECHO_CALL:
      {
        loginfo("Echo CALL Received %s\n", iperfPkt->payload);
        char rawPkt[20]; 
        IperfUdpPkt_t *respPkt = (IperfUdpPkt_t *) &rawPkt;
        uint8_t plSize = 16;
        memset(&respPkt->payload, 0x00, 16);
        strncpy((char *) respPkt->payload, iperfPkt->payload, 16);
        respPkt->seqNo = 0;
        respPkt->msgType = IPERF_ECHO_RESP;
        Iperf_SocklessUdpSendToSrc((char *) &rawPkt, sizeof(rawPkt));
        break;
      }
    case IPERF_ECHO_RESP:
      {
        loginfo("Echo RESP Received %s\n", iperfPkt->payload);
        break;
      }
    case IPERF_CONFIG_SYNC:
      {
        loginfo("Config pkt received\n");
        IperfConfigPayload_t *configPl = (IperfConfigPayload_t *) iperfPkt->payload;
        config.mode = configPl->mode;
        config.payloadSizeBytes = configPl->payloadSizeBytes;
        config.delayUs = configPl->delayUs;
        config.transferSizeBytes = configPl->transferSizeBytes;
        config.numPktsToTransfer = config.transferSizeBytes / config.payloadSizeBytes;
        Iperf_PrintConfig(false);
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
  receiverState = RECEIVER_IDLE;

  while (receiverState > RECEIVER_STOPPED) {
    msg_receive(&msg);
    logdebug("IPC Message type %d\n", msg.type);
    switch (msg.type) {
      case GNRC_NETAPI_MSG_TYPE_RCV:
        {
          logdebug("Data received\n");
          Iperf_PacketHandler(msg.content.ptr, (void *) receiverHandleIperfPacket);
          break;
        }
      case GNRC_NETAPI_MSG_TYPE_SND:
        {
          logdebug("Data to send\n");
          Iperf_PacketHandler(msg.content.ptr, (void *) receiverHandleIperfPacket);
          break;
        }
      case GNRC_NETAPI_MSG_TYPE_GET:
      case GNRC_NETAPI_MSG_TYPE_SET:
        {
          msg_reply(&msg, &reply);
          break;
        }
      case IPERF_IPC_MSG_INTEREST_TIMER_TIMEOUT:
        {
          uint16_t seqNo = 0;
          int qret = SimpleQueue_Pop(&pktReqQueue, &seqNo);
          loginfo("Send Req for seq no %d\n", seqNo);
          requestPacket(seqNo);
          if (!SimpleQueue_IsEmpty(&pktReqQueue))
          {
          /*  ipcMsg.type = IPERF_INTEREST_TIMER_TIMEOUT;*/
          /*  ztimer_set_msg(ZTIMER_USEC, &interestTimer, 1000000, &ipcMsg, receiverPid);*/
            startInterestTimer(1000000);
          }
          break;
        }
      case IPERF_IPC_MSG_EXPECTATION_TIMEOUT:
        {
          loginfo("Expectation timeout\n");
          if (!checkForCompletionAndTransition())
          {
            startExpectationTimer(1000000);
          }
          break;
        }
      case IPERF_IPC_MSG_STOP:
        {
          loginfo("Receiver stopping\n");
          receiverState = RECEIVER_STOPPED;
          stopExpectationTimer();
          stopInterestTimer();
          break;
        }
      default:
        /*loginfo("received something unexpected");*/
        break;
    }
  }
  Iperf_StopUdpServer(&udpServer);
  loginfo("Receiver thread exiting\n");
}
