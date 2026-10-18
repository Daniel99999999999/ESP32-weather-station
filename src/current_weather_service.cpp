#include "current_weather_service.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>

#include "app_config.h"
#include "settings.h"

// Bufory i metadane trzymane per miasto, żeby UI mogło korzystać z ostatnich
// poprawnych danych niezależnie od tego, który ekran jest aktualnie aktywny.
static OpenWeatherMapCurrentData s_currentWeather[CITY_COUNT] = {};
static DataFetchState s_state[CITY_COUNT] = {};
static uint8_t s_selectedCityIndex = 0;
static uint8_t s_roundRobinCityIndex = 0;
static bool s_hasData[CITY_COUNT] = {};
static uint32_t s_lastStartMs[CITY_COUNT] = {};
static uint32_t s_lastSuccessMs[CITY_COUNT] = {};
static uint32_t s_lastErrorMs[CITY_COUNT] = {};
static uint32_t s_nextAttemptMs[CITY_COUNT] = {};

// Harmonogram odświeżania i retry dla bieżącej pogody.
static const uint32_t CURRENT_WEATHER_REFRESH_MS = REFRESH_WEATHER_MIN * 60UL * 1000UL;
static const uint32_t CURRENT_WEATHER_RETRY_MS = 60UL * 1000UL;
static const uint32_t CURRENT_WEATHER_STALE_MS = CURRENT_WEATHER_REFRESH_MS * 2UL;
static const uint32_t CURRENT_WEATHER_HTTP_TIMEOUT_MS = 5000UL;

