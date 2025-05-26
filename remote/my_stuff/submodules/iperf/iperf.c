#include <stdio.h>
#include <stdarg.h>
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

#include "iperf.h"
#include "iperf_pkt.h"

#define ENABLE_DEBUG 0
#include "debug.h"

#define IPERF_DEFAULT_PORT 1
#define IPERF_PAYLOAD_DEFAULT_SIZE_BYTES 32
#define IPERF_PAYLOAD_MAX_SIZE_BYTES 512
#define IPERF_DEFAULT_DELAY_US 1000000
#define IPERF_DEFAULT_PKT_PER_SECOND // TODO
#define IPERF_DEFAULT_TRANSFER_SIZE_BYTES (IPERF_PAYLOAD_DEFAULT_SIZE_BYTES * 16) // 512 bytes
#define IPERF_DEFAULT_TRANSFER_TIME_US (IPERF_DEFAULT_DELAY_US * 10) // 10 secs

#define RECEIVER_MSG_QUEUE_SIZE 16

static sock_udp_t udpSock;
static sock_tcp_t tcpSock;
static sock_tcp_t controlSock;

static IperfConfig_s config = {
  .payloadSizeBytes = IPERF_PAYLOAD_DEFAULT_SIZE_BYTES,
  .pktPerSecond = 0, // TODO
  .delayUs = 1000000,
  .transferSizeBytes = IPERF_DEFAULT_TRANSFER_SIZE_BYTES,
  .transferTimeUs = IPERF_DEFAULT_TRANSFER_TIME_US,
  .mode = IPERF_MODE_TIMED,
};

static IperfResults_s results;

static volatile bool running = false;

static char receiver_thread_stack[THREAD_STACKSIZE_DEFAULT];
static char sender_thread_stack[THREAD_STACKSIZE_DEFAULT];
static uint8_t txBuffer[IPERF_PAYLOAD_MAX_SIZE_BYTES];

static msg_t _msg_queue[RECEIVER_MSG_QUEUE_SIZE];

static kernel_pid_t sender_pid = 0;
static kernel_pid_t receiver_pid = 0;

static gnrc_netreg_entry_t server = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF);

/////////////////////////////////// LOGPRINT TODO maybe move this chunk into its own file? or module?
typedef enum
{
  INFO,
  VERBOSE,
  ERROR,
  DEBUG,
  LOGPRINT_MAX
} LogprintTag_e;

bool logprintTags[LOGPRINT_MAX] = // TODO this can be a bitmap if you really want to deal with it
  {
    [INFO] = true,
    [VERBOSE] = false,
    [ERROR] = true,
    [DEBUG] = false 
  };

char logprintTagChars[LOGPRINT_MAX] = 
  {
    [INFO] = 'I',
    [VERBOSE] = 'V',
    [ERROR] = 'E',
    [DEBUG] = 'D'
  };

static void _logprint(LogprintTag_e tag, const char* format, ... )
{
  if (!logprintTags[tag])
  {
    return;
  }
  va_list args;
  va_start( args, format );
  printf("[IPERF][%c] ", logprintTagChars[tag]);
  vprintf( format, args );
  va_end( args );
}

#define loginfo(...) _logprint(INFO, __VA_ARGS__) // default
#define logverbose(...) _logprint(VERBOSE, __VA_ARGS__)
#define logdebug(...) _logprint(DEBUG, __VA_ARGS__)
#define logerror(...) _logprint(ERROR, __VA_ARGS__)

///////////////////////////////////

static void printConfig(bool json)
{
  loginfo((json) ? "{\"iAmSender\":%d, \"payloadSizeBytes\":%d, \"pktPerSecond\":%d, \"delayUs\":%d, \"mode\":%d, \"transferSizeBytes\":%d, \"transferTimeUs\":%d}\n" : \
           "Sender %d, Payload size %d, Pkt per second %d, DelayUs %d, Mode %d, Transfer Size %d, Transfer Time %d\n", 
           config.iAmSender, 
           config.payloadSizeBytes, 
           config.pktPerSecond, 
           config.delayUs, 
           config.mode, 
           config.transferSizeBytes, 
           config.transferTimeUs);
}

