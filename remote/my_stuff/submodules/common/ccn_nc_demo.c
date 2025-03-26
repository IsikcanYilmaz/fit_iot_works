#include "ccn_nc_demo.h"
#include <string.h>

#include "ccn-lite-riot.h"
#include "ccnl-pkt-ndntlv.h"
#include "net/gnrc/netif.h"

#define RED_CONTENT "RED"
#define GRN_CONTENT "GRN"
#define BLU_CONTENT "BLU"

#define BUF_SIZE (64)
#define MAX_ADDR_LEN (GNRC_NETIF_L2ADDR_MAXLEN)
static unsigned char _int_buf[BUF_SIZE];
static unsigned char _out[CCNL_MAX_PACKET_SIZE];

void CCN_NC_ShowCS(void)
{
  ccnl_cs_dump(&ccnl_relay);
}

void CCN_NC_Produce(void)
{
  char buf[BUF_SIZE+1];
  strcpy(buf, "redredred\0");
  printf("%s\n", buf);
  struct ccnl_prefix_s *prefix = ccnl_URItoPrefix("/red", CCNL_SUITE_NDNTLV, NULL);
  size_t offs = CCNL_MAX_PACKET_SIZE;
  size_t reslen = 0;
  int arg_len = strlen(buf);
  arg_len = ccnl_ndntlv_prependContent(prefix, (unsigned char*) buf, arg_len, NULL, NULL, &offs, _out, &reslen);
  ccnl_prefix_free(prefix);

  unsigned char *olddata;
  unsigned char *data = olddata = _out + offs;
  size_t len;
  uint64_t typ;
  if (ccnl_ndntlv_dehead(&data, &reslen, &typ, &len) || typ != NDN_TLV_Data) 
  {
    printf("%s:%d Check failed\n", __FUNCTION__, __LINE__);
    return;
  }

  struct ccnl_content_s *c = 0;
  struct ccnl_pkt_s *pk = ccnl_ndntlv_bytes2pkt(typ, olddata, &data, &reslen);
  c = ccnl_content_new(&pk);
  c->flags |= CCNL_CONTENT_FLAGS_STATIC;
  msg_t m = { .type = CCNL_MSG_CS_ADD, .content.ptr = c };

  if(msg_send(&m, ccnl_event_loop_pid) < 1){
    puts("could not add content");
  }
}

void CCN_NC_Interest(void)
{

}


// SHELL COMMANDS
int cmd_ccnl_nc_produce(int argc, char **argv)
{
  /*if (argc < 2) {*/
  /*  ccnl_cs_dump(&ccnl_relay);*/
  /*  return 0;*/
  /*}*/
  /*if (argc == 2) {*/
  /*  _content_usage(argv[0]);*/
  /*  return -1;*/
  /*}*/
  CCN_NC_Produce();
  return 0;
}

int cmd_ccnl_nc_interest(int argc, char **argv)
{
  return 0;
}

int cmd_ccnl_nc_show_cs(int argc, char **argv)
{
  CCN_NC_ShowCS();
  return 0;
}
