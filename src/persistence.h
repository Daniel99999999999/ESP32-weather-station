// SPDX-FileCopyrightText: 2023 ThingPulse Ltd., https://thingpulse.com
// SPDX-License-Identifier: MIT

#pragma once

#include <LittleFS.h>
#include "app_config.h"

void listFiles();

void initFileSystem() {
  if (LittleFS.begin()) {
    FW_LOG_I("Flash FS available!");
  } else {
    log_e("Flash FS initialisation failed!");
  }

  listFiles();
}

void listFiles() {
  FW_LOG_I("Flash FS files found:");

  File root = LittleFS.open("/");
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    FW_LOG_I("- %s, %d bytes", entry.name(), entry.size());
    entry.close();
  }
}
