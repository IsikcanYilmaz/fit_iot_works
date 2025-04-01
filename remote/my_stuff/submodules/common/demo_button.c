#include "demo_button.h"
#include "thread.h"
#include "ztimer.h"
#include "board.h"
#include <periph/gpio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

gpio_t buttonGpios[NUM_BUTTONS] = {
  [BUTTON_RED] = GPIO_PIN(1,15),
  [BUTTON_GREEN] = GPIO_PIN(1,14),
  [BUTTON_BLUE] = GPIO_PIN(1,14),
  [BUTTON_SHIFT] = GPIO_PIN(1,12)
};

char Button_ThreadStack[THREAD_STACKSIZE_DEFAULT];

kernel_pid_t buttonThreadId; // TODO decide if you want this to be here

/////////////////////////////////////////

static void ButtonIrq(void *arg)
{
  printf("BUTTON IRQ\n");
}

/////////////////////////////////////////

void Button_Init(void)
{
  for (int i = 0; i < NUM_BUTTONS; i++)
  {
    gpio_init_int(buttonGpios[i], GPIO_IN_PD, GPIO_RISING, ButtonIrq, NULL);
    gpio_irq_enable(buttonGpios[i]);
  }
}

void Button_Deinit(void)
{
  for (int i = 0; i < NUM_BUTTONS; i++)
  {
    gpio_irq_disable(buttonGpios[i]);
  }
}


