#include <stdbool.h>
#include "color.h"

#define NEOPIXEL_NUM_ROWS (2)
#define NEOPIXEL_NUM_COLUMNS (8)
#define NEOPIXEL_NUM_LEDS (NEOPIXEL_NUM_COLUMNS * NEOPIXEL_NUM_ROWS)
#define NEOPIXEL_UPDATE_PERIOD_MS (100)

typedef struct 
{
  color_rgb_t rgb;
  color_hsv_t hsv;
  uint8_t x;
  uint8_t y;
  struct Pixel_t *neighbors[5];
} Pixel_t;

bool Neopixel_Init(void);
void Neopixel_DisplayStrip(void);
void Neopixel_SetPixelRgb(Pixel_t *p, uint8_t r, uint8_t g, uint8_t b);
void Neopixel_SetPixelHsv(Pixel_t *p, float h, float s, float v);
void Neopixel_Clear(void);
Pixel_t * Neopixel_GetPixelByIdx(uint8_t idx);
Pixel_t * Neopixel_GetPixelByCoord(uint8_t x, uint8_t y);
void Neopixel_PrintPixel(Pixel_t *p);
bool Neopixel_ShouldRedraw(void);

// Shell commands
int cmd_pixelclear(int argc, char **argv);
int cmd_pixelset(int argc, char **argv);
