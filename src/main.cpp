// SafeGithubOTA - Auto-Check with Improv-WiFi BLE Provisioning
//
// Derived from: https://github.com/gibz104/SafeGithubOTA
//
// Demonstrates:
// - BLE WiFi provisioning via Improv-WiFi (ImprovWiFiBLE)
// - Automatic periodic update checks (AUTOCHECK_INTERVAL)
// - Optional one-shot post-boot check (AUTOCHECK_POST_BOOT)
// - Validation callback for rollback protection
// - Progress reporting during download (see otasupport.cpp)
// - Rollback detection via wasRolledBack()
//
// Required libraries: SafeGithubOTA, ESP32 Arduino core (Preferences), ImprovWiFiLibrary.
//
// After an OTA update, the validation callback verifies the new firmware
// before confirming it. If it returns false, the ESP32 bootloader
// reverts to the previous firmware.

#include "otasupport.hpp"
#include "wifisupport.hpp"

// Increase loop task stack for TLS operations (default 8KB is not enough)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

bool validateFirmware();  // see validatefirmware.cpp

void setup() {
    Serial.begin(115200);
    delay(3000);

    wifiSetup();

    otaSetup(validateFirmware);  // or use just otaSetup(); without validation
    log_i("Setup complete.");
}

void loop() {
    wifiLoop();
    otaLoop();

    // Your application code goes here
    delay(10);
}
