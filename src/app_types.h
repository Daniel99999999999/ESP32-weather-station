#ifndef APP_TYPES_H
#define APP_TYPES_H


#pragma once

#include <Arduino.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>

#include "app_config.h"

typedef struct RectangleDef {
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
} RectangleDef;

typedef struct DayForecast {
  float minTemp;
  float maxTemp;
  int conditionCode;
  int conditionHour;
  int day;
} DayForecast;

typedef struct {
  const char *name;
  const char *owmLocationId;
  const char *aqiStationId;

  OpenWeatherMapCurrentData current;
  OpenWeatherMapForecastData forecast[NUMBER_OF_FORECASTS];

  float tempHistory[TEMP_HISTORY_SIZE];
  uint8_t historyHead;
  uint8_t historyCount;

  int aqi;
  int pm25;
  int pm10;
  bool aqiValid;
} CityData;

typedef enum {
  PAGE_MAIN = 0,
  PAGE_STATS = 1,
  PAGE_DEBUG = 2,
  PAGE_OTA_SERVICE = 3,
  PAGE_MENU = 4
} AppPage;

enum NetworkJobType : uint8_t {
  NETJOB_NONE = 0,
  NETJOB_CURRENT,
  NETJOB_FORECAST,
  NETJOB_AQI,
  NETJOB_RADIATION
};

enum RadiationStatus {
  RAD_STATUS_OK,
  RAD_STATUS_WARN,
  RAD_STATUS_ALARM,
  RAD_STATUS_NO_DATA
};

enum RadiationSourceType : uint8_t {
  RAD_SOURCE_NONE = 0,
  RAD_SOURCE_PRIMARY,
  RAD_SOURCE_BACKUP
};

enum DataFetchState : uint8_t {
  FETCH_NONE = 0,
  FETCH_RUNNING,
  FETCH_OK,
  FETCH_STALE,
  FETCH_ERROR,
  FETCH_BACKUP
};

#endif
