
#include "msg.h"
#include "shell.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/pktdump.h"

#include "ccn-lite-riot.h"
#include "ccnl-pkt-ndntlv.h"
#include "ccnl-pkt-builder.h"
#include "ccnl-producer.h"

#include "thread.h"
#include "xtimer.h"
#include "ztimer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "node_settings.h"
#include "ccn_nc_demo.h"
#include "demo_neopixels.h"

#ifdef JON_RSSI_LIMITING
#include "demo_throttlers.h"
#endif

/* main thread's message queue */
#define MAIN_QUEUE_SIZE (8)

#define CONTENT_SIZE (128) // Bytes

static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

static bool initialized = false;

/*
 * ~ SHELL COMMANDS ~
 */

/*
 * ~ PROGRAM ~
 */
char experiment_threadStack[THREAD_STACKSIZE_DEFAULT];

void *experiment_threadHandler(void *arg) // TODO no need for this tbh
{
	(void) arg;
  while(true)
  {
    #ifdef LED0_PIN
    /*LED0_TOGGLE;*/
    /*LED2_TOGGLE;*/
    #endif
    ztimer_sleep(ZTIMER_MSEC, 1000);
  }
}

static const shell_command_t commands[] = {

  // GENERAL
  {"gettime", "Get Time", Cmd_GetTime},
  {"getname", "Get Name", Cmd_GetName},
  {"sethosts", "Set Hosts", Cmd_SetHosts},
  {"gethosts", "Get Hosts", Cmd_GetHosts},
  {"startprogram", "Start Program", Cmd_StartProgram},

  // RSSI LIMITING
#ifdef JON_RSSI_LIMITING;
  {"setrssi", "set rssi limitor", setRssiLimitor},
  {"getrssi", "get rssi limitor", getRssiLimitor},
  {"rssiprint", "toggle rssi print messages", setRssiPrint},

  // FAKE LATENCY
  {"setfakelat", "set fake latency", setFakeLatency},
  {"getfakelat", "get fake latency", getFakeLatency},
#endif

  {"txpower", "set tx power", Throttler_CmdSetTxPower},
 
  // CCN
  {"prod", "Produce", cmd_ccnl_nc_produce},
  {"int", "Interest", cmd_ccnl_nc_interest},
  {"cs", "Print CS", cmd_ccnl_nc_show_cs},
  {"pit", "Print PIT", cmd_ccnl_nc_show_pit},
  {"rm", "Remove from CS", cmd_ccnl_nc_rm_cs},
  {"rmall", "Clear CS", cmd_ccnl_nc_rm_cs_all},
  {"sethw", "Set Hardware Type", cmd_ccnl_nc_set_hw},

  // Neopixel
  {"setpixel", "set neopixel", cmd_setpixel},
  {"clearpixel", "clear neopixels", cmd_clearpixel},
  {"nextanimation", "next animation", cmd_nextanimation},
  {"setanimation", "set animation", cmd_setanimation},

  //
  {NULL, NULL, NULL}
};

/*
 * ~ MAIN ~
 */
int main(void)
{
	msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);

	kernel_pid_t experiment_threadId = thread_create(
		experiment_threadStack,
		sizeof(experiment_threadStack),
		THREAD_PRIORITY_MAIN - 1,
		THREAD_CREATE_STACKTEST,
		experiment_threadHandler,
		NULL,
		"experiment_thread"
	);

  CCN_NC_Init();

	char line_buf[SHELL_DEFAULT_BUFSIZE];
	shell_run(commands, line_buf, SHELL_DEFAULT_BUFSIZE);
	return 0;
}
