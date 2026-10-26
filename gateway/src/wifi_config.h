// The Wi-Fi network the gateway joins: secrets.h, or Wokwi's open network in the gateway_sim build.
#pragma once
#include "secrets.h"

#ifdef WIFI_SSID_OVERRIDE
constexpr const char* WIFI_NET_SSID = WIFI_SSID_OVERRIDE;
constexpr const char* WIFI_NET_PASS = "";
#else
constexpr const char* WIFI_NET_SSID = WIFI_SSID;
constexpr const char* WIFI_NET_PASS = WIFI_PASS;
#endif
