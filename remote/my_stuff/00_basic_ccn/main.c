
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
#include "rssi_limitor_func.h"
#include "ccn_nc_demo.h"

/* main thread's message queue */
#define MAIN_QUEUE_SIZE (8)

#define CONTENT_SIZE (128) // Bytes

static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

static bool initialized = false;

/*
 * ~ SHELL COMMANDS ~
 */

int producer_func(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                   struct ccnl_pkt_s *pkt){
	(void)from;
	printf("%s\n", __FUNCTION__);
	return 0;
}

/*
 * ~ PROGRAM ~
 */
char experiment_threadStack[THREAD_STACKSIZE_DEFAULT];

void *experiment_threadHandler(void *arg)
{
	(void) arg;
  while(true)
  {
    /*#ifdef LED0_PIN*/
    /*LED0_TOGGLE;*/
    /*#endif*/
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
#endif
 
  // CCN/NC
  {"prod", "CCN NC Demo Produce", cmd_ccnl_nc_produce},
  {"int", "CCN NC Demo Interest", cmd_ccnl_nc_interest},
  {"cs", "CCN NC Demo Print CS", cmd_ccnl_nc_show_cs},
  {"rm", "CCN NC Demo Remove from CS", cmd_ccnl_nc_rm_cs},

  //
  {NULL, NULL, NULL}
};

/*
 * ~ MAIN ~
 */
int main(void)
{
	msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);

	puts("ASDQWE");

	ccnl_core_init();
	ccnl_start();

	/* get the default interface */
	gnrc_netif_t *netif;

	/* set the relay's PID, configure the interface to use CCN nettype */
	if (((netif = gnrc_netif_iter(NULL)) == NULL) || (ccnl_open_netif(netif->pid, GNRC_NETTYPE_CCN) < 0)) 
	{
		puts("Error registering at network interface!");
		return -1;
	}

	gnrc_netreg_entry_t dump = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, gnrc_pktdump_pid);
	gnrc_netreg_register(GNRC_NETTYPE_CCN_CHUNK, &dump);

	ccnl_set_local_producer(producer_func);

	/*kernel_pid_t experiment_threadId = thread_create(*/
	/*	experiment_threadStack,*/
	/*	sizeof(experiment_threadStack),*/
	/*	THREAD_PRIORITY_MAIN - 1,*/
	/*	THREAD_CREATE_STACKTEST,*/
	/*	experiment_threadHandler,*/
	/*	NULL,*/
	/*	"experiment_thread"*/
	/*);*/

  CCN_NC_Init();

	char line_buf[SHELL_DEFAULT_BUFSIZE];
	shell_run(commands, line_buf, SHELL_DEFAULT_BUFSIZE);
	return 0;
}
