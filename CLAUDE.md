# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP32 / M5Stack PlatformIO firmware demonstrating [SafeGithubOTA](https://github.com/gibz104/SafeGithubOTA) with Improv-WiFi BLE provisioning and GitHub-release-driven OTA. Targets the `pioarduino` fork of platform-espressif32 (pinned to `55.03.38` in `[env]`).

## Common commands

```sh
pio run -e <env>                    # build single env
pio run -e <env> -t upload          # build + flash
pio device monitor                  # serial console (115200)
pio run -t bump_version             # dry-run patch tag bump
BUMP_EXECUTE=1 pio run -t bump_version  # tag + push (triggers release.yml)
```

Default env (when `-e` omitted): `m5stack-nanoc6` (set via `default_envs` in `[platformio]`). The full active env list and the CI subset live in `README.md` "Supported boards"; the canonical CI list is `[ci] envs` in `platformio.ini` — both `build-firmware.yml` and `release.yml` parse that section via `configparser`.

No test suite. Validation is on-device only via the `validateFirmware()` callback in [src/validatefirmware.cpp](src/validatefirmware.cpp), which the SafeGithubOTA library invokes on first boot after an OTA — returning `false` triggers bootloader rollback.

## Architecture

### Runtime flow ([src/main.cpp](src/main.cpp))

`setup()`: `wifiSetup()` → `otaSetup(validateFirmware)`. `loop()`: `wifiLoop()` + `otaLoop()` every ~10 ms. Loop task stack bumped to 16 KB (`SET_LOOP_TASK_STACK_SIZE`) — TLS handshakes overflow the default 8 KB.

WiFi path ([src/wifisupport.cpp](src/wifisupport.cpp)):
1. Load creds from NVS via `loadWiFiCredentials()` → `WiFi.begin(ssid, pw)`.
2. If absent, fall through to `seedWifiStationCredsIfEmpty()` — only seeds when build-time `WIFI_SSID` macro is defined (opt-in via `[wifi-credentials]`, off by default).
3. After `TIME_TO_CONNECT` (30 s) with no association, start `ImprovWiFiBLE` advertising. On successful provisioning, `onImprovWiFiConnectedCb` persists creds via `saveWiFiCredentials()`.
4. On `WL_CONNECTED` transition, `updateEspHostedSlave()` may flash an ESP-Hosted co-processor and trigger `ESP.restart()` — only relevant on boards with `BOARD_HAS_SDIO_ESP_HOSTED`.

OTA path: SafeGithubOTA polls `api.github.com/.../releases/latest` for an asset whose filename matches `SGO_DEFAULT_BIN`. Matching asset → download → flash inactive partition → reboot → `validateFirmware()` → commit-or-rollback. Asset naming convention and the full flow are in [OTA.md](OTA.md).

### Build-time macro injection ([scripts/inject_build_info.py](scripts/inject_build_info.py))

PlatformIO pre-script. Appends `-D` macros to `CPPDEFINES`:
- Identity: `BUILD_SHA`, `BUILD_DATE`, `BUILD_TAG`, `BUILD_REPO`, `BUILD_FIRMWARE_URI` — all best-effort, guarded by `#ifdef` in `main.cpp`.
- SafeGithubOTA defaults: `SGO_DEFAULT_OWNER` / `SGO_DEFAULT_REPO` derived from `git remote`; `SGO_DEFAULT_BIN` from `firmware_naming.ota_bin_filename`. These seed the `sgo_creds` NVS namespace on every boot via `seedSgoDefaults()` in `main.cpp`; existing NVS keys are preserved.
- `BUILD_TAG` flows into `ota.setVersion()` so the device compares against the GitHub release tag.

Dev builds (no `GITHUB_REF_NAME`) only define `BUILD_SHA` / `BUILD_DATE`; the rest stay undefined and `#ifdef` blocks compile out.

### platformio.ini composition

Strongly hierarchical `extends =` chains. Conventions:
- MCU bases: `[esp32s3]`, `[esp32c6]`, `[esp32p4]`, `[esp32h2]` set `board_build.mcu`, USB/CDC defaults.
- Board bases: `[m5stack-<name>]` set `board =` + board-specific `-D` flags; do not list a target env directly here.
- Feature mixins: `[m5gfx]`, `[m5unified]`, `[fastled]`, `[neopixel]`, `[module-llm]`, `[improv]`, `[ghota]`.
- `[build-target]` → `[debug]` or `[release]`: aggregates `${improv.build_flags} ${ghota.build_flags} ${wifi-credentials.build_flags}` plus `CORE_DEBUG_LEVEL`. **Any `[env:*]` that overrides `build_flags = ...` instead of inheriting via `${build-target.build_flags}` must also explicitly include `${improv.build_flags}`** — otherwise `ImprovWiFiBLE` compiles out and `wifisupport.cpp` fails to build (this bit `esp32p4_waveshare_devkit`).
- `lib_ldf_mode = chain` is set globally (`deep` is unreliable). Libs must declare their own transitive dependencies; if a build fails with missing headers, check that the relevant library's `library.json` lists all deps.

### CI workflows

[.github/workflows/build-firmware.yml](.github/workflows/build-firmware.yml): manual `workflow_dispatch`, matrix from `[ci] envs` (or override via JSON input). Uploads merged + OTA bins as artifacts.

[.github/workflows/release.yml](.github/workflows/release.yml): fires on `v*` tag push, builds the same `[ci]` matrix, attaches `<proj>_<env>_firmware_<ver>.bin` (full flash image) and `<proj>_<env>_ota.bin` (app-only) to the GitHub Release. Asset filenames are computed by [scripts/firmware_naming.py](scripts/firmware_naming.py); the `_ota.bin` name is what devices match against via `SGO_DEFAULT_BIN`.

[scripts/bump_version.py](scripts/bump_version.py): exposes `pio run -t bump_version`. Dry-run by default, requires `BUMP_EXECUTE=1` to actually tag/push. See [VERSIONBUMP.md](VERSIONBUMP.md).

[scripts/generate_merged_firmware.py](scripts/generate_merged_firmware.py): post-script. Stitches bootloader + partitions + app into the single `_firmware_*.bin` flashable via WebSerial ESPTool, and emits the `_ota.bin` app-only image.

## Gotchas

- `[env:*]` sections that override `build_flags` lose the implicit `${improv.build_flags}` and `${ghota.build_flags}` they would have gotten from `[build-target]`. Re-add them explicitly or extend `build-target`.
- `[ci] envs` is whitespace-separated (newline OK) and parsed by Python `configparser` — keep the indentation consistent or the workflow setup job fails silently.
- OTA NVS namespace (`sgo_creds`) is seeded but not overwritten; if you change `SGO_DEFAULT_OWNER`/`REPO`/`BIN` and a device already has values stored, it keeps the old ones. Force a re-seed by clearing that NVS namespace.
- `m5stack-tab5` carries `-DBOARD_SDMMC_POWER_CHANNEL=4` as a temp workaround — leave in until upstream M5Unified fixes it.
- Compile-time WiFi credentials (`[wifi-credentials]`) are commented out by default and intentionally not inherited by any env — opt in per env if you really want them baked in. Improv-WiFi BLE is the intended path.

## Pointers

- [OTA.md](OTA.md) — full OTA flow, asset naming rules, every build-time macro.
- [VERSIONBUMP.md](VERSIONBUMP.md) — release tagging workflow.
- [README.md](README.md) — user-facing flashing and provisioning instructions, current supported-board list.
