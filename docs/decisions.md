# Engineering decisions

Consequential choices made during the build where SPEC.md was silent. One line
of context each; this is a log, not a discussion.

## Relay
- **Language: TypeScript on Node.** SPEC says "fork the Rust tunnel from
  flipper-mcp" but the *contract* (reverse WS, topic = sonar ID, ingest POST,
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

## Session selection
- When multiple `session_id`s post to one sonar ID, the relay forwards all but
  tags each frame with a 1-byte session slot. The FAP tracks a selectable
  session; default is most-recently-active.
