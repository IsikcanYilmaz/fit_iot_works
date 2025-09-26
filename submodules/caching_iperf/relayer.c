#include <stdint.h>
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

static uint8_t *cacheBuffer = (uint8_t *) &rxtxBuffer[0];
static uint8_t cacheIdx = 0;

static gnrc_netreg_entry_t udpServer = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF); // TODO JON this can be generic, in iperf.c

#define CACHE_LOCK_SIZE_MAX 16 // TODO make generic no time aaaa
static bool cacheLock[CACHE_LOCK_SIZE_MAX]; // theres a time gap between a cache hit being noticed and that block being sent off. this lock makes sure that cache block stays until the block is sent off

#define CACHE_BLOCK_SIZE (sizeof(IperfUdpPkt_t) + config.payloadSizeBytes)
#define CODED_CACHE_BLOCK_SIZE (sizeof(IperfUdpPkt_t) + sizeof(IperfCodedPayloadPkt_t) + config.payloadSizeBytes)

#define TIME_CACHING 0
#define PRINT_TIME_CACHING 0

#define CHANCE_TO_DROP 30
#define DROP_EVEN_NUMBEREDS

#if TIME_CACHING
uint32_t sumTimeTakenForCaching = 0;
uint32_t avgTimeTakenForCaching = 0;
uint32_t cachingCtr = 0;

uint32_t sumTimeTakenForLookups = 0;
uint32_t avgTimeTakenForLookups = 0;
uint32_t lookupCtr = 0;

#endif

#if DEMO_CONFIG
uint16_t cacheHitQueueBuffer[16];
SimpleQueue_t cacheHitQueue; 
#endif

