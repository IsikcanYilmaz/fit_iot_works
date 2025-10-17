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
#include "xor_coding.h"

#include "net/gnrc/udp.h"
#include "net/ipv6/hdr.h"
#include "net/ipv6/addr.h"
#include "net/inet_csum.h"

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

  if (config.mode == IPERF_MODE_SIMPLE_CACHING)
  {
    // TODO? too many todos
  }
  else if (config.mode == IPERF_MODE_CODED_CACHING)
  {
    for (int i = 0; i < config.numCacheBlocks; i++)
    {
      IperfUdpPkt_t *p = (IperfUdpPkt_t *) (cacheBuffer + (i * CODED_CACHE_BLOCK_SIZE)); 
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

static int sendLegacyCachedPkt(uint16_t i)
{
  IperfUdpPkt_t *cached = (IperfUdpPkt_t *) (cacheBuffer + (i * CACHE_BLOCK_SIZE));
  logdebug("Sending cached idx:%d (seq no %d) to destination\n", i, cached->seqNo);
  cached->msgType = IPERF_PKT_RESP;
  cacheLock[i] = false;
  return Iperf_SocklessUdpSendToDst((char *) (cacheBuffer + (i * CACHE_BLOCK_SIZE)), CACHE_BLOCK_SIZE);
}

static int sendCodedCachedPkt(uint16_t i)
{
  IperfUdpPkt_t *cachedIperfPkt = (IperfUdpPkt_t *) (cacheBuffer + (i * CACHE_BLOCK_SIZE));
  logdebug("Sending cached idx:%d to sink\n", i);
  IperfCodedPayloadPkt_t *codedPkt = (IperfCodedPayloadPkt_t *) cachedIperfPkt->payload;
  if (logprintTags[DEBUG])
  {
    for (int i = 0; i < config.payloadSizeBytes; i++)
    {
      printf("0x%x ", cachedIperfPkt->payload[i]);
    }
    printf("\n");
  }
  cachedIperfPkt->msgType = IPERF_PKT_CODED_DATA;
  cacheLock[i] = false;
  return Iperf_SocklessUdpSendToDst((char *) (cacheBuffer + (i * CODED_CACHE_BLOCK_SIZE)), CODED_CACHE_BLOCK_SIZE);
}

// Returns -1 if every cache block is locked
static int findUnlockedCacheSlot(void)
{
  if (config.numCacheBlocks == 1)
  {
    return (cacheLock[0]) ? -1 : 0;
  }

  for (int i = (cacheIdx + 1) % config.numCacheBlocks; i != cacheIdx; i=(i+1) % config.numCacheBlocks)
  {
    printf("locked status [i %d]:%d\n", i, cacheLock[i]);
    if (!cacheLock[i])
    {
      return i;
    }
  }
  return -1;
}

static int codedFindCacheSlot(void)
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

// Once relay receives a catalogue vector, pass it here. This fn will go thru the vector, check if we can service any of the
// zeros in the vector. If we can, those cache ids will be put to the service queue, and the bits in the vector will be flipped
// returns true if we service at least one packet
static bool handleCatalogueVector(IperfCatalogueVector_t *catalogue)
{
  bool canSatisfy = false;
  // We got a catalogue. go thru every one of our cache blocks and see if anything satisfies
  uint32_t Cbefore = (uint32_t) (* (uint32_t *) catalogue->bitmap);
  for (int cacheBlockIdx = 0; cacheBlockIdx < config.numCacheBlocks; cacheBlockIdx++)
  {
    logdebug("Checking for servicability with cacheblockidx %d\n", cacheBlockIdx);

    IperfUdpPkt_t *udp = (IperfUdpPkt_t *) (cacheBuffer + (cacheBlockIdx * CODED_CACHE_BLOCK_SIZE));
    IperfCodedPayloadPkt_t *coded = (IperfCodedPayloadPkt_t *) udp->payload;
    uint8_t offset = coded->pktOffset;

    if (catalogue->pktOffset != offset)
    {
      logdebug("Catalogue offset mismatch. continuing\n");
      continue;
    }
 
    uint32_t R = (uint32_t) (* (uint32_t *) coded->bitmap);
    logverbose("R=0x%08x\n", R);
    logverbose("Cbefore=0x%08x\n", Cbefore);
    uint32_t Cafter = Cbefore ^ R;
    logverbose("Cafter=0x%08x\n", Cafter);
    uint32_t Cdecodable = (~Cbefore) & Cafter;
    uint32_t Cdependent = (~Cafter) & Cbefore;
    logverbose("Cdecodable 0x%08x Cdependent 0x%08x\n", Cdecodable, Cdependent);

    // Check if Cdecodable is a power of 2. this means there will be one fully decoded chunk 
    if (Cdecodable > 0 && ((Cdecodable - 1) & Cdecodable) == 0)
    {
      // Cdecodable is a power of 2. Find which packet we can service thru this
      uint16_t decodedPktIdx; // 
      for (int i = 0; i < IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS; i++)
      {
        if ((Cdecodable & (1 << i)) > 0)
        {
          decodedPktIdx = i;
          break;
        }
      }
      logdebug("Can service. With coded data in cache idx %d we can decode %d\n", cacheBlockIdx, decodedPktIdx);

      // Put in our outward queue cache block at $cacheblockidx
      // flip the bit
      logdebug("Catalogue before %x ", * (uint32_t *) catalogue->bitmap);
      * (uint32_t*) catalogue->bitmap |= (1 << decodedPktIdx);
      if (logprintTags[DEBUG]) printf("Catalogue after %x \n", * (uint32_t *) catalogue->bitmap);
      canSatisfy = true;
      logdebug("Putting cache block idx %d onto the service queue\n", cacheBlockIdx);
      SimpleQueue_Push(&pktReqQueue, cacheBlockIdx);
    }
  }
  return canSatisfy;
}

static void codedCache(IperfUdpPkt_t *iperfPkt)
{
  logdebug("[%s] Caching seq no %d at cache index %d : %s\n", __FUNCTION__, iperfPkt->seqNo, cacheIdx, iperfPkt->payload);

  // First, find a cache slot that is not locked. 
  if (cacheLock[cacheIdx])
  {
    logdebug("[%s] cache locked. Searching for a different cache space\n", cacheIdx);
    int newCacheIdx = findUnlockedCacheSlot();
    if (newCacheIdx < 0)
    {
      logerror("All cache slots are locked!\n");
      return;
    }
    cacheIdx = newCacheIdx;
  }
  logdebug("[%s] cacheIdx %d\n", __FUNCTION__, cacheIdx); 

  // Second, check what we currently have in this cache slot. 
  IperfUdpPkt_t *udp = (IperfUdpPkt_t *) (cacheBuffer + (cacheIdx * CODED_CACHE_BLOCK_SIZE));
  IperfCodedPayloadPkt_t *coded = (IperfCodedPayloadPkt_t *) udp->payload;
  uint8_t *codedPayload = coded->payload;
  uint8_t numCodedPackets = 0;
  uint8_t newOffset = iperfPkt->seqNo / IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS;
  uint8_t offset = coded->pktOffset;

  if (newOffset > 0)
  {
    logerror("JON JON JON UNDER CONSTRUCTION. NEWLY RECEIVED PKT HAS OFFSET %d\n", newOffset);
    return;
  }

  // If this packet contains already a coded payload, cache it directly (?)
  if (iperfPkt->msgType == IPERF_PKT_CODED_DATA)
  {
    memcpy(udp, iperfPkt, CODED_CACHE_BLOCK_SIZE); 
  }
  else
  {
    udp->msgType = IPERF_PKT_CODED_DATA;
    uint32_t bitmap = * ((uint32_t *) coded->bitmap);
    numCodedPackets = __builtin_popcount((uint32_t) bitmap); // counts 1 bits in a bit string 

    uint8_t bitIdx = iperfPkt->seqNo % IPERF_CATALOGUE_BITMAP_LENGTH_CHUNKS;
    if (numCodedPackets == 0 || numCodedPackets == 2 || !config.code) // There is 2 things cached and coded or nothing here. flush the cache and put in new thing. OR we're not doing coding and we should fall in htere every time
    {
      bitmap = (1 << bitIdx);
      memcpy(coded->payload, iperfPkt->payload, config.payloadSizeBytes);
      *((uint32_t *)coded->bitmap) = bitmap;
    }
    else if (numCodedPackets == 1) // There is 1 thing cached here. will code and cache our new thing here.
    {
      bitmap |= (1 << bitIdx);
      *((uint32_t *)coded->bitmap) = bitmap;
      for (int i = 0; i < config.payloadSizeBytes; i++)
      {
        codedPayload[i] = codedPayload[i] ^ iperfPkt->payload[i];
      }
    }
    else // we shouldnt get here 
    {
      logerror("[%s] Something went wrong line %d\n", __FUNCTION__, __LINE__);
    }

    // Increment our next cache index
    cacheIdx = (cacheIdx + 1) % config.numCacheBlocks;
    logdebug("New cache idx %d\n", cacheIdx);
    if (logprintTags[DEBUG]) Iperf_PrintCodedCache();
  }
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

/**
 * @brief computes UDP checksum for given UDP payload and checksum.
 *
 * @param[in] payload_data UDP payload
 * @param[in] size         The size of the payload
 * @param[in] checksum     Checksum field of the UDP packet.
 *                         Will be overridden with the computed checksum.
 *
 * @return  0 on success
 * @return  non-zero on failure
 */
static uint16_t computeUdpChecksum(uint8_t *payload_data, size_t size, udp_hdr_t *rawUdpHeaderBefore, ipv6_hdr_t *rawIpv6Header) 
{
  static gnrc_pktsnip_t zero_snip = {
    .users = 0,
    .next = NULL,
    .data = NULL,
    .size = 0,
    .type = GNRC_NETTYPE_UNDEF,
  };

  gnrc_pktsnip_t payload = zero_snip;
  gnrc_pktsnip_t hdr = zero_snip;
  gnrc_pktsnip_t pseudo_hdr = zero_snip;
  pseudo_hdr.type = GNRC_NETTYPE_IPV6;
  pseudo_hdr.data = rawIpv6Header;
  pseudo_hdr.size = sizeof(ipv6_hdr_t);
  pseudo_hdr.next = &hdr;

  hdr.type = GNRC_NETTYPE_UDP;
  hdr.data = rawUdpHeaderBefore;
  hdr.size = sizeof(udp_hdr_t);
  hdr.next = &payload;

  payload.type = GNRC_NETTYPE_UNDEF;
  payload.data = payload_data;
  payload.size = size;

  return gnrc_udp_calc_csum(&hdr, &pseudo_hdr);
}

static udp_hdr_t * findUdpHeaderFromIpv6Header(gnrc_pktsnip_t *snip)
{
  void *ipv6 = (void *) snip->data;
  udp_hdr_t *udpHeader = (udp_hdr_t *) (ipv6 + sizeof(ipv6_hdr_t));
  return udpHeader;
}

// Şüphesiz inkar edenler Zikr'i (Kur'-an'ı) duydukları zaman neredeyse seni gözleriyle devirecekler. (Senin için,) "Hiç şüphe yok o bir delidir" diyorlar. Halbuki o (Kur'an), âlemler için ancak bir öğüttür. 
// fhdjfhdjfdfkhdkjf
// This function returns the CACHE INDEX if the index of what's cached inside it matches the passed argument. 
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
  }
}

void Iperf_PrintCodedCache(void) // TODO better generalization
{
  printf("curr idx %d. Cache contents:\n", cacheIdx);
  for (int i = 0; i < config.numCacheBlocks; i++)
  {
    IperfUdpPkt_t *p = (IperfUdpPkt_t *) (cacheBuffer + (i * CODED_CACHE_BLOCK_SIZE));
    char chunkPayload[config.payloadSizeBytes + 1];
    if (p->msgType == IPERF_PKT_CODED_DATA)
    {
      IperfCodedPayloadPkt_t *codedPkt = (IperfCodedPayloadPkt_t *) p->payload;
      memset((char *) &chunkPayload, 0x00, config.payloadSizeBytes + 1);
      snprintf((char *) &chunkPayload, config.payloadSizeBytes, codedPkt->payload);
      printf("[cache %d]:", i);
      // Iperf_PrintBitmapHex(codedPkt);
      printf("offset %d, vector 0x%08x ", codedPkt->pktOffset, *((uint32_t *) codedPkt->bitmap));
      printf(" ");
      XorCoding_PrintBitmapBits(codedPkt->bitmap);
      // printf("] %s\n", chunkPayload);
      printf("]%c\n", (cacheIdx == i) ? '<' : ' ');
    }
    else 
    {
      printf("msgType %d\n", p->msgType);
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
            logdebug("Queue returned error code 1 %d\n", __LINE__);
            break;
          }

          if (config.mode == IPERF_MODE_SIMPLE_CACHING) // JON please eventually remove the legacy stuff 
          {
            sendLegacyCachedPkt(cacheIdxToSend);
          }
          else if (config.mode == IPERF_MODE_CODED_CACHING)
          {
            sendCodedCachedPkt(cacheIdxToSend);
          }

          // If we got more in the queue, keep coming back here
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

void Relayer_Test(uint32_t vec, uint8_t offset)
{
  IperfCatalogueVector_t testCatalogue;
  testCatalogue.pktOffset = offset;
  * ((uint32_t *) &testCatalogue.bitmap) = vec;
  bool canSatisfy = handleCatalogueVector(&testCatalogue);
  if (canSatisfy) // JON TODO maybe make this generic?
  {
    logverbose("Sending IPC\n");
    msg_t ipc;
    ipc.type = IPERF_IPC_MSG_RELAY_SERVICE_INTEREST;
    msg_send(&ipc, relayerPid);
  }
}

// Will return true if the packet should keep going
bool Iperf_RelayerIntercept(gnrc_pktsnip_t *snip)
{
  bool shouldForward = true;
  bool shouldComputeChecksum = false;
  bool shouldSendIpc = false;

  // We care about IPv6 and UNDEF snips. 
  gnrc_pktsnip_t *undef = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_UNDEF);
  gnrc_pktsnip_t *ipv6 = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_IPV6);
  ipv6_hdr_t *ipv6Header = (ipv6_hdr_t *) ipv6->data;
  udp_hdr_t *udpHeader = findUdpHeaderFromIpv6Header(ipv6);

  if (logprintTags[VERBOSE])
  {
    udp_hdr_print(udpHeader);
    ipv6_hdr_print(ipv6Header);
    printf("ipv6 header addr 0x%08x\n", ipv6Header);
  }

  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) (undef->data + sizeof(udp_hdr_t)); // since we're in the ipv6 layer RIOT only gives us the snip structure up until the ipv6 layer. after that is considered undef. 

  if (strncmp(iperfPkt->payload, "asdqwe", 6) == 0) // TEST message queue test
  {
    msg_t ipc;
    ipc.type = IPERF_IPC_MSG_RELAY_RESPOND;
    msg_send(&ipc, relayerPid);
    shouldForward = false;
  }

  if (strncmp(iperfPkt->payload, "zxc", 3) == 0) // TEST message modification test
  {
    logdebug("MOD ECHO %s : %s. BEFORE Checksum %04x\n", (iperfPkt->msgType == IPERF_ECHO_CALL ? "call" : "resp"), iperfPkt->payload, udpHeader->checksum);
    iperfPkt->payload[0] = 'A';
    shouldComputeChecksum = true;
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
    // case IPERF_PKT_CODED_DATA:
      {
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
        if (config.mode == IPERF_MODE_SIMPLE_CACHING) // TODO rm the true 
        {
          if (config.cache && coinFlip(config.cacheChancePercent))
          {
            logdebug("Payload seq %d intercepted. Will cache\n", iperfPkt->seqNo);
            legacyCache(iperfPkt);
            if (logprintTags[DEBUG]) Iperf_PrintCache();
          }
        }
        else if (config.mode == IPERF_MODE_CODED_CACHING)
        {
          if (config.cache && coinFlip(config.cacheChancePercent))
          {
            codedCache(iperfPkt);
          }
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
        if (config.cache && config.mode == IPERF_MODE_SIMPLE_CACHING)
        {
          IperfBulkInterest_t *bulkInterest = (IperfBulkInterest_t *) iperfPkt->payload;
          uint8_t numExpects = bulkInterest->len;
          uint16_t *expectArr = bulkInterest->arr;
          uint16_t numBadExpectsOrCacheHits = 0;
          logdebug("Intercepted bulk interest for %d chunks\n", numExpects);
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

            if (logprintTags[DEBUG]) printf("%d ", expectArr[i]);
          }
          if (logprintTags[DEBUG]) printf("\n");

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
        logdebug("IPERF_PKT_CATALOGUE_VECTOR received\n");
        
        if (!config.cache)
        {
          shouldForward = true;
          break;
        }

        if (logprintTags[DEBUG]) Iperf_PrintCatalogueVector((IperfCatalogueVector_t *) iperfPkt->payload);
        IperfCatalogueVector_t *catalogue = (IperfCatalogueVector_t *) iperfPkt->payload;
        bool canSatisfy = handleCatalogueVector(catalogue);
        shouldComputeChecksum = canSatisfy;
        shouldSendIpc = canSatisfy;

        if (canSatisfy)
        {
          results.cacheHits++;
        }

        if (shouldSendIpc) // JON TODO maybe make this generic?
        {
          logverbose("Sending IPC\n");
          msg_t ipc;
          ipc.type = IPERF_IPC_MSG_RELAY_SERVICE_INTEREST;
          msg_send(&ipc, relayerPid);
        }

        // If this catalogue is fully satisfied after our service, we shouldnt forward it
        if (*((uint32_t *) catalogue->bitmap) == 0xffffffff) 
        {
          shouldForward = false;
        }
        break;
      }
    case IPERF_PKT_CODED_DATA:
      {
        if (config.cache && coinFlip(config.cacheChancePercent))
        {
          codedCache(iperfPkt);
        }       
      }
    default:
      {
        break;
      }
  }

  // If the relay manipulates the packet, we need to recompute the udp checksum
  if (shouldComputeChecksum)
  {
    logverbose("Packet manipulated. Recomputing checksum 0x%04x\n", byteorder_ntohs(udpHeader->checksum));
    udpHeader->checksum = byteorder_htons(0);
    uint16_t newChecksum = 0;
    computeUdpChecksum((uint8_t *) (undef->data + sizeof(udp_hdr_t)), sizeof(IperfUdpPkt_t) + iperfPkt->plSize, (udp_hdr_t *) undef->data, ipv6Header);
    // udpHeader->checksum = byteorder_htons(newChecksum);
    logverbose("New checksum 0x%04x\n", byteorder_ntohs(udpHeader->checksum));
  }

  return shouldForward;
}
