# End-to-end tests

Proves the SPEC §8 acceptance criteria that don't require physical hardware, by
running the **real** host-side scripts and the **real** relay, and reframing to
UART with the **shared** protocol definition.

```
claudeogotchi-hook.mjs / claudeogotchi-statusline.mjs   (real, spawned with §4 payloads on stdin)
        │  HTTP POST
        ▼
   relay  (real, self-host mode, ephemeral port)
        │  WS egress
        ▼
   MockBridge   reframes JSON -> UART via proto/claudeogotchi_uart.ts (mirror of the
        │       firmware's claudeogotchi_uart.h reframe in ws_client.c)
        ▼
   MockFap      decodes with the shared parser, applies null-safe hold-last
```

## What is proven

| SPEC §8 criterion | Test |
|---|---|
| Live state within one turn | statusline stats reach the FAP bars |
| `Stop` → chime within ~1s | done frame arrives < 1s |
| Permission prompt → approval alert | waiting-approval state + transition |
| Bars degrade gracefully when data absent | null-safety hold-last test |
| Snapshot on (re)connect | last state replayed to a fresh bridge |
| Two sessions don't flap; selectable | distinct slots, tracked unaffected |
| Self-host works with no hosted dep | whole suite runs on a local relay |
| `cwd`/transcript never leave | privacy test inspects the raw wire bytes |

Haptic/sprite behaviour and on-Flipper rendering require hardware; see
BUILD_REPORT.md.

## Run

```bash
node --test --experimental-strip-types e2e/*.test.mjs
# or from repo root:
npm run test:e2e
```

Requires the relay's deps to be installed once (`cd relay && npm install`) since
the test imports the real relay server.
