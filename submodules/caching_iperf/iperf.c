#include <stdio.h>
#include <stdlib.h>
#include "macros/utils.h"
#include "net/gnrc.h"
#include "net/sock/udp.h"
#include "net/gnrc/udp.h"
#include "net/sock/tcp.h"
#include "net/gnrc/nettype.h"
#include "net/gnrc/ipv6.h"
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
#include "sender.h"

IperfConfig_s config = {
  .payloadSizeBytes = IPERF_PAYLOAD_DEFAULT_SIZE_BYTES,
  .pktPerSecond = 0, // TODO
  .delayUs = 10000,
  .transferSizeBytes = IPERF_DEFAULT_TRANSFER_SIZE_BYTES,
  .transferTimeUs = IPERF_DEFAULT_TRANSFER_TIME_US,
  .mode = IPERF_MODE_CACHING_BIDIRECTIONAL,
};

IperfResults_s results;

static volatile bool running = false;
static char threadStack[THREAD_STACKSIZE_DEFAULT];
static kernel_pid_t threadPid = KERNEL_PID_UNDEF;

char dstGlobalIpAddr[25] = "2001::2";
char srcGlobalIpAddr[25] = "2001::1"; // TODO better solution
//
char receiveFileBuffer[IPERF_TOTAL_TRANSMISSION_SIZE_MAX];
bool receivedPktIds[IPERF_TOTAL_TRANSMISSION_SIZE_MAX]; // TODO bitmap this
/*uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];*/

uint8_t rxtxBuffer[256];

// Both roles will use this queue
#define PKT_REQ_QUEUE_LEN 128
uint16_t pktReqQueueBuffer[PKT_REQ_QUEUE_LEN];
SimpleQueue_t pktReqQueue;

msg_t ipcMsg;
ztimer_t intervalTimer;

///////////////////////////////////

static void getNetifStats(void)
{
  netstats_t stats;
  netif_t *mainIface = netif_iter(NULL);

  int res = netif_get_opt(mainIface, NETOPT_STATS, NETSTATS_LAYER2, &stats, sizeof(stats));
  
  results.l2numReceivedPackets = stats.rx_count;
  results.l2numReceivedBytes = stats.rx_bytes;
  results.l2numSentPackets = (stats.tx_unicast_count + stats.tx_mcast_count);
  results.l2numSentBytes = stats.tx_bytes;
  results.l2numSuccessfulTx = stats.tx_success;
  results.l2numErroredTx = stats.tx_failed;

  res = netif_get_opt(mainIface, NETOPT_STATS, NETSTATS_IPV6, &stats, sizeof(stats));

  results.ipv6numReceivedPackets = stats.rx_count;
  results.ipv6numReceivedBytes = stats.rx_bytes;
  results.ipv6numSentPackets = (stats.tx_unicast_count + stats.tx_mcast_count);
  results.ipv6numSentBytes = stats.tx_bytes;
  results.ipv6numSuccessfulTx = stats.tx_success;
  results.ipv6numErroredTx = stats.tx_failed;
}

static void resetNetifStats(void)
{
  netif_t *mainIface = netif_iter(NULL);
  int res = netif_set_opt(mainIface, NETOPT_STATS, NETSTATS_LAYER2, NULL, 0);
  res |= netif_set_opt(mainIface, NETOPT_STATS, NETSTATS_IPV6, NULL, 0);
  if (res != 0)
  {
    logerror("%s failed to reset netif stats\n", __FUNCTION__);
  }
}

