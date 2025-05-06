
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

/* main thread's message queue */
#define MAIN_QUEUE_SIZE (32)

static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

/*
 * ~ PROGRAM ~
 */
char experiment_threadStack[THREAD_STACKSIZE_DEFAULT];
void *experiment_threadHandler(void *arg) // TODO no need for this tbh
{
	(void) arg;
  while(true)
  {
    msg_t m;
    msg_receive(&m);
    printf("Message received %d\n", m.content.value);
  }
}

/*
 * ~ MAIN ~
 */
int main(void)
{
	msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);

	kernel_pid_t experimentThreadId = thread_create(
		experiment_threadStack,
		sizeof(experiment_threadStack),
		THREAD_PRIORITY_MAIN - 1,
		THREAD_CREATE_STACKTEST,
		experiment_threadHandler,
		NULL,
		"experiment_thread"
	);

  // Thread initializations
  kernel_pid_t buttonThreadId = Button_Init(experimentThreadId);
  kernel_pid_t neopixelThreadId = Neopixel_Init(experimentThreadId);
  OnboardLeds_Init();
  Throttler_Init();

	char line_buf[SHELL_DEFAULT_BUFSIZE];
	shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);
	return 0;
}
