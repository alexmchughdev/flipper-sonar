# Build report — Flipper Claudeogotchi

A Claude Code Tamagotchi (**Clawd**) for the Flipper Zero. Built end to end from
`SPEC.md`, then iterated on real hardware. Summary of what exists, what's
verified, what's stubbed, and how to run it.

## What was built

| Component | Path | State |
|-----------|------|-------|
| FAP (Flipper) | `fap/` | Complete + **flashed and running on hardware**. Clawd creature, SESS/5H/WEEK bars, working strip (spinner + random spinner-verb + run timer + tokens), debounced haptics, settings, Board/USB link modes, WPA provisioning, SD config. |
| Shared UART protocol | `proto/` | Complete. One C header + byte-identical TS mirror; resync-safe parser, builders, decoders, tokens field. |
| Relay | `relay/` | Complete. Reverse-WS tunnel, ingest/egress/heartbeat/snapshot, in-memory per-topic state, self-host + Docker/Caddy. |
| Firmware (ESP32-S2) | `firmware/` | Complete source. WiFi station, NVS creds, WSS/TLS termination, JSON→UART reframe (incl. tokens), reconnect/backoff. Not flashed here (no board attached). |
| Installer + host runtime | `installer/` | Complete. Idempotent `npx` installer; hooks + statusline + attribution; **no-board USB bridge**; `--pair/--relay/--project/--uninstall`. |
| E2E | `e2e/` | Complete. Real host scripts → real relay → mock bridge → mock FAP. |
| Docs + Makefile | `docs/`, `README.md`, `Makefile` | Complete. One-command setup; setup/protocol/troubleshooting/decisions. |

## What passed

**47 automated tests, all green**, plus a cross-language C↔TS frame diff:

```
make test         # proto 10 · relay 16 · installer 13 · e2e 8
make proto-check  # C and TS UART encoders byte-identical
```

**On hardware:** the FAP builds against the Momentum `mntm-011` SDK (API 86,
matching the device — no "app too new" prompt), flashes via `ufbt launch`, and
runs. The live no-board path (host scripts → relay → USB bridge → Flipper) was
exercised end to end on the device: stats moved the bars, a permission prompt
showed the alert, and `done` chimed.

### SPEC §8 acceptance criteria

| Criterion | Status |
|-----------|--------|
| Live state within one turn | ✅ e2e + on hardware |
| `Stop` → chime within ~1s | ✅ e2e (<1s frame) + hardware chime |
| Permission prompt → distinct alert | ✅ e2e + hardware |
| Bars degrade gracefully when data absent | ✅ unit + e2e |
| Local / SSH / dev container unchanged | ✅ by design (telemetry from the CC process) |
| Two sessions don't flap; selectable | ✅ e2e |
| Self-host relay works with no hosted dep | ✅ whole suite + live run on `make relay` |
| No commit contains AI attribution | ✅ verified across history |
| `cwd` only a basename; transcript never leaves | ✅ enforced host-side + relay; e2e checks the wire bytes |

## Stubbed / blocked

See [`docs/blocked.md`](docs/blocked.md). In short: the **hosted** relay URL is a
placeholder (self-host + `--relay` are fully working); App Catalog and ESP Web
Tools hosting need accounts (local build/flash works); the **ESP32 board path**
is complete source but wasn't flashed here (no board attached — the USB no-board
path is the verified one).

## Run it

```bash
make setup        # install deps (relay + ufbt) — one time
make flash        # build + upload Clawd to a connected Flipper
```

No-board (USB) — Flipper plugged into this machine:

```bash
make relay                 # local relay
make pair   ID=<CODE>      # wire up Claude Code on this machine
make bridge ID=<CODE>      # stream over USB
#   then on the Flipper: Settings → Link → USB
```

`<CODE>` is the 6-char pairing code in the FAP's Settings (auto-minted on first
run). With the WiFi board instead, flash `firmware/` (`idf.py`), seat it on the
GPIO header, use the FAP's **WiFi Setup**, and pair the host with
`npx github:alexmchughdev/flipper-sonar --pair <CODE> --relay https://<relay>`.

## Notes for the maintainer

- Provision a hosted relay + domain, then update `DEFAULT_RELAY` in
  `installer/index.js` and the default in `fap/sonar_config.c`.
- For the App Catalog, add a `fap_icon` PNG and reference it in `application.fam`.
- Device used for bring-up: Flipper Zero on **Momentum `mntm-011`**. For other
  firmware, repoint `ufbt update` (see `docs/decisions.md`).
