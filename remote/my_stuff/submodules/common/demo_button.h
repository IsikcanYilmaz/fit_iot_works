#include "ztimer.h"
#include "thread.h"
#include <stdbool.h>

#define BUTTON_THREAD_SLEEP_MS (10)
#define BUTTON_POLL_PERIOD_MS (150)
#define BUTTON_DEBOUNCE_PERIOD_MS (5)
#define BUTTON_LONG_PRESS_TIME_MS (1000)

#define BUTTON_ACTIVE (1) // Active high

typedef enum 
{
  BUTTON_RED,
  BUTTON_GREEN,
  BUTTON_BLUE,
  BUTTON_SHIFT,
  NUM_BUTTONS
} DemoButton_e;

typedef enum  // Polarity
{
	BUTTON_STATE_RELEASED,
	BUTTON_STATE_PRESSED,
	BUTTON_STATE_MAX
} ButtonState_e;

typedef enum ButtonGesture_e_
{
	GESTURE_SINGLE_TAP,
	GESTURE_DOUBLE_TAP,
	GESTURE_TRIPLE_TAP,
  GESTURE_QUADRUPLE_TAP,
  GESTURE_FIVE_TAP,
	GESTURE_LONG_PRESS,
	GESTURE_VLONG_PRESS,
	GESTURE_VVLONG_PRESS,
	GESTURE_VVVLONG_PRESS,
	GESTURE_NONE,
  NUM_GESTURES 
} ButtonGesture_e; // TODO better naming

// This struct defines the message that this module produces and sends out
typedef struct ButtonGestureMessage_s_ // TODO better naming // and maybe it can be bigger than 1 byte?
{
  bool shift:1;
  DemoButton_e button:3;
  ButtonGesture_e gesture:4;
} ButtonGestureMessage_s;

typedef enum LongPressLengthMs_e_
{
	GESTURE_SINGLE_TAP_MS = 0,
	GESTURE_LONG_PRESS_MS = 1000,
	GESTURE_VLONG_PRESS_MS = 2000,
	GESTURE_VVLONG_PRESS_MS = 3000,
	GESTURE_VVVLONG_PRESS_MS = 7000
} LongPressLengthMs_e;

// For IPC between our main thread, IRQ handler, and gesture timers
typedef enum ButtonThreadMessageType_e_
{
  BUTTON_IRQ_HAPPENED,
  BUTTON_GESTURE_TIMER_TIMEOUT,
  BUTTON_DEBOUNCE_TIMER_TIMEOUT,
} ButtonThreadMessageType_e;

typedef struct ButtonThreadMessage_s_
{
  ButtonThreadMessageType_e type; 
  DemoButton_e idx;
} __attribute__((packed)) ButtonThreadMessage_s;

typedef struct ButtonContext_s_{
  ButtonState_e prevState;
  ButtonState_e currentState;

  ztimer_t debounceTimer; // TODO Decide if this is needed
  ztimer_t gestureTimer;

  uint16_t currentNumTaps;
  ButtonGesture_e currentGesture;
  uint32_t currentTapTimestamp;

} ButtonContext_s;

void Button_Init(kernel_pid_t i);
void Button_Deinit(void);
