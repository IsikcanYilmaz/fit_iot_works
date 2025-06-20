
// Dunno if any of whats below is really needed but lets see how it goe
#define IPERF_CTRL_MAGIC_NUM (0x10)
#define IPERF_PAYLOAD_MAGIC_NUM (0xf0)

typedef enum {
  IPERF_PAYLOAD,
  IPERF_PKT_REQ,
  IPERF_ECHO_CALL,
  IPERF_ECHO_RESP,
  IPERF_CTRL_MSG_MAX
} IperfMsgType_e;

typedef struct {
  uint8_t msgType; // 1
  uint8_t plSize;  // 1 
  uint16_t seqNo;  // 2
  uint8_t payload[]; // *
} __attribute__((packed)) IperfUdpPkt_t;

