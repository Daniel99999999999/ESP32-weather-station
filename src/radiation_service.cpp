#include "radiation_service.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <TFT_eSPI.h>

#include "settings.h"
#include "app_config.h"
#include "ambient.h"

// Snapshoty źródeł są utrzymywane poza tym modułem, ale tutaj składane są
// w pojedynczy wynik logiczny używany przez UI i warstwę joba.
// --- externy (stan nadal w main.cpp) ---
extern String g_radDoseDisplay[CITY_COUNT];
extern String g_radDateEnd[CITY_COUNT];


// Identyfikatory endpointu PAA dla stacji Kielce.
static const char *PAA_KIELCE_STATION_ID = "158a3029-8d37-45d5-ae16-7793cb2afc8c";
static const char *PAA_TARNOWITZ_STATION_ID = "a4d1366c-b78c-4d14-b8f7-03aed4e509a1";
static const char *PAA_LAYER_ID = "15d20873-f8a7-8899-5d69-960cc9ebbbb6";
static const char *PAA_TABLE_ID = "f5af6ec4-d759-3163-344e-cbf147d28e28";


int32_t g_prevDose = -1;
int8_t g_trend = 0; // -1 ↓, 0 →, +1 ↑

// Lokalny wybór źródła i statusu, na którym później opiera się UI.
static void selectRadiationSource(void);
static void updateRadiationTrend(void);
static void updateRadiationStatus(void);

// Parser timestampu ISO8601 do epoch UTC.
time_t parseIso8601ToEpoch(const String &ts);

uint16_t getRadiationColor(void);
RadiationStatus getRadiationStatus(void);
const char* getTrendSymbol(void);
int8_t getRadiationTrend(void);



RadiationStatus g_radStatus = RAD_STATUS_NO_DATA;
static char g_radiationLastError[48] = "";
static uint8_t g_radActiveCityIndex = 0;

// Lokalne helpery czasu i prostego parsingu tokenów JSON.
static String formatIsoUtcDate(time_t t);
static time_t makeUtcEpoch(int year, int month, int day, int hour, int minute, int second);
static bool parseJsonFloatField(const String &json, const char *token, float &outValue);
static bool parseLastJsonStringField(const String &json, const char *token, String &outValue);
static bool parseLastJsonFloatField(const String &json, const char *token, float &outValue);

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return (uint16_t)(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

// Konwersja daty kalendarzowej na liczbę dni od epoki Unix.
static int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned)(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// Buduje epoch UTC bez zależności od lokalnej strefy czasowej.
static time_t makeUtcEpoch(int year, int month, int day, int hour, int minute, int second) {
  const int64_t days = daysFromCivil(year, (unsigned)month, (unsigned)day);
  const int64_t seconds =
    days * 86400LL + (int64_t)hour * 3600LL + (int64_t)minute * 60LL + second;
  return (time_t)seconds;
}

// Formatuje epoch UTC do ISO-8601 dla zapytań PAA.
static String formatIsoUtcDate(time_t t) {
  struct tm tmUtc;
  gmtime_r(&t, &tmUtc);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000Z", &tmUtc);
  return String(buf);
}

// Parser pola PAA "YYYY-MM-DD HH:MM" traktowanego jako UTC.
static time_t parsePaaDateEndToEpoch(const String &ts) {
  int Y, M, D, h, m;
  if (sscanf(ts.c_str(), "%d-%d-%d %d:%d", &Y, &M, &D, &h, &m) != 5) {
    return 0;
  }

  return makeUtcEpoch(Y, M, D, h, m, 0);
}

// Świeżość PAA oceniamy względem końca przedziału pomiarowego.
static bool isPaaFresh(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT || g_radDateEnd[cityIndex].length() < 16) return false;

  time_t t = parsePaaDateEndToEpoch(g_radDateEnd[cityIndex]);
  time_t now = time(nullptr);

  if (t <= 0 || now <= 0) return false;
  return difftime(now, t) >= 0 && difftime(now, t) <= RADIATION_MAX_FRESH_AGE_SEC;
}
// Świeżość BfS oceniamy względem końca przedziału pomiarowego.
static bool isBfsFresh(uint8_t cityIndex) {
  if (cityIndex >= CITY_COUNT || g_radDateEnd[cityIndex].length() < 19) return false;

  time_t t = parseIso8601ToEpoch(g_radDateEnd[cityIndex]);
  time_t now = time(nullptr);

  if (t <= 0 || now <= 0) return false;
  return difftime(now, t) >= 0 && difftime(now, t) <= RADIATION_MAX_FRESH_AGE_SEC;
}


