#pragma once

#include <Arduino.h>
#include <OpenWeatherMapForecast.h>

#include "app_types.h"

// Inicjalizuje stan serwisu forecastu dla wszystkich miast.
void forecastServiceInit(void);

// Ustawia aktywne miasto dla getterów bez sufiksu ForCity().
void forecastServiceSetCity(uint8_t cityIndex);

// Wykonuje pojedynczy krok harmonogramu pobierania forecastu.
void forecastServiceUpdate(uint32_t nowMs);

// Zwraca stan serwisu dla aktywnego miasta.
DataFetchState forecastServiceGetState(void);

// Zwraca stan serwisu dla wskazanego miasta.
DataFetchState forecastServiceGetStateForCity(uint8_t cityIndex);

// Zwraca krótki opis tekstowy stanu aktywnego miasta.
const char* forecastServiceGetStateLabel(void);

// Zwraca wskaźnik do pierwszego elementu bufora forecastu aktywnego miasta albo nullptr.
const OpenWeatherMapForecastData* forecastServiceGetData(void);

// Zwraca wskaźnik do pierwszego elementu bufora forecastu wskazanego miasta albo nullptr.
const OpenWeatherMapForecastData* forecastServiceGetDataForCity(uint8_t cityIndex);

// Zwraca liczbę poprawnych rekordów forecastu dla aktywnego miasta.
uint8_t forecastServiceGetCount(void);

// Zwraca liczbę poprawnych rekordów forecastu dla wskazanego miasta.
uint8_t forecastServiceGetCountForCity(uint8_t cityIndex);

// Zwraca indeks aktualnie wybranego miasta.
uint8_t forecastServiceGetCityIndex(void);

// Zwraca nazwę aktywnego miasta z konfiguracji.
const char* forecastServiceGetCityName(void);

// Informuje, czy aktywne miasto ma poprawne dane forecastu.
bool forecastServiceHasData(void);

// Informuje, czy wskazane miasto ma poprawne dane forecastu.
bool forecastServiceHasDataForCity(uint8_t cityIndex);

// Zwraca wiek danych aktywnego miasta w sekundach względem nowMs.
uint32_t forecastServiceGetAgeSeconds(uint32_t nowMs);

// Zwraca wiek danych wskazanego miasta w sekundach względem nowMs.
uint32_t forecastServiceGetAgeSecondsForCity(uint8_t cityIndex, uint32_t nowMs);
