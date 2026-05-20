#include <Arduino.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <esp_wifi.h>
#include <cstring>
#include "wifisupport.hpp"
#include "sdkconfig.h"

// #if CONFIG_ESP_WIFI_REMOTE_ENABLED
//     #error "WiFiProv is only supported in SoCs with native Wi-Fi support"
// #endif

#ifdef WIFI_SSID
    #define INITIAL_SSID WIFI_SSID
#else
    #define INITIAL_SSID ""
#endif
#ifdef WIFI_PASSWORD
    #define INITIAL_PASSWORD WIFI_PASSWORD
#else
    #define INITIAL_PASSWORD ""
#endif
static const char* wifiSSID = INITIAL_SSID;
static const char* wifiPassword = INITIAL_PASSWORD;
#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)
    static bool wifiProvAvailable = false;
#else
    static bool wifiProvAvailable = true;
#endif

void wifiSetup() {
    // BLE-based WiFi provisioning (esp_wifi_prov). Only start if NVS has
    // no usable STA creds — build-time WIFI_SSID seeding (above) and prior
    // BLE provisioning both populate the same slot.
    bool haveCreds = hasSavedWifiStationCredentials();
    logSavedWifiStationCredentials();

    if (!seedWifiStationCredsIfEmpty(haveCreds)) {
        WiFi.begin();  // use existing NVS creds (prior boot or BLE prov)
    }

    if (!haveCreds) {
        log_i("No WiFi station creds in NVS");
        if (wifiProvAvailable) {
            log_i("starting WiFiProv provisioning");
            WiFiProv.beginProvision(NETWORK_PROV_SCHEME_BLE);
        }
    }
}

// Retrieve currently-saved STA credentials via the supported ESP-IDF API.
// Driver must be initialised first (WiFi.mode(WIFI_STA) or WiFi.begin()).
// ssid/password in wifi_config_t are fixed-size null-padded buffers; cap
// with strnlen so a non-terminated 32/64-byte blob doesn't run past the end.
bool getSavedWifiStationCredentials(String& ssid, String& password) {
    wifi_mode_t cm = WiFi.getMode();
    WiFi.mode(WIFI_STA);

    wifi_config_t cfg{};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        if (cm != WIFI_MODE_NULL) WiFi.mode(cm);
        return false;
    }
    const char* s = reinterpret_cast<const char*>(cfg.sta.ssid);
    const char* p = reinterpret_cast<const char*>(cfg.sta.password);
    ssid = String(s, strnlen(s, sizeof(cfg.sta.ssid)));
    password = String(p, strnlen(p, sizeof(cfg.sta.password)));
    if (cm != WIFI_MODE_NULL) WiFi.mode(cm);
    return ssid.length() > 0;
}

bool hasSavedWifiStationCredentials() {
    String ssid, password;
    return getSavedWifiStationCredentials(ssid, password);
}

void logSavedWifiStationCredentials() {
    String ssid, password;
    if (getSavedWifiStationCredentials(ssid, password)) {
        log_i("esp_wifi_get_config SSID: %s", ssid.c_str());
        if (password.length() > 0) {
            log_i("esp_wifi_get_config Password: [%u chars]", password.length());
        } else {
            log_i("esp_wifi_get_config Password: [empty / open]");
        }
    } else {
        log_i("esp_wifi_get_config: driver not inited or no creds");
    }
}

// Seed WiFi STA creds into NVS from build-time WIFI_SSID/WIFI_PASSWORD
// macros, only if NVS slot is empty. Returns true if WiFi.begin(ssid,pw)
// was issued (caller should skip the bare WiFi.begin()).
// NOTE: leaves WiFi.persistent(true) and WIFI_STA mode set on the driver.
bool seedWifiStationCredsIfEmpty(bool credsAlreadyPresent) {
    if (!*wifiSSID)
        return false;
    if (credsAlreadyPresent) {
        log_i("WiFi Station credentials set - not seeding");
        return false;
    }
    log_i("Seeding WiFi NVS with build-time SSID=%s", wifiSSID);
    WiFi.persistent(true);
    WiFi.begin(wifiSSID, wifiPassword);
    return true;
}