static void clearCurrentWeather(OpenWeatherMapCurrentData &data) {
  data = {};
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

static bool parseJsonUIntField(const String &json, const char *token, uint32_t &outValue) {
  int pos = json.indexOf(token);
  if (pos < 0) return false;
  pos += strlen(token);

  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\"')) pos++;

  int end = pos;
  bool seen = false;
  while (end < (int)json.length()) {
    char c = json[end];
    if (c >= '0' && c <= '9') {
      seen = true;
      end++;
      continue;
    }
    break;
  }

  if (!seen) return false;
  outValue = (uint32_t)json.substring(pos, end).toInt();
  return true;
}

static bool parseJsonStringField(const String &json, const char *token, String &outValue) {
  int pos = json.indexOf(token);
  if (pos < 0) return false;
  pos += strlen(token);

  int end = json.indexOf('\"', pos);
  if (end < 0) return false;

  outValue = json.substring(pos, end);
  return true;
}

static bool extractJsonObjectField(const String &json, const char *fieldToken, String &outObject) {
  int searchPos = 0;
  const int tokenLength = strlen(fieldToken);

  while (searchPos < (int)json.length()) {
    const int keyPos = json.indexOf(fieldToken, searchPos);
    if (keyPos < 0) return false;

    int pos = keyPos + tokenLength;
    while (pos < (int)json.length() && json[pos] == ' ') pos++;
    if (pos >= (int)json.length() || json[pos] != ':') {
      searchPos = keyPos + tokenLength;
      continue;
    }

    pos++;
    while (pos < (int)json.length() && json[pos] == ' ') pos++;
    if (pos >= (int)json.length() || json[pos] != '{') {
      // OWM wcześniej zwraca np. weather[].main jako tekst. Szukamy main będącego obiektem.
      searchPos = keyPos + tokenLength;
      continue;
    }

    const int start = pos;
    int depth = 0;
    for (; pos < (int)json.length(); pos++) {
      const char c = json[pos];
      if (c == '{') {
        depth++;
      } else if (c == '}') {
        depth--;
        if (depth == 0) {
          outObject = json.substring(start, pos + 1);
          return true;
        }
      }
    }

    return false;
  }

  return false;
}

// Pobiera i parsuje bieżącą pogodę dla wskazanego miasta.
// Zwraca true tylko wtedy, gdy udało się zbudować kompletny snapshot z ważnym timestampem.
static bool fetchCurrentWeather(OpenWeatherMapCurrentData &outData, uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return false;
  }

  const String url =
    String("http://api.openweathermap.org/data/2.5/weather?id=") +
    CITY_OWM_IDS[cityIndex] +
    "&appid=" + OPEN_WEATHER_MAP_API_KEY +
    "&units=" + (IS_METRIC ? "metric" : "imperial") +
    "&lang=" + OPEN_WEATHER_MAP_LANGUAGE;

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    log_e("Current weather HTTP begin failed for city index %u", cityIndex);
    return false;
  }

  http.setTimeout(CURRENT_WEATHER_HTTP_TIMEOUT_MS);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    log_e("Current weather HTTP error for city index %u: %d", cityIndex, code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  clearCurrentWeather(outData);
  outData.cityName = CITY_NAMES[cityIndex];

  uint32_t weatherId = 0;
  uint32_t humidity = 0;
  uint32_t pressure = 0;
  uint32_t visibility = 0;
  uint32_t observationTime = 0;
  uint32_t sunrise = 0;
  uint32_t sunset = 0;
  String description;
  String icon;
  String serverCityName;
  String mainBlock;

  bool ok = true;
  ok &= parseJsonFloatField(payload, "\"lon\":", outData.lon);
  ok &= parseJsonFloatField(payload, "\"lat\":", outData.lat);
  ok &= parseJsonUIntField(payload, "\"id\":", weatherId);
  ok &= parseJsonStringField(payload, "\"description\":\"", description);
  ok &= parseJsonStringField(payload, "\"icon\":\"", icon);
  ok &= extractJsonObjectField(payload, "\"main\"", mainBlock);
  ok &= parseJsonFloatField(mainBlock, "\"temp\":", outData.temp);
  ok &= parseJsonFloatField(mainBlock, "\"feels_like\":", outData.feelsLike);
  ok &= parseJsonUIntField(mainBlock, "\"pressure\":", pressure);
  ok &= parseJsonUIntField(mainBlock, "\"humidity\":", humidity);
  outData.tempMin = NAN;
  outData.tempMax = NAN;
  const bool hasTempMin = parseJsonFloatField(mainBlock, "\"temp_min\":", outData.tempMin);
  const bool hasTempMax = parseJsonFloatField(mainBlock, "\"temp_max\":", outData.tempMax);
  ok &= hasTempMin && hasTempMax;
  ok &= isfinite(outData.tempMin) && isfinite(outData.tempMax);
  ok &= outData.tempMin <= outData.tempMax;
  parseJsonUIntField(payload, "\"visibility\":", visibility);
  parseJsonFloatField(payload, "\"speed\":", outData.windSpeed);
  parseJsonFloatField(payload, "\"deg\":", outData.windDeg);
  parseJsonUIntField(payload, "\"dt\":", observationTime);
  parseJsonUIntField(payload, "\"sunrise\":", sunrise);
  parseJsonUIntField(payload, "\"sunset\":", sunset);
  parseJsonStringField(payload, "\"name\":\"", serverCityName);

  if (!ok) {
    log_e("Current weather parse error for city index %u (temp_min=%s, temp_max=%s)",
          cityIndex,
          hasTempMin ? "ok" : "missing",
          hasTempMax ? "ok" : "missing");
    clearCurrentWeather(outData);
    outData.cityName = CITY_NAMES[cityIndex];
    return false;
  }

  outData.weatherId = (uint16_t)weatherId;
  outData.description = description;
  outData.icon = icon;
  outData.humidity = (uint8_t)humidity;
  outData.pressure = (uint16_t)pressure;
  outData.visibility = (uint16_t)visibility;
  outData.observationTime = observationTime;
  outData.sunrise = sunrise;
  outData.sunset = sunset;
  FW_LOG_I("Current weather JSON temperatures for %s: temp=%.2f min=%.2f max=%.2f",
           CITY_NAMES[cityIndex], outData.temp, outData.tempMin, outData.tempMax);
  if (serverCityName.length() > 0) {
    outData.cityName = serverCityName;
  }

  return outData.observationTime > 0;
}

void currentWeatherServiceInit(void) {
  s_selectedCityIndex = 0;
  s_roundRobinCityIndex = 0;
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    clearCurrentWeather(s_currentWeather[city]);
    s_state[city] = FETCH_NONE;
    s_hasData[city] = false;
    s_lastStartMs[city] = 0;
    s_lastSuccessMs[city] = 0;
    s_lastErrorMs[city] = 0;
    s_nextAttemptMs[city] = 0;
  }
}

