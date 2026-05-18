#include <WiFi.h>
#include "otasupport.hpp"

// Optional validation callback - runs on first boot after OTA update.
// Return true if the new firmware is working correctly.
// Return false to automatically rollback to the previous firmware.
bool validateFirmware() {
    log_i("Running post-OTA validation...");

    // Example checks you might perform:

    // 1. Verify WiFi is still connected (allow up to 10s to associate)
    const uint32_t wifiDeadline = millis() + 10000;
    while (WiFi.status() != WL_CONNECTED &&
            (int32_t)(wifiDeadline - millis()) > 0) {
        delay(200);
    }
    if (WiFi.status() != WL_CONNECTED) {
        log_i("FAIL: WiFi not connected");
        return false;
    }

    // 2. Verify a sensor is responding
    // if (!mySensor.begin()) {
    //     log_i("FAIL: Sensor not responding");
    //     return false;
    // }

    // 3. Verify critical data in NVS is intact
    // Preferences prefs;
    // prefs.begin("myapp", true);
    // String val = prefs.getString("key", "");
    // prefs.end();
    // if (val.length() == 0) {
    //     log_i("FAIL: NVS data missing");
    //     return false;
    // }

    log_i("Validation passed!");
    return true;
}
