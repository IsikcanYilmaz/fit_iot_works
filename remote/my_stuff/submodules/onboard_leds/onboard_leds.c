#include <periph/gpio.h>
#include "board.h"
#include "led.h"
#include "onboard_leds.h"
#include "ztimer.h"
#include <stdbool.h>

ztimer_t onboardLedBlinkTimers[NUM_ONBOARD_LEDS];

static bool onboardLedInitialized = false;

static void timed_onboard_led_trigger_callback(void *arg)
{
  OnboardLedID_e led = (OnboardLedID_e) arg;
  if (led >= NUM_ONBOARD_LEDS)
  {
    return;
  }
  led_off(led);
}

void OnboardLeds_Init(void)
{
  for (int i = 0; i < NUM_ONBOARD_LEDS; i++)
  {
    onboardLedBlinkTimers[i] = (ztimer_t) {.callback = timed_onboard_led_trigger_callback, .arg = (void *) i};
  }
  onboardLedInitialized = true;
}

void OnboardLeds_Blink(OnboardLedID_e led, uint16_t ms)
{
  if (led >= NUM_ONBOARD_LEDS || !onboardLedInitialized)
  {
    return;
  }
  led_on(led);
  ztimer_remove(ZTIMER_MSEC, &onboardLedBlinkTimers[led]); // remove if already set
  ztimer_set(ZTIMER_MSEC, &onboardLedBlinkTimers[led], ms);
}

int cmd_onboardblink(int argc, char **argv)
{
  if (argc < 2)
  {
    printf("Usage: blink <r|g|b> [time]");
  }
  uint16_t ms = 10;
  OnboardLedID_e led;
  switch(argv[1][0])
  {
    case 'r':
      {
        led = ONBOARD_RED;
        break;
      }
    case 'g':
      {
        led = ONBOARD_GREEN;
        break;
      }
    case 'b':
      {
        led = ONBOARD_BLUE;
        break;
      }
    default:
      {
        printf("Bad led id\n");
        return 1;
        break;
      }
  }
  OnboardLeds_Blink(led, ms);
  return 0;
}

SHELL_COMMAND(blink, "blink <r|g|b> [time]", cmd_onboardblink);
