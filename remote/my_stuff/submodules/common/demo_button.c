#include "demo_button.h"
#include "thread.h"
#include "board.h"
#include <periph/gpio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

gpio_t buttonGpios[NUM_BUTTONS] = { // TODO maybe move these into the context struct?
  [BUTTON_RED] = GPIO_PIN(1,12),
  [BUTTON_GREEN] = GPIO_PIN(1,13),
  [BUTTON_BLUE] = GPIO_PIN(1,14),
  [BUTTON_SHIFT] = GPIO_PIN(1,15)
};

char *buttonNameStrs[NUM_BUTTONS] = {
  [BUTTON_RED] = "BUTTON_RED",
  [BUTTON_GREEN] = "BUTTON_GREEN",
  [BUTTON_BLUE] = "BUTTON_BLUE",
  [BUTTON_SHIFT] = "BUTTON_SHIFT",
};

// uint32_t t0 = ztimer_now(ZTIMER_USEC);

ButtonContext_s buttonContexts[NUM_BUTTONS];

bool buttonTriggered = false;

char buttonThreadStack[THREAD_STACKSIZE_DEFAULT];
kernel_pid_t buttonThreadId; // TODO decide if you want this to be here or in main or smt?

/////////////////////////////////////////

static inline void Button_StopTimer(DemoButton_e i)
{
  if (i >= NUM_BUTTONS)
  {
    return;
  }
  if (ztimer_is_set(ZTIMER_MSEC, &buttonContexts[i].gestureTimer))
  {
    ztimer_remove(ZTIMER_MSEC, &buttonContexts[i].gestureTimer);
  }
}

static inline void Button_StartTimer(DemoButton_e i)
{
  if (i >= NUM_BUTTONS)
  {
    return;
  }
  if (ztimer_is_set(ZTIMER_MSEC, &buttonContexts[i].gestureTimer))
  {
    printf("TIMER IS ALREADY SET!\n");
    Button_StopTimer(i);
  }
  ztimer_set(ZTIMER_MSEC, &buttonContexts[i].gestureTimer, BUTTON_POLL_PERIOD_MS);
}

static void testcallback(void *arg)
{
for (int i = 0; i < 30; i++){
    printf("BNUL\n");
    }
}

// arg is button id
static void Button_GestureTimerCallback(void *arg)
{
  ButtonContext_s *ctx = (ButtonContext_s *) arg;
  DemoButton_e buttonId = ctx->id;
  ButtonGesture_e g = GESTURE_NONE;
  uint32_t now = ztimer_now(ZTIMER_MSEC);

  if (buttonId >= NUM_BUTTONS)
  {
    printf("Button callback wrong button id %d\n", buttonId);
    return;
  }

  printf("%s %d %d\n", buttonNameStrs[buttonId], ctx->currentTapTimestamp, now);

  // Process the gesture
  /*if (ctx->currentState == BUTTON_STATE_RELEASED)*/ // TODO for some reason once this check happens, or we access the ctx struct, we freeze
  /*{*/
    /*printf("ASDF\n");*/
    /*printf("now %d currTapTs %d\n", now, ctx->currentTapTimestamp);*/
    /*uint32_t elapsedMs = now - ctx->currentTapTimestamp;*/
    /*uint32_t numElapsedLongPress = elapsedMs / BUTTON_LONG_PRESS_TIME_MS;*/
    /*printf("ElapsedMs %d\n", elapsedMs);*/
    /*if (ctx->currentNumTaps > 1) // MULTI TAP GESTURE*/
    /*{*/
    /**/
    /*}*/
    /*else // SINGLE TAP GESTURE*/
    /*{*/
    /*  if (numElapsedLongPress == GESTURE_SINGLE_TAP_SEC)*/
    /*  {*/
    /*    printf("%s SINGLE TAP\n", buttonNameStrs[buttonId]);*/
    /*    g = GESTURE_SINGLE_TAP;*/
    /*    ctx->currentNumTaps = 0;*/
    /*  }*/
    /*}*/
  }

  // Decide if we should rerun the timer:

}

// Will update the context struct. 
static uint16_t Button_HandleAll(void)
{
  uint16_t buttonBits = 0;
  for (int i = 0; i < NUM_BUTTONS; i++)
  {
    ButtonContext_s *ctx = &buttonContexts[i];
    bool rawState = gpio_read(buttonGpios[i]);
    ctx->currentState = (rawState == BUTTON_ACTIVE) ? BUTTON_STATE_PRESSED : BUTTON_STATE_RELEASED;

    if (ctx->currentState == ctx->prevState)
    {
      continue;
    }

    printf("%s State changed from %d to %d %x\n", buttonNameStrs[i], ctx->prevState, ctx->currentState, ctx);

    // If there's a state change handle it
    if (ctx->currentState == BUTTON_STATE_PRESSED) // BUTTON PRESS
    {
      /*ctx->currentTapTimestamp = ztimer_now(ZTIMER_MSEC);*/
      /*ctx->currentNumTaps++;*/
      /*Button_StopTimer(i);*/
      /*Button_StartTimer(i);*/ // TODO IMPLEMENT GESTURE HANDLING
    }
    else if (ctx->currentState == BUTTON_STATE_RELEASED) // BUTTON RELEASE
    {
      /*Button_StopTimer(i);*/
      /*Button_StartTimer(i);*/
    }

    ctx->prevState = ctx->currentState;
  }
  return buttonBits;
}

static void Button_IrqHandler(void *arg) // lean and mean
{
  buttonTriggered = true;
}

// TODO decide if we need to do debouncing. with the new switches it seems not

/////////////////////////////////////////

static void *Button_ThreadHandler(void *arg)
{
  while(true)
  {
    // If button trigger flag was up, pick up which event happened
    if (buttonTriggered)
    {
      Button_HandleAll();
      buttonTriggered = false;
    }

    ztimer_sleep(ZTIMER_MSEC, BUTTON_THREAD_SLEEP_MS);
  }
}

/////////////////////////////////////////

void Button_Init(void)
{
  // Initialize buttons and enable irqs
  for (int i = 0; i < NUM_BUTTONS; i++)
  {
    gpio_init_int(buttonGpios[i], GPIO_IN_PD, GPIO_BOTH, Button_IrqHandler, NULL);
    gpio_irq_enable(buttonGpios[i]);

    // Init our button's context
    buttonContexts[i] = (ButtonContext_s) {.id = i,
      .prevState = BUTTON_STATE_RELEASED, 
      .currentState = BUTTON_STATE_RELEASED,
      .gestureTimer = (ztimer_t) {.callback = Button_GestureTimerCallback, .arg = (void *) &buttonContexts[i] },
      .currentNumTaps = 0,
      .currentGesture = GESTURE_NONE,
      .currentTapTimestamp = 0};
  }

  // Run button handler thread
  kernel_pid_t buttonThreadId = thread_create(
    buttonThreadStack,
    sizeof(buttonThreadStack),
    THREAD_PRIORITY_MAIN - 1,
    THREAD_CREATE_STACKTEST,
    Button_ThreadHandler,
    NULL,
    "button_thread"
  );
}

void Button_Deinit(void)
{
  for (int i = 0; i < NUM_BUTTONS; i++)
  {
    gpio_irq_disable(buttonGpios[i]);
  }
}


