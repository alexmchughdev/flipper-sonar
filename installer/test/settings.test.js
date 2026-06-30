import { test } from "node:test";
import assert from "node:assert/strict";
import { applyInstall, applyUninstall, stripManagedHooks } from "../lib/settings.js";
import { quote } from "../lib/quote.js";
import { normalizeRelayOrigin } from "../index.js";

const opts = {
  nodeBin: "/usr/bin/node",
  posterPath: "/home/u/.claude/flipper-claudeogotchi/claudeogotchi-hook.mjs",
  statuslinePath: "/home/u/.claude/flipper-claudeogotchi/claudeogotchi-statusline.mjs",
  claudeogotchiId: "AB12CD",
  relayOrigin: "https://my.relay",
};

test("install adds all hook events + statusline + attribution", () => {
  const { settings } = applyInstall({}, opts);
  for (const e of [
    "UserPromptSubmit",
    "PreToolUse",
    "PostToolUse",
    "Stop",
    "Notification",
    "SessionStart",
  ]) {
    assert.ok(settings.hooks[e], `has ${e}`);
  }
  // Notification has two matchers.
  assert.equal(settings.hooks.Notification.length, 2);
  const matchers = settings.hooks.Notification.map((g) => g.matcher).sort();
  assert.deepEqual(matchers, ["idle_prompt", "permission_prompt"]);
  assert.match(settings.statusLine.command, /claudeogotchi-statusline\.mjs/);
  assert.deepEqual(settings.attribution, { commit: "", pr: "" });
  assert.equal(settings.sessionUrl, false);
});

test("install is idempotent: re-running does not duplicate hooks", () => {
  const once = applyInstall({}, opts).settings;
  const twice = applyInstall(once, opts).settings;
  assert.equal(twice.hooks.PreToolUse.length, 1);
  assert.equal(twice.hooks.Notification.length, 2);
  assert.equal(twice.hooks.Stop.length, 1);
});

test("install preserves unrelated user hooks and keys", () => {
  const existing = {
    model: "opus",
    permissions: { allow: ["Bash"] },
    hooks: {
      PreToolUse: [
        { matcher: "Bash", hooks: [{ type: "command", command: "echo user-hook" }] },
      ],
    },
  };
  const { settings } = applyInstall(existing, opts);
  assert.equal(settings.model, "opus");
  assert.deepEqual(settings.permissions, { allow: ["Bash"] });
  // user's PreToolUse hook survives alongside ours.
  const cmds = settings.hooks.PreToolUse.map((g) => g.hooks[0].command);
  assert.ok(cmds.some((c) => c.includes("echo user-hook")));
  assert.ok(cmds.some((c) => c.includes("claudeogotchi-hook.mjs")));
});

test("uninstall removes exactly what was added, leaving user data", () => {
  const existing = {
    model: "opus",
    hooks: {
      PreToolUse: [
        { matcher: "Bash", hooks: [{ type: "command", command: "echo user-hook" }] },
      ],
    },
  };
  const { settings, manifest } = applyInstall(existing, opts);
  const cleaned = applyUninstall(settings, { ...manifest });

  assert.equal(cleaned.model, "opus");
  // user hook remains, ours is gone.
  const cmds = cleaned.hooks.PreToolUse.map((g) => g.hooks[0].command);
  assert.deepEqual(cmds, ["echo user-hook"]);
  assert.ok(!("statusLine" in cleaned));
  // attribution was added by us -> removed.
  assert.ok(!("attribution" in cleaned));
  assert.ok(!("sessionUrl" in cleaned));
});

test("uninstall preserves attribution the user already had", () => {
  const existing = { attribution: { commit: "custom" }, sessionUrl: true };
  const { settings, manifest } = applyInstall(existing, opts);
  // we did not add attribution/sessionUrl -> manifest records nothing.
  assert.deepEqual(manifest.addedAttribution, []);
  const cleaned = applyUninstall(settings, manifest);
  assert.deepEqual(cleaned.attribution, { commit: "custom" });
  assert.equal(cleaned.sessionUrl, true);
});

test("stripManagedHooks drops empty event arrays", () => {
  const { settings, manifest } = applyInstall({}, opts);
  const stripped = stripManagedHooks(settings, manifest.posterPath);
  // Stop had only our hook -> event removed entirely.
  assert.ok(!stripped.hooks.Stop);
});

test("quote escapes spaces and shell metachars (no injection)", () => {
  assert.equal(quote("/usr/bin/node"), "/usr/bin/node");
  assert.equal(quote("/Users/My Name/x"), '"/Users/My Name/x"');
  assert.match(quote('a"; rm -rf /'), /^".*\\".*"$/);
});

test("normalizeRelayOrigin converts ws/wss and bare hosts to http(s) origin", () => {
  assert.equal(normalizeRelayOrigin("wss://r.example"), "https://r.example");
  assert.equal(normalizeRelayOrigin("ws://r.example:8787"), "http://r.example:8787");
  assert.equal(normalizeRelayOrigin("https://r.example"), "https://r.example");
  assert.equal(normalizeRelayOrigin("r.example"), "https://r.example");
  assert.equal(normalizeRelayOrigin(""), "https://relay.flipper-claudeogotchi.dev");
});
