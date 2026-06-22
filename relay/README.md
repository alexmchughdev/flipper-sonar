# @flipper-sonar/relay

Telemetry-only reverse WebSocket tunnel. One topic per sonar ID, one producer
(the Claude Code host) and one or more consumers (ESP32 bridges) per topic.
Stateless except for the last-known state per topic, held in memory so a freshly
connected bridge gets an immediate snapshot. No database.

This is a fork *of the transport pattern* from roostercoopllc/flipper-mcp's
reverse WS tunnel, with all device-control tooling removed — Sonar is one
direction only. See [decisions.md](../docs/decisions.md) for why it's TypeScript.

## Endpoints

| Method | Path                | Direction        | Notes |
|--------|---------------------|------------------|-------|
| POST   | `/ingest/{sonarId}` | host → relay     | event + stats payloads (SPEC §4). TLS required. |
| GET    | `/egress/{sonarId}` | relay → bridge   | WS; the bridge dials **out**, so no port forwarding. |
| GET    | `/healthz`          | —                | liveness + topic count |

## Run

```bash
npm install

# Local self-host (allows plaintext ingest on a trusted network):
npm start -- --self-host

# Behind a TLS-terminating proxy (production hosted):
npm start                          # expects x-forwarded-proto: https on ingest

# Direct HTTPS (relay terminates TLS itself):
npm start -- --tls-cert cert.pem --tls-key key.pem
```

Requires Node ≥ 22.6 (runs the TypeScript sources directly via native type
stripping — no build step).

## Self-host with Docker

```bash
docker compose up --build      # relay + Caddy TLS terminator (edit Caddyfile)
```

Or just the relay on a trusted LAN:

```bash
docker build -t sonar-relay .
docker run -p 8787:8787 sonar-relay --self-host
```

## Privacy

The relay enforces the privacy contract in code (`src/protocol.ts`):

- `cwd` is reduced to a **basename** (`project`); the full path is dropped even
  if a host mistakenly sends it.
- `transcript_path`, `transcriptPath`, and any `transcript` content are dropped
  unconditionally and never appear on the wire.

## Tests

```bash
npm test       # node --test, covers normalization/framing + topic routing
```