static void printResults(bool json)
{
  loginfo((json) ? \
           "{\"iAmSender\":%d, \"lastPktSeqNo\":%d, \"pktLossCounter\":%d, \"numReceivedPkts\":%d, \"numReceivedBytes\":%d, \"numDuplicates\":%d, \"numSentPkts\":%d, \"numSentBytes\":%d, \"startTimestamp\":%i, \"endTimestamp\":%i, \"timeDiff\":%d}\n" : \
           "Results\niAmSender           :%d\nlastPktSeqNo        :%d\npktLossCounter      :%d\nnumReceivedPkts     :%d\nnumReceivedBytes    :%d\nnumDuplicates       :%d\nnumSentPkts         :%d\nnumSentBytes        :%d\nstartTimestamp      :%i\nendTimestamp        :%i\ntimeDiff            :%d\n", \
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
           results.endTimestamp - results.startTimestamp);
}

static void setDelayFromPps(uint16_t pktPerSecond)
{

}

static void setPpsFromDelay(uint32_t delayUs)
{

}

static bool receivedPktIds[1024]; // TODO bitmap this?
static void resetResults(void)
{
  memset(&results, 0x00, sizeof(IperfResults_s));
  memset(&receivedPktIds, 0x00, 1024);
  loginfo("Results reset\n");
}

// LISTENER

// SOCKLESS 
// Yanked out of sys/shell/cmds/gnrc_udp.c
// takes address and port in _string_ form!
static int socklessUdpSend(const char *addr_str, const char *port_str, const char *data, size_t dataLen, size_t num, unsigned int delayUs)
{
  netif_t *netif;
  uint16_t port;
  ipv6_addr_t addr;

  /* parse destination address */
  if (netutils_get_ipv6(&addr, &netif, addr_str) < 0) {
    loginfo("Error: unable to parse destination address\n");
    return 1;
  }

  /* parse port */
  port = atoi(port_str);
  if (port == 0) {
    loginfo("Error: unable to parse destination port\n");
    return 1;
  }

  while (num--) {
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
    ip = gnrc_ipv6_hdr_build(udp, NULL, &addr);
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
    logdebug("Success: sent %u byte(s) to [%s]:%u\n", payload_size, addr_str, port);
    results.numSentPkts++;
    if (num) {
      ztimer_sleep(ZTIMER_USEC, delayUs);
    }
  }
  return 0;
}

static int receiverHandleIperfPayload(gnrc_pktsnip_t *pkt)
{
  iperf_udp_pkt_t *iperfPl = (iperf_udp_pkt_t *) pkt;
  logverbose("Received seq no %d\nPayload %s\nLosses %d\nDups %d\n", iperfPl->seq_no, iperfPl->payload, results.pktLossCounter, results.numDuplicates);


  /*if (results.lastPktSeqNo == iperfPl->seq_no - 1)*/
  /*{*/
  /*  // No loss*/
  /*  results.lastPktSeqNo = iperfPl->seq_no;*/
  /*}*/
  /*else if (results.lastPktSeqNo == iperfPl->seq_no && results.lastPktSeqNo != 0)*/
  /*{*/
  /*  // Duplicate*/
  /*  logverbose("DUP %d\n", iperfPl->seq_no);*/
  /*  results.numDuplicates++;*/
  /*}*/
  /*else if (results.lastPktSeqNo < iperfPl->seq_no)*/
  /*{*/
  /*  // Loss happened*/
  /*  results.pktLossCounter += (iperfPl->seq_no - results.lastPktSeqNo);*/
  /*  logverbose("LOSS %d pkts \n", iperfPl->seq_no - results.lastPktSeqNo);*/
  /*  results.lastPktSeqNo = iperfPl->seq_no;*/
  /*}*/


   

  results.numReceivedPkts++;
  results.endTimestamp = ztimer_now(ZTIMER_USEC);
  if (results.numReceivedPkts == 1)
  {
    results.startTimestamp = results.endTimestamp;
  }
}

