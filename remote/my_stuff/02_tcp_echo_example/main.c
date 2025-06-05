#include "net/gnrc.h"
#include "net/af.h"
#include "net/ipv6/addr.h"
#include "net/sock/tcp.h"
#include "net/gnrc/nettype.h"
#include "net/utils.h" 
#include "shell.h"

#define SOCK_QUEUE_LEN  (1U)

sock_tcp_t sock;
sock_tcp_t sock_queue[SOCK_QUEUE_LEN];
uint8_t buf[128];


int TCP_Echo_Server(int argc, char **argv) {
    puts("This is the server command");

    sock_tcp_ep_t local = SOCK_IPV6_EP_ANY;
    sock_tcp_queue_t queue;
 
    local.port = 12345;
 
    if (sock_tcp_listen(&queue, &local, sock_queue, SOCK_QUEUE_LEN, 0) < 0) {
        puts("Error creating listening queue");
        return 1;
    }
    puts("Listening on port 12345");
    while (1) {
        sock_tcp_t *sock;
 
        if (sock_tcp_accept(&queue, &sock, SOCK_NO_TIMEOUT) < 0) {
            puts("Error accepting new sock");
        }
        else {
            int read_res = 0;
 
            puts("Reading data");
            while (read_res >= 0) {
                read_res = sock_tcp_read(sock, &buf, sizeof(buf),
                                         SOCK_NO_TIMEOUT);
                if (read_res <= 0) {
                    puts("Disconnected");
                    break;
                }
                else {
                    int write_res;
                    printf("Read: \"");
                    for (int i = 0; i < read_res; i++) {
                        printf("%c", buf[i]);
                    }
                    puts("\"");
                    if ((write_res = sock_tcp_write(sock, &buf,
                                                    read_res)) < 0) {
                        puts("Errored on write, finished server loop");
                        break;
                    }
                }
            }
            sock_tcp_disconnect(sock);
        }
    }
    sock_tcp_stop_listen(&queue);
    return 0;
}

int TCP_Echo_Client(int argc, char **argv) {
    puts("This is the client command.");
    int res;
    sock_tcp_ep_t remote = SOCK_IPV6_EP_ANY;
 
    remote.port = 12345;
    ipv6_addr_from_str((ipv6_addr_t *)&remote.addr,"fe80::7c2d:33ff:fea8:8074");
    if (sock_tcp_connect(&sock, &remote, 0, 0) < 0) {
        puts("Error connecting sock");
        return 1;
    }
    puts("Sending \"Hello!\"");
    if ((res = sock_tcp_write(&sock, "Hello!", sizeof("Hello!"))) < 0) {
        puts("Errored on write");
    }
    else {
        if ((res = sock_tcp_read(&sock, &buf, sizeof(buf), SOCK_NO_TIMEOUT)) <= 0) {
            puts("Disconnected");
        }
        printf("Read: \"");
        for (int i = 0; i < res; i++) {
            printf("%c", buf[i]);
        }
        puts("\"");
    }
    sock_tcp_disconnect(&sock);
    return res;
}

static const shell_command_t commands[] = {
    { "tcpserver", "TCP echo server", TCP_Echo_Server },
    { "tcpclient", "TCP echo client", TCP_Echo_Client },
    { NULL, NULL, NULL }
};

int main(void) {
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(commands, line_buf, SHELL_DEFAULT_BUFSIZE);
}