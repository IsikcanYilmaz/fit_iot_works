#include "shell.h"
#include <stdbool.h>
#include <stdlib.h>
#include "net/gnrc.h"
#include "net/gnrc/netif.h"
#include "net/netopt.h"

#include "demo_throttlers.h"

// TODO this and the settings .c file should merge

#define DEFAULT_TX_POWER (0)

#ifdef JON_RSSI_LIMITING
#define DEFAULT_RSSI_LIMITOR (-100) //(JON_RSSI_LIMITING)

extern int rssiLimitor;
extern bool rssiPrint;

#endif

#ifdef JON_FAKE_LATENCY_MS
#define DEFAULT_FAKE_LATENCY_MS (0) //(JON_FAKE_LATENCY_MS) // TODO maybe remove these settings from the makefile 
extern uint32_t fakeLatencyMs;
#endif

int16_t txPower;

#define MAIN_IFACE (4)
netif_t *mainIface;

void Throttler_Init(void)
{
#ifdef JON_RSSI_LIMITING
  rssiLimitor = DEFAULT_RSSI_LIMITOR;
#endif
#ifdef JON_FAKE_LATENCY_MS
  fakeLatencyMs = DEFAULT_FAKE_LATENCY_MS;
#endif
  txPower = DEFAULT_TX_POWER;
  mainIface = netif_get_by_name("4");
  Throttler_SetTxPower(txPower);
  printf("Throttler initialized");
}

void Throttler_SetRssiLimitor(int rssi)
{
#ifdef JON_RSSI_LIMITING
  rssiLimitor = rssi;
  printf("Rssi limitor set to %d\n", rssi);
#endif
}

int Throttler_GetRssiLimitor(void)
{
#ifdef JON_RSSI_LIMITING
  return rssiLimitor;
#else 
  return 0;
#endif
}

int Throttler_SetTxPower(int16_t txpower)
{
  if (netif_set_opt(mainIface, NETOPT_TX_POWER, 0, &txpower, sizeof(int16_t)) < 0)
  {
    printf("%s unable to set tx power to %d\n", __FUNCTION__, txpower);
    return 1;
  }
  printf("%s set tx power to %d\n", __FUNCTION__, txpower);
  return 0;
}

int Throttler_GetTxPower(void)
{
  return 0; // TODO
}

// SHELL CMDS
int setRssiLimitor(int argc, char **argv)
{
#ifdef JON_RSSI_LIMITING
  if (argc != 2)
  {
    printf("Usage: setrssi <int>\n");
    return 1;
  }
  Throttler_SetRssiLimitor(atoi(argv[1]));
#endif
  return 0;
}

int getRssiLimitor(int argc, char **argv)
{
  #ifdef JON_RSSI_LIMITING
  printf("%d\n", Throttler_GetRssiLimitor());
  #endif
  return 0;
}

int setRssiPrint(int argc, char **argv)
{
  #ifdef JON_RSSI_LIMITING
  rssiPrint = !rssiPrint;
  printf("%d\n", rssiPrint);
  #endif
  return 0;
}

// TODO JON add ifdef like above
int setFakeLatency(int argc, char **argv)
{
  if (argc != 2)
  {
    printf("Usage: setfakelat <int>\n");
    return 1;
  }
  fakeLatencyMs = atoi(argv[1]);
  printf("Fake latency set to %d\n", fakeLatencyMs);
  return 0;
}

int getFakeLatency(int argc, char **argv)
{
  printf("%d\n", fakeLatencyMs);
  return 0;
}

int Throttler_CmdSetTxPower(int argc, char **argv)
{
  if (argc != 2)
  {
    printf("Usage: txpower <int>\n");
    return 1;
  }
  return Throttler_SetTxPower(atoi(argv[1]));
}
