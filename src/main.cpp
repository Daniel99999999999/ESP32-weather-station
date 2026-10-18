#include <Arduino.h>

#include <TFT_eSPI.h>
#include "GfxUi.h"
#include <TJpg_Decoder.h>

#include <LittleFS.h>
#include "persistence.h"
#include "gt911.h"

#include "settings.h"
#include "app_config.h"
#include "app_types.h"
#include "app_state.h"

#include "ambient.h"
#include "ambient_service.h"
#include "led_service.h"
#include "touch_service.h"
#include "aqi.h"
#include "aqi_service.h"
#include "current_weather_service.h"
#include "forecast_service.h"
#include "radiation_job_service.h"
#include "radiation_service.h"
#include "display.h"
#include "util.h"

#include "connectivity.h"

// Timestamp ostatniej udanej lub podjętej synchronizacji czasu.
unsigned long lastTimeSyncMillis = 0;

// Globalny stan runtime używany przez prostą pętlę firmware bez RTOS tasków użytkownika.
uint16_t ambientFiltered = 0;
bool isNightMode = false;
bool g_wifiFallbackDhcp = false;
bool g_wifiUsingStaticIp = false;
uint8_t g_wifiProfileIndex = 0xFF;
RuntimeWiFiConfig g_runtimeWiFiConfig = {};
volatile bool g_otaInProgress = false;
volatile uint8_t g_otaProgressPercent = 0;
bool g_timeSynced = false;
AppPage g_currentPage = PAGE_MAIN;
bool g_touchWasActive = false;
bool g_touchSampleValid = false;
uint16_t g_touchX = 0;
uint16_t g_touchY = 0;
TFT_eSPI tft;
TAMC_GT911* ts = nullptr;
GfxUi* ui = nullptr;
uint8_t g_activeCityIndex = 0;

// Cache view-modelu per miasto. UI czyta z nich gotowe dane zamiast pytać
// bezpośrednio serwisy sieciowe w trakcie renderowania.
CurrentWeatherViewState g_weatherCache[CITY_COUNT] = {};
ForecastViewState g_forecastCache[CITY_COUNT] = {};
AqiViewState g_aqiCache[CITY_COUNT] = {};

const int16_t centerWidth = TFT_WIDTH / 2;

// Obszary dotykowe wykorzystywane przez debug i overlay nawigacyjny.
const RectangleDef NAV_KIELCE_BUTTON = {22, 150, 124, 34};
const RectangleDef NAV_DAU_BUTTON = {174, 150, 124, 34};
const RectangleDef NAV_TARNOWITZ_BUTTON = {22, 198, 124, 34};
const RectangleDef NAV_GRAPH_BUTTON = {174, 198, 124, 34};
const RectangleDef NAV_DEBUG_BUTTON = {22, 246, 124, 34};
const RectangleDef NAV_MENU_BUTTON = {174, 246, 124, 34};
const RectangleDef MAIN_NIGHT_TOGGLE_BUTTON = {6, 10, 42, 18};
const uint32_t NAV_OVERLAY_LONG_PRESS_MS = 650U;
const uint16_t NAV_OVERLAY_MOVE_TOLERANCE = 18U;
const uint16_t COLOR_KIELCE = TFT_CYAN;
const uint16_t COLOR_DAUCHINGEN = TFT_YELLOW;
const uint16_t COLOR_TARNOWITZ = TFT_MAGENTA;
const uint16_t COLOR_GRAPH = TFT_GREEN;
const uint16_t COLOR_DEBUG = TFT_ORANGE;
const uint16_t COLOR_MENU = 0xd7e0;
constexpr uint8_t DEBUG_LINE_COUNT = 16;
uint16_t g_cityUiColors[CITY_COUNT] = {COLOR_KIELCE, COLOR_DAUCHINGEN, COLOR_TARNOWITZ};

uint16_t getCityUiColor(uint8_t cityIndex) {
  return (cityIndex < CITY_COUNT) ? g_cityUiColors[cityIndex] : TFT_WHITE;
}

const char* getCityUiName(uint8_t cityIndex) {
  switch (cityIndex) {
    case 0: return "Kielce";
    case 1: return "Dauchingen";
    case 2: return "Tarnowitz";
    default: return "?";
  }
}

enum MenuScreen : uint8_t {
  MENU_HOME = 0,
  MENU_WIFI,
  MENU_VIEW,
  MENU_BRIGHTNESS,
  MENU_COLORS,
  MENU_RGB,
  MENU_TOUCH
};

enum LcdBrightnessMode : uint8_t {
  LCD_MODE_AUTO = 0,
  LCD_MODE_DAY,
  LCD_MODE_NIGHT
};

enum MenuWifiScreen : uint8_t {
  MENU_WIFI_HOME = 0,
  MENU_WIFI_NETWORKS,
  MENU_WIFI_PASSWORD,
  MENU_WIFI_IP
};

enum MenuWifiIpField : uint8_t {
  MENU_WIFI_IP_LOCAL = 0,
  MENU_WIFI_IP_SUBNET,
  MENU_WIFI_IP_GATEWAY,
  MENU_WIFI_IP_DNS
};

struct MenuWifiScanItem {
  char ssid[33];
  int32_t rssi;
  bool secure;
};

struct MenuDraftState {
  uint8_t wifiProfileIndex;
  char wifiSsid[33];
  bool wifiUseDhcp;
  char password[64];
  IPAddress localIp;
  IPAddress subnet;
  IPAddress gateway;
  IPAddress dns;
  char localIpText[16];
  char subnetText[16];
  char gatewayText[16];
  char dnsText[16];
  bool showSeconds;
  bool showForecast;
  bool showSun;
  bool showAqi;
  bool showRad;
  uint8_t brightnessMode;
  uint8_t rgbR;
  uint8_t rgbG;
  uint8_t rgbB;
  uint16_t cityColors[CITY_COUNT];
};

const RectangleDef MENU_BACK_BUTTON = {12, 10, 74, 26};
const RectangleDef MENU_HOME_WIFI_BUTTON = {24, 72, 128, 58};
const RectangleDef MENU_HOME_VIEW_BUTTON = {168, 72, 128, 58};
const RectangleDef MENU_HOME_BRIGHTNESS_BUTTON = {24, 146, 128, 58};
const RectangleDef MENU_HOME_COLORS_BUTTON = {168, 146, 128, 58};
const RectangleDef MENU_HOME_RGB_BUTTON = {24, 220, 128, 58};
const RectangleDef MENU_HOME_TOUCH_BUTTON = {168, 220, 128, 58};
const RectangleDef MENU_VIEW_SECONDS_BOX = {26, 88, 22, 22};
const RectangleDef MENU_VIEW_FORECAST_BOX = {26, 128, 22, 22};
const RectangleDef MENU_VIEW_SUN_BOX = {26, 168, 22, 22};
const RectangleDef MENU_VIEW_AQI_BOX = {26, 208, 22, 22};
const RectangleDef MENU_VIEW_RAD_BOX = {26, 248, 22, 22};
const RectangleDef MENU_BRIGHT_AUTO_BOX = {26, 96, 22, 22};
const RectangleDef MENU_BRIGHT_DAY_BOX = {26, 146, 22, 22};
const RectangleDef MENU_BRIGHT_NIGHT_BOX = {26, 196, 22, 22};
const RectangleDef MENU_COLORS_CITY0_BOX = {22, 82, 22, 22};
const RectangleDef MENU_COLORS_CITY1_BOX = {160, 82, 22, 22};
const RectangleDef MENU_COLORS_CITY2_BOX = {22, 120, 22, 22};
const RectangleDef MENU_COLORS_DEFAULTS_BUTTON = {56, 418, 208, 28};
const RectangleDef MENU_WIFI_PROFILE0 = {18, 262, 284, 30};
const RectangleDef MENU_WIFI_PROFILE1 = {18, 298, 284, 30};
const RectangleDef MENU_WIFI_HOME_NETWORKS_BUTTON = {18, 278, 88, 30};
const RectangleDef MENU_WIFI_HOME_PASSWORD_BUTTON = {116, 278, 88, 30};
const RectangleDef MENU_WIFI_HOME_IP_BUTTON = {214, 278, 88, 30};
const RectangleDef MENU_WIFI_MODE_TOGGLE = {108, 132, 126, 26};
const RectangleDef MENU_WIFI_NETWORK_RESCAN_BUTTON = {230, 10, 78, 26};
const RectangleDef MENU_WIFI_PASSWORD_SHOW_BUTTON = {18, 118, 88, 26};
const RectangleDef MENU_WIFI_PASSWORD_CLEAR_BUTTON = {116, 118, 88, 26};
const RectangleDef MENU_WIFI_PASSWORD_DONE_BUTTON = {214, 118, 88, 26};
const RectangleDef MENU_WIFI_IP_LOCAL_FIELD = {90, 92, 210, 24};
const RectangleDef MENU_WIFI_IP_SUBNET_FIELD = {90, 126, 210, 24};
const RectangleDef MENU_WIFI_IP_GATEWAY_FIELD = {90, 160, 210, 24};
const RectangleDef MENU_WIFI_IP_DNS_FIELD = {90, 194, 210, 24};
const RectangleDef MENU_WIFI_IP_DHCP_TOGGLE = {90, 56, 120, 26};
const RectangleDef MENU_WIFI_IP_DONE_BUTTON = {226, 56, 82, 26};
const RectangleDef MENU_RGB_SLIDER_R = {78, 96, 28, 210};
const RectangleDef MENU_RGB_SLIDER_G = {146, 96, 28, 210};
const RectangleDef MENU_RGB_SLIDER_B = {214, 96, 28, 210};

// Cache renderu sekcji UI. Każda sekcja porównuje swój ostatni klucz,
// żeby uniknąć pełnych repaintów przy nieistotnych zmianach stanu.
uint32_t g_lastClockRenderMs = 0;
uint32_t g_lastTimeSyncAttemptMs = 0;
uint32_t g_lastDebugRenderMs = 0;
uint32_t g_nextNetworkJobSlotMs = 0;
uint32_t g_lastFullPageRenderMs = 0;
uint8_t g_nextNetworkJobIndex = 0;
char g_lastWeatherRenderKey[256] = "";
char g_lastForecastRenderKey[512] = "";
char g_mainDescRenderKey[128] = "";
char g_mainIconRenderKey[96] = "";
char g_mainTempRenderKey[128] = "";
char g_mainMetricsRenderKey[128] = "";
char g_mainForecastDayKeys[NUMBER_OF_DAY_FORECASTS][96] = {};
char g_mainSunRenderKey[64] = "";
char g_mainRadRenderKey[128] = "";
char g_mainAqiRenderKey[128] = "";
char g_lastClockDateKey[64] = "";
char g_lastClockTimeKey[16] = "";
char g_lastClockSecondsKey[4] = "";
bool g_lastClockColonVisible = true;
bool g_lastClockShowSeconds = false;
uint8_t g_lastNightOverrideBadgeState = 0xFF;
bool g_lastNightOverrideBadgeValid = false;
char g_lastDebugLines[DEBUG_LINE_COUNT][160] = {};
uint32_t g_lastDebugLdrSampleMs = 0;
char g_debugLdrLine[64] = "";
bool g_smoothFontLoaded = false;
String g_loadedSmoothFont = "";
bool g_navOverlayVisible = false;
bool g_navLongPressTriggered = false;
bool g_navOverlayTapArmed = false;
bool g_touchConsumeUntilRelease = false;
LcdBrightnessMode g_lcdBrightnessMode = LCD_MODE_AUTO;
uint32_t g_touchPressStartMs = 0;
uint16_t g_touchPressStartX = 0;
uint16_t g_touchPressStartY = 0;
int8_t g_overlayPressedButton = -1;
MenuScreen g_menuScreen = MENU_HOME;
MenuDraftState g_menuDraft = {};

// Historia temperatur jest trzymana jako bufor kołowy per miasto.
// Pierwsze 24 próbki zbieramy przy każdym nowym odczycie pogody,
// później dokładamy już tylko próbki godzinowe.
float g_tempHistory[CITY_COUNT][TEMP_HISTORY_SIZE] = {};
uint32_t g_tempHistorySampleTs[CITY_COUNT][TEMP_HISTORY_SIZE] = {};
uint8_t g_tempHistoryHead[CITY_COUNT] = {};
uint8_t g_tempHistoryCount[CITY_COUNT] = {};
uint32_t g_tempHistoryLastSampleMs[CITY_COUNT] = {};
uint32_t g_tempHistoryLastCaptureStamp[CITY_COUNT] = {};
char g_lastStatsRenderKey[512] = "";
char g_lastMenuRenderKey[64] = "";
char g_lastMenuWifiKey[128] = "";
char g_lastMenuViewKey[16] = "";
char g_lastMenuColorsKey[64] = "";
char g_lastMenuRgbKey[32] = "";
char g_lastMenuTouchKey[24] = "";
int16_t g_lastMenuTouchCursorX = -1;
int16_t g_lastMenuTouchCursorY = -1;
bool g_lastMenuTouchCursorVisible = false;
uint8_t g_lastMenuScreenRendered = 0xFF;
uint8_t g_lastMenuWifiScreenRendered = 0xFF;
int16_t g_menuPressedTarget = 0;
MenuWifiScreen g_menuWifiScreen = MENU_WIFI_HOME;
MenuWifiIpField g_menuWifiIpField = MENU_WIFI_IP_LOCAL;
bool g_menuWifiShowPassword = false;
bool g_menuWifiKeyboardUpper = false;
char g_menuWifiPasswordInput[64] = "";
bool g_menuWifiPasswordEdited = false;
MenuWifiScanItem g_menuWifiScan[8] = {};
uint8_t g_menuWifiScanCount = 0;
int16_t g_menuWifiPressedCode = 0;
bool g_menuWifiScanRequested = false;
bool g_menuWifiScanInProgress = false;
uint32_t g_menuWifiScanStartedMs = 0;
uint8_t g_menuWifiScanAttempt = 0;
char g_menuWifiStatusText[48] = "";
bool g_menuWifiApplyFailed = false;
uint8_t g_menuColorsSelectedCity = 0;
char g_menuColorsStatusText[32] = "";
uint32_t g_menuDirtyFlags = 0xFFFFFFFFUL;
uint32_t g_menuWifiDirtyFlags = 0xFFFFFFFFUL;

enum MenuDirtyFlag : uint32_t {
  MENU_DIRTY_FULL         = 1UL << 0,
  MENU_DIRTY_HOME_STATIC  = 1UL << 1,
  MENU_DIRTY_HOME_DYNAMIC = 1UL << 2,
  MENU_DIRTY_WIFI_STATIC  = 1UL << 3,
  MENU_DIRTY_WIFI_DYNAMIC = 1UL << 4,
  MENU_DIRTY_VIEW_STATIC  = 1UL << 5,
  MENU_DIRTY_VIEW_DYNAMIC = 1UL << 6,
  MENU_DIRTY_BRIGHT_STATIC = 1UL << 7,
  MENU_DIRTY_BRIGHT_DYNAMIC= 1UL << 8,
  MENU_DIRTY_COLORS_STATIC = 1UL << 9,
  MENU_DIRTY_COLORS_DYNAMIC= 1UL << 10,
  MENU_DIRTY_RGB_STATIC   = 1UL << 11,
  MENU_DIRTY_RGB_DYNAMIC  = 1UL << 12,
  MENU_DIRTY_TOUCH_STATIC = 1UL << 13,
  MENU_DIRTY_TOUCH_DYNAMIC= 1UL << 14
};

enum MenuWifiDirtyFlag : uint32_t {
  MENU_WIFI_DIRTY_OVERVIEW      = 1UL << 0,
  MENU_WIFI_DIRTY_NETWORKS      = 1UL << 1,
  MENU_WIFI_DIRTY_PASSWORD_TEXT = 1UL << 2,
  MENU_WIFI_DIRTY_PASSWORD_BTNS = 1UL << 3,
  MENU_WIFI_DIRTY_PASSWORD_KEYS = 1UL << 4,
  MENU_WIFI_DIRTY_IP_TOP        = 1UL << 5,
  MENU_WIFI_DIRTY_IP_FIELDS     = 1UL << 6,
  MENU_WIFI_DIRTY_IP_NUMPAD     = 1UL << 7
};

enum MenuWifiApplyResult : uint8_t {
  MENU_WIFI_APPLY_OK = 0,
  MENU_WIFI_APPLY_EMPTY,
  MENU_WIFI_APPLY_NO_SSID,
  MENU_WIFI_APPLY_AUTH,
  MENU_WIFI_APPLY_DISCONNECTED,
  MENU_WIFI_APPLY_DNS,
  MENU_WIFI_APPLY_UNKNOWN
};

static const char *FONT_VLW_UI_14 = "fonts/ui_14";
static const char *FONT_VLW_UI_16 = "fonts/ui_16";
static const char *FONT_VLW_UI_18 = "fonts/ui_18";
static const char *FONT_VLW_UI_20 = "fonts/ui_20";
static const char *FONT_VLW_UI_24 = "fonts/ui_24";
static const char *FONT_VLW_CLOCK_36 = "fonts/clock_36";
static const char *FONT_VLW_TEMP_48 = "fonts/temp_48";

// Etapy inicjalizacji bootu.
void initJpegDecoder();
void repaint();
bool pushImageToTft(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);
void drawProgress(const char *text, int8_t percentage);
void syncTime(void);
void clearMenuRenderCaches();
void invalidateMenuFull();
void invalidateMenuScreen(MenuScreen screen, bool redrawStatic, bool redrawDynamic);
void invalidateCurrentMenuScreen(bool redrawStatic, bool redrawDynamic);
void initBootCore();
void initHardwareBase();
void initRuntimeState();
void initDisplayStack();
void showBootScreen();
void startConnectivityServices();
void renderRescueMainScreen();
void renderRescueStatsScreen();
void renderRescueDebugScreen();
void renderRescueMenuScreen();
void updateRescueClock();
void updateRescueWeather(bool force);
void updateRescueForecast(bool force);
void drawMainNightOverrideBadge(bool force = false);
void toggleMainNightOverride();
void updateRescueStats(bool force);
void updateRescueDebug(bool force);
void updateRescueMenu(bool force);
void serviceRescueUi();
void serviceOta();
void serviceAmbientAndWake(uint32_t nowMs);
void serviceRescueTouch(uint32_t nowMs);
void serviceNetworkJobs(uint32_t nowMs);
void serviceMenuWifiScan(uint32_t nowMs);
void serviceTimeSync(uint32_t nowMs);
void serviceCurrentPage(uint32_t nowMs);
void renderCurrentRescuePage();
void drawOverlayButton(const RectangleDef &rect, const char *label, uint16_t baseColor, bool active, bool pressed);
void renderNavOverlay();
void hideNavOverlay();
void showNavOverlay();
bool handleOverlayTap(uint16_t x, uint16_t y);
int8_t getOverlayButtonAt(uint16_t x, uint16_t y);
void initMenuDraftState();
void drawMenuHeader(const char *title);
void drawMenuCloseButton(const char *label);
void drawMenuCard(const RectangleDef &rect, const char *title, uint16_t color);
void drawMenuCheckbox(const RectangleDef &rect, const char *label, bool checked, uint16_t color);
void drawMenuCheckboxState(const RectangleDef &rect, bool checked, uint16_t color);
void applyBrightnessMode(uint8_t mode);
void drawMenuRssiBars(int16_t x, int16_t y, int32_t rssi, uint16_t color);
void drawMenuLockIcon(int16_t x, int16_t y, uint16_t color);
void drawMenuIpLine(const char *label, const IPAddress &ip, int16_t y);
void updateMenuRgbFromTouch(uint16_t touchY);
void redrawMenuTouchGridRegion(int16_t x, int16_t y, int16_t w, int16_t h);
bool isPointInMenuTouchGrid(uint16_t x, uint16_t y);
void loadMenuDraftFromProfile(uint8_t profileIndex);
void loadMenuDraftFromRuntimeConfig();
void storeMenuDraftToRuntimeConfig();
bool doesMenuDraftDifferFromRuntimeConfig();
MenuWifiApplyResult applyMenuWifiDraftToRuntime();
const char* getMenuWifiApplyErrorText(MenuWifiApplyResult result);
void formatIpAddressToText(const IPAddress &ip, char *out, size_t outSize);
bool parseIpAddressFromText(const char *text, IPAddress &out);
void scanMenuWifiNetworks();
void drawMenuButton(const RectangleDef &rect, const char *label, uint16_t color, bool active = false, bool pressed = false);
void drawMenuWifiIpField(const RectangleDef &rect, const char *label, const char *value, bool active, bool enabled);
void drawMenuWifiKeyboard();
void drawMenuWifiNumpad();
int16_t getMenuWifiKeyboardKeyAt(uint16_t x, uint16_t y, char *outChar, bool *isAction, const char **action);
int16_t getMenuWifiNumpadKeyAt(uint16_t x, uint16_t y, const char **action);
void decodeMenuWifiKeyboardCode(int16_t code, char *outChar, bool *isAction, const char **action);
const char* decodeMenuWifiNumpadCode(int16_t code);
char* getActiveMenuWifiIpText();
void requestMenuWifiScan(bool clearPreviousResults);
void setMenuWifiStatus(const char *status);
void resetTouchInteractionState();
int16_t getMenuPressedTargetAt(uint16_t x, uint16_t y);
void handleMenuPressedTargetRelease(int16_t target);
void populateMenuWifiSavedList();
void renderMenuWifiOverview(bool redrawStatic);
void renderMenuWifiNetworks(bool redrawStatic);
void renderMenuWifiPassword(bool redrawStatic);
void renderMenuWifiIp(bool redrawStatic);
int16_t getMenuWifiPressedCodeAt(uint16_t x, uint16_t y);
void handleMenuWifiPressedRelease();
void sampleTemperatureHistory(uint32_t nowMs);
void setActiveCity(uint8_t cityIndex, bool forceRender);
void navigateToPage(AppPage page, int8_t cityIndex);
void syncCityCacheFromAppState();
const CurrentWeatherViewState& getDisplayedWeather();
const ForecastViewState& getDisplayedForecast();
const AqiViewState& getDisplayedAqi();
void advanceRescueScreen();
const char* getForecastIconName(uint16_t weatherId);
const char* getMainWeatherIconName(uint16_t weatherId);
const char* getMainWeatherIconNameByOwmIcon(const char *owmIcon, uint16_t weatherId);
void rescueDrawSeparator(uint16_t y);
void rescueDrawAqiRowAligned(int labelX, int valueX, int squareX, int y,
                             const char *label, float value, uint16_t color, uint8_t decimals);
void drawMiniTempArrow(int16_t x, int16_t y, bool up, uint16_t color);
bool loadSmoothFontCached(const char *fontPath);
void unloadSmoothFontCached();
void drawSmoothString(const String &text, int32_t x, int32_t y, uint8_t datum, uint16_t color);
void drawSmoothString(const char *text, int32_t x, int32_t y, uint8_t datum, uint16_t color);
int16_t getSmoothTextWidth(const String &text);
int16_t getSmoothTextWidth(const char *text);
bool drawTextWithSmoothFont(const char *fontPath,
                            const String &text,
                            int32_t x,
                            int32_t y,
                            uint8_t datum,
                            uint16_t color);
bool drawTextWithSmoothFont(const char *fontPath,
                            const char *text,
                            int32_t x,
                            int32_t y,
                            uint8_t datum,
                            uint16_t color);
int16_t measureTextWithSmoothFont(const char *fontPath, const String &text);
int16_t measureTextWithSmoothFont(const char *fontPath, const char *text);
int16_t getSmoothFontHeight(const char *fontPath);
void drawDebugLineIfChanged(uint8_t index, int16_t y, uint8_t fontSize, const char *text, bool force);
void drawDebugSegmentLineIfChanged(uint8_t index, int16_t y, uint8_t fontSize, const String *segments, const uint16_t *colors, uint8_t segmentCount, const char *cacheKey, bool force);

void setup(void) {
  initBootCore();
  initHardwareBase();
  initRuntimeState();
  initDisplayStack();
  showBootScreen();
  startConnectivityServices();
}

// Główna pętla utrzymuje firmware responsywny: OTA, UI i krótki yield.
void loop(void) {
  serviceOta();
  serviceRescueUi();
  delay(10);
}

void initBootCore() {
  Serial.begin(115200);
  delay(50);
  logBanner();
  logMemoryStats();
}

// Konfiguruje sprzęt bazowy, który musi działać jeszcze przed startem sieci.
void initHardwareBase() {
  setupPwmOutputs();
  setLcdBrightness(LCD_BRIGHT_DAY);
  setBlueLedBrightness(LED_BRIGHT_DAY);

  setupAmbientSensor();
  ambientServiceInit();
  touchServiceInit();

  ts = new TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TFT_WIDTH, TFT_HEIGHT);
  if (ts != nullptr) {
    initTouchScreen(ts);
  } else {
    log_e("Touch allocation failed");
  }
}

