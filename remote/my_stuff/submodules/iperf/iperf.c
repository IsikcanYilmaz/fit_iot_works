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

static sock_udp_t udpSock;
static sock_tcp_t tcpSock;
static sock_tcp_t controlSock;

static uint32_t delayUs = 1000000;

static volatile bool running = false;

static char rx_thread_stack[THREAD_STACKSIZE_DEFAULT];
static char tx_thread_stack[THREAD_STACKSIZE_DEFAULT];

/*
* Iperf
*
*
*
*/

static void *Iperf_TxThread(void *ctx)
{
  sock_udp_ep_t remote = * (sock_udp_ep_t *) *ctx;
  printf("%s Thread start\n", __FUNCTION__);
  while (running)
  {
    int sendRet = sock_udp_send(&sock, ping, sizeof(*ping) + payload_size, &remote);
    if (sendRet < 0)
    {
      printf("%s Error sending udp packet %d\n", __FUNCTION__, sendRet);
    }
    ztimer_sleep(ZTIMER_USEC, delayUs);
  }
}

static void *Iperf_RxThread(void *ctx)
{
  
  while (running)
  {
    
    ztimer_sleep(ZTIMER_USEC, delayUs);
  }
}

int Iperf_Init(void)
{
  netif_t *netif;
  sock_udp_ep_t local = {.family = AF_INET6, .netif = SOCK_ADDR_ANY_NETIF, .port = DEFAULT_PORT };
  sock_udp_ep_t remote = {.family = AF_INET6, .port = DEFAULT_PORT };

  if (sock_udp_create(&udpSock, &local, NULL, 0) < 0)
  {
    printf("%s ERROR Creating UDP socket\n", __FUNCTION__);
    return 1;
  }

  // TODO take this arg from somewhere else 
  
  char *server = "2001::2/128";

  if (netutils_get_ipv6((ipv6_addr_t *)&remote.addr.ipv6, &netif, server) < 0) {
    puts("can't resolve remote address");
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
