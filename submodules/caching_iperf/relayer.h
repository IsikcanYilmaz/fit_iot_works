#include "net/gnrc.h"
#include <stdbool.h>

bool Iperf_RelayerIntercept(gnrc_pktsnip_t *pkt);
void Iperf_PrintCache(void);
void Iperf_PrintCodedCache(void);
int Iperf_LookUpCachedPktPtr(uint16_t pktIdx);
void *Iperf_RelayerThread(void *arg);

void Relayer_CatalogueReceptionTest(uint32_t vec, uint8_t offset);
