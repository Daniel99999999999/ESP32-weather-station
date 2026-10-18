#include "aqi_service.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app_config.h"
#include "current_weather_service.h"
#include "settings.h"

// Cache AQI jest trzymany per miasto, aby przełączanie ekranów nie wymagało
// natychmiastowego fetchu nowego źródła danych.
static AqiServiceData s_data[CITY_COUNT] = {};
static DataFetchState s_state[CITY_COUNT] = {};
static uint8_t s_selectedCityIndex = 0;
static uint8_t s_roundRobinCityIndex = 0;
static bool s_hasData[CITY_COUNT] = {};
static uint32_t s_lastSuccessMs[CITY_COUNT] = {};
static uint32_t s_nextAttemptMs[CITY_COUNT] = {};

// Harmonogram odświeżania i retry dla AQI.
static const uint32_t AQI_REFRESH_MS = REFRESH_AQI_MIN * 60UL * 1000UL;
static const uint32_t AQI_RETRY_MS = 60UL * 1000UL;
static const uint32_t AQI_STALE_MS = AQI_REFRESH_MS * 2UL;
static const uint32_t AQI_HTTP_TIMEOUT_MS = 5000UL;

static bool parseJsonStringField(const String &json, const char *token, String &outValue) {
  int pos = json.indexOf(token);
  if (pos < 0) return false;
  pos += strlen(token);

  int end = json.indexOf('\"', pos);
  if (end < 0) return false;

  outValue = json.substring(pos, end);
  return true;
}

static bool parseJsonIntField(const String &json, const char *token, int &outValue) {
  int pos = json.indexOf(token);
  if (pos < 0) return false;
  pos += strlen(token);

  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\"')) pos++;

  int end = pos;
  bool seen = false;
  while (end < (int)json.length()) {
    char c = json[end];
    if ((c >= '0' && c <= '9') || c == '-') {
      seen = true;
      end++;
      continue;
    }
    break;
  }

  if (!seen) return false;
  outValue = json.substring(pos, end).toInt();
  return true;
}

static bool parseJsonFloatField(const String &json, const char *token, float &outValue) {
  int pos = json.indexOf(token);
  if (pos < 0) return false;
  pos += strlen(token);

  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\"')) pos++;

  int end = pos;
  bool seen = false;
  while (end < (int)json.length()) {
    char c = json[end];
    if ((c >= '0' && c <= '9') || c == '-' || c == '.') {
      seen = true;
      end++;
      continue;
    }
    break;
  }

  if (!seen) return false;
  outValue = json.substring(pos, end).toFloat();
  return true;
}

// Pobiera AQI z konkretnej ścieżki WAQI; może to być id stacji albo nazwa miasta.
static bool fetchAqiFromPath(AqiServiceData &outData, const String &path) {
  const String url = String("https://api.waqi.info/feed/") + path + "/?token=" + AQI_TOKEN;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    log_e("AQI HTTP begin failed for path %s", path.c_str());
    return false;
  }

  http.setTimeout(AQI_HTTP_TIMEOUT_MS);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    log_e("AQI HTTP error for path %s: %d", path.c_str(), code);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  String status;
  if (!parseJsonStringField(payload, "\"status\":\"", status) || status != "ok") {
    return false;
  }

  outData = {-1, -1, -1};
  bool ok = parseJsonIntField(payload, "\"aqi\":", outData.aqi);

  float pm25f = -1.0f;
  float pm10f = -1.0f;
  parseJsonFloatField(payload, "\"pm25\":{\"v\":", pm25f);
  parseJsonFloatField(payload, "\"pm10\":{\"v\":", pm10f);
  int pm25 = (pm25f >= 0.0f) ? (int)(pm25f + 0.5f) : -1;
  int pm10 = (pm10f >= 0.0f) ? (int)(pm10f + 0.5f) : -1;
  outData.pm25 = pm25;
  outData.pm10 = pm10;

  return ok;
}

// Próbuje pobrać AQI najpierw po station id, a potem po nazwie miasta jako fallback.
static bool fetchAqi(AqiServiceData &outData, uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return false;
  }

  const String stationId = AQI_STATION_IDS[cityIndex];
  if (fetchAqiFromPath(outData, stationId)) {
    return true;
  }

  String cityName = currentWeatherServiceGetCityNameForCity(cityIndex);
  cityName.replace(" ", "%20");
  if (fetchAqiFromPath(outData, cityName)) {
    return true;
  }

  return false;
}