// Czyści stan runtime, cache renderu i bufory historii przed wejściem w normalny tryb pracy.
void initRuntimeState() {
  ambientFiltered = 0;
  isNightMode = false;
  g_wifiFallbackDhcp = false;
  g_wifiUsingStaticIp = false;
  g_wifiProfileIndex = 0xFF;
  g_runtimeWiFiConfig = {};
  g_otaInProgress = false;
  g_otaProgressPercent = 0;
  g_timeSynced = false;
  g_touchWasActive = false;
  g_touchSampleValid = false;
  g_touchX = 0;
  g_touchY = 0;
  lastTimeSyncMillis = 0;
  g_lastClockRenderMs = 0;
  g_lastTimeSyncAttemptMs = 0;
  g_lastDebugRenderMs = 0;
  g_nextNetworkJobSlotMs = 0;
  g_lastFullPageRenderMs = 0;
  g_nextNetworkJobIndex = 0;
  g_activeCityIndex = 0;
  g_lastWeatherRenderKey[0] = '\0';
  g_lastForecastRenderKey[0] = '\0';
  g_mainDescRenderKey[0] = '\0';
  g_mainIconRenderKey[0] = '\0';
  g_mainTempRenderKey[0] = '\0';
  g_mainMetricsRenderKey[0] = '\0';
  g_mainSunRenderKey[0] = '\0';
  g_mainRadRenderKey[0] = '\0';
  g_mainAqiRenderKey[0] = '\0';
  for (uint8_t i = 0; i < NUMBER_OF_DAY_FORECASTS; i++) {
    g_mainForecastDayKeys[i][0] = '\0';
  }
  g_lastClockDateKey[0] = '\0';
  g_lastClockTimeKey[0] = '\0';
  g_lastClockSecondsKey[0] = '\0';
  g_lastClockColonVisible = true;
  g_lastClockShowSeconds = false;
  g_lastNightOverrideBadgeValid = false;
  g_lcdBrightnessMode = LCD_MODE_AUTO;
  for (uint8_t i = 0; i < DEBUG_LINE_COUNT; i++) {
    g_lastDebugLines[i][0] = '\0';
  }
  g_lastDebugLdrSampleMs = 0;
  g_debugLdrLine[0] = '\0';
  g_lastStatsRenderKey[0] = '\0';
  g_lastMenuRenderKey[0] = '\0';
  g_lastMenuWifiKey[0] = '\0';
  g_lastMenuViewKey[0] = '\0';
  g_lastMenuRgbKey[0] = '\0';
  g_lastMenuTouchKey[0] = '\0';
  g_lastMenuTouchCursorX = -1;
  g_lastMenuTouchCursorY = -1;
  g_lastMenuTouchCursorVisible = false;
  g_lastMenuScreenRendered = 0xFF;
  g_lastMenuWifiScreenRendered = 0xFF;
  g_navOverlayVisible = false;
  resetTouchInteractionState();
  g_menuWifiScanRequested = false;
  g_menuWifiScanInProgress = false;
  g_menuWifiScanStartedMs = 0;
  g_menuWifiScanAttempt = 0;
  g_menuWifiStatusText[0] = '\0';
  for (uint8_t i = 0; i < CITY_COUNT; i++) {
    g_weatherCache[i] = {};
    g_weatherCache[i].cityIndex = i;
    strncpy(g_weatherCache[i].cityName, CITY_NAMES[i], sizeof(g_weatherCache[i].cityName) - 1);
    g_weatherCache[i].fetchState = FETCH_NONE;
    g_forecastCache[i] = {};
    g_forecastCache[i].cityIndex = i;
    g_forecastCache[i].fetchState = FETCH_NONE;
    g_aqiCache[i] = {};
    g_aqiCache[i].cityIndex = i;
    g_aqiCache[i].fetchState = FETCH_NONE;
    g_aqiCache[i].aqi = -1;
    g_aqiCache[i].pm25 = -1;
    g_aqiCache[i].pm10 = -1;
    g_tempHistoryHead[i] = 0;
    g_tempHistoryCount[i] = 0;
    g_tempHistoryLastSampleMs[i] = 0;
    g_tempHistoryLastCaptureStamp[i] = 0;
    for (uint8_t j = 0; j < TEMP_HISTORY_SIZE; j++) {
      g_tempHistory[i][j] = NAN;
      g_tempHistorySampleTs[i][j] = 0;
    }
  }
  aqiServiceInit();
  currentWeatherServiceInit();
  forecastServiceInit();
  radiationJobServiceInit();
  appStateInit();
  setActiveCity(0, false);
  initMenuDraftState();
}

void initDisplayStack() {
  initJpegDecoder();
  initTft(&tft);
  initFileSystem();

  ui = new GfxUi(&tft);
  if (ui == nullptr) {
    log_e("UI allocation failed");
  }
}

void showBootScreen() {
  repaint();
}

void startConnectivityServices() {
  drawProgress("Szukam WiFi...", 10);
  startWiFi();
  initMenuDraftState();

  if (WiFi.status() == WL_CONNECTED) {
    drawProgress("Uruchamiam OTA...", 45);
    setupOTA();

    drawProgress("Ustawiam zegarek...", 75);
    syncTime();
  } else {
    log_w("Brak WiFi, pomijam OTA/NTP");
  }

  drawProgress("Zakończono", 100);
  delay(250);
  unloadSmoothFontCached();
  renderCurrentRescuePage();
}


bool loadSmoothFontCached(const char *fontPath) {
  if (fontPath == nullptr) {
    return false;
  }

  const String fsPath = String("/") + fontPath + ".vlw";
  if (!LittleFS.exists(fsPath)) {
    return false;
  }

  if (g_smoothFontLoaded && g_loadedSmoothFont == fontPath) {
    return true;
  }

  if (g_smoothFontLoaded) {
    tft.unloadFont();
    g_smoothFontLoaded = false;
    g_loadedSmoothFont = "";
  }

  tft.loadFont(String(fontPath), LittleFS);
  g_smoothFontLoaded = tft.fontLoaded;
  if (g_smoothFontLoaded) {
    g_loadedSmoothFont = fontPath;
  }
  return g_smoothFontLoaded;
}

void unloadSmoothFontCached() {
  if (!g_smoothFontLoaded) {
    return;
  }

  tft.unloadFont();
  g_smoothFontLoaded = false;
  g_loadedSmoothFont = "";
}

void drawSmoothString(const String &text, int32_t x, int32_t y, uint8_t datum, uint16_t color) {
  if (!g_smoothFontLoaded) {
    return;
  }
  tft.setTextDatum(datum);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(text, x, y);
}

void drawSmoothString(const char *text, int32_t x, int32_t y, uint8_t datum, uint16_t color) {
  if (!g_smoothFontLoaded || text == nullptr) {
    return;
  }
  tft.setTextDatum(datum);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(text, x, y);
}

void drawMiniTempArrow(int16_t x, int16_t y, bool up, uint16_t color) {
  const int16_t shaftX = x + 3;
  if (up) {
    tft.fillTriangle(x, y + 4, x + 6, y + 4, x + 3, y, color);
    tft.drawLine(shaftX, y + 4, shaftX, y + 9, color);
  } else {
    tft.fillTriangle(x, y + 5, x + 6, y + 5, x + 3, y + 9, color);
    tft.drawLine(shaftX, y, shaftX, y + 5, color);
  }
}

int16_t getSmoothTextWidth(const String &text) {
  if (!g_smoothFontLoaded) {
    return 0;
  }

  return (int16_t)tft.textWidth(text);
}

int16_t getSmoothTextWidth(const char *text) {
  if (!g_smoothFontLoaded || text == nullptr) {
    return 0;
  }

  return (int16_t)tft.textWidth(text);
}

bool drawTextWithSmoothFont(const char *fontPath,
                            const String &text,
                            int32_t x,
                            int32_t y,
                            uint8_t datum,
                            uint16_t color) {
  if (!loadSmoothFontCached(fontPath)) {
    return false;
  }

  drawSmoothString(text, x, y, datum, color);
  return true;
}

bool drawTextWithSmoothFont(const char *fontPath,
                            const char *text,
                            int32_t x,
                            int32_t y,
                            uint8_t datum,
                            uint16_t color) {
  if (!loadSmoothFontCached(fontPath)) {
    return false;
  }

  drawSmoothString(text, x, y, datum, color);
  return true;
}

int16_t measureTextWithSmoothFont(const char *fontPath, const String &text) {
  if (!loadSmoothFontCached(fontPath)) {
    return 0;
  }

  return getSmoothTextWidth(text);
}

int16_t measureTextWithSmoothFont(const char *fontPath, const char *text) {
  if (!loadSmoothFontCached(fontPath)) {
    return 0;
  }

  return getSmoothTextWidth(text);
}

int16_t getSmoothFontHeight(const char *fontPath) {
  if (!loadSmoothFontCached(fontPath)) {
    return 0;
  }

  return (int16_t)tft.fontHeight();
}

const char* getUiFontPathBySize(uint8_t fontSize) {
  switch (fontSize) {
    case 14: return FONT_VLW_UI_14;
    case 16: return FONT_VLW_UI_16;
    case 18: return FONT_VLW_UI_18;
    case 20: return FONT_VLW_UI_20;
    case 24: return FONT_VLW_UI_24;
    case 36: return FONT_VLW_CLOCK_36;
    case 48: return FONT_VLW_TEMP_48;
    default: return FONT_VLW_UI_18;
  }
}

void drawDebugLineIfChanged(uint8_t index, int16_t y, uint8_t fontSize, const char *text, bool force) {
  if (index >= DEBUG_LINE_COUNT || text == nullptr) {
    return;
  }

  if (!force && strcmp(g_lastDebugLines[index], text) == 0) {
    return;
  }

  tft.fillRect(0, y - 2, tft.width(), 24, TFT_BLACK);
  drawTextWithSmoothFont(getUiFontPathBySize(fontSize), text, centerWidth, y, TC_DATUM, TFT_WHITE);
  strncpy(g_lastDebugLines[index], text, sizeof(g_lastDebugLines[index]) - 1);
  g_lastDebugLines[index][sizeof(g_lastDebugLines[index]) - 1] = '\0';
}

void drawDebugSegmentLineIfChanged(uint8_t index, int16_t y, uint8_t fontSize,
                                   const String *segments, const uint16_t *colors,
                                   uint8_t segmentCount, const char *cacheKey, bool force) {
  if (index >= DEBUG_LINE_COUNT || segments == nullptr || colors == nullptr || cacheKey == nullptr) {
    return;
  }

  if (!force && strcmp(g_lastDebugLines[index], cacheKey) == 0) {
    return;
  }

  tft.fillRect(0, y - 2, tft.width(), 24, TFT_BLACK);
  const char *fontPath = getUiFontPathBySize(fontSize);
  if (!loadSmoothFontCached(fontPath)) {
    return;
  }

  int32_t x = 10;
  for (uint8_t i = 0; i < segmentCount; i++) {
    if (segments[i].length() == 0) {
      continue;
    }
    drawSmoothString(segments[i], x, y, TL_DATUM, colors[i]);
    x += getSmoothTextWidth(segments[i]);
  }

  strncpy(g_lastDebugLines[index], cacheKey, sizeof(g_lastDebugLines[index]) - 1);
  g_lastDebugLines[index][sizeof(g_lastDebugLines[index]) - 1] = '\0';
}

void repaint() {
  tft.fillScreen(TFT_BLACK);
  if (ui != nullptr) {
    ui->drawLogo();
  }

  drawTextWithSmoothFont(FONT_VLW_UI_16, String(APP_NAME), centerWidth, tft.height() - 50, TC_DATUM, TFT_WHITE);
  drawTextWithSmoothFont(FONT_VLW_UI_16, String(VERSION), centerWidth, tft.height() - 30, TC_DATUM, TFT_WHITE);
  drawProgress("Uruchamianie...", 5);
}

void clearPageSurface() {
  tft.fillScreen(TFT_BLACK);
}

void syncTime(void) {
  g_lastTimeSyncAttemptMs = millis();
  if (initTime()) {
    lastTimeSyncMillis = millis();
    setTimezone(TIMEZONE);
    g_timeSynced = hasValidTime();
    FW_LOG_I("Czas: %s", getCurrentTimestamp(SYSTEM_TIMESTAMP_FORMAT).c_str());
  } else {
    g_timeSynced = hasValidTime();
  }
}

void drawProgress(const char *text, int8_t percentage) {
  int pbWidth = tft.width() - 100;
  int pbX = (tft.width() - pbWidth)/2;
  int pbY = 260;
  int progressTextY = 210;

  tft.fillRect(0, progressTextY, tft.width(), 40, TFT_BLACK);
  drawTextWithSmoothFont(FONT_VLW_UI_24, String(text), centerWidth, progressTextY, TC_DATUM, TFT_WHITE);
  if (ui != nullptr) {
    ui->drawProgressBar(pbX, pbY, pbWidth, 15, percentage, TFT_WHITE, TFT_TP_BLUE);
  }
}

static void invalidateClockRenderCache() {
  g_lastClockDateKey[0] = '\0';
  g_lastClockTimeKey[0] = '\0';
  g_lastClockSecondsKey[0] = '\0';
  g_lastClockShowSeconds = false;
  g_lastNightOverrideBadgeValid = false;
}

static void invalidateMainSectionCaches(bool includeWeather, bool includeForecast) {
  invalidateClockRenderCache();
  if (includeWeather) {
    g_lastWeatherRenderKey[0] = '\0';
    g_mainDescRenderKey[0] = '\0';
    g_mainIconRenderKey[0] = '\0';
    g_mainTempRenderKey[0] = '\0';
    g_mainMetricsRenderKey[0] = '\0';
  }
  if (includeForecast) {
    g_lastForecastRenderKey[0] = '\0';
    for (uint8_t i = 0; i < NUMBER_OF_DAY_FORECASTS; i++) {
      g_mainForecastDayKeys[i][0] = '\0';
    }
    g_mainSunRenderKey[0] = '\0';
    g_mainRadRenderKey[0] = '\0';
    g_mainAqiRenderKey[0] = '\0';
  }
}

void renderRescueMainScreen() {
  g_currentPage = PAGE_MAIN;
  invalidateMainSectionCaches(true, true);
  clearPageSurface();
  updateRescueClock();
  updateRescueWeather(true);
  updateRescueForecast(true);
  g_lastFullPageRenderMs = millis();
}

void renderRescueStatsScreen() {
  g_currentPage = PAGE_STATS;
  g_lastStatsRenderKey[0] = '\0';
  clearPageSurface();
  updateRescueStats(true);
  g_lastFullPageRenderMs = millis();
}

void renderRescueDebugScreen() {
  g_currentPage = PAGE_DEBUG;
  clearPageSurface();
  updateRescueDebug(true);
  g_lastFullPageRenderMs = millis();
}

void renderRescueMenuScreen() {
  g_currentPage = PAGE_MENU;
  clearPageSurface();
  invalidateMenuFull();
  updateRescueMenu(false);
  g_lastFullPageRenderMs = millis();
}

