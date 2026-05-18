# OTA: firmware build, release, and on-device update

This document describes how [src/main.cpp](src/main.cpp) interacts with
the PlatformIO build scripts and the GitHub Actions workflows to deliver
over-the-air firmware updates via the [SafeGithubOTA] library.

[SafeGithubOTA]: https://github.com/gibz104/SafeGithubOTA

## Overview

```
git push (tag v*)
        │
        ▼
.github/workflows/release.yml ── pio run -t firmware ──► scripts/inject_build_info.py  (pre)
                                                        scripts/generate_merged_firmware.py  (post)
                                                                          │
                                                                          ▼
                                                            firmware/<proj>_<env>_firmware_<ver>.bin   ← full flash image
                                                            firmware/<proj>_<env>_ota.bin              ← OTA app-only image
                                                                          │
                                                                          ▼
                                                            GitHub Release assets (uploaded by release.yml)
                                                                          │
                                                                          ▼
device boot ── ota.loop() every 60s ── HTTPS to api.github.com/.../releases/latest
                                                                          │
                                                                          ▼
                                            asset whose "name" matches SGO_DEFAULT_BIN
                                                                          │
                                                                          ▼
                              download → flash inactive partition → reboot → validateFirmware()
                                                                          │
                                            pass ────► commit new firmware
                                            fail ────► bootloader rolls back to previous
```

## Build-time defaults baked into the firmware

[scripts/inject_build_info.py](scripts/inject_build_info.py) runs as a
PlatformIO pre-script and appends `-D` macros to `CPPDEFINES`. They are
referenced in [src/main.cpp](src/main.cpp) via `#ifdef` / `#ifndef`
guards so the firmware also compiles cleanly outside CI.

### Identity macros (best-effort, never required)