static void printResults(bool json)
{
  if (running)
  {
    getNetifStats();
  }
  printf((json) ? \

           "{\"iAmSender\":%d, \"lastPktSeqNo\":%d, \"pktLossCounter\":%d, \"numReceivedPkts\":%d, \"numReceivedBytes\":%d, \"numDuplicates\":%d, \"numSentPkts\":%d, \"numSentBytes\":%d, \"startTimestamp\":%lu, \"endTimestamp\":%lu, \"timeDiff\":%lu, \"l2numReceivedPackets\":%d, \"l2numReceivedBytes\":%d, \"l2numSentPackets\":%d, \"l2numSentBytes\":%d, \"l2numSuccessfulTx\":%d, \"l2numErroredTx\":%d, \"ipv6numReceivedPackets\":%d, \"ipv6numReceivedBytes\":%d, \"ipv6numSentPackets\":%d, \"ipv6numSentBytes\":%d, \"ipv6numSuccessfulTx\":%d, \"ipv6numErroredTx\":%d}\n" : \

           "Results\niAmSender           :%d\nlastPktSeqNo        :%d\npktLossCounter      :%d\nnumReceivedPkts     :%d\nnumReceivedBytes    :%d\nnumDuplicates       :%d\nnumSentPkts         :%d\nnumSentBytes        :%d\nstartTimestamp      :%lu\nendTimestamp        :%lu\ntimeDiff            :%lu\nl2numRxPkts         :%d\nl2numRxBytes        :%d\nl2numTxPkts         :%d\nl2numTxBytes        :%d\nl2numSuccessTx      :%d\nl2numErroredTx      :%d\nipv6numRxPkts       :%d\nipv6numRxBytes      :%d\nipv6numTxPkts       :%d\nipv6numTxBytes      :%d\nipv6numSuccessTx    :%d\nipv6numErroredTx    :%d\n", \

           config.iAmSender, \
           results.lastPktSeqNo, \
           results.pktLossCounter, \
           results.numReceivedPkts, \
           results.numReceivedBytes, \
           results.numDuplicates, \
           results.numSentPkts, \
           results.numSentBytes, \
           results.startTimestamp, \
           results.endTimestamp, \
           results.endTimestamp - results.startTimestamp, \

           results.l2numReceivedPackets, \
           results.l2numReceivedBytes, \
           results.l2numSentPackets, \
           results.l2numSentBytes, \
           results.l2numSuccessfulTx, \
           results.l2numErroredTx,

           results.ipv6numReceivedPackets, \
           results.ipv6numReceivedBytes, \
           results.ipv6numSentPackets, \
           results.ipv6numSentBytes, \
           results.ipv6numSuccessfulTx, \
           results.ipv6numErroredTx
         );
}

static void printFileTransferStatus(void)
{
  printf("Received Pkt Ids:\n");
  for (int i = 0; i < config.numPktsToTransfer; i++)
  {
    char printStr[4];
    if (receivedPktIds[i])
    {
      sprintf((char *) &printStr, "%d", i);
    }
    else
    {
      strcpy((char *) &printStr, "_\0");
    }
    printf("%3s %s", printStr, ((i+1) % 8 == 0 && i > 0) ? "\n" : "");
  }
  printf("\n");
}

static void printReceivedFileContents(void)
{
  printf("%s\n", receiveFileBuffer);
}

static void printAll(void)
{
  printf("{\"config\": ");
  Iperf_PrintConfig(true);
  printf(",\"results\" : ");
  printResults(true);
  printf("}\n");
}

void Iperf_PrintConfig(bool json)
{
  printf((json) ? "{\"iAmSender\":%d, \"payloadSizeBytes\":%d, \"pktPerSecond\":%d, \"delayUs\":%d, \"mode\":%d, \"transferSizeBytes\":%d, \"transferTimeUs\":%d, \"numPktsToTransfer\":%d}\n" : \
           "iAmSender: %d\npayloadSizeBytes: %d\npktPerSecond: %d\ndelayUs: %d\nmode %d\ntransferSizeBytes %d\ntransferTimeUs: %d\nnumPktsToTransfer: %d\n", 
           config.iAmSender, 
           config.payloadSizeBytes, 
           config.pktPerSecond, 
           config.delayUs, 
           config.mode, 
           config.transferSizeBytes, 
           config.transferTimeUs, 
           config.numPktsToTransfer);
}

void Iperf_ResetResults(void)
{
  memset(&results, 0x00, sizeof(IperfResults_s));
  memset(&receivedPktIds, 0x00, IPERF_TOTAL_TRANSMISSION_SIZE_MAX);
  memset(&receiveFileBuffer, 0x00, IPERF_TOTAL_TRANSMISSION_SIZE_MAX);
  resetNetifStats();
  loginfo("Results reset\n");
}

