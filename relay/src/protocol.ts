/**
 * Wire protocol + privacy enforcement for the relay.
 *
 * Two payload shapes arrive at POST /ingest/{sonarId}: "event" (from hooks) and
 * "stats" (from the statusline wrapper). The relay normalizes both into a
 * canonical wire message that the bridge consumes over WS and re-frames as
 * compact UART (SPEC section 5).
 *
 * Privacy is enforced HERE and nowhere else can bypass it:
 *  - `cwd` may only ever leave as a basename (the relay also defends in depth in
 *    case the host hook somehow sent a full path).
 *  - `transcript_path` and any transcript content are dropped unconditionally.
 */

// ---- Canonical states (string literal union; strip-types friendly) ----
export type SonarState =
  | "idle"
  | "working"
  | "waiting-approval"
  | "waiting-input"
  | "done";

const VALID_STATES = new Set<string>([
  "idle",
  "working",
  "waiting-approval",
  "waiting-input",
  "done",
]);

export type WireType = "stats" | "event" | "link" | "heartbeat";

export interface WireMessage {
  type: WireType;
  sonarId: string;
  /** Stable per-session slot index assigned by the topic (0..N-1). */
  session?: number;
  sessionId?: string;
  ts: number;
  // event fields
  state?: SonarState;
  tool?: string;
  project?: string;
  // stats fields
  model?: string;
  ctxPct?: number | null;
  fiveHrPct?: number | null;
  sevenDayPct?: number | null;
  costUsd?: number | null;
  tokens?: number | null;
  // link field
  link?: "connecting" | "online" | "stale";
}

/** Keys that must NEVER appear on the wire, no matter what the host sends. */
const FORBIDDEN_KEYS = new Set([
  "transcript_path",
  "transcriptPath",
  "transcript",
  "cwd", // full cwd forbidden; only `project` (a basename) is allowed through
]);

/** A sonar ID is 6 base32 chars (Crockford-ish, upper). */
const SONAR_ID_RE = /^[0-9A-HJKMNP-TV-Z]{6}$/;

export function isValidSonarId(id: unknown): id is string {
  return typeof id === "string" && SONAR_ID_RE.test(id);
}

/** Reduce any path-ish string to its final path component (basename only). */
export function basenameOnly(p: unknown): string | undefined {
  if (typeof p !== "string" || p.length === 0) return undefined;
  // Split on both separators; ignore trailing slashes.
  const parts = p.replace(/[/\\]+$/, "").split(/[/\\]+/);
  const base = parts[parts.length - 1];
  return base && base.length > 0 ? base : undefined;
}

function clampPct(v: unknown): number | null | undefined {
  if (v === null) return null;
  if (v === undefined) return undefined;
  const n = typeof v === "string" ? Number(v) : v;
  if (typeof n !== "number" || !Number.isFinite(n)) return undefined;
  return Math.max(0, Math.min(100, Math.round(n)));
}

function nonEmptyString(v: unknown, max = 64): string | undefined {
  if (typeof v !== "string") return undefined;
  const s = v.trim();
  if (s.length === 0) return undefined;
  return s.slice(0, max);
}

export class ProtocolError extends Error {}

/**
 * Validate + normalize a raw ingest body into a WireMessage. Throws
 * ProtocolError on anything unusable. Strips all forbidden/privacy-sensitive
 * fields. `sonarId` is taken from the URL, not the body (body value, if any,
 * must match).
 */
export function normalizeIngest(sonarId: string, raw: unknown): WireMessage {
  if (!isValidSonarId(sonarId)) {
    throw new ProtocolError("invalid sonarId");
  }
  if (raw === null || typeof raw !== "object") {
    throw new ProtocolError("body must be an object");
  }
  const body = raw as Record<string, unknown>;

  // Defense in depth: a host should only ever send `project` (a basename), but
  // if a full `cwd` leaks in we salvage its basename, then drop everything
  // path- or transcript-sensitive. The full path/content is never copied.
  let cwdBasename: string | undefined;
  for (const k of Object.keys(body)) {
    if (FORBIDDEN_KEYS.has(k)) {
      if ((k === "cwd") && cwdBasename === undefined) {
        cwdBasename = basenameOnly(body[k]);
      }
      delete body[k];
    }
  }

  if (body.sonarId !== undefined && body.sonarId !== sonarId) {
    throw new ProtocolError("sonarId mismatch between URL and body");
  }

  const type = body.type;
  if (type !== "event" && type !== "stats") {
    throw new ProtocolError("type must be 'event' or 'stats'");
  }

  const ts =
    typeof body.ts === "number" && Number.isFinite(body.ts)
      ? body.ts
      : Math.floor(Date.now() / 1000);

  const msg: WireMessage = {
    type,
    sonarId,
    ts,
    sessionId: nonEmptyString(body.sessionId, 128),
  };

  // `project` is the only path-derived field allowed out. Accept an explicit
  // `project` (always reduced to a basename) or fall back to the salvaged cwd
  // basename. The full cwd was already dropped above.
  const project = basenameOnly(body.project) ?? cwdBasename;
  if (project) msg.project = project;

  if (type === "event") {
    const state = body.state;
    if (typeof state !== "string" || !VALID_STATES.has(state)) {
      throw new ProtocolError("event requires a valid state");
    }
    msg.state = state as SonarState;
    const tool = nonEmptyString(body.tool, 32);
    if (tool) msg.tool = tool;
  } else {
    // stats: every metric optional + null-safe.
    msg.model = nonEmptyString(body.model, 32);
    msg.ctxPct = clampPct(body.ctxPct);
    msg.fiveHrPct = clampPct(body.fiveHrPct);
    msg.sevenDayPct = clampPct(body.sevenDayPct);
    if (body.costUsd === null) {
      msg.costUsd = null;
    } else if (body.costUsd !== undefined) {
      const c = typeof body.costUsd === "string" ? Number(body.costUsd) : body.costUsd;
      msg.costUsd =
        typeof c === "number" && Number.isFinite(c) && c >= 0
          ? Math.round(c * 100) / 100
          : undefined;
    }
    if (body.tokens === null) {
      msg.tokens = null;
    } else if (body.tokens !== undefined) {
      const t = typeof body.tokens === "string" ? Number(body.tokens) : body.tokens;
      msg.tokens =
        typeof t === "number" && Number.isFinite(t) && t >= 0 ? Math.round(t) : undefined;
    }
  }

  return msg;
}

/** Serialize a wire message for the WS egress channel. */
export function encodeWire(msg: WireMessage): string {
  return JSON.stringify(msg);
}
