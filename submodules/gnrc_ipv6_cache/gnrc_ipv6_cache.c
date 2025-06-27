#include <stdio.h>
#include "thread.h"
#include "net/gnrc.h"
#include "net/gnrc/netreg.h"
#include "net/gnrc/pkt.h"
#include "net/gnrc/ipv6.h"
#include "net/gnrc/ipv6/hdr.h"
#include "net/gnrc/nettype.h"

#include "gnrc_ipv6_cache.h"


#define MSG_QUEUE_SIZE 8

static char stack[THREAD_STACKSIZE_MAIN];
static msg_t msg_q[MSG_QUEUE_SIZE];


static void cache_event_loop (void *args)
{
    msg_t msg;
    (void)args;

    msg_init_queue(msg_q, MSG_QUEUE_SIZE);

    // Register for a gnrc_nettype   https://doc.riot-os.org/group__net__gnrc.html
    gnrc_netreg_entry_t reg_enrty = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, thread_getpid());
    gnrc_netreg_register(GNRC_NETTYPE_IPV6, &reg_enrty);

    // start the event loop
    while (1) {
        printf("ipv6 cache module: waiting for message.\n");
        msg_receive(&msg);

        switch (msg.type) {
            case GNRC_NETAPI_MSG_TYPE_RCV: {
                // print some bassic information about the packet
                // cache it for later use
                // but for now, just print something
                puts("A message has been received.");
                break;
            }
        }
    }

    return NULL;
}

void gnrc_ipv6_cache_init (void)
{
    kernel_pid_t pid = thread_create(
        stack, sizeof(stack),       // stack pointer and stack size
        THREAD_PRIORITY_MAIN - 1,   // thread priority (the lower, the more important)
        0,                          // thread configuration flag
        cache_event_loop,           // handler function defined above
        NULL,                       // arguument for the thread handler function
        "gnrc_ipv6_cache");         // name of the thread
}