// Najprostszy parser tokenów JSON, wystarczający dla małych odpowiedzi źródeł radiation.
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

static bool parseLastJsonStringField(const String &json, const char *token, String &outValue) {
  int pos = json.lastIndexOf(token);
  if (pos < 0) return false;
  pos += strlen(token);

  int end = json.indexOf('\"', pos);
  if (end < 0) return false;

  outValue = json.substring(pos, end);
  return true;
}

static bool parseLastJsonFloatField(const String &json, const char *token, float &outValue) {
  int pos = json.lastIndexOf(token);
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

// Pobiera PAA dla Kielc, zapisuje snapshot tekstowy i diagnostykę błędu.
static void updatePaaDoseForStation(const char *stationId, uint8_t cityIndex, const char *label) {
  if (cityIndex >= CITY_COUNT) {
    return;
  }

  g_radDoseDisplay[cityIndex] = "n/a";
  g_radDateEnd[cityIndex] = "";
  g_radiationLastError[0] = '\0';

  if (WiFi.status() != WL_CONNECTED) {
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "PAA no WiFi");
    return;
  }

  time_t nowUtc = time(nullptr);
  if (nowUtc <= 0) {
    log_e("PAA time not ready");
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "PAA no time");
    return;
  }

  // Tylko ostatnie 6h zamiast 48h
  time_t fromUtc = nowUtc - ((time_t)PAA_LOOKBACK_HOURS * 60 * 60);

  String dateFrom = formatIsoUtcDate(fromUtc);
  String dateTo   = formatIsoUtcDate(nowUtc);

  String url = "https://monitoring.paa.gov.pl/_api/maps/MapLayer/";
  url += PAA_LAYER_ID;
  url += "/DetailsTable/";
  url += PAA_TABLE_ID;
  url += "/Data/";
  url += stationId;
  url += "?dateFrom=";
  url += dateFrom;
  url += "&dateTo=";
  url += dateTo;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    log_e("PAA begin failed");
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "PAA begin");
    return;
  }

  http.setTimeout(PAA_HTTP_TIMEOUT_MS);
  http.useHTTP10(true);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Language", "pl-PL,pl;q=0.9,en;q=0.8");
  http.addHeader("Connection", "close");
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) ESP32WeatherStation/1.0");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    log_e("PAA HTTP error: %d", code);
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "PAA HTTP %d", code);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  float dose = -1.0f;
  String dateEnd;

  // if (!parseLastJsonFloatField(payload, "\"moc_dawki\":", dose)) {
  //   log_e("PAA moc_dawki not found");
  //   return;
  // }
  bool doseOk = parseLastJsonFloatField(payload, "\"moc_dawki\":", dose);
  bool dateOk = parseLastJsonStringField(payload, "\"date_end_str\":\"", dateEnd);

  if (!dateOk) {
    log_e("PAA date_end_str not found");
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "PAA no date len=%u", payload.length());
    return;
  }

  if (!doseOk) {
    log_e("PAA moc_dawki not found");
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "PAA no dose len=%u", payload.length());
    return;
  }

  g_radDateEnd[cityIndex] = dateEnd;

  char buf[24];
  snprintf(buf, sizeof(buf), "%.3f µSv/h", dose);
  g_radDoseDisplay[cityIndex] = String(buf);

  const time_t now = time(nullptr);
  const time_t measuredAt = parsePaaDateEndToEpoch(g_radDateEnd[cityIndex]);
  if (now > 0 && measuredAt > 0) {
    const long ageSec = (long)difftime(now, measuredAt);
    if (ageSec < 0) {
      snprintf(g_radiationLastError, sizeof(g_radiationLastError), "PAA future %lds", -ageSec);
    } else if ((unsigned long)ageSec > RADIATION_MAX_FRESH_AGE_SEC) {
      snprintf(g_radiationLastError, sizeof(g_radiationLastError), "PAA stale %lds", ageSec);
    }
  }

  FW_LOG_I("PAA %s = %s, end=%s", label, g_radDoseDisplay[cityIndex].c_str(), g_radDateEnd[cityIndex].c_str());
}

void updatePaaDoseForKielce() {
  updatePaaDoseForStation(PAA_KIELCE_STATION_ID, 0, "Kielce");
}

void updatePaaDoseForTarnowitz() {
  updatePaaDoseForStation(PAA_TARNOWITZ_STATION_ID, 2, "Tarnowitz");
}

