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
#include "xor_coding.h"
#include "receiver.h"

// TODO clean all this. there'll be a version 3 deffo

extern IperfResults_s results;
extern IperfConfig_s config;
extern uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];
extern IperfChunkStatus_e receivedPktIds[IPERF_TOTAL_TRANSMISSION_SIZE_MAX];
extern char receiveFileBuffer[IPERF_TOTAL_TRANSMISSION_SIZE_MAX];
extern char dstGlobalIpAddr[25];
extern char srcGlobalIpAddr[25];
extern msg_t ipcMsg;
extern ztimer_t intervalTimer;
extern IperfThreadState_e iperfState;

extern uint16_t pktReqQueueBuffer[];
extern SimpleQueue_t pktReqQueue; // TODO perhaps this is unnecessary. could simply go thru the unacquired packets list and send requests?

static msg_t expectationMsg; // TODO may not need all these tbh
static msg_t interestMsg;  // TODO try these without a message object for each object
static ztimer_t expectationTimer;
static ztimer_t interestTimer;
static uint16_t numExpectedPkts = 0;
static uint16_t expectationSeqNo = 0;

static msg_t _msg_queue[IPERF_MSG_QUEUE_SIZE];

static kernel_pid_t receiverPid = KERNEL_PID_UNDEF;
static gnrc_netreg_entry_t udpServer = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF);

static bool isTransferDone(void)
{
  return (config.numPktsToTransfer == results.receivedUniqueChunks);
}

