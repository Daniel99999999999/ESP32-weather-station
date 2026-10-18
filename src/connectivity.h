// SPDX-FileCopyrightText: 2023 ThingPulse Ltd., https://thingpulse.com
// SPDX-License-Identifier: MIT

#pragma once

#include <WiFi.h>
#include <ArduinoOTA.h>

#include "app_config.h"
#include "settings.h"

extern volatile bool g_otaInProgress;
extern volatile uint8_t g_otaProgressPercent;
extern bool g_wifiFallbackDhcp;
extern bool g_wifiUsingStaticIp;
extern uint8_t g_wifiProfileIndex;

struct RuntimeWiFiConfig {
  bool valid;
  bool useStaticIp;
  char ssid[33];
  char password[64];
  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
};

extern RuntimeWiFiConfig g_runtimeWiFiConfig;

inline bool isRuntimeWiFiConfigUsable() {
  return g_runtimeWiFiConfig.valid && g_runtimeWiFiConfig.ssid[0] != '\0';
}

inline void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);

  ArduinoOTA.onStart([]() {
    g_otaInProgress = true;
    g_otaProgressPercent = 0;
    FW_LOG_I("OTA start");
  });

  ArduinoOTA.onEnd([]() {
    g_otaProgressPercent = 100;
    g_otaInProgress = false;
    FW_LOG_I("OTA end");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const unsigned int percent = (total == 0U) ? 0U : ((progress * 100U) / total);
    g_otaProgressPercent = (uint8_t)percent;
  });

  ArduinoOTA.onError([](ota_error_t error) {
    g_otaInProgress = false;
    log_e("OTA error[%u]", error);
  });

  ArduinoOTA.begin();
  FW_LOG_I("OTA ready on %s (%s)", HOSTNAME, WiFi.localIP().toString().c_str());
}

static bool waitForWiFiConnect(uint32_t timeoutMs) {
  uint32_t t0 = millis();

  while ((millis() - t0) < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(500);
    yield();
  }
  return false;
}

// Sam status WL_CONNECTED potwierdza tylko połączenie z AP. Test DNS wykrywa
// m.in. statyczny adres i bramę należące do nieaktualnej podsieci.
static bool hasWorkingNetworkRoute() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  IPAddress probeIp;
  return WiFi.hostByName("api.openweathermap.org", probeIp) == 1;
}

static bool connectUsingConfig(const char* ssid,
                               const char* password,
                               bool useStaticIp,
                               const IPAddress& localIp,
                               const IPAddress& gateway,
                               const IPAddress& subnet,
                               const IPAddress& dns,
                               uint8_t profileIndex) {
  bool connected_static = false;
  bool connected_dhcp = false;

  g_wifiFallbackDhcp = false;
  g_wifiUsingStaticIp = false;
  g_wifiProfileIndex = 0xFF;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  if (useStaticIp) {
    FW_LOG_I("[WIFI] trying static SSID=%s", ssid);

    WiFi.setHostname(HOSTNAME);

    if (!WiFi.config(localIp, gateway, subnet, dns)) {
      log_w("[WIFI] static config failed");
    }

    WiFi.begin(ssid, password);
    connected_static = waitForWiFiConnect(WIFI_CONNECT_TIMEOUT_MS);

    if (connected_static && hasWorkingNetworkRoute()) {
      g_wifiUsingStaticIp = true;
      g_wifiFallbackDhcp = false;
      g_wifiProfileIndex = profileIndex;
      FW_LOG_I("[WIFI] connected static IP=%s", WiFi.localIP().toString().c_str());
      return true;
    }

#if WIFI_ENABLE_DHCP_FALLBACK
    if (connected_static) {
      log_w("[WIFI] static IP has no working route/DNS, fallback DHCP");
    } else {
      log_w("[WIFI] static connect timeout, fallback DHCP");
    }

    WiFi.disconnect(true, true);
    delay(300);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

    WiFi.begin(ssid, password);
    connected_dhcp = waitForWiFiConnect(WIFI_DHCP_FALLBACK_TIMEOUT_MS);

    if (connected_dhcp) {
      g_wifiUsingStaticIp = false;
      g_wifiFallbackDhcp = true;
      g_wifiProfileIndex = profileIndex;
      FW_LOG_I("[WIFI] connected DHCP IP=%s", WiFi.localIP().toString().c_str());
      return true;
    }

    log_w("[WIFI] DHCP fallback failed");
#else
    log_w("[WIFI] static connect timeout");
#endif
    return false;
  }

  FW_LOG_I("[WIFI] trying DHCP SSID=%s", ssid);

  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid, password);
  connected_dhcp = waitForWiFiConnect(WIFI_CONNECT_TIMEOUT_MS);

  if (connected_dhcp) {
    g_wifiUsingStaticIp = false;
    g_wifiFallbackDhcp = false;
    g_wifiProfileIndex = profileIndex;
    FW_LOG_I("[WIFI] connected DHCP IP=%s", WiFi.localIP().toString().c_str());
    return true;
  }

  log_w("[WIFI] connect failed for SSID=%s", ssid);
  return false;
}



