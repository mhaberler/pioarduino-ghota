#include <SafeGithubOTA.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <lwip/inet.h>
#include "otasupport.hpp"
#include "Pinger.hpp"
#include "wifisupport.hpp"


#ifndef SGO_DEFAULT_OWNER
    #define SGO_DEFAULT_OWNER ""
#endif
#ifndef SGO_DEFAULT_REPO
    #define SGO_DEFAULT_REPO ""
#endif
#ifndef SGO_DEFAULT_PAT
    #define SGO_DEFAULT_PAT ""
#endif
#ifndef SGO_DEFAULT_BIN
    #define SGO_DEFAULT_BIN ""
#endif

// ---- weak can be overriden, see main.cpp ----
#ifdef BUILD_TAG
    __attribute__((weak))  const char* version = BUILD_TAG;
#else
    __attribute__((weak))  const char* version = "1.0.0";
#endif
#ifdef PROV_SSID
    __attribute__((weak)) const char* provSsid = PROV_SSID;
#else
    __attribute__((weak))  const char* provSsid = "MyDevice-Setup";
#endif
#ifdef AUTOCHECK_INTERVAL
    uint32_t autoCheckInterval = AUTOCHECK_INTERVAL;
#else
    uint32_t autoCheckInterval;
#endif
#ifdef AUTOCHECK_POST_BOOT
    bool autoCheckPostBoot = true;
#else
    bool autoCheckPostBoot;
#endif

#ifndef PING_TARGET
    #define PING_TARGET "8.8.8.8"
#endif

bool postBootCheckTriggered = false;
volatile bool internetOnline = false;

Pinger gPinger(PING_TARGET);  // default to google nameserver
SafeGithubOTA ota;

void seedSgoDefaults() {
    struct Kv {
        const char* key;
        const char* def;
    };
    const Kv kvs[] = {
        {"owner", SGO_DEFAULT_OWNER},
        {"repo", SGO_DEFAULT_REPO},
        {"pat", SGO_DEFAULT_PAT},
        {"bin", SGO_DEFAULT_BIN},
    };
    Preferences p;
    if (!p.begin("sgo_creds", false)) {
        log_d("sgo_creds: NVS open failed");
        return;
    }
    for (const auto& kv : kvs) {
        String v = p.getString(kv.key, "");
        if (strcmp(kv.key, "pat") == 0)
            log_d("sgo_creds[%s] before: len=%u", kv.key, v.length());
        else
            log_d("sgo_creds[%s] before: \"%s\"", kv.key, v.c_str());
    }
    for (const auto& kv : kvs) {
        if (kv.def[0] == '\0')
            continue;
        if (p.isKey(kv.key))
            continue;
        p.putString(kv.key, kv.def);
    }
    for (const auto& kv : kvs) {
        String v = p.getString(kv.key, "");
        if (strcmp(kv.key, "pat") == 0)
            log_d("sgo_creds[%s] after:  len=%u", kv.key, v.length());
        else
            log_d("sgo_creds[%s] after:  \"%s\"", kv.key, v.c_str());
    }
    p.end();
}

void otaLog(const char *message) {
    log_i("%s", message);
}