void updateRescueClock() {
  String dateLine = "";
  if (hasValidTime()) {
    uint8_t weekday = getCurrentWeekday();
    if (weekday < 7) {
      dateLine = WEEKDAYS[weekday] + ", " + getCurrentTimestamp(UI_DATE_FORMAT);
    } else {
      dateLine = getCurrentTimestamp(UI_DATE_FORMAT);
    }
  }
  if (dateLine.length() == 0) {
    dateLine = "No NTP time";
  }

  char timeRender[16] = "-- --";
  char secondsText[4] = "";
  struct tm timeinfo;
  bool colonVisible = !g_menuDraft.showSeconds;
  if (getLocalTime(&timeinfo, 10)) {
    if (g_menuDraft.showSeconds) {
      snprintf(timeRender, sizeof(timeRender), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      snprintf(secondsText, sizeof(secondsText), "%02d", timeinfo.tm_sec);
      colonVisible = true;
    } else {
      snprintf(timeRender, sizeof(timeRender), "%02d%02d", timeinfo.tm_hour, timeinfo.tm_min);
      colonVisible = ((timeinfo.tm_sec % 2) == 0);
    }
  } else {
    if (g_menuDraft.showSeconds) {
      snprintf(timeRender, sizeof(timeRender), "--:--:--");
      snprintf(secondsText, sizeof(secondsText), "--");
      colonVisible = true;
    } else {
      snprintf(timeRender, sizeof(timeRender), "----");
    }
  }

  const bool dateChanged = strcmp(g_lastClockDateKey, dateLine.c_str()) != 0;
  const bool showSecondsChanged = (g_lastClockShowSeconds != g_menuDraft.showSeconds);
  const bool timeChanged = strcmp(g_lastClockTimeKey, timeRender) != 0;
  const bool secondsChanged = strcmp(g_lastClockSecondsKey, secondsText) != 0;
  const bool colonChanged = (g_lastClockColonVisible != colonVisible);

  if (dateChanged) {
    tft.fillRect(0, 8, tft.width(), 24, TFT_BLACK);
    drawTextWithSmoothFont(FONT_VLW_UI_16, dateLine, centerWidth, 14, TC_DATUM, TFT_WHITE);
    strncpy(g_lastClockDateKey, dateLine.c_str(), sizeof(g_lastClockDateKey) - 1);
    g_lastClockDateKey[sizeof(g_lastClockDateKey) - 1] = '\0';
  }

  if (g_menuDraft.showSeconds) {
    const String hh = String(timeRender).substring(0, 2);
    const String mm = String(timeRender).substring(3, 5);
    const String ss = String(secondsText);
    loadSmoothFontCached(FONT_VLW_CLOCK_36);
    const int16_t hhWidth = getSmoothTextWidth(hh);
    const int16_t colonWidth = getSmoothTextWidth(":");
    const int16_t mmWidth = getSmoothTextWidth(mm);
    const int16_t ssWidth = getSmoothTextWidth(ss);
    const int16_t totalWidth = hhWidth + colonWidth + mmWidth + colonWidth + ssWidth;
    const int16_t startX = centerWidth - (totalWidth / 2);
    const int16_t secColonX = startX + hhWidth + colonWidth + mmWidth;
    const int16_t secTextX = secColonX + colonWidth;

    if (showSecondsChanged || timeChanged) {
      tft.fillRect(0, 34, tft.width(), 44, TFT_BLACK);
      drawSmoothString(hh, startX, 42, TL_DATUM, TFT_WHITE);
      drawSmoothString(String(":"), startX + hhWidth, 42, TL_DATUM, TFT_WHITE);
      drawSmoothString(mm, startX + hhWidth + colonWidth, 42, TL_DATUM, TFT_WHITE);
      drawSmoothString(String(":"), secColonX, 42, TL_DATUM, TFT_WHITE);
      drawSmoothString(ss, secTextX, 42, TL_DATUM, TFT_WHITE);
      strncpy(g_lastClockTimeKey, timeRender, sizeof(g_lastClockTimeKey) - 1);
      g_lastClockTimeKey[sizeof(g_lastClockTimeKey) - 1] = '\0';
    } else if (secondsChanged) {
      tft.fillRect(secTextX, 34, ssWidth + 4, 44, TFT_BLACK);
      drawSmoothString(ss, secTextX, 42, TL_DATUM, TFT_WHITE);
    }
    strncpy(g_lastClockSecondsKey, secondsText, sizeof(g_lastClockSecondsKey) - 1);
    g_lastClockSecondsKey[sizeof(g_lastClockSecondsKey) - 1] = '\0';
  } else if (timeChanged || showSecondsChanged) {
    tft.fillRect(0, 34, tft.width(), 44, TFT_BLACK);
    loadSmoothFontCached(FONT_VLW_CLOCK_36);
    const String hh = String(timeRender).substring(0, 2);
    const String colon = colonVisible ? ":" : " ";
    const String mm = String(timeRender).substring(2, 4);
    const int16_t hhWidth = getSmoothTextWidth(hh);
    const int16_t colonWidth = getSmoothTextWidth(":");
    const int16_t mmWidth = getSmoothTextWidth(mm);
    const int16_t totalWidth = hhWidth + colonWidth + mmWidth;
    const int16_t startX = centerWidth - (totalWidth / 2);
    drawSmoothString(hh, startX, 42, TL_DATUM, TFT_WHITE);
    drawSmoothString(colon, startX + hhWidth, 42, TL_DATUM, TFT_WHITE);
    drawSmoothString(mm, startX + hhWidth + colonWidth, 42, TL_DATUM, TFT_WHITE);
    strncpy(g_lastClockTimeKey, timeRender, sizeof(g_lastClockTimeKey) - 1);
    g_lastClockTimeKey[sizeof(g_lastClockTimeKey) - 1] = '\0';
  } else if (!g_menuDraft.showSeconds && colonChanged) {
    loadSmoothFontCached(FONT_VLW_CLOCK_36);
    const String hh = String(timeRender).substring(0, 2);
    const String mm = String(timeRender).substring(2, 4);
    const int16_t hhWidth = getSmoothTextWidth(hh);
    const int16_t colonWidth = getSmoothTextWidth(":");
    const int16_t mmWidth = getSmoothTextWidth(mm);
    const int16_t totalWidth = hhWidth + colonWidth + mmWidth;
    const int16_t startX = centerWidth - (totalWidth / 2);
    tft.fillRect(startX + hhWidth, 34, colonWidth + 2, 44, TFT_BLACK);
    drawSmoothString(colonVisible ? String(":") : String(" "), startX + hhWidth, 42, TL_DATUM, TFT_WHITE);
  }

  g_lastClockShowSeconds = g_menuDraft.showSeconds;
  g_lastClockColonVisible = colonVisible;
  unloadSmoothFontCached();
  drawMainNightOverrideBadge(dateChanged || !g_lastNightOverrideBadgeValid);
  g_lastClockRenderMs = millis();
}

void drawMainNightOverrideBadge(bool force) {
  if (g_currentPage != PAGE_MAIN) {
    return;
  }
  const uint8_t badgeState = (uint8_t)g_lcdBrightnessMode;
  if (!force &&
      g_lastNightOverrideBadgeValid &&
      g_lastNightOverrideBadgeState == badgeState) {
    return;
  }

  const uint16_t borderColor = (g_lcdBrightnessMode == LCD_MODE_NIGHT)
                                 ? TFT_RED
                                 : ((g_lcdBrightnessMode == LCD_MODE_DAY) ? TFT_YELLOW : 0x39C7);
  const uint16_t textColor = borderColor;
  const String badgeText = (g_lcdBrightnessMode == LCD_MODE_NIGHT)
                             ? String("NOC")
                             : ((g_lcdBrightnessMode == LCD_MODE_DAY) ? String("DZ") : String("AUTO"));
  tft.fillRoundRect(MAIN_NIGHT_TOGGLE_BUTTON.x,
                    MAIN_NIGHT_TOGGLE_BUTTON.y,
                    MAIN_NIGHT_TOGGLE_BUTTON.width,
                    MAIN_NIGHT_TOGGLE_BUTTON.height,
                    4,
                    TFT_BLACK);
  tft.drawRoundRect(MAIN_NIGHT_TOGGLE_BUTTON.x,
                    MAIN_NIGHT_TOGGLE_BUTTON.y,
                    MAIN_NIGHT_TOGGLE_BUTTON.width,
                    MAIN_NIGHT_TOGGLE_BUTTON.height,
                    4,
                    borderColor);
  drawTextWithSmoothFont(FONT_VLW_UI_14,
                         badgeText,
                         MAIN_NIGHT_TOGGLE_BUTTON.x + (MAIN_NIGHT_TOGGLE_BUTTON.width / 2),
                         MAIN_NIGHT_TOGGLE_BUTTON.y + 1,
                         TC_DATUM,
                         textColor);
  g_lastNightOverrideBadgeState = badgeState;
  g_lastNightOverrideBadgeValid = true;
}

void toggleMainNightOverride() {
  if (g_lcdBrightnessMode == LCD_MODE_NIGHT) {
    applyBrightnessMode(LCD_MODE_AUTO);
  } else if (g_lcdBrightnessMode == LCD_MODE_DAY) {
    applyBrightnessMode(LCD_MODE_AUTO);
  } else {
    applyBrightnessMode(LCD_MODE_NIGHT);
  }
  if (g_currentPage == PAGE_MAIN) {
    drawMainNightOverrideBadge(true);
  }
}

void updateRescueWeather(bool force) {
  if (g_currentPage != PAGE_MAIN) {
    return;
  }

  const uint32_t nowMs = millis();
  const int16_t clockTextTop = 42;
  const int16_t tempTextTop = 120;
  const int16_t clockTextHeight = getSmoothFontHeight(FONT_VLW_CLOCK_36);
  const int16_t descTextHeight = getSmoothFontHeight(FONT_VLW_UI_24);
  const int16_t clockTextBottom = clockTextTop + clockTextHeight;
  const int16_t descGap = (tempTextTop - clockTextBottom - descTextHeight) / 2;
  const int16_t descTextTop = clockTextBottom + descGap;
  int16_t descClearTop = descTextTop - 2;
  int16_t descClearBottom = descTextTop + descTextHeight + 2;
  if (descClearTop < clockTextBottom) {
    descClearTop = clockTextBottom;
  }
  if (descClearBottom > tempTextTop) {
    descClearBottom = tempTextTop;
  }
  if (descClearBottom <= descClearTop) {
    descClearTop = descTextTop;
    descClearBottom = descTextTop + descTextHeight;
  }

  char lineBuf[96];
  const CurrentWeatherViewState &weather = getDisplayedWeather();
  char iconKey[96];
  char descKey[128];
  char tempKey[128];
  char metricsKey[128];

  snprintf(iconKey,
           sizeof(iconKey),
           "%u|%s|%u",
           (unsigned)weather.hasData,
           weather.icon,
           (unsigned)weather.weatherId);

  if (force || strcmp(iconKey, g_mainIconRenderKey) != 0) {
    tft.fillRect(0, 119, 96, 111, TFT_BLACK);
    if (weather.hasData && ui != nullptr) {
      String iconPath = String("/weather/") + getMainWeatherIconNameByOwmIcon(weather.icon, weather.weatherId) + ".bmp";
      ui->drawBmp(iconPath, 5, 125);
    }
    strncpy(g_mainIconRenderKey, iconKey, sizeof(g_mainIconRenderKey) - 1);
    g_mainIconRenderKey[sizeof(g_mainIconRenderKey) - 1] = '\0';
  }

  snprintf(descKey,
           sizeof(descKey),
           "%u|%s|%s",
           (unsigned)weather.hasData,
           weather.cityName,
           weather.hasData ? weather.description : currentWeatherServiceGetStateLabel());

  if (force || strcmp(descKey, g_mainDescRenderKey) != 0) {
    tft.fillRect(0, descClearTop, tft.width(), descClearBottom - descClearTop, TFT_BLACK);
    if (weather.hasData && strlen(weather.description) > 0) {
      snprintf(lineBuf, sizeof(lineBuf), "%s", weather.description);
    } else {
      snprintf(lineBuf, sizeof(lineBuf), "%s", currentWeatherServiceGetStateLabel());
    }
    drawTextWithSmoothFont(FONT_VLW_UI_24, lineBuf, centerWidth, descTextTop, TC_DATUM, TFT_WHITE);
    strncpy(g_mainDescRenderKey, descKey, sizeof(g_mainDescRenderKey) - 1);
    g_mainDescRenderKey[sizeof(g_mainDescRenderKey) - 1] = '\0';
  }

  snprintf(tempKey,
           sizeof(tempKey),
           "%u|%s|%u|%.1f|%.1f|%.1f|%.1f",
           (unsigned)weather.hasData,
           weather.icon,
           (unsigned)weather.weatherId,
           weather.temperature,
           weather.feelsLike,
           weather.tempMin,
           weather.tempMax);

  if (force || strcmp(tempKey, g_mainTempRenderKey) != 0) {
    tft.fillRect(96, 119, tft.width() - 96, 63, TFT_BLACK);

    if (weather.hasData) {
      char tempBuf[24];
      char feelsBuf[24];
      char minBuf[20];
      char maxBuf[20];
      snprintf(tempBuf, sizeof(tempBuf), "%.1f°", weather.temperature);
      snprintf(feelsBuf, sizeof(feelsBuf), "%.1f°C", weather.feelsLike);
      snprintf(minBuf, sizeof(minBuf), "%.1f°", weather.tempMin);
      snprintf(maxBuf, sizeof(maxBuf), "%.1f°", weather.tempMax);
      const int16_t tempWidth = measureTextWithSmoothFont(FONT_VLW_TEMP_48, String(tempBuf));
      const int16_t tempX = centerWidth - 26 - (tempWidth / 2);
      const int16_t feelsX = centerWidth + 62;
      const int16_t minWidth = measureTextWithSmoothFont(FONT_VLW_UI_18, minBuf);
      const int16_t maxWidth = measureTextWithSmoothFont(FONT_VLW_UI_18, maxBuf);
      const int16_t slashWidth = measureTextWithSmoothFont(FONT_VLW_UI_18, " / ");
      const int16_t twoCharOffset = measureTextWithSmoothFont(FONT_VLW_UI_18, "00");
      const int16_t arrowWidth = 10;
      const int16_t minSegmentWidth = arrowWidth + minWidth;
      const int16_t maxSegmentWidth = arrowWidth + maxWidth;
      const int16_t minMaxX = feelsX - 4 - twoCharOffset;
      const int16_t minArrowY = 166;
      const int16_t minTextX = minMaxX + arrowWidth;
      const int16_t slashX = minTextX + minWidth;
      const int16_t maxArrowX = slashX + slashWidth;
      const int16_t maxTextX = maxArrowX + arrowWidth;
      drawTextWithSmoothFont(FONT_VLW_TEMP_48, String(tempBuf), tempX, 120, TL_DATUM, TFT_WHITE);
      drawTextWithSmoothFont(FONT_VLW_UI_14, String(u8"Odczuwalna"), feelsX, 122, TL_DATUM, TFT_WHITE);
      drawTextWithSmoothFont(FONT_VLW_UI_18, String(feelsBuf), feelsX, 138, TL_DATUM, TFT_WHITE);
      drawMiniTempArrow(minMaxX, minArrowY, false, TFT_BLUE);
      drawTextWithSmoothFont(FONT_VLW_UI_18, minBuf, minTextX, 164, TL_DATUM, TFT_BLUE);
      drawTextWithSmoothFont(FONT_VLW_UI_18, " / ", slashX, 164, TL_DATUM, TFT_WHITE);
      drawMiniTempArrow(maxArrowX, minArrowY, true, TFT_RED);
      drawTextWithSmoothFont(FONT_VLW_UI_18, maxBuf, maxTextX, 164, TL_DATUM, TFT_RED);
    } else {
      drawTextWithSmoothFont(FONT_VLW_TEMP_48, String("--.-°"), centerWidth - 20, 120, TC_DATUM, TFT_WHITE);
    }
    strncpy(g_mainTempRenderKey, tempKey, sizeof(g_mainTempRenderKey) - 1);
    g_mainTempRenderKey[sizeof(g_mainTempRenderKey) - 1] = '\0';
  }

  snprintf(metricsKey,
           sizeof(metricsKey),
           "%u|%u|%u|%.1f",
           (unsigned)weather.hasData,
           (unsigned)weather.humidity,
           (unsigned)weather.pressure,
           weather.windSpeed);

  if (force || strcmp(metricsKey, g_mainMetricsRenderKey) != 0) {
    tft.fillRect(96, 182, tft.width() - 96, 48, TFT_BLACK);
    if (weather.hasData) {
      snprintf(lineBuf, sizeof(lineBuf), "%u %%", weather.humidity);
    } else {
      snprintf(lineBuf, sizeof(lineBuf), "-- %%");
    }
    drawTextWithSmoothFont(FONT_VLW_UI_18, String(lineBuf), centerWidth, 188, TC_DATUM, TFT_WHITE);

    if (weather.hasData) {
      snprintf(lineBuf, sizeof(lineBuf), "%u hPa", weather.pressure);
    } else {
      snprintf(lineBuf, sizeof(lineBuf), "-- hPa");
    }
    drawTextWithSmoothFont(FONT_VLW_UI_18, String(lineBuf), centerWidth, 210, TC_DATUM, TFT_WHITE);

    if (weather.hasData) {
      snprintf(lineBuf, sizeof(lineBuf), "%.0f m/s", weather.windSpeed);
    } else {
      snprintf(lineBuf, sizeof(lineBuf), "-- m/s");
    }
    drawTextWithSmoothFont(FONT_VLW_UI_18, String(lineBuf), tft.width() - 43, 210, TC_DATUM, TFT_WHITE);
    rescueDrawSeparator(230);
    strncpy(g_mainMetricsRenderKey, metricsKey, sizeof(g_mainMetricsRenderKey) - 1);
    g_mainMetricsRenderKey[sizeof(g_mainMetricsRenderKey) - 1] = '\0';
  }

}

void updateRescueForecast(bool force) {
  if (g_currentPage != PAGE_MAIN) {
    return;
  }

  const uint32_t nowMs = millis();

  char lineBuf[96];
  const ForecastViewState &forecast = getDisplayedForecast();
  const CurrentWeatherViewState &weather = getDisplayedWeather();
  const AqiViewState &aqi = getDisplayedAqi();
  const AppState *appState = getAppState();
  const RadiationViewState &radiation = appState->radiation;
  const bool radiationForActiveCity =
    radiation.hasData && radiation.cityIndex == g_activeCityIndex;

  const int leftCx = 60;
  const int radHeaderCx = 262;
  const int radValueRight = tft.width() - 8;
  const int radSquareX = radHeaderCx + 24;
  const int midTitleCx = centerWidth;
  const int midLabelX = 108;
  const int midValueX = 168;
  const int midSquareX = 198;
  const int rowY0 = 390;
  const int rowDy = 22;

  const int16_t widthEight = tft.width() / 8;
  if (g_menuDraft.showForecast) {
    for (uint8_t i = 0; i < NUMBER_OF_DAY_FORECASTS; i++) {
      const int16_t x = widthEight * ((i * 2) + 1);
      char dayKey[96];
      snprintf(dayKey,
               sizeof(dayKey),
               "%u|%u|%.1f|%.1f|%u",
               (unsigned)forecast.days[i].valid,
               (unsigned)forecast.days[i].weekday,
               forecast.days[i].minTemp,
               forecast.days[i].maxTemp,
               (unsigned)forecast.days[i].weatherId);
      if (force || strcmp(dayKey, g_mainForecastDayKeys[i]) != 0) {
        tft.fillRect(x - 32, 231, 64, 124, TFT_BLACK);
        if (!forecast.hasData || !forecast.days[i].valid) {
          drawTextWithSmoothFont(FONT_VLW_UI_24, String("--"), x, 235, TC_DATUM, TFT_WHITE);
          drawTextWithSmoothFont(FONT_VLW_UI_18, String("--/--"), x, 265, TC_DATUM, TFT_WHITE);
        } else {
          if (ui != nullptr) {
            String iconPath = String("/weather-small/") + getForecastIconName(forecast.days[i].weatherId) + ".bmp";
            ui->drawBmp(iconPath, x - 25, 295);
          }
          snprintf(lineBuf, sizeof(lineBuf), "%s", WEEKDAYS_ABBR[forecast.days[i].weekday].c_str());
          drawTextWithSmoothFont(FONT_VLW_UI_24, String(lineBuf), x, 235, TC_DATUM, TFT_WHITE);
          snprintf(lineBuf, sizeof(lineBuf), "%.0f/%.0f", forecast.days[i].minTemp, forecast.days[i].maxTemp);
          drawTextWithSmoothFont(FONT_VLW_UI_18, String(lineBuf), x, 265, TC_DATUM, TFT_WHITE);
        }
        strncpy(g_mainForecastDayKeys[i], dayKey, sizeof(g_mainForecastDayKeys[i]) - 1);
        g_mainForecastDayKeys[i][sizeof(g_mainForecastDayKeys[i]) - 1] = '\0';
      }
    }
  } else {
    tft.fillRect(0, 231, tft.width(), 124, TFT_BLACK);
    for (uint8_t i = 0; i < NUMBER_OF_DAY_FORECASTS; i++) {
      g_mainForecastDayKeys[i][0] = '\0';
    }
  }

  char sunKey[64];
  char timeBuf[16];
  if (g_menuDraft.showSun) {
    if (weather.hasData && weather.sunrise > 0) {
      time_t sunriseTime = (time_t)weather.sunrise;
      strftime(timeBuf, sizeof(timeBuf), UI_TIME_FORMAT_NO_SECONDS, localtime(&sunriseTime));
    } else {
      snprintf(timeBuf, sizeof(timeBuf), "--:--");
    }
    char sunriseBuf[16];
    strncpy(sunriseBuf, timeBuf, sizeof(sunriseBuf) - 1);
    sunriseBuf[sizeof(sunriseBuf) - 1] = '\0';

    if (weather.hasData && weather.sunset > 0) {
      time_t sunsetTime = (time_t)weather.sunset;
      strftime(timeBuf, sizeof(timeBuf), UI_TIME_FORMAT_NO_SECONDS, localtime(&sunsetTime));
    } else {
      snprintf(timeBuf, sizeof(timeBuf), "--:--");
    }
    snprintf(sunKey, sizeof(sunKey), "%s|%s", sunriseBuf, timeBuf);
    if (force || strcmp(sunKey, g_mainSunRenderKey) != 0) {
      tft.fillRect(0, 356, 96, 88, TFT_BLACK);
      rescueDrawSeparator(355);
      drawTextWithSmoothFont(FONT_VLW_UI_20, String(u8"Słońce"), leftCx, 365, TC_DATUM, TFT_WHITE);
      drawTextWithSmoothFont(FONT_VLW_UI_18, String(sunriseBuf), leftCx, 400, TC_DATUM, TFT_WHITE);
      drawTextWithSmoothFont(FONT_VLW_UI_18, String(timeBuf), leftCx, 425, TC_DATUM, TFT_WHITE);
      strncpy(g_mainSunRenderKey, sunKey, sizeof(g_mainSunRenderKey) - 1);
      g_mainSunRenderKey[sizeof(g_mainSunRenderKey) - 1] = '\0';
    }
  } else {
    tft.fillRect(0, 356, 96, 88, TFT_BLACK);
    g_mainSunRenderKey[0] = '\0';
  }

  char radKey[128];
  if (g_menuDraft.showRad) {
    if (radiationForActiveCity) {
      snprintf(lineBuf, sizeof(lineBuf), "%s", radiation.display);
    } else {
      snprintf(lineBuf, sizeof(lineBuf), "--");
    }
    snprintf(radKey, sizeof(radKey), "%u|%s|%d",
             (unsigned)radiationForActiveCity, lineBuf, (int)radiation.status);
    if (force || strcmp(radKey, g_mainRadRenderKey) != 0) {
      tft.fillRect(220, 356, 100, 72, TFT_BLACK);
      drawTextWithSmoothFont(FONT_VLW_UI_20, String("RAD"), radHeaderCx, 365, TC_DATUM, TFT_WHITE);
      tft.fillRect(radSquareX, 369, 10, 10, radiationForActiveCity ? radiation.color : TFT_DARKGREY);
      drawTextWithSmoothFont(FONT_VLW_UI_16, String(lineBuf), radValueRight, 400, TR_DATUM, TFT_WHITE);
      strncpy(g_mainRadRenderKey, radKey, sizeof(g_mainRadRenderKey) - 1);
      g_mainRadRenderKey[sizeof(g_mainRadRenderKey) - 1] = '\0';
    }
  } else {
    tft.fillRect(220, 356, 100, 72, TFT_BLACK);
    g_mainRadRenderKey[0] = '\0';
  }

  int pm25Aqi = (aqi.hasData && aqi.pm25 >= 0) ? pm25ToAqi((float)aqi.pm25) : -1;
  int pm10Aqi = (aqi.hasData && aqi.pm10 >= 0) ? pm10ToAqi((float)aqi.pm10) : -1;
  char aqiKey[128];
  const uint16_t cityAccentColor = getCityUiColor(g_activeCityIndex);
  if (g_menuDraft.showAqi) {
    snprintf(aqiKey, sizeof(aqiKey), "%s|%u|%d|%d|%d",
             weather.cityName,
             (unsigned)aqi.hasData,
             aqi.aqi,
             aqi.pm25,
             aqi.pm10);
    if (force || strcmp(aqiKey, g_mainAqiRenderKey) != 0) {
      tft.fillRect(96, 356, 124, 96, TFT_BLACK);
      drawTextWithSmoothFont(FONT_VLW_UI_20, String(weather.cityName), midTitleCx, 365, TC_DATUM, cityAccentColor);
      rescueDrawAqiRowAligned(midLabelX, midValueX, midSquareX, rowY0 + 0 * rowDy, "AQI",
                              (aqi.hasData && aqi.aqi >= 0) ? (float)aqi.aqi : -1.0f,
                              (aqi.hasData && aqi.aqi >= 0) ? aqiToColor(aqi.aqi) : TFT_DARKGREY, 0);
      rescueDrawAqiRowAligned(midLabelX, midValueX, midSquareX, rowY0 + 1 * rowDy, "PM2.5",
                              (aqi.hasData && aqi.pm25 >= 0) ? (float)aqi.pm25 : -1.0f,
                              pm25Aqi >= 0 ? aqiToColor(pm25Aqi) : TFT_DARKGREY, 0);
      rescueDrawAqiRowAligned(midLabelX, midValueX, midSquareX, rowY0 + 2 * rowDy, "PM10",
                              (aqi.hasData && aqi.pm10 >= 0) ? (float)aqi.pm10 : -1.0f,
                              pm10Aqi >= 0 ? aqiToColor(pm10Aqi) : TFT_DARKGREY, 0);
      strncpy(g_mainAqiRenderKey, aqiKey, sizeof(g_mainAqiRenderKey) - 1);
      g_mainAqiRenderKey[sizeof(g_mainAqiRenderKey) - 1] = '\0';
    }
  } else {
    tft.fillRect(96, 356, 124, 96, TFT_BLACK);
    g_mainAqiRenderKey[0] = '\0';
  }
}

void setActiveCity(uint8_t cityIndex, bool forceRender) {
  if (cityIndex >= CITY_COUNT) {
    return;
  }

  g_activeCityIndex = cityIndex;
  currentWeatherServiceSetCity(cityIndex);
  forecastServiceSetCity(cityIndex);
  aqiServiceSetCity(cityIndex);
  radiationJobServiceSetCity(cityIndex);
  g_lastWeatherRenderKey[0] = '\0';
  g_lastForecastRenderKey[0] = '\0';

  if (forceRender) {
    renderCurrentRescuePage();
  }
}

void navigateToPage(AppPage page, int8_t cityIndex) {
  g_navOverlayVisible = false;
  resetTouchInteractionState();
  g_touchConsumeUntilRelease = true;
  if (cityIndex >= 0 && cityIndex < (int8_t)CITY_COUNT) {
    setActiveCity((uint8_t)cityIndex, false);
  }
  g_currentPage = page;
  syncCityCacheFromAppState();
  if (page == PAGE_STATS) {
    sampleTemperatureHistory(millis());
    g_lastStatsRenderKey[0] = '\0';
  }
  renderCurrentRescuePage();
}

void syncCityCacheFromAppState() {
  for (uint8_t cityIndex = 0; cityIndex < CITY_COUNT; cityIndex++) {
    const OpenWeatherMapCurrentData *data = currentWeatherServiceGetDataForCity(cityIndex);
    CurrentWeatherViewState &weather = g_weatherCache[cityIndex];
    weather.cityIndex = cityIndex;
    weather.fetchState = currentWeatherServiceGetStateForCity(cityIndex);
    weather.ageSeconds = currentWeatherServiceGetAgeSecondsForCity(cityIndex, millis());
    if (data != nullptr) {
      weather.hasData = true;
      strncpy(weather.cityName, data->cityName.c_str(), sizeof(weather.cityName) - 1);
      weather.cityName[sizeof(weather.cityName) - 1] = '\0';
      strncpy(weather.description, data->description.c_str(), sizeof(weather.description) - 1);
      weather.description[sizeof(weather.description) - 1] = '\0';
      strncpy(weather.icon, data->icon.c_str(), sizeof(weather.icon) - 1);
      weather.icon[sizeof(weather.icon) - 1] = '\0';
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
    }
  }

  const AppState *appState = getAppState();

  for (uint8_t cityIndex = 0; cityIndex < CITY_COUNT; cityIndex++) {
    ForecastViewState &forecast = g_forecastCache[cityIndex];
    forecast.cityIndex = cityIndex;
    forecast.fetchState = forecastServiceGetStateForCity(cityIndex);
    forecast.itemCount = forecastServiceGetCountForCity(cityIndex);
    forecast.ageSeconds = forecastServiceGetAgeSecondsForCity(cityIndex, millis());
    forecast.hasData = forecastServiceHasDataForCity(cityIndex);
    for (uint8_t i = 0; i < NUMBER_OF_DAY_FORECASTS; i++) {
      forecast.days[i] = {};
    }
    const OpenWeatherMapForecastData *forecastData = forecastServiceGetDataForCity(cityIndex);
    if (forecastData != nullptr) {
      DayForecast *dayForecasts = calculateDayForecasts(const_cast<OpenWeatherMapForecastData*>(forecastData));
      for (uint8_t i = 0; i < NUMBER_OF_DAY_FORECASTS; i++) {
        forecast.days[i].valid = dayForecasts[i].conditionCode != 0;
        forecast.days[i].weekday = (uint8_t)dayForecasts[i].day;
        forecast.days[i].minTemp = dayForecasts[i].minTemp;
        forecast.days[i].maxTemp = dayForecasts[i].maxTemp;
        forecast.days[i].weatherId = (uint16_t)dayForecasts[i].conditionCode;
      }
    }

    AqiViewState &aqi = g_aqiCache[cityIndex];
    aqi.cityIndex = cityIndex;
    aqi.fetchState = aqiServiceGetStateForCity(cityIndex);
    aqi.ageSeconds = aqiServiceGetAgeSecondsForCity(cityIndex, millis());
    const AqiServiceData *aqiData = aqiServiceGetDataForCity(cityIndex);
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

  }
}

const CurrentWeatherViewState& getDisplayedWeather() {
  return g_weatherCache[g_activeCityIndex];
}

const ForecastViewState& getDisplayedForecast() {
  return g_forecastCache[g_activeCityIndex];
}

const AqiViewState& getDisplayedAqi() {
  return g_aqiCache[g_activeCityIndex];
}

void advanceRescueScreen() {
  if (g_currentPage == PAGE_MAIN) {
    if (g_activeCityIndex == 0) {
      setActiveCity(1, true);
    } else if (g_activeCityIndex == 1) {
      setActiveCity(2, true);
    } else {
      g_currentPage = PAGE_DEBUG;
      renderCurrentRescuePage();
    }
    return;
  }

  g_currentPage = PAGE_MAIN;
  setActiveCity(0, true);
  renderCurrentRescuePage();
}

void rescueDrawSeparator(uint16_t y) {
  tft.drawFastHLine(10, y, tft.width() - 30, 0x4228);
}

void rescueDrawAqiRowAligned(int labelX, int valueX, int squareX, int y,
                             const char *label, float value, uint16_t color, uint8_t decimals) {
  char valueBuf[16];

  if (value < 0.0f) {
    snprintf(valueBuf, sizeof(valueBuf), "--");
  } else if (decimals == 0) {
    snprintf(valueBuf, sizeof(valueBuf), "%d", (int)(value + 0.5f));
  } else {
    snprintf(valueBuf, sizeof(valueBuf), "%.1f", value);
  }

  drawTextWithSmoothFont(FONT_VLW_UI_18, String(label), labelX, y, TL_DATUM, TFT_WHITE);
  drawTextWithSmoothFont(FONT_VLW_UI_18, String(valueBuf), valueX, y, TL_DATUM, TFT_WHITE);
  tft.fillRect(squareX, y + 4, 10, 10, color);
}

static bool pointInRect(uint16_t x, uint16_t y, const RectangleDef &rect) {
  return x >= rect.x &&
         x < (rect.x + rect.width) &&
         y >= rect.y &&
         y < (rect.y + rect.height);
}

void formatIpAddressToText(const IPAddress &ip, char *out, size_t outSize) {
  snprintf(out, outSize, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

bool parseIpAddressFromText(const char *text, IPAddress &out) {
  int a = 0, b = 0, c = 0, d = 0;
  if (sscanf(text, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    return false;
  }
  if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
    return false;
  }
  out = IPAddress((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d);
  return true;
}

uint8_t countCharInText(const char *text, char needle) {
  uint8_t count = 0;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p == needle) {
      count++;
    }
  }
  return count;
}

bool canAppendIpChar(const char *current, char ch) {
  const size_t len = strlen(current);
  if (len >= 15) {
    return false;
  }

  if (ch == '.') {
    if (len == 0 || current[len - 1] == '.') {
      return false;
    }
    if (countCharInText(current, '.') >= 3) {
      return false;
    }
    return true;
  }

  if (ch < '0' || ch > '9') {
    return false;
  }

  const char *segmentStart = strrchr(current, '.');
  segmentStart = (segmentStart == nullptr) ? current : (segmentStart + 1);
  const size_t segmentLen = strlen(segmentStart);
  if (segmentLen >= 3) {
    return false;
  }

  char candidate[4] = {};
  strncpy(candidate, segmentStart, sizeof(candidate) - 1);
  candidate[segmentLen] = ch;
  candidate[segmentLen + 1] = '\0';
  return atoi(candidate) <= 255;
}

void loadMenuDraftFromProfile(uint8_t profileIndex) {
  if (profileIndex >= WIFI_PROFILE_COUNT) {
    return;
  }
  g_menuDraft.wifiProfileIndex = profileIndex;
  const WiFiProfile &profile = WIFI_PROFILES[profileIndex];
  strncpy(g_menuDraft.wifiSsid, profile.ssid, sizeof(g_menuDraft.wifiSsid) - 1);
  g_menuDraft.wifiSsid[sizeof(g_menuDraft.wifiSsid) - 1] = '\0';
  g_menuDraft.wifiUseDhcp = !profile.useStaticIp;
  strncpy(g_menuDraft.password, profile.password, sizeof(g_menuDraft.password) - 1);
  g_menuDraft.password[sizeof(g_menuDraft.password) - 1] = '\0';
  g_menuDraft.localIp = profile.localIp;
  g_menuDraft.subnet = profile.subnet;
  g_menuDraft.gateway = profile.gateway;
  g_menuDraft.dns = profile.dns;
  formatIpAddressToText(g_menuDraft.localIp, g_menuDraft.localIpText, sizeof(g_menuDraft.localIpText));
  formatIpAddressToText(g_menuDraft.subnet, g_menuDraft.subnetText, sizeof(g_menuDraft.subnetText));
  formatIpAddressToText(g_menuDraft.gateway, g_menuDraft.gatewayText, sizeof(g_menuDraft.gatewayText));
  formatIpAddressToText(g_menuDraft.dns, g_menuDraft.dnsText, sizeof(g_menuDraft.dnsText));
}

void loadMenuDraftFromRuntimeConfig() {
  g_menuDraft.wifiProfileIndex = 0xFF;
  strncpy(g_menuDraft.wifiSsid, g_runtimeWiFiConfig.ssid, sizeof(g_menuDraft.wifiSsid) - 1);
  g_menuDraft.wifiSsid[sizeof(g_menuDraft.wifiSsid) - 1] = '\0';
  g_menuDraft.wifiUseDhcp = !g_runtimeWiFiConfig.useStaticIp;
  strncpy(g_menuDraft.password, g_runtimeWiFiConfig.password, sizeof(g_menuDraft.password) - 1);
  g_menuDraft.password[sizeof(g_menuDraft.password) - 1] = '\0';
  g_menuDraft.localIp = g_runtimeWiFiConfig.localIp;
  g_menuDraft.subnet = g_runtimeWiFiConfig.subnet;
  g_menuDraft.gateway = g_runtimeWiFiConfig.gateway;
  g_menuDraft.dns = g_runtimeWiFiConfig.dns;
  formatIpAddressToText(g_menuDraft.localIp, g_menuDraft.localIpText, sizeof(g_menuDraft.localIpText));
  formatIpAddressToText(g_menuDraft.subnet, g_menuDraft.subnetText, sizeof(g_menuDraft.subnetText));
  formatIpAddressToText(g_menuDraft.gateway, g_menuDraft.gatewayText, sizeof(g_menuDraft.gatewayText));
  formatIpAddressToText(g_menuDraft.dns, g_menuDraft.dnsText, sizeof(g_menuDraft.dnsText));
}

void storeMenuDraftToRuntimeConfig() {
  g_runtimeWiFiConfig = {};
  g_runtimeWiFiConfig.valid = g_menuDraft.wifiSsid[0] != '\0';
  g_runtimeWiFiConfig.useStaticIp = !g_menuDraft.wifiUseDhcp;
  strncpy(g_runtimeWiFiConfig.ssid, g_menuDraft.wifiSsid, sizeof(g_runtimeWiFiConfig.ssid) - 1);
  strncpy(g_runtimeWiFiConfig.password, g_menuDraft.password, sizeof(g_runtimeWiFiConfig.password) - 1);
  g_runtimeWiFiConfig.localIp = g_menuDraft.localIp;
  g_runtimeWiFiConfig.subnet = g_menuDraft.subnet;
  g_runtimeWiFiConfig.gateway = g_menuDraft.gateway;
  g_runtimeWiFiConfig.dns = g_menuDraft.dns;
}

bool doesMenuDraftDifferFromRuntimeConfig() {
  if (!g_runtimeWiFiConfig.valid) {
    return true;
  }
  return strcmp(g_menuDraft.wifiSsid, g_runtimeWiFiConfig.ssid) != 0 ||
         strcmp(g_menuDraft.password, g_runtimeWiFiConfig.password) != 0 ||
         g_menuDraft.wifiUseDhcp != !g_runtimeWiFiConfig.useStaticIp ||
         g_menuDraft.localIp != g_runtimeWiFiConfig.localIp ||
         g_menuDraft.subnet != g_runtimeWiFiConfig.subnet ||
         g_menuDraft.gateway != g_runtimeWiFiConfig.gateway ||
         g_menuDraft.dns != g_runtimeWiFiConfig.dns;
}

const char* getMenuWifiApplyErrorText(MenuWifiApplyResult result) {
  switch (result) {
    case MENU_WIFI_APPLY_NO_SSID:
      return "Brak sieci SSID.";
    case MENU_WIFI_APPLY_AUTH:
      return u8"Błędne hasło lub AP odrzuca.";
    case MENU_WIFI_APPLY_DISCONNECTED:
      return u8"Brak/utrata połączenia.";
    case MENU_WIFI_APPLY_DNS:
      return u8"Połączono, ale brak DNS.";
    case MENU_WIFI_APPLY_EMPTY:
      return "Brak konfiguracji WiFi.";
    case MENU_WIFI_APPLY_UNKNOWN:
    default:
      return u8"Błąd połączenia, wróciłem.";
  }
}

MenuWifiApplyResult applyMenuWifiDraftToRuntime() {
  RuntimeWiFiConfig previousConfig = g_runtimeWiFiConfig;
  const bool hadPreviousConfig = isRuntimeWiFiConfigUsable();

  storeMenuDraftToRuntimeConfig();
  if (!isRuntimeWiFiConfigUsable()) {
    return MENU_WIFI_APPLY_EMPTY;
  }

  FW_LOG_I("[WIFI] applying runtime config ssid=%s mode=%s",
           g_runtimeWiFiConfig.ssid,
           g_runtimeWiFiConfig.useStaticIp ? "static" : "dhcp");

  WiFi.disconnect(true, true);
  delay(200);
  const bool connected = connectUsingRuntimeConfig();
  if (connected) {
    IPAddress dnsProbeIp;
    if (WiFi.status() == WL_CONNECTED && WiFi.hostByName("pool.ntp.org", dnsProbeIp) != 1) {
      log_w("[WIFI] DNS probe failed after connect");
      g_runtimeWiFiConfig = previousConfig;
      WiFi.disconnect(true, true);
      delay(200);
      if (hadPreviousConfig && isRuntimeWiFiConfigUsable()) {
        connectUsingRuntimeConfig();
      } else {
        startWiFi();
      }
      if (WiFi.status() == WL_CONNECTED) {
        setupOTA();
        syncTime();
      }
      return MENU_WIFI_APPLY_DNS;
    }
    if (WiFi.status() == WL_CONNECTED) {
      setupOTA();
      syncTime();
    }
    return MENU_WIFI_APPLY_OK;
  }

  log_w("[WIFI] runtime config failed, restoring previous connection");
  wl_status_t failedStatus = WiFi.status();
  g_runtimeWiFiConfig = previousConfig;
  WiFi.disconnect(true, true);
  delay(200);
  if (hadPreviousConfig && isRuntimeWiFiConfigUsable()) {
    if (connectUsingRuntimeConfig()) {
      if (WiFi.status() == WL_CONNECTED) {
        setupOTA();
        syncTime();
      }
    }
  } else {
    startWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      setupOTA();
      syncTime();
    }
  }

  switch (failedStatus) {
    case WL_NO_SSID_AVAIL:
      return MENU_WIFI_APPLY_NO_SSID;
    case WL_CONNECT_FAILED:
      return MENU_WIFI_APPLY_AUTH;
    case WL_DISCONNECTED:
    case WL_CONNECTION_LOST:
      return MENU_WIFI_APPLY_DISCONNECTED;
    default:
      return MENU_WIFI_APPLY_UNKNOWN;
  }
}

void resetTouchInteractionState() {
  g_touchWasActive = false;
  g_navLongPressTriggered = false;
  g_navOverlayTapArmed = false;
  g_touchConsumeUntilRelease = false;
  g_touchPressStartMs = 0;
  g_touchPressStartX = g_touchX;
  g_touchPressStartY = g_touchY;
  g_overlayPressedButton = -1;
  g_menuPressedTarget = 0;
}

void setMenuWifiStatus(const char *status) {
  strncpy(g_menuWifiStatusText, status, sizeof(g_menuWifiStatusText) - 1);
  g_menuWifiStatusText[sizeof(g_menuWifiStatusText) - 1] = '\0';
}

void requestMenuWifiScan(bool clearPreviousResults) {
  if (clearPreviousResults) {
    g_menuWifiScanCount = 0;
  }
  g_menuWifiScanRequested = true;
  g_menuWifiScanInProgress = false;
  g_menuWifiScanStartedMs = 0;
  g_menuWifiScanAttempt = 0;
  setMenuWifiStatus("Szukam sieci...");
}

void populateMenuWifiSavedList() {
  g_menuWifiScanCount = min<uint8_t>(WIFI_PROFILE_COUNT, 8);
  g_menuWifiScanRequested = false;
  g_menuWifiScanInProgress = false;
  g_menuWifiScanStartedMs = 0;
  g_menuWifiScanAttempt = 0;
  for (uint8_t i = 0; i < g_menuWifiScanCount; i++) {
    strncpy(g_menuWifiScan[i].ssid, WIFI_PROFILES[i].ssid, sizeof(g_menuWifiScan[i].ssid) - 1);
    g_menuWifiScan[i].ssid[sizeof(g_menuWifiScan[i].ssid) - 1] = '\0';
    g_menuWifiScan[i].secure = WIFI_PROFILES[i].password != nullptr && WIFI_PROFILES[i].password[0] != '\0';
    g_menuWifiScan[i].rssi = -100;
  }
  setMenuWifiStatus("Zapisane profile");
}

void initMenuDraftState() {
  if (isRuntimeWiFiConfigUsable()) {
    loadMenuDraftFromRuntimeConfig();
  } else {
    const uint8_t defaultProfile = (g_wifiProfileIndex < WIFI_PROFILE_COUNT) ? g_wifiProfileIndex : 0;
    loadMenuDraftFromProfile(defaultProfile);
  }
  g_menuDraft.showSeconds = false;
  g_menuDraft.showForecast = true;
  g_menuDraft.showSun = true;
  g_menuDraft.showAqi = true;
  g_menuDraft.showRad = true;
  g_menuDraft.brightnessMode = (uint8_t)g_lcdBrightnessMode;
  g_menuDraft.rgbR = 0;
  g_menuDraft.rgbG = 0;
  g_menuDraft.rgbB = 0;
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    g_menuDraft.cityColors[city] = g_cityUiColors[city];
  }
  g_menuWifiScreen = MENU_WIFI_HOME;
  g_menuWifiIpField = MENU_WIFI_IP_LOCAL;
  g_menuWifiShowPassword = false;
  g_menuWifiKeyboardUpper = false;
  g_menuWifiPasswordInput[0] = '\0';
  g_menuWifiPasswordEdited = false;
  g_menuWifiScanCount = 0;
  g_menuWifiPressedCode = 0;
  g_menuWifiScanRequested = false;
  g_menuWifiScanInProgress = false;
  g_menuWifiScanStartedMs = 0;
  g_menuWifiScanAttempt = 0;
  g_menuWifiStatusText[0] = '\0';
  g_menuWifiApplyFailed = false;
  g_menuColorsSelectedCity = 0;
  g_menuColorsStatusText[0] = '\0';
}

