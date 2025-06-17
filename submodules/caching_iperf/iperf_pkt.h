
// Dunno if any of whats below is really needed but lets see how it goe
#define IPERF_CTRL_MAGIC_NUM (0x10)
#define IPERF_PAYLOAD_MAGIC_NUM (0xf0)

typedef enum {
  IPERF_BEGIN_MSG, // 0
  IPERF_BEGIN_RSP, // 1
  IPERF_STOP_MSG,  // 2
  IPERF_STOP_RSP,  // 3
  IPERF_CTRL_MSG_MAX
} IperfCtrlMsgList_e;

typedef struct {
  uint16_t seq_no;  // 2
  uint8_t pl_size;  // 1 
  uint8_t msg_type; // 1
  uint8_t payload[]; // *
} __attribute__((packed)) iperf_udp_pkt_t;

typedef struct 
{
  uint8_t ctrlMsgType;
} __attribute__((packed)) IperfMsg_t;
