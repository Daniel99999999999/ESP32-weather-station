#include "forecast_service.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <JsonListener.h>
#include <JsonStreamingParser.h>

#include "app_config.h"
#include "current_weather_service.h"
#include "settings.h"

// Forecast jest przechowywany per miasto, tak aby ekran i wykres mogły
// korzystać z ostatnich poprawnych danych bez czekania na świeży fetch po przełączeniu.
static OpenWeatherMapForecastData s_forecasts[CITY_COUNT][NUMBER_OF_FORECASTS] = {};
// Bufor pobierania nie może leżeć na stosie loopTask: 40 rekordów zawiera wiele
// obiektów String, a razem z DNS/HTTP przekraczało to dostępny stos pętli ESP32.
static OpenWeatherMapForecastData s_pendingForecasts[NUMBER_OF_FORECASTS] = {};
static DataFetchState s_state[CITY_COUNT] = {};
static uint8_t s_selectedCityIndex = 0;
static uint8_t s_roundRobinCityIndex = 0;
static uint8_t s_count[CITY_COUNT] = {};
static bool s_hasData[CITY_COUNT] = {};
static uint32_t s_lastSuccessMs[CITY_COUNT] = {};
static uint32_t s_nextAttemptMs[CITY_COUNT] = {};

// Harmonogram odświeżania forecastu.
static const uint32_t FORECAST_REFRESH_MS = REFRESH_WEATHER_MIN * 60UL * 1000UL;
static const uint32_t FORECAST_RETRY_MS = 60UL * 1000UL;
static const uint32_t FORECAST_STALE_MS = FORECAST_REFRESH_MS * 2UL;
static const uint32_t FORECAST_HTTP_TIMEOUT_MS = 6000UL;

static void clearForecastData(uint8_t cityIndex) {
  for (uint8_t i = 0; i < NUMBER_OF_FORECASTS; i++) {
    s_forecasts[cityIndex][i] = {};
  }
  s_count[cityIndex] = 0;
}

class ForecastPayloadParser : public JsonListener {
 public:
  ForecastPayloadParser(OpenWeatherMapForecastData *target, uint8_t maxItems)
      : data(target), maxForecasts(maxItems) {}

  uint8_t getCount() const { return currentForecast; }

  void startDocument() override {
    currentKey = "";
    currentParent = "";
    currentForecast = 0;
    isCurrentForecastAllowed = true;
  }

  void endDocument() override {}
  void whitespace(char) override {}
  void startArray() override {}
  void endArray() override {}

  void key(String key) override {
    currentKey = key;
  }

  void startObject() override {
    currentParent = currentKey;
  }

  void endObject() override {
    currentParent = "";
  }

  void value(String value) override {
    if (currentForecast >= maxForecasts) {
      return;
    }

    if (currentKey == "dt") {
      data[currentForecast].observationTime = value.toInt();

      if (allowedHoursCount > 0) {
        // Firmware używa tylko wybranych godzin UTC, żeby uprościć późniejszy widok
        // forecastu do stałej siatki dziennych punktów.
        isCurrentForecastAllowed = false;
        time_t forecastTime = data[currentForecast].observationTime;
        const struct tm *timeInfo = gmtime(&forecastTime);
        if (timeInfo != nullptr) {
          for (uint8_t i = 0; i < allowedHoursCount; i++) {
            if ((uint8_t)timeInfo->tm_hour == allowedHours[i]) {
              isCurrentForecastAllowed = true;
              break;
            }
          }
        }
      }
      return;
    }

    if (!isCurrentForecastAllowed) {
      if (currentKey == "dt_txt") {
        isCurrentForecastAllowed = true;
      }
      return;
    }

    if (currentKey == "temp") {
      data[currentForecast].temp = value.toFloat();
      data[currentForecast].rain = 0.0f;
    } else if (currentKey == "feels_like") {
      data[currentForecast].feelsLike = value.toFloat();
    } else if (currentKey == "temp_min") {
      data[currentForecast].tempMin = value.toFloat();
    } else if (currentKey == "temp_max") {
      data[currentForecast].tempMax = value.toFloat();
    } else if (currentKey == "pressure") {
      data[currentForecast].pressure = value.toFloat();
    } else if (currentKey == "humidity") {
      data[currentForecast].humidity = (uint8_t)value.toInt();
    } else if (currentKey == "speed") {
      data[currentForecast].windSpeed = value.toFloat();
    } else if (currentKey == "deg") {
      data[currentForecast].windDeg = value.toFloat();
    } else if (currentKey == "3h") {
      data[currentForecast].rain = value.toFloat();
    } else if (currentKey == "dt_txt") {
      data[currentForecast].observationTimeText = value;
      currentForecast++;
    }

    if (currentParent == "weather") {
      if (currentKey == "id") {
        data[currentForecast].weatherId = (uint16_t)value.toInt();
      } else if (currentKey == "main") {
        data[currentForecast].main = value;
      } else if (currentKey == "description") {
        data[currentForecast].description = value;
      } else if (currentKey == "icon") {
        data[currentForecast].icon = value;
      }
    }
  }

  void setAllowedHours(const uint8_t *hours, uint8_t count) {
    allowedHours = hours;
    allowedHoursCount = count;
  }

 private:
  String currentKey;
  String currentParent;
  OpenWeatherMapForecastData *data = nullptr;
  uint8_t maxForecasts = 0;
  uint8_t currentForecast = 0;
  const uint8_t *allowedHours = nullptr;
  uint8_t allowedHoursCount = 0;
  bool isCurrentForecastAllowed = true;
};

