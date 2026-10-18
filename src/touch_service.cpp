#include "touch_service.h"
#include "ambient.h"
#include "app_config.h"

// Touch service realizuje prosty model:
// pierwszy dotyk przy zgaszonym ekranie tylko wybudza LCD,
// drugi w krótkim oknie czasu może już wykonać akcję UI.
static bool s_wakeWindowActive = false;
static uint32_t s_lastWakeTouchMs = 0;

void touchServiceInit(void) {
  s_wakeWindowActive = false;
  s_lastWakeTouchMs = 0;
}

bool touchServiceAllowPageSwitch(uint8_t currentLcdPwm, uint32_t nowMs) {
  // jeśli LCD nie jest bardzo przyciemnione, działamy normalnie
  if (currentLcdPwm > LCD_BRIGHT_NIGHT) {
    s_wakeWindowActive = false;
    return true;
  }

  // pierwszy dotyk tylko wybudza do 50%
  if (!s_wakeWindowActive || (nowMs - s_lastWakeTouchMs) > 5000U) {
    setLcdBrightness(LCD_BRIGHT_ONE_TOUCH); // ~50%
    s_wakeWindowActive = true;
    s_lastWakeTouchMs = nowMs;
    return false;
  }

  // drugi dotyk w oknie 5 s
  s_lastWakeTouchMs = nowMs;
  return true;
}

void touchServiceTimeout(uint32_t nowMs, uint8_t ambientTargetLcdPwm) {
  if (!s_wakeWindowActive) return;

  if ((nowMs - s_lastWakeTouchMs) > 5000U) {
      FW_LOG_I("[UI] wake timeout -> dim");
    s_wakeWindowActive = false;
    setLcdBrightness(ambientTargetLcdPwm);
  }
}

bool touchServiceWakeWindowActive(void) {
  return s_wakeWindowActive;
}
