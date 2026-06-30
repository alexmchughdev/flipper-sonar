# Blocked items

Things that genuinely need the human or an external resource. Each is stubbed so
the build still compiles and runs.

| Item | Why blocked | Stub / workaround in place |
|------|-------------|----------------------------|
| Hosted relay URL | No account/domain provisioned for a public hosted relay. | Installer defaults to `wss://relay.flipper-claudeogotchi.dev` (placeholder); `--relay` overrides it and the self-host path (`make relay` / Docker) is fully working and was used for bring-up. |
| Flipper App Catalog listing | Requires a Flipper account + submission, and a `fap_icon`. | FAP builds + flashes locally with `ufbt` (`make flash`); verified running on hardware. |
| ESP Web Tools manifest hosting | Needs a static host + signed binaries. | `firmware/manifest.json` + flashing instructions provided; binaries built locally with ESP-IDF. |
| ESP32-S2 board (WiFi path) | No board attached during bring-up. | Firmware is complete source; the **USB no-board path** was the verified one (host bridge → Flipper USB). Wire + flash steps in BUILD_REPORT.md. |

Resolved during bring-up: the FAP is flashed and running on a Flipper Zero
(Momentum `mntm-011`), and the no-board USB telemetry path works end to end.
