#pragma once

#include <Arduino.h>

#include "app_types.h"

// Znormalizowany snapshot pogody bieżącej używany przez warstwę UI.
typedef struct {
  bool hasData;
  uint8_t cityIndex;
  char cityName[32];
  char description[48];
  char icon[8];
  uint16_t weatherId;
  float temperature;
  float feelsLike;
  float tempMin;
  float tempMax;
  uint32_t observationTime;
  uint8_t humidity;
  uint16_t pressure;
  float windSpeed;
  uint32_t sunrise;
  uint32_t sunset;
  uint32_t ageSeconds;
  DataFetchState fetchState;
} CurrentWeatherViewState;

// Znormalizowany snapshot pojedynczego dnia forecastu.
typedef struct {
  bool valid;
  uint8_t weekday;
  float minTemp;
  float maxTemp;
  uint16_t weatherId;
} ForecastDayViewState;

// Znormalizowany snapshot forecastu zredukowany do danych potrzebnych UI.
typedef struct {
  bool hasData;
  uint8_t cityIndex;
  uint8_t itemCount;
  uint32_t ageSeconds;
  DataFetchState fetchState;
  ForecastDayViewState days[NUMBER_OF_DAY_FORECASTS];
} ForecastViewState;

// Znormalizowany snapshot AQI używany przez UI.
typedef struct {
  bool hasData;
  uint8_t cityIndex;
  int aqi;
  int pm25;
  int pm10;
  uint32_t ageSeconds;
  DataFetchState fetchState;
} AqiViewState;

// Znormalizowany snapshot radiation przygotowany do bezpośredniego renderowania.
typedef struct {
  bool hasData;
  uint8_t cityIndex;
  char display[24];
  char dateEnd[32];
  uint32_t ageSeconds;
  DataFetchState fetchState;
  RadiationStatus status;
  RadiationSourceType sourceType;
  int8_t trend;
  uint16_t color;
} RadiationViewState;

// Agregat wszystkich danych widoku dostępny dla warstwy UI.
typedef struct {
  CurrentWeatherViewState currentWeather;
  ForecastViewState forecast;
  AqiViewState aqi;
  RadiationViewState radiation;
} AppState;

// Resetuje cały znormalizowany stan aplikacji.
void appStateInit(void);
// Składa nowy snapshot AppState z danych udostępnianych przez serwisy.
void appStateUpdate(uint32_t nowMs);
// Zwraca wskaźnik do aktualnego agregatu AppState.
const AppState* getAppState(void);
