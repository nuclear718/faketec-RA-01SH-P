# Agent instructions

> **TL;DR**
>
> |                |                                                                           |
> | -------------- | ------------------------------------------------------------------------- |
> | Local tests    | `./bin/run-tests.sh` (exit 0 GREEN · 1 RED · 2 AMBER · 3 FILTERED)        |
> | Hardware tests | `./mcp-server/run-tests.sh`                                               |
> | Format         | `trunk fmt`                                                               |
> | Mirror docs    | `.github/copilot-instructions.md` (canonical) · `CLAUDE.md` (Claude Code) |
>
> **Need this? It's here.**
>
> |                                             |                                                            |
> | ------------------------------------------- | ---------------------------------------------------------- |
> | General helpers (clamp, UTF-8, string fmt…) | `src/meshUtils.h`                                          |
> | Logging macros (LOG_DEBUG / INFO / WARN…)   | `src/DebugConfiguration.h`                                 |
> | New module skeleton                         | inherit `ProtobufModule<T>` in `src/mesh/ProtobufModule.h` |
> | Observer / event wiring                     | `src/Observer.h`                                           |

This repository is the [Meshtastic](https://meshtastic.org) firmware — a C++17 embedded codebase targeting ESP32 / nRF52 / RP2040 / STM32WL / Linux-Portduino LoRa mesh radios — plus a Python MCP server in `mcp-server/` that AI agents use to flash, configure, and test connected devices.

## Fork-specific notes: faketec RA-01SH-P / NTsocial

This repository is an independent fork of `meshtastic/firmware`, not a clean upstream checkout. Preserve local hardware support unless the operator explicitly asks to remove it.

Current upstream sync baseline:

- Official source synced from `https://github.com/meshtastic/firmware.git`.
- Last merged official branch: `develop`.
- Official commit included in this fork: `b44ed4552`.
- Local merge commit: `4d6508754`.
- Firmware artifact tracking commit: `7ff14bbfe`.
- Firmware version after sync: `2.8.0`.

If the `meshtastic-official` remote is not configured, refresh the official tracking ref with:

```powershell
git fetch https://github.com/meshtastic/firmware.git develop:refs/remotes/meshtastic-official/develop
```

### Custom nRF52 Pro Micro XTAL target

The local board target `nrf52_promicro_diy_xtal` is intentional and must be preserved even if upstream deletes or reshapes nearby DIY variants.

Key files:

- `variants/nrf52840/diy/nrf52_promicro_diy_xtal/platformio.ini`
- `variants/nrf52840/diy/nrf52_promicro_diy_xtal/variant.h`
- `variants/nrf52840/diy/nrf52_promicro_diy_xtal/variant.cpp`
- `src/mesh/RadioInterface.cpp`
- `src/mesh/RF95Interface.cpp`
- `src/mesh/RadioLibRF95.cpp`

Preserve these local hardware assumptions:

- MCU target: `promicro-nrf52840`.
- Meshtastic env: `nrf52_promicro_diy_xtal`.
- LoRa radio: RFM95W / SX127x using `USE_RF95`.
- SPI pins: MISO `P0.08`, MOSI `P0.06`, SCK `P0.17`.
- Radio pins: CS `P0.24`, DIO0/IRQ `P0.11`, RESET `P0.09`.
- Battery sense uses one auto-detecting UF2: prefer `VBAT → 1 MΩ → P0.31/AIN7 → 1 MΩ → GND` with a `2.0` ADC multiplier, otherwise use nRF52840 `VDDH/5` only in high-voltage mode. Keep the 40 µs SAADC sample time. VDDH-only readings are unknown while USB is present.
- GPS pins use the upstream 2.8 names: `GPS_TX_PIN` for MCU TX to GPS RX and `GPS_RX_PIN` for MCU RX from GPS TX.
- RF95 custom power behavior is intentional:
  - `RF95_MAX_POWER 20`
  - `RF95_CURRENT_LIMIT 120`
  - `RF95_ALLOW_20DBM_TX_POWER`
  - `LORA_TW_POWER_LIMIT_OVERRIDE 20`

`RadioInterface.cpp` uses `getEffectiveRegionPowerLimit()` so Taiwan (`TW`) can keep the local 20 dBm override while still following upstream 2.8 power-limit logic. `RF95Interface.cpp` normalizes RF95 requested power without directly reading `config.lora`, because upstream 2.8 refactored that area. Do not revert these changes when merging upstream.

`variant.cpp` should keep upstream-compatible `variant_shutdown()` support for wake/sense behavior.

### Build and firmware artifacts

Preferred build command on this Windows workspace:

```powershell
py -3.12 -m platformio run -e nrf52_promicro_diy_xtal
```

Use `pio run -e nrf52_promicro_diy_xtal` only when PlatformIO is available on `PATH`. PlatformIO was installed as a Python 3.12 user module, so `py -3.12 -m platformio` is the reliable invocation here.

The validated 2.8.0 build produced:

```text
.pio/build/nrf52_promicro_diy_xtal/firmware-nrf52_promicro_diy_xtal-2.8.0.4d65087.uf2
.pio/build/nrf52_promicro_diy_xtal/firmware-nrf52_promicro_diy_xtal-2.8.0.4d65087.zip
.pio/build/nrf52_promicro_diy_xtal/firmware-nrf52_promicro_diy_xtal-2.8.0.4d65087.hex
.pio/build/nrf52_promicro_diy_xtal/firmware-nrf52_promicro_diy_xtal-2.8.0.4d65087.mt.json
```

Do not commit the whole `.pio` directory. It contains PlatformIO caches, dependency checkouts, objects, and local build databases. `.gitignore` intentionally tracks only the custom target's release artifacts (`uf2`, `zip`, `hex`, `mt.json`) and continues to ignore generated caches and intermediate files.

Before publishing a firmware artifact, rebuild from the current commit so the filename hash matches `HEAD`. Remove stale firmware artifacts with older hashes unless the operator explicitly wants to archive them.

## Primary instruction file

**Read `.github/copilot-instructions.md` first.** That file is the canonical agent-facing document for this repo. It covers project layout, coding conventions (naming, module framework, Observer pattern, thread safety), the build system, CI/CD, the native C++ test suite, and — most importantly for automation work — the **MCP Server & Hardware Test Harness** section. Read it top-to-bottom before starting any non-trivial change.

This file (`AGENTS.md`) is a short pointer + quick reference for agents that don't read `.github/copilot-instructions.md` by default.

## Quick command reference

| Action                           | Command                                                                                                                                                               |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Build a firmware variant         | `pio run -e <env>` (e.g. `pio run -e rak4631`, `pio run -e heltec-v3`)                                                                                                |
| Build native macOS host binary   | `pio run -e native-macos` (Homebrew prereqs + CH341 LoRa setup in `variants/native/portduino/platformio.ini`)                                                         |
| Clean + rebuild                  | `pio run -e <env> -t clean && pio run -e <env>`                                                                                                                       |
| Flash a device                   | `pio run -e <env> -t upload --upload-port <port>` (or use the `pio_flash` MCP tool)                                                                                   |
| Run firmware unit tests (native) | `./bin/run-tests.sh` (preferred — ASan/LSan + RED/AMBER/GREEN verdict); or raw: `~/.platformio/penv/bin/python -m platformio test -e native > /tmp/test_out.txt 2>&1` |
| Run MCP hardware tests           | `./mcp-server/run-tests.sh`                                                                                                                                           |
| Live TUI test runner             | `mcp-server/.venv/bin/meshtastic-mcp-test-tui`                                                                                                                        |
| Format before commit             | `trunk fmt`                                                                                                                                                           |
| Regenerate protobuf bindings     | `bin/regen-protos.sh`                                                                                                                                                 |
| Generate CI matrix               | `./bin/generate_ci_matrix.py all [--level pr]`                                                                                                                        |

## MCP server (device + test automation)

The `mcp-server/` package exposes ~32 MCP tools for device discovery, building, flashing, serial monitoring, and live-node administration. Tools are grouped as:

- **Discovery**: `list_devices`, `list_boards`, `get_board`
- **Build & flash**: `build`, `clean`, `pio_flash`, `erase_and_flash` (ESP32 factory), `update_flash` (ESP32 OTA), `touch_1200bps`
- **Serial sessions**: `serial_open`, `serial_read`, `serial_list`, `serial_close`
- **Device reads**: `device_info`, `list_nodes`
- **Device writes** (require `confirm=True`): `set_owner`, `get_config`, `set_config`, `get_channel_url`, `set_channel_url`, `send_text`, `reboot`, `shutdown`, `factory_reset`, `set_debug_log_api`
- **userPrefs admin**: `userprefs_get`, `userprefs_set`, `userprefs_reset`, `userprefs_manifest`, `userprefs_testing_profile`
- **Vendor escape hatches**: `esptool_*`, `nrfutil_*`, `picotool_*`

Setup: `cd mcp-server && python3 -m venv .venv && .venv/bin/pip install -e '.[test]'`. The repo registers the server via `.mcp.json` — Claude Code picks it up automatically.

See `mcp-server/README.md` for argument shapes and the **MCP Server & Hardware Test Harness** section of `.github/copilot-instructions.md` for agent usage rules (tool surface, fixture contract, firmware integration points, recovery playbooks).

## Slash commands (AI-assisted workflows)

Three test-and-diagnose workflows exist as slash commands:

- **`/test` (Claude Code) / `/mcp-test` (Copilot)** — run the hardware test suite and interpret failures
- **`/diagnose` / `/mcp-diagnose`** — read-only device health report
- **`/repro` / `/mcp-repro`** — flakiness triage: re-run one test N times, diff firmware logs between passes and failures

Bodies live in `.claude/commands/` and `.github/prompts/` respectively. `.claude/commands/README.md` is the index.

## Encryption at a glance

Two layers, both in `src/mesh/CryptoEngine.cpp`:

- **Channel (symmetric)** — **AES-CTR** with a channel-wide PSK (AES-128 or AES-256). Nonce = packet_id ‖ from_node ‖ block_counter. No AEAD; integrity is soft (channel-hash filter). The well-known default PSK lives in `src/mesh/Channels.h`; a 1-byte PSK is a short-form index into it.
- **Per-peer PKI** — **X25519 ECDH** (Curve25519, 32-byte keys) → SHA-256 → **AES-256-CCM** with an 8-byte MAC. Fresh 32-bit `extraNonce` per packet, sent in the clear alongside the MAC. 12-byte wire overhead (`MESHTASTIC_PKC_OVERHEAD`). Used for DMs. Also used for remote admin (`src/modules/AdminModule.cpp`), where AdminMessage authorization is gated by `config.security.admin_key[0..2]`. Disabled entirely in Ham mode (`user.is_licensed=true`).

Key rotation to never trigger casually: only the **full** factory reset (`factory_reset_device`, `eraseBleBonds=true`) wipes `security.private_key` and regenerates the keypair — every peer holds the old public key, so DMs silently fail PKI decrypt until NodeInfo re-exchanges. The **partial** config reset (`factory_reset_config`) preserves the private key and doesn't invalidate peer relationships. Explicitly blanking `security.private_key` via admin also triggers regen. See the **Encryption & Key Management** section of `.github/copilot-instructions.md` for the full spec (nonce layout, send/receive selection logic including infrastructure-portnum exceptions, admin-key + session-passkey authorization, `is_managed` scope, key-rotation hazards).

## House rules

- **No destructive device operations without operator approval.** `factory_reset`, `erase_and_flash`, `reboot`, `shutdown`, history-rewriting git ops — describe the action and stop. Operator authorizes.
- **One MCP call per serial port at a time.** The port lock is exclusive; concurrent calls deadlock. Sequence: open → read/mutate → close, then next device.
- **`userPrefs.jsonc` is session state during tests.** The `_session_userprefs` fixture snapshots + restores it; never edit it from inside a test.
- **Don't speculate about firmware root causes.** When evidence doesn't support a classification, say "unknown" and list what would disambiguate.
- **Run `trunk fmt` before proposing a commit.** The `trunk_check` CI gate will reject unformatted code. Claude Code runs it automatically via the PostToolUse hook in `.claude/settings.json`; trunk's launcher needs `curl` or `wget` to bootstrap its pinned CLI — see **Formatting & the trunk toolchain** in `.github/copilot-instructions.md` for the no-curl bootstrap procedure.
- **`confirm=True` on destructive MCP tools is a real gate, not a formality.** Don't bypass it via auto-approve settings.
- **Keep code comments minimal — one or two lines, max.** Comment only when the _why_ isn't obvious from the code; never restate what the next line does. No multi-paragraph block comments explaining straightforward changes. The diff and commit message carry the rationale; the code carries the behavior.
- **Use `Throttle` for time-based rate limiting, not raw `millis()` math.** `src/mesh/Throttle.h` provides `Throttle::isWithinTimespanMs(lastMs, intervalMs)` (returns true while inside the cooldown) and `Throttle::execute(&lastMs, intervalMs, func)` (function-pointer form that updates the timestamp on fire). Use these for any "did N ms pass since X" check — raw `millis() > lastMs + N` is rollover-unsafe (breaks after ~49.7 days) and inconsistent with the rest of the codebase. The helpers compute `now - lastMs` with unsigned subtraction, which wraps correctly.

## Typical agent workflows

### Flashing a device

1. `list_devices` → find the port + likely VID
2. `list_boards` → confirm the env, or use the known default for the hardware
3. `pio_flash(env=..., port=..., confirm=True)` for any arch, or `erase_and_flash(env=..., port=..., confirm=True)` for an ESP32 factory install

### Inspecting live node state

1. `device_info(port=...)` — short summary (node num, firmware version, region, peer count)
2. `list_nodes(port=...)` — full peer table (SNR, RSSI, pubkey presence, last_heard)
3. `get_config(section="lora", port=...)` — LoRa settings for cross-device comparison

Sequence these; don't parallelize on the same port.

### Testing a firmware change

1. Build locally: `pio run -e <env>`
2. Flash the test device: `pio_flash(env=..., port=..., confirm=True)`
3. Run the suite: `./mcp-server/run-tests.sh tests/<tier>` or `/test tests/<tier>`
4. On failure, open `mcp-server/tests/report.html` → `Meshtastic debug` section for the firmware log tail + device state dump
5. Iterate

### Debugging a flaky test

1. `/repro <test-node-id> [count]` — re-runs the test N times, diffs firmware logs between passes and failures
2. If the first attempt always fails and the rest pass, that's a state-leak pattern → suggest `--force-bake` or a clean device state, don't chase the first failure
3. If all N fail, this isn't a flake — it's a regression. Stop iterating and escalate to `/test` for full-suite context.

## Where to look

| Path                              | What's there                                                                                                             |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `src/`                            | Firmware C++ source (`mesh/`, `modules/`, `platform/`, `graphics/`, `gps/`, `motion/`, `mqtt/`, …)                       |
| `src/mesh/`                       | Core: NodeDB, Router, Channels, CryptoEngine, radio interfaces, StreamAPI, PhoneAPI                                      |
| `src/modules/`                    | Feature modules; `Telemetry/Sensor/` has 50+ I2C sensor drivers                                                          |
| `variants/`                       | 200+ hardware variant definitions (`variant.h` + `platformio.ini` per board)                                             |
| `protobufs/`                      | `.proto` definitions; regenerate with `bin/regen-protos.sh`                                                              |
| `test/`                           | Firmware unit tests (19 suites; `./bin/run-tests.sh` preferred, falls back to `pio test -e native`)                      |
| `mcp-server/`                     | Python MCP server + pytest hardware integration tests                                                                    |
| `mcp-server/tests/`               | Tiered pytest suite: `unit/`, `mesh/`, `telemetry/`, `monitor/`, `recovery/`, `ui/`, `fleet/`, `admin/`, `provisioning/` |
| `.claude/commands/`               | Claude Code slash command bodies                                                                                         |
| `.github/prompts/`                | Copilot prompt bodies (mirrors of the Claude Code ones)                                                                  |
| `.github/copilot-instructions.md` | **Primary agent instructions — read this**                                                                               |
| `.github/workflows/`              | CI pipelines                                                                                                             |
| `.mcp.json`                       | MCP server registration for Claude Code                                                                                  |

## Recovery one-liners

- **`userPrefs.jsonc` dirty after a test run?** Re-run `./mcp-server/run-tests.sh` once (pre-flight self-heals from the sidecar). If still dirty: `git checkout userPrefs.jsonc`.
- **nRF52 not responding?** `mcp__meshtastic__touch_1200bps(port=...)` drops it into the DFU bootloader, then `pio_flash` re-installs.
- **Device fully wedged (no DFU)?** `mcp__meshtastic__uhubctl_cycle(role="nrf52", confirm=True)` hard-power-cycles it via USB hub PPPS. Needs `uhubctl` installed (`brew install uhubctl` / `apt install uhubctl`); on Linux without udev rules, permission errors fail fast, so use `sudo uhubctl` yourself or configure udev access.
- **Port busy?** `lsof <port>` to find the holder. Usually a stale `pio device monitor` or zombie `meshtastic_mcp` process. Kill it.
- **Multiple MCP servers running?** `ps aux | grep meshtastic_mcp` — zombies hold ports. Kill all but the one your host spawned.
- **macOS: `LIBUSB_ERROR_BUSY` on a CH341 LoRa adapter?** A third-party WCH `CH34xVCPDriver` is claiming interface 0. Find the bundle ID with `ioreg -p IOUSB -l -w 0 | grep -B2 -A30 0x5512`, then `sudo kmutil unload -b <bundleID>`. Apple's bundled CH34x kext targets the CH340 UART (PID 0x7523), not the SPI bridge — it's never the culprit.

## Environment variables (test harness)

| Var                                  | Purpose                                                                                                                                                                                                    |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MESHTASTIC_MCP_ENV_<ROLE>`          | Override PlatformIO env for a role (e.g. `MESHTASTIC_MCP_ENV_NRF52=rak4631-dap`). Default map: `nrf52→rak4631`, `esp32s3→heltec-v3`.                                                                       |
| `MESHTASTIC_MCP_SEED`                | PSK seed for the session test profile. Defaults to `mcp-<user>-<host>`.                                                                                                                                    |
| `MESHTASTIC_MCP_FLASH_LOG`           | File path to tee pio/esptool/nrfutil/picotool output. `run-tests.sh` sets this to `tests/flash.log` so the TUI can stream live flash progress.                                                             |
| `MESHTASTIC_MCP_TCP_HOST`            | `host` or `host:port` of a `meshtasticd` daemon (e.g. the `native-macos` build). Surfaces it in `list_devices` as `tcp://host:port` so `connect()`-based tools target it transparently. Default port 4403. |
| `MESHTASTIC_UHUBCTL_BIN`             | Absolute path to `uhubctl` binary. Default: PATH lookup.                                                                                                                                                   |
| `MESHTASTIC_UHUBCTL_LOCATION_<ROLE>` | Pin a role to a specific uhubctl hub location (e.g. `1-1.3`). Wins over VID auto-detection — use when multiple devices share a VID.                                                                        |
| `MESHTASTIC_UHUBCTL_PORT_<ROLE>`     | Pin a role to a specific hub port number. Required alongside `LOCATION_<ROLE>`.                                                                                                                            |
| `MESHTASTIC_UI_CAMERA_BACKEND`       | Camera backend for UI tier + `capture_screen` tool: `opencv` / `ffmpeg` / `null` / `auto` (default).                                                                                                       |
| `MESHTASTIC_UI_CAMERA_DEVICE`        | Generic camera device (index or path). Used by the UI tier when no per-role var is set.                                                                                                                    |
| `MESHTASTIC_UI_CAMERA_DEVICE_<ROLE>` | Per-role camera pinning (e.g. `MESHTASTIC_UI_CAMERA_DEVICE_ESP32S3=0` for the OLED-bearing heltec-v3).                                                                                                     |
| `MESHTASTIC_UI_OCR_BACKEND`          | OCR engine selection: `easyocr` / `pytesseract` / `null` / `auto` (default).                                                                                                                               |
| `MESHTASTIC_UI_TUI_CAMERA`           | Set to `1` to mount the live camera-feed panel in `meshtastic-mcp-test-tui`.                                                                                                                               |
