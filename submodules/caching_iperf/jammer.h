#include <stdint.h>
#include <stdbool.h>
typedef struct
{
  uint16_t payloadSizeBytes;
  uint16_t burstMax;
  uint16_t burstDelayMsMin;
  uint16_t burstDelayMsMax;
  uint16_t sleepDelayMsMin;
  uint16_t sleepDelayMsMax;
  bool continuous;
} IperfJammerConfig_s;

void *Iperf_JammerThread(void *arg);
