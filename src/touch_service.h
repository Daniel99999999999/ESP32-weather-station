#ifndef touch_service_H
#define touch_service_H


#pragma once

#include <Arduino.h>

// Resetuje stan logiki wake-on-touch.
void touchServiceInit(void);
// Zwraca true, jeśli dotyk może zostać potraktowany jako normalna akcja UI.
// Gdy ekran jest mocno przygaszony, pierwszy dotyk tylko rozjaśnia LCD.
bool touchServiceAllowPageSwitch(uint8_t currentLcdPwm, uint32_t nowMs);
// Wygasza tymczasowe rozjaśnienie po timeoutcie i przywraca jasność ambientową.
void touchServiceTimeout(uint32_t nowMs, uint8_t ambientTargetLcdPwm);
// Informuje, czy trwa okno "wybudzenia" po pierwszym dotyku.
bool touchServiceWakeWindowActive(void);

#endif
