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

  // TIMED MODE
  uint32_t transferTimeUs;

  // SIZE MODE
  uint32_t transferSizeBytes;

} IperfConfig_s;

typedef struct
{
  uint32_t lastPktSeqNo;
  uint32_t pktLossCounter;
  uint32_t numReceivedPkts;
  uint32_t numReceivedBytes;
  uint32_t numReceivedGoodBytes;
  uint32_t numDuplicates;
  uint32_t numSentPkts;
  uint32_t numSentBytes;
  uint32_t startTimestamp;
  uint32_t endTimestamp;

  uint32_t l2numSentPackets;
  uint32_t l2numSentBytes;
  uint32_t l2numReceivedPackets;
  uint32_t l2numReceivedBytes;
  uint32_t l2numSuccessfulTx;
  uint32_t l2numErroredTx;

  uint32_t ipv6numSentPackets;
  uint32_t ipv6numSentBytes;
  uint32_t ipv6numReceivedPackets;
  uint32_t ipv6numReceivedBytes;
  uint32_t ipv6numSuccessfulTx;
  uint32_t ipv6numErroredTx;
} IperfResults_s;

int Iperf_Deinit(void);
int Iperf_Init(bool iAmSender);
