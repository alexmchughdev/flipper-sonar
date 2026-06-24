import { test } from "node:test";
import assert from "node:assert/strict";
import {
  normalizeIngest,
  basenameOnly,
  isValidClaudeogotchiId,
  ProtocolError,
} from "../src/protocol.ts";

test("claudeogotchi IDs are 6 Crockford base32 chars", () => {
  assert.ok(isValidClaudeogotchiId("AB12CD"));
  assert.ok(isValidClaudeogotchiId("0123HJ"));
  assert.ok(!isValidClaudeogotchiId("ab12cd")); // lowercase
  assert.ok(!isValidClaudeogotchiId("AB12C")); // too short
  assert.ok(!isValidClaudeogotchiId("ABILOU")); // I, L, O, U excluded
  assert.ok(!isValidClaudeogotchiId(""));
  assert.ok(!isValidClaudeogotchiId(123 as unknown));
});

test("basenameOnly reduces any path to its final component", () => {
  assert.equal(basenameOnly("/Users/me/secret/myrepo"), "myrepo");
  assert.equal(basenameOnly("C:\\work\\client-x\\proj"), "proj");
  assert.equal(basenameOnly("myrepo"), "myrepo");
  assert.equal(basenameOnly("/Users/me/myrepo/"), "myrepo");
  assert.equal(basenameOnly(""), undefined);
  assert.equal(basenameOnly(undefined), undefined);
});

test("event normalization keeps state + tool, derives project basename", () => {
  const msg = normalizeIngest("AB12CD", {
    type: "event",
    sessionId: "sess-1",
    state: "waiting-approval",
    tool: "Bash",
    project: "myrepo",
    ts: 1750000000,
  });
  assert.equal(msg.type, "event");
  assert.equal(msg.state, "waiting-approval");
  assert.equal(msg.tool, "Bash");
  assert.equal(msg.project, "myrepo");
  assert.equal(msg.claudeogotchiId, "AB12CD");
});

test("PRIVACY: full cwd never passes through; only a basename does", () => {
  const msg = normalizeIngest("AB12CD", {
    type: "event",
    state: "working",
    cwd: "/Users/me/clients/acme-secret/myrepo",
  });
  const json = JSON.stringify(msg);
  assert.ok(!json.includes("/Users/me"), "no absolute path on the wire");
  assert.ok(!json.includes("acme-secret"), "no parent dirs on the wire");
  assert.ok(!("cwd" in msg), "cwd key dropped");
  assert.equal(msg.project, "myrepo", "basename derived from cwd");
});

test("PRIVACY: transcript_path and transcript content are always dropped", () => {
  const msg = normalizeIngest("AB12CD", {
    type: "event",
    state: "working",
    transcript_path: "/Users/me/.claude/projects/x/transcript.jsonl",
    transcript: "secret model output",
    transcriptPath: "/also/secret",
  });
  const json = JSON.stringify(msg);
  assert.ok(!json.includes("transcript"), "no transcript field on the wire");
  assert.ok(!json.includes("secret"), "no transcript content on the wire");
});

test("stats normalization clamps percentages and rounds cost", () => {
  const msg = normalizeIngest("AB12CD", {
    type: "stats",
    model: "Opus 4.8",
    ctxPct: 142,
    fiveHrPct: -5,
    sevenDayPct: 18.7,
    costUsd: 4.8349,
    ts: 1750000000,
  });
  assert.equal(msg.ctxPct, 100);
  assert.equal(msg.fiveHrPct, 0);
  assert.equal(msg.sevenDayPct, 19);
  assert.equal(msg.costUsd, 4.83);
  assert.equal(msg.model, "Opus 4.8");
});

test("NULL-SAFETY: missing rate_limits / current_usage degrade, not crash", () => {
  const msg = normalizeIngest("AB12CD", { type: "stats", model: "Sonnet 4.6" });
  assert.equal(msg.ctxPct, undefined);
  assert.equal(msg.fiveHrPct, undefined);
  assert.equal(msg.sevenDayPct, undefined);
  assert.equal(msg.costUsd, undefined);
  assert.equal(msg.model, "Sonnet 4.6");
});

test("NULL-SAFETY: explicit nulls are preserved distinctly from absent", () => {
  const msg = normalizeIngest("AB12CD", {
    type: "stats",
    ctxPct: null,
    costUsd: null,
  });
  assert.equal(msg.ctxPct, null);
  assert.equal(msg.costUsd, null);
});

test("rejects unknown type and bad bodies", () => {
  assert.throws(() => normalizeIngest("AB12CD", { type: "nope" }), ProtocolError);
  assert.throws(() => normalizeIngest("AB12CD", null), ProtocolError);
  assert.throws(() => normalizeIngest("AB12CD", "string"), ProtocolError);
  assert.throws(
    () => normalizeIngest("AB12CD", { type: "event" }),
    ProtocolError,
  );
  assert.throws(
    () => normalizeIngest("AB12CD", { type: "event", state: "bogus" }),
    ProtocolError,
  );
});

test("rejects claudeogotchiId mismatch between URL and body", () => {
  assert.throws(
    () =>
      normalizeIngest("AB12CD", {
        type: "event",
        state: "idle",
        claudeogotchiId: "ZZZZZZ",
      }),
    ProtocolError,
  );
});
