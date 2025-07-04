#include <stdio.h>
#include <stdlib.h>
#include "iperf.h"
#include "relayer.h"
#include "logger.h"

extern uint8_t rxtxBuffer[IPERF_BUFFER_SIZE_BYTES];
extern IperfConfig_s config;

static uint8_t *cacheBuffer = (uint8_t *) &rxtxBuffer;

void Iperf_RelayerIntercept(gnrc_pktsnip_t *snip)
{
  // We care about IPv6 and UNDEF snips. 
  gnrc_pktsnip_t *ipv6 = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_IPV6);
  gnrc_pktsnip_t *undef = gnrc_pktsnip_search_type(snip, GNRC_NETTYPE_UNDEF);
  
  if (ipv6)
  {
    printf("JON IPV6 RECEIVED\n");

  }

  if (undef)
  {
    printf("JON UNDEF SNIP RECEIVED\n");
    for (int i = 0; i < snip->size; i++)
    {
      char data = * (char *) (snip->data + i);
      printf("%02x %c %s", data, data, (i % 8 == 0 && i > 0) ? "\n" : "");
    }
    printf("\n");
  }
}
