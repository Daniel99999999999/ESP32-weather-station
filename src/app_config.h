#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#pragma once

#include <Arduino.h>


#define ENABLE_VERBOSE_LOG 0

#define FW_ENABLE_INFO_LOGS 0

#if ENABLE_VERBOSE_LOG
  #define DBG_I(...) log_i(__VA_ARGS__)
#else
  #define DBG_I(...)
#endif

#if FW_ENABLE_INFO_LOGS
  #define FW_LOG_I(...) log_i(__VA_ARGS__)
  #define FW_LOG_D(...) log_d(__VA_ARGS__)
#else
  #define FW_LOG_I(...)
  #define FW_LOG_D(...)
#endif

// --- WiFi Settings ---
#define WIFI_CONNECT_TIMEOUT_MS        3500UL
#define WIFI_DHCP_FALLBACK_TIMEOUT_MS  5000UL
#define WIFI_ENABLE_DHCP_FALLBACK      1
#define WIFI_RETRY_DELAY_MS            500UL

// --- Forecast ---
static const uint8_t forecastHoursUtc[] = {0, 3, 6, 9, 12, 15, 18, 21};

#define NUMBER_OF_FORECASTS 40
#define NUMBER_OF_DAY_FORECASTS 4

// --- History ---
#define TEMP_HISTORY_SIZE 24
#define TEMP_HISTORY_INTERVAL_MS (60UL * 60UL * 1000UL)

// --- Refresh policy ---
#define REFRESH_WEATHER_MIN     20
#define REFRESH_AQI_MIN         20
#define REFRESH_RADIATION_MIN   30
#define RADIATION_START_DELAY_MS        45000UL
#define RADIATION_CITY_SWITCH_DELAY_MS   5000UL

// --- PAA optimization ---
#define PAA_LOOKBACK_HOURS      6
#define PAA_HTTP_TIMEOUT_MS     2500
#define BFS_HTTP_TIMEOUT_MS     4000
#define PAA_DEBUG_SNIPPET_LEN   160

#define RADIATION_MAX_FRESH_AGE_SEC (90UL * 60UL)
#endif
