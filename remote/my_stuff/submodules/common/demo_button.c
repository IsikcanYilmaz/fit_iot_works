#include "demo_button.h"
#include "thread.h"
#include "board.h"
#include <periph/gpio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

// arg is button id
static void Button_GestureTimerCallback(void *arg)
{
  DemoButton_e buttonIdx = (DemoButton_e) arg;
  if (buttonIdx >= NUM_BUTTONS)
  {
    return;
  }
  ButtonThreadMessage_s buttonMsg = {.type = BUTTON_GESTURE_TIMER_TIMEOUT, .idx = buttonIdx};
  msg_t m;
  memcpy(&m.content.value, &buttonMsg, sizeof(ButtonThreadMessage_s));
  msg_send(&m, buttonThreadId);


  /*ButtonContext_s *ctx = &(buttonContexts[idx]);*/
  /*DemoButton_e buttonId = idx;*/
  /*ButtonGesture_e g = GESTURE_NONE;*/
  /*uint32_t now = ztimer_now(ZTIMER_MSEC);*/

  /*if (buttonId >= NUM_BUTTONS)*/
  /*{*/
  /*  printf("Button callback wrong button id %d\n", buttonId);*/
  /*  return;*/
  /*}*/

  /*gpio_irq_disable(buttonGpios[idx]);*/

  /*printf("%s %d %d\n", buttonNameStrs[buttonId], buttonContexts[idx].currentTapTimestamp, now);*/

  // Process the gesture
  /*if (buttonContexts[idx].currentState == BUTTON_STATE_RELEASED)*/
  /*{*/
  /*  int a = 0;*/
  /*  for (int i = 0; i < 2; i++)*/
  /*  {*/
  /*    a++;*/
  /*    printf("%d\n", a);*/
  /*  }*/
  /*  printf("ASDF%d\n", a);*/
  /*  printf("now %d currTapTs %d\n", now, buttonContexts[idx].currentTapTimestamp);*/
  /*  uint32_t elapsedMs = now - buttonContexts[idx].currentTapTimestamp;*/
  /*  uint32_t numElapsedLongPress = elapsedMs / BUTTON_LONG_PRESS_TIME_MS;*/
  /*  printf("ElapsedMs %d\n", elapsedMs);*/
  /*  if (buttonContexts[idx].currentNumTaps > 1) // MULTI TAP GESTURE*/
  /*  {*/
  /**/
  /*  }*/
  /*  else // SINGLE TAP GESTURE*/
  /*  {*/
  /*    if (numElapsedLongPress == GESTURE_SINGLE_TAP_SEC)*/
  /*    {*/
  /*      printf("%s SINGLE TAP\n", buttonNameStrs[buttonId]);*/
  /*      g = GESTURE_SINGLE_TAP;*/
  /*      buttonContexts[idx].currentNumTaps = 0;*/
  /*    }*/
  /*  }*/
  /*}*/

  // Decide if we should rerun the timer:

  /*gpio_irq_enable(buttonGpios[idx]);*/
}

static void Button_IrqHandler(void *arg) // lean and mean
{
  LED1_TOGGLE;
  DemoButton_e buttonIdx = (DemoButton_e) arg;
  if (buttonIdx >= NUM_BUTTONS)
  {
    return;
  }

  ButtonThreadMessage_s buttonMsg = {.type = BUTTON_IRQ_HAPPENED, .idx = buttonIdx};
  msg_t m;
  memcpy(&m.content.value, &buttonMsg, sizeof(ButtonThreadMessage_s));
  msg_send(&m, buttonThreadId);
}

// Will update the context struct. 
static uint16_t Button_HandleAll(void) // JON TODO could make this "HandleOne" and pass button id
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

    printf("%s State changed from %d to %d\n", buttonNameStrs[i], ctx->prevState, ctx->currentState, ctx);

    // If there's a state change handle it
    if (ctx->currentState == BUTTON_STATE_PRESSED) // BUTTON PRESS
    {
      ctx->currentTapTimestamp = ztimer_now(ZTIMER_MSEC);
      ctx->currentNumTaps++;
      Button_StopTimer(i);
      Button_StartTimer(i); // TODO IMPLEMENT GESTURE HANDLING
    }
    else if (ctx->currentState == BUTTON_STATE_RELEASED) // BUTTON RELEASE
    {
      Button_StopTimer(i);
      Button_StartTimer(i);
    }

    ctx->prevState = ctx->currentState;
  }
  return buttonBits;
}



// TODO decide if we need to do debouncing. with the new switches it seems not

/////////////////////////////////////////

static void *Button_ThreadHandler(void *arg)
{
  while(true)
  {
    msg_t m;
    msg_receive(&m);
    ButtonThreadMessage_s *buttonMsg = (ButtonThreadMessage_s *) &m.content.value;
    printf("MSG RECEIVED TYPE %d IDX %d\n", buttonMsg->type, buttonMsg->idx);

    switch (buttonMsg->type)
    {
      case BUTTON_IRQ_HAPPENED:
        {
          Button_HandleAll();
          break;
        }
      case BUTTON_GESTURE_TIMER_TIMEOUT:
        {

          break;
        }
      default:
        {
          printf("Bad Button Event\n");
          break;
        }
    }
  }
}

/////////////////////////////////////////

void Button_Init(void)
{
  // Initialize buttons and enable irqs
  for (uint8_t i = 0; i < NUM_BUTTONS; i++)
  {
    gpio_init_int(buttonGpios[i], GPIO_IN_PD, GPIO_BOTH, Button_IrqHandler, (void *) i);
    gpio_irq_enable(buttonGpios[i]);

    // Init our button's context
    buttonContexts[i] = (ButtonContext_s) {.id = i,
      .prevState = BUTTON_STATE_RELEASED, 
      .currentState = BUTTON_STATE_RELEASED,
      .gestureTimer = (ztimer_t) {.callback = Button_GestureTimerCallback, .arg = (void *) i /* (void *) &buttonContexts[i] */ },
      .currentNumTaps = 0,
      .currentGesture = GESTURE_NONE,
      .currentTapTimestamp = 0};
  }

  // Run button handler thread
  buttonThreadId = thread_create(
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


