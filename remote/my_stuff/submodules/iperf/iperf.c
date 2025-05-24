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
#define IPERF_PAYLOAD_MAX_SIZE_BYTES 1024
#define IPERF_DEFAULT_DELAY_US 1000000
#define IPERF_DEFAULT_PKT_PER_SECOND 
#define RECEIVER_MSG_QUEUE_SIZE 16

static sock_udp_t udpSock;
static sock_tcp_t tcpSock;
static sock_tcp_t controlSock;

static IperfConfig_s config = {
  .payloadSizeBytes = IPERF_PAYLOAD_DEFAULT_SIZE_BYTES,
  .pktPerSecond = 0, // TODO
  .delayUs = 1000000,
  .mode = IPERF_MODE_SIZE,
};

static IperfResults_s results;

static volatile bool running = false;

static char receiver_thread_stack[THREAD_STACKSIZE_DEFAULT];
static char sender_thread_stack[THREAD_STACKSIZE_DEFAULT];

static msg_t _msg_queue[RECEIVER_MSG_QUEUE_SIZE];

static kernel_pid_t sender_pid = 0;
static kernel_pid_t receiver_pid = 0;

static gnrc_netreg_entry_t server = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF);

static bool printEnabled = true;

static void logprint( const char* format, ... )  // TODO eventually move prints here and enable/disable them on the fly
{
  if (!printEnabled)
  {
    return;
  }
  va_list args;
  va_start( args, format );
  vprintf( format, args );
  va_end( args );
}

static void printConfig(bool json)
{
  if (json)
  {
    logprint("{\"iAmSender\":%d, \"payloadSizeBytes\":%d, \"pktPerSecond\":%d, \"delayUs\":%d}\n", 
           config.iAmSender, config.payloadSizeBytes, config.pktPerSecond, config.delayUs);
  }
  else
  {
    logprint("[IPERF] I am %s, Payload size %d, Pkt per second %d, DelayUs %d\n", 
          (config.iAmSender) ? "SENDER" : "RECEIVER", config.payloadSizeBytes, config.pktPerSecond, config.delayUs);
  }
}