static void setupOtaEventHandler() {
    Network.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_READY:
                log_i("WiFi Ready");
                break;

            case ARDUINO_EVENT_WIFI_SCAN_DONE:
                log_i("WiFi scan done");
                break;

            case ARDUINO_EVENT_WIFI_STA_START:
                log_i("Station Mode Started");
                break;

            case ARDUINO_EVENT_WIFI_STA_STOP:
                log_i("Station Mode Stopped");
                break;

            case ARDUINO_EVENT_WIFI_STA_CONNECTED:
                log_i(
                    "Connected to AP. SSID: %s | BSSID: "
                    "%02X:%02X:%02X:%02X:%02X:%02X",
                    (char*)info.wifi_sta_connected.ssid,
                    info.wifi_sta_connected.bssid[0],
                    info.wifi_sta_connected.bssid[1],
                    info.wifi_sta_connected.bssid[2],
                    info.wifi_sta_connected.bssid[3],
                    info.wifi_sta_connected.bssid[4],
                    info.wifi_sta_connected.bssid[5]);
                break;

            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                log_w(
                    "STA Disconnected. Reason: %u %s",
                    info.wifi_sta_disconnected.reason,
                    WiFi.STA.disconnectReasonName(
                        (wifi_err_reason_t)info.wifi_sta_disconnected.reason));
                break;

            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                log_i(
                    "STA Got IP: %s | GW: %s",
                    IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
                    IPAddress(info.got_ip.ip_info.gw.addr).toString().c_str());

                log_i("run ping online test");
                gPinger.onResult([](uint32_t tx, uint32_t rx,
                uint32_t loss, uint32_t dur) {
                    internetOnline = (rx > 0);
                    log_i("ping %lu/%lu rx, %lu%% loss, %lu ms -> online=%d",
                          (unsigned long)rx, (unsigned long)tx,
                          (unsigned long)loss, (unsigned long)dur,
                          (int)internetOnline);
                });
                gPinger.start();
                postBootCheckTriggered = true;  // force an immediate check
                break;

            case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
                log_i("STA Got IPv6 : %s ",
                      inet6_ntoa(*(struct in6_addr*)info.got_ip6.ip6_info.ip.addr));
                break;

            case ARDUINO_EVENT_WIFI_AP_START:
                log_i("AP start");
                break;

            case ARDUINO_EVENT_WIFI_AP_STOP:
                log_i("AP stop");
                break;

            case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
                log_i("Client Joined AP. MAC: %02X:%02X:%02X:%02X:%02X:%02X | AID: %u",
                      info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                      info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                      info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5],
                      info.wifi_ap_staconnected.aid);
                break;

            case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
                log_i("Client left AP. MAC: %02X:%02X:%02X:%02X:%02X:%02X | AID: %u | reason %u",
                      info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
                      info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
                      info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5],
                      info.wifi_ap_stadisconnected.aid,
                      info.wifi_ap_stadisconnected.reason);
                break;

            case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
                log_i("WiFi AP Station IP %s assigned to MAC %02X:%02X:%02X:%02X:%02X:%02X",
                      IPAddress(  info.wifi_ap_staipassigned.ip.addr).toString().c_str(),
                      info.wifi_ap_staipassigned.mac[0], info.wifi_ap_staipassigned.mac[1],
                      info.wifi_ap_staipassigned.mac[2], info.wifi_ap_staipassigned.mac[3],
                      info.wifi_ap_staipassigned.mac[4], info.wifi_ap_staipassigned.mac[5]);
                break;

            case ARDUINO_EVENT_PROV_INIT:
                log_i("Provisioning Init");
                break;

            case ARDUINO_EVENT_PROV_DEINIT:
                log_i("Provisioning Deinit");
                break;

            case ARDUINO_EVENT_PROV_START:
                log_i("Provisioning Start");
                break;

            case ARDUINO_EVENT_PROV_END:
                log_i("Provisioning Ends, rebooting");
                ESP.restart();
                break;

            case ARDUINO_EVENT_PROV_CRED_RECV:
                log_i("Provisioning credentials received");
                break;

            case ARDUINO_EVENT_PROV_CRED_SUCCESS:
                log_i("Provisioning Successful");
                break;

            default:
                log_e("Unhandled WiFi Event: %d", event);
                break;
        }
    });
}

static void logConfig() {
#ifdef BUILD_REPO
    log_i("repo:  " BUILD_REPO);
#endif
#ifdef BUILD_SHA
    log_i("sha:   " BUILD_SHA);
#endif
#ifdef BUILD_TAG
    log_i("tag:   " BUILD_TAG);
#endif
#ifdef BUILD_DATE
    log_i("built: " BUILD_DATE);
#endif
#ifdef BUILD_FIRMWARE_URI
    log_i("uri:   " BUILD_FIRMWARE_URI);
#endif
}

void otaSetup(SGO_ValidationCallback validationCb) {
    logConfig();

    Network.begin();
    setupOtaEventHandler();

    // #endif
    ota.onLog(otaLog);

    // disable PAT for public repos
    ota.setPatRequired(false);

    ota.setVersion(version);
    log_i("=== SafeGithubOTA Auto-Check Example (%s) ===", version);

    // Set the validation callback for rollback protection.
    // After an OTA update, this runs on first boot. If it returns false,
    // the ESP32 bootloader automatically reverts to the previous firmware.
    if (validationCb) {
        ota.onValidation(validationCb);
    }

    // Set progress callback for download status
    ota.onProgress([](uint32_t written, uint32_t total) {
        if (total > 0) {
            uint32_t pct = (written * 100) / total;
            log_i("Download: %u%% (%u / %u bytes)", pct, written,
                  total);
        } else {
            log_i("Download: %u bytes", written);
        }
    });

    // Auto-check for release updates every AUTOCHECK_INTERVAL seconds
    ota.setAutoCheckInterval(autoCheckInterval);

    // Seed SGO credentials into NVS from build-time defaults (idempotent;
    // existing keys are preserved).
    seedSgoDefaults();
    if (!ota.isProvisioned()) {
        log_i("OTA not provisioned. Starting setup portal SSID=%s...",
              provSsid);
        ota.startProvisioningPortal(provSsid);
    }


    // Initialize OTA (must be called after WiFi is connected).
    // Syncs time via NTP, handles post-OTA validation, loads creds from NVS.
    SGO_Error err = ota.begin();
    if (err != SGO_Error::OK) {
        log_e("ota.begin() failed: %s", ota.getLastError());
    }

    // Check if we rolled back from a failed OTA update.
    // This checks OTA partition state and persists until the next OTA.
    if (ota.wasRolledBack()) {
        log_w(
            "WARNING: Previous OTA update failed validation and was rolled "
            "back!");
    }
    if (autoCheckPostBoot) {
        log_i("Auto-check post boot once.");
    }

    if (autoCheckInterval) {
        log_i("Auto-check every %d seconds running in the background.",
              autoCheckInterval);
    }
}

void otaLoop() {
    // IMPORTANT: Call ota.loop() to process the auto-check timer.
    // When a new version is found, it downloads, flashes, and reboots
    // automatically.
    ota.loop();

    if (autoCheckPostBoot && postBootCheckTriggered && internetOnline) {
        postBootCheckTriggered = false;
        log_i("running post boot checkAndUpdate");
        ota.checkAndUpdate();
    }
}
