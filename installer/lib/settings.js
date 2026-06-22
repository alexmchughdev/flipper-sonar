/**
 * Pure settings-merge logic for the installer. No I/O here so it is trivially
 * unit-testable. The CLI (index.js) handles file reads/writes.
 *
 * Idempotency strategy: every hook we add is "managed" — recognizable because
 * its command string contains the poster script path. Re-installing first
 * strips managed entries, then re-adds, so hooks never duplicate. Unrelated
 * hooks the user defined are preserved untouched.
 */

import { quote } from "./quote.js";

const HOOK_EVENTS = [
  { event: "PreToolUse", matcher: "", state: "working" },
  { event: "PostToolUse", matcher: "", state: "working" },
  { event: "Stop", matcher: "", state: "done" },
  { event: "Notification", matcher: "permission_prompt", state: "waiting-approval" },
  { event: "Notification", matcher: "idle_prompt", state: "waiting-input" },
  { event: "SessionStart", matcher: "", state: "idle" },
];

function hookCommand(nodeBin, posterPath, event, state, sonarId, relayOrigin) {
  return [
    quote(nodeBin),
    quote(posterPath),
    "--event",
    event,
    "--state",
    state,
    "--sonar",
    sonarId,
    "--relay",
    quote(relayOrigin),
  ].join(" ");
}

function statuslineCommand(nodeBin, statuslinePath, sonarId, relayOrigin) {
  return [
    quote(nodeBin),
    quote(statuslinePath),
    "--sonar",
    sonarId,
    "--relay",
    quote(relayOrigin),
  ].join(" ");
}

function isManagedGroup(group, posterPath) {
  return (
    Array.isArray(group?.hooks) &&
    group.hooks.some(
      (h) => typeof h?.command === "string" && h.command.includes(posterPath),
    )
  );
}

/** Strip our managed hook groups from a settings object (in place-safe copy). */
export function stripManagedHooks(settings, posterPath) {
  const out = { ...settings };
  if (!out.hooks || typeof out.hooks !== "object") return out;
  const hooks = {};
  for (const [event, groups] of Object.entries(out.hooks)) {
    if (!Array.isArray(groups)) {
      hooks[event] = groups;
      continue;
    }
    const kept = groups.filter((g) => !isManagedGroup(g, posterPath));
    if (kept.length) hooks[event] = kept;
  }
  out.hooks = hooks;
  return out;
}

/**
 * Apply the install to a settings object. Returns { settings, manifest }.
 * Does not mutate the input.
 */
export function applyInstall(settings, opts) {
  const { nodeBin, posterPath, statuslinePath, sonarId, relayOrigin } = opts;

  // Start from a copy with our prior hooks stripped (idempotent re-install).
  let out = stripManagedHooks(settings, posterPath);
  out = { ...out, hooks: { ...(out.hooks || {}) } };

  for (const { event, matcher, state } of HOOK_EVENTS) {
    const group = {
      matcher,
      hooks: [
        {
          type: "command",
          command: hookCommand(nodeBin, posterPath, event, state, sonarId, relayOrigin),
        },
      ],
    };
    const existing = Array.isArray(out.hooks[event]) ? out.hooks[event] : [];
    out.hooks[event] = [...existing, group];
  }

  // Statusline wrapper.
  out.statusLine = {
    type: "command",
    command: statuslineCommand(nodeBin, statuslinePath, sonarId, relayOrigin),
    padding: 0,
  };

  // Attribution stripping (SPEC §6). Only record keys we actually introduce, so
  // uninstall can restore the prior shape precisely.
  const addedAttribution = [];
  if (!("attribution" in out)) {
    out.attribution = { commit: "", pr: "" };
    addedAttribution.push("attribution");
  }
  if (!("sessionUrl" in out)) {
    out.sessionUrl = false;
    addedAttribution.push("sessionUrl");
  }

  const manifest = {
    posterPath,
    statuslinePath,
    sonarId,
    relayOrigin,
    addedAttribution,
  };
  return { settings: out, manifest };
}

/** Remove everything the installer added, using the manifest. Pure. */
export function applyUninstall(settings, manifest) {
  let out = stripManagedHooks(settings, manifest.posterPath);

  // Remove our statusline only if it still points at our wrapper.
  if (
    out.statusLine &&
    typeof out.statusLine.command === "string" &&
    out.statusLine.command.includes(manifest.statuslinePath)
  ) {
    out = { ...out };
    delete out.statusLine;
  }

  // Remove attribution keys only if we introduced them.
  if (Array.isArray(manifest.addedAttribution) && manifest.addedAttribution.length) {
    out = { ...out };
    for (const key of manifest.addedAttribution) delete out[key];
  }

  return out;
}