void clearMenuRenderCaches() {
  g_lastMenuRenderKey[0] = '\0';
  g_lastMenuWifiKey[0] = '\0';
  g_lastMenuViewKey[0] = '\0';
  g_lastMenuColorsKey[0] = '\0';
  g_lastMenuRgbKey[0] = '\0';
  g_lastMenuTouchKey[0] = '\0';
  g_lastMenuTouchCursorX = -1;
  g_lastMenuTouchCursorY = -1;
  g_lastMenuTouchCursorVisible = false;
  g_lastMenuScreenRendered = 0xFF;
  g_lastMenuWifiScreenRendered = 0xFF;
  g_menuWifiDirtyFlags = 0xFFFFFFFFUL;
}

uint32_t getMenuStaticDirtyFlag(MenuScreen screen) {
  switch (screen) {
    case MENU_HOME: return MENU_DIRTY_HOME_STATIC;
    case MENU_WIFI: return MENU_DIRTY_WIFI_STATIC;
    case MENU_VIEW: return MENU_DIRTY_VIEW_STATIC;
    case MENU_BRIGHTNESS: return MENU_DIRTY_BRIGHT_STATIC;
    case MENU_COLORS: return MENU_DIRTY_COLORS_STATIC;
    case MENU_RGB:  return MENU_DIRTY_RGB_STATIC;
    case MENU_TOUCH:return MENU_DIRTY_TOUCH_STATIC;
    default:        return 0;
  }
}

uint32_t getMenuDynamicDirtyFlag(MenuScreen screen) {
  switch (screen) {
    case MENU_HOME: return MENU_DIRTY_HOME_DYNAMIC;
    case MENU_WIFI: return MENU_DIRTY_WIFI_DYNAMIC;
    case MENU_VIEW: return MENU_DIRTY_VIEW_DYNAMIC;
    case MENU_BRIGHTNESS: return MENU_DIRTY_BRIGHT_DYNAMIC;
    case MENU_COLORS: return MENU_DIRTY_COLORS_DYNAMIC;
    case MENU_RGB:  return MENU_DIRTY_RGB_DYNAMIC;
    case MENU_TOUCH:return MENU_DIRTY_TOUCH_DYNAMIC;
    default:        return 0;
  }
}

void invalidateMenuFull() {
  g_menuDirtyFlags = MENU_DIRTY_FULL |
                     MENU_DIRTY_HOME_STATIC | MENU_DIRTY_HOME_DYNAMIC |
                     MENU_DIRTY_WIFI_STATIC | MENU_DIRTY_WIFI_DYNAMIC |
                     MENU_DIRTY_VIEW_STATIC | MENU_DIRTY_VIEW_DYNAMIC |
                     MENU_DIRTY_BRIGHT_STATIC | MENU_DIRTY_BRIGHT_DYNAMIC |
                     MENU_DIRTY_COLORS_STATIC | MENU_DIRTY_COLORS_DYNAMIC |
                     MENU_DIRTY_RGB_STATIC | MENU_DIRTY_RGB_DYNAMIC |
                     MENU_DIRTY_TOUCH_STATIC | MENU_DIRTY_TOUCH_DYNAMIC;
}

void invalidateMenuScreen(MenuScreen screen, bool redrawStatic, bool redrawDynamic) {
  if (redrawStatic) {
    g_menuDirtyFlags |= getMenuStaticDirtyFlag(screen);
  }
  if (redrawDynamic) {
    g_menuDirtyFlags |= getMenuDynamicDirtyFlag(screen);
  }
}

void invalidateCurrentMenuScreen(bool redrawStatic, bool redrawDynamic) {
  invalidateMenuScreen(g_menuScreen, redrawStatic, redrawDynamic);
}

void invalidateMenuWifiRegions(uint32_t flags) {
  g_menuWifiDirtyFlags |= flags;
  invalidateMenuScreen(MENU_WIFI, false, true);
}

void drawMenuHeader(const char *title) {
  drawTextWithSmoothFont(FONT_VLW_UI_20, String(title), centerWidth, 14, TC_DATUM, TFT_WHITE);
  tft.drawRoundRect(MENU_BACK_BUTTON.x, MENU_BACK_BUTTON.y, MENU_BACK_BUTTON.width, MENU_BACK_BUTTON.height, 5, TFT_DARKGREY);
  drawTextWithSmoothFont(FONT_VLW_UI_14, String("Wróć"), MENU_BACK_BUTTON.x + (MENU_BACK_BUTTON.width / 2), MENU_BACK_BUTTON.y + 4, TC_DATUM, TFT_WHITE);
}

void drawMenuCloseButton(const char *label) {
  tft.drawRoundRect(MENU_BACK_BUTTON.x, MENU_BACK_BUTTON.y, MENU_BACK_BUTTON.width, MENU_BACK_BUTTON.height, 5, TFT_DARKGREY);
  drawTextWithSmoothFont(FONT_VLW_UI_14, String(label), MENU_BACK_BUTTON.x + (MENU_BACK_BUTTON.width / 2), MENU_BACK_BUTTON.y + 4, TC_DATUM, TFT_WHITE);
}

void drawMenuButton(const RectangleDef &rect, const char *label, uint16_t color, bool active, bool pressed) {
  const uint16_t border = pressed ? TFT_WHITE : (active ? TFT_WHITE : color);
  const uint16_t fill = pressed ? 0x4228 : (active ? 0x18E3 : TFT_BLACK);
  const uint16_t text = pressed ? TFT_WHITE : (active ? TFT_WHITE : color);
  tft.fillRoundRect(rect.x, rect.y, rect.width, rect.height, 6, fill);
  tft.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 6, border);
  drawTextWithSmoothFont(FONT_VLW_UI_14, String(label), rect.x + (rect.width / 2), rect.y + 4, TC_DATUM, text);
}

uint16_t getMenuPaletteColor(uint8_t index) {
  static const uint16_t palette[] = {
    TFT_RED, TFT_GREEN, TFT_BLUE,
    TFT_CYAN, TFT_YELLOW, TFT_MAGENTA,
    TFT_ORANGE, TFT_PINK, TFT_GREENYELLOW
  };
  const uint8_t paletteSize = sizeof(palette) / sizeof(palette[0]);
  return (index < paletteSize) ? palette[index] : TFT_WHITE;
}

uint8_t getMenuPaletteColorCount() {
  return 9;
}

RectangleDef getMenuPaletteRect(uint8_t index) {
  const uint8_t col = index % 3U;
  const uint8_t row = index / 3U;
  RectangleDef rect = {};
  rect.x = (uint16_t)(28 + (int16_t)col * 92);
  rect.y = (uint16_t)(190 + (int16_t)row * 64);
  rect.width = 48;
  rect.height = 48;
  return rect;
}

void drawMenuColorSwatch(uint8_t index, bool selected) {
  const RectangleDef rect = getMenuPaletteRect(index);
  const uint16_t color = getMenuPaletteColor(index);
  bool occupiedByOtherCity = false;
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    if (city != g_menuColorsSelectedCity && g_cityUiColors[city] == color) {
      occupiedByOtherCity = true;
      break;
    }
  }
  tft.fillRoundRect(rect.x, rect.y, rect.width, rect.height, 6, color);
  tft.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 6, selected ? TFT_WHITE : 0x39C7);
  if (selected) {
    tft.drawRoundRect(rect.x - 2, rect.y - 2, rect.width + 4, rect.height + 4, 8, TFT_WHITE);
  } else if (occupiedByOtherCity) {
    tft.drawLine(rect.x + 8, rect.y + 8, rect.x + rect.width - 9, rect.y + rect.height - 9, TFT_BLACK);
    tft.drawLine(rect.x + 8, rect.y + rect.height - 9, rect.x + rect.width - 9, rect.y + 8, TFT_BLACK);
  }
}

void drawMenuWifiIpField(const RectangleDef &rect, const char *label, const char *value, bool active, bool enabled) {
  const uint16_t border = !enabled ? TFT_DARKGREY : (active ? TFT_WHITE : 0x39C7);
  const uint16_t textColor = enabled ? TFT_WHITE : TFT_DARKGREY;
  drawTextWithSmoothFont(FONT_VLW_UI_16, String(label), 24, rect.y + 2, TL_DATUM, TFT_DARKGREY);
  tft.fillRoundRect(rect.x, rect.y, rect.width, rect.height, 4, TFT_BLACK);
  tft.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 4, border);
  drawTextWithSmoothFont(FONT_VLW_UI_16, String(value), rect.x + 8, rect.y + 4, TL_DATUM, textColor);
  if (active && enabled) {
    const int16_t caretX = rect.x + 8 + measureTextWithSmoothFont(FONT_VLW_UI_16, String(value)) + 2;
    tft.drawFastVLine(min<int16_t>(caretX, rect.x + rect.width - 6), rect.y + 5, rect.height - 10, TFT_WHITE);
  }
}

void scanMenuWifiNetworks() {
  g_menuWifiScanCount = 0;
  const int networkCount = WiFi.scanComplete();
  if (networkCount <= 0) {
    if (networkCount == 0) {
      setMenuWifiStatus("Brak sieci.");
    } else {
      setMenuWifiStatus("Skan nieudany.");
    }
    return;
  }

  const uint8_t cappedCount = (uint8_t)min(networkCount, 8);
  for (uint8_t i = 0; i < cappedCount; i++) {
    String ssid = WiFi.SSID(i);
    strncpy(g_menuWifiScan[i].ssid, ssid.c_str(), sizeof(g_menuWifiScan[i].ssid) - 1);
    g_menuWifiScan[i].ssid[sizeof(g_menuWifiScan[i].ssid) - 1] = '\0';
    g_menuWifiScan[i].rssi = WiFi.RSSI(i);
    g_menuWifiScan[i].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  g_menuWifiScanCount = cappedCount;
  char statusBuf[48];
  snprintf(statusBuf, sizeof(statusBuf), "Znaleziono %u sieci", (unsigned)cappedCount);
  setMenuWifiStatus(statusBuf);
  WiFi.scanDelete();
}

void serviceMenuWifiScan(uint32_t nowMs) {
  if (!g_menuWifiScanRequested && !g_menuWifiScanInProgress) {
    return;
  }

  if (g_menuWifiScanRequested && !g_menuWifiScanInProgress) {
    g_menuWifiScanRequested = false;
    g_menuWifiScanInProgress = true;
    g_menuWifiScanStartedMs = nowMs;
    g_menuWifiScanAttempt++;
    g_menuWifiScanCount = 0;
    setMenuWifiStatus("Szukam sieci...");
    WiFi.scanDelete();
    const int scanStart = WiFi.scanNetworks(true, true);
    if (scanStart == WIFI_SCAN_FAILED) {
      if (g_menuWifiScanAttempt < 2) {
        g_menuWifiScanRequested = true;
      } else {
        g_menuWifiScanInProgress = false;
        setMenuWifiStatus("Skan nieudany.");
      }
      if (g_currentPage == PAGE_MENU && g_menuScreen == MENU_WIFI && g_menuWifiScreen == MENU_WIFI_NETWORKS) {
        invalidateMenuScreen(MENU_WIFI, false, true);
        updateRescueMenu(false);
      }
    }
    return;
  }

  const int scanState = WiFi.scanComplete();
  if (scanState == WIFI_SCAN_RUNNING) {
    if ((nowMs - g_menuWifiScanStartedMs) > 15000U) {
      WiFi.scanDelete();
      g_menuWifiScanInProgress = false;
      setMenuWifiStatus("Przekroczono czas skanu.");
      if (g_currentPage == PAGE_MENU && g_menuScreen == MENU_WIFI && g_menuWifiScreen == MENU_WIFI_NETWORKS) {
        invalidateMenuScreen(MENU_WIFI, false, true);
        updateRescueMenu(false);
      }
    }
    return;
  }

  if (scanState == WIFI_SCAN_FAILED) {
    g_menuWifiScanInProgress = false;
    if (g_menuWifiScanAttempt < 2) {
      requestMenuWifiScan(false);
    } else {
      setMenuWifiStatus("Skan nieudany.");
    }
    if (g_currentPage == PAGE_MENU && g_menuScreen == MENU_WIFI && g_menuWifiScreen == MENU_WIFI_NETWORKS) {
      invalidateMenuScreen(MENU_WIFI, false, true);
      updateRescueMenu(false);
    }
    return;
  }

  g_menuWifiScanInProgress = false;
  scanMenuWifiNetworks();
  if (g_menuWifiScanCount == 0 && g_menuWifiScanAttempt < 2) {
    requestMenuWifiScan(false);
  }
  if (g_currentPage == PAGE_MENU && g_menuScreen == MENU_WIFI && g_menuWifiScreen == MENU_WIFI_NETWORKS) {
    invalidateMenuScreen(MENU_WIFI, false, true);
    updateRescueMenu(false);
  }
}

char* getActiveMenuWifiIpText() {
  switch (g_menuWifiIpField) {
    case MENU_WIFI_IP_LOCAL:
      return g_menuDraft.localIpText;
    case MENU_WIFI_IP_SUBNET:
      return g_menuDraft.subnetText;
    case MENU_WIFI_IP_GATEWAY:
      return g_menuDraft.gatewayText;
    case MENU_WIFI_IP_DNS:
    default:
      return g_menuDraft.dnsText;
  }
}

void drawMenuCard(const RectangleDef &rect, const char *title, uint16_t color) {
  tft.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 8, color);
  drawTextWithSmoothFont(FONT_VLW_UI_18, String(title), rect.x + (rect.width / 2), rect.y + 18, TC_DATUM, color);
}

void drawMenuCheckbox(const RectangleDef &rect, const char *label, bool checked, uint16_t color) {
  tft.fillRect(rect.x, rect.y, rect.width, rect.height, TFT_BLACK);
  tft.drawRect(rect.x, rect.y, rect.width, rect.height, color);
  drawMenuCheckboxState(rect, checked, color);
  drawTextWithSmoothFont(FONT_VLW_UI_18, String(label), rect.x + rect.width + 12, rect.y - 2, TL_DATUM, TFT_WHITE);
}

void drawMenuCheckboxState(const RectangleDef &rect, bool checked, uint16_t color) {
  tft.fillRect(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, TFT_BLACK);
  tft.drawRect(rect.x, rect.y, rect.width, rect.height, color);
  if (!checked) {
    return;
  }

  const int16_t x1 = rect.x + 4;
  const int16_t y1 = rect.y + 12;
  const int16_t x2 = rect.x + 9;
  const int16_t y2 = rect.y + 17;
  const int16_t x3 = rect.x + 17;
  const int16_t y3 = rect.y + 6;
  tft.drawLine(x1, y1, x2, y2, TFT_RED);
  tft.drawLine(x1, y1 + 1, x2, y2 + 1, TFT_RED);
  tft.drawLine(x2, y2, x3, y3, TFT_RED);
  tft.drawLine(x2, y2 + 1, x3, y3 + 1, TFT_RED);
}

void applyBrightnessMode(uint8_t mode) {
  g_menuDraft.brightnessMode = mode;
  g_lcdBrightnessMode = (mode <= LCD_MODE_NIGHT) ? (LcdBrightnessMode)mode : LCD_MODE_AUTO;
  g_lastNightOverrideBadgeValid = false;
  if (g_currentPage == PAGE_MAIN) {
    drawMainNightOverrideBadge(true);
  }
}

void drawMenuRssiBars(int16_t x, int16_t y, int32_t rssi, uint16_t color) {
  uint8_t bars = 0;
  if (rssi >= -67) bars = 4;
  else if (rssi >= -75) bars = 3;
  else if (rssi >= -85) bars = 2;
  else if (rssi >= -95) bars = 1;

  for (uint8_t i = 0; i < 4; i++) {
    const int16_t barH = 5 + (i * 4);
    const int16_t barX = x + (i * 6);
    const int16_t barY = y - barH;
    const uint16_t barColor = (i < bars) ? color : 0x2945;
    tft.fillRect(barX, barY, 4, barH, barColor);
  }
}

void drawMenuLockIcon(int16_t x, int16_t y, uint16_t color) {
  tft.drawRoundRect(x, y + 8, 10, 8, 2, color);
  tft.drawCircle(x + 5, y + 7, 4, color);
  tft.drawFastVLine(x + 1, y + 7, 2, TFT_BLACK);
  tft.drawFastVLine(x + 9, y + 7, 2, TFT_BLACK);
}

void drawMenuIpLine(const char *label, const IPAddress &ip, int16_t y) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  drawTextWithSmoothFont(FONT_VLW_UI_16, String(label), 24, y, TL_DATUM, TFT_DARKGREY);
  drawTextWithSmoothFont(FONT_VLW_UI_16, String(buf), 126, y, TL_DATUM, TFT_WHITE);
}

void updateMenuRgbFromTouch(uint16_t touchY) {
  auto mapSlider = [&](const RectangleDef &slider) -> uint8_t {
    int32_t clamped = touchY;
    if (clamped < slider.y) clamped = slider.y;
    if (clamped > (slider.y + slider.height)) clamped = slider.y + slider.height;
    const int32_t offset = clamped - slider.y;
    return (uint8_t)(255 - ((offset * 255) / slider.height));
  };

  if (pointInRect(g_touchX, g_touchY, MENU_RGB_SLIDER_R)) {
    g_menuDraft.rgbR = mapSlider(MENU_RGB_SLIDER_R);
  } else if (pointInRect(g_touchX, g_touchY, MENU_RGB_SLIDER_G)) {
    g_menuDraft.rgbG = mapSlider(MENU_RGB_SLIDER_G);
  } else if (pointInRect(g_touchX, g_touchY, MENU_RGB_SLIDER_B)) {
    g_menuDraft.rgbB = mapSlider(MENU_RGB_SLIDER_B);
  }
}

bool isPointInMenuTouchGrid(uint16_t x, uint16_t y) {
  return x >= 20U && x < (uint16_t)(tft.width() - 20) && y >= 86U && y < 406U;
}

void redrawMenuTouchGridRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
  const int16_t gridX = 20;
  const int16_t gridY = 86;
  const int16_t gridW = tft.width() - 40;
  const int16_t gridH = 320;
  const int16_t right = min<int16_t>(x + w, gridX + gridW);
  const int16_t bottom = min<int16_t>(y + h, gridY + gridH);
  x = max<int16_t>(x, gridX);
  y = max<int16_t>(y, gridY);
  if (right <= x || bottom <= y) {
    return;
  }

  tft.fillRect(x, y, right - x, bottom - y, TFT_BLACK);

  for (int16_t gridLineX = gridX; gridLineX < (gridX + gridW); gridLineX += 30) {
    if (gridLineX < x || gridLineX >= right) {
      continue;
    }
    tft.drawFastVLine(gridLineX, y, bottom - y, 0x18C3);
  }
  for (int16_t gridLineY = gridY; gridLineY < (gridY + gridH); gridLineY += 30) {
    if (gridLineY < y || gridLineY >= bottom) {
      continue;
    }
    tft.drawFastHLine(x, gridLineY, right - x, 0x18C3);
  }
}