static void initRelayer(void)
{
  relayerPid = thread_getpid();
  cacheBuffer = (uint8_t *) &rxtxBuffer[0];
  if (IPERF_BUFFER_SIZE_BYTES < sizeof(uint8_t) * config.numCacheBlocks * CACHE_BLOCK_SIZE)
  {
    logerror("WARNING!\nrxtxBuffer will overflow!!!\n%d < %d\ncachebuffer %d bytes, %d num blocks, %d num bytes per block!\n", \
             IPERF_BUFFER_SIZE_BYTES, sizeof(uint8_t) * config.numCacheBlocks * CACHE_BLOCK_SIZE, \
             IPERF_BUFFER_SIZE_BYTES, config.numCacheBlocks, CACHE_BLOCK_SIZE);
  }
  memset(rxtxBuffer, 0x00, sizeof(uint8_t) * config.numCacheBlocks * CODED_CACHE_BLOCK_SIZE); // TODO THIS IS CRASHING OUT IF I MEMSET THE WHOLE BUFFER!!!!!
  memset(&cacheLock, 0x00, sizeof(bool) * CACHE_LOCK_SIZE_MAX);

  if (config.mode == IPERF_MODE_CACHING_BIDIRECTIONAL)
  {
    // TODO? too many todos
  }
  else if (config.mode == IPERF_MODE_CACHING_CODING)
  {
    for (int i = 0; i < config.numCacheBlocks; i++)
    {
      IperfUdpPkt_t *p = (IperfUdpPkt_t *) (cacheBuffer + (i * CODED_CACHE_BLOCK_SIZE)); 
      printf("%d->%x\n" , i, p);
      p->msgType = IPERF_PKT_CODED_DATA;
      p->plSize = sizeof(IperfCodedPayloadPkt_t) + (sizeof(uint8_t) * config.payloadSizeBytes);
      p->seqNo = 0;
    }
  }

  // printf("cache blocks %d ", config.numCacheBlocks);
  // printf("%x | ", cacheBuffer);
  // for (int i = 0; i < 4 * CODED_CACHE_BLOCK_SIZE; i++)
  // {
  //   printf("%02x ", * (uint8_t *) (cacheBuffer + i));
  // }
  // printf("\n");
  // printf("%x | ", rxtxBuffer);
  // for (int i = 0; i < 4 * CODED_CACHE_BLOCK_SIZE; i++)
  // {
  //   printf("%02x ", * (uint8_t *) (rxtxBuffer + i));
  // }
  // printf("\n");

  Iperf_StartUdpServer(&udpServer, relayerPid);

  #if TIME_CACHING
  sumTimeTakenForCaching = 0;
  avgTimeTakenForCaching = 0;
  cachingCtr = 0;

  sumTimeTakenForLookups = 0;
  avgTimeTakenForLookups = 0;
  lookupCtr = 0;

  printf("TIME_CACHING enabled. setting cache chance percent to 100\n");
  config.cacheChancePercent = 100;
  #endif

  #if DEMO_CONFIG // This lets the neopixel module know there's been a cache hit
  SimpleQueue_Init(&cacheHitQueue, (uint16_t *) &cacheHitQueueBuffer, 16);
  #endif
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

static int sendCachedPkt(uint16_t i)
{
  IperfUdpPkt_t *cached = (IperfUdpPkt_t *) (cacheBuffer + (i * CACHE_BLOCK_SIZE));
  logdebug("Sending cached idx:%d (seq no %d) to destination\n", i, cached->seqNo);
  cached->msgType = IPERF_PKT_RESP;
  cacheLock[i] = false;
  return Iperf_SocklessUdpSendToDst((char *) (cacheBuffer + (i * CACHE_BLOCK_SIZE)), CACHE_BLOCK_SIZE);
}

// Returns -1 if every cache block is locked
static int findUnlockedCacheSlot(void)
{
  for (int i = (cacheIdx + 1) % config.numCacheBlocks; i != cacheIdx; i=(i+1) % config.numCacheBlocks)
  {
    if (!cacheLock[i])
    {
      return i;
    }
  }
  return -1;
}

static int codedFindCacheSlot(uint16_t seqNo)
{
  // JON TODO
  return 0;
}

static int codedCacheLookup(uint8_t chunkIdx)
{
  for (int i = 0; i < config.numCacheBlocks; i++)
  {
    IperfUdpPkt_t *udp = (IperfUdpPkt_t *) (cacheBuffer + (i * CODED_CACHE_BLOCK_SIZE));
    IperfCodedPayloadPkt_t *coded = (IperfCodedPayloadPkt_t *) udp->payload;
    uint8_t *bitmap = coded->bitmap;
    uint8_t offset = coded->pktOffset;

    if (chunkIdx > offset * IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS)
    {
      return -1; 
    }
  }
}

static int codedCanSatisfyRequest(IperfCatalogueVector_t *catalogue)
{
  // We got a catalogue. go thru every one of our cache blocks and see if anything satisfies
  uint64_t Cbefore = (uint64_t) (* (uint64_t *) catalogue->bitmap);
  for (int cacheBlockIdx = 0; cacheBlockIdx < config.numCacheBlocks; cacheBlockIdx++)
  {
    printf("Checking for servicability with cacheblockidx %d\n", cacheBlockIdx);

    IperfUdpPkt_t *udp = (IperfUdpPkt_t *) (cacheBuffer + (cacheBlockIdx * CODED_CACHE_BLOCK_SIZE));
    IperfCodedPayloadPkt_t *coded = (IperfCodedPayloadPkt_t *) udp->payload;
    uint8_t *bitmap = coded->bitmap;
    uint8_t offset = coded->pktOffset;

    if (catalogue->pktOffset != offset)
    {
      printf("Catalogue offset mismatch. continuing\n");
      continue;
    }
 
    uint64_t R = (uint64_t) (* (uint64_t *) bitmap);
    printf("R=0x%08x\n", R);
    printf("Cbefore=0x%08x\n", Cbefore);
    uint64_t Cafter = Cbefore ^ R;
    printf("Cafter=0x%08x\n", Cafter);
    uint64_t Cdiff = (Cafter > Cbefore) ? Cafter - Cbefore : Cbefore - Cafter;
    printf("Cdiff=0x%08x\n", Cdiff);

    // // Check if Cdiff is a power of 2
    // if (Cdiff > 0 && ((Cdiff - 1) & Cdiff) == 0)
    // {
    //   // Cdiff is a power of 2. Find which packet we can service thru this
    //   uint16_t decodedPktIdx; // 
    //   for (int i = 0; i < IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS; i++)
    //   {
    //     if (Cdiff & (1 << i) > 0)
    //     {
    //       decodedPktIdx = i;
    //       break;
    //     }
    //   }
    //   printf("Can service. With coded data in cache idx %d we can decode %d\n", cacheBlockIdx, decodedPktIdx);
    //
    //   // Put in our outward queue cache block at $cacheblockidx
    //   // flip the bit
    // }

  }
}

static void codedCache(IperfUdpPkt_t *iperfPkt)
{
  logdebug("[CODED] Caching seq no %d at cache index %d : %s\n", iperfPkt->seqNo, cacheIdx, iperfPkt->payload);

  // ASSUMING 1 cache slot
  // First lets look at if cache slot is taken up by anything
  IperfUdpPkt_t *udp = (IperfUdpPkt_t *) cacheBuffer;
  IperfCodedPayloadPkt_t *coded = (IperfCodedPayloadPkt_t *) udp->payload;
  uint8_t *codedPayload = coded->payload;
  uint8_t *bitmap = coded->bitmap;

  uint8_t numCodedPackets = 0;
  uint16_t indices[2]; // no need eventually
  uint8_t offset = coded->pktOffset;

  for (int byte = 0; byte < IPERF_CATALOGUE_BITMAP_LENGTH_BYTES; byte++)
  {
    for (int bit = 0; bit < 8; bit++)
    {
      if(bitmap[byte] & (0x1 << bit)) // Cached content found
      {
        indices[numCodedPackets] = (offset * IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS * 8) + (byte * 8) + bit;
        printf("[cacheIdx:%d][chunkIdx:%d] %d \n", cacheIdx, numCodedPackets, indices[numCodedPackets]);
        numCodedPackets++;
      }
    }
  }

  uint8_t byteIdx = iperfPkt->seqNo / 8;
  uint8_t bitIdx = iperfPkt->seqNo % 8;
  if (numCodedPackets < 2) // TEST if there's nothing cached coded, cache the first thing
  {
    bitmap[byteIdx] = bitmap[byteIdx] ^ (1 << bitIdx);
    // printf("numCoded < 2. caching/coding \n");
    for (int i = 0; i < config.payloadSizeBytes; i++)
    {
      codedPayload[i] = codedPayload[i] ^ iperfPkt->payload[i];
      // printf("%x ", codedPayload[i]);
    }
    // printf("\n");
  }
  else // There is 2 things cached and coded. flush the cache and put in new thing
  {
    memset(bitmap, 0x00, IPERF_CATALOGUE_BITMAP_LENGTH_BYTES);
    memset(coded, 0x00, CODED_CACHE_BLOCK_SIZE);
    bitmap[byteIdx] = bitmap[byteIdx] ^ (1 << bitIdx);
    // printf("numCoded == 2. flushing \n");
    for (int i = 0; i < config.payloadSizeBytes; i++)
    {
      codedPayload[i] = codedPayload[i] ^ iperfPkt->payload[i];
      // printf("%x ", codedPayload[i]);
    }
    printf("\n");
  }

  // Iperf_PrintBitmapHex(coded);
  // printf(" num coded packets %d ", numCodedPackets);
  // printf("%s\n", codedPayload);
}

static void legacyCache(IperfUdpPkt_t *iperfPkt)
{
  #if TIME_CACHING
  uint32_t t0 = ztimer_now(ZTIMER_USEC);
  #endif
  logdebug("[NO-CODING] Caching seq no %d at cache index %d : %s\n", iperfPkt->seqNo, cacheIdx, iperfPkt->payload);
  if (cacheLock[cacheIdx])
  {
    logdebug("%d cache locked. Searching for a different cache space\n", cacheIdx);
    int newCacheIdx = findUnlockedCacheSlot();
    if (newCacheIdx < 0)
    {
      logdebug("All caches are locked\n");
      return;
    }
    cacheIdx = newCacheIdx;
  }

  memcpy((uint8_t *) (cacheBuffer + (cacheIdx * CACHE_BLOCK_SIZE)), iperfPkt, CACHE_BLOCK_SIZE);
  cacheIdx = (cacheIdx + 1) % config.numCacheBlocks;

  #if TIME_CACHING
  uint32_t t1 = ztimer_now(ZTIMER_USEC);
  uint32_t diff = t1-t0;
  sumTimeTakenForCaching += diff;
  cachingCtr++;
  avgTimeTakenForCaching = sumTimeTakenForCaching / cachingCtr;
  #if PRINT_TIME_CACHING
  printf("cache took %d us, on average %d\n", diff, avgTimeTakenForCaching);
  #endif
  #endif

  return;
}

// Şüphesiz inkar edenler Zikr'i (Kur'-an'ı) duydukları zaman neredeyse seni gözleriyle devirecekler. (Senin için,) "Hiç şüphe yok o bir delidir" diyorlar. Halbuki o (Kur'an), âlemler için ancak bir öğüttür. 
// fhdjfhdjfdfkhdkjf
int Iperf_LookUpCachedPktPtr(uint16_t pktIdx)
{
  for (int i = 0; i < config.numCacheBlocks; i++)
  {
    IperfUdpPkt_t *p = (IperfUdpPkt_t *) (cacheBuffer + (i * CACHE_BLOCK_SIZE));
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

void Iperf_PrintCache(void)
{
  printf("Cache contents:\n");
  for (int i = 0; i < config.numCacheBlocks; i++)
  {
    IperfUdpPkt_t *p = (IperfUdpPkt_t *) (cacheBuffer + (i * CACHE_BLOCK_SIZE));
    char chunkPayload[config.payloadSizeBytes + 1];
    if (p->msgType == IPERF_PAYLOAD || p->msgType == IPERF_PKT_RESP)
    {
      memset((char *) &chunkPayload, 0x00, config.payloadSizeBytes + 1);
      snprintf((char *) &chunkPayload, config.payloadSizeBytes, p->payload);
      printf("[cache %d]:[chunk %d] %s\n", i, p->seqNo, chunkPayload);
    }
    else if (p->msgType == IPERF_PKT_CODED_DATA)
    {
      IperfCodedPayloadPkt_t *codedPkt = (IperfCodedPayloadPkt_t *) p->payload;
      memset((char *) &chunkPayload, 0x00, config.payloadSizeBytes + 1);
      snprintf((char *) &chunkPayload, config.payloadSizeBytes, codedPkt->payload);
      printf("[cache %d]:", i);
      Iperf_PrintBitmapHex(codedPkt);
      printf(" ");
      XorCoding_PrintBitmapBits(codedPkt->bitmap);
      printf("] %s\n", chunkPayload);
    }
  }
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
      case IPERF_IPC_MSG_RELAY_RESPOND: // IF relayer needs to do something instead of simply forwarding
      {
        logdebug("RELAYER RESPONSE\n");
        sendPayload();
        break;
      }
      case IPERF_IPC_MSG_RELAY_SERVICE_INTEREST:
      {
        logdebug("RELAYER SERVICING INTEREST\n");
        uint16_t cacheIdxToSend;
        int ret = SimpleQueue_Pop(&pktReqQueue, &cacheIdxToSend);
        if (ret)
        {
          logdebug("Queue returned 1 %d\n", __LINE__);
          break;
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

  if (strncmp(iperfPkt->payload, "asdqwe", 6) == 0)
  {
    msg_t ipc;
    ipc.type = IPERF_IPC_MSG_RELAY_RESPOND;
    msg_send(&ipc, relayerPid);
    shouldForward = false;
  }

  // FILTERS
  switch (iperfPkt->msgType)
  {
    case IPERF_ECHO_RESP:
    case IPERF_ECHO_CALL:
      {
        logdebug("Forwarding Echo %s : %s\n", (iperfPkt->msgType == IPERF_ECHO_CALL ? "call" : "resp"), iperfPkt->payload);
        break;
      }
    case IPERF_CONFIG_SYNC:
      {
        Iperf_HandleConfigSync(iperfPkt);
        break;
      }
    case IPERF_PAYLOAD:
    case IPERF_PKT_RESP:
      {
        if (iperfPkt->seqNo == 2 || iperfPkt->seqNo == 3) // JON TODO RM
        {
          codedCache(iperfPkt);
        }

        if (iperfPkt->seqNo == 2)
        {
          shouldForward = false;
          break; // JON TODO RM
        }

#if CHANCE_TO_DROP
        // shouldForward = !coinFlip(CHANCE_TO_DROP);
#ifdef DROP_EVEN_NUMBEREDS 
        // shouldForward = iperfPkt->seqNo % 2 > 0 ? true : false; // drop half
#endif
        if (!shouldForward)
        {
          logdebug("Simulating pkt drop. payload no %d\n", iperfPkt->seqNo);
          return shouldForward;
        }
#endif
        if (config.mode == IPERF_MODE_CACHING_BIDIRECTIONAL) // TODO rm the true 
        {
          if (config.cache && coinFlip(config.cacheChancePercent))
          {
            logdebug("Payload seq %d intercepted. Will cache\n", iperfPkt->seqNo);
            legacyCache(iperfPkt);
            if (logprintTags[DEBUG]) Iperf_PrintCache();
          }
        }
        else if (config.mode == IPERF_MODE_CACHING_CODING)
        {
          // if (config.cache && config.code)
          // {
          //   // TODO CACHE CODE LOGIC
          //   codedCache(iperfPkt);
          // }
        }

        break;
      }
    case IPERF_PKT_BULK_REQ:
      {
        /*
        * BULK REQUEST INTERCEPTED:
        * - Go thru the requested pkts in the request.
        * - Check if any are in our cache. 
        *   - If so, remove that from the interest and add it to our service queue
        *   - If not, simply forward
        */
        if (config.cache && config.mode == IPERF_MODE_CACHING_BIDIRECTIONAL)
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
            int cachedPktIdx = Iperf_LookUpCachedPktPtr(expectArr[i]);
            if (cachedPktIdx > -1)
            {
              // CACHE HIT
              // Remove interest from bulk interest, put it in our service list
              logdebug("Cache hit! Seq no %d at cache idx %d\n", expectArr[i], cachedPktIdx);
              printf("CACHE HIT CACHE HIT %d\n", expectArr[i]);

              #if DEMO_CONFIG
              SimpleQueue_Push(&cacheHitQueue, expectArr[i]);
              results.lastPktSeqNo = expectArr[i];
              #endif

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
        break;
      }
    case IPERF_PKT_CATALOGUE_VECTOR:
      {
        // CODED CACHING
        // We just caught a catalogue vector. This will tell us what the receiver has and what it does not have
        //
        printf("IPERF_PKT_CATALOGUE_VECTOR\n");
        Iperf_PrintCatalogueVector((IperfCatalogueVector_t *) iperfPkt->payload);
        codedCanSatisfyRequest((IperfCatalogueVector_t *) iperfPkt->payload);
        break;
      }
    default:
      {
      break;
      }
  }
  
  return shouldForward;
}
