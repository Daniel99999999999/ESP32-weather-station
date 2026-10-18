#include <Arduino.h>
#include "ambient.h"

// Ostatnie ustawione poziomy PWM są trzymane lokalnie, aby inne moduły mogły
// odpytywać stan bez czytania rejestrów sprzętowych.
static uint8_t g_lcdBrightness = LCD_BRIGHT_DAY;
static uint8_t g_blueBrightness = 0;

void setupPwmOutputs(void) {
  ledcSetup(PWM_CH_LCD, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(PIN_LCD_BL, PWM_CH_LCD);

  ledcSetup(PWM_CH_RED, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(PIN_RGB_R, PWM_CH_RED);

  ledcSetup(PWM_CH_GREEN, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(PIN_RGB_G, PWM_CH_GREEN);

  ledcSetup(PWM_CH_BLUE, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(PIN_RGB_B, PWM_CH_BLUE);
}

void setLcdBrightness(uint8_t value) {
  g_lcdBrightness = value;
  ledcWrite(PWM_CH_LCD, value);
}

int getCurrentLcdPwm(void) {
  return (int)g_lcdBrightness;
}

void setBlueLedBrightness(uint8_t value) {
  g_blueBrightness = value;
  setRgbCommonAnode(0, 0, value);
}

// Dla wspólnej anody niski poziom PWM oznacza jaśniejsze świecenie.
void setRgbCommonAnode(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(PWM_CH_RED,   255 - r);
  ledcWrite(PWM_CH_GREEN, 255 - g);
  ledcWrite(PWM_CH_BLUE,  255 - b);
}

uint16_t readAmbientRaw(void) {
  return analogRead(PIN_LDR);
}

void setupAmbientSensor(void) {
  pinMode(PIN_LDR, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);
}

uint8_t getLcdBrightness(void) {
  return g_lcdBrightness;
}

uint8_t getBlueLedBrightness(void) {
  return g_blueBrightness;
}

// Zachowane jako referencja starej logiki status LED dla radiation.
// void updateRadiationLed(uint8_t rstatus) {
//   switch (rstatus) {
//     case RAD_STATUS_OK:
//       setRgbCommonAnode(0, 50, 0); // zielony
//       break;

//     case RAD_STATUS_WARN:
//       setRgbCommonAnode(50, 50, 0); // żółty
//       break;

//     case RAD_STATUS_ALARM:
//       setRgbCommonAnode(255, 0, 0); // czerwony
//       break;

//     default:
//       setRgbCommonAnode(0, 0, 20); // niebieski = brak danych
//   }
// }


uint8_t getLedBrightnessForAmbient(uint16_t ldr) {
  if (ldr <= 200) return LED_BRIGHT_DAY;
  if (ldr <= 500) return LED_BRIGHT_MID;
  return LED_BRIGHT_NIGHT;
}

// Proste progi jasności dobrane pod ten konkretny czujnik i obudowę.
uint8_t getLcdBrightnessForAmbient(uint16_t ldr) {
  if (ldr <= 200) return LCD_BRIGHT_DAY;   // 100%
  if (ldr <= 500) return LCD_BRIGHT_MID;   // ~50%
  return LCD_BRIGHT_NIGHT;                    // ~5%
}
