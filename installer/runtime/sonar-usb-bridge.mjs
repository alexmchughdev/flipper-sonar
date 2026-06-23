#!/usr/bin/env node
/**
 * No-board USB bridge. Does the ESP32's job in software on whatever machine the
 * Flipper is plugged into: subscribes to the relay's egress WebSocket over the
 * network, re-frames each message into the compact UART protocol, and writes the
 * bytes to the Flipper's USB serial port. The FAP must be in Link=USB mode.
 *
 * Works for local, SSH, and dev-container Claude Code, because THIS machine
 * reaches the relay over the network exactly like the ESP32 would.
 *
 *   node sonar-usb-bridge.mjs --sonar AB12CD --relay wss://my.relay
 *   node sonar-usb-bridge.mjs --sonar AB12CD --relay http://192.168.1.50:8787 --port /dev/cu.usbmodemXXXX
 *
 * Zero dependencies (Node stdlib + global WebSocket). The protocol encoder is
 * inlined here so the deployed script is standalone (mirror of proto/sonar_uart).
 */
import fs from "node:fs";
import os from "node:os";
import { execFileSync } from "node:child_process";

// ---- args ----
function parseArgs(argv) {
  const a = {};
  for (let i = 0; i < argv.length; i++) {
    if (argv[i].startsWith("--")) {
      a[argv[i].slice(2)] = argv[i + 1];
      i++;
    }
  }
  return a;
}
const args = parseArgs(process.argv.slice(2));
const sonarId = args.sonar;
if (!sonarId) {
  console.error("usage: sonar-usb-bridge --sonar <ID> --relay <url> [--port <dev>]");
  process.exit(1);
}

/** relay arg (any scheme) -> ws/wss origin for egress. */
function wsOrigin(input) {
  let s = String(input || "ws://127.0.0.1:8787").trim();
  if (!/^[a-z]+:\/\//i.test(s)) s = "ws://" + s;
  const u = new URL(s);
  const proto =
    u.protocol === "https:" || u.protocol === "wss:" ? "wss:" : "ws:";
  return `${proto}//${u.host}`;
}
const egressUrl = `${wsOrigin(args.relay)}/egress/${sonarId}`;

// ---- protocol encoder (mirror of proto/sonar_uart.h) ----
const SOF = 0x7e, T_STATS = 0x01, T_EVENT = 0x02, T_HEARTBEAT = 0x04;
const STATE = { idle: 0, working: 1, "waiting-approval": 2, "waiting-input": 3, done: 4 };
const F_CTX = 1, F_5H = 2, F_7D = 4, F_COST = 8, F_MODEL = 16, F_TOKENS = 32;
const MAX_STR = 32;

function crc8(bytes) {
  let crc = 0;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++) crc = crc & 0x80 ? ((crc << 1) ^ 0x31) & 0xff : (crc << 1) & 0xff;
  }
  return crc & 0xff;
}
function encode(type, payload) {
  const len = payload.length + 1;
  const out = Buffer.alloc(4 + payload.length);
  out[0] = SOF; out[1] = len; out[2] = type;
  Buffer.from(payload).copy(out, 3);
  out[3 + payload.length] = crc8(out.subarray(2, 2 + len));
  return out;
}
function pushStr(arr, s, max = MAX_STR) {
  const b = s ? Array.from(Buffer.from(String(s), "utf8")).slice(0, max) : [];
  arr.push(b.length, ...b);
}
function clampPct(v) {
  return v == null || !Number.isFinite(v) ? 0 : Math.max(0, Math.min(100, Math.round(v)));
}
function reframe(msg) {
  const session = msg.session ?? 0;
  if (msg.type === "stats") {
    const pl = [session];
    const fi = pl.length; pl.push(0);
    let flags = 0;
    pl.push(clampPct(msg.ctxPct)); if (msg.ctxPct != null) flags |= F_CTX;
    pl.push(clampPct(msg.fiveHrPct)); if (msg.fiveHrPct != null) flags |= F_5H;
    pl.push(clampPct(msg.sevenDayPct)); if (msg.sevenDayPct != null) flags |= F_7D;
    const cents = msg.costUsd != null ? Math.min(65535, Math.max(0, Math.round(msg.costUsd * 100))) : 0;
    pl.push(cents & 0xff, (cents >> 8) & 0xff); if (msg.costUsd != null) flags |= F_COST;
    if (msg.model) flags |= F_MODEL;
    pushStr(pl, msg.model);
    const th = msg.tokens != null ? Math.min(65535, Math.max(0, Math.round(msg.tokens / 100))) : 0;
    pl.push(th & 0xff, (th >> 8) & 0xff);
    if (msg.tokens != null) flags |= F_TOKENS;
    pl[fi] = flags;
    return encode(T_STATS, pl);
  }
  if (msg.type === "event") {
    const pl = [session, STATE[msg.state] ?? 0];
    pushStr(pl, msg.tool);
    pushStr(pl, msg.project);
    return encode(T_EVENT, pl);
  }
  if (msg.type === "heartbeat") return encode(T_HEARTBEAT, []);
  return null;
}

