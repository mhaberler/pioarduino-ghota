// SafeGithubOTA - Auto-Check with BLE Provisioning
//
// Derived from: https://github.com/gibz104/SafeGithubOTA
//
// Demonstrates:
// - BLE provisioning via WiFiProv (NETWORK_PROV_SCHEME_BLE)
// - Automatic periodic update checks (AUTOCHECK_INTERVAL)
// - Optional one-shot post-boot check (AUTOCHECK_POST_BOOT)
// - Validation callback for rollback protection
// - Progress reporting during download (see otasupport.cpp)
// - Rollback detection via wasRolledBack()
//
// Required libraries: SafeGithubOTA, ESP32 Arduino core (Preferences, WiFiProv).
//
// After an OTA update, the validation callback verifies the new firmware
// before confirming it. If it returns false, the ESP32 bootloader
// reverts to the previous firmware.

#include "otasupport.hpp"

// Increase loop task stack for TLS operations (default 8KB is not enough)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

bool validateFirmware();  // see validatefirmware.cpp

void setup() {
    Serial.begin(115200);
    delay(3000);

    otaSetup(validateFirmware);  // or use just otaSetup(); without validation
    log_i("Setup complete.");
}

void loop() {
    otaLoop();

    // Your application code goes here
    delay(10);
}
