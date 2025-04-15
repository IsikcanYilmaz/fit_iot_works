#include "animation_ccn_display.h"
#include "demo_neopixels.h"
#include <string.h>

#include "color.h"
#include "ccn-lite-riot.h"
#include "ccnl-pkt-ndntlv.h"
#include "net/gnrc/netif.h"

typedef enum 
{ 
  PIXEL_OFF,
  PIXEL_SOLID,
  PIXEL_BLINKING,
  PIXEL_NUM_STATES
} CcnlPixelState_e;

typedef struct 
{
  CcnlPixelState_e state;
  color_rgb_t rgb;
  Pixel_t *p;
} CcnlPixel_s;

CcnlPixel_s ccnlPixels[NEOPIXEL_NUM_LEDS];

void AnimationCcnDisplay_Init(void)
{
  for (int i = 0; i < NEOPIXEL_NUM_LEDS; i++)
  {
    ccnlPixels[i] = (CcnlPixel_s) {.state = PIXEL_OFF, .rgb = {.r=0, .g=0, .b=0}, .p = Neopixel_GetPixelByIdx(i)};
  }
}

void AnimationCcnDisplay_Update(void)
{
  // Periodically check cs and pit
  char s[CCNL_MAX_PREFIX_SIZE];

  // Reset our pixels TODO could be cleaner but who cares?
  for (int i = 0; i < NEOPIXEL_NUM_LEDS; i++)
  {
    ccnlPixels[i].state = PIXEL_OFF;
  }

  // cs
  struct ccnl_content_s *contentPtr = ccnl_relay.contents;
  for (int i = 0; i < ccnl_relay.contentcnt; i++)
  {
    char *str = ccnl_prefix_to_str(contentPtr->pkt->pfx, s, CCNL_MAX_PREFIX_SIZE);
    /*printf("%s content %d) %s : %s\n", __FUNCTION__, i, str, contentPtr->pkt->content);*/

    contentPtr = contentPtr->next;
    CcnlPixel_s *cpx = &ccnlPixels[i];

    if (strncmp(str, "/red", CCNL_MAX_PREFIX_SIZE) == 0)
    {
      LED0_ON;
      cpx->state = PIXEL_SOLID; 
      cpx->rgb = COLOR_RED;
      continue;
    }
    else if (strncmp(str, "/grn", CCNL_MAX_PREFIX_SIZE) == 0)
    {
      LED1_ON;
      cpx->state = PIXEL_SOLID; 
      cpx->rgb = COLOR_GREEN;
      continue;
    }
    else if (strncmp(str, "/blu", CCNL_MAX_PREFIX_SIZE) == 0)
    {
      LED2_ON;
      cpx->state = PIXEL_SOLID;
      cpx->rgb = COLOR_BLUE;
      continue;
    }
  }

  // pit
  struct ccnl_interest_s *pendingInterest = ccnl_relay.pit;
  for (int i = 0; i < ccnl_relay.pitcnt; i++)
  {
    struct ccnl_prefix_s *prefix = pendingInterest->pkt->pfx;
    char *str = ccnl_prefix_to_str(prefix, s, CCNL_MAX_PREFIX_SIZE);
    /*printf("%s interest %d) %s \n", __FUNCTION__, i, str);*/
    pendingInterest = pendingInterest->next;

    CcnlPixel_s *cpx = &ccnlPixels[NEOPIXEL_NUM_LEDS/2 + i];

    if (strncmp(str, "/red", CCNL_MAX_PREFIX_SIZE) == 0) // TODO can clean up a bit
    {
      LED0_ON;
      cpx->state = PIXEL_BLINKING; 
      cpx->rgb = COLOR_RED;
      continue;
    }
    else if (strncmp(str, "/grn", CCNL_MAX_PREFIX_SIZE) == 0)
    {
      LED1_ON;
      cpx->state = PIXEL_BLINKING; 
      cpx->rgb = COLOR_GREEN;
      continue;
    }
    else if (strncmp(str, "/blu", CCNL_MAX_PREFIX_SIZE) == 0)
    {
      LED2_ON;
      cpx->state = PIXEL_BLINKING;
      cpx->rgb = COLOR_BLUE;
      continue;
    }
  }

}

void AnimationCcnDisplay_Draw(void)
{
  Neopixel_SetPixelRgb(Neopixel_GetPixelByCoord(0, 0), 10, 0, 0);
  for (int i = 0; i < NEOPIXEL_NUM_LEDS; i++)
  {
    CcnlPixel_s *cpx = &ccnlPixels[i];
    if (cpx->state == PIXEL_SOLID || (cpx->state == PIXEL_BLINKING && Neopixel_PixelIsBlank(cpx->p)))
    {
      Neopixel_SetPixelRgb(cpx->p, cpx->rgb.r, cpx->rgb.g, cpx->rgb.b);
    }
    else 
    {
      Neopixel_SetPixelRgb(cpx->p, 0, 0, 0);
    }
  }
}
