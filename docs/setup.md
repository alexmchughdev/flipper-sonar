# Setup

The shipped, plug-and-play flow. A pairing code is the only thing you carry
between the two ends — no Tailscale, no port forwarding, no homelab.

## Standalone desk device

### 1. Install the FAP on the Flipper

- From the Flipper App Catalog: search **Sonar**, install. *(Catalog submission
  pending — see [blocked.md](blocked.md). Until then build locally with `ufbt`;
  see [`../fap/README.md`](../fap/README.md).)*

### 2. Flash the ESP32-S2 bridge (once, no toolchain)

- Open the ESP Web Tools flasher in a Chromium browser and connect the
  ESP32-S2 WiFi Dev Board over USB. *(Hosted flasher pending; build + flash
  locally per [`../firmware/README.md`](../firmware/README.md).)*
- Wire the board to the Flipper expansion header: ESP TX → Flipper **pin 14**
  (RX), ESP RX → Flipper **pin 13** (TX), share GND/3V3. See
  [protocol.md](protocol.md).

### 3. Provision WiFi + read your sonar ID (on the Flipper)

1. Open the Sonar FAP → **WiFi Setup**.
2. Type your **WPA Wi-Fi SSID**, then the **password**.
   - This is a *join WPA network* path. Captive portals (hotels, cafés,
     conferences) can't be completed by a headless board — use a phone hotspot
     or travel router. The app says so; see [troubleshooting](troubleshooting.md).
3. The FAP shows your **sonar ID** (a 6-character pairing code). Write it down.

### 4. Pair Claude Code (on the machine where Claude Code runs)

Run this **wherever Claude Code actually runs** — your laptop, an SSH box, or
inside a dev container. It edits *that* environment's config, which is what makes
remote work:

```bash
npx github:<owner>/flipper-sonar --pair <SONAR_ID>
```

Self-hosted relay:

```bash
npx github:<owner>/flipper-sonar --pair <SONAR_ID> --relay https://my.relay
```

Inside a dev container, bake it into the image or `postCreateCommand`:

```jsonc
"postCreateCommand": "npx -y github:<owner>/flipper-sonar --pair <SONAR_ID>"
```

Start (or restart) Claude Code. Within one turn the Flipper lights up.

The board + Flipper then sit on USB power. **Step 4 is identical for local, SSH,
and container** — that's the whole point.

## Self-hosting the relay

Sensitive-sector users can run the relay themselves with no hosted dependency:

```bash
cd relay
docker compose up --build      # relay + Caddy TLS (edit Caddyfile for your domain)
```

Then `--relay https://your-domain` on the installer, and set the same base URL
on the Flipper (re-run WiFi Setup, which re-provisions the bridge). See
[`../relay/README.md`](../relay/README.md).

## How updates flow

- **Events** (hooks) → state + haptics, low latency.
- **Stats** (statusline) → bars, once per turn.
- **Heartbeat** (relay) → the FAP shows stale/disconnected if it stops.
