#include <Arduino.h>

#include "led_service.h"
#include "ambient.h"

uint16_t getCityUiColor(uint8_t cityIndex);

static uint8_t scale8(uint8_t x, uint8_t scale) {
  return (uint16_t)x * scale / 255;
}

static uint8_t rgb565To8R(uint16_t color) {
  return (uint8_t)((((color >> 11) & 0x1F) * 255U) / 31U);
}

static uint8_t rgb565To8G(uint16_t color) {
  return (uint8_t)((((color >> 5) & 0x3F) * 255U) / 63U);
}

static uint8_t rgb565To8B(uint16_t color) {
  return (uint8_t)(((color & 0x1F) * 255U) / 31U);
}

void applyRgbLedState(AppPage currentPage,
                      uint8_t activeCity,
                      RadiationStatus radStatus,
                      uint16_t ambientFiltered,
                      bool otaInProgress) {
  uint8_t ledScale = getLedBrightnessForAmbient(ambientFiltered);

  if (otaInProgress) {
    setRgbCommonAnode(scale8(255, ledScale), 0, 0);
    return;
  }

  if (radStatus == RAD_STATUS_ALARM) {
    setRgbCommonAnode(scale8(255, ledScale), 0, 0);
    return;
  }

  switch (currentPage) {
    case PAGE_STATS:
      setRgbCommonAnode(0, scale8(255, ledScale), 0);
      return;

    case PAGE_DEBUG:
      setRgbCommonAnode(scale8(170, ledScale), 0, scale8(255, ledScale));
      return;

    case PAGE_MENU:
      setRgbCommonAnode(scale8(255, ledScale), scale8(96, ledScale), 0);
      return;

    case PAGE_OTA_SERVICE:
      setRgbCommonAnode(scale8(255, ledScale), 0, 0);
      return;

    case PAGE_MAIN:
    default: {
      const uint16_t cityColor = getCityUiColor(activeCity);
      setRgbCommonAnode(scale8(rgb565To8R(cityColor), ledScale),
                        scale8(rgb565To8G(cityColor), ledScale),
                        scale8(rgb565To8B(cityColor), ledScale));
      return;
    }
  }
}