// Getter bez sufiksu ForCity() ma odwoływać się do tego miasta.
void currentWeatherServiceSetCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return;
  }
  s_selectedCityIndex = cityIndex;
}

// Serwis aktualizuje miasta round-robin, żeby oba cache żyły w tle równolegle.
void currentWeatherServiceUpdate(uint32_t nowMs) {
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    if (s_hasData[city] &&
        (nowMs - s_lastSuccessMs[city]) > CURRENT_WEATHER_STALE_MS &&
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
      !s_hasData[city] || (nowMs - s_lastSuccessMs[city]) >= CURRENT_WEATHER_REFRESH_MS;
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
  s_lastStartMs[city] = nowMs;

  OpenWeatherMapCurrentData nextData = {};
  if (fetchCurrentWeather(nextData, city)) {
    s_currentWeather[city] = nextData;
    s_hasData[city] = true;
    s_lastSuccessMs[city] = nowMs;
    s_state[city] = FETCH_OK;
    s_nextAttemptMs[city] = nowMs + CURRENT_WEATHER_REFRESH_MS;
    FW_LOG_I("Current weather updated for %s temp=%.1f weatherId=%u",
          s_currentWeather[city].cityName.c_str(),
          s_currentWeather[city].temp,
          s_currentWeather[city].weatherId);
    return;
  }

  s_lastErrorMs[city] = nowMs;
  s_state[city] = s_hasData[city] ? FETCH_STALE : FETCH_ERROR;
  s_nextAttemptMs[city] = nowMs + CURRENT_WEATHER_RETRY_MS;
}

DataFetchState currentWeatherServiceGetState(void) {
  return currentWeatherServiceGetStateForCity(s_selectedCityIndex);
}

DataFetchState currentWeatherServiceGetStateForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return FETCH_NONE;
  }
  return s_state[cityIndex];
}

const char* currentWeatherServiceGetStateLabel(void) {
  switch (currentWeatherServiceGetState()) {
    case FETCH_RUNNING: return "running";
    case FETCH_OK: return "ok";
    case FETCH_STALE: return "stale";
    case FETCH_ERROR: return "error";
    case FETCH_BACKUP: return "backup";
    default: return "none";
  }
}

const OpenWeatherMapCurrentData* currentWeatherServiceGetData(void) {
  return currentWeatherServiceGetDataForCity(s_selectedCityIndex);
}

const OpenWeatherMapCurrentData* currentWeatherServiceGetDataForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT || !s_hasData[cityIndex]) {
    return nullptr;
  }
  return &s_currentWeather[cityIndex];
}

const char* currentWeatherServiceGetCityName(void) {
  if (s_selectedCityIndex < CITY_COUNT &&
      s_hasData[s_selectedCityIndex] &&
      s_currentWeather[s_selectedCityIndex].cityName.length() > 0) {
    return s_currentWeather[s_selectedCityIndex].cityName.c_str();
  }
  return (s_selectedCityIndex < CITY_COUNT) ? CITY_NAMES[s_selectedCityIndex] : "n/a";
}

uint8_t currentWeatherServiceGetCityIndex(void) {
  return s_selectedCityIndex;
}

bool currentWeatherServiceHasData(void) {
  return currentWeatherServiceHasDataForCity(s_selectedCityIndex);
}

bool currentWeatherServiceHasDataForCity(uint8_t cityIndex) {
  return cityIndex < CITY_COUNT && s_hasData[cityIndex];
}

uint32_t currentWeatherServiceGetAgeSeconds(uint32_t nowMs) {
  return currentWeatherServiceGetAgeSecondsForCity(s_selectedCityIndex, nowMs);
}

uint32_t currentWeatherServiceGetAgeSecondsForCity(uint8_t cityIndex, uint32_t nowMs) {
  if (cityIndex >= CITY_COUNT || !s_hasData[cityIndex] || s_lastSuccessMs[cityIndex] == 0) {
    return 0;
  }
  return (nowMs - s_lastSuccessMs[cityIndex]) / 1000UL;
}

uint32_t currentWeatherServiceGetLastSuccessMsForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return 0;
  }
  return s_lastSuccessMs[cityIndex];
}
