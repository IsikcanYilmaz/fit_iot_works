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

static bool running = false;

int Iperf_Init(void)
{
  return 0;
}