static int receiverHandlePkt(gnrc_pktsnip_t *pkt)
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
              printf("%02x(%c) %s", data, data, (i % 8 == 0 && i > 0) ? "\n" : "");
            }
          }
          logdebug("\n");
          receiverHandleIperfPayload(snip->data);
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
      case GNRC_NETTYPE_TCP:
        {
          logdebug("TCP\n");
          break;
        }
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

static int startUdpServer(void)
{
  /* check if server is already running or the handler thread is not */
  if (server.target.pid != KERNEL_PID_UNDEF) {
    loginfo("Error: server already running on port %" PRIu32 "\n", server.demux_ctx);
    return 1;
  }
  if (receiver_pid == KERNEL_PID_UNDEF)
  {
    logerror("Error: server thread not running!\n");
    return 1;
  }
  server.target.pid = receiver_pid;
  server.demux_ctx = (uint32_t) IPERF_DEFAULT_PORT;
  gnrc_netreg_register(GNRC_NETTYPE_UDP, &server);
  loginfo("Started UDP server on port %d\n", server.demux_ctx);
  return 0;
}

static int stopUdpServer(void)
{
  /* check if server is running at all */
  if (server.target.pid == KERNEL_PID_UNDEF) {
    logerror("Error: server was not running\n");
    return 1;
  }
  /* stop server */
  gnrc_netreg_unregister(GNRC_NETTYPE_UDP, &server);
  server.target.pid = KERNEL_PID_UNDEF;
  loginfo("Success: stopped UDP server\n"); 
  return 0;
}

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
  return ret;
}

static void *Iperf_SenderThread(void *arg)
{
  loginfo("%s Thread start\n", __FUNCTION__);
  uint16_t pktSize = config.payloadSizeBytes;
  memset(&txBuffer, 0x01, pktSize);

  iperf_udp_pkt_t *pl = (void *) &txBuffer;

  char *plString = "ASDFQWERASDFQWE\0";
  strncpy((char *) &pl->payload, plString, strlen(plString));

  pl->seq_no = 0;
  pl->pl_size = 16;

  results.startTimestamp = ztimer_now(ZTIMER_USEC);

  while (!isTransferDone())
  {
    char *dstAddr;
    dstAddr = "2001::2";
    int sendRet = socklessUdpSend(dstAddr, "1", (char *) pl, pktSize, 1, config.delayUs);
    ztimer_sleep(ZTIMER_USEC, config.delayUs);
    pl->seq_no++;
    results.numSentBytes += pktSize;
  }
  results.endTimestamp = ztimer_now(ZTIMER_USEC);
  Iperf_Deinit();
  loginfo("Sender task done\n");
  printResults(false);
  loginfo("Sender thread exiting\n");
}

static void *Iperf_ReceiverThread(void *arg)
{
  (void)arg;
  msg_t msg, reply;

  /* setup the message queue */
  msg_init_queue(_msg_queue, RECEIVER_MSG_QUEUE_SIZE);

  reply.content.value = (uint32_t)(-ENOTSUP);
  reply.type = GNRC_NETAPI_MSG_TYPE_ACK;

  while (running) {
    msg_receive(&msg);

    switch (msg.type) {
      case GNRC_NETAPI_MSG_TYPE_RCV:
        logdebug("Data received\n");
        receiverHandlePkt(msg.content.ptr);
        break;
      case GNRC_NETAPI_MSG_TYPE_SND:
        logdebug("Data to send");
        receiverHandlePkt(msg.content.ptr);
        break;
      case GNRC_NETAPI_MSG_TYPE_GET:
      case GNRC_NETAPI_MSG_TYPE_SET:
        msg_reply(&msg, &reply);
        break;
      default:
        /*loginfo("received something unexpected");*/
        break;
    }
  }
  loginfo("Receiver thread exiting");
}

