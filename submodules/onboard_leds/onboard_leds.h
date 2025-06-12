
typedef enum { // TODO mv to header
  ONBOARD_RED,
  ONBOARD_GREEN,
  ONBOARD_BLUE,
  NUM_ONBOARD_LEDS
} OnboardLedID_e;

void OnboardLeds_Init(void);
void OnboardLeds_Blink(OnboardLedID_e led, uint16_t ms);
