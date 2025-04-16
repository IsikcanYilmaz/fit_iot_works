#include "ccn_nc_demo.h"
#include <string.h>
#include <stdbool.h>

#include "ccn-lite-riot.h"
#include "ccnl-pkt-ndntlv.h"
#include "net/gnrc/netif.h"

#include "thread.h"
#include "ztimer.h"
#include "board.h"

#include "periph/pm.h"

#include "demo_throttlers.h"

// For onboard leds
#include "led.h"
#include <periph/gpio.h>

// for neopixels 
#include "demo_neopixels.h"

#include "demo_button.h"

#define BUF_SIZE (64)
#define MAX_ADDR_LEN (GNRC_NETIF_L2ADDR_MAXLEN)
static unsigned char _int_buf[BUF_SIZE];
static unsigned char _out[CCNL_MAX_PACKET_SIZE];

static bool onBoardLedDemo = false;

#define LED_THREAD_SLEEP_MS 500
#define MAIN_THREAD_SLEEP_MS 10

static char *contentNames[NUM_CONTENT_TYPES] = {"/red", "/grn", "/blu"}; // TODO CLEAN

typedef enum { // TODO mv to header
  ONBOARD_RED,
  ONBOARD_GREEN,
  ONBOARD_BLUE,
  NUM_ONBOARD_LEDS
} OnboardLedID_e;

typedef enum {
  HW_ONBOARD_LEDS,
  HW_BREADBOARD_LEDS,
  HW_NEOPIXELS,
  NUM_HARDWARE_TYPES
} HardwareType_e;

typedef enum {
  OFF,
  SOLID,
  BLINKING,
  NUM_LED_STATES
} LedState_e;

static HardwareType_e currentHardware = HW_NEOPIXELS; //HW_BREADBOARD_LEDS; //HW_ONBOARD_LEDS;

static LedState_e ledStates[NUM_ONBOARD_LEDS];

static gpio_t breadboardLedGpios[NUM_ONBOARD_LEDS] = {
  [ONBOARD_RED] = GPIO_PIN(0,28),
  [ONBOARD_GREEN] = GPIO_PIN(0, 3),
  [ONBOARD_BLUE] = GPIO_PIN(0, 2),
};

static kernel_pid_t mainThreadId, ledThreadId;
extern kernel_pid_t buttonThreadId;

///////////////////////////////////////////////

static struct ccnl_interest_s * pit_lookup(char *prefixStr)
{
  char s[CCNL_MAX_PREFIX_SIZE];
  struct ccnl_interest_s *pendingInterest = ccnl_relay.pit;
  for (int i = 0; i < ccnl_relay.pitcnt; i++)
  {
    if (strncmp(prefixStr, ccnl_prefix_to_str(pendingInterest->pkt->pfx, s, CCNL_MAX_PREFIX_SIZE), CCNL_MAX_PREFIX_SIZE) == 0)
    {
      return pendingInterest;
    }
    pendingInterest = pendingInterest->next;
  }
  return NULL;
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
  printf("Prefix lookup of %s : %s %x\n", prefixStr, (content != NULL) ? "EXISTS" : "DOESNT", content);
  return (content != NULL);
}

static void init_hardware(HardwareType_e type)
{
  switch(type)
  {
    case HW_ONBOARD_LEDS:
      {
        break;
      }
    case HW_BREADBOARD_LEDS:
      {
        for (int i = 0; i < NUM_ONBOARD_LEDS; i++)
        {
          gpio_init(breadboardLedGpios[i], GPIO_OUT);
        }
        Button_Init(mainThreadId);
        break;
      }
    case HW_NEOPIXELS:
      {
        Neopixel_Init();
        Button_Init(mainThreadId);
        break;
      }
    default:
      {}
  }
}

// TODO Consolidate and generalize
static void onboard_led_on(OnboardLedID_e led)
{
  if (led > NUM_ONBOARD_LEDS)
  {
    return;
  }
  if (currentHardware == HW_ONBOARD_LEDS)
  {
    led_on(led);
  } 
  else if (currentHardware == HW_BREADBOARD_LEDS)
  {
    gpio_set(breadboardLedGpios[led]);
  }
}

static void onboard_led_off(OnboardLedID_e led)
{
  if (led > NUM_ONBOARD_LEDS)
  {
    return;
  }
  if (currentHardware == HW_ONBOARD_LEDS)
  {
    led_off(led);
  } 
  else if (currentHardware == HW_BREADBOARD_LEDS)
  {
    gpio_clear(breadboardLedGpios[led]);
  }
}

static void onboard_led_toggle(OnboardLedID_e led)
{
  if (led > NUM_ONBOARD_LEDS)
  {
    return;
  }
  if (currentHardware == HW_ONBOARD_LEDS)
  {
    led_toggle(led);
  } 
  else if (currentHardware == HW_BREADBOARD_LEDS)
  {
    gpio_toggle(breadboardLedGpios[led]);
  }
}

