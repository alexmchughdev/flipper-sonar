/**
 * claudeogotchi_uart.ts — byte-for-byte mirror of claudeogotchi_uart.h for the mock bridge and
 * e2e tests. If you change the wire contract, change BOTH files together.
 *
 * Frame: 0x7E | LEN | TYPE | PAYLOAD[LEN-1] | CRC8
 *   LEN  = payload length + 1 (covers TYPE + PAYLOAD), 1..255.
 *   CRC8 = poly 0x31, MSB-first, init 0x00, over (TYPE + PAYLOAD).
 *   Little-endian multibyte ints.
 */

export const SOF = 0x7e;

export const T_STATS = 0x01;
export const T_EVENT = 0x02;
export const T_LINK = 0x03;
export const T_HEARTBEAT = 0x04;
export const T_PROVISION = 0x10;

export const STATE = {
  idle: 0,
  working: 1,
  "waiting-approval": 2,
  "waiting-input": 3,
  done: 4,
} as const;
export type StateName = keyof typeof STATE;
export const STATE_NAME: Record<number, StateName> = {
  0: "idle",
  1: "working",
  2: "waiting-approval",
  3: "waiting-input",
  4: "done",
};

export const LINK = { connecting: 0, online: 1, stale: 2 } as const;
export const LINK_NAME: Record<number, keyof typeof LINK> = {
  0: "connecting",
  1: "online",
  2: "stale",
};

export const F_CTX = 0x01;
export const F_5H = 0x02;
export const F_7D = 0x04;
export const F_COST = 0x08;
export const F_MODEL = 0x10;
export const F_TOKENS = 0x20;

export const MAX_PAYLOAD = 254;
export const MAX_STR = 32;

export function crc8(data: Uint8Array): number {
  let crc = 0x00;
  for (let i = 0; i < data.length; i++) {
    crc ^= data[i];
    for (let b = 0; b < 8; b++) {
      crc = crc & 0x80 ? ((crc << 1) ^ 0x31) & 0xff : (crc << 1) & 0xff;
    }
  }
  return crc & 0xff;
}

export function encode(type: number, payload: Uint8Array): Uint8Array {
  if (payload.length > MAX_PAYLOAD) throw new Error("payload too large");
  const len = payload.length + 1;
  const out = new Uint8Array(4 + payload.length);
  out[0] = SOF;
  out[1] = len;
  out[2] = type;
  out.set(payload, 3);
  out[3 + payload.length] = crc8(out.subarray(2, 2 + len));
  return out;
}

function pushStr(arr: number[], s: string | undefined, max = MAX_STR): void {
  const bytes = s ? Array.from(Buffer.from(s, "utf8")).slice(0, max) : [];
  arr.push(bytes.length, ...bytes);
}

function clampPct(v: number | null | undefined): number {
  if (v == null || !Number.isFinite(v)) return 0;
  return Math.max(0, Math.min(100, Math.round(v)));
}

export interface StatsInput {
  session?: number;
  ctxPct?: number | null;
  fiveHrPct?: number | null;
  sevenDayPct?: number | null;
  costUsd?: number | null;
  model?: string;
  tokens?: number | null; // output tokens (absolute count)
}

export function buildStats(s: StatsInput): Uint8Array {
  const pl: number[] = [];
  pl.push(s.session ?? 0);
  const flagsIdx = pl.length;
  pl.push(0);
  let flags = 0;
  pl.push(clampPct(s.ctxPct));
  if (s.ctxPct != null) flags |= F_CTX;
  pl.push(clampPct(s.fiveHrPct));
  if (s.fiveHrPct != null) flags |= F_5H;
  pl.push(clampPct(s.sevenDayPct));
  if (s.sevenDayPct != null) flags |= F_7D;
  const cents =
    s.costUsd != null && Number.isFinite(s.costUsd)
      ? Math.min(65535, Math.max(0, Math.round(s.costUsd * 100)))
      : 0;
  pl.push(cents & 0xff, (cents >> 8) & 0xff);
  if (s.costUsd != null) flags |= F_COST;
  const model = s.model && s.model.length ? s.model : undefined;
  if (model) flags |= F_MODEL;
  pushStr(pl, model);
  // tokens stored in units of 100 (uint16).
  const th =
    s.tokens != null && Number.isFinite(s.tokens)
      ? Math.min(65535, Math.max(0, Math.round(s.tokens / 100)))
      : 0;
  pl.push(th & 0xff, (th >> 8) & 0xff);
  if (s.tokens != null) flags |= F_TOKENS;
  pl[flagsIdx] = flags;
  return encode(T_STATS, Uint8Array.from(pl));
}

export interface EventInput {
  session?: number;
  state: StateName;
  tool?: string;
  project?: string;
}

