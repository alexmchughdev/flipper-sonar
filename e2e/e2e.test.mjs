/**
 * End-to-end proof of the SPEC §8 acceptance criteria that don't need hardware.
 *
 * Chain under test:
 *   real host scripts (sonar-hook.mjs / sonar-statusline.mjs)
 *     -> real relay (self-host mode)
 *       -> mock bridge reframes to UART (shared proto)
 *         -> mock FAP decodes + applies (shared parser, null-safe holds)
 */
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRelay } from "../relay/src/server.ts";
import { MockBridge, MockFap, waitFor } from "./mock_bridge.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const HOOK = path.join(__dirname, "../installer/runtime/sonar-hook.mjs");
const STATUS = path.join(__dirname, "../installer/runtime/sonar-statusline.mjs");

// Each test uses a fresh sonar ID so session->slot assignment is isolated (the
// relay keeps per-topic slot state for the life of the process).
const IDS = ["AB12CD", "AB12CE", "AB12CF", "AB12CG", "AB12CH", "AB12CJ", "AB12CK", "AB12CM"];
let idCounter = 0;
const nextId = () => IDS[idCounter++ % IDS.length];

let relay;
let origin; // http origin for the host scripts

before(async () => {
  relay = await createRelay({ port: 0, allowInsecure: true });
  origin = `http://127.0.0.1:${relay.port}`;
});

after(async () => {
  await relay.close();
});

/** Fire a hook exactly as Claude Code would: JSON on stdin, args from settings. */
function fireHook(id, event, state, payload) {
  return runScript(HOOK, ["--event", event, "--state", state, "--sonar", id, "--relay", origin], payload);
}
function fireStatusline(id, payload) {
  return runScript(STATUS, ["--sonar", id, "--relay", origin], payload);
}

function runScript(script, args, stdinObj) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [script, ...args], {
      stdio: ["pipe", "pipe", "inherit"],
    });
    let out = "";
    child.stdout.on("data", (d) => (out += d));
    child.on("close", () => resolve(out));
    child.stdin.write(JSON.stringify(stdinObj));
    child.stdin.end();
  });
}

async function newBridge(id, tracked = 0) {
  const fap = new MockFap(tracked);
  const bridge = new MockBridge(`ws://127.0.0.1:${relay.port}/egress/${id}`, fap);
  await bridge.connect();
  return { fap, bridge };
}

test("live state within one turn: statusline stats reach the FAP bars", async () => {
  const id = nextId();
  const { fap, bridge } = await newBridge(id);
  await fireStatusline(id, {
    session_id: "s1",
    model: { display_name: "Opus 4.8" },
    context_window: { used_percentage: 42 },
    rate_limits: {
      five_hour: { used_percentage: 28 },
      seven_day: { used_percentage: 18 },
    },
    cost: { total_cost_usd: 4.83 },
    cwd: "/Users/me/clients/acme/myrepo",
  });
  await waitFor(() => fap.model.ctxPct === 42);
  assert.equal(fap.model.model, "Opus 4.8");
  assert.equal(fap.model.fiveHrPct, 28);
  assert.equal(fap.model.sevenDayPct, 18);
  assert.equal(fap.model.costUsd, 4.83);
  bridge.close();
});

test("Stop produces a 'done' frame at the FAP within ~1s", async () => {
  const id = nextId();
  const { fap, bridge } = await newBridge(id);
  const t0 = Date.now();
  await fireHook(id, "Stop", "done", { session_id: "s1", cwd: "/x/y/repo" });
  await waitFor(() => fap.model.state === "done");
  const elapsed = Date.now() - t0;
  assert.ok(elapsed < 1000, `done within 1s (was ${elapsed}ms)`);
  assert.deepEqual(fap.transitions.at(-1), { from: "idle", to: "done" });
  bridge.close();
});

test("permission prompt -> waiting-approval (distinct alert state)", async () => {
  const id = nextId();
  const { fap, bridge } = await newBridge(id);
  await fireHook(id, "Notification", "waiting-approval", {
    session_id: "s1",
    cwd: "/x/y/repo",
  });
  await waitFor(() => fap.model.state === "waiting-approval");
  assert.equal(fap.transitions.at(-1).to, "waiting-approval");
  bridge.close();
});