static void printResults(bool json)
{
    logprint((json) ? \
             "{\"iAmSender\":%d, \"lastPktSeqNo\":%d, \"pktLossCounter\":%d, \"numReceivedPkts\":%d, \"numReceivedBytes\":%d, \"numReceivedGoodBytes\":%d, \"numDuplicates\":%d, \"numSentPkts\":%d, \"startTimestamp\":%d, \"endTimestamp\":%d, \"timeDiff\":%d}\n" : \
             "iAmSender:%d\nlastPktSeqNo:%d\npktLossCounter:%d\nnumReceivedPkts:%d\nnumReceivedBytes:%d\nnumReceivedGoodBytes:%d\nnumDuplicates:%d\nnumSentPkts:%d\nstartTimestamp:%d\nendTimestamp:%d\ntimeDiff:%d\n", \
             config.iAmSender, \
             results.lastPktSeqNo, \
             results.pktLossCounter, \
             results.numReceivedPkts, \
             results.numReceivedBytes, \
             results.numReceivedGoodBytes, \
             results.numDuplicates, \
             results.numSentPkts, \
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
    logprint("[IPERF] Error: unable to parse destination address\n");
    return 1;
  }

  /* parse port */
  port = atoi(port_str);
  if (port == 0) {
    logprint("[IPERF] Error: unable to parse destination port\n");
    return 1;
  }

  while (num--) {
    gnrc_pktsnip_t *payload, *udp, *ip;
    unsigned payload_size;

    /* allocate payload */
    payload = gnrc_pktbuf_add(NULL, data, dataLen, GNRC_NETTYPE_UNDEF);
    if (payload == NULL) {
      logprint("[IPERF] Error: unable to copy data to packet buffer\n");
      return 1;
    }

    /* store size for output */
    payload_size = (unsigned)payload->size;

    /* allocate UDP header, set source port := destination port */
    udp = gnrc_udp_hdr_build(payload, port, port);
    if (udp == NULL) {
      logprint("[IPERF] Error: unable to allocate UDP header\n");
      gnrc_pktbuf_release(payload);
      return 1;
    }

    /* allocate IPv6 header */
    ip = gnrc_ipv6_hdr_build(udp, NULL, &addr);
    if (ip == NULL) {
      logprint("[IPERF] Error: unable to allocate IPv6 header\n");
      gnrc_pktbuf_release(udp);
      return 1;
    }

    /* add netif header, if interface was given */
    if (netif != NULL) {
      gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, NULL, 0);
      if (netif_hdr == NULL) {
        logprint("[IPERF] Error: unable to allocate netif header\n");
        gnrc_pktbuf_release(ip);
        return 1;
      }
      gnrc_netif_hdr_set_netif(netif_hdr->data, container_of(netif, gnrc_netif_t, netif));
      ip = gnrc_pkt_prepend(ip, netif_hdr);
    }

    /* send packet */
    if (!gnrc_netapi_dispatch_send(GNRC_NETTYPE_UDP,
                                   GNRC_NETREG_DEMUX_CTX_ALL, ip)) {
      logprint("[IPERF] Error: unable to locate UDP thread\n");
      gnrc_pktbuf_release(ip);
      return 1;
    }

    /* access to `payload` was implicitly given up with the send operation
     * above
     * => use temporary variable for output */
    logprint("[IPERF] Success: sent %u byte(s) to [%s]:%u\n", payload_size, addr_str, port);
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
  logprint("Received seq no %d\n", iperfPl->seq_no);
  logprint("Payload %s\n", iperfPl->payload);
  logprint("Losses %d\n", results.pktLossCounter);
  logprint("Dups %d\n", results.numDuplicates);
  if (results.lastPktSeqNo == iperfPl->seq_no - 1)
  {
    // No loss
    results.lastPktSeqNo = iperfPl->seq_no;
    results.numReceivedGoodBytes += pkt->size;
  }
  else if (results.lastPktSeqNo == iperfPl->seq_no)
  {
    // Duplicate
    printf("DUP %d\n", iperfPl->seq_no);
    results.numDuplicates++;
  }
  else if (results.lastPktSeqNo < iperfPl->seq_no)
  {
    // Loss happened
    results.pktLossCounter += (iperfPl->seq_no - results.lastPktSeqNo);
    printf("LOSS %d pkts \n", iperfPl->seq_no - results.lastPktSeqNo);
    results.lastPktSeqNo = iperfPl->seq_no;
  }
  results.numReceivedPkts++;
  results.numReceivedBytes += pkt->size;
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
  logprint("[IPERF] Handle packet\n");
  while(snip != NULL)
  {
    /*logprint("SNIP %d. %d bytes. type: %d ", snips, snip->size, snip->type);*/
    switch(snip->type)
    {
      case GNRC_NETTYPE_NETIF:
        {
          logprint("NETIF");
          break;
        }
      case GNRC_NETTYPE_UNDEF:  // APP PAYLOAD HERE
        {
          logprint("UNDEF\n");
          /*for (int i = 0; i < snip->size; i++)*/
          /*{*/
          /*  char data = * (char *) (snip->data + i);*/
          /*  logprint("%02x(%c) %s", data, data, (i % 8 == 0 && i > 0) ? "\n" : "");*/
          /*}*/
          /*logprint("\n");*/
          receiverHandleIperfPayload(snip->data);
          break;
        }
      case GNRC_NETTYPE_SIXLOWPAN:
        {
          logprint("6LP");
          break;
        }
      case GNRC_NETTYPE_IPV6:
        {
          logprint("IPV6");
          break;
        }
      case GNRC_NETTYPE_ICMPV6:
        {
          logprint("ICMPV6");
          break;
        }
      case GNRC_NETTYPE_TCP:
        {
          logprint("TCP");
          break;
        }
      case GNRC_NETTYPE_UDP:
        {
          logprint("UDP");
          break;
        }
      default:
        {
          logprint("NONE");
          break;
        }
    }
    logprint("\n");
    size += snip->size;
    snip = snip->next;
    snips++;
  }
  logprint("\n");

  gnrc_pktbuf_release(pkt);
  return 1;
}

static int startUdpServer(void)
{
  /* check if server is already running or the handler thread is not */
  if (server.target.pid != KERNEL_PID_UNDEF) {
    logprint("Error: server already running on port %" PRIu32 "\n", server.demux_ctx);
    return 1;
  }
  if (receiver_pid == KERNEL_PID_UNDEF)
  {
    logprint("[IPERF] Error: server thread not running!\n");
    return 1;
  }
  server.target.pid = receiver_pid;
  server.demux_ctx = (uint32_t) IPERF_DEFAULT_PORT;
  gnrc_netreg_register(GNRC_NETTYPE_UDP, &server);
  logprint("[IPERF] Started UDP server on port %d\n", server.demux_ctx);
  return 0;
}

