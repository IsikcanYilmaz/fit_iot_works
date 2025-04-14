#include <periph/gpio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "demo_neopixels.h"
#include "ws281x.h"
#include "ws281x_params.h"
#include "thread.h"
#include "ztimer.h"
#include "board.h"

#include "animation_ccn_display.h"
#include "animation_line.h"

Animation_s animations[ANIMATION_MAX] = 
{
  [ANIMATION_CCN_DISPLAY] = {
    .name = "ccn_display",
    .init = AnimationCcnDisplay_Init,
    .draw = AnimationCcnDisplay_Draw,
  },
  [ANIMATION_LINE] = {
    .name = "line",
    .init = AnimationLine_Init,
    .draw = AnimationLine_Draw,
  }
};

AnimationIdx_e currentAnimationIdx = ANIMATION_CCN_DISPLAY;

ws281x_t handle;
bool addrLedInitialized = false;
bool shouldRedraw = false;
Pixel_t pixels[NEOPIXEL_NUM_LEDS];

ztimer_t drawTimer;

kernel_pid_t neopixelThreadId;
static char neopixelThreadStack[THREAD_STACKSIZE_DEFAULT];

static inline void kickDrawTimer(void)
{
  msg_t drawMessage = (msg_t) {.type = NEOPIXEL_DRAW_ANIMATION};
  ztimer_set_msg(ZTIMER_MSEC, &drawTimer, NEOPIXEL_UPDATE_PERIOD_MS, &drawMessage, thread_getpid());
}

static void *Neopixel_ThreadHandler(void *arg)
{
  while (true)
  {
    msg_t m;

    if (!ztimer_is_set(ZTIMER_MSEC, &drawTimer))
    {
      kickDrawTimer();
    }

    msg_receive(&m);
    switch(m.type)
    {
      case NEOPIXEL_REFRESH:
        {
          break;
        }
      case NEOPIXEL_TRIGGER_ANIMATION:
        {
          break;
        }
      case NEOPIXEL_DRAW_ANIMATION:
        {
          if (shouldRedraw)
          {
            Neopixel_DisplayStrip();
          }
          break;
        }
      default:
        {
          break;
        }
    }
    printf("NEOPIXEL THREAD HANDLER\n");
  }
}

bool Neopixel_Init(void)
{
  // Initialize ws281x module
  int retval;
  if ((retval = ws281x_init(&handle, &ws281x_params[0])) != 0) {
    printf("Initialization failed with error code %d\n", retval);
    return false;
  }

  // Initialize internal books
  for (int i = 0; i < NEOPIXEL_NUM_LEDS; i++)
  {
    pixels[i] = (Pixel_t) {
       .stripIdx = i,
       .rgb = (color_rgb_t){.r = 0, .g = 0, .b = 0},
       .hsv = (color_hsv_t){.h = 0.0, .s = 0.0, .v = 0.0},
       .x = NEOPIXEL_NUM_COLUMNS - 1 - (i % NEOPIXEL_NUM_COLUMNS),
       .y = (i / NEOPIXEL_NUM_ROWS),
       // TODO neighbors?
       };
  }

  drawTimer = (ztimer_t) {.callback = NULL, .arg = NULL};

  neopixelThreadId = thread_create(
    neopixelThreadStack,
    sizeof(neopixelThreadStack),
    THREAD_PRIORITY_MAIN - 1,
    THREAD_CREATE_STACKTEST,
    Neopixel_ThreadHandler,
    NULL,
    "neopixel_thread"
  );

  printf("Neopixels initialized\n");

  addrLedInitialized = true;
  return true;
}

void Neopixel_DisplayStrip(void)
{
  ws281x_write(&handle);
  shouldRedraw = false;
}

void Neopixel_SetPixelRgb(Pixel_t *p, uint8_t r, uint8_t g, uint8_t b)
{
  p->rgb = (color_rgb_t){.r = r, .g = g, .b = b};
  color_rgb2hsv(&(p->rgb), &(p->hsv));
	ws281x_set(&handle, p->stripIdx, p->rgb);
  shouldRedraw = true;
}

void Neopixel_SetPixelHsv(Pixel_t *p, float h, float s, float v)
{
  p->hsv = (color_hsv_t){.h = h, .s = s, .v = v};
  color_hsv2rgb(&(p->hsv), &(p->rgb));
	ws281x_set(&handle, p->stripIdx, p->rgb);
  shouldRedraw = true;
}

void Neopixel_Clear(void)
{
  for (int i = 0; i < NEOPIXEL_NUM_LEDS; i++)
  {
    Pixel_t *p = &pixels[i];
    Neopixel_SetPixelRgb(p, 0, 0, 0);
  }
}

Pixel_t * Neopixel_GetPixelByIdx(uint8_t idx)
{
  if (idx >= NEOPIXEL_NUM_LEDS)
  {
    printf("%s Bad pixel Idx %d\n", __FUNCTION__, idx);
    return NULL;
  }
  return &pixels[idx];
}

Pixel_t * Neopixel_GetPixelByCoord(uint8_t x, uint8_t y)
{
  if (x >= NEOPIXEL_NUM_COLUMNS || y >= NEOPIXEL_NUM_ROWS)
  {
    printf("%s Bad pixel coord %d %d\n", __FUNCTION__, x, y);
    return NULL;
  }
  return &pixels[y * NEOPIXEL_NUM_COLUMNS + (NEOPIXEL_NUM_COLUMNS - 1 - x)];
}

void Neopixel_PrintPixel(Pixel_t *p)
{
  printf("Pixel x:%d y:%d r:%d g:%d b:%d\n", p->x, p->y, p->rgb.r, p->rgb.g, p->rgb.b);
}

bool Neopixel_ShouldRedraw(void)
{
  return shouldRedraw;
}

// SHELL COMMANDS

int cmd_setpixel(int argc, char **argv)
{
  if (argc != 6)
  {
    printf("Usage: setpixel <x> <y> <r> <g> <b>\n");
    return 1;
  }

  uint8_t x = atoi(argv[1]);
  uint8_t y = atoi(argv[2]);
  uint8_t r = atoi(argv[3]);
  uint8_t g = atoi(argv[4]);
  uint8_t b = atoi(argv[5]);

  printf("Setting %d %d to %d %d %d\n", x, y, r, g, b);
  Pixel_t *p = Neopixel_GetPixelByCoord(x, y);
  if (p == NULL)
  {
    printf("Bad pixel\n");
    return 1;
  }
  Neopixel_SetPixelRgb(p, r, g, b);
  return 0;
}

int cmd_clearpixel(int argc, char **argv)
{
  Neopixel_Clear();
  return 0;
}
