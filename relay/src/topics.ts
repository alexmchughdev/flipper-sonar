/**
 * Topic registry. One topic per sonar ID. One logical producer (the CC host,
 * possibly several sessions) and one or more consumers (bridges) per topic.
 *
 * Responsibilities:
 *  - Assign a stable 1-byte session slot per sessionId so the FAP can pick which
 *    session to track and concurrent sessions never make the display flap.
 *  - Hold the last-known state per topic (last stats + last event per session)
 *    so a freshly connected bridge gets an immediate snapshot.
 *  - Fan messages out to subscribed bridges.
 *  - Heartbeat each active topic so the FAP can detect stale/disconnected.
 *
 * In-memory only; no database (SPEC 3.1).
 */

import type { WireMessage } from "./protocol.ts";
import { encodeWire } from "./protocol.ts";

export interface Subscriber {
  send(data: string): void;
  readonly closed: boolean;
}

const MAX_SESSIONS = 8; // 1 byte slot, but keep small; FAP only needs a few.

interface SessionState {
  slot: number;
  lastStats?: WireMessage;
  lastEvent?: WireMessage;
  lastSeen: number;
}

class Topic {
  readonly sonarId: string;
  private subscribers = new Set<Subscriber>();
  private sessions = new Map<string, SessionState>();
  private slotsUsed = new Set<number>();
  lastActivity = Date.now();

  constructor(sonarId: string) {
    this.sonarId = sonarId;
  }

  get subscriberCount(): number {
    return this.subscribers.size;
  }

  get sessionCount(): number {
    return this.sessions.size;
  }

  private slotFor(sessionId: string | undefined): number {
    const key = sessionId ?? "_default";
    let s = this.sessions.get(key);
    if (s) {
      s.lastSeen = Date.now();
      return s.slot;
    }
    // Assign the lowest free slot; if full, evict the least-recently-seen.
    let slot = -1;
    for (let i = 0; i < MAX_SESSIONS; i++) {
      if (!this.slotsUsed.has(i)) {
        slot = i;
        break;
      }
    }
    if (slot === -1) {
      let oldestKey: string | undefined;
      let oldest = Infinity;
      for (const [k, v] of this.sessions) {
        if (v.lastSeen < oldest) {
          oldest = v.lastSeen;
          oldestKey = k;
        }
      }
      if (oldestKey !== undefined) {
        const evicted = this.sessions.get(oldestKey)!;
        slot = evicted.slot;
        this.slotsUsed.delete(slot);
        this.sessions.delete(oldestKey);
      } else {
        slot = 0;
      }
    }
    this.slotsUsed.add(slot);
    s = { slot, lastSeen: Date.now() };
    this.sessions.set(key, s);
    return slot;
  }

  /** Record + fan out a normalized message. Returns the message with `session`. */
  publish(msg: WireMessage): WireMessage {
    const slot = this.slotFor(msg.sessionId);
    msg.session = slot;
    this.lastActivity = Date.now();

    const key = msg.sessionId ?? "_default";
    const s = this.sessions.get(key)!;
    if (msg.type === "stats") s.lastStats = msg;
    else if (msg.type === "event") s.lastEvent = msg;

    this.broadcast(encodeWire(msg));
    return msg;
  }

  subscribe(sub: Subscriber): void {
    this.subscribers.add(sub);
    // Immediate snapshot: replay last event + stats for every known session.
    for (const s of this.sessions.values()) {
      if (s.lastStats) sub.send(encodeWire(s.lastStats));
      if (s.lastEvent) sub.send(encodeWire(s.lastEvent));
    }
  }

  unsubscribe(sub: Subscriber): void {
    this.subscribers.delete(sub);
  }

  private broadcast(data: string): void {
    for (const sub of this.subscribers) {
      if (sub.closed) {
        this.subscribers.delete(sub);
        continue;
      }
      try {
        sub.send(data);
      } catch {
        this.subscribers.delete(sub);
      }
    }
  }

  heartbeat(now: number): void {
    const hb: WireMessage = {
      type: "heartbeat",
      sonarId: this.sonarId,
      ts: Math.floor(now / 1000),
    };
    this.broadcast(encodeWire(hb));
  }

  isIdle(now: number, ttlMs: number): boolean {
    return (
      this.subscribers.size === 0 && now - this.lastActivity > ttlMs
    );
  }
}

export class TopicRegistry {
  private topics = new Map<string, Topic>();
  private hbTimer?: ReturnType<typeof setInterval>;
  private readonly heartbeatMs: number;
  private readonly idleTtlMs: number;

  constructor(heartbeatMs = 10_000, idleTtlMs = 5 * 60_000) {
    this.heartbeatMs = heartbeatMs;
    this.idleTtlMs = idleTtlMs;
  }

  private get(sonarId: string): Topic {
    let t = this.topics.get(sonarId);
    if (!t) {
      t = new Topic(sonarId);
      this.topics.set(sonarId, t);
    }
    return t;
  }

  publish(msg: WireMessage): WireMessage {
    return this.get(msg.sonarId).publish(msg);
  }

  subscribe(sonarId: string, sub: Subscriber): () => void {
    const t = this.get(sonarId);
    t.subscribe(sub);
    return () => t.unsubscribe(sub);
  }

  topicCount(): number {
    return this.topics.size;
  }

  inspect(sonarId: string): { subscribers: number; sessions: number } | undefined {
    const t = this.topics.get(sonarId);
    if (!t) return undefined;
    return { subscribers: t.subscriberCount, sessions: t.sessionCount };
  }

  start(): void {
    if (this.hbTimer) return;
    this.hbTimer = setInterval(() => {
      const now = Date.now();
      for (const [id, t] of this.topics) {
        if (t.subscriberCount > 0) t.heartbeat(now);
        if (t.isIdle(now, this.idleTtlMs)) this.topics.delete(id);
      }
    }, this.heartbeatMs);
    // Don't keep the process alive solely for heartbeats.
    if (typeof this.hbTimer.unref === "function") this.hbTimer.unref();
  }

  stop(): void {
    if (this.hbTimer) {
      clearInterval(this.hbTimer);
      this.hbTimer = undefined;
    }
  }
}
