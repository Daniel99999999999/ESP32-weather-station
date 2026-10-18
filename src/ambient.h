#ifndef AMBIENT_H
#define AMBIENT_H

#include "Arduino.h"

// Piny sterujące podświetleniem LCD, LED RGB oraz wejściem fotorezystora.
#define PIN_LCD_BL      27
#define PIN_RGB_R       4
#define PIN_RGB_G       16
#define PIN_RGB_B       17
#define PIN_LDR         34

// Wspólne parametry PWM dla podświetlenia i diody RGB.
#define PWM_FREQ        5000
#define PWM_RES_BITS    8

// Kanały PWM przypisane do konkretnych wyjść.
#define PWM_CH_LCD      0
#define PWM_CH_RED      1
#define PWM_CH_GREEN    2
#define PWM_CH_BLUE     3

// Docelowe poziomy jasności LCD dla trybu dziennego, pośredniego i nocnego.
#define LCD_BRIGHT_DAY  255
#define LCD_BRIGHT_MID  128
#define LCD_BRIGHT_NIGHT 13     // 26 =ok. 10%, 13 = 5%
#define LCD_BRIGHT_ONE_TOUCH  100

// Docelowe poziomy jasności niebieskiej LED statusowej.
#define LED_BRIGHT_DAY  128
#define LED_BRIGHT_MID  30
#define LED_BRIGHT_NIGHT 5     // 26 =ok. 10%, 13 = 5%

// Konfiguruje wszystkie kanały PWM używane przez LCD i diodę RGB.
void setupPwmOutputs(void);

// Ustawia jasność podświetlenia LCD w skali PWM 0..255.
void setLcdBrightness(uint8_t value);

// Ustawia poziom niebieskiej składowej LED statusowej.
void setBlueLedBrightness(uint8_t value);

// Ustawia kolor wspólnoanodowej diody RGB.
// r/g/b: intensywności 0..255.
void setRgbCommonAnode(uint8_t r, uint8_t g, uint8_t b);

// Odczytuje surową wartość z fotorezystora.
uint16_t readAmbientRaw(void);

// Konfiguruje wejście analogowe czujnika oświetlenia.
void setupAmbientSensor(void);

// Zwraca ostatnio ustawioną jasność LCD.
uint8_t getLcdBrightness(void);

// Zwraca ostatnio ustawioną jasność niebieskiej LED.
uint8_t getBlueLedBrightness(void);

// Zwraca bieżący poziom PWM podświetlenia LCD jako int.
int getCurrentLcdPwm(void);
// void updateRadiationLed(uint8_t rstatus);

// Mapuje surowy odczyt LDR na jasność LED.
uint8_t getLedBrightnessForAmbient(uint16_t ldr);

// Mapuje surowy odczyt LDR na jasność LCD.
uint8_t getLcdBrightnessForAmbient(uint16_t ldr);

#endif
