#include "demo_button.h"
#include "thread.h"
#include "ztimer.h"
#include "board.h"
#include <periph/gpio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

gpio_t buttonGpios[NUM_BUTTONS] = {
  [BUTTON_RED] = GPIO_PIN(1,12),
  [BUTTON_GREEN] = GPIO_PIN(1,13),
  [BUTTON_BLUE] = GPIO_PIN(1,14),
  [BUTTON_SHIFT] = GPIO_PIN(1,15)
};

char buttonThreadStack[THREAD_STACKSIZE_DEFAULT];

kernel_pid_t buttonThreadId; // TODO decide if you want this to be here

bool buttonStates[NUM_BUTTONS];
bool buttonStates[NUM_BUTTONS];

bool buttonTriggered = false;

/////////////////////////////////////////

static void Button_Read(void)
{
  for (int i = 0; i < NUM_BUTTONS; i++)
  {
    printf("%d ", gpio_read(buttonGpios[i]));
  }
  printf("\n");
}

static void Button_IrqHandler(void *arg)
{
  buttonTriggered = true;
}

/////////////////////////////////////////

static void *Button_ThreadHandler(void *arg)
{
  if (!buttonTriggered)
  {
    return;
  }
  Button_Read();
}

/////////////////////////////////////////

void Button_Init(void)
{
  for (int i = 0; i < NUM_BUTTONS; i++)
  {
    buttonStates[i] = false;
    gpio_init_int(buttonGpios[i], GPIO_IN_PD, GPIO_BOTH, Button_IrqHandler, NULL);
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


