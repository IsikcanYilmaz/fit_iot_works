
#include "msg.h"
#include "shell.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/pktdump.h"

#include "thread.h"
#include "xtimer.h"
#include "ztimer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "demo_button.h"
#include "demo_neopixels.h"
#include "onboard_leds.h"
#include "demo_throttlers.h"

#include "iperf.h"

/* main thread's message queue */
#define MAIN_QUEUE_SIZE (32)

static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];
char line_buf[SHELL_DEFAULT_BUFSIZE];

#ifdef DEMO_CONFIG
char buttonSignalDispatcherStack[THREAD_STACKSIZE_DEFAULT];
extern const shell_command_xfa_t shell_commands_xfa_v2[];
static void *buttonSignalDispatcherThread(void *arg)
{
  (void) arg;
  while (true)
  {
    msg_t m;
    msg_receive(&m); // blocking call
    ButtonGestureMessage_s *gestureMessage = (ButtonGestureMessage_s *) &m.content.value;
    switch(gestureMessage->button)
    {
      case BUTTON_RED:
      {
        char *argv[] = {"iperf", (gestureMessage->shift) ? "restart" : "start", NULL};
        int argc = 2;
        Iperf_CmdHandler(argc, argv);
        break;
      }
      case BUTTON_GREEN:
      {
        break;
      }
      case BUTTON_BLUE:
      {
        break;
      }
      case BUTTON_SHIFT:
      {
        break;
      }
      default:
      {}
    }
  }
}
#endif

/*
 * ~ MAIN ~
 */
int main(void)
{
	msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);

  OnboardLeds_Init();
  Throttler_Init();
  #ifdef DEMO_CONFIG
  kernel_pid_t buttonDispatchThreadId = thread_create(
    buttonSignalDispatcherStack,
    sizeof(buttonSignalDispatcherStack),
    THREAD_PRIORITY_MAIN - 1,
    THREAD_CREATE_STACKTEST,
    buttonSignalDispatcherThread,
    NULL,
    "ccn_nc_thread"
	);
  Neopixel_Init();
  Button_Init(buttonDispatchThreadId);
  #endif

	shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);
	return 0;
}