// TODO below functions can be macros or consolidated
static void startExpectationTimer(uint32_t timeoutUs)
{
  if (config.mode < IPERF_MODE_SIMPLE_CACHING) // TODO we shouldnt even get here. fix in v3
  {
    return;
  }
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

static inline void restartExpectationTimer(void)
{
  if (config.mode < IPERF_MODE_SIMPLE_CACHING)
  {
    return;
  }
  stopExpectationTimer();
  startExpectationTimer(config.expectationDelayUs);
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

static inline void restartInterestTimer(void)
{
  stopInterestTimer();
  startInterestTimer(config.interestDelayUs);
}

static int copyPayloadString(IperfUdpPkt_t *pkt)
{
  // The text is divided into $numPktsToTransfer chunks. This is chunk no $seqNo
  uint16_t offset = pkt->seqNo * pkt->plSize;
  strncpy(&receiveFileBuffer[offset], pkt->payload, pkt->plSize);
  logdebug("Copying contents seqno: %d, offset: %d\n", pkt->seqNo, offset);
  /*Iperf_PrintFileContents();*/
  return 0;
}

static bool checkForCompletionAndTransition(void)
{
  logdebug("Receiver checking for completion. Expecting %d Received %d packets\n", \
          config.numPktsToTransfer, results.receivedUniqueChunks);
  
  for (int i = 0; i < config.numPktsToTransfer; i++)
  {
    if (receivedPktIds[i] != RECEIVED)
    {
      logdebug("We're not done!\n");
      // We're not done
      return false;
    }
  }
  // We're done. send done message
  msg_t m; 
  // m.type = IPERF_IPC_MSG_STOP;
  m.type = IPERF_IPC_MSG_IDLE;
  msg_send(&m, receiverPid);
  logdebug("We're done!\n");
  return true;
}

// Checks the received packets. If all of the ones from the first IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS amount are done, 
// increments the offset by one, and so on. returns offset
static uint8_t getCurrentOffset(void)
{
  uint8_t offset = 0;
  uint16_t i;
  for (i = 0; i < numExpectedPkts; i++)
  {
    if (receivedPktIds[i] != RECEIVED)
    {
      break;
    }
  }
  offset = i / IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS;
  return offset;
}

// If successful, returns the idx of the packet we just decoded by handling the newly
// received coded payload. This function does the decoding and saving to our rx buffer. 
// Doesnt change the results structure, that needs to be done outside.
static int handleCodedPayload(IperfCodedPayloadPkt_t *p)
{
  logdebug("%s: ", __FUNCTION__);
  if (logprintTags[DEBUG]) Iperf_PrintCatalogueVector((IperfCatalogueVector_t *) p);
  
  // Figure out which chunk can be acquired thru the decoding of this newly acquired coded payload
  uint8_t currentOffset = getCurrentOffset();
  uint8_t receivedOffset = p->pktOffset;

  // Sanity check if the received coded payload has the same offset as us
  // if (p->pktOffset != currentOffset)
  // {
  //   logerror("ERROR: Received coded payload has different offset %d != %d\n", p->pktOffset, currentOffset);
  //   return -1;
  // }

  uint32_t Cbefore = Iperf_GetCatalogueVector((IperfChunkStatus_e *) &receivedPktIds, receivedOffset);
  uint32_t R = * (uint32_t *) (p->bitmap);
  uint32_t Cafter = Cbefore ^ R;
  uint32_t Cdecodable = (~Cbefore) & Cafter;
  uint32_t Cdependent = (~Cafter) & Cbefore;

  // This may be an uncoded packet. if there exists no dependent chunk thats what it means. if so, directly copy over the contents
  if (XorCoding_IsPowerOfTwo(R))
  {
    logdebug("Received uncoded data 0x%08x : %s\n", R, p->payload);
    uint32_t receivedChunkIdx;
    for (int i = 0; i < 32; i++)
    {
      if ((R & (1<<i)) > 0)
      {
        receivedChunkIdx = (receivedOffset * IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS) + i;
      }
    }
    memcpy(&receiveFileBuffer[receivedChunkIdx * config.payloadSizeBytes], p->payload, config.payloadSizeBytes);
    return receivedChunkIdx;
  }

  // If Cdecodable isnt a power of 2 that means the result is not one uncoded chunk
  // i.e. we cant decode this packet successfully
  if (!XorCoding_IsPowerOfTwo(Cdecodable))
  {
    logerror("ERROR: We received an undecodable coded payload Cdecodable 0x%08x\n", Cdecodable);
    return -1;
  }

  // Figure out which chunk needs to be xor'd with it to do the decoding
  uint32_t dependentChunkIdx; // This chunk will be used to decode the resulting chunk
  uint32_t decodableChunkIdx; // this chunk is the resulting chunk
  for (int i = 0; i < 32; i++)
  {
    if ((Cdecodable & (1<<i)) > 0)
    {
      decodableChunkIdx = i + (receivedOffset * IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS);
      break;
    }
  }

  for (int i = 0; i < 32; i++)
  {
    if ((Cdependent & (1<<i)) > 0)
    {
       dependentChunkIdx = i + (receivedOffset * IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS);
    }
  }
  logdebug("Decodable Chunk Idx %d, Needed Chunk Idx %d\n", decodableChunkIdx, dependentChunkIdx);

  // check if we even have the dependent chunk. We should, if we dont thats an error
  // if (receivedPktIds[dependentChunkIdx] != RECEIVED)
  // {
  //   logerror("ERROR: For some reason we havent recevied the dependent chunk. decodable %d dependent %d\n", decodableChunkIdx, dependentChunkIdx);
  //   return -1;
  // }

  // JON TODO below function can be generalized and moved elsewhere
  // Do the decoding. current implementation is thru XOR
  // for each byte pl[x] in the payload, xor it with the dependent byte dep[x] and put the resultin buffer[x]
  for (int i = 0; i < config.payloadSizeBytes; i++)
  {
    uint16_t decodableByteIdx = (receivedOffset * IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS) + i + config.payloadSizeBytes * decodableChunkIdx;
    uint16_t dependentByteIdx = (receivedOffset * IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS) + i + config.payloadSizeBytes * dependentChunkIdx;
    logdebug("offset %d byteIdx %d result (0x%02x) ^ (%c 0x%02x) = (%c 0x%02x)\n", receivedOffset, 
             decodableByteIdx, 
             p->payload[i],
             receiveFileBuffer[dependentByteIdx],
             receiveFileBuffer[dependentByteIdx],
             p->payload[i] ^ receiveFileBuffer[dependentByteIdx],
             p->payload[i] ^ receiveFileBuffer[dependentByteIdx]);
    receiveFileBuffer[decodableByteIdx] = p->payload[i] ^ receiveFileBuffer[dependentByteIdx];
  }
  results.numPktDecodes++;

  return decodableChunkIdx;
}

static int receiverHandleIperfPacket(gnrc_pktsnip_t *pkt)
{
  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) pkt;

  if (iperfPkt->msgType >= IPERF_CTRL_MSG_MAX)
  {
    logerror("Receiver received bad msg_type %x\n", iperfPkt->msgType);
    return 1;
  }

  logdebug("Received Iperf Pkt: Type %d\n", iperfPkt->msgType);

  restartExpectationTimer();
  
  switch (iperfPkt->msgType)
  {
    case IPERF_PAYLOAD:
      {
        /*if (coinFlip(5))*/
        /*{*/
        /*  return 0;*/
        /*}*/

        logdebug("[IPERF_PAYLOAD] %d received\n", iperfPkt->seqNo);

        // If it's the first payload packet we receive, ////
        if (iperfState == IPERF_STATE_IDLE)
        {
          logdebug("Received first packet. State IPERF_STATE_RECEIVING\n");

          Iperf_ResetResults();
          iperfState = IPERF_STATE_RECEIVING; // TODO see if this logic is needed
          
          // Start our expectation timer
          startExpectationTimer(config.expectationDelayUs);
        }
        
        // handle packet seq no
        if (results.lastPktSeqNo == iperfPkt->seqNo - 1)
        {
          // GOOD RX //////////////////////////////////////
          results.lastPktSeqNo = iperfPkt->seqNo;
          logdebug("RX %d\n", iperfPkt->seqNo);
          results.receivedUniqueChunks++;
          copyPayloadString(iperfPkt);
          restartExpectationTimer();
        }
        else if (receivedPktIds[iperfPkt->seqNo] == RECEIVED)
        {
          // DUPLICATE PKT ////////////////////////////////
          results.numDuplicates++; 
          logdebug("DUP %d\n", iperfPkt->seqNo);
        }
        else if (results.lastPktSeqNo < iperfPkt->seqNo)
        {
          // OUT OF ORDER (LOSS) //////////////////////////
          uint16_t lostPkts = (iperfPkt->seqNo - results.lastPktSeqNo);
          // Should we send an interest as soon as we detect a loss? 
          
          if (config.mode == IPERF_MODE_SIMPLE_CACHING) // This is a feature of the SIMPLE_CACHING mode. if we get OoO packets, add the unreceived ones to our interest queue
          {
            for (uint16_t i = results.lastPktSeqNo + 1; i < iperfPkt->seqNo; i++)
            {
              logdebug("Lost packet %d adding to request queue\n", i);
              SimpleQueue_Push(&pktReqQueue, i);
              receivedPktIds[i] = REQUESTED;
            }
            startInterestTimer(config.interestDelayUs);
          }

          results.pktLossCounter += lostPkts;
          results.lastPktSeqNo = iperfPkt->seqNo;
          results.receivedUniqueChunks++;
          copyPayloadString(iperfPkt);
          restartExpectationTimer();
          logdebug("LOSS %d pkts. Current Last Pkt %d \n", lostPkts, iperfPkt->seqNo);
        }

        // Tally
        if (iperfPkt->seqNo < IPERF_TOTAL_TRANSMISSION_SIZE_MAX)
        {
          receivedPktIds[iperfPkt->seqNo] = RECEIVED;
        }
        else
        {
          logerror("%s:%d Out of bounds sequence number!! %d\n", __FUNCTION__, __LINE__, iperfPkt->seqNo);
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
        logerror("[IPERF_PKT_REQ] %d received. Wrong role!\n", iperfPkt->seqNo);
        break;
      }
    case IPERF_PKT_RESP:
      {
        if (iperfState != IPERF_STATE_RECEIVING) // This could be a test interest service
        {
          logdebug("[IPERF_PKT_RESP] %d received\n", iperfPkt->seqNo);
          break;
        }
        
        if (iperfPkt->seqNo < IPERF_TOTAL_TRANSMISSION_SIZE_MAX && receivedPktIds[iperfPkt->seqNo] != RECEIVED)
        {
          logdebug("[IPERF_PKT_RESP] %d received\n", iperfPkt->seqNo);
          receivedPktIds[iperfPkt->seqNo] = RECEIVED;
          results.receivedUniqueChunks++;
          results.numReceivedPkts++;
          results.endTimestamp = ztimer_now(ZTIMER_USEC);
          copyPayloadString(iperfPkt);
          restartExpectationTimer();
          //Iperf_PrintFileTransferStatus();
        }
        else if (iperfPkt->seqNo < IPERF_TOTAL_TRANSMISSION_SIZE_MAX && receivedPktIds[iperfPkt->seqNo] == RECEIVED)
        {
          results.numDuplicates++;
          results.numReceivedPkts++;
          results.endTimestamp = ztimer_now(ZTIMER_USEC);
          restartExpectationTimer();
        }
        else
        {
          logerror("%s:%d Out of bounds sequence number!! %d\n", __FUNCTION__, __LINE__, iperfPkt->seqNo);
        }
        checkForCompletionAndTransition();
        break;
      }
    case IPERF_PKT_CODED_DATA:
      {
        IperfCodedPayloadPkt_t *coded = (IperfCodedPayloadPkt_t *) iperfPkt->payload;
        int decodableChunkIdx = handleCodedPayload(coded);
        logdebug("[IPERF_PKT_CODED_DATA] %d received\n", decodableChunkIdx);
        Iperf_PrintCatalogueVector((IperfCatalogueVector_t *) iperfPkt->payload);
        results.numReceivedPkts++;
        
        if (decodableChunkIdx < 0)
        {
          logerror("%s:%d decodableChunkIdx < 0\n", __FUNCTION__, __LINE__);
          break;
        }

        if (receivedPktIds[decodableChunkIdx] != RECEIVED)
        {
          receivedPktIds[decodableChunkIdx] = RECEIVED;
          results.receivedUniqueChunks++;
          results.endTimestamp = ztimer_now(ZTIMER_USEC);
          checkForCompletionAndTransition();
          // results.cacheHits++;
        }

        restartExpectationTimer();
        break;
      }
    case IPERF_ECHO_CALL:
      {
        loginfo("[IPERF_ECHO_CALL] Received %s\n", iperfPkt->payload);
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
        loginfo("[IPERF_ECHO_RESP] Received %s\n", iperfPkt->payload);
        break;
      }
    case IPERF_CONFIG_SYNC:
      {
        loginfo("[IPERF_CONFIG_SYNC] Received\n");
        Iperf_HandleConfigSync(iperfPkt);
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

void *Iperf_ReceiverThread(void *arg)
{
  (void) arg;
  msg_t msg, reply;
  msg_init_queue(_msg_queue, IPERF_MSG_QUEUE_SIZE);

  reply.content.value = (uint32_t)(-ENOTSUP);
  reply.type = GNRC_NETAPI_MSG_TYPE_ACK;

  receiverPid = thread_getpid();
  Iperf_StartUdpServer(&udpServer, receiverPid);
  loginfo("Starting Receiver Thread. Sitting Idle. Pid %d\n", receiverPid);
  iperfState = IPERF_STATE_IDLE;

  while (iperfState > IPERF_STATE_STOPPED) {
    msg_receive(&msg);
    logdebug("IPC Message type %x\n", msg.type);
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
          // Interest timeouts fire out bulk interests to queued up interested packets
          // Queueing is done by the expectation timeouts
          // If this is a cached coding run, interests mean sending catalogue vectors
          if (config.mode == IPERF_MODE_SIMPLE_CACHING) // legacy
          {
            if (SimpleQueue_IsEmpty(&pktReqQueue))
            {
              logerror("Interest timer time out but queue is empty!\n");
              break;
            }

            // If this is the caching only version, send the bulk interest
            uint16_t expectArr[IPERF_MAX_PKTS_IN_ONE_BULK_REQ];
            uint16_t expectArrIdx = 0;

            logdebug("Send Req for :");
            for (expectArrIdx = 0; expectArrIdx < IPERF_MAX_PKTS_IN_ONE_BULK_REQ; expectArrIdx++)
            {
              if (SimpleQueue_IsEmpty(&pktReqQueue))
              {
                break;
              }
              int ret = SimpleQueue_Pop(&pktReqQueue, &(expectArr[expectArrIdx]));
              if (logprintTags[DEBUG]) printf("%d ", expectArr[expectArrIdx]);
            }
            if (logprintTags[DEBUG]) printf(" | %d chunks \n", expectArrIdx);
            Iperf_SendBulkInterest((uint16_t *) &expectArr, expectArrIdx);

            if (!SimpleQueue_IsEmpty(&pktReqQueue))
            {
              startInterestTimer(config.interestDelayUs);
            }
          }
          else if (config.mode == IPERF_MODE_CODED_CACHING)
          {
            // If this is caching coding we dont use this queue anyway
            // while (!SimpleQueue_IsEmpty(&pktReqQueue)) 
            // {
            //   SimpleQueue_Pop(&pktReqQueue, NULL);
            // }

            bool expecting = false;
            uint8_t earliestExpectedOffset = 0;
            for (uint16_t i = 0; i < expectationSeqNo; i++)
            {
              if (receivedPktIds[i] != RECEIVED)
              {
                earliestExpectedOffset = i / IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS;
                expecting = true;
                break;
              }
            }
            
            // Only send the catalogue if we're expecting chunks
            if (expecting)
            {
              Iperf_SendCatalogueVector((IperfChunkStatus_e *) &receivedPktIds, earliestExpectedOffset);
            }
          }
          break;
        }
      case IPERF_IPC_MSG_EXPECTATION_TIMEOUT:
        {
          // Expectation timeouts fill up our Interest queue
          // and starts the interest timer.
          // In CodedCaching mode we dont have to do any enqueueing or anything. 
          // we should just send out the catalogue

          logdebug("Expectation timeout\n");
          if (expectationSeqNo < results.lastPktSeqNo) // Bump expectation index
          {
            expectationSeqNo = results.lastPktSeqNo+1;
          }
          else if (expectationSeqNo < config.numPktsToTransfer)
          {
            expectationSeqNo++;
          }

          logdebug("Expecting up to %d: ", expectationSeqNo);

          bool expecting = false;
          for (uint16_t i = 0; i < expectationSeqNo; i++)
          {
            if (receivedPktIds[i] == RECEIVED || SimpleQueue_IsEnqueued(&pktReqQueue, i))
            {
              continue;
            }
            else
            {
              if (logprintTags[DEBUG]) printf("%d ", i);
              
              if (config.mode == IPERF_MODE_SIMPLE_CACHING) // TODO honestly just remove the old caching code
              {
                SimpleQueue_Push(&pktReqQueue, i);
                receivedPktIds[i] = EXPECTED;
              }
              expecting = true;
              /*SimpleQueue_PrintQueue(&pktReqQueue);*/
            }
          }
          if (logprintTags[DEBUG]) printf("\n");

          if (expecting)
          {
            startInterestTimer(config.interestDelayUs);
          }

          if (!checkForCompletionAndTransition())
          {
            startExpectationTimer(config.expectationDelayUs);
          }
          break;
        }
      case IPERF_IPC_MSG_STOP:
        {
          loginfo("Receiver stopping\n");
          iperfState = IPERF_STATE_STOPPED;
          stopExpectationTimer();
          stopInterestTimer();
          break;
        }
      case IPERF_IPC_MSG_IDLE: // FOR TEST PURPOSES
        {
          loginfo("Receiver going into idle mode\n");
          iperfState = IPERF_STATE_IDLE;
          stopExpectationTimer();
          stopInterestTimer();
          uint32_t usecs = (results.endTimestamp - results.startTimestamp);
          loginfo("Receiver thread exiting. Transfer complete in %d useconds\n", usecs);
          break;
        }
      default:
        // loginfo("received something unexpected");
        break;
    }
  }

  expectationSeqNo = 0; // TODO put this in a cleanup function
  Iperf_StopUdpServer(&udpServer);
  uint32_t usecs = (results.endTimestamp - results.startTimestamp);
  loginfo("Receiver thread exiting. Transfer complete in %d useconds\n", usecs);
}
