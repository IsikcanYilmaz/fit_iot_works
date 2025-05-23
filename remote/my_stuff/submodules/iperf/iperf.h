#include <stdbool.h>

typedef enum
{
  IPERF_MODE_TIMED,
  IPERF_MODE_SIZE,
  // TODO
  IPERF_MODE_MAX
} IperfMode_e;

typedef struct 
{
  bool iAmSender; // If false we're the listener 
  
  uint16_t senderPort;
  uint16_t senderNetifId;
  uint16_t listenerPort;
  char *listenerIpStr;

  uint16_t payloadSizeBytes;
  uint16_t pktPerSecond;
  uint32_t delayUs;

  IperfMode_e mode;

} IperfConfig_s;

typedef struct
{
  uint32_t lastPktSeqNo;
  uint32_t pktLossCounter;
  uint32_t numReceivedPkts;
  uint32_t numSentPkts;
  uint32_t startTimestamp;
  uint32_t endTimestamp;
} IperfResults_s;
