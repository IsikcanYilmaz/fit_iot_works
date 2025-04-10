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

ws281x_t dev;

bool Neopixel_Init(void)
{
  // Initialize ws281x module
  int retval;
  if ((retval = ws281x_init(&dev, &ws281x_params[0])) != 0) {
    printf("Initialization failed with error code %d\n", retval);
    return false;
  }

  // Should the thread be initialized here? should there be a thread?
}
