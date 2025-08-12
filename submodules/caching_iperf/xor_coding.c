#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "xor_coding.h"

#define XOR_CODING_DEBUG 0

/*
* Coding Module
* Currently only does XOR based (field size of 2) coding
*/

// $SRC = $SRC ^ $DST
void XorCoding_DoCoding(CodingConfig_t *config, CodedPacket_t *src, CodedPacket_t *dst)
{
  // First code together the vectors 
  src->vector = src->vector ^ dst->vector;
  
  // Then, code the payloads
  uint8_t *srcPl = src->payload;
  uint8_t *dstPl = dst->payload;
  for (uint32_t i = 0; i < config->payloadSizeBytes; i++)
  {
    srcPl[i] = srcPl[i] ^ dstPl[i];
  }
}

void XorCoding_PrintEntry(XorCodingConfig_t *config, CodedPacket_t *c)
{
  printf("Vector: ");
  uint32_t vector = c->vector;
  for (int i = 0; i < 32; i++)
  {
    printf("%d", (c->vector & (0x1 << i)) ? 1 : 0 );
  }
  printf("\nContent: \n");
  for (int i = 0; i < config->payloadSizeBytes; i++)
  {
    printf("%x ", c->payload[i]);
  }
  printf("\n\n");
}

#if 0
// TESTER
int main()
{
  uint8_t a[] = {1, 2, 3, 4, 5, 6};
  uint8_t b[] = {7, 1, 0, 1, 20, 8};

  uint8_t aaBuf[sizeof(uint32_t) + (sizeof(uint8_t) * 32)];
  uint8_t bbBuf[sizeof(uint32_t) + (sizeof(uint8_t) * 32)];

  CodedPacket_t *aa = (CodedPacket_t *) &aaBuf;
  CodedPacket_t *bb = (CodedPacket_t *) &bbBuf;

  memset(&(aa->payload), 0x00, 32);
  memset(&(bb->payload), 0x00, 32);

  memcpy(&(aa->payload), &a, sizeof(a));
  memcpy(&(bb->payload), &b, sizeof(b));
  aa->vector = 0x1 << 0;
  bb->vector = 0x1 << 1;

  XorCoding_PrintEntry(&config, aa);
  XorCoding_PrintEntry(&config, bb);
  
  printf("Doing coding!\n");
  XorCoding_DoCoding(&config, aa, bb);

  XorCoding_PrintEntry(&config, aa);
  XorCoding_PrintEntry(&config, bb);

  printf("Doing coding again!\n");
  XorCoding_DoCoding(&config, aa, bb);

  XorCoding_PrintEntry(&config, aa);
  XorCoding_PrintEntry(&config, bb);

  return 0;
}

#endif
