#include "util.h"
#include "app_config.h"
#include "settings.h"

char timestampBuffer[64];


/**
 * Use the 3h/5d OWM forecast data to condense it into minimal daily forecasts (as required by this app).
 * Algo:
 * - iterate over all OWM forecasts
 * - skip the ones from the current day
 * - find min/max temp for each day by comparing the temp from the current 3h forecast against the min/max found so far
 * - use the condition code (i.e. the weather) of the one 3h forecast closest to 12 moon
 *
 * @param forecasts NUMBER_OF_DAY_FORECASTS 3h/5d OWM forecast containers
 * @return DayForecast* array of NUMBER_OF_DAY_FORECASTS minimal daily forecast containers
 */
DayForecast* calculateDayForecasts(OpenWeatherMapForecastData *forecasts) {
  uint8_t weekday = getCurrentWeekday();
  static DayForecast dayForecasts[NUMBER_OF_DAY_FORECASTS];
  for (int i = 0; i < NUMBER_OF_DAY_FORECASTS; i++) {
    dayForecasts[i] = {200.0f, -200.0f, 0, 23, -1};
  }
  int k = -1;
  int currentForecastDay = -1;

  for (uint8_t i = 0; i < NUMBER_OF_FORECASTS; i++) {
    OpenWeatherMapForecastData forecast = forecasts[i];
    time_t forecastTimeUtc = forecast.observationTime;
    struct tm *forecastLocalTime = localtime(&forecastTimeUtc);

    if (weekday == forecastLocalTime->tm_wday) {
      strftime(timestampBuffer, sizeof(timestampBuffer), SYSTEM_TIMESTAMP_FORMAT, forecastLocalTime);
      FW_LOG_D("Skipping forecast for today %s", timestampBuffer);
      continue;
    }

    if (forecastLocalTime->tm_wday != currentForecastDay) {
      currentForecastDay = forecastLocalTime->tm_wday;
      k++;
      dayForecasts[k].day = currentForecastDay;
    }
    FW_LOG_D("Current forecast day: %d, array index: %d, hour: %d, temp: %.1f", currentForecastDay, k, forecastLocalTime->tm_hour, forecast.temp);
    if (forecast.temp < dayForecasts[k].minTemp) dayForecasts[k].minTemp = forecast.temp;
    if (forecast.temp > dayForecasts[k].maxTemp) dayForecasts[k].maxTemp = forecast.temp;
    // find the condition closest to 12 noon (tm_hour is 0-23)
    if (abs(12 - forecastLocalTime->tm_hour) < abs(12 - dayForecasts[k].conditionHour)) {
      dayForecasts[k].conditionCode = forecast.weatherId;
      dayForecasts[k].conditionHour = forecastLocalTime->tm_hour;
    }
  }
  return dayForecasts;
}

uint8_t getCurrentWeekday() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    log_e("Failed to obtain time.");
    return -1;
  }
  return timeinfo.tm_wday;
}

String getCurrentTimestamp(const char* format) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    log_e("Failed to obtain time.");
    return "";
  }
  strftime(timestampBuffer, sizeof(timestampBuffer), format, &timeinfo);
  return String(timestampBuffer);
}

bool hasValidTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) {
    return false;
  }
  return timeinfo.tm_year >= (2024 - 1900);
}

boolean initTime() {
  struct tm timeinfo;
  const uint32_t timeoutMs = 15000;
  const uint32_t stepMs = 500;
  uint32_t waitedMs = 0;

  FW_LOG_I("Synchronizing time.");
  // Connect to NTP servers with 0 TZ offset, call setTimezone() later.
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");

  while (waitedMs < timeoutMs) {
    if (getLocalTime(&timeinfo, stepMs)) {
      FW_LOG_I("UTC time: %s", getCurrentTimestamp(SYSTEM_TIMESTAMP_FORMAT).c_str());
      return true;
    }
    waitedMs += stepMs;
  }

  log_e("Failed to obtain time after %lu ms.", timeoutMs);
  return false;
}

void logBanner() {
  FW_LOG_I("********************************");
  FW_LOG_I("* ESP32 Stacja pogodowa v%s *", VERSION);
  FW_LOG_I("********************************");
}

void logMemoryStats() {
  FW_LOG_I("Total heap: %d", ESP.getHeapSize());
  FW_LOG_I("Free heap: %d", ESP.getFreeHeap());
  FW_LOG_I("Total PSRAM: %d", ESP.getPsramSize());
  FW_LOG_I("Free PSRAM: %d", ESP.getFreePsram());
}

void setTimezone(const char* timezone) {
  FW_LOG_I("Setting timezone to '%s'.", timezone);
  // Clock settings are adjusted to show the new local time
  setenv("TZ", timezone, 1);
  tzset();
}

// Algorithm: http://howardhinnant.github.io/date_algorithms.html
int days_from_epoch(int y, int m, int d) {
  y -= m <= 2;
  int era = y / 400;
  int yoe = y - era * 400;                                  // [0, 399]
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
  return era * 146097 + doe - 719468;
}
