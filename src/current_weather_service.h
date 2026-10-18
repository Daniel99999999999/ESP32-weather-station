#pragma once

#include <Arduino.h>
#include <OpenWeatherMapCurrent.h>

#include "app_types.h"

// Inicjalizuje stan serwisu pogody bieżącej dla wszystkich miast.
void currentWeatherServiceInit(void);

// Ustawia miasto aktywne dla getterów bez sufiksu ForCity().
// cityIndex: indeks miasta z zakresu [0, CITY_COUNT).
void currentWeatherServiceSetCity(uint8_t cityIndex);

// Wykonuje pojedynczy krok harmonogramu pobierania danych.
// nowMs: aktualny czas z millis().
void currentWeatherServiceUpdate(uint32_t nowMs);

// Zwraca stan pobierania dla aktualnie wybranego miasta.
DataFetchState currentWeatherServiceGetState(void);

// Zwraca stan pobierania dla wskazanego miasta.
DataFetchState currentWeatherServiceGetStateForCity(uint8_t cityIndex);

// Zwraca krótki tekstowy opis stanu aktywnego miasta.
const char* currentWeatherServiceGetStateLabel(void);

// Zwraca wskaźnik do ostatnich poprawnych danych aktywnego miasta albo nullptr.
const OpenWeatherMapCurrentData* currentWeatherServiceGetData(void);

// Zwraca wskaźnik do ostatnich poprawnych danych wskazanego miasta albo nullptr.
const OpenWeatherMapCurrentData* currentWeatherServiceGetDataForCity(uint8_t cityIndex);

// Zwraca nazwę aktywnego miasta z serwera albo nazwę konfiguracyjną jako fallback.
const char* currentWeatherServiceGetCityName(void);

// Zwraca nazwę miasta z pola "name" JSON albo nazwę awaryjną.
const char* currentWeatherServiceGetCityNameForCity(uint8_t cityIndex);

// Zwraca indeks aktualnie wybranego miasta.
uint8_t currentWeatherServiceGetCityIndex(void);

// Informuje, czy aktywne miasto ma poprawny snapshot danych.
bool currentWeatherServiceHasData(void);

// Informuje, czy wskazane miasto ma poprawny snapshot danych.
bool currentWeatherServiceHasDataForCity(uint8_t cityIndex);

// Zwraca wiek danych aktywnego miasta w sekundach względem nowMs.
uint32_t currentWeatherServiceGetAgeSeconds(uint32_t nowMs);

// Zwraca wiek danych wskazanego miasta w sekundach względem nowMs.
uint32_t currentWeatherServiceGetAgeSecondsForCity(uint8_t cityIndex, uint32_t nowMs);

// Zwraca znacznik millis() ostatniego poprawnego odświeżenia dla miasta.
uint32_t currentWeatherServiceGetLastSuccessMsForCity(uint8_t cityIndex);
