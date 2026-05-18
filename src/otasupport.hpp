#pragma once

#include <SafeGithubOTA.h>
#include <esp_wifi_types_generic.h>

void seedSgoDefaults();
void otaSetup(SGO_ValidationCallback validationCb = nullptr);
void otaLoop();

extern bool postBootCheckTriggered;
extern SafeGithubOTA ota;