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
#define IPERF_PAYLOAD_SIZE_MAX 16 
#define RECEIVER_MSG_QUEUE_SIZE 8

static sock_udp_t udpSock;
static sock_tcp_t tcpSock;
static sock_tcp_t controlSock;

static uint32_t pktPerSecond = 0; // TODO
static uint32_t delayUs = 1000000;

static volatile bool running = false;

static char receiver_thread_stack[THREAD_STACKSIZE_DEFAULT];
static char sender_thread_stack[THREAD_STACKSIZE_DEFAULT];

static msg_t _msg_queue[RECEIVER_MSG_QUEUE_SIZE];

static IperfConfig_s config;

static kernel_pid_t sender_pid = 0;
static kernel_pid_t receiver_pid = 0;

static gnrc_netreg_entry_t server = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF);

static bool printEnabled = true;

// Dunno if any of whats below is really needed but lets see how it goe
/*typedef enum {*/
/*  IPERF_CTRL_MSG,*/
/*  IPERF_PAYLOAD_MSG,*/
/*  IPERF_MSG_TYPE_MAX*/
/*} IperfMsgType_e;*/
/**/
/*typedef enum {*/
/*  IPERF_BEGIN_MSG,*/
/*  IPERF_BEGIN_RSP,*/
/*  IPERF_STOP_MSG,*/
/*  IPERF_STOP_RSP,*/
/*  IPERF_CTRL_MSG_MAX*/
/*} IperfCtrlMsg_e;*/

/*typedef struct */
/*{*/
/**/
/*} IperfExperimentTraffic_t;*/

/*typedef struct {*/
/*  uint8_t flags;*/
/*  uint32_t seq_no;*/
/*  uint8_t payload[];*/
/*} __attribute__((packed)) iperf_udp_pkt_t;*/
/**/
/*
* Iperf
*
*
*
*/

void logprint( const char* format, ... )  // TODO eventually move prints here and enable/disable them on the fly
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
    logprint("[IPERF] Success: sent %u byte(s) to [%s]:%u\n", payload_size, addr_str,
           port);
    if (num) {
      ztimer_sleep(ZTIMER_USEC, delayUs);
    }
  }
  return 0;
}

static uint32_t seqCounter = 0;
static uint16_t lossCounter = 0;
static int receiverHandleIperfPayload(gnrc_pktsnip_t *pkt)
{
  iperf_udp_pkt_t *iperfPl = (iperf_udp_pkt_t *) pkt;
  logprint("Received seq no %d\n", iperfPl->seq_no);
  logprint("Payload %s\n", iperfPl->payload);
  logprint("Losses %d\n", lossCounter);
  if (seqCounter == iperfPl->seq_no - 1)
  {
    // No loss
  }
  else
  {
    // Loss happened
    lossCounter += seqCounter - iperfPl->seq_no;
  }
  seqCounter = iperfPl->seq_no;
}

static int receiverHandlePkt(gnrc_pktsnip_t *pkt)
{
  int snips = 0;
  int size = 0;
  gnrc_pktsnip_t *snip = pkt;
  logprint("[IPERF] Handle packet\n");
  while(snip != NULL)
  {
    logprint("SNIP %d. %d bytes. type: %d ", snips, snip->size, snip->type);
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
          for (int i = 0; i < snip->size; i++)
          {
            char data = * (char *) (snip->data + i);
            logprint("%02x(%c) %s", data, data, (i % 8 == 0 && i > 0) ? "\n" : "");
          }
          receiverHandleIperfPayload((iperf_udp_pkt_t *) snip->data);
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
  /*sock_udp_ep_t remote = * (sock_udp_ep_t *) ctx;*/
  logprint("[IPERF] %s Thread start\n", __FUNCTION__);
  uint8_t txBuffer[IPERF_PAYLOAD_SIZE_MAX];
  memset(&txBuffer, 0x00, IPERF_PAYLOAD_SIZE_MAX);

  iperf_udp_pkt_t *pl = (void *) &txBuffer;

  char *plString = "ASDFQWERASDFQWE\0";
  strncpy((char *) &pl->payload, plString, strlen(plString));

  pl->seq_no = 0;
  pl->pl_size = 16;

  while (running)
  {
    char *dstAddr;
    dstAddr = "2001::2";
    int sendRet = socklessUdpSend(dstAddr, "1", (char *) pl, 32, 1, delayUs);
    ztimer_sleep(ZTIMER_USEC, delayUs);
    pl->seq_no++;
  }
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
        logprint("[IPERF] Data received");
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
  lossCounter = 0;
  seqCounter = 0;
  config.iAmSender = iAmSender;
  config.senderPort = 1; // TODO make more generic? 
  config.listenerPort = 1;
 
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
      logprint("[IPERF] Current delay %d us\n", delayUs);
      return 0;
    }
    if (running)
    {
      logprint("[IPERF] Need to first stop iperf!\n");
      return 1;
    }
    delayUs = atoi(argv[2]);
    logprint("[IPERF] Set delay to %d us\n", delayUs);
  }
  else if (strncmp(argv[1], "toggleprint", 16) == 0)
  {
    logprint("[IPERF] Toggling iperf prints\n");
    printEnabled = !printEnabled;
  }
  else
  {
    goto usage;
  }

  return 0;

usage:
  logprint("[IPERF] Usage: iperf <sender|receiver|stop|delay|toggleprint>\n");
  return 1;
}

SHELL_COMMAND(iperf, "Iperf command handler", Iperf_CmdHandler);


