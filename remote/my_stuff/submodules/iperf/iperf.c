#include <stdio.h>

#include "macros/utils.h"
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

static sock_udp_t udpSock;
static sock_tcp_t tcpSock;
static sock_tcp_t controlSock;

static uint32_t delayUs = 1000000;

static bool running = false;

/*
* Iperf
*
*
*
*/

static void *Iperf_TxThread(void *ctx)
{

}

static void *Iperf_RxThread(void *ctx)
{
  
  while (true)
  {
    
    ztimer_sleep(ZTIMER_USEC, delayUs);
  }
}

int Iperf_Init(void)
{
  return 0;
}
