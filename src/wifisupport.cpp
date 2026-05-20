#include <Arduino.h>
#include <WiFi.h>
#include <ImprovWiFiBLE.h>
#include <esp_wifi.h>
#include <cstring>
#include "wifisupport.hpp"
#include "sdkconfig.h"
#include "credstore.hpp"


// start Improv-WiFi BLE provisioning service after 10 sec w/o wifi connect
#define TIME_TO_CONNECT 10*1000

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
ImprovWiFiBLE improvBLE;
static wl_status_t wifiStatus = WL_NO_SHIELD; // status tracking

void onImprovWiFiErrorCb(ImprovTypes::Error err) {
    log_e("improv error %d", err);
}

void onImprovWiFiConnectedCb(const char *ssid, const char *password) {
    // commit ssid and password here
    log_i("wifi connected %s %s", ssid, password);
    saveWiFiCredentials( ssid, password);
}

void onImprovWiFiIdentifyCb() {
    // Visible/audible identification of this device.
    log_w("identify received");
}

void getMacAddress(String &macStr) {
    uint8_t mac[6];
    Network.macAddress(mac);
    macStr = String(mac[0], HEX) + String(mac[1], HEX) + String(mac[2], HEX) + String(mac[3], HEX) + String(mac[4], HEX) + String(mac[5], HEX);
    macStr.toUpperCase();
}

void startImprovProvisioning() {
    improvBLE.onImprovError(onImprovWiFiErrorCb);
    improvBLE.onImprovConnected(onImprovWiFiConnectedCb);
    improvBLE.onImprovIdentify(onImprovWiFiIdentifyCb);  // Optional
    String mac;
    getMacAddress(mac);
    String devId = "PGH_" + mac;
    // starts the advertisement + provisioning process
    improvBLE.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "My-Device-9a4c2b", "2.1.5", devId.c_str());
}

void wifiSetup() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    String savedSSID, savedPASS;
    if (loadWiFiCredentials(savedSSID, savedPASS)) {
        log_w("loaded creds: %s %s", savedSSID.c_str(), savedPASS.c_str());
        WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    } else {
        if (seedWifiStationCredsIfEmpty(false)) {
            return;
        }
    }
    log_i("No Wi-Fi credentials found in Preferences.");
    WiFi.begin();
}


void wifiLoop() {
    static wl_status_t wifiStatus = WL_NO_SHIELD;
    wl_status_t s = WiFi.status();
    if (wifiStatus ^ s) {
        log_i("WiFi status change %u -> %u", wifiStatus, s);
        wifiStatus = s;
    }
    if (wifiStatus != WL_CONNECTED && millis() > TIME_TO_CONNECT) {
        if (!improvBLE.isAdvertising()) {
            startImprovProvisioning();
        }
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