void drawMenuWifiKeyboard() {
  static const char *rows[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const int16_t keyW = 26;
  const int16_t keyH = 24;
  const int16_t startX = 16;
  const int16_t startY = 154;
  for (uint8_t row = 0; row < 4; row++) {
    const size_t len = strlen(rows[row]);
    int16_t rowX = startX + (int16_t)((10 - len) * (keyW + 2) / 2);
    for (uint8_t col = 0; col < len; col++) {
      const uint16_t keyX = (uint16_t)(rowX + (int16_t)col * (keyW + 2));
      const uint16_t keyY = (uint16_t)(startY + (int16_t)row * (keyH + 8));
      RectangleDef keyRect = {keyX, keyY, (uint16_t)keyW, (uint16_t)keyH};
      char label[2] = {rows[row][col], '\0'};
      if (g_menuWifiKeyboardUpper && label[0] >= 'a' && label[0] <= 'z') {
        label[0] = (char)(label[0] - 'a' + 'A');
      }
      const int16_t keyIndex = (int16_t)(40 + row * 10 + col);
      drawMenuButton(keyRect, label, 0x5ACB, false, g_menuWifiPressedCode == keyIndex);
    }
  }
  drawMenuButton({18, 286, 50, 26}, g_menuWifiKeyboardUpper ? "abc" : "ABC", COLOR_GRAPH, false, g_menuWifiPressedCode == 90);
  drawMenuButton({76, 286, 70, 26}, "Spacja", COLOR_GRAPH, false, g_menuWifiPressedCode == 91);
  drawMenuButton({154, 286, 40, 26}, ".", COLOR_GRAPH, false, g_menuWifiPressedCode == 92);
  drawMenuButton({202, 286, 98, 26}, "Usuń", COLOR_DEBUG, false, g_menuWifiPressedCode == 93);
}

int16_t getMenuWifiKeyboardKeyAt(uint16_t x, uint16_t y, char *outChar, bool *isAction, const char **action) {
  static const char *rows[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const int16_t keyW = 26;
  const int16_t keyH = 24;
  const int16_t startX = 16;
  const int16_t startY = 154;
  *isAction = false;
  *action = nullptr;
  *outChar = '\0';

  for (uint8_t row = 0; row < 4; row++) {
    const size_t len = strlen(rows[row]);
    int16_t rowX = startX + (int16_t)((10 - len) * (keyW + 2) / 2);
    for (uint8_t col = 0; col < len; col++) {
      const uint16_t keyX = (uint16_t)(rowX + (int16_t)col * (keyW + 2));
      const uint16_t keyY = (uint16_t)(startY + (int16_t)row * (keyH + 8));
      RectangleDef keyRect = {keyX, keyY, (uint16_t)keyW, (uint16_t)keyH};
      if (pointInRect(x, y, keyRect)) {
        char c = rows[row][col];
        if (g_menuWifiKeyboardUpper && c >= 'a' && c <= 'z') {
          c = (char)(c - 'a' + 'A');
        }
        *outChar = c;
        return (int16_t)(40 + row * 10 + col);
      }
    }
  }

  if (pointInRect(x, y, {18, 286, 50, 26})) {
    *isAction = true; *action = "shift"; return 90;
  }
  if (pointInRect(x, y, {76, 286, 70, 26})) {
    *isAction = true; *action = "space"; return 91;
  }
  if (pointInRect(x, y, {154, 286, 40, 26})) {
    *outChar = '.'; return 92;
  }
  if (pointInRect(x, y, {202, 286, 98, 26})) {
    *isAction = true; *action = "backspace"; return 93;
  }
  return -1;
}

void decodeMenuWifiKeyboardCode(int16_t code, char *outChar, bool *isAction, const char **action) {
  *outChar = '\0';
  *isAction = false;
  *action = nullptr;

  if (code >= 40 && code <= 49) {
    const char *row = "1234567890";
    *outChar = row[code - 40];
    return;
  }
  if (code >= 50 && code <= 59) {
    const char *row = "qwertyuiop";
    char c = row[code - 50];
    *outChar = g_menuWifiKeyboardUpper ? (char)(c - 'a' + 'A') : c;
    return;
  }
  if (code >= 60 && code <= 68) {
    const char *row = "asdfghjkl";
    char c = row[code - 60];
    *outChar = g_menuWifiKeyboardUpper ? (char)(c - 'a' + 'A') : c;
    return;
  }
  if (code >= 70 && code <= 76) {
    const char *row = "zxcvbnm";
    char c = row[code - 70];
    *outChar = g_menuWifiKeyboardUpper ? (char)(c - 'a' + 'A') : c;
    return;
  }
  if (code == 90) { *isAction = true; *action = "shift"; return; }
  if (code == 91) { *isAction = true; *action = "space"; return; }
  if (code == 92) { *outChar = '.'; return; }
  if (code == 93) { *isAction = true; *action = "backspace"; return; }
}

void drawMenuWifiNumpad() {
  static const char *labels[12] = {"1","2","3","4","5","6","7","8","9",".","0","BS"};
  const int16_t startX = 56;
  const int16_t startY = 238;
  const int16_t keyW = 58;
  const int16_t keyH = 32;
  for (uint8_t i = 0; i < 12; i++) {
    const uint8_t row = i / 3;
    const uint8_t col = i % 3;
    const uint16_t keyX = (uint16_t)(startX + (int16_t)col * (keyW + 8));
    const uint16_t keyY = (uint16_t)(startY + (int16_t)row * (keyH + 8));
    RectangleDef rect = {keyX, keyY, (uint16_t)keyW, (uint16_t)keyH};
    drawMenuButton(rect, labels[i], (i == 11) ? COLOR_DEBUG : COLOR_GRAPH, false, g_menuWifiPressedCode == (120 + i));
  }
}

void drawMenuWifiPasswordTopButtons() {
  drawMenuButton(MENU_WIFI_PASSWORD_SHOW_BUTTON, g_menuWifiShowPassword ? "Ukryj" : "Pokaż", COLOR_MENU, false, g_menuWifiPressedCode == 30);
  drawMenuButton(MENU_WIFI_PASSWORD_CLEAR_BUTTON, "Czyść", COLOR_DEBUG, false, g_menuWifiPressedCode == 31);
  drawMenuButton(MENU_WIFI_PASSWORD_DONE_BUTTON, "OK", COLOR_GRAPH, false, g_menuWifiPressedCode == 32);
}

void drawMenuWifiIpTopBar() {
  tft.fillRect(88, 52, 220, 34, TFT_BLACK);
  drawMenuButton(MENU_WIFI_IP_DHCP_TOGGLE, g_menuDraft.wifiUseDhcp ? "DHCP" : "Statyczne", COLOR_KIELCE, true, g_menuWifiPressedCode == 100);
  drawMenuButton(MENU_WIFI_IP_DONE_BUTTON, "OK", COLOR_MENU, false, g_menuWifiPressedCode == 101);
}

void drawMenuWifiKeyboardKeyByCode(int16_t code) {
  static const char *rows[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const int16_t keyW = 26;
  const int16_t keyH = 24;
  const int16_t startX = 16;
  const int16_t startY = 154;

  if (code >= 40 && code <= 76) {
    uint8_t row = 0;
    uint8_t col = 0;
    if (code <= 49) {
      row = 0; col = (uint8_t)(code - 40);
    } else if (code <= 59) {
      row = 1; col = (uint8_t)(code - 50);
    } else if (code <= 68) {
      row = 2; col = (uint8_t)(code - 60);
    } else {
      row = 3; col = (uint8_t)(code - 70);
    }
    const size_t len = strlen(rows[row]);
    const int16_t rowX = startX + (int16_t)((10 - len) * (keyW + 2) / 2);
    const uint16_t keyX = (uint16_t)(rowX + (int16_t)col * (keyW + 2));
    const uint16_t keyY = (uint16_t)(startY + (int16_t)row * (keyH + 8));
    RectangleDef keyRect = {keyX, keyY, (uint16_t)keyW, (uint16_t)keyH};
    char label[2] = {rows[row][col], '\0'};
    if (g_menuWifiKeyboardUpper && label[0] >= 'a' && label[0] <= 'z') {
      label[0] = (char)(label[0] - 'a' + 'A');
    }
    drawMenuButton(keyRect, label, 0x5ACB, false, g_menuWifiPressedCode == code);
    return;
  }

  if (code == 90) {
    drawMenuButton({18, 286, 50, 26}, g_menuWifiKeyboardUpper ? "abc" : "ABC", COLOR_GRAPH, false, g_menuWifiPressedCode == 90);
  } else if (code == 91) {
    drawMenuButton({76, 286, 70, 26}, "Spacja", COLOR_GRAPH, false, g_menuWifiPressedCode == 91);
  } else if (code == 92) {
    drawMenuButton({154, 286, 40, 26}, ".", COLOR_GRAPH, false, g_menuWifiPressedCode == 92);
  } else if (code == 93) {
    drawMenuButton({202, 286, 98, 26}, "Usuń", COLOR_DEBUG, false, g_menuWifiPressedCode == 93);
  }
}

void drawMenuWifiNumpadKeyByCode(int16_t code) {
  static const char *labels[12] = {"1","2","3","4","5","6","7","8","9",".","0","BS"};
  if (code < 120 || code > 131) {
    return;
  }
  const uint8_t idx = (uint8_t)(code - 120);
  const int16_t startX = 56;
  const int16_t startY = 238;
  const int16_t keyW = 58;
  const int16_t keyH = 32;
  const uint8_t row = idx / 3;
  const uint8_t col = idx % 3;
  const uint16_t keyX = (uint16_t)(startX + (int16_t)col * (keyW + 8));
  const uint16_t keyY = (uint16_t)(startY + (int16_t)row * (keyH + 8));
  RectangleDef rect = {keyX, keyY, (uint16_t)keyW, (uint16_t)keyH};
  drawMenuButton(rect, labels[idx], (idx == 11) ? COLOR_DEBUG : COLOR_GRAPH, false, g_menuWifiPressedCode == code);
}

void drawMenuWifiIpFieldByCode(int16_t code) {
  const bool enabled = !g_menuDraft.wifiUseDhcp;
  switch (code) {
    case 110:
      drawMenuWifiIpField(MENU_WIFI_IP_LOCAL_FIELD, "IP", g_menuDraft.localIpText, g_menuWifiIpField == MENU_WIFI_IP_LOCAL, enabled);
      break;
    case 111:
      drawMenuWifiIpField(MENU_WIFI_IP_SUBNET_FIELD, "Maska", g_menuDraft.subnetText, g_menuWifiIpField == MENU_WIFI_IP_SUBNET, enabled);
      break;
    case 112:
      drawMenuWifiIpField(MENU_WIFI_IP_GATEWAY_FIELD, "Brama", g_menuDraft.gatewayText, g_menuWifiIpField == MENU_WIFI_IP_GATEWAY, enabled);
      break;
    case 113:
      drawMenuWifiIpField(MENU_WIFI_IP_DNS_FIELD, "DNS", g_menuDraft.dnsText, g_menuWifiIpField == MENU_WIFI_IP_DNS, enabled);
      break;
    default:
      break;
  }
}

void redrawMenuWifiPasswordPressedControl(int16_t code) {
  if (code >= 40 && code <= 93) {
    drawMenuWifiKeyboardKeyByCode(code);
    return;
  }
  if (code == 30 || code == 31 || code == 32) {
    drawMenuWifiPasswordTopButtons();
  }
}

void redrawMenuWifiIpPressedControl(int16_t code) {
  if (code >= 120 && code <= 131) {
    drawMenuWifiNumpadKeyByCode(code);
    return;
  }
  if (code >= 110 && code <= 113) {
    drawMenuWifiIpFieldByCode(code);
    return;
  }
  if (code == 100 || code == 101) {
    drawMenuWifiIpTopBar();
  }
}

void redrawMenuWifiPasswordTextRegion() {
  tft.fillRect(90, 46, 214, 24, TFT_BLACK);
  tft.fillRect(90, 80, 214, 28, TFT_BLACK);
  drawTextWithSmoothFont(FONT_VLW_UI_16, String(g_menuDraft.wifiSsid), 92, 52, TL_DATUM, TFT_WHITE);

  const bool hasTypedPassword = strlen(g_menuWifiPasswordInput) > 0;
  const bool hasStoredPassword = strlen(g_menuDraft.password) > 0;
  String passwordValue;
  if (hasTypedPassword) {
    if (g_menuWifiShowPassword) {
      passwordValue = String(g_menuWifiPasswordInput);
    } else {
      const size_t typedLen = strlen(g_menuWifiPasswordInput);
      char masked[64];
      const size_t maskedLen = min<size_t>(typedLen, sizeof(masked) - 1);
      memset(masked, '-', maskedLen);
      masked[maskedLen] = '\0';
      passwordValue = String(masked);
    }
  } else if (g_menuWifiPasswordEdited) {
    passwordValue = String("");
  } else if (hasStoredPassword) {
    passwordValue = String("(zapisane)");
  } else {
    passwordValue = String("(puste)");
  }
  drawTextWithSmoothFont(FONT_VLW_UI_16, passwordValue, 92, 86, TL_DATUM, TFT_WHITE);
  if (hasTypedPassword || g_menuWifiPasswordEdited) {
    const int16_t pwdCaretX = 92 + measureTextWithSmoothFont(FONT_VLW_UI_16, passwordValue) + 2;
    tft.drawFastVLine(min<int16_t>(pwdCaretX, 300), 84, 18, TFT_WHITE);
  }
}

void redrawAllMenuWifiIpFields() {
  const bool enabled = !g_menuDraft.wifiUseDhcp;
  drawMenuWifiIpField(MENU_WIFI_IP_LOCAL_FIELD, "IP", g_menuDraft.localIpText, g_menuWifiIpField == MENU_WIFI_IP_LOCAL, enabled);
  drawMenuWifiIpField(MENU_WIFI_IP_SUBNET_FIELD, "Maska", g_menuDraft.subnetText, g_menuWifiIpField == MENU_WIFI_IP_SUBNET, enabled);
  drawMenuWifiIpField(MENU_WIFI_IP_GATEWAY_FIELD, "Brama", g_menuDraft.gatewayText, g_menuWifiIpField == MENU_WIFI_IP_GATEWAY, enabled);
  drawMenuWifiIpField(MENU_WIFI_IP_DNS_FIELD, "DNS", g_menuDraft.dnsText, g_menuWifiIpField == MENU_WIFI_IP_DNS, enabled);
}

int16_t getMenuWifiNumpadKeyAt(uint16_t x, uint16_t y, const char **action) {
  static const char *labels[12] = {"1","2","3","4","5","6","7","8","9",".","0","backspace"};
  const int16_t startX = 56;
  const int16_t startY = 238;
  const int16_t keyW = 58;
  const int16_t keyH = 32;
  for (uint8_t i = 0; i < 12; i++) {
    const uint8_t row = i / 3;
    const uint8_t col = i % 3;
    const uint16_t keyX = (uint16_t)(startX + (int16_t)col * (keyW + 8));
    const uint16_t keyY = (uint16_t)(startY + (int16_t)row * (keyH + 8));
    RectangleDef rect = {keyX, keyY, (uint16_t)keyW, (uint16_t)keyH};
    if (pointInRect(x, y, rect)) {
      *action = labels[i];
      return (int16_t)(120 + i);
    }
  }
  return -1;
}

const char* decodeMenuWifiNumpadCode(int16_t code) {
  switch (code) {
    case 120: return "1";
    case 121: return "2";
    case 122: return "3";
    case 123: return "4";
    case 124: return "5";
    case 125: return "6";
    case 126: return "7";
    case 127: return "8";
    case 128: return "9";
    case 129: return ".";
    case 130: return "0";
    case 131: return "backspace";
    default: return nullptr;
  }
}

void renderMenuWifiOverview(bool redrawStatic) {
  if (redrawStatic) {
    drawMenuHeader("WiFi");
    drawTextWithSmoothFont(FONT_VLW_UI_14, String("Aktywna konfiguracja"), 18, 46, TL_DATUM, TFT_DARKGREY);
    drawTextWithSmoothFont(FONT_VLW_UI_16, String("SSID"), 24, 86, TL_DATUM, TFT_DARKGREY);
    drawTextWithSmoothFont(FONT_VLW_UI_16, String("Hasło"), 24, 112, TL_DATUM, TFT_DARKGREY);
    drawTextWithSmoothFont(FONT_VLW_UI_16, String("Tryb"), 24, 138, TL_DATUM, TFT_DARKGREY);
  }

  char wifiKey[128];
  snprintf(wifiKey, sizeof(wifiKey), "%s|%s|%u|%u.%u.%u.%u|%d",
           g_menuDraft.wifiSsid,
           g_menuWifiStatusText,
           (unsigned)g_menuDraft.wifiUseDhcp,
           g_menuDraft.localIp[0], g_menuDraft.localIp[1], g_menuDraft.localIp[2], g_menuDraft.localIp[3],
           (int)g_menuWifiPressedCode);
  if (!redrawStatic && strcmp(wifiKey, g_lastMenuWifiKey) == 0) {
    return;
  }

  tft.fillRect(16, 64, 292, 18, TFT_BLACK);
  tft.fillRect(16, 274, 288, 36, TFT_BLACK);
  drawMenuButton(MENU_WIFI_HOME_NETWORKS_BUTTON, "Sieci", COLOR_MENU, false, g_menuWifiPressedCode == 1);
  drawMenuButton(MENU_WIFI_HOME_PASSWORD_BUTTON, "Hasło", COLOR_GRAPH, false, g_menuWifiPressedCode == 2);
  drawMenuButton(MENU_WIFI_HOME_IP_BUTTON, "IP", COLOR_DEBUG, false, g_menuWifiPressedCode == 3);
  tft.fillRect(100, 80, 208, 74, TFT_BLACK);
  tft.fillRect(16, 166, 288, 102, TFT_BLACK);
  drawTextWithSmoothFont(FONT_VLW_UI_14,
                         String(g_menuWifiStatusText[0] != '\0' ? g_menuWifiStatusText : ""),
                         18,
                         66,
                         TL_DATUM,
                         g_menuWifiStatusText[0] != '\0' ? TFT_YELLOW : TFT_DARKGREY);
  drawTextWithSmoothFont(FONT_VLW_UI_16, String(g_menuDraft.wifiSsid), 106, 86, TL_DATUM, TFT_WHITE);
  char masked[32];
  const size_t pwdLen = strlen(g_menuDraft.password);
  const size_t maskedLen = min<size_t>(pwdLen, sizeof(masked) - 1);
  memset(masked, '-', maskedLen);
  masked[maskedLen] = '\0';
  drawTextWithSmoothFont(FONT_VLW_UI_16, String(maskedLen > 0 ? masked : "(puste)"), 106, 112, TL_DATUM, TFT_WHITE);
  drawMenuButton(MENU_WIFI_MODE_TOGGLE, g_menuDraft.wifiUseDhcp ? "DHCP" : "Statyczne", COLOR_KIELCE, true, g_menuWifiPressedCode == 4);
  drawMenuIpLine("IP", g_menuDraft.localIp, 172);
  drawMenuIpLine("Maska", g_menuDraft.subnet, 198);
  drawMenuIpLine("Brama", g_menuDraft.gateway, 224);
  drawMenuIpLine("DNS", g_menuDraft.dns, 250);

  strncpy(g_lastMenuWifiKey, wifiKey, sizeof(g_lastMenuWifiKey) - 1);
  g_lastMenuWifiKey[sizeof(g_lastMenuWifiKey) - 1] = '\0';
}

void renderMenuWifiNetworks(bool redrawStatic) {
  if (redrawStatic) {
    drawMenuHeader("Sieci WiFi");
    drawTextWithSmoothFont(FONT_VLW_UI_14, String("Wybierz SSID z listy"), 18, 52, TL_DATUM, TFT_DARKGREY);
  }
  char key[112];
  snprintf(key, sizeof(key), "%u|%s|%d|%d|%s",
           (unsigned)g_menuWifiScanCount,
           g_menuDraft.wifiSsid,
           (int)g_menuWifiPressedCode,
           (int)g_menuWifiScanInProgress,
           g_menuWifiStatusText);
  if (!redrawStatic && strcmp(key, g_lastMenuWifiKey) == 0) {
    return;
  }
  tft.fillRect(226, 8, 84, 30, TFT_BLACK);
  drawMenuButton(MENU_WIFI_NETWORK_RESCAN_BUTTON, "Skanuj", COLOR_MENU, false, g_menuWifiPressedCode == 5);
  tft.fillRect(12, 68, 296, 332, TFT_BLACK);
  drawTextWithSmoothFont(FONT_VLW_UI_14, String(g_menuWifiStatusText[0] != '\0' ? g_menuWifiStatusText : "Gotowe"), 18, 74, TL_DATUM, g_menuWifiScanInProgress ? TFT_YELLOW : TFT_DARKGREY);
  for (uint8_t i = 0; i < g_menuWifiScanCount; i++) {
    RectangleDef row = {16, (uint16_t)(98 + i * 38), 288, 32};
    const bool active = strcmp(g_menuDraft.wifiSsid, g_menuWifiScan[i].ssid) == 0;
    const bool pressed = g_menuWifiPressedCode == (10 + i);
    const uint16_t fill = pressed ? 0x4228 : TFT_BLACK;
    const uint16_t border = pressed ? TFT_WHITE : (active ? TFT_WHITE : 0x39C7);
    const uint16_t text = pressed ? TFT_WHITE : (active ? TFT_WHITE : TFT_DARKGREY);
    tft.fillRoundRect(row.x, row.y, row.width, row.height, 5, fill);
    tft.drawRoundRect(row.x, row.y, row.width, row.height, 5, border);
    drawTextWithSmoothFont(FONT_VLW_UI_16, String(g_menuWifiScan[i].ssid), row.x + 10, row.y + 6, TL_DATUM, text);
    if (g_menuWifiScan[i].secure) {
      drawMenuLockIcon(row.x + row.width - 60, row.y + 8, pressed ? TFT_WHITE : TFT_ORANGE);
    }
    drawMenuRssiBars(row.x + row.width - 34, row.y + 23, g_menuWifiScan[i].rssi, text);
  }
  if (g_menuWifiScanCount == 0) {
    drawTextWithSmoothFont(FONT_VLW_UI_18,
                           String(g_menuWifiScanInProgress ? "Skanowanie..." : "Brak wyników. Kliknij Skanuj."),
                           centerWidth, 146, TC_DATUM, TFT_DARKGREY);
  }
  strncpy(g_lastMenuWifiKey, key, sizeof(g_lastMenuWifiKey) - 1);
  g_lastMenuWifiKey[sizeof(g_lastMenuWifiKey) - 1] = '\0';
}

void renderMenuWifiPassword(bool redrawStatic) {
  if (redrawStatic) {
    drawMenuHeader("Hasło WiFi");
    drawTextWithSmoothFont(FONT_VLW_UI_16, String("SSID"), 20, 52, TL_DATUM, TFT_DARKGREY);
    drawTextWithSmoothFont(FONT_VLW_UI_16, String("Hasło"), 20, 86, TL_DATUM, TFT_DARKGREY);
    g_menuWifiDirtyFlags |= MENU_WIFI_DIRTY_PASSWORD_TEXT | MENU_WIFI_DIRTY_PASSWORD_BTNS | MENU_WIFI_DIRTY_PASSWORD_KEYS;
  }
  char key[128];
  snprintf(key, sizeof(key), "%s|%s|%u|%u|%u|%d",
           g_menuDraft.wifiSsid,
           g_menuWifiPasswordInput,
           (unsigned)g_menuWifiPasswordEdited,
           (unsigned)g_menuWifiShowPassword,
           (unsigned)g_menuWifiKeyboardUpper,
           (int)g_menuWifiPressedCode);
  if (!redrawStatic && strcmp(key, g_lastMenuWifiKey) == 0) {
    return;
  }
  if (redrawStatic || (g_menuWifiDirtyFlags & MENU_WIFI_DIRTY_PASSWORD_TEXT)) {
    redrawMenuWifiPasswordTextRegion();
  }
  if (redrawStatic || (g_menuWifiDirtyFlags & MENU_WIFI_DIRTY_PASSWORD_BTNS)) {
    tft.fillRect(12, 112, 296, 36, TFT_BLACK);
    drawMenuWifiPasswordTopButtons();
  }
  if (redrawStatic || (g_menuWifiDirtyFlags & MENU_WIFI_DIRTY_PASSWORD_KEYS)) {
    drawMenuWifiKeyboard();
  }
  strncpy(g_lastMenuWifiKey, key, sizeof(g_lastMenuWifiKey) - 1);
  g_lastMenuWifiKey[sizeof(g_lastMenuWifiKey) - 1] = '\0';
  g_menuWifiDirtyFlags &= ~(MENU_WIFI_DIRTY_PASSWORD_TEXT | MENU_WIFI_DIRTY_PASSWORD_BTNS | MENU_WIFI_DIRTY_PASSWORD_KEYS);
}

void renderMenuWifiIp(bool redrawStatic) {
  if (redrawStatic) {
    drawMenuHeader("IP / DNS");
    drawTextWithSmoothFont(FONT_VLW_UI_14, String("Adresy sieci"), 18, 42, TL_DATUM, TFT_DARKGREY);
    g_menuWifiDirtyFlags |= MENU_WIFI_DIRTY_IP_TOP | MENU_WIFI_DIRTY_IP_FIELDS | MENU_WIFI_DIRTY_IP_NUMPAD;
  }
  char key[144];
  snprintf(key, sizeof(key), "%u|%u|%s|%s|%s|%s|%d|%s",
           (unsigned)g_menuDraft.wifiUseDhcp,
           (unsigned)g_menuWifiIpField,
           g_menuDraft.localIpText,
           g_menuDraft.subnetText,
           g_menuDraft.gatewayText,
           g_menuDraft.dnsText,
           (int)g_menuWifiPressedCode,
           g_menuWifiStatusText);
  if (!redrawStatic && strcmp(key, g_lastMenuWifiKey) == 0) {
    return;
  }
  const bool enabled = !g_menuDraft.wifiUseDhcp;
  tft.fillRect(16, 218, 292, 18, TFT_BLACK);
  drawTextWithSmoothFont(FONT_VLW_UI_14,
                         String(g_menuWifiStatusText[0] != '\0' ? g_menuWifiStatusText : ""),
                         18,
                         220,
                         TL_DATUM,
                         g_menuWifiStatusText[0] != '\0' ? TFT_YELLOW : TFT_DARKGREY);
  if (redrawStatic || (g_menuWifiDirtyFlags & MENU_WIFI_DIRTY_IP_TOP)) {
    drawMenuWifiIpTopBar();
  }
  if (redrawStatic || (g_menuWifiDirtyFlags & MENU_WIFI_DIRTY_IP_FIELDS)) {
    redrawAllMenuWifiIpFields();
    if (g_menuWifiPressedCode >= 110 && g_menuWifiPressedCode <= 113) {
      const RectangleDef *pressedRect = nullptr;
      if (g_menuWifiPressedCode == 110) pressedRect = &MENU_WIFI_IP_LOCAL_FIELD;
      else if (g_menuWifiPressedCode == 111) pressedRect = &MENU_WIFI_IP_SUBNET_FIELD;
      else if (g_menuWifiPressedCode == 112) pressedRect = &MENU_WIFI_IP_GATEWAY_FIELD;
      else if (g_menuWifiPressedCode == 113) pressedRect = &MENU_WIFI_IP_DNS_FIELD;
      if (pressedRect != nullptr) {
        tft.drawRoundRect(pressedRect->x, pressedRect->y, pressedRect->width, pressedRect->height, 4, TFT_WHITE);
      }
    }
  }
  if (redrawStatic || (g_menuWifiDirtyFlags & MENU_WIFI_DIRTY_IP_NUMPAD)) {
    drawMenuWifiNumpad();
  }
  strncpy(g_lastMenuWifiKey, key, sizeof(g_lastMenuWifiKey) - 1);
  g_lastMenuWifiKey[sizeof(g_lastMenuWifiKey) - 1] = '\0';
  g_menuWifiDirtyFlags &= ~(MENU_WIFI_DIRTY_IP_TOP | MENU_WIFI_DIRTY_IP_FIELDS | MENU_WIFI_DIRTY_IP_NUMPAD);
}

int16_t getMenuWifiPressedCodeAt(uint16_t x, uint16_t y) {
  if (g_menuWifiScreen == MENU_WIFI_HOME) {
    if (pointInRect(x, y, MENU_WIFI_HOME_NETWORKS_BUTTON)) return 1;
    if (pointInRect(x, y, MENU_WIFI_HOME_PASSWORD_BUTTON)) return 2;
    if (pointInRect(x, y, MENU_WIFI_HOME_IP_BUTTON)) return 3;
    if (pointInRect(x, y, MENU_WIFI_MODE_TOGGLE)) return 4;
    return 0;
  }
  if (g_menuWifiScreen == MENU_WIFI_NETWORKS) {
    if (pointInRect(x, y, MENU_WIFI_NETWORK_RESCAN_BUTTON)) return 5;
    for (uint8_t i = 0; i < g_menuWifiScanCount; i++) {
      RectangleDef row = {16, (uint16_t)(98 + i * 38), 288, 32};
      if (pointInRect(x, y, row)) return (int16_t)(10 + i);
    }
    return 0;
  }
  if (g_menuWifiScreen == MENU_WIFI_PASSWORD) {
    if (pointInRect(x, y, MENU_WIFI_PASSWORD_SHOW_BUTTON)) return 30;
    if (pointInRect(x, y, MENU_WIFI_PASSWORD_CLEAR_BUTTON)) return 31;
    if (pointInRect(x, y, MENU_WIFI_PASSWORD_DONE_BUTTON)) return 32;
    char keyChar = '\0';
    bool isAction = false;
    const char *action = nullptr;
    return getMenuWifiKeyboardKeyAt(x, y, &keyChar, &isAction, &action);
  }
  if (g_menuWifiScreen == MENU_WIFI_IP) {
    if (pointInRect(x, y, MENU_WIFI_IP_DHCP_TOGGLE)) return 100;
    if (pointInRect(x, y, MENU_WIFI_IP_DONE_BUTTON)) return 101;
    if (pointInRect(x, y, MENU_WIFI_IP_LOCAL_FIELD)) return 110;
    if (pointInRect(x, y, MENU_WIFI_IP_SUBNET_FIELD)) return 111;
    if (pointInRect(x, y, MENU_WIFI_IP_GATEWAY_FIELD)) return 112;
    if (pointInRect(x, y, MENU_WIFI_IP_DNS_FIELD)) return 113;
    const char *action = nullptr;
    return getMenuWifiNumpadKeyAt(x, y, &action);
  }
  return 0;
}

int16_t getMenuPressedTargetAt(uint16_t x, uint16_t y) {
  if (pointInRect(x, y, MENU_BACK_BUTTON)) {
    return 1;
  }

  if (g_menuScreen == MENU_WIFI) {
    const int16_t wifiCode = getMenuWifiPressedCodeAt(x, y);
    return (wifiCode > 0) ? (1000 + wifiCode) : 0;
  }

  if (g_menuScreen == MENU_HOME) {
    if (pointInRect(x, y, MENU_HOME_WIFI_BUTTON)) return 10;
    if (pointInRect(x, y, MENU_HOME_VIEW_BUTTON)) return 11;
    if (pointInRect(x, y, MENU_HOME_BRIGHTNESS_BUTTON)) return 12;
    if (pointInRect(x, y, MENU_HOME_COLORS_BUTTON)) return 13;
    if (pointInRect(x, y, MENU_HOME_RGB_BUTTON)) return 14;
    if (pointInRect(x, y, MENU_HOME_TOUCH_BUTTON)) return 15;
    return 0;
  }

  if (g_menuScreen == MENU_VIEW) {
    if (pointInRect(x, y, MENU_VIEW_SECONDS_BOX)) return 20;
    if (pointInRect(x, y, MENU_VIEW_FORECAST_BOX)) return 21;
    if (pointInRect(x, y, MENU_VIEW_SUN_BOX)) return 22;
    if (pointInRect(x, y, MENU_VIEW_AQI_BOX)) return 23;
    if (pointInRect(x, y, MENU_VIEW_RAD_BOX)) return 24;
    return 0;
  }

  if (g_menuScreen == MENU_BRIGHTNESS) {
    if (pointInRect(x, y, MENU_BRIGHT_AUTO_BOX)) return 30;
    if (pointInRect(x, y, MENU_BRIGHT_DAY_BOX)) return 31;
    if (pointInRect(x, y, MENU_BRIGHT_NIGHT_BOX)) return 32;
    return 0;
  }

  if (g_menuScreen == MENU_COLORS) {
    if (pointInRect(x, y, MENU_COLORS_CITY0_BOX)) return 40;
    if (pointInRect(x, y, MENU_COLORS_CITY1_BOX)) return 41;
    if (pointInRect(x, y, MENU_COLORS_CITY2_BOX)) return 42;
    if (pointInRect(x, y, MENU_COLORS_DEFAULTS_BUTTON)) return 43;
    for (uint8_t i = 0; i < getMenuPaletteColorCount(); i++) {
      if (pointInRect(x, y, getMenuPaletteRect(i))) return (int16_t)(50 + i);
    }
    return 0;
  }

  return 0;
}

void handleMenuPressedTargetRelease(int16_t target) {
  if (target == 0) {
    return;
  }

  if (target == 1) {
    if (g_menuScreen == MENU_HOME) {
      if (doesMenuDraftDifferFromRuntimeConfig()) {
        const MenuWifiApplyResult applyResult = applyMenuWifiDraftToRuntime();
        if (applyResult != MENU_WIFI_APPLY_OK) {
          setMenuWifiStatus(getMenuWifiApplyErrorText(applyResult));
          g_menuWifiApplyFailed = true;
          g_menuScreen = MENU_WIFI;
          g_menuWifiScreen = MENU_WIFI_HOME;
          resetTouchInteractionState();
          invalidateMenuFull();
          updateRescueMenu(false);
          return;
        }
      }
      g_menuWifiApplyFailed = false;
      navigateToPage(PAGE_MAIN, -1);
    } else if (g_menuScreen == MENU_WIFI && g_menuWifiScreen != MENU_WIFI_HOME) {
      g_menuWifiScreen = MENU_WIFI_HOME;
      resetTouchInteractionState();
      invalidateMenuFull();
      updateRescueMenu(false);
    } else {
      if (g_menuScreen == MENU_WIFI && g_menuWifiApplyFailed) {
        initMenuDraftState();
        g_menuWifiApplyFailed = false;
      }
      g_menuScreen = MENU_HOME;
      resetTouchInteractionState();
      invalidateMenuFull();
      updateRescueMenu(false);
    }
    return;
  }

  if (target >= 1000) {
    g_menuWifiPressedCode = target - 1000;
    handleMenuWifiPressedRelease();
    return;
  }

  if (g_menuScreen == MENU_HOME) {
    if (target == 10) {
      g_menuScreen = MENU_WIFI;
      g_menuWifiScreen = MENU_WIFI_HOME;
    } else if (target == 11) {
      g_menuScreen = MENU_VIEW;
    } else if (target == 12) {
      g_menuScreen = MENU_BRIGHTNESS;
    } else if (target == 13) {
      g_menuScreen = MENU_COLORS;
    } else if (target == 14) {
      g_menuScreen = MENU_RGB;
    } else if (target == 15) {
      g_menuScreen = MENU_TOUCH;
    } else {
      return;
    }
    resetTouchInteractionState();
    invalidateMenuFull();
    updateRescueMenu(false);
    return;
  }

  if (g_menuScreen == MENU_VIEW) {
    if (target == 20) g_menuDraft.showSeconds = !g_menuDraft.showSeconds;
    else if (target == 21) g_menuDraft.showForecast = !g_menuDraft.showForecast;
    else if (target == 22) g_menuDraft.showSun = !g_menuDraft.showSun;
    else if (target == 23) g_menuDraft.showAqi = !g_menuDraft.showAqi;
    else if (target == 24) g_menuDraft.showRad = !g_menuDraft.showRad;
    else return;
    invalidateMenuScreen(MENU_VIEW, false, true);
    updateRescueMenu(false);
    return;
  }

  if (g_menuScreen == MENU_BRIGHTNESS) {
    if (target == 30) applyBrightnessMode(LCD_MODE_AUTO);
    else if (target == 31) applyBrightnessMode(LCD_MODE_DAY);
    else if (target == 32) applyBrightnessMode(LCD_MODE_NIGHT);
    else return;
    invalidateMenuScreen(MENU_BRIGHTNESS, false, true);
    updateRescueMenu(false);
    return;
  }

  if (g_menuScreen == MENU_COLORS) {
    if (target >= 40 && target <= 42) {
      g_menuColorsSelectedCity = (uint8_t)(target - 40);
      g_menuColorsStatusText[0] = '\0';
      invalidateMenuScreen(MENU_COLORS, false, true);
      updateRescueMenu(false);
      return;
    }

    if (target == 43) {
      g_cityUiColors[0] = COLOR_KIELCE;
      g_cityUiColors[1] = COLOR_DAUCHINGEN;
      g_cityUiColors[2] = COLOR_TARNOWITZ;
      for (uint8_t city = 0; city < CITY_COUNT; city++) {
        g_menuDraft.cityColors[city] = g_cityUiColors[city];
      }
      g_menuColorsStatusText[0] = '\0';
      g_lastWeatherRenderKey[0] = '\0';
      g_lastForecastRenderKey[0] = '\0';
      g_lastStatsRenderKey[0] = '\0';
      for (uint8_t i = 0; i < DEBUG_LINE_COUNT; i++) {
        g_lastDebugLines[i][0] = '\0';
      }
      invalidateMenuScreen(MENU_COLORS, false, true);
      updateRescueMenu(false);
      return;
    }

    if (target >= 50 && target < (50 + getMenuPaletteColorCount())) {
      const uint16_t chosenColor = getMenuPaletteColor((uint8_t)(target - 50));
      bool colorBusy = false;
      for (uint8_t city = 0; city < CITY_COUNT; city++) {
        if (city != g_menuColorsSelectedCity && g_cityUiColors[city] == chosenColor) {
          colorBusy = true;
          break;
        }
      }

      if (colorBusy) {
        strncpy(g_menuColorsStatusText, "Kolor zajęty", sizeof(g_menuColorsStatusText) - 1);
        g_menuColorsStatusText[sizeof(g_menuColorsStatusText) - 1] = '\0';
      } else {
        g_cityUiColors[g_menuColorsSelectedCity] = chosenColor;
        g_menuDraft.cityColors[g_menuColorsSelectedCity] = chosenColor;
        g_menuColorsStatusText[0] = '\0';
        g_lastWeatherRenderKey[0] = '\0';
        g_lastForecastRenderKey[0] = '\0';
        g_lastStatsRenderKey[0] = '\0';
        for (uint8_t i = 0; i < DEBUG_LINE_COUNT; i++) {
          g_lastDebugLines[i][0] = '\0';
        }
      }

      invalidateMenuScreen(MENU_COLORS, false, true);
      updateRescueMenu(false);
      return;
    }
  }
}

void handleMenuWifiPressedRelease() {
  if (g_menuWifiPressedCode == 0) {
    return;
  }

  const int16_t code = g_menuWifiPressedCode;
  g_menuWifiPressedCode = 0;

  if (g_menuWifiScreen == MENU_WIFI_PASSWORD) {
    redrawMenuWifiPasswordPressedControl(code);
  } else if (g_menuWifiScreen == MENU_WIFI_IP) {
    redrawMenuWifiIpPressedControl(code);
  }

  if (g_menuWifiScreen == MENU_WIFI_HOME) {
    if (code == 1) {
      g_menuWifiScreen = MENU_WIFI_NETWORKS;
      populateMenuWifiSavedList();
      invalidateMenuFull();
      updateRescueMenu(false);
    } else if (code == 2) {
      g_menuWifiScreen = MENU_WIFI_PASSWORD;
      g_menuWifiShowPassword = false;
      g_menuWifiKeyboardUpper = false;
      g_menuWifiPasswordInput[0] = '\0';
      g_menuWifiPasswordEdited = false;
      g_menuWifiDirtyFlags |= MENU_WIFI_DIRTY_PASSWORD_TEXT | MENU_WIFI_DIRTY_PASSWORD_BTNS | MENU_WIFI_DIRTY_PASSWORD_KEYS;
      invalidateMenuFull();
      updateRescueMenu(false);
    } else if (code == 3) {
      g_menuWifiScreen = MENU_WIFI_IP;
      g_menuWifiDirtyFlags |= MENU_WIFI_DIRTY_IP_TOP | MENU_WIFI_DIRTY_IP_FIELDS | MENU_WIFI_DIRTY_IP_NUMPAD;
      invalidateMenuFull();
      updateRescueMenu(false);
    } else if (code == 4) {
      g_menuDraft.wifiUseDhcp = !g_menuDraft.wifiUseDhcp;
      invalidateMenuScreen(MENU_WIFI, false, true);
      updateRescueMenu(false);
    }
    return;
  }

  if (g_menuWifiScreen == MENU_WIFI_NETWORKS) {
    if (code == 5) {
      requestMenuWifiScan(true);
      invalidateMenuScreen(MENU_WIFI, false, true);
      updateRescueMenu(false);
      return;
    }
    if (code >= 10 && code < (10 + g_menuWifiScanCount)) {
      const uint8_t index = (uint8_t)(code - 10);
      strncpy(g_menuDraft.wifiSsid, g_menuWifiScan[index].ssid, sizeof(g_menuDraft.wifiSsid) - 1);
      g_menuDraft.wifiSsid[sizeof(g_menuDraft.wifiSsid) - 1] = '\0';
      g_menuDraft.wifiProfileIndex = 0xFF;
      bool matchedProfile = false;
      for (uint8_t profileIndex = 0; profileIndex < WIFI_PROFILE_COUNT; profileIndex++) {
        if (strcmp(WIFI_PROFILES[profileIndex].ssid, g_menuWifiScan[index].ssid) == 0) {
          loadMenuDraftFromProfile(profileIndex);
          matchedProfile = true;
          break;
        }
      }
      if (!matchedProfile) {
        g_menuDraft.password[0] = '\0';
        g_menuDraft.wifiUseDhcp = true;
      }
      g_menuWifiShowPassword = false;
      g_menuWifiKeyboardUpper = false;
      g_menuWifiPasswordInput[0] = '\0';
      g_menuWifiPasswordEdited = false;
      g_menuWifiScreen = MENU_WIFI_HOME;
      setMenuWifiStatus("");
      invalidateMenuFull();
      updateRescueMenu(false);
    }
    return;
  }

  if (g_menuWifiScreen == MENU_WIFI_PASSWORD) {
    if (code == 30) {
      g_menuWifiShowPassword = !g_menuWifiShowPassword;
      invalidateMenuWifiRegions(MENU_WIFI_DIRTY_PASSWORD_TEXT | MENU_WIFI_DIRTY_PASSWORD_BTNS);
      updateRescueMenu(false);
      return;
    }
    if (code == 31) {
      g_menuWifiPasswordInput[0] = '\0';
      g_menuWifiPasswordEdited = true;
      invalidateMenuWifiRegions(MENU_WIFI_DIRTY_PASSWORD_TEXT);
      updateRescueMenu(false);
      return;
    }
    if (code == 32) {
      if (g_menuWifiPasswordEdited) {
        strncpy(g_menuDraft.password, g_menuWifiPasswordInput, sizeof(g_menuDraft.password) - 1);
        g_menuDraft.password[sizeof(g_menuDraft.password) - 1] = '\0';
      }
      g_menuWifiScreen = MENU_WIFI_HOME;
      invalidateMenuFull();
      updateRescueMenu(false);
      return;
    }
    if (code == 90) {
      g_menuWifiKeyboardUpper = !g_menuWifiKeyboardUpper;
      invalidateMenuWifiRegions(MENU_WIFI_DIRTY_PASSWORD_KEYS);
      updateRescueMenu(false);
      return;
    }
    if (code == 91) {
      size_t len = strlen(g_menuWifiPasswordInput);
      if (len + 1 < sizeof(g_menuWifiPasswordInput)) {
        g_menuWifiPasswordInput[len] = ' ';
        g_menuWifiPasswordInput[len + 1] = '\0';
      }
      g_menuWifiPasswordEdited = true;
      invalidateMenuWifiRegions(MENU_WIFI_DIRTY_PASSWORD_TEXT);
      updateRescueMenu(false);
      return;
    }
    if (code == 93) {
      size_t len = strlen(g_menuWifiPasswordInput);
      if (len > 0) {
        g_menuWifiPasswordInput[len - 1] = '\0';
      }
      g_menuWifiPasswordEdited = true;
      invalidateMenuWifiRegions(MENU_WIFI_DIRTY_PASSWORD_TEXT);
      updateRescueMenu(false);
      return;
    }
    if (code >= 40 && code <= 93) {
      char keyChar = '\0';
      bool isAction = false;
      const char *action = nullptr;
      decodeMenuWifiKeyboardCode(code, &keyChar, &isAction, &action);
      if (!isAction && keyChar != '\0') {
        size_t len = strlen(g_menuWifiPasswordInput);
        if (len + 1 < sizeof(g_menuWifiPasswordInput)) {
          g_menuWifiPasswordInput[len] = keyChar;
          g_menuWifiPasswordInput[len + 1] = '\0';
        }
        g_menuWifiPasswordEdited = true;
        invalidateMenuWifiRegions(MENU_WIFI_DIRTY_PASSWORD_TEXT);
        updateRescueMenu(false);
      }
    }
    return;
  }

  if (g_menuWifiScreen == MENU_WIFI_IP) {
    if (code == 100) {
      g_menuDraft.wifiUseDhcp = !g_menuDraft.wifiUseDhcp;
      setMenuWifiStatus("");
      invalidateMenuWifiRegions(MENU_WIFI_DIRTY_IP_TOP | MENU_WIFI_DIRTY_IP_FIELDS);
      updateRescueMenu(false);
      return;
    }
    if (code == 101) {
      if (!g_menuDraft.wifiUseDhcp) {
        if (!parseIpAddressFromText(g_menuDraft.localIpText, g_menuDraft.localIp)) {
          setMenuWifiStatus(u8"Błędny adres IP.");
          invalidateMenuScreen(MENU_WIFI, false, true);
          updateRescueMenu(false);
          return;
        }
        if (!parseIpAddressFromText(g_menuDraft.subnetText, g_menuDraft.subnet)) {
          setMenuWifiStatus(u8"Błędna maska.");
          invalidateMenuScreen(MENU_WIFI, false, true);
          updateRescueMenu(false);
          return;
        }
        if (!parseIpAddressFromText(g_menuDraft.gatewayText, g_menuDraft.gateway)) {
          setMenuWifiStatus(u8"Błędna brama.");
          invalidateMenuScreen(MENU_WIFI, false, true);
          updateRescueMenu(false);
          return;
        }
        if (!parseIpAddressFromText(g_menuDraft.dnsText, g_menuDraft.dns)) {
          setMenuWifiStatus(u8"Błędny DNS.");
          invalidateMenuScreen(MENU_WIFI, false, true);
          updateRescueMenu(false);
          return;
        }
      }
      setMenuWifiStatus("");
      g_menuWifiScreen = MENU_WIFI_HOME;
      invalidateMenuFull();
      updateRescueMenu(false);
      return;
    }
    if (code >= 110 && code <= 113) {
      const MenuWifiIpField prevField = g_menuWifiIpField;
      g_menuWifiIpField = (MenuWifiIpField)(code - 110);
      drawMenuWifiIpFieldByCode((int16_t)(110 + prevField));
      drawMenuWifiIpFieldByCode(code);
      updateRescueMenu(false);
      return;
    }
    if (!g_menuDraft.wifiUseDhcp && code >= 120 && code <= 131) {
      const char *action = decodeMenuWifiNumpadCode(code);
      if (action != nullptr) {
        char *target = getActiveMenuWifiIpText();
        size_t len = strlen(target);
        if (strcmp(action, "backspace") == 0) {
          if (len > 0) {
            target[len - 1] = '\0';
          }
        } else if (canAppendIpChar(target, action[0])) {
          target[len] = action[0];
          target[len + 1] = '\0';
        }
        drawMenuWifiIpFieldByCode((int16_t)(110 + g_menuWifiIpField));
        updateRescueMenu(false);
      }
    }
  }
}

void drawOverlayButton(const RectangleDef &rect, const char *label, uint16_t baseColor, bool active, bool pressed) {
  const uint16_t borderColor = pressed ? TFT_WHITE : (active ? 0x7BEF : baseColor);
  const uint16_t textColor = pressed ? TFT_WHITE : (active ? 0x7BEF : baseColor);
  const uint16_t fillColor = pressed ? 0x4228 : (active ? 0x18E3 : TFT_BLACK);
  tft.fillRoundRect(rect.x, rect.y, rect.width, rect.height, 6, fillColor);
  tft.drawRoundRect(rect.x, rect.y, rect.width, rect.height, 6, borderColor);
  unloadSmoothFontCached();
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(textColor, fillColor);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(label, rect.x + (rect.width / 2), rect.y + 12);
  tft.setFreeFont(nullptr);
}

int8_t getOverlayButtonAt(uint16_t x, uint16_t y) {
  if (pointInRect(x, y, NAV_KIELCE_BUTTON)) return 0;
  if (pointInRect(x, y, NAV_DAU_BUTTON)) return 1;
  if (pointInRect(x, y, NAV_TARNOWITZ_BUTTON)) return 2;
  if (pointInRect(x, y, NAV_GRAPH_BUTTON)) return 3;
  if (pointInRect(x, y, NAV_DEBUG_BUTTON)) return 4;
  if (pointInRect(x, y, NAV_MENU_BUTTON)) return 5;
  return -1;
}

void renderNavOverlay() {
  const uint16_t overlayFill = 0x0861;
  for (int16_t y = 132; y < 300; y += 4) {
    for (int16_t x = 12 + ((y / 4) % 2) * 2; x < 308; x += 4) {
      tft.fillRect(x, y, 2, 2, overlayFill);
    }
  }

  tft.drawRoundRect(12, 132, 296, 164, 10, 0x4228);
  const bool activeKielce = (g_currentPage == PAGE_MAIN && g_activeCityIndex == 0);
  const bool activeDau = (g_currentPage == PAGE_MAIN && g_activeCityIndex == 1);
  const bool activeTarnowitz = (g_currentPage == PAGE_MAIN && g_activeCityIndex == 2);
  const bool activeGraph = (g_currentPage == PAGE_STATS);
  const bool activeDebug = (g_currentPage == PAGE_DEBUG);
  const bool activeMenu = (g_currentPage == PAGE_MENU);
  drawOverlayButton(NAV_KIELCE_BUTTON, "Kielce", getCityUiColor(0), activeKielce, g_overlayPressedButton == 0);
  drawOverlayButton(NAV_DAU_BUTTON, "Dauchingen", getCityUiColor(1), activeDau, g_overlayPressedButton == 1);
  drawOverlayButton(NAV_TARNOWITZ_BUTTON, "Tarnowitz", getCityUiColor(2), activeTarnowitz, g_overlayPressedButton == 2);
  drawOverlayButton(NAV_GRAPH_BUTTON, "Wykres", COLOR_GRAPH, activeGraph, g_overlayPressedButton == 3);
  drawOverlayButton(NAV_DEBUG_BUTTON, "Debug", COLOR_DEBUG, activeDebug, g_overlayPressedButton == 4);
  drawOverlayButton(NAV_MENU_BUTTON, "Menu", COLOR_MENU, activeMenu, g_overlayPressedButton == 5);
}

void hideNavOverlay() {
  if (!g_navOverlayVisible) {
    return;
  }
  g_navOverlayVisible = false;
  resetTouchInteractionState();
  g_touchConsumeUntilRelease = true;
  renderCurrentRescuePage();
}

void showNavOverlay() {
  if (g_navOverlayVisible) {
    return;
  }
  g_navOverlayVisible = true;
  resetTouchInteractionState();
  g_touchConsumeUntilRelease = true;
  renderNavOverlay();
}

bool handleOverlayTap(uint16_t x, uint16_t y) {
  if (!g_navOverlayVisible) {
    return false;
  }

  const int8_t selection = getOverlayButtonAt(x, y);
  if (selection == 0) {
    navigateToPage(PAGE_MAIN, 0);
    return true;
  }
  if (selection == 1) {
    navigateToPage(PAGE_MAIN, 1);
    return true;
  }
  if (selection == 2) {
    navigateToPage(PAGE_MAIN, 2);
    return true;
  }
  if (selection == 3) {
    navigateToPage(PAGE_STATS, -1);
    return true;
  }
  if (selection == 4) {
    navigateToPage(PAGE_DEBUG, -1);
    return true;
  }
  if (selection == 5) {
    navigateToPage(PAGE_MENU, -1);
    return true;
  }

  hideNavOverlay();
  return true;
}

const char* getMainWeatherIconName(uint16_t weatherId) {
  if (weatherId >= 200 && weatherId < 300) return "thunderstorm";
  if (weatherId >= 300 && weatherId < 400) return "drizzle";
  if (weatherId == 500) return "light-rain";
  if (weatherId >= 501 && weatherId <= 504) return "rain";
  if (weatherId >= 511 && weatherId < 600) return "sleet";
  if (weatherId >= 600 && weatherId < 700) return "snow";
  if (weatherId >= 700 && weatherId < 800) return "fog";
  if (weatherId == 800) return "clear-day";
  if (weatherId == 801) return "partly-cloudy-day";
  if (weatherId >= 802 && weatherId <= 804) return "cloudy";
  return "unknown";
}

const char* getMainWeatherIconNameByOwmIcon(const char *owmIcon, uint16_t weatherId) {
  if (owmIcon != nullptr && strlen(owmIcon) >= 3) {
    if (strcmp(owmIcon, "01n") == 0) return "clear-night";
    if (strcmp(owmIcon, "01d") == 0) return "clear-day";
    if (strcmp(owmIcon, "02n") == 0) return "partly-cloudy-night";
    if (strcmp(owmIcon, "02d") == 0) return "partly-cloudy-day";
    if (strcmp(owmIcon, "03n") == 0 || strcmp(owmIcon, "03d") == 0) return "cloudy";
    if (strcmp(owmIcon, "04n") == 0 || strcmp(owmIcon, "04d") == 0) return "cloudy";
    if (strcmp(owmIcon, "09n") == 0 || strcmp(owmIcon, "09d") == 0) return "drizzle";
    if (strcmp(owmIcon, "10n") == 0 || strcmp(owmIcon, "10d") == 0) return "rain";
    if (strcmp(owmIcon, "11n") == 0 || strcmp(owmIcon, "11d") == 0) return "thunderstorm";
    if (strcmp(owmIcon, "13n") == 0 || strcmp(owmIcon, "13d") == 0) return "snow";
    if (strcmp(owmIcon, "50n") == 0 || strcmp(owmIcon, "50d") == 0) return "fog";
  }

  return getMainWeatherIconName(weatherId);
}

void updateRescueDebug(bool force) {
  if (g_currentPage != PAGE_DEBUG) {
    return;
  }

  const uint32_t nowMs = millis();
  if (!force && (nowMs - g_lastDebugRenderMs) < 1000U) {
    return;
  }

  const uint32_t uptimeSec = millis() / 1000U;
  const uint32_t uptimeMin = uptimeSec / 60U;
  const uint32_t uptimeRemSec = uptimeSec % 60U;
  char lineBuf[160];

  auto fetchStateShort = [](DataFetchState state) -> const char* {
    switch (state) {
      case FETCH_RUNNING: return "pobieram";
      case FETCH_OK: return "ok";
      case FETCH_STALE: return "stare";
      case FETCH_ERROR: return "błąd";
      case FETCH_BACKUP: return "backup";
      default: return "brak";
    }
  };

  auto bootReasonText = []() -> const char* {
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON:   return "power on";
      case ESP_RST_EXT:       return "pin reset";
      case ESP_RST_SW:        return "software";
      case ESP_RST_PANIC:     return "panic";
      case ESP_RST_INT_WDT:   return "int wdt";
      case ESP_RST_TASK_WDT:  return "task wdt";
      case ESP_RST_WDT:       return "wdt";
      case ESP_RST_DEEPSLEEP: return "deep sleep";
      case ESP_RST_BROWNOUT:  return "brownout";
      case ESP_RST_SDIO:      return "sdio";
      default:                return "nieznany";
    }
  };

  auto shortenRadTimestamp = [](const char *dateEnd, char *buf, size_t bufSize) {
    if (bufSize == 0) {
      return;
    }
    buf[0] = '\0';
    if (dateEnd == nullptr || dateEnd[0] == '\0') {
      snprintf(buf, bufSize, "brak ts");
      return;
    }

    int Y, M, D, h, m, s;
    if (sscanf(dateEnd, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) == 6) {
      snprintf(buf, bufSize, "%02d-%02d %02d:%02d", M, D, h, m);
      return;
    }
    if (sscanf(dateEnd, "%d-%d-%d %d:%d", &Y, &M, &D, &h, &m) == 5) {
      snprintf(buf, bufSize, "%02d-%02d %02d:%02d", M, D, h, m);
      return;
    }

    strncpy(buf, dateEnd, bufSize - 1);
    buf[bufSize - 1] = '\0';
  };

  snprintf(lineBuf, sizeof(lineBuf), "Czas pracy: %lum %lus",
           (unsigned long)uptimeMin,
           (unsigned long)uptimeRemSec);
  drawDebugLineIfChanged(0, 76, 16, lineBuf, force);

  String timestampLine = getCurrentTimestamp(SYSTEM_TIMESTAMP_FORMAT);
  if (timestampLine.length() == 0) {
    timestampLine = "Czas: niedostępny";
  }
  drawDebugLineIfChanged(1, 100, 16, timestampLine.c_str(), force);

  snprintf(lineBuf, sizeof(lineBuf), "Powód resetu: %s", bootReasonText());
  drawDebugLineIfChanged(2, 124, 16, lineBuf, force);
  char cityValue[3][96] = {};
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    const CurrentWeatherViewState &weather = g_weatherCache[city];
    if (weather.hasData) {
      snprintf(cityValue[city], sizeof(cityValue[city]), " %.1fC (%lus)",
               weather.temperature, (unsigned long)weather.ageSeconds);
    } else {
      snprintf(cityValue[city], sizeof(cityValue[city]), " %s", fetchStateShort(weather.fetchState));
    }

    String seg[] = {
      String(city == 0 ? "Aktualna: " : "            "),
      String(getCityUiName(city)),
      String(cityValue[city])
    };
    uint16_t colors[] = {TFT_WHITE, getCityUiColor(city), TFT_WHITE};
    String key = seg[0] + seg[1] + seg[2];
    drawDebugSegmentLineIfChanged(3 + city, 148 + (city * 24), 14, seg, colors, 3, key.c_str(), force);
  }

  String forecastSeg[1 + CITY_COUNT * 3];
  uint16_t forecastCol[1 + CITY_COUNT * 3];
  forecastSeg[0] = "Prognoza: ";
  forecastCol[0] = TFT_WHITE;
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    const ForecastViewState &forecast = g_forecastCache[city];
    if (forecast.hasData) {
      snprintf(cityValue[city], sizeof(cityValue[city]), " %u/%lus",
               (unsigned)forecast.itemCount, (unsigned long)forecast.ageSeconds);
    } else {
      snprintf(cityValue[city], sizeof(cityValue[city]), " %s", fetchStateShort(forecast.fetchState));
    }
    const uint8_t base = 1 + city * 3;
    forecastSeg[base + 0] = String(cityValue[city]);
    forecastSeg[base + 1] = (city + 1 < CITY_COUNT) ? String("  ") : String("");
    forecastSeg[base + 2] = String("");
    forecastCol[base + 0] = getCityUiColor(city);
    forecastCol[base + 1] = TFT_WHITE;
    forecastCol[base + 2] = TFT_WHITE;
  }
  String forecastKey = "";
  for (const String &seg : forecastSeg) forecastKey += seg;
  drawDebugSegmentLineIfChanged(6, 220, 14, forecastSeg, forecastCol, 1 + CITY_COUNT * 3, forecastKey.c_str(), force);

  String nextSeg[1 + CITY_COUNT * 3];
  uint16_t nextCol[1 + CITY_COUNT * 3];
  nextSeg[0] = "Kolejny: ";
  nextCol[0] = TFT_WHITE;
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    const ForecastViewState &forecast = g_forecastCache[city];
    if (forecast.hasData && forecast.days[0].valid) {
      snprintf(cityValue[city], sizeof(cityValue[city]), " %s %.0f/%.0fC",
               WEEKDAYS_ABBR[forecast.days[0].weekday].c_str(),
               forecast.days[0].minTemp,
               forecast.days[0].maxTemp);
    } else {
      snprintf(cityValue[city], sizeof(cityValue[city]), " brak");
    }
    const uint8_t base = 1 + city * 3;
    nextSeg[base + 0] = String(cityValue[city]);
    nextSeg[base + 1] = (city + 1 < CITY_COUNT) ? String("  ") : String("");
    nextSeg[base + 2] = String("");
    nextCol[base + 0] = getCityUiColor(city);
    nextCol[base + 1] = TFT_WHITE;
    nextCol[base + 2] = TFT_WHITE;
  }
  String nextKey = "";
  for (const String &seg : nextSeg) nextKey += seg;
  drawDebugSegmentLineIfChanged(7, 244, 14, nextSeg, nextCol, 1 + CITY_COUNT * 3, nextKey.c_str(), force);

  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    const AqiViewState &aqi = g_aqiCache[city];
    if (aqi.hasData) {
      snprintf(cityValue[city], sizeof(cityValue[city]), " AQI:%d PM2.5:%d PM10:%d age=%lus",
               aqi.aqi, aqi.pm25, aqi.pm10, (unsigned long)aqi.ageSeconds);
    } else {
      snprintf(cityValue[city], sizeof(cityValue[city]), " %s", fetchStateShort(aqi.fetchState));
    }
    String seg[] = {String("AQI: "), String(getCityUiName(city)), String(cityValue[city])};
    uint16_t colors[] = {TFT_WHITE, getCityUiColor(city), TFT_WHITE};
    String key = seg[0] + seg[1] + seg[2];
    drawDebugSegmentLineIfChanged(8 + city, 268 + (city * 24), 14, seg, colors, 3, key.c_str(), force);
  }

  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    const char *radValue = radiationJobServiceGetDisplayForCity(city);
    const char *radDateEnd = radiationJobServiceGetDateEndForCity(city);
    const char *radSource = (city == 1) ? "BfS" : "PAA";
    char radTsBuf[20];
    shortenRadTimestamp(radDateEnd, radTsBuf, sizeof(radTsBuf));
    snprintf(cityValue[city], sizeof(cityValue[city]), " %s %s %s",
             radSource,
             (radValue != nullptr && strlen(radValue) > 0) ? radValue : "n/a",
             radTsBuf);
    String seg[] = {String("RAD: "), String(getCityUiName(city)), String(cityValue[city])};
    uint16_t colors[] = {TFT_WHITE, getCityUiColor(city), TFT_WHITE};
    String key = seg[0] + seg[1] + seg[2];
    drawDebugSegmentLineIfChanged(11 + city, 340 + (city * 24), 14, seg, colors, 3, key.c_str(), force);
  }

  wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus == WL_CONNECTED) {
    String ssid = WiFi.SSID();
    if (ssid.length() > 14) {
      ssid = ssid.substring(0, 14) + "...";
    }
    snprintf(lineBuf, sizeof(lineBuf), "WiFi: połączono, %s, RSSI %d dBm", ssid.c_str(), WiFi.RSSI());
  } else {
    const char *wifiState = "rozłączone";
    if (wifiStatus == WL_CONNECT_FAILED) {
      wifiState = "błąd hasła";
    } else if (wifiStatus == WL_NO_SSID_AVAIL) {
      wifiState = "brak SSID";
    } else if (wifiStatus == WL_IDLE_STATUS) {
      wifiState = "bezczynne";
    }
    snprintf(lineBuf, sizeof(lineBuf), "WiFi: %s", wifiState);
  }
  drawDebugLineIfChanged(14, 412, 14, lineBuf, force);

  if (force || g_debugLdrLine[0] == '\0' || (nowMs - g_lastDebugLdrSampleMs) >= 2000U) {
    snprintf(g_debugLdrLine, sizeof(g_debugLdrLine), "LDR RGB: %u, skala LED: %u",
             ambientFiltered,
             (unsigned)getLedBrightnessForAmbient(ambientFiltered));
    g_lastDebugLdrSampleMs = nowMs;
  }
  drawDebugLineIfChanged(15, 436, 14, g_debugLdrLine, force);

  g_lastDebugRenderMs = nowMs;
}

