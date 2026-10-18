#pragma once

#include <Arduino.h>

#include "app_types.h"

// Znormalizowany snapshot danych jakości powietrza używany przez UI.
typedef struct {
  int aqi;
  int pm25;
  int pm10;
} AqiServiceData;

// Inicjalizuje stan serwisu AQI dla wszystkich miast.
void aqiServiceInit(void);
// Ustawia aktywne miasto dla getterów bez sufiksu ForCity().
void aqiServiceSetCity(uint8_t cityIndex);
// Wykonuje pojedynczy krok harmonogramu pobierania AQI.
void aqiServiceUpdate(uint32_t nowMs);

// Zwraca stan serwisu dla aktywnego miasta.
DataFetchState aqiServiceGetState(void);
// Zwraca stan serwisu dla wskazanego miasta.
DataFetchState aqiServiceGetStateForCity(uint8_t cityIndex);
// Zwraca krótki opis tekstowy stanu aktywnego miasta.
const char* aqiServiceGetStateLabel(void);
// Informuje, czy aktywne miasto ma poprawne dane AQI.
bool aqiServiceHasData(void);
// Informuje, czy wskazane miasto ma poprawne dane AQI.
bool aqiServiceHasDataForCity(uint8_t cityIndex);
// Zwraca indeks aktualnie wybranego miasta.
uint8_t aqiServiceGetCityIndex(void);
// Zwraca nazwę aktywnego miasta z konfiguracji.
const char* aqiServiceGetCityName(void);
// Zwraca wiek danych aktywnego miasta w sekundach względem nowMs.
uint32_t aqiServiceGetAgeSeconds(uint32_t nowMs);
// Zwraca wiek danych wskazanego miasta w sekundach względem nowMs.
uint32_t aqiServiceGetAgeSecondsForCity(uint8_t cityIndex, uint32_t nowMs);
// Zwraca wskaźnik do snapshotu AQI aktywnego miasta albo nullptr.
const AqiServiceData* aqiServiceGetData(void);
// Zwraca wskaźnik do snapshotu AQI wskazanego miasta albo nullptr.
const AqiServiceData* aqiServiceGetDataForCity(uint8_t cityIndex);
