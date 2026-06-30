# Flipper Sonar

Ambient status display for Claude Code on a Flipper Zero. Sits on your desk, shows what a Claude Code session is doing right now (model, context fill, rate limits, cost, current state) as on-screen bars and an animated sprite, and vibrates or chimes when the session finishes or needs your approval. Works regardless of where Claude Code runs: local, SSH box, or remote dev container.

It is the telemetry return channel that HID-based remotes (claupper) structurally cannot provide. claupper sends keystrokes in; Sonar pings the session and the state comes back.

---

## 1. Design constraints (read first)

- The Flipper Zero has no internet radio. Telemetry must arrive over WiFi via an ESP32 bridge on the GPIO header, or it does not arrive. USB is power or optional control only, never the data path.
- Telemetry originates inside the Claude Code process (hooks + statusline), so it works wherever Claude Code runs. The machine you look at is irrelevant.
- The Flipper has no TLS stack. All TLS terminates on the ESP32. The STM32 only ever sees plain framed UART.
- Must be plug and play for any user, not bespoke to one network. No Tailscale, no port forwarding, no homelab assumptions. A pairing code is the only thing the user carries between the two ends.
- Statusline data is turn-cadence, not streaming. Events are real-time. Treat them as two separate update rates.

---

## 2. Architecture

```
Claude Code (local / SSH / dev container)
  ├─ hooks  (events)      ─┐
  └─ statusline (stats)   ─┤ HTTPS POST, keyed by sonar ID
                           ▼
                  Relay (reverse WS tunnel, hosted or self-hosted)
                           │  topic = sonar ID
                           ▼
                  ESP32-S2 bridge on Flipper GPIO  (WiFi client, TLS terminator)
                           │  plain framed UART
                           ▼
                  Flipper FAP "sonar"  (bars + sprite + vibro/chime)
```

Three update sources collapse into one state object the FAP renders:
- Events (hooks): low latency, drive state transitions and haptics.
- Stats (statusline): per turn, drive the bars.
- Heartbeat: relay emits keepalive so the FAP can show a stale/disconnected state.

---

## 3. Components (monorepo layout)

```
/fap          Flipper Zero app, C, built with ufbt
/firmware     ESP32-S2 bridge firmware
/relay        Relay server + reverse WS tunnel
/installer    npx-runnable installer for the Claude Code side
/skill        optional Claude Code skill + /review command
/infra        IaC for the hosted relay (optional)
/docs         setup, protocol, troubleshooting
```

### 3.1 `/relay`
- Reuse the reverse WebSocket tunnel pattern from roostercoopllc/flipper-mcp (no port forwarding, NAT on both ends). Fork the tunnel, drop all device-control tooling. Sonar is telemetry only, one direction.
- Topics keyed by sonar ID (short pairing code, e.g. 6 base32 chars). One producer (the CC host), one consumer (the bridge) per topic.
- Ingest endpoint: `POST /ingest/{sonarId}` accepting the event and stat payloads in section 4. TLS required.
- Egress: WS subscription per sonar ID that the bridge connects out to.
- Emit a heartbeat every N seconds on each active topic.
- Stateless except for the last-known state per topic (so a freshly connected bridge gets an immediate snapshot). In-memory is fine; no database.
- Ship a `--self-host` mode and a Docker image. Default config can point at a hosted instance, but self-host must be a first-class, documented path for sensitive-sector users.

### 3.2 `/firmware` (ESP32-S2)
- WiFi station mode. Credentials provisioned from the FAP over UART, stored in NVS. Never hard-coded.
- Connects out to the relay WS for its sonar ID. Terminates TLS here.
- Decodes relay messages, re-frames them as the compact UART protocol in section 5, writes to the Flipper.
- Reconnect with backoff. Surface link state to the FAP (connecting / online / stale).
- Suggested stack: Rust (esp-idf-svc) to match the upstream tunnel, or ESP-IDF C. Pick one and be consistent.

