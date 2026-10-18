
#ifndef UTIL_H
#define UTIL_H



#pragma once
#include <Arduino.h>
#include <time.h>
#include <OpenWeatherMapForecast.h>
#include "settings.h"
#include "app_config.h"
#include "app_types.h"

extern char timestampBuffer[64];

DayForecast* calculateDayForecasts(OpenWeatherMapForecastData *forecasts);
uint8_t getCurrentWeekday();
String getCurrentTimestamp(const char *format);
bool hasValidTime();
bool initTime();
void logBanner();
void logMemoryStats();
void setTimezone(const char *timezone);
int days_from_epoch(int year, int month, int day);
time_t parseIso8601ToEpoch(const String &ts);

#endif
