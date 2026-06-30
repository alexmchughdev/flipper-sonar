import { test } from "node:test";
import assert from "node:assert/strict";
import { TopicRegistry } from "../src/topics.ts";
import type { Subscriber } from "../src/topics.ts";
import { normalizeIngest } from "../src/protocol.ts";

function mockSub(): Subscriber & { messages: string[]; _closed: boolean } {
  return {
    messages: [],
    _closed: false,
    send(d: string) {
      this.messages.push(d);
    },
    get closed() {
      return this._closed;
    },
  };
}

test("routing: a message only reaches subscribers of its topic", () => {
  const reg = new TopicRegistry();
  const a = mockSub();
  const b = mockSub();
  reg.subscribe("AAAAAA", a);
  reg.subscribe("BBBBBB", b);

  reg.publish(normalizeIngest("AAAAAA", { type: "event", state: "working" }));

  assert.equal(a.messages.length, 1);
  assert.equal(b.messages.length, 0);
  const got = JSON.parse(a.messages[0]);
  assert.equal(got.claudeogotchiId, "AAAAAA");
  assert.equal(got.state, "working");
});

test("snapshot: a freshly connected bridge gets last stats + event immediately", () => {
  const reg = new TopicRegistry();
  reg.publish(
    normalizeIngest("AAAAAA", { type: "stats", model: "Opus 4.8", ctxPct: 40 }),
  );
  reg.publish(
    normalizeIngest("AAAAAA", { type: "event", state: "waiting-approval" }),
  );

  const late = mockSub();
  reg.subscribe("AAAAAA", late);

  assert.equal(late.messages.length, 2, "replayed last stats + last event");
  const types = late.messages.map((m) => JSON.parse(m).type).sort();
  assert.deepEqual(types, ["event", "stats"]);
});

test("session slots: concurrent sessions get stable distinct slots", () => {
  const reg = new TopicRegistry();
  const sub = mockSub();
  reg.subscribe("AAAAAA", sub);

  const m1 = reg.publish(
    normalizeIngest("AAAAAA", { type: "event", state: "working", sessionId: "s1" }),
  );
  const m2 = reg.publish(
    normalizeIngest("AAAAAA", { type: "event", state: "idle", sessionId: "s2" }),
  );
  const m1again = reg.publish(
    normalizeIngest("AAAAAA", { type: "event", state: "done", sessionId: "s1" }),
  );

  assert.notEqual(m1.session, m2.session, "distinct sessions, distinct slots");
  assert.equal(m1.session, m1again.session, "same session keeps its slot");
});

test("dead subscribers are pruned on broadcast", () => {
  const reg = new TopicRegistry();
  const dead = mockSub();
  reg.subscribe("AAAAAA", dead);
  dead._closed = true;
  reg.publish(normalizeIngest("AAAAAA", { type: "event", state: "idle" }));
  const info = reg.inspect("AAAAAA");
  assert.equal(info?.subscribers, 0);
});

test("heartbeat is emitted to subscribed topics only", () => {
  const reg = new TopicRegistry(20);
  const sub = mockSub();
  reg.subscribe("AAAAAA", sub);
  reg.publish(normalizeIngest("AAAAAA", { type: "event", state: "idle" }));
  reg.start();
  return new Promise<void>((resolve) => {
    setTimeout(() => {
      reg.stop();
      const hb = sub.messages.map((m) => JSON.parse(m)).filter((m) => m.type === "heartbeat");
      assert.ok(hb.length >= 1, "at least one heartbeat delivered");
      resolve();
    }, 60);
  });
});

test("unsubscribe stops delivery", () => {
  const reg = new TopicRegistry();
  const sub = mockSub();
  const off = reg.subscribe("AAAAAA", sub);
  off();
  reg.publish(normalizeIngest("AAAAAA", { type: "event", state: "idle" }));
  assert.equal(sub.messages.length, 0);
});
