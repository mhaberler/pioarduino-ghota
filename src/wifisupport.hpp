#pragma once

#include <esp_wifi_types_generic.h>
#include <WString.h>

void wifiSetup();
void wifiLoop();

bool seedWifiStationCredsIfEmpty(bool credsAlreadyPresent);
bool hasSavedWifiStationCredentials();
bool getSavedWifiStationCredentials(String& ssid, String& password);
void logSavedWifiStationCredentials();
