#!/usr/bin/env node
/**
 * Host-side hook poster. Claude Code pipes the hook payload as JSON on stdin.
 * This maps it to a Claudeogotchi event and POSTs it to the relay ingest endpoint.
 *
 * PRIVACY (enforced here, on the host, before anything leaves):
 *  - `cwd` is reduced to its basename and sent as `project`. The full path
 *    never leaves the host.
 *  - `transcript_path` is never read and never sent.
 *
 * Usage (set by the installer):
 *   node claudeogotchi-hook.mjs --event <HookEvent> --state <state> \
 *        --claudeogotchi <ID> --relay <https-origin>
 *
 * Fire-and-forget with a short timeout; failures are swallowed so a hook never
 * breaks a Claude Code turn.
 */
import { basename } from "node:path";
import { request as httpsRequest } from "node:https";
import { request as httpRequest } from "node:http";

function parseArgs(argv) {
  const a = {};
  for (let i = 0; i < argv.length; i++) {
    if (argv[i].startsWith("--")) a[argv[i].slice(2)] = argv[i + 1];
  }
  return a;
}

function readStdin() {
  return new Promise((resolve) => {
    let data = "";
    let done = false;
    const finish = () => {
      if (!done) {
        done = true;
        resolve(data);
      }
    };
    process.stdin.setEncoding("utf8");
    process.stdin.on("data", (c) => (data += c));
    process.stdin.on("end", finish);
    process.stdin.on("error", finish);
    // If no stdin is piped, don't hang.
    setTimeout(finish, 500);
  });
}

function post(origin, claudeogotchiId, payload) {
  return new Promise((resolve) => {
    let url;
    try {
      url = new URL(`${origin.replace(/\/$/, "")}/ingest/${claudeogotchiId}`);
    } catch {
      return resolve();
    }
    const body = Buffer.from(JSON.stringify(payload));
    const isHttps = url.protocol === "https:";
    const req = (isHttps ? httpsRequest : httpRequest)(
      url,
      {
        method: "POST",
        headers: {
          "content-type": "application/json",
          "content-length": body.length,
        },
        timeout: 2000,
      },
      (res) => {
        res.resume();
        res.on("end", resolve);
      },
    );
    req.on("error", () => resolve());
    req.on("timeout", () => {
      req.destroy();
      resolve();
    });
    req.write(body);
    req.end();
  });
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const { event, state, claudeogotchi, relay } = args;
  if (!claudeogotchi || !relay) process.exit(0);

  const raw = await readStdin();
  let hook = {};
  try {
    hook = raw ? JSON.parse(raw) : {};
  } catch {
    hook = {};
  }

  // SessionStart resets state for that session; everything else is a state set.
  const resolvedState =
    event === "SessionStart" ? "idle" : state || "working";

  const payload = {
    type: "event",
    claudeogotchiId: claudeogotchi,
    sessionId: hook.session_id,
    state: resolvedState,
    ts: Math.floor(Date.now() / 1000),
  };

  // Only ever a basename — never the full path. transcript_path is ignored.
  if (typeof hook.cwd === "string" && hook.cwd.length) {
    payload.project = basename(hook.cwd);
  }
  if (event === "PreToolUse" && typeof hook.tool_name === "string") {
    payload.tool = hook.tool_name;
  }

  await post(relay, claudeogotchi, payload);
  process.exit(0);
}

main().catch(() => process.exit(0));