// SOCKLESS 
// Yanked out of sys/shell/cmds/gnrc_udp.c
// takes address and port in _string_ form!
int Iperf_SocklessUdpSend(const char *data, size_t dataLen, char *targetIp)
{
  netif_t *netif;
  uint16_t port;
  ipv6_addr_t addr;

  /* parse destination address */
  if (netutils_get_ipv6(&addr, &netif, targetIp) < 0) {
    loginfo("Error: unable to parse destination address\n");
    return 1;
  }

  /* parse port */
  port = IPERF_DEFAULT_PORT;
  if (port == 0) {
    loginfo("Error: unable to parse destination port\n");
    return 1;
  }

  gnrc_pktsnip_t *payload, *udp, *ip;
  unsigned payload_size;

  /* allocate payload */
  payload = gnrc_pktbuf_add(NULL, data, dataLen, GNRC_NETTYPE_UNDEF);
  if (payload == NULL) {
    logerror("Error: unable to copy data to packet buffer\n");
    return 1;
  }

  /* store size for output */
  payload_size = (unsigned)payload->size;

  /* allocate UDP header, set source port := destination port */
  udp = gnrc_udp_hdr_build(payload, port, port);
  if (udp == NULL) {
    logerror("Error: unable to allocate UDP header\n");
    gnrc_pktbuf_release(payload);
    return 1;
  }

  /* allocate IPv6 header */
  ip = (gnrc_pktsnip_t *) gnrc_ipv6_hdr_build(udp, NULL, &addr);
  if (ip == NULL) {
    logerror("Error: unable to allocate IPv6 header\n");
    gnrc_pktbuf_release(udp);
    return 1;
  }

  /* add netif header, if interface was given */
  if (netif != NULL) {
    gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, NULL, 0);
    if (netif_hdr == NULL) {
      loginfo("Error: unable to allocate netif header\n");
      gnrc_pktbuf_release(ip);
      return 1;
    }
    gnrc_netif_hdr_set_netif(netif_hdr->data, container_of(netif, gnrc_netif_t, netif));
    ip = gnrc_pkt_prepend(ip, netif_hdr);
  }

  /* send packet */
  if (!gnrc_netapi_dispatch_send(GNRC_NETTYPE_UDP,
                                 GNRC_NETREG_DEMUX_CTX_ALL, ip)) {
    logerror("Error: unable to locate UDP thread\n");
    gnrc_pktbuf_release(ip);
    return 1;
  }

  /* access to `payload` was implicitly given up with the send operation
     * above
     * => use temporary variable for output */
  logdebug("Success: sent %u byte(s) to [%s]:%u\n", payload_size, targetIp, port);
  results.numSentPkts++;
  return 0;
}

inline int Iperf_SocklessUdpSendToDst(const char *data, size_t dataLen)
{
  return Iperf_SocklessUdpSend(data, dataLen, dstGlobalIpAddr);
}

inline int Iperf_SocklessUdpSendToSrc(const char *data, size_t dataLen)
{
  return Iperf_SocklessUdpSend(data, dataLen, srcGlobalIpAddr);
}

// With the following two fns, we assume the Tx machine is the master the Rx machine is the follower
int Iperf_SendEcho(char *str)
{
  char rawPkt[20]; 
  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) &rawPkt;
  uint8_t plSize = 16;
  memset(&iperfPkt->payload, 0x00, 16);
  strncpy((char *) iperfPkt->payload, str, 16);
  iperfPkt->seqNo = 0;
  iperfPkt->msgType = IPERF_ECHO_CALL;
  return Iperf_SocklessUdpSendToDst((char *) &rawPkt, sizeof(rawPkt));
}

int Iperf_SendConfig(void)
{
  char rawPkt[20];
  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) &rawPkt;
  IperfConfigPayload_t *configPl = (IperfConfigPayload_t *) iperfPkt->payload;
  memset(&rawPkt, 0x00, 20);
  iperfPkt->msgType = IPERF_CONFIG_SYNC;
  configPl->mode = config.mode;
  configPl->payloadSizeBytes = config.payloadSizeBytes;
  configPl->delayUs = config.delayUs;
  configPl->transferSizeBytes = config.transferSizeBytes;
  configPl->numPktsToTransfer = config.numPktsToTransfer;
  return Iperf_SocklessUdpSendToDst((char *) &rawPkt, sizeof(rawPkt));
}

