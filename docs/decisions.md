# Engineering decisions

Consequential choices made during the build where SPEC.md was silent. One line
of context each; this is a log, not a discussion.

## Relay
- **Language: TypeScript on Node.** SPEC says "fork the Rust tunnel from
  flipper-mcp" but the *contract* (reverse WS, topic = claudeogotchi ID, ingest POST,
  heartbeat, last-state snapshot) is what matters and is reproduced faithfully.
  TS keeps the relay, installer, and e2e tests in one toolchain, lowers the
  contribution bar, and the transport is a thin `ws` server. The reverse-WS
  pattern (bridge dials out, no port forwarding) is preserved exactly.
- **No database.** Last-known state per topic held in memory, as specified.
- **Heartbeat interval: 10s.** FAP marks link stale after 3 missed (~30s).

## Firmware (ESP32-S2)
- **Language: ESP-IDF C** rather than Rust. Matches the FAP's C toolchain,
  smaller flash footprint, and ESP Web Tools flashing is well-trodden for
  ESP-IDF. The build is documented but CI-built artifacts are not produced here
  (no hardware in the loop); see BUILD_REPORT.md.

## UART
- **UART channel: USART1 on GPIO pins 13 (TX) / 14 (RX)** of the Flipper
  expansion header, 115200 8N1. Chosen to avoid the LPUART used by the Flipper
  CLI on the USB-C/expansion bridge. Documented in docs/protocol.md.
- **CRC-8/MAXIM (poly 0x31, reflected)** for the frame checksum — tiny table-free
  implementation on both sides.

## FAP
- **Single app, multiple views** via ViewDispatcher: main (bars+sprite),
  settings, wifi-provisioning, text-input. Worker thread owns UART RX.

## Installer
- **Pure Node, zero runtime deps** so `npx github:...` works without an install
  step. Uses only Node stdlib (fs, path, https). JSON settings merge is
  hand-rolled to avoid clobbering unrelated keys.

## Installer / hooks
- **Command-type hooks running a host-side poster**, not raw `http` hooks. SPEC
  §4.1 suggests the `http` hook type, but the hard rule "cwd never leaves the
  host as more than a basename, enforced in code" requires stripping to happen
  ON the host before transmission. A tiny zero-dep Node poster
  (`claudeogotchi-hook.mjs`) does the basename reduction and never reads
  `transcript_path`, then POSTs. The relay enforces the same contract again as
  defense in depth.
- **User-level settings by default** (`~/.claude/settings.json`) so telemetry
  works from any cwd; `--project` targets `./.claude/settings.json`.
- **Sidecar manifest** (`~/.claude/flipper-claudeogotchi/install.json`) records exactly
  what keys/hooks the installer added, so `--uninstall` is precise and never
  removes the user's unrelated hooks or settings.

## Product / FAP UI (post-hardware iteration)
- **Renamed to "Flipper Claudeogotchi"** (appid `claudeogotchi`); the on-screen
  character is **Clawd**, Claude Code's 8-bit mascot, an original 1-bit creature inspired by the Claude Code
  art (flat-top body, square side tabs sized so head-above = 2× tab height and
  gap-below = tab height, two thin wide-set eye slits, four symmetric legs with
  the outer pair flush to the body).
- **Bars are session/5h/weekly usage, no cost.** Usage is the meaningful signal
  on any plan (cost is just an estimate on subscriptions). Cost plumbing was
  removed from the UI.
- **Spinner verbs**: the full ~106 Claude Code spinner verbs, chosen at random
  per work-session and re-rolled ~every 30s. **Run timer** is on-device from a
  `work_start_tick`. **Output tokens** added end-to-end (proto + relay +
  firmware + statusline best-effort) and shown like the CLI (`7.3k`).
- **Bottom bar only shows when something is happening** (working/input/approval/
  done); idle hides it. Attention shows as a blinking `!` (input) / `?`
  (approval) beside Clawd. **Done** plays a brief `> <` happy face (~3s, tracked
  via `state_tick`) then reverts to the normal face.
- **Sprite tuning is done off-device** with Pillow preview scripts
  (`fap/tools_clawd_preview.py`, `fap/tools_screen_preview.py`) rendered and
  compared to the reference before flashing — far cheaper than guess-and-flash.

## No-board USB mode
- **Link setting: Board (GPIO/ESP32) or USB.** USB mode lets the Flipper run
  with no WiFi board: a host-side `claudeogotchi-usb-bridge.mjs` subscribes to the relay
  and writes UART frames to the Flipper's USB serial. Works for remote Claude
  Code too, since the bridge machine does the networking.
- **USB mode uses the dual CDC config**, not single. The Flipper CLI/RPC stays
  alive on channel 0 (so ufbt/qFlipper keep working and the app can be reflashed)
  while Clawd reads telemetry on channel 1. Single-CDC takeover wedged the CLI
  until a replug — dual CDC fixes that. The bridge targets the higher-numbered
  serial node (channel 1).

## Build/flash
- **Device runs Momentum firmware `mntm-011` (API 86).** The FAP is built with
  the Momentum SDK pinned to that release (`ufbt update --url …mntm-011…`) so
  there's no "app too new" prompt. See [[../README]] / Makefile.

## Session selection
- When multiple `session_id`s post to one claudeogotchi ID, the relay forwards all but
  tags each frame with a 1-byte session slot. The FAP tracks a selectable
  session; default is most-recently-active.
