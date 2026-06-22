#!/usr/bin/env node
/**
 * Statusline wrapper. Claude Code pipes its statusline JSON on stdin each turn.
 * This prints a normal status line to stdout AND posts the stats to the relay.
 *
 * Every field is optional and null-safe (SPEC §4.2). `current_usage` is null
 * before the first API call and right after /compact; in that case we simply
 * omit the affected metrics (sent as absent), and the FAP holds its last value —
 * it never flickers to zero. `rate_limits` may be absent on some versions/routes
 * and is likewise omitted, not zeroed.
 *
 * Usage (set by the installer):
 *   node sonar-statusline.mjs --sonar <ID> --relay <https-origin>
 */
import { basename } from "node:path";
import { request as httpsRequest } from "node:https";
import { request as httpRequest } from "node:http";

function parseArgs(argv) {
  const a = {};
  for (let i = 0; i < argv.length; i++) {
    if (argv[i].startsWith("--")) a[argv[i].slice(2)] = argv[i + 1];
  }
  return a;
}

function readStdin() {
  return new Promise((resolve) => {
    let data = "";
    let done = false;
    const finish = () => {
      if (!done) {
        done = true;
        resolve(data);
      }
    };
    process.stdin.setEncoding("utf8");
    process.stdin.on("data", (c) => (data += c));
    process.stdin.on("end", finish);
    process.stdin.on("error", finish);
    setTimeout(finish, 500);
  });
}

/** Safe nested getter; returns undefined for any missing/null link in the path. */
function get(obj, path) {
  let cur = obj;
  for (const k of path) {
    if (cur == null || typeof cur !== "object") return undefined;
    cur = cur[k];
  }
  return cur == null ? undefined : cur;
}

function post(origin, sonarId, payload) {
  return new Promise((resolve) => {
    let url;
    try {
      url = new URL(`${origin.replace(/\/$/, "")}/ingest/${sonarId}`);
    } catch {
      return resolve();
    }
    const body = Buffer.from(JSON.stringify(payload));
    const isHttps = url.protocol === "https:";
    const req = (isHttps ? httpsRequest : httpRequest)(
      url,
      {
        method: "POST",
        headers: {
          "content-type": "application/json",
          "content-length": body.length,
        },
        timeout: 2000,
      },
      (res) => {
        res.resume();
        res.on("end", resolve);
      },
    );
    req.on("error", () => resolve());
    req.on("timeout", () => {
      req.destroy();
      resolve();
    });
    req.write(body);
    req.end();
  });
}

/** Build the stats payload, omitting any metric that is absent/null. */
export function buildStats(sonarId, j) {
  const payload = {
    type: "stats",
    sonarId,
    sessionId: get(j, ["session_id"]),
    ts: Math.floor(Date.now() / 1000),
  };
  const model = get(j, ["model", "display_name"]);
  if (typeof model === "string") payload.model = model;

  const ctx = get(j, ["context_window", "used_percentage"]);
  if (typeof ctx === "number") payload.ctxPct = ctx;

  const five = get(j, ["rate_limits", "five_hour", "used_percentage"]);
  if (typeof five === "number") payload.fiveHrPct = five;

  const seven = get(j, ["rate_limits", "seven_day", "used_percentage"]);
  if (typeof seven === "number") payload.sevenDayPct = seven;

  const cost = get(j, ["cost", "total_cost_usd"]);
  if (typeof cost === "number") payload.costUsd = cost;

  const cwd = get(j, ["cwd"]) ?? get(j, ["workspace", "current_dir"]);
  if (typeof cwd === "string") payload.project = basename(cwd);

  return payload;
}

/** The human-readable status line printed to stdout. */
export function renderLine(j) {
  const model = get(j, ["model", "display_name"]) ?? "Claude";
  const ctx = get(j, ["context_window", "used_percentage"]);
  const cost = get(j, ["cost", "total_cost_usd"]);
  const cwd = get(j, ["cwd"]) ?? get(j, ["workspace", "current_dir"]);
  const parts = [model];
  if (typeof cwd === "string") parts.push(basename(cwd));
  if (typeof ctx === "number") parts.push(`ctx ${Math.round(ctx)}%`);
  if (typeof cost === "number") parts.push(`$${cost.toFixed(2)}`);
  return parts.join("  |  ");
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const { sonar, relay } = args;
  const raw = await readStdin();
  let j = {};
  try {
    j = raw ? JSON.parse(raw) : {};
  } catch {
    j = {};
  }

  // Always print the status line, even if telemetry fails.
  process.stdout.write(renderLine(j) + "\n");

  if (sonar && relay) {
    await post(relay, sonar, buildStats(sonar, j));
  }
  process.exit(0);
}

// Only run when executed directly (so tests can import buildStats/renderLine).
if (import.meta.url === `file://${process.argv[1]}`) {
  main().catch(() => process.exit(0));
}
