# pioarduino-ghota

ESP32 / M5Stack PlatformIO firmware demonstrating [SafeGithubOTA] with
BLE WiFi provisioning and automated GitHub-release-driven OTA updates.

[SafeGithubOTA]: https://github.com/gibz104/SafeGithubOTA

## What it does

- BLE WiFi provisioning via `WiFiProv` (`NETWORK_PROV_SCHEME_BLE`) —
  the device advertises a setup SSID; use the Espressif
  "ESP BLE Provisioning" app to hand over credentials, which are
  persisted to NVS for subsequent boots.
- Automatic OTA from GitHub releases: one-shot post-boot check
  (`AUTOCHECK_POST_BOOT`) and/or periodic poll (`AUTOCHECK_INTERVAL`).
- Validation callback on first boot after an OTA; if it returns
  `false`, the ESP32 bootloader rolls back to the previous firmware.
- Reproducible CI builds and tagged releases via GitHub Actions.

## ESP BLE Prov mobile app

You will need the `ESP BLE Prov` provisioning app to set the WiFi credentials.

- [ESP BLE Prov for Android](https://play.google.com/store/apps/details?id=com.espressif.provble&hl=en)
- [ESP BLE Prov for iOS](https://apps.apple.com/us/app/esp-ble-provisioning/id1473590141)

Alternatively you can use WiFi to configure WiFi credentials - see the [SafeGithubOTA examples](https://github.com/gibz104/SafeGithubOTA/tree/main/examples) if your want to go that route.

## How to use this repo without building firmware yourself

1. go to [https://github.com/mhaberler/pioarduino-ghota](https://github.com/mhaberler/pioarduino-ghota/releases/)
2. select the latest release at the top
3. find a `_firmware_<version>.bin` file matching your hardware - this is the factory version to flash pristine hardware - the `_ota.bin` file is just for Over-the-Air updates.
4. and download it
5. run the [WebSerial ESPTool](https://jason2866.github.io/WebSerial_ESPTool/)
   1. select the erial port
   2. in `Choose a file ...` select the firmware\_<version>.bin` file just dowloaded
   3. Click `Program`
   4. Connect a terminal window - you should see some logs during startup
6. At this stage the firmware lacks WiFi credentials - they can be set with the `ESP BLE Prov` app (available for iOS and Android)
7. Once the credentials are set, firmware will reboot, connect to WiFi and check for new OTA releases as configure in the platformio.ini and src/main.cpp files.

## My hardware is not listed / I want to use this for my project

1. fork this repo
2. find a matching hardware configuration in [platformio.ini](platformio.ini) or create a new one.
3. Add your code.
4. Build and flash the firmware locally until confident. If WiFi credentials are set the firmware should check for OTA updateds but fail as the automatic build is not yet set up - that is the next steps:
5. Edit the `[ci]` section and replace with your target configuration.
6. commit and push
7. push a tag higher than the current version - use the Pioarduino Custom `Bump semver git tag` action.
8. The firmware for the new target should build on github and show up under releases
9. restart the hardware. An OTA update should happen and the new release come up.

## Supported boards

[platformio.ini](platformio.ini) declares envs for the full M5Stack
family — Basic, Fire, M5Go, Core2, CoreS3, StickC family, Atom and
AtomS3 family, CoreInk, Paper, PaperS3, StampS3, StampP4, Capsule,
Dial, Cardputer, DIN Meter, NanoC6, NanoH2, Tab5, and several
`m5unified` / LLM variants.

Only the envs listed in the `[ci]` section are built by CI, currently:

- `m5stack-nanoc6`
- `m5stack-cores3-m5unified`

Edit as required in [platformio.ini](platformio.ini).

Other envs build locally but are not gated by CI. See [build-firmware.yml](.github/workflows/build-firmware.yml) and [release.yml](.github/workflows/release.yml).

### Non-M5Stack Espressif targets

Any ESP32 / S2 / S3 / C3 / C6 / H2 board supported by the ESP32
Arduino core works. Add an `[env:my-board]` section to
[platformio.ini](platformio.ini) with the right `board =`, extend
`build-target`, and inherit the appropriate `lib_deps`. The M5Stack
envs are preconfigured only because the upstream boilerplate
(see Credits) ships them.

## Quick start

Prereqs: [PlatformIO Core](https://docs.platformio.org/en/latest/core/)
(this repo targets the `pioarduino` fork; standard PlatformIO works
for most envs too) with `pio` on PATH.

```sh
pio run -e m5stack-cores3-m5unified -t upload
pio device monitor
```

On first boot, BLE provisioning starts under the SSID `MyDevice-Setup`.
Use the Espressif "ESP BLE Provisioning" app to deliver WiFi
credentials; they persist in NVS for subsequent boots.

## Configuration

Build-time macros (injected by
[scripts/inject_build_info.py](scripts/inject_build_info.py) or set in
[platformio.ini](platformio.ini)):

| Macro                                                                          | Purpose                                     | Source                                                       |
| ------------------------------------------------------------------------------ | ------------------------------------------- | ------------------------------------------------------------ |
| `SGO_DEFAULT_OWNER` / `SGO_DEFAULT_REPO` / `SGO_DEFAULT_BIN`                   | OTA target repo + asset name                | [scripts/inject_build_info.py](scripts/inject_build_info.py) |
| `SGO_DEFAULT_PAT`                                                              | optional private-repo personal access token | env override                                                 |
| `AUTOCHECK_INTERVAL`                                                           | periodic OTA poll interval (seconds)        | `[ota-checking]` in [platformio.ini](platformio.ini)         |
| `AUTOCHECK_POST_BOOT`                                                          | one-shot OTA check after first `STA_GOT_IP` | `[ota-checking]`                                             |
| `BUILD_TAG` / `BUILD_SHA` / `BUILD_DATE` / `BUILD_REPO` / `BUILD_FIRMWARE_URI` | identity strings printed at boot            | [scripts/inject_build_info.py](scripts/inject_build_info.py) |

OTA defaults are seeded into the `sgo_creds` NVS namespace on every
boot; existing keys are preserved (see `seedSgoDefaults()` in
[src/main.cpp](src/main.cpp)).

## Repository layout

- [src/main.cpp](src/main.cpp) — firmware entry point
- [include/](include/) — public headers
- [scripts/](scripts/) — PlatformIO pre/post scripts (build-info
  injection, merged-firmware generation, version bump)
- [firmware/](firmware/) — built artifacts (`custom_firmware_dir`)
- [platformio.ini](platformio.ini) + [platformio-m5stack.ini](platformio-m5stack.ini) — environments
- [.github/workflows/](.github/workflows/) — CI builds + tagged releases
- [OTA.md](OTA.md) — full OTA flow and asset naming
- [VERSIONBUMP.md](VERSIONBUMP.md) — release tagging workflow
- [LICENCE.md](LICENCE.md) — license

## Building the firmware

### a) Locally with pioarduino

```sh
pio run -e <env>                 # build
pio run -e <env> -t upload       # build + flash
pio device monitor               # serial console
```

### b) GitHub Actions: "Build merged firmware"

Manual trigger from the repo's **Actions** tab → **Build merged
firmware** workflow → pick the branch → **Run workflow**. The matrix
builds each env listed under `[ci] envs` in
[platformio.ini](platformio.ini); merged firmware binaries are
attached to the workflow run as artifacts. See
[build-firmware.yml](.github/workflows/build-firmware.yml).

### c) Tagged release

Push a higher semver `v*` tag to fire
[release.yml](.github/workflows/release.yml), which builds the `[ci]`
matrix and uploads OTA assets that deployed firmware then discovers
via `api.github.com/.../releases/latest`.

Use the `bump_version` PlatformIO custom target to compute and push
the next tag:

```sh
pio run -t bump_version                # dry-run patch bump
BUMP_EXECUTE=1 pio run -t bump_version # actually tag and push
```

Full options in [VERSIONBUMP.md](VERSIONBUMP.md). The OTA flow and
asset naming rules are in [OTA.md](OTA.md).

## Credits

- Upstream boilerplate: [3110/m5stack-platformio-boilerplate-code](https://github.com/3110/m5stack-platformio-boilerplate-code)
  by `3110`. This repo is a recent fork with modifications —
  SafeGithubOTA integration, BLE provisioning, NVS WiFi seeding,
  release workflows.
- OTA library: [gibz104/SafeGithubOTA](https://github.com/gibz104/SafeGithubOTA)
  by `gibz104`.

## License

See [LICENCE.md](LICENCE.md).

---

## Secondary: compile-time WiFi credentials

**Not the recommended path** — BLE provisioning above is preferred and
keeps secrets out of build artifacts. Compile-time seeding exists for
locked-down dev firmware or factory provisioning.

To enable, in [platformio.ini](platformio.ini):

1. Uncomment `-DPRESET_WIFI_CREDS` and the two `-DWIFI_*` lines in
   the `[wifi-credentials]` section.
1. Inherit `${wifi-credentials.build_flags}` from the env(s) that
   should bake the creds in (opt-in per env).
1. Export the values before building:

   ```sh
   export WIFI_SSID="your-ssid"
   export WIFI_PASSWORD="your-password"
   pio run -e <env> -t upload
   ```

On first boot the device calls `WiFi.begin(WIFI_SSID, WIFI_PASSWORD)`,
which persists the credentials to the IDF `nvs.net80211` slot.
Subsequent boots reconnect via `WiFi.begin()` with no args. See
`seedWifiCredsIfEmpty()` in [src/main.cpp](src/main.cpp).

## Notes

I have not tested Arduino 2 and classic Platformio - I only focused on [Pioarduino](https://github.com/pioarduino).ioarduino
