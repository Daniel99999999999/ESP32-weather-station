#ifndef LED_SERVICE_H
#define LED_SERVICE_H

#include <Arduino.h>
#include "app_types.h"

void applyRgbLedState(AppPage currentPage,
                      uint8_t activeCity,
                      RadiationStatus radStatus,
                      uint16_t ambientFiltered,
                      bool otaInProgress);

#endif