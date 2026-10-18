#pragma once

#include <Arduino.h>

#include "app_types.h"

// Snapshot radiation przygotowany bezpośrednio do użycia przez UI.
typedef struct {
  char display[24];
  char dateEnd[32];
  RadiationStatus status;
  RadiationSourceType sourceType;
  int8_t trend;
  uint16_t color;
} RadiationJobData;

// Inicjalizuje job radiation i ustawia opóźniony pierwszy start.
void radiationJobServiceInit(void);
// Ustawia aktywne miasto dla automatycznych odczytów radiation.
void radiationJobServiceSetCity(uint8_t cityIndex);
// Wykonuje pojedynczy krok harmonogramu automatycznych odczytów radiation.
void radiationJobServiceUpdate(uint32_t nowMs);
// Ręcznie uruchamia odczyt radiation dla miasta; używane m.in. przez debug.
bool radiationJobServiceRunManual(uint8_t cityIndex);

// Zwraca stan joba radiation.
DataFetchState radiationJobServiceGetState(void);
// Zwraca krótki opis tekstowy stanu joba.
const char* radiationJobServiceGetStateLabel(void);
// Informuje, czy jest gotowy poprawny snapshot radiation do wyświetlenia.
bool radiationJobServiceHasData(void);
// Zwraca indeks miasta, dla którego trzymany jest aktualny snapshot.
uint8_t radiationJobServiceGetCityIndex(void);
// Zwraca wiek ostatniego snapshotu radiation w sekundach.
uint32_t radiationJobServiceGetAgeSeconds(uint32_t nowMs);
// Zwraca wskaźnik do snapshotu radiation albo nullptr.
const RadiationJobData* radiationJobServiceGetData(void);
// Zwraca tekstową wartość dawki dla wskazanego miasta/sensoru.
const char* radiationJobServiceGetDisplayForCity(uint8_t cityIndex);
// Zwraca tekst końca próbki dla wskazanego miasta/sensoru.
const char* radiationJobServiceGetDateEndForCity(uint8_t cityIndex);