int Iperf_PacketHandler(gnrc_pktsnip_t *pkt, void (*fn) (gnrc_pktsnip_t *pkt))
{
  int snips = 0;
  int size = 0;
  gnrc_pktsnip_t *snip = pkt;
  logdebug("Handle packet\n");
  while(snip != NULL)
  {
    /*loginfo("SNIP %d. %d bytes. type: %d ", snips, snip->size, snip->type);*/
    switch(snip->type)
    {
      case GNRC_NETTYPE_NETIF:
        {
          logdebug("NETIF\n");
          break;
        }
      case GNRC_NETTYPE_UNDEF:  // APP PAYLOAD HERE
        {
          logdebug("UNDEF\n");
          
          if (logprintTags[DEBUG])
          {
            for (int i = 0; i < snip->size; i++)
            {
              char data = * (char *) (snip->data + i);
              printf("%02x %c %s", data, data, (i % 8 == 0 && i > 0) ? "\n" : "");
            }
            printf("\n");
          }

          /*if (!config.iAmSender && logprintTags[VERBOSE])*/
          /*{*/
            /*printFileTransferStatus();*/
            /*printf("\n");*/
          /*}*/

          if (fn)
          {
            fn(snip->data);
          }
          break;
        }
      case GNRC_NETTYPE_SIXLOWPAN:
        {
          logdebug("6LP\n");
          break;
        }
      case GNRC_NETTYPE_IPV6:
        {
          logdebug("IPV6\n");
          break;
        }
      case GNRC_NETTYPE_ICMPV6:
        {
          logdebug("ICMPV6\n");
          break;
        }
      /*case GNRC_NETTYPE_TCP:*/
      /*  {*/
      /*    logdebug("TCP\n");*/
      /*    break;*/
      /*  }*/
      case GNRC_NETTYPE_UDP:
        {
          logdebug("UDP\n");
          break;
        }
      default:
        {
          logdebug("NONE\n");
          break;
        }
    }
    logdebug("\n");
    size += snip->size;
    snip = snip->next;
    snips++;
  }

  results.numReceivedBytes += size;
  logdebug("\n");

  gnrc_pktbuf_release(pkt);
  return 1;
}

int Iperf_StartUdpServer(gnrc_netreg_entry_t *server, kernel_pid_t pid)
{
  /* check if server is already running or the handler thread is not */
  if (server->target.pid != KERNEL_PID_UNDEF) {
    loginfo("Error: server already running on port %" PRIu32 "\n", server->demux_ctx);
    return 1;
  }
  server->target.pid = pid;
  server->demux_ctx = (uint32_t) IPERF_DEFAULT_PORT;
  gnrc_netreg_register(GNRC_NETTYPE_UDP, server);
  loginfo("Started UDP server on port %d\n", server->demux_ctx);
  return 0;
}

int Iperf_StopUdpServer(gnrc_netreg_entry_t *server)
{
  /* check if server is running at all */
  if (server->target.pid == KERNEL_PID_UNDEF) {
    logerror("Error: server was not running\n");
    return 1;
  }
  /* stop server */
  gnrc_netreg_unregister(GNRC_NETTYPE_UDP, server);
  server->target.pid = KERNEL_PID_UNDEF;
  loginfo("Success: stopped UDP server\n"); 
  return 0;
}

