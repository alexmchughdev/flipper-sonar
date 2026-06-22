# Blocked items

Things that genuinely need the human or an external resource. Each is stubbed so
the build still compiles and runs.

| Item | Why blocked | Stub / workaround in place |
|------|-------------|----------------------------|
| Hosted relay URL | No account/domain provisioned for a public hosted relay. | Installer defaults to `wss://relay.flipper-sonar.dev` as a placeholder; `--relay` overrides it and the self-host path is fully functional. |
| Flipper App Catalog listing | Requires a Flipper account + submission. | FAP builds locally with `ufbt`; install via `ufbt launch`. |
| ESP Web Tools manifest hosting | Needs a static host + signed binaries. | `firmware/manifest.json` + flashing instructions provided; binaries built locally with ESP-IDF. |
| Real ESP32 + Flipper hardware | None attached to the build environment. | All non-hardware acceptance criteria proven via mock CC session + mock bridge in `/e2e`. Hardware flash/run steps in BUILD_REPORT.md. |
