#ifndef RADIATION_SERVICE_H
#define RADIATION_SERVICE_H

#pragma once

#include <Arduino.h>
#include "app_types.h"

// Pobiera radiation dla wskazanego miasta i aktualizuje wspólny stan źródła/statusu.
void updateRadiationForCityIndex(uint8_t cityIndex);
// Zwraca aktualny status radiation używany przez UI.
RadiationStatus getRadiationStatus(void);
// Zwraca, czy aktywne źródło jest primary, backup czy brak danych.
RadiationSourceType getRadiationSourceType(void);
// Informuje, czy system aktualnie działa na źródle zapasowym.
bool isRadiationUsingBackupSource(void);
// Zwraca trend dawki: -1, 0 lub +1.
int8_t getRadiationTrend(void);
// Zwraca kolor reprezentujący poziom dawki i świeżość danych.
uint16_t getRadiationColor(void);
// Zwraca symbol trendu do ewentualnej prezentacji tekstowej.
const char* getTrendSymbol(void);
// Zwraca kolor dla świeżego wyniku PAA.
uint16_t getPaaColor(void);
// Zwraca kolor dla świeżego wyniku BfS.
uint16_t getBfsColor(void);
// Zwraca ostatni zapisany komunikat błędu diagnostycznego.
const char* getRadiationLastError(void);

#endif
