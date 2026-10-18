#ifndef ambient_service_H
#define ambient_service_H

#pragma once

#include <Arduino.h>

// Resetuje filtr i stan serwisu światła otoczenia.
void ambientServiceInit(void);

// Aktualizuje filtr oświetlenia, tryb nocny i wyjścia PWM.
// ambientFiltered: zwraca przefiltrowany odczyt LDR.
// isNightMode: zwraca uproszczoną flagę trybu nocnego.
// wakeWindowActive: gdy true, serwis nie nadpisuje chwilowego boostu LCD.
void ambientServiceUpdate(uint16_t &ambientFiltered, bool &isNightMode, bool wakeWindowActive);

#endif
