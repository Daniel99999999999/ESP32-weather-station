#include "app_state.h"

#include <string.h>

#include "current_weather_service.h"
#include "forecast_service.h"
#include "aqi_service.h"
#include "radiation_job_service.h"
#include "util.h"

// app_state scala wiele serwisów w jeden spójny snapshot dla warstwy UI.
static AppState s_appState = {};

// Bezpieczne kopiowanie C-stringów do stałych buforów w AppState.
static void copyText(char *dest, size_t destSize, const char *src) {
  if (dest == nullptr || destSize == 0) {
    return;
  }

  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }

  strncpy(dest, src, destSize - 1);
  dest[destSize - 1] = '\0';
}

void appStateInit(void) {
  s_appState = {};
  s_appState.currentWeather.cityIndex = 0xFF;
  s_appState.currentWeather.fetchState = FETCH_NONE;
  copyText(s_appState.currentWeather.cityName, sizeof(s_appState.currentWeather.cityName), "n/a");
  s_appState.forecast.cityIndex = 0xFF;
  s_appState.forecast.fetchState = FETCH_NONE;
  s_appState.aqi.cityIndex = 0xFF;
  s_appState.aqi.fetchState = FETCH_NONE;
  s_appState.aqi.aqi = -1;
  s_appState.aqi.pm25 = -1;
  s_appState.aqi.pm10 = -1;
  s_appState.radiation.cityIndex = 0xFF;
  s_appState.radiation.fetchState = FETCH_NONE;
}

// Aktualizacja AppState nie pobiera nic z sieci; tylko mapuje gotowe dane z serwisów
// do uproszczonych struktur używanych przez renderery.
void appStateUpdate(uint32_t nowMs) {
  CurrentWeatherViewState &weather = s_appState.currentWeather;
  weather.cityIndex = currentWeatherServiceGetCityIndex();
  weather.fetchState = currentWeatherServiceGetState();
  weather.ageSeconds = currentWeatherServiceGetAgeSeconds(nowMs);
  copyText(weather.cityName, sizeof(weather.cityName), currentWeatherServiceGetCityName());

  const OpenWeatherMapCurrentData *data = currentWeatherServiceGetData();
  if (data == nullptr) {
    weather.hasData = false;
    copyText(weather.icon, sizeof(weather.icon), "");
    weather.weatherId = 0;
    weather.temperature = 0.0f;
    weather.feelsLike = 0.0f;
    weather.tempMin = 0.0f;
    weather.tempMax = 0.0f;
    weather.observationTime = 0;
    weather.humidity = 0;
    weather.pressure = 0;
    weather.windSpeed = 0.0f;
    weather.sunrise = 0;
    weather.sunset = 0;
    copyText(weather.description, sizeof(weather.description), "");
  } else {
    weather.hasData = true;
    copyText(weather.icon, sizeof(weather.icon), data->icon.c_str());
    weather.weatherId = data->weatherId;
    weather.temperature = data->temp;
    weather.feelsLike = data->feelsLike;
    weather.tempMin = data->tempMin;
    weather.tempMax = data->tempMax;
    weather.observationTime = data->observationTime;
    weather.humidity = data->humidity;
    weather.pressure = data->pressure;
    weather.windSpeed = data->windSpeed;
    weather.sunrise = data->sunrise;
    weather.sunset = data->sunset;
    copyText(weather.description, sizeof(weather.description), data->description.c_str());
  }

  ForecastViewState &forecast = s_appState.forecast;
  forecast.cityIndex = forecastServiceGetCityIndex();
  forecast.fetchState = forecastServiceGetState();
  forecast.itemCount = forecastServiceGetCount();
  forecast.ageSeconds = forecastServiceGetAgeSeconds(nowMs);
  forecast.hasData = forecastServiceHasData();

  for (uint8_t i = 0; i < NUMBER_OF_DAY_FORECASTS; i++) {
    forecast.days[i] = {};
  }

  const OpenWeatherMapForecastData *forecastData = forecastServiceGetData();
  if (forecastData != nullptr) {
    // Biblioteka zwraca forecast 3h, a UI potrzebuje dziennych skrótów,
    // więc tutaj następuje redukcja do NUMBER_OF_DAY_FORECASTS rekordów.
    DayForecast *dayForecasts = calculateDayForecasts(const_cast<OpenWeatherMapForecastData*>(forecastData));
    for (uint8_t i = 0; i < NUMBER_OF_DAY_FORECASTS; i++) {
      forecast.days[i].valid = dayForecasts[i].conditionCode != 0;
      forecast.days[i].weekday = (uint8_t)dayForecasts[i].day;
      forecast.days[i].minTemp = dayForecasts[i].minTemp;
      forecast.days[i].maxTemp = dayForecasts[i].maxTemp;
      forecast.days[i].weatherId = (uint16_t)dayForecasts[i].conditionCode;
    }
  }

  AqiViewState &aqi = s_appState.aqi;
  aqi.cityIndex = aqiServiceGetCityIndex();
  aqi.fetchState = aqiServiceGetState();
  aqi.ageSeconds = aqiServiceGetAgeSeconds(nowMs);

  const AqiServiceData *aqiData = aqiServiceGetData();
  if (aqiData == nullptr) {
    aqi.hasData = false;
    aqi.aqi = -1;
    aqi.pm25 = -1;
    aqi.pm10 = -1;
  } else {
    aqi.hasData = true;
    aqi.aqi = aqiData->aqi;
    aqi.pm25 = aqiData->pm25;
    aqi.pm10 = aqiData->pm10;
  }

  RadiationViewState &radiation = s_appState.radiation;
  radiation.cityIndex = radiationJobServiceGetCityIndex();
  radiation.fetchState = radiationJobServiceGetState();
  radiation.ageSeconds = radiationJobServiceGetAgeSeconds(nowMs);

  const RadiationJobData *radiationData = radiationJobServiceGetData();
  if (radiationData == nullptr) {
    radiation.hasData = false;
    radiation.display[0] = '\0';
    radiation.dateEnd[0] = '\0';
    radiation.status = RAD_STATUS_NO_DATA;
    radiation.sourceType = RAD_SOURCE_NONE;
    radiation.trend = 0;
    radiation.color = 0;
    return;
  }

  radiation.hasData = true;
  copyText(radiation.display, sizeof(radiation.display), radiationData->display);
  copyText(radiation.dateEnd, sizeof(radiation.dateEnd), radiationData->dateEnd);
  radiation.status = radiationData->status;
  radiation.sourceType = radiationData->sourceType;
  radiation.trend = radiationData->trend;
  radiation.color = radiationData->color;
}

const AppState* getAppState(void) {
  return &s_appState;
}