// Pobiera BfS dla Dauchingen / St. Georgen, zapisuje snapshot tekstowy i diagnostykę.
void updateBfsDoseForDauchingen() {
  g_radDoseDisplay[1] = "n/a";
  g_radDateEnd[1] = "";
  g_radiationLastError[0] = '\0';
  String ts;

  if (WiFi.status() != WL_CONNECTED) {
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "BfS no WiFi");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  // Profesjonalnie: filtrujemy po stronie serwera
  // Najpierw po PLZ. To powinno ograniczyć payload do właściwej stacji/stacji.
  String url =
    "https://www.imis.bfs.de/ogc/opendata/ows"
    "?service=WFS"
    "&version=1.1.0"
    "&request=GetFeature"
    "&typeName=opendata:odlinfo_odl_1h_latest"
    "&outputFormat=application/json"
    "&propertyName=plz,name,value,unit,start_measure,end_measure"
    "&cql_filter=plz%3D%2778112%27";

  if (!http.begin(client, url)) {
    log_e("BfS begin failed");
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "BfS begin");
    return;
  }

  http.setTimeout(BFS_HTTP_TIMEOUT_MS);
  http.useHTTP10(true);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Language", "pl-PL,pl;q=0.9,en;q=0.8");
  http.addHeader("Connection", "close");
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) ESP32WeatherStation/1.0");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    log_e("BfS HTTP error: %d", code);
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "BfS HTTP %d", code);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();


  // Odpowiedź po filtrowaniu powinna być mała, więc czytamy pierwsze "value"
  float value = -1.0f;
  if (!parseJsonFloatField(payload, "\"value\":", value)) {
    log_e("BfS value not found");
    snprintf(g_radiationLastError, sizeof(g_radiationLastError), "BfS no value len=%u", payload.length());
    return;
  }
//fixme: mioejsce?

if (parseLastJsonStringField(payload, "\"end_measure\":\"", ts)) {//timestamp
  g_radDateEnd[1] = ts;
} else {
  g_radDateEnd[1] = "";
  snprintf(g_radiationLastError, sizeof(g_radiationLastError), "BfS no end len=%u", payload.length());
  return;
}

  char buf[24];
  snprintf(buf, sizeof(buf), "%.3f µSv/h", value);
  g_radDoseDisplay[1] = String(buf);

  const time_t now = time(nullptr);
  const time_t measuredAt = parseIso8601ToEpoch(g_radDateEnd[1]);
  if (now > 0 && measuredAt > 0) {
    const long ageSec = (long)difftime(now, measuredAt);
    if (ageSec < 0) {
      snprintf(g_radiationLastError, sizeof(g_radiationLastError), "BfS future %lds", -ageSec);
    } else if ((unsigned long)ageSec > RADIATION_MAX_FRESH_AGE_SEC) {
      snprintf(g_radiationLastError, sizeof(g_radiationLastError), "BfS stale %lds", ageSec);
    }
  }

  FW_LOG_I("BfS St. Georgen = %s", g_radDoseDisplay[1].c_str());
}


// Aktualizuje radiation dla miasta, a potem wyznacza wspólny wynik do UI.
void updateRadiationForCityIndex(uint8_t cityIndex) {
  g_radActiveCityIndex = cityIndex;

  if (cityIndex == 0) {
    FW_LOG_I("[NET] RAD start Kielce");
    updatePaaDoseForKielce();
  } else if (cityIndex == 1) {
    FW_LOG_I("[NET] RAD start Dauchingen");
    updateBfsDoseForDauchingen();
  } else if (cityIndex == 2) {
    FW_LOG_I("[NET] RAD start Tarnowitz");
    updatePaaDoseForTarnowitz();
  }

  selectRadiationSource();
  updateRadiationTrend();
  updateRadiationStatus();
  // updateRadiationLed(g_radStatus);

  if (cityIndex < CITY_COUNT) {
    FW_LOG_I("[NET] RAD done city=%u value=%s",
             (unsigned)cityIndex,
             g_radDoseDisplay[cityIndex].c_str());
  }
}

enum RadiationSource {
  RAD_NONE,
  RAD_PAA,
  RAD_BFS
};

RadiationSource g_radSource = RAD_NONE;
int32_t g_radDose_x1000 = -1; // µSv/h * 1000

// Konwersja tekstu dawki do skali x1000 używanej przez trend, status i kolor.
int32_t parseDoseToInt(const String &s) {
  float f = s.toFloat();
  return (int32_t)(f * 1000.0f);
}

