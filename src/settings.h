#pragma once

#include <Arduino.h>
#include <WiFi.h>

// ****************************************************************************
// User settings
// ****************************************************************************
// extern const char *SSID;
extern const char *HOSTNAME;
// extern const char *WIFI_PWD;

// Central European Time / Central European Summer Time
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"

// // Static network configuration
// #define WIFI_USE_STATIC_IP true
// extern const IPAddress WIFI_LOCAL_IP;
// extern const IPAddress WIFI_GATEWAY;
// extern const IPAddress WIFI_SUBNET;
// extern const IPAddress WIFI_DNS;

struct WiFiProfile {
  const char* ssid;
  const char* password;
  bool useStaticIp;
  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
};

extern const WiFiProfile WIFI_PROFILES[];
extern const uint8_t WIFI_PROFILE_COUNT;

#define UPDATE_INTERVAL_MINUTES 10

// uncomment to get "08/23/2022 02:55:02 pm" instead of "23.08.2022 14:55:02"
// #define DATE_TIME_FORMAT_US

extern bool IS_METRIC;

// OpenWeatherMap Settings
extern const String OPEN_WEATHER_MAP_API_KEY;

#define CITY_COUNT 3
// Nazwy awaryjne używane przed pierwszym poprawnym odczytem OpenWeather.
extern const char *CITY_FALLBACK_NAMES[CITY_COUNT];
// Parametr lokalizacji OpenWeatherMap, np. "id=769250" albo "zip=15518,DE".
extern const char *CITY_OWM_LOCATIONS[CITY_COUNT];
extern const char *AQI_STATION_IDS[CITY_COUNT];
extern const String AQI_TOKEN;

// Supported languages
extern const String OPEN_WEATHER_MAP_LANGUAGE;

extern const String WEEKDAYS[7];
extern const String WEEKDAYS_ABBR[7];

// ****************************************************************************
// System settings
// ****************************************************************************

// 2: portrait, on/off switch right side -> 0/0 top left
// 3: landscape, on/off switch at the top -> 0/0 top left
#define TFT_ROTATION 0

#define TOUCH_ROTATION ROTATION_INVERTED
#define TOUCH_SENSITIVITY 40
#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define TOUCH_INT 21
#define TOUCH_RST 25

#define TFT_LED_BRIGHTNESS 200
#define TFT_TP_BLUE 0x0336

#ifdef DATE_TIME_FORMAT_US
  #define UI_DATE_FORMAT "%m/%d/%Y"
  #define UI_TIME_FORMAT "%I:%M:%S %P"
  #define UI_TIME_FORMAT_NO_SECONDS "%I:%M %P"
  #define UI_TIMESTAMP_FORMAT (UI_DATE_FORMAT + " " + UI_TIME_FORMAT)
#else
  #define UI_DATE_FORMAT "%d.%m.%Y"
  #define UI_TIME_FORMAT "%H:%M:%S"
  #define UI_TIME_FORMAT_NO_SECONDS "%H:%M"
  #define UI_TIMESTAMP_FORMAT (UI_DATE_FORMAT + " " + UI_TIME_FORMAT)
#endif

#define SYSTEM_TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"

#define APP_NAME "ESP32 Stacja Pogodowa"
#define VERSION "2.1.1"
