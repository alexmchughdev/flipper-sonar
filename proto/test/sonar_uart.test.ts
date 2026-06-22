import { test } from "node:test";
import assert from "node:assert/strict";
import * as u from "../sonar_uart.ts";

test("crc8 known vectors (poly 0x31, MSB-first, init 0x00)", () => {
  // Check value for THIS variant (poly 0x31, init 0x00, no reflection) over the
  // standard "123456789" string. The C header must agree (see gen_frames diff).
  const data = Uint8Array.from(Buffer.from("123456789", "ascii"));
  assert.equal(u.crc8(data), 0xa2);
  assert.equal(u.crc8(new Uint8Array(0)), 0x00);
  assert.equal(u.crc8(Uint8Array.from([0x00])), 0x00);
});

test("frame layout: SOF, LEN=payload+1, CRC over type+payload", () => {
  const f = u.buildHeartbeat();
  assert.equal(f[0], u.SOF);
  assert.equal(f[1], 1, "len = 1 (type only, no payload)");
  assert.equal(f[2], u.T_HEARTBEAT);
  assert.equal(f[3], u.crc8(f.subarray(2, 3)));
  assert.equal(f.length, 4);
});

test("stats round-trips with all fields", () => {
  const f = u.buildStats({
    session: 3,
    ctxPct: 42,
    fiveHrPct: 28,
    sevenDayPct: 18,
    costUsd: 4.83,
    model: "Opus 4.8",
  });
  const [d] = new u.Parser().push(f);
  assert.equal(d.type, "stats");
  if (d.type !== "stats") return;
  assert.equal(d.session, 3);
  assert.equal(d.ctxPct, 42);
  assert.equal(d.fiveHrPct, 28);
  assert.equal(d.sevenDayPct, 18);
  assert.equal(d.costUsd, 4.83);
  assert.equal(d.model, "Opus 4.8");
});

test("NULL-SAFETY: absent stats decode as null, not zero", () => {
  const f = u.buildStats({ session: 0, ctxPct: 55, fiveHrPct: null, costUsd: null });
  const [d] = new u.Parser().push(f);
  if (d.type !== "stats") throw new Error("expected stats");
  assert.equal(d.ctxPct, 55);
  assert.equal(d.fiveHrPct, null);
  assert.equal(d.sevenDayPct, null);
  assert.equal(d.costUsd, null);
  assert.equal(d.model, null);
});

test("event round-trips with and without tool/project", () => {
  const p = new u.Parser();
  const [a] = p.push(
    u.buildEvent({ session: 1, state: "waiting-approval", tool: "Bash", project: "myrepo" }),
  );
  if (a.type !== "event") throw new Error();
  assert.equal(a.state, "waiting-approval");
  assert.equal(a.tool, "Bash");
  assert.equal(a.project, "myrepo");

  const [b] = p.push(u.buildEvent({ session: 0, state: "done" }));
  if (b.type !== "event") throw new Error();
  assert.equal(b.state, "done");
  assert.equal(b.tool, null);
  assert.equal(b.project, null);
});

test("provision round-trips", () => {
  const f = u.buildProvision("HomeWiFi", "pass123", "wss://r/egress", "AB12CD");
  const [d] = new u.Parser().push(f);
  if (d.type !== "provision") throw new Error();
  assert.equal(d.ssid, "HomeWiFi");
  assert.equal(d.pass, "pass123");
  assert.equal(d.relayUrl, "wss://r/egress");
  assert.equal(d.sonarId, "AB12CD");
});

test("RESYNC: non-SOF garbage before a frame is skipped cleanly", () => {
  const f = u.buildLink("online");
  // Garbage with no 0x7E is discarded in the wait-SOF state.
  const noisy = Uint8Array.from([0x00, 0xff, 0x12, 0xaa, 0x55, ...f]);
  const frames = new u.Parser().push(noisy);
  const link = frames.find((x) => x.type === "link");
  assert.ok(link, "valid frame recovered after garbage");
});

test("RESYNC: a corrupted (bad CRC) frame is dropped, next frame survives", () => {
  const good = u.buildEvent({ session: 0, state: "working" });
  const bad = u.buildEvent({ session: 0, state: "idle" });
  bad[bad.length - 1] ^= 0xff; // corrupt CRC
  const frames = new u.Parser().push(Uint8Array.from([...bad, ...good]));
  assert.equal(frames.length, 1, "only the good frame decodes");
  assert.equal(frames[0].type, "event");
});

test("RESYNC: byte-at-a-time feeding works", () => {
  const f = u.buildStats({ session: 0, ctxPct: 10 });
  const p = new u.Parser();
  let got: u.Decoded[] = [];
  for (const b of f) got = got.concat(p.push(Uint8Array.from([b])));
  assert.equal(got.length, 1);
  assert.equal(got[0].type, "stats");
});

test("model string is truncated to MAX_STR", () => {
  const long = "x".repeat(100);
  const f = u.buildStats({ session: 0, model: long });
  const [d] = new u.Parser().push(f);
  if (d.type !== "stats") throw new Error();
  assert.equal(d.model?.length, u.MAX_STR);
});
