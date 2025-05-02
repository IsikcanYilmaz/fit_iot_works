
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