test("PRIVACY: only a basename reaches the wire; no cwd path, no transcript", async () => {
  const id = nextId();
  const { fap, bridge } = await newBridge(id);
  await fireHook(id, "PreToolUse", "working", {
    session_id: "s1",
    cwd: "/Users/me/clients/acme-secret/myrepo",
    transcript_path: "/Users/me/.claude/projects/x/transcript.jsonl",
    tool_name: "Bash",
  });
  await waitFor(() => fap.model.state === "working");
  assert.equal(fap.model.project, "myrepo");
  assert.equal(fap.model.tool, "Bash");
  // Inspect the actual bytes that crossed the link.
  const wire = Buffer.from(bridge.uartBytes).toString("latin1");
  assert.ok(!wire.includes("/Users/me"), "no absolute path on the wire");
  assert.ok(!wire.includes("acme-secret"), "no parent dirs on the wire");
  assert.ok(!wire.includes("transcript"), "no transcript reference on the wire");
  bridge.close();
});

test("NULL-SAFETY: absent rate_limits/current_usage hold last value, no flicker", async () => {
  const id = nextId();
  const { fap, bridge } = await newBridge(id);
  // First a full stats sample.
  await fireStatusline(id, {
    session_id: "s1",
    model: { display_name: "Opus 4.8" },
    context_window: { used_percentage: 50 },
    rate_limits: { five_hour: { used_percentage: 30 }, seven_day: { used_percentage: 20 } },
    cost: { total_cost_usd: 2.0 },
  });
  await waitFor(() => fap.model.fiveHrPct === 30);

  // Then a sample with NO rate_limits and NO context (current_usage null case).
  await fireStatusline(id, {
    session_id: "s1",
    model: { display_name: "Opus 4.8" },
    cost: { total_cost_usd: 2.5 },
  });
  await waitFor(() => fap.model.costUsd === 2.5);

  // The absent metrics must HOLD, never flicker to zero.
  assert.equal(fap.model.fiveHrPct, 30, "5h held");
  assert.equal(fap.model.sevenDayPct, 20, "7d held");
  assert.equal(fap.model.ctxPct, 50, "ctx held");
  bridge.close();
});

test("snapshot: a freshly connected bridge gets the last state immediately", async () => {
  const id = nextId();
  // Post state with NO bridge connected.
  await fireHook(id, "Stop", "done", { session_id: "s9", cwd: "/x/repo" });
  await fireStatusline(id, {
    session_id: "s9",
    model: { display_name: "Sonnet 4.6" },
    context_window: { used_percentage: 12 },
  });
  // Connect afterwards; the relay replays the snapshot.
  const { fap, bridge } = await newBridge(id);
  await waitFor(() => fap.model.state === "done" && fap.model.ctxPct === 12);
  assert.equal(fap.model.model, "Sonnet 4.6");
  bridge.close();
});

test("two concurrent sessions get distinct slots and do not flap the display", async () => {
  const id = nextId();
  const { fap, bridge } = await newBridge(id, 0); // FAP tracks session slot 0
  await fireHook(id, "PreToolUse", "working", { session_id: "alpha", cwd: "/a/one" });
  await waitFor(() => fap.model.state === "working");

  // A different session goes to a different slot; FAP tracking slot 0 ignores it.
  await fireHook(id, "Stop", "done", { session_id: "beta", cwd: "/b/two" });
  // Give it a moment to (not) apply.
  await new Promise((r) => setTimeout(r, 150));

  assert.ok(fap.seenSessions.size >= 2, "two sessions observed on the wire");
  assert.equal(fap.model.state, "working", "tracked session unaffected by the other");
  bridge.close();
});

test("self-host relay path works end to end with no hosted dependency", () => {
  // The whole suite runs against a locally created relay in self-host mode with
  // allowInsecure ingest and a direct ws egress — no hosted service involved.
  assert.ok(origin.startsWith("http://127.0.0.1:"));
});