static bool connectUsingProfile(const WiFiProfile& profile, uint8_t profileIndex) {
  return connectUsingConfig(profile.ssid,
                            profile.password,
                            profile.useStaticIp,
                            profile.localIp,
                            profile.gateway,
                            profile.subnet,
                            profile.dns,
                            profileIndex);
}

inline bool connectUsingRuntimeConfig() {
  if (!isRuntimeWiFiConfigUsable()) {
    return false;
  }
  return connectUsingConfig(g_runtimeWiFiConfig.ssid,
                            g_runtimeWiFiConfig.password,
                            g_runtimeWiFiConfig.useStaticIp,
                            g_runtimeWiFiConfig.localIp,
                            g_runtimeWiFiConfig.gateway,
                            g_runtimeWiFiConfig.subnet,
                            g_runtimeWiFiConfig.dns,
                            0xFF);
}

// inline void startWiFi() {
//   WiFi.mode(WIFI_STA);

// #if WIFI_USE_STATIC_IP
//   if (!WiFi.config(WIFI_LOCAL_IP, WIFI_GATEWAY, WIFI_SUBNET, WIFI_DNS)) {
//     log_e("Failed to apply static IP configuration.");
//   }
// #endif

//   WiFi.setHostname(HOSTNAME);
//   WiFi.begin(SSID, WIFI_PWD);
//   log_i("Connecting to WiFi '%s'...", SSID);
//   while (WiFi.status() != WL_CONNECTED) {
//     log_i(".");
//     delay(200);
//   }

//   log_i("WiFi connected. IP: %s, RSSI: %d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
//   setupOTA();
// }

/*
void startWiFi() {
  FW_LOG_I("[WIFI] start");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  bool connected = false;

#if WIFI_USE_STATIC_IP
  log_i("[WIFI] trying static IP: %s", WIFI_LOCAL_IP.toString().c_str());

  if (!WiFi.config(WIFI_LOCAL_IP, WIFI_GATEWAY, WIFI_SUBNET, WIFI_DNS)) {
    log_w("[WIFI] config static failed");
  }

  WiFi.begin(SSID, WIFI_PWD);
  connected = waitForWiFiConnect(8000);

  if (!connected) {
    log_w("[WIFI] static failed → fallback DHCP");

    WiFi.disconnect(true, true);
    delay(300);

    WiFi.mode(WIFI_STA);

    // KLUCZOWE — reset config → DHCP
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

    WiFi.begin(SSID, WIFI_PWD);
    connected = waitForWiFiConnect(8000);
  }
#else
  log_i("[WIFI] DHCP only");
  WiFi.begin(SSID, WIFI_PWD);
  connected = waitForWiFiConnect(8000);
#endif

  if (connected) {
    log_i("[WIFI] connected IP=%s RSSI=%d",
          WiFi.localIP().toString().c_str(),
          WiFi.RSSI());
  } else {
    log_e("[WIFI] connect FAILED");
  }
}
*/

inline void startWiFi() {
  log_i("[WIFI] start");

  g_wifiFallbackDhcp = false;
  g_wifiUsingStaticIp = false;
  g_wifiProfileIndex = 0xFF;

  if (connectUsingRuntimeConfig()) {
    FW_LOG_I("[WIFI] connected using runtime config ssid=%s ip=%s rssi=%d",
             g_runtimeWiFiConfig.ssid,
             WiFi.localIP().toString().c_str(),
             WiFi.RSSI());
    return;
  }

  for (uint8_t i = 0; i < WIFI_PROFILE_COUNT; i++) {
    if (connectUsingProfile(WIFI_PROFILES[i], i)) {
      FW_LOG_I("[WIFI] connected using profile=%u ssid=%s ip=%s rssi=%d",
            i,
            WIFI_PROFILES[i].ssid,
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI());
      return;
    }
  }

  log_e("[WIFI] all profiles failed");
}
