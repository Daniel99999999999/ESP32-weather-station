
#include "settings.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif

const WiFiProfile WIFI_PROFILES[] = {
  {
    WIFI_PROFILE_1_SSID,
    WIFI_PROFILE_1_PASSWORD,
    true,
    IPAddress(192, 168, 1, 200),
    IPAddress(192, 168, 1, 1),
    IPAddress(255, 255, 255, 0),
    IPAddress(193, 47, 158, 254)
  },
  {
    WIFI_PROFILE_2_SSID,
    WIFI_PROFILE_2_PASSWORD,
    true,
    IPAddress(192, 168, 0, 200),
    IPAddress(192, 168, 0, 1),
    IPAddress(255, 255, 255, 0),
    IPAddress(80, 69, 100, 174)
  }
};

const uint8_t WIFI_PROFILE_COUNT =
  sizeof(WIFI_PROFILES) / sizeof(WIFI_PROFILES[0]);

const char *HOSTNAME = "weather-station";

bool IS_METRIC = true;

const String OPEN_WEATHER_MAP_API_KEY = OPEN_WEATHER_MAP_API_KEY_VALUE;

const char *CITY_FALLBACK_NAMES[CITY_COUNT] = {"Kielce", "Rauen", "Tarnowitz"};
const char *CITY_OWM_LOCATIONS[CITY_COUNT] = {
  "id=769250",
  "id=6547646",
  "id=3083440"
};
const char *AQI_STATION_IDS[CITY_COUNT] = {"A463567", "A52348", "A190096"};
const String AQI_TOKEN = AQI_TOKEN_VALUE;

const String OPEN_WEATHER_MAP_LANGUAGE = "pl";

const String WEEKDAYS[7] = {
  u8"Niedziela", u8"Poniedziałek", u8"Wtorek", u8"Środa", u8"Czwartek", u8"Piątek", u8"Sobota"
};

const String WEEKDAYS_ABBR[7] = {
  "NIE", "PON", "WT", u8"ŚR", "CZW", "PT", "SO"
};