// THREAD HANDLERS //////////////////////
// TODO maybe move this elsewhere?
char ccn_nc_led_thread_stack[THREAD_STACKSIZE_DEFAULT];
static void *ccn_nc_led_thread_handler(void *arg) // TODO Move this to the neopixel file
{
  while(true)
  {
    if (currentHardware == HW_ONBOARD_LEDS || currentHardware == HW_BREADBOARD_LEDS)
    {
      // Check CS and PIT
      for (int i = 0; i < NUM_CONTENT_TYPES; i++)
      {
        if (ccnl_cs_lookup(&ccnl_relay, contentNames[i]))
        {
          ledStates[i] = SOLID;
        }
        else if (pit_lookup(contentNames[i]))
        {
          ledStates[i] = BLINKING;
        }
        else
        {
          ledStates[i] = OFF;
        }
      }

      // Handle animations
      for (int i = 0; i < NUM_ONBOARD_LEDS; i++)
      {
        switch(ledStates[i])
        {
          case OFF:
            {
              onboard_led_off(i);
              break;
            }
          case SOLID:
            {
              onboard_led_on(i);
              break;
            }
          case BLINKING:
            {
              onboard_led_toggle(i);
              break;
            }
          default:
            {

            }
        }
      }
    }
    else if (currentHardware == HW_NEOPIXELS)
    {
      /*if (Neopixel_ShouldRedraw())*/ 
      /*{*/
      /*  printf("NEOPIXEL SHOULD REDRAW\n");*/
      /*  Neopixel_DisplayStrip();*/
      /*}*/
    }
    ztimer_sleep(ZTIMER_MSEC, LED_THREAD_SLEEP_MS);
  }
}

char ccn_nc_main_thread_stack[THREAD_STACKSIZE_DEFAULT];
static void *ccn_nc_main_thread_handler(void *arg)
{
  (void) arg;
  while(true)
  {
    msg_t m;
    msg_receive(&m); // block until thread receives message
    printf("Message received %d\n", m.content.value);

    if (m.sender_pid == buttonThreadId) // Button Message received 
    {
      ButtonGestureMessage_s *gestureMessage = (ButtonGestureMessage_s *) &m.content.value;
      switch(gestureMessage->button) // low prio TODO: put button functionality on a matrix type thing
      {
        case BUTTON_RED:
          {
            if (!gestureMessage->shift)
              CCN_NC_Produce(RED_CONTENT, false);
            else
              CCN_NC_Interest("/red");
            break;
          }
        case BUTTON_GREEN:
          {
            if (!gestureMessage->shift)
              CCN_NC_Produce(GRN_CONTENT, false);
            else
              CCN_NC_Interest("/grn");
            break;
          }
        case BUTTON_BLUE:
          {
            if (!gestureMessage->shift)
              CCN_NC_Produce(BLU_CONTENT, false);
            else
              CCN_NC_Interest("/blu");
            break;
          }
        case BUTTON_SHIFT:
          {
            if (gestureMessage->gesture == GESTURE_TRIPLE_TAP)
            {
              pm_reboot(); // TODO doesnt have to be this hard
            }
            break;
          }
        default:
          {

          }
      }
    }
    else
    {
      printf("Unknown sender %d\n", m.sender_pid);
    }
  }
}

///////////////////////////////////////////////
///////////////////////////////////////////////
///////////////////////////////////////////////

void CCN_NC_Init(void)
{
  for (int i = 0; i < NUM_ONBOARD_LEDS; i++)
  {
    ledStates[i] = OFF;
  }

  // Kick off threads
  mainThreadId = thread_create(
    ccn_nc_main_thread_stack,
    sizeof(ccn_nc_main_thread_stack),
    THREAD_PRIORITY_MAIN - 1,
    THREAD_CREATE_STACKTEST,
    ccn_nc_main_thread_handler,
    NULL,
    "ccn_nc_thread"
	);

	/* ledThreadId = thread_create(*/ // TODO? what to do with this?
	/*   ccn_nc_led_thread_stack,*/
	/*   sizeof(ccn_nc_led_thread_stack),*/
	/*   THREAD_PRIORITY_MAIN - 1,*/
	/*   THREAD_CREATE_STACKTEST,*/
	/*   ccn_nc_led_thread_handler,*/
	/*   NULL,*/
	/*   "ccn_nc_onboard_led_thread"*/
	/*);*/

  Throttler_Init();
  init_hardware(currentHardware);
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
  ledStates[t] = SOLID;
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

int CCN_NC_Interest(char *prefixStr)
{
  memset(_int_buf, '\0', BUF_SIZE);
  printf("Sending interest for %s\n", prefixStr);
  struct ccnl_prefix_s *prefix = ccnl_URItoPrefix(prefixStr, CCNL_SUITE_NDNTLV, NULL);
  int res = ccnl_send_interest(prefix, _int_buf, BUF_SIZE, NULL);
  ccnl_prefix_free(prefix);
  return res;
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

int cmd_ccnl_nc_rm_cs_all(int argc, char **argv)
{
  struct ccnl_content_s *c;
  for (c = ccnl_relay.contents; c; c=c->next)
  {
    ccnl_content_remove(&ccnl_relay, c);
  }
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

int cmd_ccnl_nc_show_pit(int argc, char **argv)
{
  (void) argv;
  char s[CCNL_MAX_PREFIX_SIZE];
  struct ccnl_interest_s *pendingInterest = ccnl_relay.pit;
  for (int i = 0; i < ccnl_relay.pitcnt; i++)
  {
    struct ccnl_pkt_s *pkt = pendingInterest->pkt; 
    struct ccnl_prefix_s *prefix = pkt->pfx;
    printf("%d: %s \n", i, ccnl_prefix_to_str(prefix, s, CCNL_MAX_PREFIX_SIZE));
    pendingInterest = pendingInterest->next;
  }
}

int cmd_ccnl_nc_set_hw(int argc, char **argv)
{
  if (argc != 2)
  {
    currentHardware = HW_ONBOARD_LEDS;
  }
  else 
  {
    int tmp = atoi(argv[1]);
    if (tmp > NUM_HARDWARE_TYPES)
    {
      currentHardware = HW_ONBOARD_LEDS;
    }
    else 
    {
      currentHardware = tmp;
    }
  }
  init_hardware(currentHardware);
  printf("Set current hardware to %d\n", currentHardware);
  return 0;
}

int cmd_ccnl_nc_rm_limitors(int argc, char **argv)
{

  return 0;
}

int cmd_ccnl_nc_default_limitors(int argc, char **argv)
{

  return 0;
}