int Iperf_Init(bool iAmSender)
{
  if (running)
  {
    logerror("Already running!\n");
    return 1;
  }

  resetResults();

  config.iAmSender = iAmSender;
  config.senderPort = IPERF_DEFAULT_PORT; // TODO make more generic? TODO make address more generic
  config.listenerPort = IPERF_DEFAULT_PORT;
 
  running = true;
  if (!iAmSender)
  {
    receiver_pid = thread_create(receiver_thread_stack, sizeof(receiver_thread_stack), THREAD_PRIORITY_MAIN - 2, 0, Iperf_ReceiverThread, NULL, "Iperf receiver thread");

    startUdpServer();
  }
  if (iAmSender)
  {
    sender_pid = thread_create(sender_thread_stack, sizeof(sender_thread_stack), THREAD_PRIORITY_MAIN - 1, 0, Iperf_SenderThread, NULL, "Iperf sender thread"); 
  }

  return 0;
}

int Iperf_Deinit(void)
{
  stopUdpServer();
  running = false;
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
    loginfo("STARTING IPERF SENDER AT 2001::2/128\n");
    Iperf_Init(true);
  }
  else if (strncmp(argv[1], "receiver", 16) == 0)
  {
    Iperf_Init(false);
  }
  else if (strncmp(argv[1], "stop", 16) == 0)
  {
    if (!running)
    {
      return 1;
    }
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
      printConfig(false);
      return 0;
    }
    else
    {
      if (running)
      {
        logerror("Stop iperf first!");
        return 1;
      }
      if (strncmp(argv[2], "payloadsize", 16) == 0)
      {
        config.payloadSizeBytes = atoi(argv[3]);
        loginfo("Set payloadSizeBytes to %d\n", config.payloadSizeBytes);
      }
      else if (strncmp(argv[2], "pktpersecond", 16) == 0)
      {
        config.pktPerSecond = atoi(argv[3]);
        loginfo("Set pktpersecond to %d\n", config.pktPerSecond);
      }
      else if (strncmp(argv[2], "delayus", 16) == 0)
      {
        config.delayUs = atoi(argv[3]);
        loginfo("Set delayus to %d\n", config.delayUs);
      }
      else if (strncmp(argv[2], "transfertimeus", 16) == 0)
      {
        config.transferTimeUs = atoi(argv[3]);
        loginfo("Set transferTimeUs to %d\n", config.transferTimeUs);
      }
      else if (strncmp(argv[2], "transfersizebytes", 16) == 0)
      {
        config.transferSizeBytes = atoi(argv[3]);
        loginfo("Set transferSizeBytes to %d\n", config.transferSizeBytes);
      }
      else if (strncmp(argv[2], "mode", 16) == 0)
      {
        uint8_t newMode = atoi(argv[3]);
        if (newMode < IPERF_MODE_MAX)
        {
          config.mode = newMode;
          loginfo("Set mode to %d\n", config.mode);
        }
      }
      else
      {
        logerror("Wrong config parameter %s. Available options:\npayloadsize, pktpersecond, delayus, transfertimeus, transfersizebytes, mode\n", argv[2]);
        return 1;
      }
      printConfig(false);
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
      else if (strncmp(argv[2], "reset", 16) == 0)
      {
        resetResults();
      }
    }
    else
    {
      printResults(false);
    }
  }
  else
  {
    goto usage;
  }

  return 0;

usage:
  logerror("Usage: iperf <sender|receiver|stop|delay|log|config|results>\n");
  return 1;
}

SHELL_COMMAND(iperf, "Iperf command handler", Iperf_CmdHandler);


