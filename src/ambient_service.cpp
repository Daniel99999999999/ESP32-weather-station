#include "ambient_service.h"
#include "ambient.h"

// Prosty filtr IIR wygładza odczyty LDR, żeby LCD i LED nie "pompowały"
// przy krótkich zmianach oświetlenia.
static bool s_initialized = false;
static uint16_t s_filtered = 0;

void ambientServiceInit(void) {
  s_initialized = false;
  s_filtered = 0;
}

void ambientServiceUpdate(uint16_t &ambientFiltered, bool &isNightMode, bool wakeWindowActive) {
  uint16_t raw = readAmbientRaw();

  if (!s_initialized) {
    s_filtered = raw;
    s_initialized = true;
  } else {
    s_filtered = (s_filtered * 7U + raw) / 8U;
  }

  ambientFiltered = s_filtered;
  isNightMode = (ambientFiltered > 500);

  uint8_t lcdTarget = getLcdBrightnessForAmbient(ambientFiltered);
  uint8_t ledTarget = getLedBrightnessForAmbient(ambientFiltered);

  // Ambient nie nadpisuje chwilowego wake boost po dotyku ekranu.
  if (!wakeWindowActive) {
    setLcdBrightness(lcdTarget);
  }

  setBlueLedBrightness(ledTarget);
}