void aqiServiceInit(void) {
  s_selectedCityIndex = 0;
  s_roundRobinCityIndex = 0;
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    s_data[city] = {-1, -1, -1};
    s_state[city] = FETCH_NONE;
    s_hasData[city] = false;
    s_lastSuccessMs[city] = 0;
    s_nextAttemptMs[city] = 0;
  }
}

// Getter bez sufiksu ForCity() ma odwoływać się do tego miasta.
void aqiServiceSetCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return;
  }
  s_selectedCityIndex = cityIndex;
}

// Serwis aktualizuje miasta round-robin, żeby oba cache AQI żyły równolegle w tle.
void aqiServiceUpdate(uint32_t nowMs) {
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    if (s_hasData[city] &&
        (nowMs - s_lastSuccessMs[city]) > AQI_STALE_MS &&
        s_state[city] == FETCH_OK) {
      s_state[city] = FETCH_STALE;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  int8_t cityToUpdate = -1;
  for (uint8_t step = 0; step < CITY_COUNT; step++) {
    const uint8_t city = (uint8_t)((s_roundRobinCityIndex + step) % CITY_COUNT);
    if (nowMs < s_nextAttemptMs[city]) {
      continue;
    }
    const bool refreshDue =
      !s_hasData[city] || (nowMs - s_lastSuccessMs[city]) >= AQI_REFRESH_MS;
    if (!refreshDue && s_state[city] != FETCH_ERROR) {
      continue;
    }
    cityToUpdate = (int8_t)city;
    break;
  }
  if (cityToUpdate < 0) {
    return;
  }

  const uint8_t city = (uint8_t)cityToUpdate;
  s_roundRobinCityIndex = (uint8_t)((city + 1U) % CITY_COUNT);
  s_state[city] = FETCH_RUNNING;

  AqiServiceData nextData = {-1, -1, -1};
  if (fetchAqi(nextData, city)) {
    s_data[city] = nextData;
    s_hasData[city] = true;
    s_lastSuccessMs[city] = nowMs;
    s_state[city] = FETCH_OK;
    s_nextAttemptMs[city] = nowMs + AQI_REFRESH_MS;
    FW_LOG_I("AQI updated for %s aqi=%d pm25=%d pm10=%d",
          currentWeatherServiceGetCityNameForCity(city),
          s_data[city].aqi,
          s_data[city].pm25,
          s_data[city].pm10);
    return;
  }

  s_state[city] = s_hasData[city] ? FETCH_STALE : FETCH_ERROR;
  s_nextAttemptMs[city] = nowMs + AQI_RETRY_MS;
}

DataFetchState aqiServiceGetState(void) {
  return aqiServiceGetStateForCity(s_selectedCityIndex);
}

DataFetchState aqiServiceGetStateForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return FETCH_NONE;
  }
  return s_state[cityIndex];
}

const char* aqiServiceGetStateLabel(void) {
  switch (aqiServiceGetState()) {
    case FETCH_RUNNING: return "running";
    case FETCH_OK: return "ok";
    case FETCH_STALE: return "stale";
    case FETCH_ERROR: return "error";
    case FETCH_BACKUP: return "backup";
    default: return "none";
  }
}

bool aqiServiceHasData(void) {
  return aqiServiceHasDataForCity(s_selectedCityIndex);
}

bool aqiServiceHasDataForCity(uint8_t cityIndex) {
  return cityIndex < CITY_COUNT && s_hasData[cityIndex];
}

uint8_t aqiServiceGetCityIndex(void) {
  return s_selectedCityIndex;
}

const char* aqiServiceGetCityName(void) {
  return currentWeatherServiceGetCityNameForCity(s_selectedCityIndex);
}

uint32_t aqiServiceGetAgeSeconds(uint32_t nowMs) {
  return aqiServiceGetAgeSecondsForCity(s_selectedCityIndex, nowMs);
}

uint32_t aqiServiceGetAgeSecondsForCity(uint8_t cityIndex, uint32_t nowMs) {
  if (cityIndex >= CITY_COUNT || !s_hasData[cityIndex] || s_lastSuccessMs[cityIndex] == 0) {
    return 0;
  }
  return (nowMs - s_lastSuccessMs[cityIndex]) / 1000UL;
}

const AqiServiceData* aqiServiceGetData(void) {
  return aqiServiceGetDataForCity(s_selectedCityIndex);
}

const AqiServiceData* aqiServiceGetDataForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT || !s_hasData[cityIndex]) {
    return nullptr;
  }
  return &s_data[cityIndex];
}
