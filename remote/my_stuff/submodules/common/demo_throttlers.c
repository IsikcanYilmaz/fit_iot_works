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