void updateRescueStats(bool force) {
  if (g_currentPage != PAGE_STATS) {
    return;
  }

  struct CityHistoryStats {
    bool hasData;
    float minTemp;
    float maxTemp;
    uint32_t minTs;
    uint32_t maxTs;
  };

  auto oldestIndexFor = [](uint8_t city, uint8_t offset) -> uint8_t {
    return (uint8_t)((g_tempHistoryHead[city] + TEMP_HISTORY_SIZE - g_tempHistoryCount[city] + offset) % TEMP_HISTORY_SIZE);
  };

  auto formatHistoryClock = [](uint32_t sampleTs, char *buf, size_t bufSize) {
    if (sampleTs == 0) {
      snprintf(buf, bufSize, "--:--");
      return;
    }
    time_t raw = (time_t)sampleTs;
    struct tm *tmInfo = localtime(&raw);
    if (tmInfo == nullptr) {
      snprintf(buf, bufSize, "--:--");
      return;
    }
    strftime(buf, bufSize, UI_TIME_FORMAT_NO_SECONDS, tmInfo);
  };

  auto sampleTimeForOffset = [&](uint8_t city, uint8_t offset) -> uint32_t {
    if (g_tempHistoryCount[city] == 0) {
      return 0;
    }
    const uint8_t idx = oldestIndexFor(city, offset);
    return g_tempHistorySampleTs[city][idx];
  };

  CityHistoryStats stats[CITY_COUNT] = {};

  char renderKey[512];
  size_t renderKeyLen = 0;
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    const float latestTemp = (g_tempHistoryCount[city] > 0)
      ? g_tempHistory[city][(g_tempHistoryHead[city] + TEMP_HISTORY_SIZE - 1) % TEMP_HISTORY_SIZE]
      : NAN;
    renderKeyLen += snprintf(renderKey + renderKeyLen,
                             sizeof(renderKey) - renderKeyLen,
                             "%u|%u|%.1f|%lu|%lu|",
                             g_tempHistoryHead[city],
                             g_tempHistoryCount[city],
                             latestTemp,
                             (unsigned long)sampleTimeForOffset(city, 0),
                             (unsigned long)sampleTimeForOffset(city, g_tempHistoryCount[city] > 0 ? (g_tempHistoryCount[city] - 1) : 0));
    if (renderKeyLen >= sizeof(renderKey)) {
      break;
    }
  }
  if (!force && strcmp(renderKey, g_lastStatsRenderKey) == 0) {
    return;
  }

  tft.fillScreen(TFT_BLACK);
  drawTextWithSmoothFont(FONT_VLW_UI_20, "Wykres temperatur", centerWidth, 14, TC_DATUM, TFT_WHITE);
  const int16_t headerXs[CITY_COUNT] = {18, 122, 226};
  char lineBuf[32];
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    drawTextWithSmoothFont(FONT_VLW_UI_14, getCityUiName(city), headerXs[city], 42, TL_DATUM, getCityUiColor(city));
    if (g_weatherCache[city].hasData) {
      snprintf(lineBuf, sizeof(lineBuf), "%.1fC", g_weatherCache[city].temperature);
    } else {
      snprintf(lineBuf, sizeof(lineBuf), "--");
    }
    drawTextWithSmoothFont(FONT_VLW_UI_14, lineBuf, headerXs[city], 60, TL_DATUM, TFT_WHITE);
  }

  const int graphX = 34;
  const int graphY = 112;
  const int graphW = tft.width() - 56;
  const int graphH = 150;
  const int graphBottomY = graphY + graphH - 1;

  tft.drawRoundRect(graphX - 2, graphY - 2, graphW + 4, graphH + 4, 6, 0x4228);

  float minTemp = 999.0f;
  float maxTemp = -999.0f;
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    for (uint8_t i = 0; i < g_tempHistoryCount[city]; i++) {
      const uint8_t idx = (g_tempHistoryHead[city] + TEMP_HISTORY_SIZE - g_tempHistoryCount[city] + i) % TEMP_HISTORY_SIZE;
      const float v = g_tempHistory[city][idx];
      if (!isnan(v)) {
        if (v < minTemp) minTemp = v;
        if (v > maxTemp) maxTemp = v;
        if (!stats[city].hasData) {
          stats[city].hasData = true;
          stats[city].minTemp = v;
          stats[city].maxTemp = v;
          stats[city].minTs = g_tempHistorySampleTs[city][idx];
          stats[city].maxTs = g_tempHistorySampleTs[city][idx];
        } else {
          if (v < stats[city].minTemp) {
            stats[city].minTemp = v;
            stats[city].minTs = g_tempHistorySampleTs[city][idx];
          }
          if (v > stats[city].maxTemp) {
            stats[city].maxTemp = v;
            stats[city].maxTs = g_tempHistorySampleTs[city][idx];
          }
        }
      }
    }
  }
  if (minTemp > maxTemp) {
    minTemp = -5.0f;
    maxTemp = 25.0f;
  }
  if ((maxTemp - minTemp) < 4.0f) {
    const float mid = (minTemp + maxTemp) * 0.5f;
    minTemp = mid - 2.0f;
    maxTemp = mid + 2.0f;
  }

  const uint8_t gridRows = 4;
  const uint8_t gridCols = 6;
  for (uint8_t row = 0; row <= gridRows; row++) {
    const int16_t y = graphY + (int32_t)row * graphH / gridRows;
    tft.drawFastHLine(graphX, y, graphW, row == (gridRows / 2) ? 0x3186 : 0x2124);
    const float rowTemp = maxTemp - ((maxTemp - minTemp) * row / gridRows);
    snprintf(lineBuf, sizeof(lineBuf), "%.1f", rowTemp);
    drawTextWithSmoothFont(FONT_VLW_UI_14, lineBuf, graphX - 4, y - 7, TR_DATUM, TFT_WHITE);
  }
  for (uint8_t col = 0; col <= gridCols; col++) {
    const int16_t x = graphX + (int32_t)col * (graphW - 1) / gridCols;
    tft.drawFastVLine(x, graphY, graphH, 0x2124);
  }
  drawTextWithSmoothFont(FONT_VLW_UI_14, "Temp [C]", graphX, graphY - 4, BL_DATUM, TFT_DARKGREY);

  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    const uint16_t color = getCityUiColor(city);
    int16_t prevX = -1;
    int16_t prevY = -1;
    const int slotDivisor = TEMP_HISTORY_SIZE;
    for (uint8_t i = 0; i < g_tempHistoryCount[city]; i++) {
      const uint8_t idx = oldestIndexFor(city, i);
      const float v = g_tempHistory[city][idx];
      if (isnan(v)) {
        continue;
      }
      const int16_t x = graphX + (int32_t)i * (graphW - 1) / slotDivisor;
      const float norm = (v - minTemp) / (maxTemp - minTemp);
      const int16_t y = graphY + graphH - 1 - (int16_t)(norm * (graphH - 1));
      if (prevX >= 0) {
        tft.drawLine(prevX, prevY, x, y, color);
        tft.drawLine(prevX, prevY + 1, x, y + 1, color);
      }
      tft.fillRect(x, y, 2, 2, color);
      prevX = x;
      prevY = y;
    }
  }

  const int16_t axisTickBaseY = graphBottomY + 4;
  const int16_t axisTickTallY = graphBottomY + 10;
  for (uint8_t i = 0; i < TEMP_HISTORY_SIZE; i++) {
    const int16_t x = graphX + (int32_t)i * (graphW - 1) / TEMP_HISTORY_SIZE;
    const bool majorTick = (i % 4U) == 0U || i == (TEMP_HISTORY_SIZE - 1U);
    tft.drawFastVLine(x, axisTickBaseY, majorTick ? 7 : 4, majorTick ? 0x528A : 0x3186);
  }

  uint8_t labelCity = 0;
  for (uint8_t city = 1; city < CITY_COUNT; city++) {
    if (g_tempHistoryCount[city] > g_tempHistoryCount[labelCity]) {
      labelCity = city;
    }
  }
  if (g_tempHistoryCount[labelCity] > 0) {
    const uint8_t lastOffset = (uint8_t)(g_tempHistoryCount[labelCity] - 1U);
    char lastTimeBuf[8];
    formatHistoryClock(sampleTimeForOffset(labelCity, lastOffset), lastTimeBuf, sizeof(lastTimeBuf));
    const int16_t lastX = graphX + (int32_t)lastOffset * (graphW - 1) / TEMP_HISTORY_SIZE;
    const int16_t lastWidth = measureTextWithSmoothFont(FONT_VLW_UI_14, lastTimeBuf);
    const int16_t lastReservedLeft = lastX - (lastWidth / 2);
    int16_t prevRight = -32768;

    for (uint8_t offset = 0; offset < g_tempHistoryCount[labelCity]; offset++) {
      char timeBuf[8];
      formatHistoryClock(sampleTimeForOffset(labelCity, offset), timeBuf, sizeof(timeBuf));
      const int16_t x = graphX + (int32_t)offset * (graphW - 1) / TEMP_HISTORY_SIZE;
      const int16_t textWidth = measureTextWithSmoothFont(FONT_VLW_UI_14, timeBuf);
      const int16_t left = x - (textWidth / 2);
      const int16_t right = left + textWidth;
      const bool isFirst = (offset == 0U);
      const bool isLast = (offset == lastOffset);

      if (isLast) {
        continue;
      }
      if (!isFirst && left <= (prevRight + 8)) {
        continue;
      }
      if (!isFirst && right >= (lastReservedLeft - 8)) {
        continue;
      }

      drawTextWithSmoothFont(FONT_VLW_UI_14, timeBuf, x, graphBottomY + 18, TC_DATUM, TFT_WHITE);
      prevRight = right;
    }

    drawTextWithSmoothFont(FONT_VLW_UI_14, lastTimeBuf, lastX, graphBottomY + 18, TC_DATUM, TFT_WHITE);
  }

  drawTextWithSmoothFont(FONT_VLW_UI_14, "Czas", centerWidth, graphBottomY + 34, TC_DATUM, TFT_DARKGREY);
  drawTextWithSmoothFont(FONT_VLW_UI_14, "24 probki: co odsw. pogody, potem co 1h", centerWidth, graphBottomY + 50, TC_DATUM, TFT_DARKGREY);

  const int16_t summaryTopY = 344;
  const int16_t summaryXs[CITY_COUNT] = {12, 114, 216};
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    drawTextWithSmoothFont(FONT_VLW_UI_14, getCityUiName(city), summaryXs[city], summaryTopY, TL_DATUM, getCityUiColor(city));
  }

  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    const int16_t baseX = summaryXs[city];
    char minBuf[24];
    char maxBuf[24];
    char minTime[8];
    char maxTime[8];

    if (stats[city].hasData) {
      snprintf(minBuf, sizeof(minBuf), "Min %.1fC", stats[city].minTemp);
      snprintf(maxBuf, sizeof(maxBuf), "Max %.1fC", stats[city].maxTemp);
      formatHistoryClock(stats[city].minTs, minTime, sizeof(minTime));
      formatHistoryClock(stats[city].maxTs, maxTime, sizeof(maxTime));
    } else {
      snprintf(minBuf, sizeof(minBuf), "Min --");
      snprintf(maxBuf, sizeof(maxBuf), "Max --");
      snprintf(minTime, sizeof(minTime), "--:--");
      snprintf(maxTime, sizeof(maxTime), "--:--");
    }

    drawTextWithSmoothFont(FONT_VLW_UI_14, minBuf, baseX, summaryTopY + 26, TL_DATUM, TFT_WHITE);
    drawTextWithSmoothFont(FONT_VLW_UI_14, minTime, baseX, summaryTopY + 44, TL_DATUM, TFT_DARKGREY);
    drawTextWithSmoothFont(FONT_VLW_UI_14, maxBuf, baseX, summaryTopY + 72, TL_DATUM, TFT_WHITE);
    drawTextWithSmoothFont(FONT_VLW_UI_14, maxTime, baseX, summaryTopY + 90, TL_DATUM, TFT_DARKGREY);
  }

  strncpy(g_lastStatsRenderKey, renderKey, sizeof(g_lastStatsRenderKey) - 1);
  g_lastStatsRenderKey[sizeof(g_lastStatsRenderKey) - 1] = '\0';
}

