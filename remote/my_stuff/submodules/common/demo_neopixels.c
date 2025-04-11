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

ws281x_t handle;
bool addrLedInitialized = false;
bool shouldRedraw = false;
Pixel_t pixels[NEOPIXEL_NUM_LEDS];

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
       .rgb = (color_rgb_t){.r = 0, .g = 0, .b = 0},
       .hsv = (color_hsv_t){.h = 0.0, .s = 0.0, .v = 0.0},
       .x = (i % NEOPIXEL_NUM_COLUMNS),
       .y = (i / NEOPIXEL_NUM_ROWS),
       // TODO neighbors?
       };
  }

  // Should the thread be initialized here? should there be a thread?

  addrLedInitialized = true;
  return true;
}

void Neopixel_DisplayStrip(void)
{
  ws281x_write(&handle);
}

void Neopixel_SetPixelRgb(Pixel_t *p, uint8_t r, uint8_t g, uint8_t b)
{
  p->rgb = (color_rgb_t){.r = r, .g = g, .b = b};
  color_rgb2hsv(&(p->rgb), &(p->hsv));
}

void Neopixel_SetPixelHsv(Pixel_t *p, float h, float s, float v)
{
  p->hsv = (color_hsv_t){.h = h, .s = s, .v = v};
  color_hsv2rgb(&(p->hsv), &(p->rgb));
}

void Neopixel_Clear(void)
{
  for (int i = 0; i < NEOPIXEL_NUM_LEDS; i++)
  {
    Pixel_t *p = &pixels[i];
    p->rgb = (color_rgb_t){.r = 0, .g = 0, .b = 0};
    p->hsv = (color_hsv_t){.h = 0.0, .s = 0.0, .v = 0.0};
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
  return &pixels[y * NEOPIXEL_NUM_COLUMNS + x];
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

int cmd_pixelset(int argc, char **argv)
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

int cmd_pixelclear(int argc, char **argv)
{
  Neopixel_Clear();
  return 0;
}
