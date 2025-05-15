#include <stdio.h>

#include "macros/utils.h"
#include "net/gnrc.h"
#include "net/sock/udp.h"
#include "net/sock/tcp.h"
#include "net/utils.h"
#include "sema_inv.h"
#include "test_utils/benchmark_udp.h"
#include "ztimer.h"
#include "shell.h"

#include "iperf.h"

#define ENABLE_DEBUG 0
#include "debug.h"

#define IPERF_DEFAULT_PORT 1
#define IPERF_PAYLOAD_SIZE_MAX 16 

static sock_udp_t udpSock;
static sock_tcp_t tcpSock;
static sock_tcp_t controlSock;

static uint32_t pktPerSecond = 0; // TODO
static uint32_t delayUs = 1000000;

static volatile bool running = false;

static char rx_thread_stack[THREAD_STACKSIZE_DEFAULT];
static char tx_thread_stack[THREAD_STACKSIZE_DEFAULT];

typedef struct {
  uint32_t flags;
  uint32_t seq_no;
  uint8_t payload[];
} iperf_udp_pkt_t;

/*
* Iperf
*
*
*
*/

// SOCKLESS 
static void udpSendSockless(void) // TODO make generic
{
  netif_t *netif;
  uint16_t port;
  ipv6_addr_t addr;

  char *addr_str = "2001::2"; // TODO g

  /* parse destination addr */
  if (netutils_get_ipv6(&addr, &netif, addr_str) < 0)
  {
    printf("%s: %d Error\n", __FUNCTION__, __LINE__);
    return;
  }

  /* parse port */
  port = 1; // TODO g

  

}

static void *Iperf_TxThread(void *ctx)
{
  sock_udp_ep_t remote = * (sock_udp_ep_t *) ctx;
  printf("%s Thread start\n", __FUNCTION__);

  uint8_t txBuffer[IPERF_PAYLOAD_SIZE_MAX];

  iperf_udp_pkt_t *pl = (void *) &txBuffer;

  while (running)
  {
    printf("tx \n");
    int sendRet = sock_udp_send(&udpSock, &txBuffer, 8, &remote);
    printf("%d\n", sendRet);
    /*if (sendRet < 0)*/
    /*{*/
    /*  printf("%s Error sending udp packet %d\n", __FUNCTION__, sendRet);*/
    /*}*/
    /*ztimer_sleep(ZTIMER_USEC, delayUs);*/
    ztimer_sleep(ZTIMER_MSEC, 1000);
  }
}

static void *Iperf_RxThread(void *ctx)
{
  while (running)
  {
    printf("rx\n");
    ztimer_sleep(ZTIMER_USEC, delayUs);
  }
}

int Iperf_Init(void)
{
  netif_t *netif;
  sock_udp_ep_t local = {.family = AF_INET6, .netif = SOCK_ADDR_ANY_NETIF, .port = IPERF_DEFAULT_PORT };
  sock_udp_ep_t remote = {.family = AF_INET6, .port = IPERF_DEFAULT_PORT };

  if (sock_udp_create(&udpSock, &local, NULL, 0) < 0)
  {
    printf("%s ERROR Creating UDP socket\n", __FUNCTION__);
    return 1;
  }

  // TODO take this arg from somewhere else 
  
  char *server = "2001::2";
  /*char *server = "fe80::ec0a:1a43:ebf7:2f8f";*/

  if (netutils_get_ipv6((ipv6_addr_t *)&remote.addr.ipv6, &netif, server) < 0) {
    printf("%s can't resolve remote address\n", __FUNCTION__);
    sock_udp_close(&udpSock);
    return 1;
  }
  if (netif) {
    remote.netif = netif_get_id(netif);
  } else {
    remote.netif = SOCK_ADDR_ANY_NETIF;
  }

  running = true;
  thread_create(rx_thread_stack, sizeof(rx_thread_stack), THREAD_PRIORITY_MAIN - 2, 0, Iperf_RxThread, NULL, "Iperf rx thread");
  thread_create(tx_thread_stack, sizeof(tx_thread_stack), THREAD_PRIORITY_MAIN - 1, 0, Iperf_TxThread, &remote, "Iperf tx thread");

  return 0;
}

int Iperf_CmdHandler(int argc, char **argv)
{
  printf("STARTING IPERF AGAINST 2001::2/120\n");
  Iperf_Init();
  return 0;
}

SHELL_COMMAND(iperf, "Iperf command handler", Iperf_CmdHandler);


