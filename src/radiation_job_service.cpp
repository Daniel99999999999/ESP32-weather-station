#include "radiation_job_service.h"

#include <string.h>

#include "app_config.h"
#include "radiation_service.h"
#include "util.h"

// Tekstowe snapshoty źródeł są współdzielone z radiation_service.
String g_radDoseDisplay[CITY_COUNT] = {"n/a", "n/a", "n/a"};
String g_radDateEnd[CITY_COUNT] = {"", "", ""};

static RadiationJobData s_data = {};
static DataFetchState s_state = FETCH_NONE;
static uint8_t s_cityIndex = 0;
static bool s_hasData = false;
static uint32_t s_lastSuccessMs = 0;
static uint32_t s_nextAttemptMs = 0;

// Harmonogram joba radiation.
static const uint32_t RADIATION_REFRESH_MS = REFRESH_RADIATION_MIN * 60UL * 1000UL;
static const uint32_t RADIATION_RETRY_MS = 5UL * 60UL * 1000UL;
static const uint32_t RADIATION_STALE_MS = RADIATION_REFRESH_MS * 2UL;

// Bezpiecznie kopiuje String do stałego bufora UI.
static void copyText(char *dest, size_t destSize, const String &src) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  strncpy(dest, src.c_str(), destSize - 1);
  dest[destSize - 1] = '\0';
}

// Liczy wiek snapshotu radiation na podstawie timestampu wybranego źródła.
static uint32_t parseAgeSeconds() {
  time_t now = time(nullptr);
  time_t ts = 0;

  if (s_cityIndex < CITY_COUNT &&
      getRadiationSourceType() == RAD_SOURCE_PRIMARY &&
      g_radDateEnd[s_cityIndex].length() > 0) {
    int Y, M, D, h, m;
    if (sscanf(g_radDateEnd[s_cityIndex].c_str(), "%d-%d-%d %d:%d", &Y, &M, &D, &h, &m) == 5) {
      char iso[24];
      snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:00", Y, M, D, h, m);
      ts = parseIso8601ToEpoch(String(iso));
    }
  } else if (s_cityIndex < CITY_COUNT && g_radDateEnd[s_cityIndex].length() > 0) {
    ts = parseIso8601ToEpoch(g_radDateEnd[s_cityIndex]);
  }

  if (now <= 0 || ts <= 0 || now < ts) {
    return 0;
  }
  return (uint32_t)(now - ts);
}

// Zbiera z radiation_service gotowy snapshot do wykorzystania przez app_state/UI.
static bool snapshotRadiation(RadiationJobData &outData) {
  outData = {};
  outData.status = getRadiationStatus();
  outData.sourceType = getRadiationSourceType();
  outData.trend = getRadiationTrend();
  outData.color = getRadiationColor();

  if (outData.sourceType == RAD_SOURCE_PRIMARY) {
    copyText(outData.display, sizeof(outData.display), g_radDoseDisplay[s_cityIndex]);
    copyText(outData.dateEnd, sizeof(outData.dateEnd), g_radDateEnd[s_cityIndex]);
  } else if (outData.sourceType == RAD_SOURCE_BACKUP) {
    copyText(outData.display, sizeof(outData.display), g_radDoseDisplay[s_cityIndex]);
    copyText(outData.dateEnd, sizeof(outData.dateEnd), g_radDateEnd[s_cityIndex]);
  } else {
    copyText(outData.display, sizeof(outData.display), "n/a");
  }

  return outData.sourceType != RAD_SOURCE_NONE && strlen(outData.display) > 0 && strcmp(outData.display, "n/a") != 0;
}

void radiationJobServiceInit(void) {
  s_data = {};
  s_state = FETCH_NONE;
  s_cityIndex = 0;
  s_hasData = false;
  s_lastSuccessMs = 0;
  s_nextAttemptMs = millis() + RADIATION_START_DELAY_MS;
}

// Zmiana miasta czyści snapshot i opóźnia następny odczyt, żeby nie dusić UI.
void radiationJobServiceSetCity(uint8_t cityIndex) {
  if (cityIndex == s_cityIndex) {
    return;
  }

  s_cityIndex = cityIndex;
  s_data = {};
  s_hasData = false;
  s_lastSuccessMs = 0;
  s_nextAttemptMs = millis() + RADIATION_CITY_SWITCH_DELAY_MS;
  s_state = FETCH_NONE;
}

// Ręczne uruchomienie jest używane np. przez ekran debug do testowania źródeł.
bool radiationJobServiceRunManual(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT || WiFi.status() != WL_CONNECTED) {
    s_state = FETCH_ERROR;
    return false;
  }

  s_state = FETCH_RUNNING;
  s_cityIndex = cityIndex;
  updateRadiationForCityIndex(cityIndex);

  RadiationJobData nextData = {};
  if (snapshotRadiation(nextData)) {
    s_data = nextData;
    s_hasData = true;
    s_lastSuccessMs = millis();
    s_nextAttemptMs = s_lastSuccessMs + RADIATION_REFRESH_MS;
    s_state = (nextData.sourceType == RAD_SOURCE_BACKUP) ? FETCH_BACKUP : FETCH_OK;
    return true;
  }

  s_state = s_hasData ? FETCH_STALE : FETCH_ERROR;
  s_nextAttemptMs = millis() + RADIATION_RETRY_MS;
  return false;
}

// Automatyczny job tylko pilnuje harmonogramu i deleguje właściwy fetch do radiation_service.
void radiationJobServiceUpdate(uint32_t nowMs) {
  if (s_hasData && (nowMs - s_lastSuccessMs) > RADIATION_STALE_MS && s_state == FETCH_OK) {
    s_state = FETCH_STALE;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (nowMs < s_nextAttemptMs) {
    return;
  }

  const bool refreshDue = !s_hasData || (nowMs - s_lastSuccessMs) >= RADIATION_REFRESH_MS;
  if (!refreshDue && s_state != FETCH_ERROR) {
    return;
  }

  radiationJobServiceRunManual(s_cityIndex);
}

DataFetchState radiationJobServiceGetState(void) {
  return s_state;
}

const char* radiationJobServiceGetStateLabel(void) {
  switch (s_state) {
    case FETCH_RUNNING: return "running";
    case FETCH_OK: return "ok";
    case FETCH_STALE: return "stale";
    case FETCH_ERROR: return "error";
    case FETCH_BACKUP: return "backup";
    default: return "none";
  }
}

bool radiationJobServiceHasData(void) {
  return s_hasData;
}

uint8_t radiationJobServiceGetCityIndex(void) {
  return s_cityIndex;
}

uint32_t radiationJobServiceGetAgeSeconds(uint32_t nowMs) {
  (void)nowMs;
  if (!s_hasData) {
    return 0;
  }
  return parseAgeSeconds();
}

const RadiationJobData* radiationJobServiceGetData(void) {
  return s_hasData ? &s_data : nullptr;
}

const char* radiationJobServiceGetDisplayForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return "n/a";
  }
  return g_radDoseDisplay[cityIndex].c_str();
}

const char* radiationJobServiceGetDateEndForCity(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT) {
    return "";
  }
  return g_radDateEnd[cityIndex].c_str();
}
