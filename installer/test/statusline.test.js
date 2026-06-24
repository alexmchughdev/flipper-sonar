import { test } from "node:test";
import assert from "node:assert/strict";
import { buildStats, renderLine } from "../runtime/claudeogotchi-statusline.mjs";

const full = {
  session_id: "sess-1",
  model: { display_name: "Opus 4.8" },
  context_window: { used_percentage: 42 },
  rate_limits: {
    five_hour: { used_percentage: 28 },
    seven_day: { used_percentage: 18 },
  },
  cost: { total_cost_usd: 4.83 },
  cwd: "/Users/me/clients/acme/myrepo",
};

test("buildStats maps all fields and strips cwd to a basename", () => {
  const s = buildStats("AB12CD", full);
  assert.equal(s.type, "stats");
  assert.equal(s.model, "Opus 4.8");
  assert.equal(s.ctxPct, 42);
  assert.equal(s.fiveHrPct, 28);
  assert.equal(s.sevenDayPct, 18);
  assert.ok(!("costUsd" in s), "cost is not sent to the device (shown only in the terminal)");
  assert.equal(s.project, "myrepo");
  // PRIVACY: no absolute path anywhere.
  assert.ok(!JSON.stringify(s).includes("/Users/me"));
  assert.ok(!JSON.stringify(s).includes("acme"));
});

test("NULL-SAFETY: missing rate_limits omits those metrics (not zero)", () => {
  const s = buildStats("AB12CD", {
    session_id: "x",
    model: { display_name: "Sonnet 4.6" },
    context_window: { used_percentage: 10 },
    cost: { total_cost_usd: 0.5 },
  });
  assert.equal(s.ctxPct, 10);
  assert.ok(!("fiveHrPct" in s), "absent, not zero");
  assert.ok(!("sevenDayPct" in s), "absent, not zero");
});

test("NULL-SAFETY: current_usage null (no context/cost) degrades gracefully", () => {
  const s = buildStats("AB12CD", {
    session_id: "x",
    model: { display_name: "Opus 4.8" },
    context_window: null,
    cost: null,
    rate_limits: null,
  });
  assert.equal(s.model, "Opus 4.8");
  assert.ok(!("ctxPct" in s));
  assert.ok(!("costUsd" in s));
  assert.ok(!("fiveHrPct" in s));
});

test("buildStats tolerates a totally empty payload", () => {
  const s = buildStats("AB12CD", {});
  assert.equal(s.type, "stats");
  assert.equal(s.claudeogotchiId, "AB12CD");
  assert.ok(!("model" in s));
});

test("renderLine never throws and includes model + basename", () => {
  assert.match(renderLine(full), /Opus 4\.8/);
  assert.match(renderLine(full), /myrepo/);
  assert.match(renderLine(full), /\$4\.83/);
  // empty input still renders something printable.
  assert.equal(typeof renderLine({}), "string");
  assert.ok(renderLine({}).length > 0);
});
