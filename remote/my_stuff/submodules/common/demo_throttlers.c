#include "shell.h"
#include <stdbool.h>
#include <stdlib.h>

#include "demo_throttlers.h"

#ifdef JON_RSSI_LIMITING
extern int rssiLimitor;
extern bool rssiPrint;

int setRssiLimitor(int argc, char **argv)
{
  if (argc != 2)
  {
    printf("Usage: setrssi <int>\n");
    return 1;
  }
  rssiLimitor = atoi(argv[1]);
  printf("Rssi limitor set to %d\n", rssiLimitor);
  return 0;
}

int getRssiLimitor(int argc, char **argv)
{
  printf("%d\n", rssiLimitor);
  return 0;
}

int setRssiPrint(int argc, char **argv)
{
  rssiPrint = !rssiPrint;
  printf("%d\n", rssiPrint);
  return 0;
}

#endif

// TODO JON add ifdef like above
extern int FAKE_LATENCY_MS;
int setFakeLatency(int argc, char **argv)
{
  if (argc != 2)
  {
    printf("Usage: setfakelat <int>\n");
    return 1;
  }
  FAKE_LATENCY_MS = atoi(argv[1]);
  printf("Fake latency set to %d\n", FAKE_LATENCY_MS);
  return 0;
}

int getFakeLatency(int argc, char **argv)
{
  printf("%d\n", FAKE_LATENCY_MS);
  return 0;
}

