# Flipper Claudeogotchi FAP

The Flipper Zero app. 128×64 mono. Renders one state object fed by the UART
worker: header (model + link glyph), four bars (context, 5h, 7d, cost vs soft
cap), and a state-driven sprite, with haptics/chime on transitions.

## Modules

| File | Role |
|------|------|
| `claudeogotchi.c` | app lifecycle, ViewDispatcher, menu, claudeogotchi-ID minting |
| `claudeogotchi_worker.c` | UART RX worker thread + ISR; updates shared state under a mutex |
| `claudeogotchi_main_view.c` | bars + sprite + header; animation timer |
| `claudeogotchi_settings.c` | cost cap, haptics, sound, tracked session, claudeogotchi ID |
| `claudeogotchi_wifi_setup.c` | SSID/password entry → provisioning frame → pairing code |
| `claudeogotchi_notifications.c` | debounced haptic/sound sequences on transitions |
| `claudeogotchi_config.c` | SD-card persistence (`flipper_format`) |
| `claudeogotchi_state.h` | the shared, mutex-guarded model |

The wire framing is the shared [`proto/claudeogotchi_uart.h`](../proto/claudeogotchi_uart.h),
included verbatim (no copy).

## Thread-safety

- The RX **ISR** only does `furi_stream_buffer_send` (ISR-safe) — no mutex, no
  thread flags.
- The **worker** drains the stream buffer, parses frames, and is the only writer
  of the shared model, always under `app->mutex`.
- The **view** snapshots the model under the mutex into a local copy, then draws
  from the copy — the critical section never spans rendering.
- Lock order is acyclic: the worker takes only `app->mutex`; the view takes the
  view-model lock then `app->mutex` (a leaf). No deadlock.

## Build & run (ufbt)

```bash
python3 -m pip install --user ufbt
cd fap
ufbt            # build the .fap (downloads the matching SDK on first run)
ufbt launch     # build, upload, and start on a connected Flipper
```

The shared header is the single source of truth in `proto/claudeogotchi_uart.h`. The FAP
includes a one-line shim (`fap/claudeogotchi_uart.h`) that pulls it in via a relative
path, so there is no copy to drift and no special ufbt include config needed.

## Wiring

USART on expansion **pin 13 (TX) / pin 14 (RX)**, 115200 8N1, to the ESP32-S2
bridge. See [../docs/protocol.md](../docs/protocol.md).

## UI

- **Live View**: bars + sprite. Left/Right cycles the tracked session among
  those seen, so concurrent sessions never make the display flap.
- **WiFi Setup**: type SSID then password (WPA only — captive portals need a
  phone hotspot; the app says so). Then it shows your claudeogotchi ID / pairing code.
- **Settings**: claudeogotchi ID, cost soft-cap, haptics, sound, tracked session,
  captive-portal help.
