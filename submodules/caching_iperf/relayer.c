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
#include "simple_queue.h"

#include "net/ipv6/hdr.h"
#include "net/ipv6/addr.h"

extern uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];
extern IperfConfig_s config;
extern ztimer_t intervalTimer;
extern msg_t ipcMsg;
extern SimpleQueue_t pktReqQueue; 
extern IperfResults_s results;

static msg_t _msg_queue[IPERF_MSG_QUEUE_SIZE];
static volatile kernel_pid_t relayerPid = KERNEL_PID_UNDEF;

static uint8_t *cacheBuffer = (uint8_t *) &rxtxBuffer;
static uint8_t cacheIdx = 0;

static gnrc_netreg_entry_t udpServer = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF); // TODO JON this can be generic, in iperf.c

#define CACHE_LOCK_SIZE_MAX 16 // TODO make generic no time aaaa
static bool cacheLock[CACHE_LOCK_SIZE_MAX];

static void initRelayer(void)
{
  relayerPid = thread_getpid();
  memset(cacheBuffer, 0x00, sizeof(uint8_t) * config.numCacheBlocks * config.payloadSizeBytes); // TODO THIS IS CRASHING OUT IF I MEMSET THE WHOLE BUFFER!!!!!
  memset(&cacheLock, 0x00, sizeof(bool) * CACHE_LOCK_SIZE_MAX);
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

static int sendCachedPkt(uint16_t cacheIdx)
{
  IperfUdpPkt_t *cached = (IperfUdpPkt_t *) (cacheBuffer + (cacheIdx * config.payloadSizeBytes));
  loginfo("Sending cached idx:%d (seq no %d) to destination\n", cacheIdx, cached->seqNo);
  cached->msgType = IPERF_PKT_RESP;
  cacheLock[cacheIdx] = false;
  return Iperf_SocklessUdpSendToDst((char *) (cacheBuffer + (cacheIdx * config.payloadSizeBytes)), config.payloadSizeBytes);
}

static inline bool coinFlip(uint8_t percent)
{
  if (rand() % 100 < percent)
  {
    return true;
  }
  return false;
}

static void cache(IperfUdpPkt_t *iperfPkt)
{
  loginfo("Caching seq no %d at cache index %d : %s\n", iperfPkt->seqNo, cacheIdx, iperfPkt->payload);
  if (cacheLock[cacheIdx])
  {
    // TODO SINCE SO FAR WE ASSUME A SINGLE CACHE SPACE THIS CAN STAY UNDONE
    loginfo("%d cache locked. Searching for a different cache space\n", cacheIdx);
    loginfo("TODO CACHE LOCK MECHANISM\n");
    return;
  }
  memcpy((uint8_t *) (cacheBuffer + (cacheIdx * config.payloadSizeBytes)), iperfPkt, sizeof(iperfPkt) + (config.payloadSizeBytes));
  cacheIdx = (cacheIdx + 1) % config.numCacheBlocks;
}

void Iperf_PrintCache(void)
{
  printf("Cache contents:\n");
  for (int i = 0; i < config.numCacheBlocks; i++)
  {
    IperfUdpPkt_t *p = (IperfUdpPkt_t *) (cacheBuffer + (i * config.payloadSizeBytes));
    if (p->msgType != IPERF_PAYLOAD && p->msgType != IPERF_PKT_RESP)
    {
      continue;
    }
    printf("[cache %d]:[chunk %d] %s\n", i, p->seqNo, p->payload);
  }
}

// Şüphesiz inkar edenler Zikr'i (Kur'-an'ı) duydukları zaman neredeyse seni gözleriyle devirecekler. (Senin için,) "Hiç şüphe yok o bir delidir" diyorlar. Halbuki o (Kur'an), âlemler için ancak bir öğüttür. 
// fhdjfhdjfdfkhdkjf
static int lookUpCachedPktPtr(uint16_t pktIdx)
{
  for (int i = 0; i < config.numCacheBlocks; i++)
  {
    IperfUdpPkt_t *p = (IperfUdpPkt_t *) (cacheBuffer + (i * config.payloadSizeBytes));
    if (p->msgType != IPERF_PAYLOAD && p->msgType != IPERF_PKT_RESP)
    {
      continue;
    }
    /*loginfo("Looking up %d : %d\n", pktIdx, p->seqNo);*/
    if (p->seqNo == pktIdx)
    {
      return i;
    }
  }
  return -1;
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
    loginfo("IPC Message type %x\n", msg.type);
    switch (msg.type) {
      case IPERF_IPC_MSG_RELAY_RESPOND: // IF relayer needs to do something instead of simply forwarding
      {
        loginfo("RELAYER RESPONSE\n");
        sendPayload();
        break;
      }
      case IPERF_IPC_MSG_RELAY_SERVICE_INTEREST:
      {
        loginfo("RELAYER SERVICING INTEREST\n");
        uint16_t cacheIdxToSend;
        int ret = SimpleQueue_Pop(&pktReqQueue, &cacheIdxToSend);
        if (ret)
        {
          logdebug("Queue returned 1 %d\n", __LINE__);
          return;
        }
        sendCachedPkt(cacheIdxToSend);
        if (!SimpleQueue_IsEmpty(&pktReqQueue))
        {
          msg_t ipc;
          ipc.type = IPERF_IPC_MSG_RELAY_SERVICE_INTEREST;
          ztimer_set_msg(ZTIMER_USEC, &intervalTimer, config.delayUs, &ipc, relayerPid);
        }
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
  bool shouldForward = true;

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
  else if (iperfPkt->msgType == IPERF_CONFIG_SYNC)
  {
    Iperf_HandleConfigSync(iperfPkt);
  }
  else if (iperfPkt->msgType == IPERF_PAYLOAD || iperfPkt->msgType == IPERF_PKT_RESP)
  {
    if (config.cache && coinFlip(config.cacheChancePercent))
    {
      logdebug("Payload seq %d intercepted. Will cache\n", iperfPkt->seqNo);
      cache(iperfPkt);
      if (logprintTags[DEBUG]) Iperf_PrintCache();

      /*if (coinFlip(50)) // TODO testing purposes*/
      /*{*/
      /*  shouldForward = false;*/
      /*}*/
    } 

    // CACHING SERVICING TEST
    /*if (iperfPkt->seqNo > 6)*/
    /*{*/
    /*  shouldForward = false;*/
    /*}*/
  }
  else if (iperfPkt->msgType == IPERF_PKT_REQ)
  {
    if (config.cache)
    {
    }
  }
  else if (iperfPkt->msgType == IPERF_PKT_BULK_REQ)
  {
    /*
    * BULK REQUEST INTERCEPTED:
    * - Go thru the requested pkts in the request.
    * - Check if any are in our cache. 
    *   - If so, remove that from the interest and add it to our service queue
    *   - If not, simply forward
    */
    if (config.cache)
    {
      IperfBulkInterest_t *bulkInterest = (IperfBulkInterest_t *) iperfPkt->payload;
      uint8_t numExpects = bulkInterest->len;
      uint16_t *expectArr = bulkInterest->arr;
      uint16_t numBadExpectsOrCacheHits = 0;
      logdebug("Intercepted bulk interest for %d chunks\n", numExpects);
      bool shouldSendIpc = false;
      for (int i = 0; i < numExpects; i++)
      {
        if (expectArr[i] == SIMPLE_QUEUE_INVALID_NUMBER)
        {
          numBadExpectsOrCacheHits++;
          continue;
        }
        int cachedPktIdx = lookUpCachedPktPtr(expectArr[i]);
        if (cachedPktIdx > -1)
        {
          // CACHE HIT
          // Remove interest from bulk interest, put it in our service list
          loginfo("Cache hit! Seq no %d at cache idx %d\n", expectArr[i], cachedPktIdx);
          expectArr[i] = SIMPLE_QUEUE_INVALID_NUMBER;
          cacheLock[cachedPktIdx] = true;
          SimpleQueue_Push(&pktReqQueue, cachedPktIdx);
          shouldSendIpc = true;
          results.cacheHits++;
          numBadExpectsOrCacheHits++;
        }

        if (logprintTags[DEBUG])
          printf("%d ", expectArr[i]);
      }
      if (logprintTags[DEBUG])
        printf("\n");

      if (shouldSendIpc)
      {
        logverbose("Sending IPC\n");
        msg_t ipc;
        ipc.type = IPERF_IPC_MSG_RELAY_SERVICE_INTEREST;
        msg_send(&ipc, relayerPid);
      }
      
      // TODO TODO NEED TO REORDER THESE SINCE ONCE YOU REMOVE ONE EXPECTATION
      if (numExpects == numBadExpectsOrCacheHits)
      {
        logdebug("Won't forward this bulk interest since every interest in it is serviced!\n");
        shouldForward = false;
      }
    }
  }
  return shouldForward;
}
