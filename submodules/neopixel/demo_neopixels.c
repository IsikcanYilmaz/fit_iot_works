#include <periph/gpio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "demo_neopixels.h"
#include "ws281x.h"
#include "ws281x_params.h"
#include "thread.h"
#include "ztimer.h"
#include "board.h"
#include "shell.h"

#ifdef NEOPIXEL_ANIMATION_CCN_DISPLAY_ENABLED
#include "animation_ccn_display.h"
#endif

#include "animation_line.h"

Animation_s animations[ANIMATION_MAX] = 
{
#ifdef NEOPIXEL_ANIMATION_CCN_DISPLAY_ENABLED
  [ANIMATION_CCN_DISPLAY] = {
    .name = "ccn_display",
    .init = AnimationCcnDisplay_Init,
    .update = AnimationCcnDisplay_Update,
    .draw = AnimationCcnDisplay_Draw,
  },
#endif
  [ANIMATION_LINE] = {
    .name = "line",
    .init = AnimationLine_Init,
    .update = AnimationLine_Update,
    .draw = AnimationLine_Draw,
  },
  [ANIMATION_CANVAS] = {
    .name = "canvas",
    .init = NULL,
    .update = NULL,
    .draw = NULL,
  }
};

AnimationIdx_e currentAnimationIdx = ANIMATION_LINE;

ws281x_t handle;
bool addrLedInitialized = false;
volatile bool shouldRedraw = false;
Pixel_t pixels[NEOPIXEL_NUM_LEDS];

ztimer_t drawTimer;

kernel_pid_t neopixelThreadId;
static char neopixelThreadStack[THREAD_STACKSIZE_DEFAULT];

kernel_pid_t mainThreadId;

static inline void kickDrawTimer(void)
{
  msg_t drawMessage = (msg_t) {.type = NEOPIXEL_DRAW_ANIMATION};
  ztimer_set_msg(ZTIMER_MSEC, &drawTimer, NEOPIXEL_UPDATE_PERIOD_MS, &drawMessage, thread_getpid());
}

static void * Neopixel_ThreadHandler(void *arg)
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
          if (animations[currentAnimationIdx].update)
            animations[currentAnimationIdx].update();
          if (animations[currentAnimationIdx].draw)
            animations[currentAnimationIdx].draw();
          if (shouldRedraw)
          {
            /*printf("NEOPIXEL_DRAW_ANIMATION\n");*/
            Neopixel_DisplayStrip();
          }
          break;
        }
      default:
        {
          break;
        }
    }
    /*printf("NEOPIXEL THREAD HANDLER %d\n", m.type);*/
  }
}

kernel_pid_t Neopixel_Init(kernel_pid_t i)
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

  for (int i = 0; i < ANIMATION_MAX; i++)
  {
    if (animations[i].init)
    {
      animations[i].init();
    }
  }

  printf("Neopixels initialized\n");

  addrLedInitialized = true;

  // Pass events and such to this thread id
  // can be null
  mainThreadId = i;
  return neopixelThreadId;
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

Pixel_t * Neopixel_GetPixelByLineIdx(uint8_t idx)
{
  if (idx >= NEOPIXEL_NUM_LEDS)
  {
    printf("%s Bad pixel Idx %d\n", __FUNCTION__, idx);
    return NULL;
  }

  if (idx < 8)
  {
    return &pixels[idx];
  }
  else 
  {
    return Neopixel_GetPixelByIdx(NEOPIXEL_NUM_LEDS-(1+idx-NEOPIXEL_NUM_COLUMNS));
  }

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

bool Neopixel_PixelIsBlank(Pixel_t *p)
{
  return (p->rgb.r == 0 && p->rgb.g == 0 && p->rgb.b == 0);
}

void Neopixel_SetAnimation(uint8_t animIdx) // TODO Smooth transitions? prolly no need
{
  if (animIdx > ANIMATION_MAX)
  {
    printf("Bad animation idx %d\n", animIdx);
    return;
  }
  currentAnimationIdx = animIdx;
}

void Neopixel_NextAnimation(void)
{
  Neopixel_SetAnimation((currentAnimationIdx + 1) % ANIMATION_MAX);
}

void Neopixel_IncrementAllByHSV(float h, float s, float v)
{
  for (int i = 0; i < NEOPIXEL_NUM_LEDS; i++)
  {
    Pixel_t *p = Neopixel_GetPixelByIdx(i);
    color_hsv_t hsv = (color_hsv_t) p->hsv;

    hsv.h = fmod(hsv.h + h, 360.0);
    hsv.h = (hsv.h < 0) ? 360.0 + hsv.h : hsv.h;

    hsv.s += s;
    hsv.s = (hsv.s > 0.0) ? ((hsv.s > 1.0) ? 1.0 : hsv.s) : 0.0;

    hsv.v += v;
    hsv.v = (hsv.v > 0.0) ? ((hsv.v > 1.0) ? 1.0 : hsv.v) : 0.0;

    Neopixel_SetPixelHsv(p, hsv.h, hsv.s, hsv.v);
  }
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

// 
int cmd_nextanimation(int argc, char **argv)
{
  Neopixel_NextAnimation();
  return 0;
}

int cmd_setanimation(int argc, char **argv)
{
  if (argc != 2)
  {
    printf("Usage: setanimation <animation name>\n");
    return 1;
  }
  for (int i = 0; i < ANIMATION_MAX; i++)
  {
    if (strncmp(animations[i].name, argv[1], 16) == 0)
    {
      Neopixel_SetAnimation(i);
      return 0;
    }
  }
  printf("Animation %s couldnt be found.\n", argv[1]);
  return 0;
}

#ifdef NEOPIXEL_SHELL_COMMANDS
SHELL_COMMAND(setpixel, "setpixel <x> <y> <r> <g> <b>", cmd_setpixel);
SHELL_COMMAND(clearpixel, "clears all", cmd_clearpixel);
SHELL_COMMAND(setanimation, "setanimation <animation name>", cmd_setanimation);
SHELL_COMMAND(nextanimation, "next animation", cmd_nextanimation);
#endif
