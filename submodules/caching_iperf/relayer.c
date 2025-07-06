#include <stdio.h>
#include <stdlib.h>
#include "iperf.h"
#include "relayer.h"
#include "logger.h"
#include "iperf_pkt.h"

#include "net/ipv6/hdr.h"
#include "net/ipv6/addr.h"

extern uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];
extern IperfConfig_s config;

static uint8_t *cacheBuffer = (uint8_t *) &rxtxBuffer;



bool Iperf_RelayerIntercept(gnrc_pktsnip_t *snip)
{
  // We care about IPv6 and UNDEF snips. 
  gnrc_pktsnip_t *ipv6 = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_IPV6);
  gnrc_pktsnip_t *undef = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_UNDEF);
  
  if (ipv6)
  {
    printf("[IPV6]\n");
    ipv6_hdr_print(ipv6->data);
  }

  if (undef)
  {
    printf("[UNDEF]\n");
    for (int i = 0; i < undef->size; i++)
    {
      char data = * (char *) (undef->data + i);
      printf("%02x %c %s", data, data, (i % 8 == 0 && i > 0) ? "\n" : "");
    }
    printf("\n");
  }

  IperfUdpPkt_t *iperfPkt = (IperfUdpPkt_t *) (undef->data + 8); // JON TODO HACK //  Idk why I need this 8 byte offset but i do
  ipv6_hdr_t *ipv6header = (ipv6_hdr_t *) ipv6->data;
  
  printf("\n PL:%s\n", iperfPkt->payload);

  if (strncmp(iperfPkt->payload, "asdqwe", 6) == 0)
  {
    printf("TEST PAYLOAD RECEIVED\n");


    // TODO JON GOTTA BE A BETTER WAY THANB THIS!
    // This conversts ip struct to string to ip struct back again which is very inefficient!
    char addr_str[IPV6_ADDR_MAX_STR_LEN];
    ipv6_addr_to_str(addr_str, &ipv6header->src, sizeof(addr_str));
    printf("RETURN TO %s\n", addr_str);

    char rawPkt[sizeof(IperfUdpPkt_t) + sizeof(IperfInterest_t)];
    IperfUdpPkt_t *newIperfPkt = (IperfUdpPkt_t *) &rawPkt;
    /*memcpy(&newIperfPkt->payload, iperfPkt->payload, iperfPkt->plSize);*/
    newIperfPkt->msgType = iperfPkt->msgType;
    /*Iperf_SocklessUdpSendToStringAddr((char *) &rawPkt, sizeof(rawPkt), addr_str);*/
    return false; 
  }

  if (ipv6)
  {
    printf("[IPV6]\n");
    ipv6_hdr_print(ipv6->data);
  }

  return true;
}