export function buildEvent(e: EventInput): Uint8Array {
  const pl: number[] = [e.session ?? 0, STATE[e.state]];
  pushStr(pl, e.tool);
  pushStr(pl, e.project);
  return encode(T_EVENT, Uint8Array.from(pl));
}

export function buildLink(link: keyof typeof LINK): Uint8Array {
  return encode(T_LINK, Uint8Array.from([LINK[link]]));
}

export function buildHeartbeat(): Uint8Array {
  return encode(T_HEARTBEAT, new Uint8Array(0));
}

export function buildProvision(
  ssid: string,
  pass: string,
  relayUrl: string,
  claudeogotchiId: string,
): Uint8Array {
  const pl: number[] = [];
  for (const s of [ssid, pass, relayUrl, claudeogotchiId]) pushStr(pl, s, 63);
  return encode(T_PROVISION, Uint8Array.from(pl));
}

// ---- Decoder ----

export interface DecodedStats {
  type: "stats";
  session: number;
  ctxPct: number | null;
  fiveHrPct: number | null;
  sevenDayPct: number | null;
  costUsd: number | null;
  model: string | null;
  tokens: number | null; // absolute output token count
}
export interface DecodedEvent {
  type: "event";
  session: number;
  state: StateName;
  tool: string | null;
  project: string | null;
}
export interface DecodedLink {
  type: "link";
  link: keyof typeof LINK;
}
export interface DecodedHeartbeat {
  type: "heartbeat";
}
export interface DecodedProvision {
  type: "provision";
  ssid: string;
  pass: string;
  relayUrl: string;
  claudeogotchiId: string;
}
export type Decoded =
  | DecodedStats
  | DecodedEvent
  | DecodedLink
  | DecodedHeartbeat
  | DecodedProvision;

function readStr(buf: Uint8Array, off: { i: number }): string {
  const len = buf[off.i++];
  const s = Buffer.from(buf.subarray(off.i, off.i + len)).toString("utf8");
  off.i += len;
  return s;
}

export function decodePayload(type: number, payload: Uint8Array): Decoded | null {
  const off = { i: 0 };
  if (type === T_STATS) {
    const session = payload[off.i++];
    const flags = payload[off.i++];
    const ctx = payload[off.i++];
    const five = payload[off.i++];
    const seven = payload[off.i++];
    const cents = payload[off.i] | (payload[off.i + 1] << 8);
    off.i += 2;
    const model = readStr(payload, off);
    let tokensH = 0;
    if (flags & F_TOKENS && off.i + 2 <= payload.length) {
      tokensH = payload[off.i] | (payload[off.i + 1] << 8);
      off.i += 2;
    }
    return {
      type: "stats",
      session,
      ctxPct: flags & F_CTX ? ctx : null,
      fiveHrPct: flags & F_5H ? five : null,
      sevenDayPct: flags & F_7D ? seven : null,
      costUsd: flags & F_COST ? cents / 100 : null,
      model: flags & F_MODEL ? model : null,
      tokens: flags & F_TOKENS ? tokensH * 100 : null,
    };
  }
  if (type === T_EVENT) {
    const session = payload[off.i++];
    const state = STATE_NAME[payload[off.i++]];
    const tool = readStr(payload, off);
    const project = readStr(payload, off);
    return {
      type: "event",
      session,
      state,
      tool: tool || null,
      project: project || null,
    };
  }
  if (type === T_LINK) {
    return { type: "link", link: LINK_NAME[payload[0]] };
  }
  if (type === T_HEARTBEAT) {
    return { type: "heartbeat" };
  }
  if (type === T_PROVISION) {
    const ssid = readStr(payload, off);
    const pass = readStr(payload, off);
    const relayUrl = readStr(payload, off);
    const claudeogotchiId = readStr(payload, off);
    return { type: "provision", ssid, pass, relayUrl, claudeogotchiId };
  }
  return null;
}

/** Streaming, resync-safe parser mirroring ClaudeogotchiParser in the C header. */
export class Parser {
  private state = 0;
  private len = 0;
  private idx = 0;
  private buf = new Uint8Array(MAX_PAYLOAD + 1);

  /** Push bytes; returns every decoded frame found. */
  push(bytes: Uint8Array): Decoded[] {
    const out: Decoded[] = [];
    for (const b of bytes) {
      switch (this.state) {
        case 0:
          if (b === SOF) this.state = 1;
          break;
        case 1:
          if (b === 0) {
            this.state = 0;
          } else {
            this.len = b;
            this.idx = 0;
            this.state = 2;
          }
          break;
        case 2:
          this.buf[this.idx++] = b;
          if (this.idx >= this.len) this.state = 3;
          break;
        case 3: {
          const want = crc8(this.buf.subarray(0, this.len));
          this.state = 0;
          if (want === b) {
            const decoded = decodePayload(
              this.buf[0],
              this.buf.subarray(1, this.len),
            );
            if (decoded) out.push(decoded);
          }
          break;
        }
      }
    }
    return out;
  }
}