### 3.3 `/fap` (Flipper Zero, C, ufbt)
- 128x64 monochrome. Borrow the ufbt scaffold and NotificationApp patterns from claupper; do not borrow its button/HID logic.
- A UART worker thread reads framed messages (section 5) and updates a shared state struct under a mutex.
- A view renders:
  - Header: model display name, link state glyph.
  - Bars: context fill %, 5h limit %, 7d limit %, and cost (cost as a bar against a user-set soft cap, with the dollar figure as text). Bars are filled rectangles via `canvas_draw_box` inside a `canvas_draw_frame`, proportional to %.
  - State + sprite: idle / working / waiting-approval / done, with a small animated dolphin/sonar sprite whose animation is driven by state (e.g. ping pulse while working, alert pose while waiting).
- Haptics + sound via NotificationApp sequences on transitions only:
  - to `waiting-approval`: distinct chime + vibration, sprite to alert.
  - to `done`: success chime + single vibration.
  - debounce so rapid PreToolUse/PostToolUse churn does not buzz.
- Settings screen: sonar ID display, WiFi provisioning entry, cost soft-cap, haptics on/off, sound on/off, which session to track when more than one is active.
- Config persistence: sonar ID, relay URL, and preferences in an SD config file so they survive reboot.
- Setup screen provisions WiFi: user types SSID + password on the Flipper keyboard, FAP pushes them to the ESP32 over UART, then displays the sonar ID/pairing code.

### 3.4 `/installer`
- Runnable as `npx github:<owner>/flipper-sonar --pair <SONAR_ID>`.
- Runs on the machine where Claude Code runs (local, SSH box, or inside the dev container). It edits that environment's Claude Code config, not the laptop's.
- Writes into the target `settings.json`:
  - the hooks block (section 4.1), pointed at the relay ingest URL for the given sonar ID,
  - the statusline wrapper (section 4.2),
  - the attribution settings to strip AI attribution (section 6).
- Idempotent: re-running updates in place, never duplicates hooks.
- `--relay <url>` to target a self-hosted relay. `--uninstall` to cleanly remove everything it added.
- Must merge into existing settings without clobbering unrelated keys.

### 3.5 `/skill` (optional)
- A `/review` project command (section 6) so the review gate is deterministic across Claude Code versions.
- Optional pairing with claupper's three-option decision skill so the waiting-approval state can show the three option labels.

---

## 4. Claude Code telemetry contract

All fields below are verified against current Claude Code docs. Treat every field as optional and null-safe.

### 4.1 Events via hooks (use the `http` hook type to POST directly to the relay)
- `Stop` -> state `done`. Fires once per turn when Claude finishes responding.
- `Notification` matcher `permission_prompt` -> state `waiting-approval`.
- `Notification` matcher `idle_prompt` -> state `waiting-input`.
- `PreToolUse` -> state `working`, optionally surface `tool_name`.
- `PostToolUse` -> stays `working`; used to keep the working animation alive.
- `SessionStart` -> reset state for that `session_id`.

Every hook payload carries `session_id`, `cwd`, `transcript_path`, `hook_event_name`. Send `session_id` and a basename-only `cwd`. Never send `transcript_path` contents.

Event payload to relay (example):
```json
{ "type": "event", "sonarId": "AB12CD", "sessionId": "...", "state": "waiting-approval", "tool": "Bash", "project": "myrepo", "ts": 1750000000 }
```

### 4.2 Stats via statusline wrapper
Claude Code pipes JSON to the statusline command on each turn. The wrapper prints the normal status line to stdout AND posts the stats to the relay. Fields:
- `model.display_name` -> header label.
- `context_window.used_percentage` -> context bar. Input-token-only metric; if you compute your own, match that formula or it will disagree with the Anthropic UI.
- `rate_limits.five_hour.used_percentage`, `rate_limits.seven_day.used_percentage` -> limit bars. May be absent on older versions or some routes. Hide the bar when absent.
- `cost.total_cost_usd` -> cost bar + text.
- `session_id` -> dimension key.

Null cases to handle: `current_usage` is null before the first API call in a session and immediately after `/compact` until the next call. Bars must hold last value or show empty, never crash or flicker to zero.

Stats payload to relay (example):
```json
{ "type": "stats", "sonarId": "AB12CD", "sessionId": "...", "model": "Opus 4.8", "ctxPct": 42, "fiveHrPct": 28, "sevenDayPct": 18, "costUsd": 4.83, "ts": 1750000000 }
```

