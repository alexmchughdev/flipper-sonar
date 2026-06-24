/**
 * Mock ESP32 bridge + mock FAP for end-to-end tests, with no hardware.
 *
 * The bridge connects out to the relay's egress WS exactly as the firmware does,
 * then re-frames each relay JSON message into the compact UART protocol using
 * the SAME shared definition the firmware uses (proto/claudeogotchi_uart.ts mirrors
 * claudeogotchi_uart.h). The mock FAP parses those UART bytes with the shared parser and
 * applies them with the same null-safe "hold last value" rules as the real FAP
 * worker. So a passing test exercises the whole chain:
 *
 *   host scripts -> relay -> bridge reframe -> UART bytes -> FAP decode/apply
 */
import * as u from "../proto/claudeogotchi_uart.ts";

/** Mirror of ws_client.c handle_message: relay JSON -> UART frame bytes. */
export function reframe(msg) {
  const session = msg.session ?? 0;
  if (msg.type === "stats") {
    return u.buildStats({
      session,
      ctxPct: msg.ctxPct ?? null,
      fiveHrPct: msg.fiveHrPct ?? null,
      sevenDayPct: msg.sevenDayPct ?? null,
      costUsd: msg.costUsd ?? null,
      model: msg.model,
      tokens: msg.tokens ?? null,
    });
  }
  if (msg.type === "event") {
    return u.buildEvent({
      session,
      state: msg.state,
      tool: msg.tool,
      project: msg.project,
    });
  }
  if (msg.type === "heartbeat") return u.buildHeartbeat();
  return null;
}

/** Mirror of the FAP worker's apply_frame + null-safe hold-last rules. */
export class MockFap {
  constructor(trackedSession = 0) {
    this.tracked = trackedSession;
    this.model = {
      model: null,
      ctxPct: null,
      fiveHrPct: null,
      sevenDayPct: null,
      costUsd: null,
      tokens: null,
      state: "idle",
      tool: null,
      project: null,
      link: "connecting",
    };
    this.transitions = [];
    this.heartbeats = 0;
    this.seenSessions = new Set();
  }

  apply(d) {
    if (d.type === "stats") {
      this.seenSessions.add(d.session);
      if (d.session !== this.tracked) return; // don't flap the display
      if (d.ctxPct != null) this.model.ctxPct = d.ctxPct;
      if (d.fiveHrPct != null) this.model.fiveHrPct = d.fiveHrPct;
      if (d.sevenDayPct != null) this.model.sevenDayPct = d.sevenDayPct;
      if (d.costUsd != null) this.model.costUsd = d.costUsd;
      if (d.tokens != null) this.model.tokens = d.tokens;
      if (d.model != null) this.model.model = d.model;
    } else if (d.type === "event") {
      this.seenSessions.add(d.session);
      if (d.session !== this.tracked) return;
      if (d.state !== this.model.state) {
        this.transitions.push({ from: this.model.state, to: d.state });
      }
      this.model.state = d.state;
      this.model.project = d.project;
      this.model.tool = d.tool;
    } else if (d.type === "heartbeat") {
      this.heartbeats++;
    } else if (d.type === "link") {
      this.model.link = d.link;
    }
  }
}

export class MockBridge {
  constructor(wsUrl, fap) {
    this.url = wsUrl;
    this.fap = fap;
    this.parser = new u.Parser();
    this.frames = []; // decoded UART frames (what the FAP sees)
    this.uartBytes = []; // raw UART byte stream
  }

  connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.url);
      this.ws.onopen = () => resolve();
      this.ws.onerror = (e) => reject(e);
      this.ws.onmessage = (ev) => this._onMessage(ev.data);
    });
  }

  _onMessage(data) {
    let msg;
    try {
      msg = JSON.parse(typeof data === "string" ? data : data.toString());
    } catch {
      return;
    }
    const bytes = reframe(msg);
    if (!bytes) return;
    for (const b of bytes) this.uartBytes.push(b);
    for (const d of this.parser.push(bytes)) {
      this.frames.push(d);
      this.fap.apply(d);
    }
  }

  close() {
    try {
      this.ws?.close();
    } catch {
      /* ignore */
    }
  }
}

/** Poll until predicate() is truthy or timeout (ms). Resolves to predicate's value. */
export function waitFor(predicate, timeoutMs = 2000, stepMs = 10) {
  return new Promise((resolve, reject) => {
    const start = Date.now();
    const tick = () => {
      const v = predicate();
      if (v) return resolve(v);
      if (Date.now() - start > timeoutMs) return reject(new Error("waitFor timeout"));
      setTimeout(tick, stepMs);
    };
    tick();
  });
}