int Iperf_Init(bool iAmSender)
{
  if (running)
  {
    logerror("Already running!\n");
    return 1;
  }

  Iperf_ResetResults();

  config.iAmSender = iAmSender;
  config.senderPort = IPERF_DEFAULT_PORT; // TODO make more generic? TODO make address more generic
  config.listenerPort = IPERF_DEFAULT_PORT;

  config.numPktsToTransfer = (config.transferSizeBytes / config.payloadSizeBytes);

  SimpleQueue_Init(&pktReqQueue, (uint16_t *) &pktReqQueueBuffer, PKT_REQ_QUEUE_LEN);

  if (!iAmSender)
  {
    threadPid = thread_create(threadStack, sizeof(threadStack), THREAD_PRIORITY_MAIN - 1, 0, Iperf_ReceiverThread, NULL, "iperf_receiver");
  }
  if (iAmSender)
  {
    threadPid = thread_create(threadStack, sizeof(threadStack), THREAD_PRIORITY_MAIN - 1, 0, Iperf_SenderThread, NULL, "iperf_sender"); 
  }
  running = true;
  return 0;
}

int Iperf_Deinit(void) // TODO if sender quits on its own you have to call this somehow. until then, do the stop/start manually
{
  running = false;
  msg_t m;
  m.type = IPERF_IPC_MSG_STOP;
  msg_send(&m, threadPid);
  threadPid = KERNEL_PID_UNDEF;
  loginfo("Deinitialized\n");
  return 0;
}

