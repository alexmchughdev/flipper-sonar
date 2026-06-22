# Flipper Sonar bridge firmware (ESP32-S2)

WiFi station that dials **out** to the relay over WSS, **terminates TLS here**,
and re-frames relay messages as the compact UART protocol the Flipper speaks.
The Flipper never sees TLS and never sees the network.

## Responsibilities

- Station-mode WiFi. Credentials are provisioned from the FAP over UART and
  stored in NVS — never hard-coded (`nvs_store.c`, `wifi.c`).
- Connect out to `<relay_url>/egress/<sonar_id>` over `wss://`, verifying the
  relay certificate against the bundled CA set (`ws_client.c`).
- Decode relay JSON → compact UART frames (`proto/sonar_uart.h`), null-safe for
  every telemetry field.
- Surface link state to the FAP: `connecting` / `online` / `stale`.
- Reconnect with bounded backoff (WiFi in `wifi.c`, WS via the client's own
  reconnect).

## UART wiring

USART (`UART_NUM_1` by default) at 115200 8N1. The ESP32-side TX/RX pins
(`SONAR_UART_TX_PIN` / `SONAR_UART_RX_PIN`, default GPIO17/18) must be wired to
Flipper expansion header **pin 13 (RX)** and **pin 14 (TX)** — ESP TX → Flipper
RX (pin 14) and ESP RX → Flipper TX (pin 13). Share GND. See
[`../docs/protocol.md`](../docs/protocol.md). Override the pins at build time if
your dev board routes them differently:

```bash
idf.py build -DSONAR_UART_TX_PIN=17 -DSONAR_UART_RX_PIN=18
```

## Build & flash (toolchain)

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s2
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Flash without a toolchain (end users)

Use [ESP Web Tools](https://esphome.github.io/esp-web-tools/) with
`manifest.json` after building once. Host `manifest.json` alongside
`build/bootloader/bootloader.bin`, `build/partition_table/partition-table.bin`,
and `build/sonar_bridge.bin` (rename to the paths in the manifest) on any static
host and open it in a Chromium browser. See [blocked.md](../docs/blocked.md) for
the hosting status.

## Provisioning

On first boot with no stored config the bridge waits, reporting `connecting`.
The FAP setup screen sends an `0x10` provisioning frame (SSID, password, relay
URL, sonar ID); the bridge persists it to NVS and connects. Re-provisioning at
runtime is applied live.