static int stopUdpServer(void)
{
  /* check if server is running at all */
  if (server.target.pid == KERNEL_PID_UNDEF) {
    logprint("[IPERF] Error: server was not running\n");
    return 1;
  }
  /* stop server */
  gnrc_netreg_unregister(GNRC_NETTYPE_UDP, &server);
  server.target.pid = KERNEL_PID_UNDEF;
  logprint("[IPERF] Success: stopped UDP server\n"); 
  return 0;
}

static void *Iperf_SenderThread(void *arg)
{
  logprint("[IPERF] %s Thread start\n", __FUNCTION__);
  uint8_t txBuffer[config.payloadSizeBytes];
  memset(&txBuffer, 0x00, config.payloadSizeBytes);

  iperf_udp_pkt_t *pl = (void *) &txBuffer;

  char *plString = "ASDFQWERASDFQWE\0";
  strncpy((char *) &pl->payload, plString, strlen(plString));

  pl->seq_no = 0;
  pl->pl_size = 16;

  results.startTimestamp = ztimer_now(ZTIMER_USEC);
  while (running)
  {
    char *dstAddr;
    dstAddr = "2001::2";
    int sendRet = socklessUdpSend(dstAddr, "1", (char *) pl, 32, 1, config.delayUs);
    ztimer_sleep(ZTIMER_USEC, config.delayUs);
    pl->seq_no++;
  }
  results.endTimestamp = ztimer_now(ZTIMER_USEC);
  logprint("[IPERF] Sender thread exiting");
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
        logprint("[IPERF] Data received\n");
        receiverHandlePkt(msg.content.ptr);
        break;
      case GNRC_NETAPI_MSG_TYPE_SND:
        logprint("[IPERF] Data to send");
        receiverHandlePkt(msg.content.ptr);
        break;
      case GNRC_NETAPI_MSG_TYPE_GET:
      case GNRC_NETAPI_MSG_TYPE_SET:
        msg_reply(&msg, &reply);
        break;
      default:
        /*logprint("received something unexpected");*/
        break;
    }
  }

  logprint("[IPERF] Receiver thread exiting");
  /* never reached */
  return NULL;
}

int Iperf_Init(bool iAmSender)
{
  if (running)
  {
    logprint("[IPERF] Already running!\n");
    return 1;
  }

  // Reset our results and config structs
  memset(&results, 0x00, sizeof(IperfResults_s));

  config.iAmSender = iAmSender;
  config.senderPort = IPERF_DEFAULT_PORT; // TODO make more generic? 
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
  logprint("[IPERF] Deinitialized\n");
  return 0;
}

int Iperf_CmdHandler(int argc, char **argv)
{
  if (argc < 2)
  {
    goto usage;
  }

  if (strncmp(argv[1], "sender", 16) == 0)
  {
    logprint("[IPERF] STARTING IPERF SENDER AGAINST 2001::2/128\n");
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
      logprint("[IPERF] Current delay %d us\n", config.delayUs);
      return 0;
    }
    if (running)
    {
      logprint("[IPERF] Need to first stop iperf!\n");
      return 1;
    }
    config.delayUs = atoi(argv[2]);
    logprint("[IPERF] Set delay to %d us\n", config.delayUs);
  }
  else if (strncmp(argv[1], "toggleprint", 16) == 0)
  {
    logprint("[IPERF] Toggling iperf prints\n");
    printEnabled = !printEnabled;
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
        logprint("[IPERF] Stop iperf first!");
        return 1;
      }
      if (strncmp(argv[2], "payloadsize", 16) == 0)
      {
        config.payloadSizeBytes = atoi(argv[3]);
        logprint("[IPERF] Set payloadSizeBytes to %d\n", config.payloadSizeBytes);
      }
      else if (strncmp(argv[2], "pktpersecond", 16) == 0)
      {
        config.pktPerSecond = atoi(argv[3]);
        logprint("[IPERF] Set pktpersecond to %d\n", config.pktPerSecond);
      }
      else if (strncmp(argv[2], "delayus", 16) == 0)
      {
        config.delayUs = atoi(argv[3]);
        logprint("[IPERF] Set delayus to %d\n", config.delayUs);
      }
      else
      {
        logprint("[IPERF] Wrong config parameter %s. Available options:\npayloadsize, pktpersecond, delayus\n", argv[2]);
        return 1;
      }
      printConfig(false);
    }
  }
  else if (strncmp(argv[1], "results", 16) == 0)
  {
    if (argc > 2 && strncmp(argv[2], "json", 16) == 0)
    {
      printResults(true);
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
  logprint("[IPERF] Usage: iperf <sender|receiver|stop|delay|toggleprint|config|results>\n");
  return 1;
}

SHELL_COMMAND(iperf, "Iperf command handler", Iperf_CmdHandler);