void updateRescueMenu(bool force) {
  if (g_currentPage != PAGE_MENU) {
    return;
  }

  if (force) {
    invalidateMenuFull();
  }

  const bool fullRedraw = (g_menuDirtyFlags & MENU_DIRTY_FULL) != 0;
  if (fullRedraw) {
    tft.fillScreen(TFT_BLACK);
    clearMenuRenderCaches();
  }

  const uint32_t staticFlag = getMenuStaticDirtyFlag(g_menuScreen);
  const uint32_t dynamicFlag = getMenuDynamicDirtyFlag(g_menuScreen);
  const bool redrawStatic = fullRedraw || ((g_menuDirtyFlags & staticFlag) != 0);
  const bool redrawDynamic = redrawStatic || ((g_menuDirtyFlags & dynamicFlag) != 0);

  if (!redrawStatic && !redrawDynamic) {
    return;
  }

  if (redrawStatic) {
    g_lastMenuScreenRendered = (uint8_t)g_menuScreen;
    if (g_menuScreen == MENU_WIFI) {
      g_lastMenuWifiScreenRendered = (uint8_t)g_menuWifiScreen;
    }
  }

  switch (g_menuScreen) {
    case MENU_HOME:
      if (redrawStatic) {
        drawTextWithSmoothFont(FONT_VLW_UI_20, String("Menu / Ustawienia"), centerWidth, 18, TC_DATUM, TFT_WHITE);
        drawMenuCloseButton("Wyjdź");
        drawMenuCard(MENU_HOME_WIFI_BUTTON, "WiFi", COLOR_MENU);
        drawMenuCard(MENU_HOME_VIEW_BUTTON, "Widok", COLOR_GRAPH);
        drawMenuCard(MENU_HOME_BRIGHTNESS_BUTTON, "Jasność", TFT_WHITE);
        drawMenuCard(MENU_HOME_COLORS_BUTTON, "Kolory", TFT_RED);
        drawMenuCard(MENU_HOME_RGB_BUTTON, "RGB LED", COLOR_DEBUG);
        drawMenuCard(MENU_HOME_TOUCH_BUTTON, "Test dotyku", COLOR_KIELCE);
      }
      break;

    case MENU_WIFI: {
      if (redrawDynamic) {
        g_lastMenuWifiScreenRendered = (uint8_t)g_menuWifiScreen;
        switch (g_menuWifiScreen) {
          case MENU_WIFI_HOME:
            renderMenuWifiOverview(redrawStatic);
            break;
          case MENU_WIFI_NETWORKS:
            renderMenuWifiNetworks(redrawStatic);
            break;
          case MENU_WIFI_PASSWORD:
            renderMenuWifiPassword(redrawStatic);
            break;
          case MENU_WIFI_IP:
          default:
            renderMenuWifiIp(redrawStatic);
            break;
        }
      }
      break;
    }

    case MENU_VIEW: {
      if (redrawStatic) {
        drawMenuHeader("Widok");
        drawMenuCheckbox(MENU_VIEW_SECONDS_BOX, "Sekundy na zegarze", false, TFT_WHITE);
        drawMenuCheckbox(MENU_VIEW_FORECAST_BOX, "Prognoza", false, TFT_WHITE);
        drawMenuCheckbox(MENU_VIEW_SUN_BOX, "Słońce", false, TFT_WHITE);
        drawMenuCheckbox(MENU_VIEW_AQI_BOX, "AQI", false, TFT_WHITE);
        drawMenuCheckbox(MENU_VIEW_RAD_BOX, "RAD", false, TFT_WHITE);
      }

      char viewKey[16];
      snprintf(viewKey, sizeof(viewKey), "%u%u%u%u%u",
               (unsigned)g_menuDraft.showSeconds,
               (unsigned)g_menuDraft.showForecast,
               (unsigned)g_menuDraft.showSun,
               (unsigned)g_menuDraft.showAqi,
               (unsigned)g_menuDraft.showRad);
      if (!redrawDynamic || (!redrawStatic && strcmp(viewKey, g_lastMenuViewKey) == 0)) {
        break;
      }

      drawMenuCheckboxState(MENU_VIEW_SECONDS_BOX, g_menuDraft.showSeconds, TFT_WHITE);
      drawMenuCheckboxState(MENU_VIEW_FORECAST_BOX, g_menuDraft.showForecast, TFT_WHITE);
      drawMenuCheckboxState(MENU_VIEW_SUN_BOX, g_menuDraft.showSun, TFT_WHITE);
      drawMenuCheckboxState(MENU_VIEW_AQI_BOX, g_menuDraft.showAqi, TFT_WHITE);
      drawMenuCheckboxState(MENU_VIEW_RAD_BOX, g_menuDraft.showRad, TFT_WHITE);

      strncpy(g_lastMenuViewKey, viewKey, sizeof(g_lastMenuViewKey) - 1);
      g_lastMenuViewKey[sizeof(g_lastMenuViewKey) - 1] = '\0';
      break;
    }

    case MENU_BRIGHTNESS: {
      if (redrawStatic) {
        drawMenuHeader("Jasność");
        drawMenuCheckbox(MENU_BRIGHT_AUTO_BOX, "Auto", false, TFT_WHITE);
        drawMenuCheckbox(MENU_BRIGHT_DAY_BOX, "Dzień wymuszony", false, TFT_WHITE);
        drawMenuCheckbox(MENU_BRIGHT_NIGHT_BOX, "Noc wymuszona", false, TFT_WHITE);
        drawTextWithSmoothFont(FONT_VLW_UI_14, String("LCD reaguje od razu"), 24, 250, TL_DATUM, TFT_DARKGREY);
        drawTextWithSmoothFont(FONT_VLW_UI_14, String("Tryb nocny gasi RGB"), 24, 272, TL_DATUM, TFT_DARKGREY);
      }

      char brightKey[16];
      snprintf(brightKey, sizeof(brightKey), "%u", (unsigned)g_menuDraft.brightnessMode);
      if (!redrawDynamic || (!redrawStatic && strcmp(brightKey, g_lastMenuRenderKey) == 0)) {
        break;
      }

      drawMenuCheckboxState(MENU_BRIGHT_AUTO_BOX, g_menuDraft.brightnessMode == LCD_MODE_AUTO, TFT_WHITE);
      drawMenuCheckboxState(MENU_BRIGHT_DAY_BOX, g_menuDraft.brightnessMode == LCD_MODE_DAY, TFT_WHITE);
      drawMenuCheckboxState(MENU_BRIGHT_NIGHT_BOX, g_menuDraft.brightnessMode == LCD_MODE_NIGHT, TFT_WHITE);

      strncpy(g_lastMenuRenderKey, brightKey, sizeof(g_lastMenuRenderKey) - 1);
      g_lastMenuRenderKey[sizeof(g_lastMenuRenderKey) - 1] = '\0';
      break;
    }

    case MENU_COLORS: {
      if (redrawStatic) {
        drawMenuHeader("Kolory miast");
        drawTextWithSmoothFont(FONT_VLW_UI_16, String("Kolor RGB = kolor miasta"), centerWidth, 48, TC_DATUM, TFT_DARKGREY);
        drawTextWithSmoothFont(FONT_VLW_UI_14, String("Wybierz miasto"), 24, 64, TL_DATUM, TFT_WHITE);
      }

      char colorsKey[64];
      snprintf(colorsKey, sizeof(colorsKey), "%u|%u|%u|%u|%s",
               (unsigned)g_menuColorsSelectedCity,
               (unsigned)g_cityUiColors[0],
               (unsigned)g_cityUiColors[1],
               (unsigned)g_cityUiColors[2],
               g_menuColorsStatusText);
      if (!redrawDynamic || (!redrawStatic && strcmp(colorsKey, g_lastMenuColorsKey) == 0)) {
        break;
      }

      tft.fillRect(0, 76, tft.width(), 364, TFT_BLACK);
      drawMenuCheckbox(MENU_COLORS_CITY0_BOX, "Kielce", false, getCityUiColor(0));
      drawMenuCheckbox(MENU_COLORS_CITY1_BOX, "Dauchingen", false, getCityUiColor(1));
      drawMenuCheckbox(MENU_COLORS_CITY2_BOX, "Tarnowitz", false, getCityUiColor(2));
      drawMenuCheckboxState(MENU_COLORS_CITY0_BOX, g_menuColorsSelectedCity == 0, getCityUiColor(0));
      drawMenuCheckboxState(MENU_COLORS_CITY1_BOX, g_menuColorsSelectedCity == 1, getCityUiColor(1));
      drawMenuCheckboxState(MENU_COLORS_CITY2_BOX, g_menuColorsSelectedCity == 2, getCityUiColor(2));

      drawTextWithSmoothFont(FONT_VLW_UI_14, String("Paleta TFT"), 24, 158, TL_DATUM, TFT_WHITE);
      for (uint8_t i = 0; i < getMenuPaletteColorCount(); i++) {
        const bool selected = g_cityUiColors[g_menuColorsSelectedCity] == getMenuPaletteColor(i);
        drawMenuColorSwatch(i, selected);
      }

      drawMenuButton(MENU_COLORS_DEFAULTS_BUTTON, "Domyślne kolory", TFT_RED, false, false);

      if (g_menuColorsStatusText[0] != '\0') {
        drawTextWithSmoothFont(FONT_VLW_UI_14, String(g_menuColorsStatusText), centerWidth, 396, TC_DATUM, TFT_ORANGE);
      } else {
        drawTextWithSmoothFont(FONT_VLW_UI_14, String("Krzyżyk = kolor zajęty"), centerWidth, 396, TC_DATUM, TFT_DARKGREY);
      }

      strncpy(g_lastMenuColorsKey, colorsKey, sizeof(g_lastMenuColorsKey) - 1);
      g_lastMenuColorsKey[sizeof(g_lastMenuColorsKey) - 1] = '\0';
      break;
    }

    case MENU_RGB: {
      const RectangleDef sliders[3] = {MENU_RGB_SLIDER_R, MENU_RGB_SLIDER_G, MENU_RGB_SLIDER_B};
      const uint16_t sliderColors[3] = {TFT_RED, TFT_GREEN, TFT_BLUE};
      const char *labels[3] = {"R", "G", "B"};
      const uint8_t values[3] = {g_menuDraft.rgbR, g_menuDraft.rgbG, g_menuDraft.rgbB};
      if (redrawStatic) {
        drawMenuHeader("RGB LED");
        drawTextWithSmoothFont(FONT_VLW_UI_18, String("Zmiana koloru na zywo"), centerWidth, 56, TC_DATUM, TFT_DARKGREY);
        for (uint8_t i = 0; i < 3; i++) {
          const RectangleDef &slider = sliders[i];
          tft.drawRect(slider.x, slider.y, slider.width, slider.height, sliderColors[i]);
          drawTextWithSmoothFont(FONT_VLW_UI_18, String(labels[i]), slider.x + (slider.width / 2), slider.y - 20, TC_DATUM, sliderColors[i]);
        }
      }

      char rgbKey[32];
      snprintf(rgbKey, sizeof(rgbKey), "%u|%u|%u",
               (unsigned)g_menuDraft.rgbR,
               (unsigned)g_menuDraft.rgbG,
               (unsigned)g_menuDraft.rgbB);
      if (!redrawDynamic || (!redrawStatic && strcmp(rgbKey, g_lastMenuRgbKey) == 0)) {
        break;
      }

      for (uint8_t i = 0; i < 3; i++) {
        const RectangleDef &slider = sliders[i];
        tft.fillRect(slider.x + 1, slider.y + 1, slider.width - 2, slider.height - 2, TFT_BLACK);
        const int16_t knobY = slider.y + slider.height - 1 - ((int32_t)values[i] * (slider.height - 8) / 255);
        tft.fillRect(slider.x + 4, knobY, slider.width - 8, 8, sliderColors[i]);
        tft.fillRect(slider.x - 10, slider.y + slider.height + 4, slider.width + 20, 22, TFT_BLACK);
        char valueBuf[8];
        snprintf(valueBuf, sizeof(valueBuf), "%u", values[i]);
        drawTextWithSmoothFont(FONT_VLW_UI_14, String(valueBuf), slider.x + (slider.width / 2), slider.y + slider.height + 8, TC_DATUM, TFT_WHITE);
      }
      tft.fillRect(70, 350, 180, 70, TFT_BLACK);
      tft.fillRoundRect(82, 358, 156, 34, 6, ((g_menuDraft.rgbR >> 3) << 11) | ((g_menuDraft.rgbG >> 2) << 5) | (g_menuDraft.rgbB >> 3));
      char rgbBuf[32];
      snprintf(rgbBuf, sizeof(rgbBuf), "R:%u G:%u B:%u", g_menuDraft.rgbR, g_menuDraft.rgbG, g_menuDraft.rgbB);
      drawTextWithSmoothFont(FONT_VLW_UI_16, String(rgbBuf), centerWidth, 404, TC_DATUM, TFT_WHITE);

      strncpy(g_lastMenuRgbKey, rgbKey, sizeof(g_lastMenuRgbKey) - 1);
      g_lastMenuRgbKey[sizeof(g_lastMenuRgbKey) - 1] = '\0';
      break;
    }

    case MENU_TOUCH: {
      if (redrawStatic) {
        drawMenuHeader("Test dotyku");
        drawTextWithSmoothFont(FONT_VLW_UI_18, String("Ekran testowy dotyku"), centerWidth, 56, TC_DATUM, TFT_DARKGREY);
        for (int x = 20; x < tft.width(); x += 30) {
          tft.drawFastVLine(x, 86, 320, 0x18C3);
        }
        for (int y = 86; y < 406; y += 30) {
          tft.drawFastHLine(20, y, tft.width() - 40, 0x18C3);
        }
      }

      const bool cursorVisible = g_touchSampleValid && isPointInMenuTouchGrid(g_touchX, g_touchY);
      char touchKey[24];
      snprintf(touchKey, sizeof(touchKey), "%u|%u|%u",
               (unsigned)cursorVisible,
               (unsigned)g_touchX,
               (unsigned)g_touchY);
      if (!redrawDynamic || (!redrawStatic && strcmp(touchKey, g_lastMenuTouchKey) == 0)) {
        break;
      }

      if (g_lastMenuTouchCursorVisible) {
        redrawMenuTouchGridRegion(g_lastMenuTouchCursorX - 18, g_lastMenuTouchCursorY - 18, 37, 37);
      }

      tft.fillRect(0, 416, tft.width(), 24, TFT_BLACK);
      if (g_touchSampleValid) {
        char touchBuf[32];
        snprintf(touchBuf, sizeof(touchBuf), "X=%u Y=%u", g_touchX, g_touchY);
        drawTextWithSmoothFont(FONT_VLW_UI_18, String(touchBuf), centerWidth, 426, TC_DATUM, TFT_WHITE);
      }

      if (cursorVisible) {
        tft.drawCircle((int16_t)g_touchX, (int16_t)g_touchY, 10, TFT_TP_BLUE);
        tft.drawFastHLine((int16_t)g_touchX - 16, (int16_t)g_touchY, 33, TFT_TP_BLUE);
        tft.drawFastVLine((int16_t)g_touchX, (int16_t)g_touchY - 16, 33, TFT_TP_BLUE);
        g_lastMenuTouchCursorX = (int16_t)g_touchX;
        g_lastMenuTouchCursorY = (int16_t)g_touchY;
      } else {
        g_lastMenuTouchCursorX = -1;
        g_lastMenuTouchCursorY = -1;
      }
      g_lastMenuTouchCursorVisible = cursorVisible;
      strncpy(g_lastMenuTouchKey, touchKey, sizeof(g_lastMenuTouchKey) - 1);
      g_lastMenuTouchKey[sizeof(g_lastMenuTouchKey) - 1] = '\0';
      break;
    }
  }

  g_menuDirtyFlags &= ~(MENU_DIRTY_FULL | staticFlag | dynamicFlag);
}

