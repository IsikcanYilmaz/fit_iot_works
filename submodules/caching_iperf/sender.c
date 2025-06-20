#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "thread.h"
#include "ztimer.h"
#include "msg.h"

#include "macros/utils.h"
#include "net/gnrc.h"
#include "net/sock/udp.h"
#include "net/gnrc/udp.h"
#include "net/sock/tcp.h"
#include "net/gnrc/nettype.h"
#include "net/utils.h"
#include "sema_inv.h"
#include "test_utils/benchmark_udp.h"
#include "ztimer.h"
#include "shell.h"
#include "od.h"
#include "net/netstats.h"

#include "iperf.h"
#include "iperf_pkt.h"
#include "message.h"
#include "logger.h"

#include "receiver.h"

typedef enum
{
  SENDER_STOPPED,
  SENDER_SENDING_FILE,
  SENDER_IDLE,
  SENDER_STATE_MAX
} IperfSenderState_e;

extern IperfResults_s results;
extern IperfConfig_s config;
extern uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];

static uint8_t *txBuffer = &rxtxBuffer;
static msg_t _msg_queue[IPERF_MSG_QUEUE_SIZE];

static IperfSenderState_e senderState = SENDER_STOPPED;
static kernel_pid_t senderPid = KERNEL_PID_UNDEF;
static gnrc_netreg_entry_t udpServer = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, KERNEL_PID_UNDEF);

static bool isTransferDone(void)
{
  bool ret = false;
  if (config.mode == IPERF_MODE_SIZE)
  {
    ret = (results.numSentBytes >= config.transferSizeBytes);
  }
  else if (config.mode == IPERF_MODE_TIMED)
  {
    ret = (ztimer_now(ZTIMER_USEC) - results.startTimestamp >= config.transferTimeUs);
  }
  return ret;
}

static int senderHandleIperfPacket(gnrc_pktsnip_t *pkt)
{
  loginfo("%s called\n", __FUNCTION__);
}

static void initSender(void)
{
  senderPid = thread_getpid();
  memset(txBuffer, 0x01, sizeof(IperfUdpPkt_t) + config.payloadSizeBytes);
  IperfUdpPkt_t *payloadPkt = txBuffer;
  payloadPkt->plSize = config.payloadSizeBytes;
  payloadPkt->msgType = IPERF_PAYLOAD;
  payloadPkt->seqNo = 0;
  strncpy((char *) &payloadPkt->payload, IperfMessage_GetPointer(0), config.payloadSizeBytes);
  Iperf_StartUdpServer(&udpServer, senderPid);
}

static void deinitSender(void)
{
  Iperf_StopUdpServer(&udpServer);
}

static int sendPayload(void)
{
  logverbose("Sending payload\n");
  return Iperf_SocklessUdpSend((char *) txBuffer, config.payloadSizeBytes + sizeof(IperfUdpPkt_t));
}

void *Iperf_SenderThread(void *arg)
{
  (void) arg;
  msg_t msg, reply, ipcMsg;
  ztimer_t intervalTimer;
  msg_init_queue(_msg_queue, IPERF_MSG_QUEUE_SIZE);

  reply.content.value = (uint32_t)(-ENOTSUP);
  reply.type = GNRC_NETAPI_MSG_TYPE_ACK;

  initSender();
  loginfo("Starting Sender Thread. Pid %d\n", senderPid);

  senderState = SENDER_SENDING_FILE;

  ipcMsg.type = IPERF_IPC_MSG_SEND_PAYLOAD;
  ztimer_set_msg(ZTIMER_USEC, &intervalTimer, 0, &ipcMsg, senderPid); // Start immediately

  while (senderState > SENDER_STOPPED) {
    msg_receive(&msg);
    logdebug("Received type %d\n", msg.type);
    switch (msg.type) {
      case GNRC_NETAPI_MSG_TYPE_RCV:
        logdebug("Data received\n");
        Iperf_PacketHandler(msg.content.ptr, senderHandleIperfPacket);
        break;
      case GNRC_NETAPI_MSG_TYPE_SND:
        logdebug("Data to send");
        /*Iperf_PacketHandler(msg.content.ptr, senderHandleIperfPacket);*/
        break;
      case GNRC_NETAPI_MSG_TYPE_GET:
      case GNRC_NETAPI_MSG_TYPE_SET:
        msg_reply(&msg, &reply);
        break;
      case IPERF_IPC_MSG_SEND_PAYLOAD:
        sendPayload();
        ztimer_set_msg(ZTIMER_USEC, &intervalTimer, 1000000, &ipcMsg, senderPid);
        break;
      case IPERF_IPC_MSG_DONE:
        senderState = SENDER_STOPPED;
        break;
      default:
        logverbose("received something unexpected");
        break;
    }
  }
  deinitSender();
  loginfo("Sender thread exiting\n");
  return NULL;
}
