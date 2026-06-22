# Build report — Flipper Sonar

Built end to end from `SPEC.md`. Summary of what exists, what's verified, what's
stubbed/blocked, and exactly how to flash and run on real hardware.

## What was built

| Component | Path | State |
|-----------|------|-------|
| Shared UART protocol | `proto/` | Complete. Single C header + byte-identical TS mirror, resync-safe parser, typed builders + decoders. |
| Relay | `relay/` | Complete. Reverse-WS tunnel, ingest/egress/heartbeat/snapshot, in-memory per-topic state, self-host + Docker/Caddy. |
| Firmware (ESP32-S2) | `firmware/` | Complete source. WiFi station, NVS creds, WSS/TLS termination, JSON→UART reframe, reconnect/backoff, UART provisioning intake. |
| FAP (Flipper) | `fap/` | Complete source. Bars + state-driven sprite, mutex-guarded model, ISR-safe UART worker, debounced haptics, settings, WPA provisioning, SD config, session selection. |
| Installer | `installer/` | Complete. Zero-dep idempotent `npx` installer; hooks + statusline + attribution; `--pair/--relay/--project/--uninstall`. |
| E2E | `e2e/` | Complete. Real host scripts → real relay → mock bridge → mock FAP. |
| Docs | `docs/`, `README.md` | Complete. Setup, protocol, troubleshooting, decisions, blocked. |
| Review gate | `.claude/commands/review.md` | Complete. Run before each commit. |

## What passed (automated, no hardware)

**47 tests, all green.**

```
proto      10  (CRC vectors, framing, round-trip, resync, null-safety;
                 + cross-language C↔TS frame diff: byte-identical)
relay      16  (normalization/privacy/framing, topic routing, slots, snapshot,
                 heartbeat, dead-sub pruning)
installer  13  (idempotent merge, clean uninstall, no-clobber, null-safety,
                 shell-quote/no-injection, relay URL normalization)
e2e         8  (live state in one turn, Stop→done <1s, approval alert,
                 null-safe hold-last, snapshot replay, no session flap,
                 self-host path, privacy: no cwd/transcript on the wire)
```

Run them:

```bash
cd relay && npm install && cd ..   # one-time (e2e imports the real relay)
npm test
```

### SPEC §8 acceptance criteria

| Criterion | Status |
|-----------|--------|
| Live state within one turn | ✅ e2e |
| `Stop` → chime+vibro within ~1s | ✅ e2e proves the `done` frame < 1s; the chime itself is hardware |
| Permission prompt → distinct chime + alert pose | ✅ e2e proves the `waiting-approval` transition; chime/pose are hardware |
| Bars track ctx/5h/7d/cost, degrade gracefully | ✅ e2e + unit |
| Works local / SSH / dev container unchanged | ✅ by design — telemetry originates in the CC process; identical install step |
| Two sessions don't flap; selectable | ✅ e2e |
| Relay self-host works with no hosted dep | ✅ e2e runs entirely on a local relay |
| No commit contains AI attribution | ✅ verified across full history |
| `cwd` only ever a basename; transcript never leaves | ✅ enforced in host scripts + relay; e2e inspects raw wire bytes |

## What is stubbed or blocked (and why)

See [`docs/blocked.md`](docs/blocked.md). In short:

- **Hosted relay URL** — no public instance provisioned; installer default is a
  placeholder, `--relay` and self-host are fully working.
- **App Catalog listing / ESP Web Tools hosting** — need accounts + a static
  host; local build/flash paths are complete and documented.
- **Real ESP32 + Flipper hardware** — none in the build environment, so on-device
  rendering and physical haptics are not exercised here. All non-hardware
  criteria are proven by the e2e suite.

The firmware and FAP are written against ESP-IDF and the Flipper SDK
respectively; they were **not** compiled here because neither toolchain (ESP-IDF,
ufbt) is installed in this environment. The shared protocol code they depend on
*is* compiled and tested (C `proto` round-trip + cross-language diff).

## Flash & run on real hardware

### 1. Relay (self-host, recommended for first run)

```bash
cd relay
npm install
npm start -- --self-host          # plaintext on :8787 for a trusted LAN
# or: docker compose up --build    # relay + Caddy TLS for a public domain
```

### 2. ESP32-S2 bridge

```bash
cd firmware
. $IDF_PATH/export.sh
idf.py set-target esp32s2
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Wire to the Flipper expansion header: **ESP TX → Flipper pin 14 (RX)**, **ESP RX
→ Flipper pin 13 (TX)**, share GND/3V3. (Override ESP pins with
`-DSONAR_UART_TX_PIN=.. -DSONAR_UART_RX_PIN=..` if your board differs.)

### 3. FAP

```bash
python3 -m pip install --user ufbt
cd fap
ufbt            # build (downloads the matching SDK first time)
ufbt launch     # upload + run on a connected Flipper
```

### 4. Provision + pair

1. On the Flipper: Sonar → **WiFi Setup** → enter WPA SSID + password → note the
   **sonar ID**.
2. Where Claude Code runs:
   ```bash
   npx github:<owner>/flipper-sonar --pair <SONAR_ID> --relay https://<your-relay>
   ```
   (omit `--relay` to use the hosted default once it exists; for self-host on a
   LAN without TLS, point `--relay http://<host>:8787` and run the relay with
   `--self-host`.)
3. Restart Claude Code. The Flipper shows live state within one turn.

## Notes for the maintainer

- Set `<owner>` in the installer docs/README to your GitHub org/user before
  publishing the `npx github:` path.
- Provision a hosted relay + domain, then update the default URL in
  `installer/index.js` (`DEFAULT_RELAY`) and `fap/sonar_config.c`.
- For the App Catalog, add a `fap_icon` PNG to `fap/` and reference it in
  `application.fam`.