int Iperf_CmdHandler(int argc, char **argv) // Bit of a mess. maybe move it to own file
{
  if (argc < 2)
  {
    goto usage;
  }

  if (strncmp(argv[1], "sender", 16) == 0)
  {
    loginfo("STARTING IPERF SENDER AT %s\n", dstGlobalIpAddr);
    Iperf_Init(true);
    if (argc > 2 && strncmp(argv[2], "start", 16) == 0)
    {
      msg_t m;
      m.type = IPERF_IPC_MSG_START;
      msg_send(&m, threadPid);
    }
  }
  else if (strncmp(argv[1], "receiver", 16) == 0)
  {
    Iperf_Init(false);
  }
  else if (strncmp(argv[1], "start", 16) == 0)
  {
    // TODO do we want this? sender should IDLE until receiving this command so that 
    // ECHO commands can still be sent and received
    if (!running)
    {
      logerror("First select a role: iperf <sender|receiver>");
      return 1;
    }
    msg_t m;
    m.type = IPERF_IPC_MSG_START;
    msg_send(&m, threadPid);
  }
  else if (strncmp(argv[1], "stop", 16) == 0)
  {
    if (!running)
    {
      return 1;
    }
    getNetifStats();
    Iperf_Deinit();
  }
  else if (strncmp(argv[1], "delay", 16) == 0)
  {
    if (argc < 3)
    {
      loginfo("Current delay %d us\n", config.delayUs);
      return 0;
    }
    if (running)
    {
      logerror("Need to first stop iperf!\n");
      return 1;
    }
    config.delayUs = atoi(argv[2]);
    loginfo("Set delay to %d us\n", config.delayUs);
  }
  else if (strncmp(argv[1], "log", 16) == 0)
  {
    if (argc < 4)
    {
      for (int i = 0; i < LOGPRINT_MAX; i++)
      {
        loginfo("Logprint tag %c : %s\n", logprintTagChars[i], (logprintTags[i])?"ON":"OFF");
      }
      loginfo("Usage: iperf log <i|v|e|d|all> <0|1>\n");
    }
    else
    {
      bool enabled = (atoi(argv[3]) == 1);
      char tag = argv[2][0];
      switch(tag)
      {
        case 'i':
          {
            logprintTags[INFO] = enabled;
            break;
          }
        case 'v':
          {
            logprintTags[VERBOSE] = enabled;
            break;
          }
        case 'e':
          {
            logprintTags[ERROR] = enabled;
            break;
          }
        case 'd':
          {
            logprintTags[DEBUG] = enabled;
            break;
          }
        case 'a':
          {
            for (int i = 0; i < LOGPRINT_MAX; i++)
            {
              logprintTags[i] = enabled;
            }
            break;
          }
        default:
          {
            logerror("Bad tag! %c\n", tag);
            logerror("Usage: iperf log <i|v|e|d|all> <0|1>\n");
          }
      }
    }
  }
  else if (strncmp(argv[1], "config", 16) == 0)
  {
    if (argc < 3)
    {
      Iperf_PrintConfig(false);
      return 0;
    }
    else if (strncmp(argv[2], "json", 16) == 0)
    {
      Iperf_PrintConfig(true);
      return 0;
    }
    else if (strncmp(argv[2], "sync", 16) == 0)
    {
      return Iperf_SendConfig();
    }
    else
    {
      if (running)
      {
        logerror("Stop iperf first!\n");
        return 1;
      }
      uint8_t argIdx = 2;
      while (argIdx < argc)
      {
        if (strncmp(argv[argIdx], "payloadsizebytes", 16) == 0)
        {
          config.payloadSizeBytes = atoi(argv[argIdx+1]);
          config.numPktsToTransfer = (config.transferSizeBytes / config.payloadSizeBytes);
          loginfo("Set payloadSizeBytes to %d\n", config.payloadSizeBytes);
          argIdx+=2;
          continue;
        }
        else if (strncmp(argv[argIdx], "pktpersecond", 16) == 0)
        {
          config.pktPerSecond = atoi(argv[argIdx+1]);
          loginfo("Set pktpersecond to %d\n", config.pktPerSecond);
          argIdx+=2;
          continue;
        }
        else if (strncmp(argv[argIdx], "delayus", 16) == 0)
        {
          config.delayUs = atoi(argv[argIdx+1]);
          loginfo("Set delayus to %d\n", config.delayUs);
          argIdx+=2;
          continue;
        }
        else if (strncmp(argv[argIdx], "transfertimeus", 16) == 0)
        {
          config.transferTimeUs = atoi(argv[argIdx+1]);
          loginfo("Set transferTimeUs to %d\n", config.transferTimeUs);
          argIdx+=2;
          continue;
        }
        else if (strncmp(argv[argIdx], "transfersizebytes", 16) == 0)
        {
          config.transferSizeBytes = atoi(argv[argIdx+1]);
          config.numPktsToTransfer = (config.transferSizeBytes / config.payloadSizeBytes);
          loginfo("Set transferSizeBytes to %d\n", config.transferSizeBytes);
          argIdx+=2;
          continue;
        }
        else if (strncmp(argv[argIdx], "mode", 16) == 0)
        {
          uint8_t newMode = atoi(argv[argIdx+1]);
          if (newMode < IPERF_MODE_MAX)
          {
            config.mode = newMode;
            loginfo("Set mode to %d\n", config.mode);
            argIdx+=2;
            continue;
          }
        }
        else
        {
          logerror("Wrong config parameter %s. Available options:\npayloadsize, pktpersecond, delayus, transfertimeus, transfersizebytes, mode\n", argv[argIdx]);
          /*return 1;*/
        }
        argIdx++;
      }

      Iperf_PrintConfig(false);
    }
  }
  else if (strncmp(argv[1], "results", 16) == 0)
  {
    if (argc > 2)
    {
      if (strncmp(argv[2], "json", 16) == 0)
      {
        printResults(true);
      }
      else if (strncmp(argv[2], "all", 16) == 0)
      {
        printAll();
      }
      else if (strncmp(argv[2], "file", 16) == 0)
      {
        printFileTransferStatus();
      }
      else if (strncmp(argv[2], "contents", 16) == 0)
      {
        printReceivedFileContents();
      }
      else if (strncmp(argv[2], "reset", 16) == 0)
      {
        Iperf_ResetResults();
      }
    }
    else
    {
      printResults(false);
    }
  }
  else if (strncmp(argv[1], "target", 16) == 0)
  {
    if (argc > 2)
    {
      strncpy((char *) &dstGlobalIpAddr, argv[2], 25);
    }
    loginfo("%s\n", dstGlobalIpAddr);
  }
  else if (strncmp(argv[1], "echo", 16) == 0)
  {
    char *str = (argc >= 2) ? argv[2] : "TEST";
    return Iperf_SendEcho(str);
  }
  else
  {
    goto usage;
  }

  return 0;

usage:
  logerror("Usage: iperf <sender|receiver|start|stop|delay|log|config|target|results|echo>\n");
  return 1;
}

SHELL_COMMAND(iperf, "Iperf command handler", Iperf_CmdHandler);


