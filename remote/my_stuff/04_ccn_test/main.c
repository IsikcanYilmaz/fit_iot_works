
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "thread.h"
#include "xtimer.h"
#include "ztimer.h"

#include "ccn-lite-riot.h"
#include "ccnl-pkt-builder.h"

#include "msg.h"
#include "shell.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/pktdump.h"
// #include "pktcnt.h"
#include "xtimer.h"

#include "ccn-lite-riot.h"
#include "ccnl-pkt-builder.h"
// #include "net/hopp/hopp.h"

#define MAIN_QUEUE_SIZE (8)

#define PREFIX "m3"

static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

uint8_t my_hwaddr[GNRC_NETIF_L2ADDR_MAXLEN];
char my_hwaddr_str[GNRC_NETIF_L2ADDR_MAXLEN * 3];

static int _publish(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	// char name[30];
	// int name_len = sprintf(name, "/%s/%s", PREFIX, my_hwaddr_str);
	// 

}

int producer_func(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                   struct ccnl_pkt_s *pkt){
	(void)from;
	// if(pkt->pfx->compcnt == 4) { /* /PREFIX/ID/gasval/<value> */
	//     /* match PREFIX and ID and "gasval" */
	//     if (!memcmp(pkt->pfx->comp[0], PREFIX, pkt->pfx->complen[0]) &&
	//         !memcmp(pkt->pfx->comp[1], my_hwaddr_str, pkt->pfx->complen[1]) &&
	//         !memcmp(pkt->pfx->comp[2], "gasval", pkt->pfx->complen[2])) {
	//         return produce_cont_and_cache(relay, pkt, atoi((const char *)pkt->pfx->comp[3]));
	//     }
	// }
	printf("%s\n", __FUNCTION__);
	return 0;
}

static const shell_command_t shell_commands[] = {
    // { "hr", "start HoPP root", _root },
    { "hp", "publish data", _publish },
    // { "he", "HoPP end", _hopp_end },
    // { "req_start", "start periodic content requests", _req_start },
    // { "pktcnt_start", "start pktcnt module", _pktcnt_start },
    { NULL, NULL, NULL }
};

int main(void)
{
	msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);
	printf("CCN TEST\n");
	ccnl_core_init();
	ccnl_start();
	gnrc_netif_t *netif;
	/* set the relay's PID, configure the interface to use CCN nettype */
	if (((netif = gnrc_netif_iter(NULL)) == NULL) || (ccnl_open_netif(netif->pid, GNRC_NETTYPE_CCN) < 0)) 
	{
		puts("Error registering at network interface!");
		return -1;
	}
	gnrc_netreg_entry_t dump = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL,
	gnrc_pktdump_pid);
	gnrc_netreg_register(GNRC_NETTYPE_CCN_CHUNK, &dump);
	// ccnl_set_local_producer(producer_func);
	gnrc_netapi_get(netif->pid, NETOPT_ADDRESS_LONG, 0, my_hwaddr, sizeof(my_hwaddr));
	gnrc_netif_addr_to_str(my_hwaddr, sizeof(my_hwaddr), my_hwaddr_str);
	printf("hwaddr: %s\n", my_hwaddr_str);
	char line_buf[SHELL_DEFAULT_BUFSIZE];
	shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);
	return 0;
}
