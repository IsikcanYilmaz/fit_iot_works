#include <stdio.h>
#include "thread.h"
#include "net/gnrc.h"
#include "net/gnrc/netreg.h"
#include "net/gnrc/pkt.h"
#include "net/gnrc/ipv6.h"
#include "net/gnrc/ipv6/hdr.h"
#include "net/gnrc/nettype.h"

#include "gnrc_ipv6_cache.h"


#define MSG_QUEUE_SIZE 64

static void ipv6_cache_thread(void *arg)
{
    msg_t msg;
    (void)arg;
    //static msg_t _msg_q[MSG_QUEUE_SIZE];
    //msg_init_queue(_msg_q, MSG_QUEUE_SIZE);

    while (1) {
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

void gnrc_ipv6_cache_init(void)
{
    static char stack[THREAD_STACKSIZE_MAIN];

    kernel_pid_t pid = thread_create(
        stack, sizeof(stack),       // stack pointer and stack size
        THREAD_PRIORITY_MAIN - 1,   // thread priority (the lower, the more important)
        NULL,                       // thread configuration flag
        ipv6_cache_thread,          // handler function defined above
        NULL,                       // arguument for the thread handler function
        "gnrc_ipv6_cache");         // name of the thread

    // Register for a gnrc_nettype   https://doc.riot-os.org/group__net__gnrc.html
    gnrc_netreg_entry_t reg_enrty = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, pid);
    gnrc_netreg_register(GNRC_NETTYPE_IPV6);
}
