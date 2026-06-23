# Flipper Claudeogotchi

Ambient status display for **Claude Code** on a **Flipper Zero**. It sits on your
desk and shows what a Claude Code session is doing *right now* — model, context
fill, rate limits, cost, and current state — as on-screen bars and an animated
sprite, and it **vibrates or chimes** when the session finishes or needs your
approval.

It works no matter where Claude Code runs: **local, an SSH box, or a remote dev
container.** Telemetry originates inside the Claude Code process (hooks +
statusline), so the machine you're looking at is irrelevant.

Sonar is the telemetry *return* channel that HID-based remotes structurally
can't provide: those send keystrokes *in*; Sonar pings the session and the state
comes *back*.

```
Claude Code (local / SSH / dev container)
  ├─ hooks  (events)   ─┐  HTTPS POST, keyed by sonar ID
  └─ statusline (stats)─┤
                        ▼
              Relay (reverse WS tunnel, hosted or self-hosted)
                        │  topic = sonar ID
                        ▼
              ESP32-S2 bridge on the Flipper GPIO  (WiFi, terminates TLS)
                        │  plain framed UART (pins 13/14)
                        ▼
              Flipper FAP "Sonar"  (bars + sprite + vibro/chime)
```

The Flipper has no internet radio and no TLS stack, so an ESP32-S2 bridge on the
GPIO header does the WiFi and terminates TLS; the Flipper only ever sees tiny
plain UART frames.

## Quick start

1. **FAP:** install **Sonar** from the Flipper App Catalog (or build with `ufbt`).
2. **Bridge:** flash the ESP32-S2 once (ESP Web Tools, or `idf.py`), wire it to
   the Flipper expansion header.
3. **Flipper:** open Sonar → **WiFi Setup**, enter your WPA SSID + password, read
   off your **sonar ID**.
4. **Claude Code host:** run it where Claude Code runs —

   ```bash
   npx github:<owner>/flipper-sonar --pair <SONAR_ID>
   ```

Restart Claude Code; within a turn the Flipper lights up. Full walkthrough:
[docs/setup.md](docs/setup.md).

## Privacy

Built for sensitive-sector use:

- **`cwd` never leaves the host as more than a basename.** The host-side hook
  poster and statusline wrapper reduce it to the project name *before* sending,
  and the relay strips it again as defense in depth.
- **Transcript content never leaves at all** — `transcript_path` is never read.
- **TLS terminates on the ESP32**, never on the Flipper.
- **Self-host the relay** with one `docker compose up` — no hosted dependency.
- **No AI attribution** in any commit (enforced in repo config and by the
  installer for your config).

## Repository layout

| Path | What |
|------|------|
| [`fap/`](fap/) | Flipper Zero app (C, ufbt): bars, sprite, haptics, settings, provisioning |
| [`firmware/`](firmware/) | ESP32-S2 bridge (ESP-IDF C): WiFi, WSS/TLS, UART reframe |
| [`relay/`](relay/) | Reverse WS tunnel (TypeScript): ingest, egress, heartbeat, snapshot |
| [`installer/`](installer/) | `npx` installer for the Claude Code side (zero deps) |
| [`proto/`](proto/) | Shared UART framing: `sonar_uart.h` + TS mirror (byte-identical) |
| [`e2e/`](e2e/) | Mock CC session + mock bridge proving the acceptance criteria |
| [`skill/`](skill/) | Optional `/review` command + claupper pairing notes |
| [`docs/`](docs/) | [setup](docs/setup.md) · [protocol](docs/protocol.md) · [troubleshooting](docs/troubleshooting.md) · [decisions](docs/decisions.md) |

## Tests

```bash
cd relay && npm install && cd ..    # one-time: relay deps (used by e2e too)
npm test                            # proto + relay + installer + e2e
```

Hardware-dependent behaviour (on-Flipper rendering, real haptics, flashing) is
documented in [BUILD_REPORT.md](BUILD_REPORT.md); everything else is covered by
47 automated tests.

## How it works (a bit more)

- **Three update rates collapse into one state object** the FAP renders: events
  (hooks) drive state + haptics; stats (statusline) drive the bars once per
  turn; the relay heartbeat drives stale detection.
- **UART framing** is tiny and resync-safe: `0x7E | len | type | payload | crc8`.
  One shared definition ([`proto/sonar_uart.h`](proto/sonar_uart.h)) is used by
  both the firmware and the FAP; a cross-language test proves the TS mirror is
  byte-identical.
- **Every telemetry field is optional and null-safe.** Missing `rate_limits` or
  `current_usage` degrades gracefully — bars hold their last value or show empty,
  never crash, never flicker to zero.

## License

MIT. See [LICENSE](LICENSE).