// Mapuje dawkę i świeżość na kolor zgodny z przyjętą skalą radiation.
static uint16_t radiationColorFromDoseAndFreshness(int32_t dose_x1000, bool fresh, bool warnIfFresh) {
  (void)warnIfFresh;

  if (!fresh || dose_x1000 < 0) {
    return TFT_DARKGREY;
  }

  // dose_x1000 is uSv/h * 1000, so it is numerically equal to nSv/h.
  if (dose_x1000 > 2000) {
    return rgb565(102, 29, 175);
  }

  if (dose_x1000 > 500) {
    return rgb565(65, 29, 175);
  }

  if (dose_x1000 > 300) {
    return rgb565(29, 65, 175);
  }

  if (dose_x1000 > 200) {
    return rgb565(29, 102, 175);
  }

  if (dose_x1000 > 100) {
    return rgb565(29, 139, 175);
  }

  return rgb565(29, 175, 175);
}


uint16_t getPaaColor(void) {
  const uint8_t cityIndex = (g_radActiveCityIndex == 2) ? 2 : 0;
  bool fresh = isPaaFresh(cityIndex);
  int32_t dose = parseDoseToInt(g_radDoseDisplay[cityIndex]);
  return radiationColorFromDoseAndFreshness(dose, fresh, false);
}

uint16_t getBfsColor(void) {
  bool fresh = isBfsFresh(1);
  int32_t dose = parseDoseToInt(g_radDoseDisplay[1]);
  return radiationColorFromDoseAndFreshness(dose, fresh, true);
}



// Wybiera najlepsze świeże źródło: preferowane PAA, fallback BfS.
static void selectRadiationSource(void) {
  bool paaFresh = (g_radActiveCityIndex == 0 || g_radActiveCityIndex == 2) &&
                  isPaaFresh(g_radActiveCityIndex);
  bool bfsFresh = (g_radActiveCityIndex == 1) && isBfsFresh(1);

  static RadiationSource lastSource = RAD_NONE;

if (g_radSource != lastSource) {
  g_prevDose = -1; // reset trend
  lastSource = g_radSource;
}

  if (g_radActiveCityIndex == 0 || g_radActiveCityIndex == 2) {
    g_radSource = RAD_PAA;
    g_radDose_x1000 = paaFresh ? parseDoseToInt(g_radDoseDisplay[g_radActiveCityIndex]) : -1;
  } else if (g_radActiveCityIndex == 1) {
    g_radSource = RAD_BFS;
    g_radDose_x1000 = bfsFresh ? parseDoseToInt(g_radDoseDisplay[1]) : -1;
  } else {
    g_radSource = RAD_NONE;
    g_radDose_x1000 = -1;
  }
}

// Trend liczony jest względem poprzedniej dawki tego samego źródła.
void updateRadiationTrend() {
  if (g_prevDose < 0 || g_radDose_x1000 < 0) {
    g_trend = 0;
  } else {
    int32_t diff = g_radDose_x1000 - g_prevDose;

    if (diff > 10) g_trend = +1;        // >0.01 µSv
    else if (diff < -10) g_trend = -1;
    else g_trend = 0;
  }

  g_prevDose = g_radDose_x1000;
}

// Status upraszcza wynik radiation do poziomów używanych przez UI i LED.
void updateRadiationStatus() {
  if (g_radDose_x1000 < 0) {
    g_radStatus = RAD_STATUS_NO_DATA;
    return;
  }

  if (g_radDose_x1000 > 300) { // 0.3 µSv/h
    g_radStatus = RAD_STATUS_ALARM;
  }
  else if (g_radSource == RAD_BFS) {
    g_radStatus = RAD_STATUS_WARN;
  }
  else {
    g_radStatus = RAD_STATUS_OK;
  }
}


uint16_t getRadiationColor() {
  switch (g_radSource) {
    case RAD_PAA:
      return getPaaColor();
    case RAD_BFS:
      return getBfsColor();
    default:
      return TFT_DARKGREY;
  }
}


const char* getTrendSymbol() {
  if (g_trend > 0) return "↑";
  if (g_trend < 0) return "↓";
  return "→";
}


RadiationStatus getRadiationStatus(void) {
  return g_radStatus;
}

RadiationSourceType getRadiationSourceType(void) {
  switch (g_radSource) {
    case RAD_PAA:
      return RAD_SOURCE_PRIMARY;
    case RAD_BFS:
      return RAD_SOURCE_BACKUP;
    default:
      return RAD_SOURCE_NONE;
  }
}

bool isRadiationUsingBackupSource(void) {
  return getRadiationSourceType() == RAD_SOURCE_BACKUP;
}


int8_t getRadiationTrend(void) {
  return g_trend;
}

const char* getRadiationLastError(void) {
  return g_radiationLastError;
}

time_t parseIso8601ToEpoch(const String &ts) {
  int Y, M, D, h, m, s;
  if (sscanf(ts.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) != 6) {
    return 0;
  }

  return makeUtcEpoch(Y, M, D, h, m, s);
}
