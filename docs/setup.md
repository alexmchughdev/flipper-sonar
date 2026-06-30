# Setup

Two ways to get telemetry to the Flipper: **USB (no board)** — simplest, the
Flipper stays plugged into a computer — or **WiFi board** — a standalone desk
device. A 6-char **pairing code** (shown in the FAP's Settings, auto-minted on
first run) is the only thing you carry between the two ends.

```bash
make setup     # one-time: install deps (relay + ufbt)
make flash     # build + upload Clawd to a connected Flipper
```

---

## Option A — USB, no board (simplest)

The Flipper stays on USB to a computer; a tiny host bridge does the networking,
so this still works when Claude Code runs locally, over SSH, or in a dev
container (the bridge machine reaches the relay over the network).

1. **Relay** (terminal 1):
   ```bash
   make relay                      # self-host on :8787
   ```
2. **Pair Claude Code** on the machine it runs on:
   ```bash
   make pair ID=<CODE>             # or: make pair ID=<CODE> RELAY=http://host:8787
   ```
3. **Bridge** (terminal 2), on the machine the Flipper is plugged into:
   ```bash
   make bridge ID=<CODE>
   ```
4. On the Flipper: **Settings → Link → USB**.

Restart Claude Code; within a turn Clawd starts reacting. (USB mode uses a dual
CDC, so the Flipper CLI/qFlipper keep working alongside it.)

---

## Option B — WiFi board (standalone)

### 1. Flash the ESP32-S2 bridge (once)

Build `firmware/` with ESP-IDF (`idf.py set-target esp32s2 && idf.py build &&
idf.py flash`), or use ESP Web Tools with `firmware/manifest.json`. Seat the
board on the Flipper expansion header (ESP TX → Flipper **pin 14**, ESP RX →
Flipper **pin 13**, share GND/3V3). See [protocol.md](protocol.md).

### 2. Provision WiFi on the Flipper

FAP → **WiFi Setup** → type your **WPA** SSID + password (captive portals can't
be done by a headless board — use a phone hotspot; see
[troubleshooting](troubleshooting.md)). Note the **pairing code**, and make sure
**Settings → Link → Board**.

### 3. Pair Claude Code (where it runs)

```bash
npx github:alexmchughdev/flipper-claudeogotchi --pair <CODE> --relay https://<your-relay>
```

Inside a dev container, bake it into `postCreateCommand`. Identical for local,
SSH, and container — that's what makes remote work.

---

## Self-hosting the relay

```bash
cd relay && docker compose up --build   # relay + Caddy TLS (edit Caddyfile)
```

…or `make relay` on a trusted LAN (plaintext). Point `--pair … --relay` (host
side) and the FAP's relay URL at it. See [`../relay/README.md`](../relay/README.md).

## How updates flow

- **Events** (hooks) → Clawd's face + haptics, low latency.
- **Stats** (statusline) → the bars + working strip, once per turn.
- **Heartbeat** (relay) → the link glyph goes stale (`x`) if data stops.
