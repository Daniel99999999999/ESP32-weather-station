// SPDX-FileCopyrightText: 2023 ThingPulse Ltd., https://thingpulse.com
// SPDX-License-Identifier: MIT

#pragma once

#include <gt911.h>
#include <TFT_eSPI.h>

#include "app_config.h"
#include "settings.h"

// The library defines the type "setup_t" as a struct
// Calling tft.getSetup(user) populates it with the settings
setup_t user;

void initTft(TFT_eSPI *tft) {
  tft->init();
  tft->setRotation(TFT_ROTATION);
  tft->setSwapBytes(true);

  tft->fillScreen(TFT_BLACK);
}

void initTouchScreen(TAMC_GT911 *ts) {
  ts->begin(GT911_ADDR1);
  ts->setRotation(TOUCH_ROTATION);

  FW_LOG_I("GT911 touch initialized");
}

void logDisplayDebugInfo(TFT_eSPI *tft) {
  tft->getSetup(user);

  FW_LOG_I("TFT info");
  FW_LOG_I("===========================");
  FW_LOG_I("TFT_eSPI version:  %s", user.version);
  FW_LOG_I("Transactions:      %s", (user.trans  ==  1) ? "yes" : "no");
  FW_LOG_I("Display driver:    0x%04x", user.tft_driver);
  FW_LOG_I("Display width:     %d (at rotation 0)", user.tft_width);
  FW_LOG_I("Display height:    %d (at rotation 0)", user.tft_height);
  FW_LOG_I("Fonts loaded:");
  uint16_t fonts = tft->fontsLoaded();
  if (fonts & (1 << 1)) { FW_LOG_I("- font GLCD        loaded"); }
  if (fonts & (1 << 2)) { FW_LOG_I("- font 2           loaded"); }
  if (fonts & (1 << 4)) { FW_LOG_I("- font 4           loaded"); }
  if (fonts & (1 << 6)) { FW_LOG_I("- font 6           loaded"); }
  if (fonts & (1 << 7)) { FW_LOG_I("- font 7           loaded"); }
  if (fonts & (1 << 9)) {
    FW_LOG_I("- font 8N          loaded");
  } else if (fonts & (1 << 8)) {
    FW_LOG_I("- font 8           loaded");
  }
  if (fonts & (1 << 15)) { FW_LOG_I("Smooth font:       enabled"); }
  if (user.serial == 1) { FW_LOG_I("SPI frequency:     %.2f", user.tft_spi_freq / 10.0); }
  FW_LOG_I("");
  FW_LOG_I("Touch screen info");
  FW_LOG_I("===========================");
  FW_LOG_I("Touch controller:  GT911");
  FW_LOG_I("Touch resolution:  %dx%d", TFT_WIDTH, TFT_HEIGHT);
}
