#include <stdbool.h>
#include "color.h"

#define NEOPIXEL_NUM_ROWS (2)
#define NEOPIXEL_NUM_COLUMNS (8)
#define NEOPIXEL_NUM_LEDS (NEOPIXEL_NUM_COLUMNS * NEOPIXEL_NUM_ROWS)
#define NEOPIXEL_UPDATE_PERIOD_MS (100)

#define COLOR_BLANK   (color_rgb_t){.r = 0, .g = 0, .b = 0}
#define COLOR_RED     (color_rgb_t){.r = 100, .g = 0, .b = 0}
#define COLOR_GREEN   (color_rgb_t){.r = 0, .g = 100, .b = 0} 
#define COLOR_BLUE    (color_rgb_t){.r = 0, .g = 0, .b = 100}
#define COLOR_YELLOW  (color_rgb_t){.r = 100, .g = 100, .b = 0}
#define COLOR_MAGENTA (color_rgb_t){.r = 100, .g = 0, .b = 100}

typedef enum
{
  NEOPIXEL_REFRESH,
  NEOPIXEL_TRIGGER_ANIMATION,
  NEOPIXEL_DRAW_ANIMATION,
  NEOPIXEL_NUM_MESSAGES
} DemoNeopixelMessage_e;

typedef struct 
{
  uint8_t stripIdx; // index of the pixel on the strip
  color_rgb_t rgb;
  color_hsv_t hsv;
  uint8_t x;
  uint8_t y;
  struct Pixel_t *neighbors[5];
} Pixel_t;

typedef enum
{
  ANIMATION_CCN_DISPLAY,
  ANIMATION_LINE,
  ANIMATION_MAX
} AnimationIdx_e;

typedef struct 
{
  char *name;
  void (*init)(void);
  void (*update)(void);
  void (*draw)(void);
} Animation_s;

/*
* This middleware is aimed at driving the neopixel strips I got from Paul Schwenteck. Thanks man.
* These are panels with 2 rows and 8 columns of neopixels. Looks like the following:
*
*                       x,y=1,1 | idx = 8
*                               v
*       5V pin    o o o o o o o o  GND pin
*       DIN pin   o o o o o o o o  GND pin
*                 ^             ^ 
*         x,y=0,0 |             | actual strip begins here. idx = 0
*         idx = 7
*/

bool Neopixel_Init(void);
void Neopixel_DisplayStrip(void);
void Neopixel_SetPixelRgb(Pixel_t *p, uint8_t r, uint8_t g, uint8_t b);
void Neopixel_SetPixelHsv(Pixel_t *p, float h, float s, float v);
void Neopixel_Clear(void);
Pixel_t * Neopixel_GetPixelByIdx(uint8_t idx);
Pixel_t * Neopixel_GetPixelByLineIdx(uint8_t idx);
Pixel_t * Neopixel_GetPixelByCoord(uint8_t x, uint8_t y);
void Neopixel_PrintPixel(Pixel_t *p);
bool Neopixel_PixelIsBlank(Pixel_t *p);
bool Neopixel_ShouldRedraw(void);
void Neopixel_SetAnimation(uint8_t animIdx);
void Neopixel_NextAnimation(void);
void Neopixel_IncrementAllByHSV(float h, float s, float v);

// Shell commands
int cmd_setpixel(int argc, char **argv);
int cmd_clearpixel(int argc, char **argv);
int cmd_nextanimation(int argc, char **argv);
int cmd_setanimation(int argc, char **argv);