const char* getForecastIconName(uint16_t weatherId) {
  if (weatherId >= 200 && weatherId < 300) return "thunderstorm";
  if (weatherId >= 300 && weatherId < 400) return "drizzle";
  if (weatherId == 500) return "light-rain";
  if (weatherId >= 501 && weatherId <= 504) return "rain";
  if (weatherId >= 511 && weatherId < 600) return "sleet";
  if (weatherId >= 600 && weatherId < 700) return "snow";
  if (weatherId >= 700 && weatherId < 800) return "fog";
  if (weatherId == 800) return "clear-day";
  if (weatherId == 801) return "partly-cloudy-day";
  if (weatherId >= 802 && weatherId <= 804) return "cloudy";
  return "unknown";
}

void renderCurrentRescuePage() {
  switch (g_currentPage) {
    case PAGE_DEBUG:
      renderRescueDebugScreen();
      return;
    case PAGE_STATS:
      renderRescueStatsScreen();
      return;
    case PAGE_MENU:
      renderRescueMenuScreen();
      return;
    case PAGE_MAIN:
    default:
      renderRescueMainScreen();
      return;
  }
}

void sampleTemperatureHistory(uint32_t nowMs) {
  for (uint8_t city = 0; city < CITY_COUNT; city++) {
    const CurrentWeatherViewState &weather = g_weatherCache[city];
    if (!weather.hasData) {
      continue;
    }

    const uint32_t updateStamp = currentWeatherServiceGetLastSuccessMsForCity(city);
    if (updateStamp == 0 || updateStamp == g_tempHistoryLastCaptureStamp[city]) {
      continue;
    }

    const bool bootstrapPhase = g_tempHistoryCount[city] < TEMP_HISTORY_SIZE;
    if (!bootstrapPhase && g_tempHistoryCount[city] > 0) {
      const uint8_t lastIdx =
        (uint8_t)((g_tempHistoryHead[city] + TEMP_HISTORY_SIZE - 1U) % TEMP_HISTORY_SIZE);
      const uint32_t lastSampleTs = g_tempHistorySampleTs[city][lastIdx];
      const uint32_t currentSampleTs =
        weather.observationTime > 0 ? weather.observationTime :
        (hasValidTime() ? (uint32_t)time(nullptr) : 0U);

      if (lastSampleTs > 0 && currentSampleTs > 0 &&
          (currentSampleTs - lastSampleTs) < (TEMP_HISTORY_INTERVAL_MS / 1000UL)) {
        g_tempHistoryLastCaptureStamp[city] = updateStamp;
        continue;
      }
      if (lastSampleTs == 0 &&
          (nowMs - g_tempHistoryLastSampleMs[city]) < TEMP_HISTORY_INTERVAL_MS) {
        g_tempHistoryLastCaptureStamp[city] = updateStamp;
        continue;
      }
    }

    const uint8_t head = g_tempHistoryHead[city];
    g_tempHistory[city][head] = weather.temperature;
    g_tempHistorySampleTs[city][head] =
      weather.observationTime > 0 ? weather.observationTime :
      (hasValidTime() ? (uint32_t)time(nullptr) : 0U);
    g_tempHistoryHead[city] = (uint8_t)((g_tempHistoryHead[city] + 1U) % TEMP_HISTORY_SIZE);
    if (g_tempHistoryCount[city] < TEMP_HISTORY_SIZE) {
      g_tempHistoryCount[city]++;
    }
    g_tempHistoryLastSampleMs[city] = nowMs;
    g_tempHistoryLastCaptureStamp[city] = updateStamp;
  }
}

void serviceRescueTouch(uint32_t nowMs) {
  if (ts == nullptr) {
    return;
  }

  ts->read();
  const bool touchActive = ts->isTouched && ts->touches > 0;

  if (touchActive) {
    g_touchX = ts->points[0].x;
    g_touchY = ts->points[0].y;
    g_touchSampleValid = true;
  }

  if (g_currentPage == PAGE_MENU) {
    if (!touchActive) {
      if (g_touchWasActive) {
        const int16_t releasedTarget = g_menuPressedTarget;
        g_menuPressedTarget = 0;
        handleMenuPressedTargetRelease(releasedTarget);
      }
      g_menuWifiPressedCode = 0;
      g_touchWasActive = false;
      return;
    }

    const int16_t pressedTarget = getMenuPressedTargetAt(g_touchX, g_touchY);
    if (g_menuScreen == MENU_RGB || g_menuScreen == MENU_TOUCH) {
      g_menuPressedTarget = pressedTarget;
    } else if (pressedTarget != 0) {
      g_menuPressedTarget = pressedTarget;
    }
    if (g_menuScreen == MENU_WIFI) {
      const int16_t pressedCode = (pressedTarget >= 1000) ? (pressedTarget - 1000) : 0;
      if (pressedCode != g_menuWifiPressedCode) {
        const int16_t prevPressedCode = g_menuWifiPressedCode;
        g_menuWifiPressedCode = pressedCode;
        if (g_menuWifiScreen == MENU_WIFI_PASSWORD) {
          if (prevPressedCode != 0) {
            redrawMenuWifiPasswordPressedControl(prevPressedCode);
          }
          if (pressedCode != 0) {
            redrawMenuWifiPasswordPressedControl(pressedCode);
          }
        } else if (g_menuWifiScreen == MENU_WIFI_IP) {
          if (prevPressedCode != 0) {
            redrawMenuWifiIpPressedControl(prevPressedCode);
          }
          if (pressedCode != 0) {
            redrawMenuWifiIpPressedControl(pressedCode);
          }
        } else {
          invalidateMenuScreen(MENU_WIFI, false, true);
          updateRescueMenu(false);
        }
      }
    } else if (g_menuScreen == MENU_RGB) {
      if (pointInRect(g_touchX, g_touchY, MENU_RGB_SLIDER_R) ||
          pointInRect(g_touchX, g_touchY, MENU_RGB_SLIDER_G) ||
          pointInRect(g_touchX, g_touchY, MENU_RGB_SLIDER_B)) {
        updateMenuRgbFromTouch(g_touchY);
        invalidateMenuScreen(MENU_RGB, false, true);
        updateRescueMenu(false);
      }
    } else if (g_menuScreen == MENU_TOUCH) {
      invalidateMenuScreen(MENU_TOUCH, false, true);
      updateRescueMenu(false);
    }

    g_touchWasActive = true;
    return;
  }

  bool handledRelease = false;
  if (!touchActive) {
    if (!g_navOverlayVisible &&
        g_currentPage == PAGE_MAIN &&
        g_touchWasActive &&
        !g_navLongPressTriggered &&
        pointInRect(g_touchPressStartX, g_touchPressStartY, MAIN_NIGHT_TOGGLE_BUTTON) &&
        pointInRect(g_touchX, g_touchY, MAIN_NIGHT_TOGGLE_BUTTON)) {
      toggleMainNightOverride();
      g_touchConsumeUntilRelease = false;
      g_touchWasActive = false;
      return;
    }
    if (g_navOverlayVisible && g_touchWasActive && !g_touchConsumeUntilRelease && g_navOverlayTapArmed) {
      handledRelease = handleOverlayTap(g_touchX, g_touchY);
    }
    if (!handledRelease) {
      if (g_navOverlayVisible && g_touchConsumeUntilRelease) {
        g_navOverlayTapArmed = true;
      }
      g_touchConsumeUntilRelease = false;
    }
    if (g_navOverlayVisible && g_overlayPressedButton != -1) {
      g_overlayPressedButton = -1;
      renderNavOverlay();
    }
  }

  if (touchActive && g_touchConsumeUntilRelease) {
    g_touchWasActive = touchActive;
    return;
  }

  if (touchActive && !g_touchWasActive) {
    g_touchPressStartMs = nowMs;
    g_touchPressStartX = g_touchX;
    g_touchPressStartY = g_touchY;
    g_navLongPressTriggered = false;

    if (g_navOverlayVisible) {
      const int8_t pressedButton = getOverlayButtonAt(g_touchX, g_touchY);
      if (pressedButton != g_overlayPressedButton) {
        g_overlayPressedButton = pressedButton;
        renderNavOverlay();
      }
      g_touchWasActive = touchActive;
      return;
    }

  } else if (touchActive && g_currentPage != PAGE_MENU && !g_navOverlayVisible && !g_navLongPressTriggered) {
    const uint16_t dx = (g_touchX > g_touchPressStartX) ? (g_touchX - g_touchPressStartX) : (g_touchPressStartX - g_touchX);
    const uint16_t dy = (g_touchY > g_touchPressStartY) ? (g_touchY - g_touchPressStartY) : (g_touchPressStartY - g_touchY);
    if (dx <= NAV_OVERLAY_MOVE_TOLERANCE &&
        dy <= NAV_OVERLAY_MOVE_TOLERANCE &&
        (nowMs - g_touchPressStartMs) >= NAV_OVERLAY_LONG_PRESS_MS &&
        touchServiceAllowPageSwitch((uint8_t)getCurrentLcdPwm(), nowMs)) {
      g_navLongPressTriggered = true;
      showNavOverlay();
    }
  } else if (touchActive && g_navOverlayVisible) {
    const int8_t pressedButton = getOverlayButtonAt(g_touchX, g_touchY);
    if (pressedButton != g_overlayPressedButton) {
      g_overlayPressedButton = pressedButton;
      renderNavOverlay();
    }
  } else if (!touchActive && g_navOverlayVisible) {
    // keep overlay open until explicit tap
  }

  g_touchWasActive = touchActive;
}

void serviceOta() {
  ArduinoOTA.handle();
}

void serviceAmbientAndWake(uint32_t nowMs) {
  ambientServiceUpdate(ambientFiltered, isNightMode, touchServiceWakeWindowActive());
  touchServiceTimeout(nowMs, getLcdBrightnessForAmbient(ambientFiltered));

  if (g_lcdBrightnessMode == LCD_MODE_NIGHT) {
    isNightMode = true;
    setLcdBrightness(LCD_BRIGHT_NIGHT);
    setRgbCommonAnode(0, 0, 0);
    return;
  }

  if (g_lcdBrightnessMode == LCD_MODE_DAY) {
    isNightMode = false;
    setLcdBrightness(LCD_BRIGHT_DAY);
  }

  if (g_currentPage == PAGE_MENU && g_menuScreen == MENU_RGB) {
    const uint8_t ledScale = getLedBrightnessForAmbient(ambientFiltered);
    const uint8_t liveR = (uint8_t)(((uint16_t)g_menuDraft.rgbR * ledScale) / 255U);
    const uint8_t liveG = (uint8_t)(((uint16_t)g_menuDraft.rgbG * ledScale) / 255U);
    const uint8_t liveB = (uint8_t)(((uint16_t)g_menuDraft.rgbB * ledScale) / 255U);
    setRgbCommonAnode(liveR, liveG, liveB);
    return;
  }

  const AppState *appState = getAppState();
  RadiationStatus radiationStatus = RAD_STATUS_NO_DATA;
  if (appState->radiation.hasData && appState->radiation.cityIndex == g_activeCityIndex) {
    radiationStatus = appState->radiation.status;
  }
  applyRgbLedState(g_currentPage,
                   g_activeCityIndex,
                   radiationStatus,
                   ambientFiltered,
                   g_otaInProgress);
}

void serviceNetworkJobs(uint32_t nowMs) {
  static const uint32_t NETWORK_JOB_SLOT_MS = 750U;

  if (nowMs >= g_nextNetworkJobSlotMs) {
    switch (g_nextNetworkJobIndex) {
      case 0:
        currentWeatherServiceUpdate(nowMs);
        break;
      case 1:
        forecastServiceUpdate(nowMs);
        break;
      case 2:
        aqiServiceUpdate(nowMs);
        break;
      case 3:
        radiationJobServiceUpdate(nowMs);
        break;
      default:
        break;
    }

    g_nextNetworkJobIndex = (uint8_t)((g_nextNetworkJobIndex + 1U) % 4U);
    g_nextNetworkJobSlotMs = millis() + NETWORK_JOB_SLOT_MS;
  }

  appStateUpdate(nowMs);
  syncCityCacheFromAppState();
  sampleTemperatureHistory(nowMs);
}

void serviceTimeSync(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) {
    if (g_timeSynced) {
      g_timeSynced = false;
      if (g_currentPage == PAGE_MAIN) {
        invalidateMainSectionCaches(false, true);
      } else if (g_currentPage == PAGE_DEBUG) {
        updateRescueDebug(true);
      }
    }
    return;
  }

  const bool timeValidNow = hasValidTime();
  if (timeValidNow != g_timeSynced) {
    g_timeSynced = timeValidNow;
    if (g_currentPage == PAGE_MAIN) {
      invalidateMainSectionCaches(false, true);
    } else if (g_currentPage == PAGE_DEBUG) {
      updateRescueDebug(true);
    }
  }

  if (!g_timeSynced && (nowMs - g_lastTimeSyncAttemptMs) >= 30000U) {
    syncTime();
    if (g_currentPage == PAGE_MAIN) {
      invalidateMainSectionCaches(false, true);
    } else if (g_currentPage == PAGE_DEBUG) {
      updateRescueDebug(true);
    }
  }
}

void serviceCurrentPage(uint32_t nowMs) {
  if (g_navOverlayVisible) {
    return;
  }
  switch (g_currentPage) {
    case PAGE_MAIN:
      updateRescueClock();
      updateRescueWeather(false);
      updateRescueForecast(false);
      break;
    case PAGE_STATS:
      updateRescueStats(false);
      break;
    case PAGE_DEBUG:
      updateRescueDebug(false);
      break;
    case PAGE_MENU:
      updateRescueMenu(false);
      break;
    default:
      break;
  }
}

void serviceRescueUi() {
  const uint32_t nowMs = millis();

  serviceAmbientAndWake(nowMs);
  serviceRescueTouch(nowMs);
  serviceNetworkJobs(nowMs);
  serviceMenuWifiScan(nowMs);
  serviceTimeSync(nowMs);
  serviceCurrentPage(nowMs);
}

bool pushImageToTft(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= tft.height()) {
    return 0;
  }
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void initJpegDecoder() {
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(pushImageToTft);
}