// Pobiera pełny payload forecastu i filtruje go do rekordów używanych przez UI.
static bool fetchForecast(OpenWeatherMapForecastData *outData, uint8_t &outCount, uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return false;
  }

  const String url =
    String("http://api.openweathermap.org/data/2.5/forecast?") +
    CITY_OWM_LOCATIONS[cityIndex] +
    "&appid=" + OPEN_WEATHER_MAP_API_KEY +
    "&units=" + (IS_METRIC ? "metric" : "imperial") +
    "&lang=" + OPEN_WEATHER_MAP_LANGUAGE;

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    log_e("Forecast HTTP begin failed for city index %u", cityIndex);
    return false;
  }

  http.setTimeout(FORECAST_HTTP_TIMEOUT_MS);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    log_e("Forecast HTTP error for city index %u: %d", cityIndex, code);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  for (uint8_t i = 0; i < NUMBER_OF_FORECASTS; i++) {
    outData[i] = {};
  }

  JsonStreamingParser parser;
  ForecastPayloadParser listener(outData, NUMBER_OF_FORECASTS);
  listener.setAllowedHours(forecastHoursUtc, sizeof(forecastHoursUtc) / sizeof(forecastHoursUtc[0]));
  parser.setListener(&listener);

  for (uint32_t i = 0; i < payload.length(); i++) {
    parser.parse(payload[i]);
  }

  outCount = listener.getCount();
  return outCount > 0;
}

void forecastServiceInit(void) {
  s_selectedCityIndex = 0;
  s_roundRobinCityIndex = 0;
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    clearForecastData(city);
    s_state[city] = FETCH_NONE;
    s_hasData[city] = false;
    s_lastSuccessMs[city] = 0;
    s_nextAttemptMs[city] = 0;
  }
}

// Getter bez sufiksu ForCity() ma odwoływać się do tego miasta.
void forecastServiceSetCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return;
  }
  s_selectedCityIndex = cityIndex;
}

// Serwis odświeża miasta round-robin, dzięki czemu oba cache forecastu żyją w tle.
void forecastServiceUpdate(uint32_t nowMs) {
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    if (s_hasData[city] &&
        (nowMs - s_lastSuccessMs[city]) > FORECAST_STALE_MS &&
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
      !s_hasData[city] || (nowMs - s_lastSuccessMs[city]) >= FORECAST_REFRESH_MS;
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

  uint8_t nextCount = 0;
  if (fetchForecast(s_pendingForecasts, nextCount, city)) {
    for (uint8_t i = 0; i < NUMBER_OF_FORECASTS; i++) {
      s_forecasts[city][i] = s_pendingForecasts[i];
    }
    s_count[city] = nextCount;
    s_hasData[city] = true;
    s_lastSuccessMs[city] = nowMs;
    s_state[city] = FETCH_OK;
    s_nextAttemptMs[city] = nowMs + FORECAST_REFRESH_MS;
    FW_LOG_I("Forecast updated for %s count=%u",
             currentWeatherServiceGetCityNameForCity(city),
             s_count[city]);
    return;
  }

  s_state[city] = s_hasData[city] ? FETCH_STALE : FETCH_ERROR;
  s_nextAttemptMs[city] = nowMs + FORECAST_RETRY_MS;
}

DataFetchState forecastServiceGetState(void) {
  return forecastServiceGetStateForCity(s_selectedCityIndex);
}

DataFetchState forecastServiceGetStateForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return FETCH_NONE;
  }
  return s_state[cityIndex];
}

const char* forecastServiceGetStateLabel(void) {
  switch (forecastServiceGetState()) {
    case FETCH_RUNNING: return "running";
    case FETCH_OK: return "ok";
    case FETCH_STALE: return "stale";
    case FETCH_ERROR: return "error";
    case FETCH_BACKUP: return "backup";
    default: return "none";
  }
}

const OpenWeatherMapForecastData* forecastServiceGetData(void) {
  return forecastServiceGetDataForCity(s_selectedCityIndex);
}

const OpenWeatherMapForecastData* forecastServiceGetDataForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT || !s_hasData[cityIndex]) {
    return nullptr;
  }
  return s_forecasts[cityIndex];
}

uint8_t forecastServiceGetCount(void) {
  return forecastServiceGetCountForCity(s_selectedCityIndex);
}

uint8_t forecastServiceGetCountForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return 0;
  }
  return s_count[cityIndex];
}

uint8_t forecastServiceGetCityIndex(void) {
  return s_selectedCityIndex;
}

const char* forecastServiceGetCityName(void) {
  return currentWeatherServiceGetCityNameForCity(s_selectedCityIndex);
}

bool forecastServiceHasData(void) {
  return forecastServiceHasDataForCity(s_selectedCityIndex);
}

bool forecastServiceHasDataForCity(uint8_t cityIndex) {
  return cityIndex < CITY_COUNT && s_hasData[cityIndex];
}

uint32_t forecastServiceGetAgeSeconds(uint32_t nowMs) {
  return forecastServiceGetAgeSecondsForCity(s_selectedCityIndex, nowMs);
}

uint32_t forecastServiceGetAgeSecondsForCity(uint8_t cityIndex, uint32_t nowMs) {
  if (cityIndex >= CITY_COUNT || !s_hasData[cityIndex] || s_lastSuccessMs[cityIndex] == 0) {
    return 0;
  }
  return (nowMs - s_lastSuccessMs[cityIndex]) / 1000UL;
}
