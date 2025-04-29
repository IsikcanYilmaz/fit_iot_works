
#include "animation_line.h"

#include "demo_neopixels.h"
#include <string.h>

#include "color.h"
#include "ccn-lite-riot.h"
#include "ccnl-pkt-ndntlv.h"
#include "net/gnrc/netif.h"

typedef struct 
{
  uint8_t len;
  uint8_t headIdx; // [0 - 15]
  color_hsv_t hsv; // [0.0 - 360.0, 0.0 - 1.0, 0.0 - 1.0]
} Line_s;

Line_s line;

void AnimationLine_Init(void)
{
  line = (Line_s) {.len = 1, .headIdx = 0};
  line.hsv = (color_hsv_t) {.h = 0.0, .s = 0.8, .v = 0.9};
}

void AnimationLine_Update(void)
{
  line.headIdx = (line.headIdx + 1) % NEOPIXEL_NUM_LEDS;
}

void AnimationLine_Draw(void)
{
  /*for (int i = 0; i < line.len; i++) // TOOD currently len = 1*/
  /*{*/
  /**/
  /*}*/
  /*Neopixel_Clear();*/
  Neopixel_SetPixelHsv(Neopixel_GetPixelByLineIdx(line.headIdx), line.hsv.h, line.hsv.s, line.hsv.v);
  Neopixel_IncrementAllByHSV(-40.0, 0.0, -0.20);
  printf("%d\n", line.headIdx);
}