| Macro                 | Source                                        | Used in `main.cpp`                              |
|-----------------------|-----------------------------------------------|-------------------------------------------------|
| `BUILD_SHA`           | `git rev-parse --short HEAD`                  | [main.cpp:133-135](src/main.cpp#L133-L135)      |
| `BUILD_DATE`          | `datetime.now(UTC)` at build time             | [main.cpp:139-141](src/main.cpp#L139-L141)      |
| `BUILD_REPO`          | `$GITHUB_SERVER_URL/$GITHUB_REPOSITORY` (CI)  | [main.cpp:130-132](src/main.cpp#L130-L132)      |
| `BUILD_TAG`           | `$GITHUB_REF_NAME` if it matches `v?X.Y.Z…`   | [main.cpp:136-138](src/main.cpp#L136-L138), used as `ota.setVersion(BUILD_TAG)` at [main.cpp:204-206](src/main.cpp#L204-L206) |
| `BUILD_FIRMWARE_URI`  | Direct download URL of the merged-bin asset   | [main.cpp:142-144](src/main.cpp#L142-L144)      |

Dev builds (no `GITHUB_REF_NAME`) define only `BUILD_SHA` and
`BUILD_DATE`; the others stay undefined and the `#ifdef` blocks are
skipped.

### SafeGithubOTA defaults (`SGO_DEFAULT_*`)

These four macros are read by `seedSgoDefaults()`
([main.cpp:41-77](src/main.cpp#L41-L77)) on every boot and written to
the `sgo_creds` NVS namespace **only if the key is not already set** —
so values entered through the provisioning portal always win over
compile-time defaults.

| Macro                 | Source                                                                  | Notes                                            |
|-----------------------|-------------------------------------------------------------------------|--------------------------------------------------|
| `SGO_DEFAULT_OWNER`   | Parsed from `git remote get-url origin` (GitHub URL only)               | Override in `platformio.ini` wins.               |
| `SGO_DEFAULT_REPO`    | Same parse                                                              | Override in `platformio.ini` wins.               |
| `SGO_DEFAULT_BIN`     | `firmware_naming.ota_bin_filename(env)` — always `<proj>_<env>_ota.bin` | Must match exact asset name in the release.      |
| `SGO_DEFAULT_PAT`     | Not auto-injected; expected via `${sysenv.SGO_DEFAULT_PAT}` if needed   | Public repos: `ota.setPatRequired(false)`.       |

The injection logic checks `env["CPPDEFINES"]` and skips any macro
already defined (see
[inject_build_info.py:30-38](scripts/inject_build_info.py#L30-L38)), so
the precedence order is:

1. Explicit `-D...` in `platformio.ini` (highest)
2. Auto-injected default from git remote / `firmware_naming`
3. The compile-time fallback `""` in `main.cpp`

## Artifact naming (single source of truth)

All filenames are produced by [scripts/firmware_naming.py](scripts/firmware_naming.py).
Both the pre-script (for `BUILD_FIRMWARE_URI` and `SGO_DEFAULT_BIN`) and
the post-script (for the actual files written to `firmware/`) call into
the same module, so the macro baked into the device can never diverge
from the asset uploaded to the release.

| Filename                                       | Recipe                                          | When                          |
|------------------------------------------------|-------------------------------------------------|-------------------------------|
| `<proj>_<env>_firmware_<version>.bin`          | `merged_bin_filename()` with non-empty version  | Tagged CI build (release)     |
| `<proj>_<env>_firmware.bin`                    | `merged_bin_filename()` with empty version      | Dev / non-tagged build        |
| `<proj>_<env>_ota.bin`                         | `ota_bin_filename()` — **always versionless**   | Every build                   |

The OTA filename is intentionally versionless. SafeGithubOTA matches
release assets by exact filename
([SGO_GitHubClient.cpp `_findMatchingAsset`][sgo-match]). A versioned
filename would work for exactly one release and then break OTA for every
previously deployed device, because the next release would publish a
different filename. A versionless OTA asset is overwritten on each
release and stays discoverable forever.

[sgo-match]: .pio/libdeps/m5stack-nanoc6/SafeGithubOTA/src/SGO_GitHubClient.cpp

## Workflows

### `.github/workflows/build-firmware.yml`

Manual (`workflow_dispatch`) build over a matrix of PlatformIO
environments. Runs `pio run -e <env> -t firmware` and uploads
`firmware/*_<env>_firmware*.bin` as an artifact. Does **not** publish a
release; useful for CI verification on branches.

### `.github/workflows/release.yml`

Triggered by `push` of a tag matching `v*`. Runs the same build, then
the `release` job aggregates `firmware/*.bin` from every matrix env and
attaches them to a GitHub Release via `softprops/action-gh-release`.

Because `GITHUB_REF_NAME` is set to the tag (e.g. `v1.2.3`),
`resolve_version()` returns `1.2.3` and the merged-bin filename includes
the version. The OTA filename remains versionless either way.

## On-device flow

1. **Boot.** `setup()` runs, prints `BUILD_*` macros, brings up WiFi,
   then calls `seedSgoDefaults()` which writes `SGO_DEFAULT_*` into NVS
   if absent ([main.cpp:231](src/main.cpp#L231)).
2. **Provisioning.** If `!ota.isProvisioned()`, the BLE/portal flow
   collects WiFi credentials ([main.cpp:232-239](src/main.cpp#L232-L239)).
3. **Version pin.** `ota.setVersion(BUILD_TAG)` (or `FW_VERSION` if no
   tag was baked in) is what SafeGithubOTA compares against the latest
   GitHub release's `tag_name`
   ([main.cpp:204-210](src/main.cpp#L204-L210)).
4. **Auto-check loop.** `ota.setAutoCheckInterval(60)` then `ota.loop()`
   in `loop()` polls the GitHub API every 60 s
   ([main.cpp:228](src/main.cpp#L228),
   [main.cpp:255-263](src/main.cpp#L255-L263)).
5. **Match + download.** Newer tag → download the asset whose name
   equals the NVS `bin` value (which came from `SGO_DEFAULT_BIN` unless
   overridden) → write to the inactive OTA partition → reboot.
6. **Validate.** On post-OTA boot, `validateFirmware()` runs
   ([main.cpp:90-123](src/main.cpp#L90-L123)). It waits up to 10 s for
   WiFi to associate; if WiFi never comes up it returns `false` and the
   ESP32 bootloader marks the new partition invalid on the next reboot
   (rollback). `ota.wasRolledBack()` then reports the rollback on the
   following boot ([main.cpp:248-250](src/main.cpp#L248-L250)).

## Overriding the build-time defaults

In `platformio.ini`, uncomment the relevant line under `[env] build_flags`:

```ini
-DSGO_DEFAULT_OWNER=\"${sysenv.SGO_DEFAULT_OWNER}\"
-DSGO_DEFAULT_REPO=\"${sysenv.SGO_DEFAULT_REPO}\"
-DSGO_DEFAULT_BIN=\"${sysenv.SGO_DEFAULT_BIN}\"
-DSGO_DEFAULT_PAT=\"${sysenv.SGO_DEFAULT_PAT}\"
```

…and export the matching env var before invoking `pio run`. The
auto-inject skips any macro already present in `CPPDEFINES`, so the
explicit value wins.

At runtime, values entered via `ota.startProvisioningPortal()` are
written to NVS directly and override the compile-time defaults for the
life of the device (until a factory reset clears the `sgo_creds`
namespace).

## Cutting a release — checklist

1. Bump `FW_VERSION` in [main.cpp:83](src/main.cpp#L83) if you want the
   value reported when `BUILD_TAG` is unavailable. (CI builds replace
   it with the tag anyway.)
2. Commit, then tag and push: `git tag v1.2.3 && git push --tags`.
3. `release.yml` builds every env in the matrix, uploads
   `firmware/*.bin` as release assets.
4. Deployed devices pick up the new tag within `AutoCheckInterval`
   seconds, fetch `<proj>_<env>_ota.bin`, flash, validate, commit (or
   roll back).

## Local dev quickstart

```bash
pio run -e m5stack-nanoc6 -t firmware
ls firmware/
# pioarduino_test_m5stack-nanoc6_firmware.bin   ← full image (flash with esptool)
# pioarduino_test_m5stack-nanoc6_ota.bin        ← app-only OTA image
```

For a one-off versioned dev build matching what CI produces:

```bash
GITHUB_REF_NAME=v1.2.3 pio run -e m5stack-nanoc6 -t firmware
# adds firmware/pioarduino_test_m5stack-nanoc6_firmware_1.2.3.bin
```

Verify which `SGO_DEFAULT_*` values were baked in:

```bash
pio run -e m5stack-nanoc6 -v 2>&1 | grep -oE -- '-DSGO_DEFAULT[^ ]*' | sort -u
```
