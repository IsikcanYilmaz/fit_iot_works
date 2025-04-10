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

char *gestureStrs[NUM_GESTURES] = {
  [GESTURE_SINGLE_TAP] = "GESTURE_SINGLE_TAP",
  [GESTURE_DOUBLE_TAP] = "GESTURE_DOUBLE_TAP",
  [GESTURE_TRIPLE_TAP] = "GESTURE_TRIPLE_TAP",
  [GESTURE_LONG_PRESS] = "GESTURE_LONG_PRESS",
  [GESTURE_VLONG_PRESS] = "GESTURE_VLONG_PRESS",
  [GESTURE_VVLONG_PRESS] = "GESTURE_VVLONG_PRESS",
  [GESTURE_VVVLONG_PRESS] = "GESTURE_VVVLONG_PRESS",
  [GESTURE_NONE] = "GESTURE_NONE",
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

// TODO Consolidate irq and timer callbacks
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
}

static void Button_DebounceTimerCallback(void *arg) // TODO currently unused
{
  DemoButton_e buttonIdx = (DemoButton_e) arg;
  if (buttonIdx >= NUM_BUTTONS)
  {
    return;
  }
  ButtonThreadMessage_s buttonMsg = {.type = BUTTON_DEBOUNCE_TIMER_TIMEOUT, .idx = buttonIdx};
  msg_t m;
  memcpy(&m.content.value, &buttonMsg, sizeof(ButtonThreadMessage_s));
  msg_send(&m, buttonThreadId);
}

// arg is button id
static void Button_IrqHandler(void *arg) // lean and mean
{
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
static void Button_HandleChange(DemoButton_e idx) 
{
  ButtonContext_s *ctx = &buttonContexts[idx];
  bool rawState = gpio_read(buttonGpios[idx]);
  ctx->currentState = (rawState == BUTTON_ACTIVE) ? BUTTON_STATE_PRESSED : BUTTON_STATE_RELEASED;

  if (ctx->currentState == ctx->prevState)
  {
    return;
  }

  /*printf("%s State changed from %d to %d\n", buttonNameStrs[idx], ctx->prevState, ctx->currentState);*/
 
  LED1_TOGGLE;

  // If there's a state change handle it
  if (ctx->currentState == BUTTON_STATE_PRESSED) // BUTTON PRESS
  {
    ctx->currentTapTimestamp = ztimer_now(ZTIMER_MSEC);
    ctx->currentNumTaps++;
    Button_StopTimer(idx);
    Button_StartTimer(idx); // TODO IMPLEMENT GESTURE HANDLING
  }
  else if (ctx->currentState == BUTTON_STATE_RELEASED) // BUTTON RELEASE
  {
    Button_StopTimer(idx);
    Button_StartTimer(idx);
  }

  ctx->prevState = ctx->currentState;
}

static ButtonGesture_e Button_ComputeGesture(DemoButton_e idx)
{
  ButtonGesture_e gesture = GESTURE_NONE;
  bool shiftHeld = (gpio_read(buttonGpios[BUTTON_SHIFT]) == BUTTON_ACTIVE);
  ButtonGestureState_s gestureState = {.shift = shiftHeld, .gesture = gesture};
  ButtonContext_s *ctx = &buttonContexts[idx];
  uint32_t now = ztimer_now(ZTIMER_MSEC);
  
  uint32_t pressHoldLen = now - ctx->currentTapTimestamp;

  // Can be a single tap
  switch(ctx->currentNumTaps)
  {
    case 1:
      {
        // Either single tap or long press
        if (pressHoldLen < GESTURE_LONG_PRESS_MS) // 0 - 1000ms single tap
        {
          gesture = GESTURE_SINGLE_TAP;
        }
        else if (pressHoldLen < GESTURE_VLONG_PRESS_MS) // 1000 - 2000ms long press
        {
          gesture = GESTURE_LONG_PRESS;
        }
        else if (pressHoldLen < GESTURE_VVLONG_PRESS_MS) // 2000 - 3000ms very long press
        {
          gesture = GESTURE_VLONG_PRESS;
        }
        else if (pressHoldLen < GESTURE_VVVLONG_PRESS_MS) // 3000 - 70000ms very very long press
        {
          gesture = GESTURE_VVLONG_PRESS;
        }
        else {
          gesture = GESTURE_VVVLONG_PRESS;
        }
        break;
      }
    case 2:
      {
        gesture = GESTURE_DOUBLE_TAP;
        break;
      }
    case 3:
      {
        gesture = GESTURE_TRIPLE_TAP;
        break;
      }
    default:
      {
        printf("%s %d taps, dunno what to do\n", __FUNCTION__, ctx->currentNumTaps);
      }
  }
  
  gestureState.gesture = gesture;
  printf("GESTURE STATE %d %d\n", gestureState.shift, gestureState.gesture);
  return gesture;
}

static void Button_HandleTimer(DemoButton_e idx)
{
  ButtonContext_s *ctx = &buttonContexts[idx];
  uint32_t now = ztimer_now(ZTIMER_MSEC);

  if (ctx->currentState == BUTTON_STATE_RELEASED) // Gesture ended
  {
    // Compute what kind of gesture just happened:
    ButtonGesture_e gesture = Button_ComputeGesture(idx);
    /*printf("%s %s CurrState %d NumTaps %d phTime %d\n", buttonNameStrs[idx], gestureStrs[gesture], ctx->currentState, ctx->currentNumTaps, now - ctx->currentTapTimestamp);*/
    printf("%s %s \n", buttonNameStrs[idx], gestureStrs[gesture], ctx->currentState, ctx->currentNumTaps, now - ctx->currentTapTimestamp);
    ctx->currentNumTaps = 0;
  }
  else // Press hold going
  {
    Button_StartTimer(idx);
  }
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
    /*printf("MSG RECEIVED TYPE %d IDX %d\n", buttonMsg->type, buttonMsg->idx);*/

    switch (buttonMsg->type)
    {
      case BUTTON_IRQ_HAPPENED:
        {
          Button_HandleChange(buttonMsg->idx);
          break;
        }
      case BUTTON_GESTURE_TIMER_TIMEOUT:
        {
          Button_HandleTimer(buttonMsg->idx);
          break;
        }
      default:
        {
          printf("Bad Button Event type %d\n", buttonMsg->type);
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
    buttonContexts[i] = (ButtonContext_s) {
      .prevState = BUTTON_STATE_RELEASED, 
      .currentState = BUTTON_STATE_RELEASED,
      .gestureTimer = (ztimer_t) {.callback = Button_GestureTimerCallback, .arg = (void *) i }, // TODO this arg can be the message itself. that way the isr can be almost empty
      /*.debounceTimer = (ztimer_t) {.callback = Button_DebounceTimerCallback, .arg = (void *) i }, // TODO this arg can be the message itself. that way the isr can be almost empty*/
      .currentNumTaps = 0,
      .currentGesture = GESTURE_NONE,
      .currentTapTimestamp = 0
    };
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


