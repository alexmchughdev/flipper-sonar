# Protocol reference

Two protocols. The **relay wire protocol** (JSON over HTTPS/WSS) between the
Claude Code host and the bridge, and the **UART framing** (compact binary)
between the ESP32-S2 bridge and the Flipper STM32.

---

## 1. Relay wire protocol (host ↔ relay ↔ bridge)

### Ingest — `POST /ingest/{sonarId}` (TLS required)

Event payload (from hooks):

```json
{ "type": "event", "sessionId": "...", "state": "waiting-approval",
  "tool": "Bash", "project": "myrepo", "ts": 1750000000 }
```

Stats payload (from the statusline wrapper):

```json
{ "type": "stats", "sessionId": "...", "model": "Opus 4.8",
  "ctxPct": 42, "fiveHrPct": 28, "sevenDayPct": 18, "costUsd": 4.83,
  "ts": 1750000000 }
```

`states`: `idle | working | waiting-approval | waiting-input | done`.

**Privacy (enforced in `relay/src/protocol.ts`):** `project` is the only
path-derived field allowed. A full `cwd` is reduced to its basename and the full
path dropped. `transcript_path` / `transcript` content is dropped
unconditionally. Every metric is optional and null-safe.

### Egress — `GET /egress/{sonarId}` (WebSocket)

The bridge dials **out** to this (no port forwarding). The relay immediately
replays the last stats + last event per session (snapshot), then streams new
messages plus a `heartbeat` every ~10s:

```json
{ "type": "heartbeat", "sonarId": "AB12CD", "ts": 1750000000 }
```

Each forwarded message carries a `session` field (0-based slot) so the FAP can
track a chosen session without flapping when several are active.

---

## 2. UART framing (ESP32 ↔ Flipper)

Canonical definition: [`proto/sonar_uart.h`](../proto/sonar_uart.h) (C) and
[`proto/sonar_uart.ts`](../proto/sonar_uart.ts) (mirror). A CI diff
(`proto/test/gen_frames.*`) proves the two are byte-identical.

### Physical layer

- **USART1**, GPIO **pin 13 = TX**, **pin 14 = RX** on the Flipper expansion
  header (ESP32 RX ← Flipper TX and vice-versa). 3V3/GND from the same header.
- **115200 baud, 8N1**, no flow control.
- Chosen to avoid the LPUART the Flipper CLI uses on the USB-C/expansion bridge,
  so USB stays free for power or CLI.

### Frame

```
0x7E | LEN | TYPE | PAYLOAD[LEN-1] | CRC8
```

- `LEN` = payload length + 1 (covers `TYPE` + `PAYLOAD`), range 1..255.
- `CRC8` = poly **0x31**, MSB-first, init **0x00**, over `TYPE`+`PAYLOAD` only.
- Multi-byte integers little-endian.
- Malformed frames are dropped silently; the parser resyncs on the next `0x7E`.

### Types

| Type | Dir | Name | Payload |
|------|-----|------|---------|
| `0x01` | ESP→FAP | stats | `session, flags, ctx, 5h, 7d, costLo, costHi, modelLen, model…` |
| `0x02` | ESP→FAP | event | `session, state, toolLen, tool…, projLen, proj…` |
| `0x03` | ESP→FAP | link | `link` (0 connecting / 1 online / 2 stale) |
| `0x04` | ESP→FAP | heartbeat | *(empty)* |
| `0x10` | FAP→ESP | provision | `ssidLen, ssid…, passLen, pass…, urlLen, url…, idLen, id…` |

**Stats `flags`** bitfield marks which metrics are present:
`0x01 ctx`, `0x02 5h`, `0x04 7d`, `0x08 cost`, `0x10 model`. A cleared bit means
*absent* — the FAP holds the last value or shows the bar empty, never zero-flaps.
Cost is a little-endian uint16 in **cents**.

### Worked examples (from `gen_frames`)

```
stats_full   7e 11 01 00 1f 2a 1c 12 e3 01 08 4f70757320342e38 f5
             SOF len ty se fl ct 5h 7d  cost  ml  "Opus 4.8"     crc
heartbeat    7e 01 04 c4           (SOF, len=1, type=0x04, crc)
link_online  7e 02 03 01 1c        (SOF, len=2, type=0x03, link=online, crc)
```
