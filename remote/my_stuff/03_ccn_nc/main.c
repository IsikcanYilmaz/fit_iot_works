
#include "msg.h"
#include "shell.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/pktdump.h"

#include "ccn-lite-riot.h"
#include "ccnl-pkt-ndntlv.h"

#include "thread.h"
#include "xtimer.h"
#include "ztimer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* main thread's message queue */
#define MAIN_QUEUE_SIZE (8)
#define NAME_SIZE (16)

#define CONTENT_SIZE (16) // Bytes

#define MAX_HOSTS (16) // Including me

static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

static char myName[NAME_SIZE+1];

static uint16_t myId = 0xffff;

static uint16_t knownHosts[MAX_HOSTS];
static uint16_t numKnownHosts = 0;

static bool initialized = false;

/*
 * ~ SHELL COMMANDS ~
 */

static int getTime(int argc, char **argv)
{
	uint32_t t = ztimer_now(ZTIMER_USEC);
	printf("%d\n", t);
	return 0;
}

static int setName(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("argc %d needs to be 2\n");
		return 1;
	}
	strncpy(&myName, argv[1], NAME_SIZE);
	myId = atoi(&myName);
	printf("Set name to %s (%d)\n", myName, myId);
	return 0;
}

static int getName(int argc, char **argv)
{
	printf("%s\n", myName);
	return 0;
}

static int setHosts(int argc, char **argv)
{
	if (argc == 1)
	{
		printf("Need hostnames!\n");
		return 1;
	}
	numKnownHosts = 0;
	for (int i = 1; i < argc; i++)
	{
		printf("New host m3-%s\n", argv[i]);
		knownHosts[i-1] = atoi(argv[i]);
		numKnownHosts++;
	}
	return 0;
}

static int getHosts(int argc, char **argv)
{
	for (int i = 0; i < numKnownHosts; i++)
	{
		printf("Host %d = m3-%d\n", i, knownHosts[i]);
	}
	return 0;
}

static int startProgram(int argc, char **argv)
{
	if (myId == 0xffff || numKnownHosts == 0)
	{
		printf("myId %x numKnownHosts %d BAD!\n", myId, numKnownHosts);
		return 1;
	}
	initialized = true;
	return 0;
}

static int showCs(int argc, char **argv)
{
	ccnl_cs_dump(&ccnl_relay);
	return 0;
}

SHELL_COMMAND(gettime, "Get time in usec", getTime);
SHELL_COMMAND(setname, "Set name", setName);
SHELL_COMMAND(getname, "Get name", getName);
SHELL_COMMAND(sethosts, "Set available hosts", setHosts);
SHELL_COMMAND(gethosts, "Get set hosts", getHosts);
SHELL_COMMAND(showcs, "Show contents", showCs);
SHELL_COMMAND(startprogram, "Start our program", startProgram);

/*
 * ~ PROGRAM ~
 */
char experiment_threadStack[THREAD_STACKSIZE_DEFAULT];
static unsigned char _out[CCNL_MAX_PACKET_SIZE];

typedef struct Content_s
{
} Content_t;

void *experiment_threadHandler(void *arg)
{
	(void) arg;
	while(!initialized) {
		ztimer_sleep(ZTIMER_MSEC, 1000);
	}
	printf("Initialized!\n");
	uint32_t ctr = 0;
	while(true)
	{
		// Create new content with the name $myName
		printf("\n%d\n%d\n", ctr, __LINE__);ztimer_sleep(ZTIMER_MSEC, 10);
		struct ccnl_prefix_s *prefix = ccnl_URItoPrefix(&myName, CCNL_SUITE_NDNTLV, NULL);
		size_t offs = CCNL_MAX_PACKET_SIZE;
		size_t reslen = 0;
		unsigned char *buf = "ASD";
		printf("%d\n", __LINE__);ztimer_sleep(ZTIMER_MSEC, 10);
		int8_t ret = ccnl_ndntlv_prependContent(prefix, (unsigned char*) buf, strlen(buf), NULL, NULL, &offs, _out, &reslen);

		printf("%d\n", __LINE__);ztimer_sleep(ZTIMER_MSEC, 10);
		ccnl_prefix_free(prefix);
		unsigned char *olddata;
		unsigned char *data = olddata = _out + offs;

		size_t len;
		uint64_t typ;

		printf("%d\n", __LINE__);ztimer_sleep(ZTIMER_MSEC, 10);
		if (ccnl_ndntlv_dehead(&data, &reslen, &typ, &len) ||
			typ != NDN_TLV_Data) {
			return -1;
		}

		struct ccnl_content_s *c = 0;
		
		printf("%d\n", __LINE__);ztimer_sleep(ZTIMER_MSEC, 10);
		struct ccnl_pkt_s *pk = ccnl_ndntlv_bytes2pkt(typ, olddata, &data, &reslen);

		printf("%d\n", __LINE__);ztimer_sleep(ZTIMER_MSEC, 10);
		c = ccnl_content_new(&pk);
		c->flags |= CCNL_CONTENT_FLAGS_STATIC;
		msg_t m = { .type = CCNL_MSG_CS_ADD, .content.ptr = c };

		printf("%d\n", __LINE__);ztimer_sleep(ZTIMER_MSEC, 10);
		if(msg_send(&m, ccnl_event_loop_pid) < 1)
		{
			printf("could not add content\n");
		}
		else
		{
			printf("New content created!\n");
		}

		printf("%d\n", __LINE__);ztimer_sleep(ZTIMER_MSEC, 10);
		ztimer_sleep(ZTIMER_MSEC, 1000);
		ctr++;
	}
}

/*
 * ~ MAIN ~
 */
int main(void)
{
	msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);

	puts("Basic CCN-Lite example");

	ccnl_core_init();

	ccnl_start();

	/* get the default interface */
	gnrc_netif_t *netif;

	/* set the relay's PID, configure the interface to use CCN nettype */
	if (((netif = gnrc_netif_iter(NULL)) == NULL) ||
		(ccnl_open_netif(netif->pid, GNRC_NETTYPE_CCN) < 0)) {
		puts("Error registering at network interface!");
		return -1;
	}

#ifdef MODULE_NETIF
	gnrc_netreg_entry_t dump = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL,
	gnrc_pktdump_pid);
	gnrc_netreg_register(GNRC_NETTYPE_CCN_CHUNK, &dump);
#endif

	kernel_pid_t experiment_threadId = thread_create(
		experiment_threadStack,
		sizeof(experiment_threadStack),
		THREAD_PRIORITY_MAIN - 1,
		THREAD_CREATE_STACKTEST,
		experiment_threadHandler,
		NULL,
		"experiment_thread"
	);

	char line_buf[SHELL_DEFAULT_BUFSIZE];
	shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);
	return 0;
}
