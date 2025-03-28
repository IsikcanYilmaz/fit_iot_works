#include "ccn_nc_demo.h"
#include <string.h>
#include <stdbool.h>

#include "ccn-lite-riot.h"
#include "ccnl-pkt-ndntlv.h"
#include "net/gnrc/netif.h"

#include "thread.h"
#include "ztimer.h"
#include "board.h"

// For onboard leds
#include "led.h"

#define BUF_SIZE (64)
#define MAX_ADDR_LEN (GNRC_NETIF_L2ADDR_MAXLEN)
static unsigned char _int_buf[BUF_SIZE];
static unsigned char _out[CCNL_MAX_PACKET_SIZE];

static bool onBoardLedDemo = false;

#define LED_THREAD_SLEEP_MS 500

typedef enum { // TODO mv to header
  ONBOARD_RED,
  ONBOARD_GREEN,
  ONBOARD_BLUE,
  NUM_ONBOARD_LEDS
} OnboardLedID_e;

typedef enum {
  OFF,
  SOLID,
  BLINKING,
  NUM_LED_STATES
} LedState_e;

typedef struct OnboardLed_s
{
  LedState_e state;
} OnboardLed_t;

OnboardLed_t onboardLeds[NUM_ONBOARD_LEDS];

kernel_pid_t mainThreadId, ledThreadId;

///////////////////////////////////////////////

char ccn_nc_onboard_led_thread_stack[THREAD_STACKSIZE_DEFAULT];
static void *ccn_nc_onboard_led_thread_handler(void *arg)
{
  while(true)
  {
    // Handle animations
    for (int i = 0; i < NUM_ONBOARD_LEDS; i++)
    {
      switch(onboardLeds[i].state)
      {
        case OFF:
          {
            led_off(i);
            break;
          }
        case SOLID:
          {
            led_on(i);
            break;
          }
        case BLINKING:
          {
            led_toggle(i);
            break;
          }
        default:
          {

          }
      }
    }

    ztimer_sleep(ZTIMER_MSEC, LED_THREAD_SLEEP_MS);
  }

}

char ccn_nc_thread_stack[THREAD_STACKSIZE_DEFAULT];
static void *ccn_nc_thread_handler(void *arg)
{
  (void) arg;
  while(true)
  {
    
    ztimer_sleep(ZTIMER_MSEC, 1000);
  }
}

static bool cs_remove(char *prefixStr)
{
  int ret = ccnl_cs_remove(&ccnl_relay, prefixStr);
  if (ret == -1)
  {
    printf("%s ccnl or prefix ptr not provided!\n");
  }
  else if (ret == -2 || ret == -3)
  { 
    printf("%s prefix str not found or something wrong with it\n");
  }
  return ret;
}

static bool prefix_exists(char *prefixStr)
{
  struct ccnl_content_s *content = ccnl_cs_lookup(&ccnl_relay, prefixStr);
  printf("Prefix lookup of %s : %s\n", prefixStr, (content != NULL) ? "EXISTS" : "DOESNT");
  return (content != NULL);
}

///////////////////////////////////////////////
///////////////////////////////////////////////
///////////////////////////////////////////////

void CCN_NC_Init(void)
{
  memset(&onboardLeds, 0x00, sizeof(OnboardLed_t) * NUM_ONBOARD_LEDS);
  for (int i = 0; i < NUM_ONBOARD_LEDS; i++)
  {
    onboardLeds[i] = (OnboardLed_t){.state = OFF};
  }

  // Kick off threads
  mainThreadId = thread_create(
    ccn_nc_thread_stack,
    sizeof(ccn_nc_thread_stack),
    THREAD_PRIORITY_MAIN - 1,
    THREAD_CREATE_STACKTEST,
    ccn_nc_thread_handler,
    NULL,
    "ccn_nc_thread"
	);

  ledThreadId = thread_create(
    ccn_nc_onboard_led_thread_stack,
    sizeof(ccn_nc_onboard_led_thread_stack),
    THREAD_PRIORITY_MAIN - 1,
    THREAD_CREATE_STACKTEST,
    ccn_nc_onboard_led_thread_handler,
    NULL,
    "ccn_nc_onboard_led_thread"
	);
}

void CCN_NC_ShowCS(void)
{
  ccnl_cs_dump(&ccnl_relay);
}

void CCN_NC_Produce(ContentTypes_e t, bool overwrite)
{
  struct ccnl_prefix_s *prefix;
  char *prefixStr;
  char buf[BUF_SIZE+1];
  memset(buf, 0x00, sizeof(buf));
  switch(t)
  {
    default:
      {
        printf("%s:%d Wrong type %d\n", __FUNCTION__, __LINE__, t);
        // Fall thru
      }
    case RED_CONTENT:
      {
        sprintf(buf, "red:%d", ztimer_now(ZTIMER_MSEC));
        prefixStr = "/red";
        break;
      }
    case BLU_CONTENT:
      {
        sprintf(buf, "blu:%d", ztimer_now(ZTIMER_MSEC));
        prefixStr = "/blu";
        break;
      }
    case GRN_CONTENT:
      {
        sprintf(buf, "grn:%d", ztimer_now(ZTIMER_MSEC));
        prefixStr = "/grn";
        break;
      }
  }
  prefix = ccnl_URItoPrefix(prefixStr, CCNL_SUITE_NDNTLV, NULL);
  bool prefixExists = prefix_exists(prefixStr);
  printf("%s : %s\n", buf, (prefixExists) ? "exists" : "doesnt exist");
  if (prefixExists)
  {
    if (overwrite)
    {
      cs_remove(prefixStr);
    }
    else
    {
      printf("CS Entry already exists. Exiting\n");
      return;
    }
  }

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

void CCN_NC_Interest(char *prefixStr)
{
  //
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
// SHELL COMMANDS /////////////////////////////////////////////////
int cmd_ccnl_nc_produce(int argc, char **argv)
{
  ContentTypes_e contentType = RED_CONTENT;
  if (argc < 2)
  {
    CCN_NC_Produce(contentType, true);
    return 0;
  }
  
  switch (argv[1][0])
  {
    case 'r':
      {
        contentType = RED_CONTENT;
        break;
      }
    case 'g':
      {
        contentType = GRN_CONTENT;
        break;
      }
    case 'b':
      {
        contentType = BLU_CONTENT;
        break;
      }
    default:
      {
        // fall thru
      }
  }

  CCN_NC_Produce(contentType, true);
  return 0;
}

int cmd_ccnl_nc_interest(int argc, char **argv)
{
  CCN_NC_Interest(argv[1]);
  return 0;
}

int cmd_ccnl_nc_show_cs(int argc, char **argv)
{
  CCN_NC_ShowCS();
  return 0;
}

int cmd_ccnl_nc_rm_cs(int argc, char **argv)
{
  if (argc < 2)
  {
    printf("argc == %d\n", argc);
    return 1;
  }
  return cs_remove(argv[1]);
}