---

## 5. UART framing (ESP32 <-> Flipper)

Keep it tiny; the display is mono and the link is a microcontroller UART.
- Byte-oriented, length-prefixed frames: `0x7E | len (1 byte) | type (1 byte) | payload | crc8`.
- Types: `0x01` stats, `0x02` event/state, `0x03` link-status, `0x04` heartbeat, `0x10` provisioning (FAP -> ESP32, WiFi creds + relay URL + sonar ID).
- Stats payload: packed bytes, one per percentage (0-100), cost as a uint16 in cents, model as a short index or a length-prefixed string.
- Reject malformed frames silently; resync on `0x7E`.
- Choose a UART channel that does not fight the Flipper CLI/expansion protocol already on USB. Document the GPIO pins used.

---

## 6. Build behaviour and git policy (non-negotiable)

These exist because this is built unattended. They go into repo config so they apply to every session.

- Strip all AI attribution from commits and PRs. Add to the project `.claude/settings.json`:
  ```json
  { "attribution": { "commit": "", "pr": "" }, "sessionUrl": false }
  ```
  (The deprecated `includeCoAuthoredBy: false` also works on older versions; the `attribution` block takes precedence.)
- Commit messages: conventional-commit style, imperative, no AI mention of any kind, no co-author trailer, no "generated with" line.
- Ship a project `/review` command at `.claude/commands/review.md` so the gate is deterministic regardless of Claude Code version. It must review staged changes for logic errors and edge cases, security issues (injected creds, the relay leaking cwd/transcript, missing TLS), null-safety against the section 4 fields, and resource/thread-safety on the FAP UART worker. `/code-review` and `/security-review` are the built-in equivalents if preferred.
- Run `/review` and resolve its findings BEFORE every commit. Do not ask the user to review.
- Conventional commits, small and atomic, push as you go.

---

## 7. Plug-and-play setup (the shipped user flow)

Standalone desk device:
1. Install the `sonar` FAP from the Flipper App Catalog.
2. Flash the ESP32-S2 WiFi Dev Board once via a browser flasher (ESP Web Tools). No toolchain.
3. Open the FAP setup screen, type WiFi SSID + password, read off the sonar ID.
4. On the machine where Claude Code runs: `npx github:<owner>/flipper-sonar --pair <SONAR_ID>`. Inside a dev container, bake it into the image or `postCreateCommand`.

Then the board + Flipper sit on USB power. Step 4 is identical for local, SSH, or container, which is what makes remote work.

Networks:
- Home/office WPA: works directly.
- Captive portal (hotel, cafe, conference): a headless ESP32 cannot complete a web sign-in. Document the answer: bring an upstream with a screen, i.e. a phone hotspot (cellular has no portal) or a travel router. The FAP setup screen should expose this as an explicit "join WPA network" path and note the hotspot workaround, so the portal case fails loudly with guidance instead of silently.

---

## 8. Acceptance criteria

- Fresh user, no prior config, completes section 7 and sees live state within one Claude Code turn.
- A `Stop` produces a chime+vibration within ~1s of the turn ending.
- A permission prompt produces the distinct approval chime and the sprite alert pose.
- Bars track context, 5h, 7d, and cost, and degrade gracefully when `rate_limits` or `current_usage` is absent.
- Works unchanged whether Claude Code runs locally, over SSH, or in a remote dev container.
- Two concurrent sessions do not make the display flap; the tracked session is selectable.
- Relay self-host path works end to end with no hosted dependency.
- No commit anywhere in history contains AI attribution.
- `cwd` never leaves the host as more than a basename; transcript content never leaves at all.

---

## 9. Prior art to reuse, not reinvent

- roostercoopllc/flipper-mcp: ESP32-S2 firmware + Rust relay with reverse WS tunnel + IaC. Fork the tunnel/transport.
- Wet-wr-Labs/claupper: ufbt FAP scaffold, NotificationApp haptic/sound patterns, optional three-option decision skill.
- xunholy/promptzero: transcript-based usage aggregation, if richer usage than statusline exposes is wanted later.
