/**
 * Relay server: HTTP(S) ingest + WebSocket egress.
 *
 *  - POST /ingest/{claudeogotchiId}   producer -> relay (hooks + statusline)
 *  - GET  /egress/{claudeogotchiId}   relay -> bridge (WS; bridge dials OUT, so no port
 *                             forwarding is needed on the bridge side)
 *  - GET  /healthz            liveness
 *
 * TLS terminates here. In hosted mode you run behind a TLS-terminating proxy or
 * pass key/cert to listen over HTTPS directly. The ingest path REQUIRES a secure
 * transport unless explicitly allowed for local self-host testing.
 */

import http from "node:http";
import https from "node:https";
import { WebSocketServer } from "ws";
import type { WebSocket } from "ws";
import { TopicRegistry } from "./topics.ts";
import type { Subscriber } from "./topics.ts";
import { normalizeIngest, isValidClaudeogotchiId, ProtocolError } from "./protocol.ts";

export interface RelayOptions {
  port: number;
  host?: string;
  heartbeatMs?: number;
  /** TLS material for direct HTTPS (otherwise run behind a TLS proxy). */
  tls?: { key: Buffer; cert: Buffer };
  /**
   * Allow plaintext ingest. Only for local self-host / tests. When false and no
   * TLS is configured, the relay assumes a TLS-terminating proxy sets
   * `x-forwarded-proto: https` and rejects requests that aren't forwarded https.
   */
  allowInsecure?: boolean;
  /** Max ingest body size in bytes. */
  maxBody?: number;
}

const INGEST_RE = /^\/ingest\/([^/]+)\/?$/;
const EGRESS_RE = /^\/egress\/([^/]+)\/?$/;

export interface RelayHandle {
  registry: TopicRegistry;
  close(): Promise<void>;
  port: number;
}

export function createRelay(opts: RelayOptions): Promise<RelayHandle> {
  const registry = new TopicRegistry(opts.heartbeatMs ?? 10_000);
  const maxBody = opts.maxBody ?? 8 * 1024;
  const allowInsecure = opts.allowInsecure ?? false;
  const usingTls = !!opts.tls;

  const handler = (req: http.IncomingMessage, res: http.ServerResponse) => {
    const url = req.url ?? "/";

    if (req.method === "GET" && (url === "/healthz" || url === "/")) {
      res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify({ ok: true, topics: registry.topicCount() }));
      return;
    }

    const ingest = req.method === "POST" && INGEST_RE.exec(url.split("?")[0]);
    if (ingest) {
      const claudeogotchiId = ingest[1];
      // Enforce secure transport on the ingest path.
      const forwardedHttps =
        (req.headers["x-forwarded-proto"] || "")
          .toString()
          .split(",")[0]
          .trim() === "https";
      const secure = usingTls || forwardedHttps;
      if (!secure && !allowInsecure) {
        return json(res, 426, { error: "TLS required for ingest" });
      }
      if (!isValidClaudeogotchiId(claudeogotchiId)) {
        return json(res, 400, { error: "invalid claudeogotchiId" });
      }
      readBody(req, maxBody)
        .then((buf) => {
          let parsed: unknown;
          try {
            parsed = JSON.parse(buf.toString("utf8"));
          } catch {
            return json(res, 400, { error: "invalid JSON" });
          }
          try {
            const msg = normalizeIngest(claudeogotchiId, parsed);
            registry.publish(msg);
            return json(res, 202, { ok: true });
          } catch (e) {
            if (e instanceof ProtocolError) {
              return json(res, 422, { error: e.message });
            }
            return json(res, 500, { error: "internal error" });
          }
        })
        .catch((e) => {
          const code = e?.code === "BODY_TOO_LARGE" ? 413 : 400;
          return json(res, code, { error: e?.message ?? "bad request" });
        });
      return;
    }

    json(res, 404, { error: "not found" });
  };

  const server = usingTls
    ? https.createServer({ key: opts.tls!.key, cert: opts.tls!.cert }, handler)
    : http.createServer(handler);

  // WS egress: bridge connects out to /egress/{claudeogotchiId}.
  const wss = new WebSocketServer({ noServer: true });
  const subUnsubscribers = new WeakMap<WebSocket, () => void>();

  server.on("upgrade", (req, socket, head) => {
    const url = req.url ?? "";
    const m = EGRESS_RE.exec(url.split("?")[0]);
    if (!m || !isValidClaudeogotchiId(m[1])) {
      socket.write("HTTP/1.1 400 Bad Request\r\n\r\n");
      socket.destroy();
      return;
    }
    const claudeogotchiId = m[1];
    wss.handleUpgrade(req, socket, head, (ws) => {
      const sub: Subscriber = {
        send: (data) => ws.send(data),
        get closed() {
          return ws.readyState !== ws.OPEN;
        },
      };
      const unsub = registry.subscribe(claudeogotchiId, sub);
      subUnsubscribers.set(ws, unsub);
      ws.on("close", () => unsub());
      ws.on("error", () => unsub());
      // Telemetry is one-directional; ignore anything the bridge sends.
      ws.on("message", () => {});
    });
  });

  registry.start();

  return new Promise((resolve) => {
    server.listen(opts.port, opts.host ?? "0.0.0.0", () => {
      const addr = server.address();
      const port = typeof addr === "object" && addr ? addr.port : opts.port;
      resolve({
        registry,
        port,
        close: () =>
          new Promise<void>((res) => {
            registry.stop();
            for (const ws of wss.clients) ws.terminate();
            wss.close(() => server.close(() => res()));
          }),
      });
    });
  });
}

function json(res: http.ServerResponse, code: number, body: unknown): void {
  res.writeHead(code, { "content-type": "application/json" });
  res.end(JSON.stringify(body));
}

function readBody(req: http.IncomingMessage, max: number): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    let size = 0;
    req.on("data", (c: Buffer) => {
      size += c.length;
      if (size > max) {
        const err = new Error("body too large") as Error & { code: string };
        err.code = "BODY_TOO_LARGE";
        req.destroy();
        reject(err);
        return;
      }
      chunks.push(c);
    });
    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", reject);
  });
}
