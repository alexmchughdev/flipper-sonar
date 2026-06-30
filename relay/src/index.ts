#!/usr/bin/env node
/**
 * Relay CLI entry point.
 *
 *   node src/index.ts                 # plaintext on :8787 (behind a TLS proxy)
 *   node src/index.ts --self-host     # local self-host, allows insecure ingest
 *   node src/index.ts --tls-cert c.pem --tls-key k.pem   # direct HTTPS
 *
 * Env: PORT, HOST, HEARTBEAT_MS, ALLOW_INSECURE=1, TLS_CERT, TLS_KEY.
 */

import fs from "node:fs";
import { createRelay } from "./server.ts";
import type { RelayOptions } from "./server.ts";

function parseArgs(argv: string[]): Record<string, string | boolean> {
  const out: Record<string, string | boolean> = {};
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a.startsWith("--")) {
      const key = a.slice(2);
      const next = argv[i + 1];
      if (next && !next.startsWith("--")) {
        out[key] = next;
        i++;
      } else {
        out[key] = true;
      }
    }
  }
  return out;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const selfHost = !!args["self-host"];

  const port = Number(args.port ?? process.env.PORT ?? 8787);
  const host = (args.host as string) ?? process.env.HOST ?? "0.0.0.0";
  const heartbeatMs = Number(
    args.heartbeat ?? process.env.HEARTBEAT_MS ?? 10_000,
  );

  const certPath = (args["tls-cert"] as string) ?? process.env.TLS_CERT;
  const keyPath = (args["tls-key"] as string) ?? process.env.TLS_KEY;

  const opts: RelayOptions = {
    port,
    host,
    heartbeatMs,
    allowInsecure: selfHost || process.env.ALLOW_INSECURE === "1",
  };

  if (certPath && keyPath) {
    opts.tls = {
      cert: fs.readFileSync(certPath),
      key: fs.readFileSync(keyPath),
    };
  }

  const relay = await createRelay(opts);
  const scheme = opts.tls ? "https/wss" : "http/ws";
  // eslint-disable-next-line no-console
  console.log(
    `[claudeogotchi-relay] listening ${scheme} on ${host}:${relay.port} ` +
      `(${selfHost ? "self-host" : "hosted"}, heartbeat ${heartbeatMs}ms)`,
  );
  if (!opts.tls && !opts.allowInsecure) {
    console.log(
      "[claudeogotchi-relay] ingest requires x-forwarded-proto: https (run behind a TLS proxy)",
    );
  }

  const shutdown = async () => {
    await relay.close();
    process.exit(0);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
}

main().catch((e) => {
  console.error("[claudeogotchi-relay] fatal:", e);
  process.exit(1);
});