// ---- serial port ----
function detectPort() {
  if (args.port) return args.port;
  const dir = "/dev";
  let files = [];
  try {
    files = fs.readdirSync(dir);
  } catch {
    return null;
  }
  const plat = os.platform();
  const candidates = files
    .filter((f) =>
      plat === "darwin"
        ? f.startsWith("cu.usbmodem")
        : f.startsWith("ttyACM") || f.startsWith("ttyUSB"),
    )
    .map((f) => `${dir}/${f}`);
  // Prefer a Flipper-looking node.
  candidates.sort((a, b) => (b.includes("flip") ? 1 : 0) - (a.includes("flip") ? 1 : 0));
  return candidates[0] || null;
}

function makeRaw(port) {
  // Disable line discipline / output post-processing so binary frames pass
  // through unmangled. macOS uses -f, Linux uses -F.
  const flag = os.platform() === "darwin" ? "-f" : "-F";
  try {
    execFileSync("stty", [flag, port, "raw", "-echo", "115200"], { stdio: "ignore" });
  } catch {
    /* CDC ignores baud; raw is the important part. Non-fatal if stty missing. */
  }
}

let portStream = null;
let portPath = null;
function openPort() {
  const p = detectPort();
  if (!p) return false;
  if (p === portPath && portStream) return true;
  portPath = p;
  makeRaw(p);
  try {
    portStream = fs.createWriteStream(p);
    portStream.on("error", () => {
      portStream = null;
    });
    console.log(`[usb-bridge] writing to ${p}`);
    return true;
  } catch (e) {
    console.error(`[usb-bridge] cannot open ${p}: ${e.message}`);
    portStream = null;
    return false;
  }
}

function writeFrame(buf) {
  if (!portStream && !openPort()) return;
  try {
    portStream.write(buf);
  } catch {
    portStream = null;
  }
}

// ---- relay subscription with reconnect ----
let ws = null;
let backoff = 1000;
function connect() {
  console.log(`[usb-bridge] connecting ${egressUrl}`);
  ws = new WebSocket(egressUrl);
  ws.onopen = () => {
    backoff = 1000;
    console.log("[usb-bridge] relay connected");
    openPort();
  };
  ws.onmessage = (ev) => {
    let msg;
    try {
      msg = JSON.parse(typeof ev.data === "string" ? ev.data : ev.data.toString());
    } catch {
      return;
    }
    const frame = reframe(msg);
    if (frame) writeFrame(frame);
  };
  ws.onclose = () => scheduleReconnect();
  ws.onerror = () => {
    try {
      ws.close();
    } catch {
      /* ignore */
    }
  };
}
function scheduleReconnect() {
  console.log(`[usb-bridge] relay disconnected; retry in ${backoff}ms`);
  setTimeout(connect, backoff);
  backoff = Math.min(backoff * 2, 30000);
}

console.log(`[usb-bridge] sonar ${sonarId} -> ${egressUrl}`);
connect();
