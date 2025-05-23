#include <stdio.h>

#include "macros/utils.h"
#include "net/sock/udp.h"
#include "net/utils.h"
#include "sema_inv.h"
#include "test_utils/benchmark_udp.h"
#include "ztimer.h"
#include "shell.h"

#define ENABLE_DEBUG 0
#include "debug.h"

static sock_udp_t sock;

static char send_thread_stack[THREAD_STACKSIZE_DEFAULT];
static char listen_thread_stack[THREAD_STACKSIZE_DEFAULT];

static uint8_t buf_tx[BENCH_PAYLOAD_SIZE_MAX + sizeof(benchmark_msg_ping_t)];
static benchmark_msg_ping_t *ping = (void *)buf_tx;

static bool running = false;
static sema_inv_t thread_sync;

// CONFIG
static const char *bench_server = BENCH_SERVER_DEFAULT;
static uint16_t bench_port = BENCH_PORT_DEFAULT;
static uint32_t delay_us = US_PER_SEC;
static uint16_t payload_size = 32;

struct {
  uint32_t seq_no;
  uint32_t time_tx_us;
} record_tx[4];

static uint32_t _get_rtt(uint32_t seq_num, uint32_t prev)
{
  for (unsigned i = 0; i < ARRAY_SIZE(record_tx); ++i) {
    if (record_tx[i].seq_no == seq_num) {
      return ztimer_now(ZTIMER_USEC) - record_tx[i].time_tx_us;
    }
  }

  return prev;
}

static void _put_rtt(uint32_t seq_num) {
  uint8_t oldest = 0;
  uint32_t oldest_diff = 0;
  uint32_t now = ztimer_now(ZTIMER_USEC);

  for (unsigned i = 0; i < ARRAY_SIZE(record_tx); ++i) {
    uint32_t diff = now - record_tx[i].time_tx_us;
    if (diff > oldest_diff) {
      oldest_diff = diff;
      oldest = i;
    }
  }

  record_tx[oldest].seq_no = seq_num;
  record_tx[oldest].time_tx_us = now;
}

static void *_listen_thread(void *ctx)
{
  (void)ctx;

  static uint8_t buf[BENCH_PAYLOAD_SIZE_MAX + sizeof(benchmark_msg_ping_t)];
  benchmark_msg_cmd_t *cmd = (void *)buf;

  puts("bench_udp: listen thread start");

  while (running) {
    ssize_t res;

    res = sock_udp_recv(&sock, buf, sizeof(buf), 2 * delay_us, NULL);
    if (res < 0) {
      if (res != -ETIMEDOUT) {
        printf("Error receiving message: %" PRIdSIZE "\n", res);
      }
      continue;
    }

    unsigned state = irq_disable();
    if (cmd->flags & BENCH_FLAG_CMD_PKT) {
      ping->seq_no  = 0;
      ping->replies = 0;
      ping->flags   = cmd->flags & BENCH_MASK_COOKIE;
      delay_us      = cmd->delay_us;
      payload_size  = MIN(cmd->payload_len, BENCH_PAYLOAD_SIZE_MAX);
    } else {
      benchmark_msg_ping_t *pong = (void *)buf;

      ping->replies++;
      ping->rtt_last = _get_rtt(pong->seq_no, ping->rtt_last);
    }
    irq_restore(state);
  }

  puts("bench_udp: listen thread terminates");
  sema_inv_post(&thread_sync);

  return NULL;
}

static void *_send_thread(void *ctx)
{
  sock_udp_ep_t remote = *(sock_udp_ep_t*)ctx;

  puts("sending thread start");

  while (running) {
    _put_rtt(ping->seq_no);

    if (sock_udp_send(&sock, ping, sizeof(*ping) + payload_size, &remote) < 0) {
      puts("Error sending message");
      continue;
    } else {
      unsigned state = irq_disable();
      ping->seq_no++;
      irq_restore(state);
    }

    ztimer_sleep(ZTIMER_USEC, delay_us);
  }

  puts("bench_udp: sending thread terminates");
  sema_inv_post(&thread_sync);

  return NULL;
}

int benchmark_udp_start(const char *server, uint16_t port)
{
  netif_t *netif;
  sock_udp_ep_t local = { .family = AF_INET6,
    .netif = SOCK_ADDR_ANY_NETIF,
    .port = port };
  sock_udp_ep_t remote = { .family = AF_INET6,
    .port = port };

  /* stop threads first */
  benchmark_udp_stop();

  if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
    puts("Error creating UDP sock");
    return 1;
  }

  if (netutils_get_ipv6((ipv6_addr_t *)&remote.addr.ipv6, &netif, server) < 0) {
    puts("can't resolve remote address");
    sock_udp_close(&sock);
    return 1;
  }
  if (netif) {
    remote.netif = netif_get_id(netif);
  } else {
    remote.netif = SOCK_ADDR_ANY_NETIF;
  }

  running = true;
  thread_create(listen_thread_stack, sizeof(listen_thread_stack),
                THREAD_PRIORITY_MAIN - 2, 0,
                _listen_thread, NULL, "UDP receiver");
  thread_create(send_thread_stack, sizeof(send_thread_stack),
                THREAD_PRIORITY_MAIN - 1, 0,
                _send_thread, &remote, "UDP sender");
  return 0;
}

bool benchmark_udp_stop(void)
{
  if (!running) {
    return false;
  }

  /* signal threads to stop */
  sema_inv_init(&thread_sync, 2);
  running = false;

  puts("bench_udp: waiting for threads to terminate");

  /* wait for threads to terminate */
  sema_inv_wait(&thread_sync);
  sock_udp_close(&sock);

  puts("bench_udp: threads terminated");

  /* clear cookie & stack */
  ping->flags = 0;
  memset(send_thread_stack, 0, sizeof(send_thread_stack));
  memset(listen_thread_stack, 0, sizeof(listen_thread_stack));

  return true;
}

void benchmark_udp_auto_init(void)
{
  benchmark_udp_start(BENCH_SERVER_DEFAULT, BENCH_PORT_DEFAULT);
}

int benchmark_udp_cmd_handler(int argc, char **argv)
{
  if (argc < 2) {
    goto usage;
  }

  if (strcmp(argv[1], "start") == 0) {
    if (argc > 2) {
      bench_server = argv[2];
    }
    if (argc > 3) {
      bench_port = atoi(argv[3]);
    }
    return benchmark_udp_start(bench_server, bench_port);
  }
  if (strcmp(argv[1], "config") == 0) {
    if (argc < 3) {
      printf("server: %s\n", bench_server);
      printf("port  : %u\n", bench_port);
    } else {
      bench_server = argv[2];
    }
    if (argc > 3) {
      bench_port = atoi(argv[3]);
    }
  }
  if (strcmp(argv[1], "server") == 0)
  {

  }
  if (strcmp(argv[1], "port") == 0)
  {

  }
  if (strcmp(argv[1], "delay") == 0)
  {
    if (argc > 2)
    {
      delay_us = atoi(argv[2]);
    }
    printf("current delay us : %d\n", delay_us);
    return 0;
  }
  if (strcmp(argv[1], "stop") == 0) {
    if (benchmark_udp_stop()) {
      puts("benchmark process stopped");
    } else {
      puts("no benchmark was running");
    }
    return 0;
  }

usage:
  printf("usage: %s [start|stop|config|delay] <server> <port>\n", argv[0]);
  return -1;
}


SHELL_COMMAND(bench_udp, "UDP benchmark", benchmark_udp_cmd_handler);

/** @} */
