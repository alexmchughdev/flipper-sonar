#!/usr/bin/env node
/**
 * Flipper Claudeogotchi installer. Runs on the machine where Claude Code runs (local,
 * SSH box, or inside a dev container) and edits THAT environment's Claude Code
 * settings — never a remote laptop's.
 *
 *   npx github:alexmchughdev/flipper-claudeogotchi --pair AB12CD
 *   npx github:alexmchughdev/flipper-claudeogotchi --pair AB12CD --relay https://my.relay
 *   npx github:alexmchughdev/flipper-claudeogotchi --uninstall
 *
 * Flags:
 *   --pair <ID>     claudeogotchi pairing code (6 Crockford base32 chars)
 *   --relay <url>   relay origin (https/wss/host); default hosted placeholder
 *   --project       target ./.claude/settings.json instead of ~/.claude
 *   --uninstall     cleanly remove everything the installer added
 *   --help
 */
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { fileURLToPath } from "node:url";
import { applyInstall, applyUninstall } from "./lib/settings.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_RELAY = "https://relay.flipper-claudeogotchi.dev";
const CLAUDEOGOTCHI_ID_RE = /^[0-9A-HJKMNP-TV-Z]{6}$/;

function parseArgs(argv) {
  const a = {};
  for (let i = 0; i < argv.length; i++) {
    const t = argv[i];
    if (t === "--uninstall" || t === "--project" || t === "--help" || t === "-h") {
      a[t.replace(/^-+/, "")] = true;
    } else if (t.startsWith("--")) {
      a[t.slice(2)] = argv[i + 1];
      i++;
    }
  }
  return a;
}

/** Normalize any relay form to an https/http origin used for the ingest POST. */
export function normalizeRelayOrigin(input) {
  let s = String(input || "").trim();
  if (!s) return DEFAULT_RELAY;
  if (!/^[a-z]+:\/\//i.test(s)) s = "https://" + s;
  let u;
  try {
    u = new URL(s);
  } catch {
    return DEFAULT_RELAY;
  }
  const proto =
    u.protocol === "wss:" || u.protocol === "https:"
      ? "https:"
      : u.protocol === "ws:" || u.protocol === "http:"
        ? "http:"
        : "https:";
  return `${proto}//${u.host}`;
}

function settingsPath(useProject) {
  return useProject
    ? path.resolve(process.cwd(), ".claude", "settings.json")
    : path.join(os.homedir(), ".claude", "settings.json");
}

function runtimeDir() {
  return path.join(os.homedir(), ".claude", "flipper-claudeogotchi");
}

function readJson(file) {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8"));
  } catch {
    return null;
  }
}

/**
 * Read settings, distinguishing "missing" (ok, treat as {}) from "exists but
 * unparseable" (abort — never clobber a config we can't safely merge into).
 */
function readSettingsOrAbort(file) {
  if (!fs.existsSync(file)) return {};
  const v = readJson(file);
  if (v === null || typeof v !== "object") {
    process.stderr.write(
      `Error: ${file} exists but is not valid JSON. Refusing to overwrite it.\n` +
        `Fix or remove it, then re-run.\n`,
    );
    process.exit(1);
  }
  return v;
}

/** Atomic write: temp file + rename, so a crash can't truncate the real config. */
function writeJson(file, obj) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const tmp = `${file}.tmp-${process.pid}`;
  fs.writeFileSync(tmp, JSON.stringify(obj, null, 2) + "\n");
  fs.renameSync(tmp, file);
}

function deployRuntime() {
  const dir = runtimeDir();
  fs.mkdirSync(dir, { recursive: true });
  const files = ["claudeogotchi-hook.mjs", "claudeogotchi-statusline.mjs", "claudeogotchi-usb-bridge.mjs"];
  const out = {};
  for (const f of files) {
    const src = path.join(__dirname, "runtime", f);
    const dst = path.join(dir, f);
    fs.copyFileSync(src, dst);
    out[f] = dst;
  }
  return out;
}

function help() {
  process.stdout.write(
    `Flipper Claudeogotchi installer

  --pair <ID>     claudeogotchi pairing code (6 chars), required for install
  --relay <url>   relay origin (https/wss/host); default ${DEFAULT_RELAY}
  --project       edit ./.claude/settings.json instead of ~/.claude
  --uninstall     remove everything the installer added
  --help

Examples:
  npx github:alexmchughdev/flipper-claudeogotchi --pair AB12CD
  npx github:alexmchughdev/flipper-claudeogotchi --pair AB12CD --relay https://my.relay
  npx github:alexmchughdev/flipper-claudeogotchi --uninstall
`,
  );
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) return help();

  const sPath = settingsPath(args.project);
  const manifestPath = path.join(runtimeDir(), "install.json");

  if (args.uninstall) {
    const manifest = readJson(manifestPath);
    if (!manifest) {
      process.stdout.write("Nothing to uninstall (no install manifest found).\n");
      return;
    }
    const targetPath = manifest.settingsPath || sPath;
    const settings = readJson(targetPath) || {};
    const cleaned = applyUninstall(settings, manifest);
    writeJson(targetPath, cleaned);
    // Remove deployed runtime + manifest.
    for (const f of [
      "claudeogotchi-hook.mjs",
      "claudeogotchi-statusline.mjs",
      "claudeogotchi-usb-bridge.mjs",
      "install.json",
    ]) {
      try {
        fs.rmSync(path.join(runtimeDir(), f));
      } catch {
        /* ignore */
      }
    }
    process.stdout.write(`Uninstalled Claudeogotchi from ${targetPath}\n`);
    return;
  }

  const claudeogotchiId = args.pair;
  if (!claudeogotchiId || !CLAUDEOGOTCHI_ID_RE.test(claudeogotchiId)) {
    process.stderr.write(
      "Error: --pair <ID> required (6 chars, A-Z0-9 excluding I/L/O/U).\n\n",
    );
    help();
    process.exit(1);
  }

  const relayOrigin = normalizeRelayOrigin(args.relay);
  const deployed = deployRuntime();
  const settings = readSettingsOrAbort(sPath);

  const { settings: updated, manifest } = applyInstall(settings, {
    nodeBin: process.execPath,
    posterPath: deployed["claudeogotchi-hook.mjs"],
    statuslinePath: deployed["claudeogotchi-statusline.mjs"],
    claudeogotchiId,
    relayOrigin,
  });

  writeJson(sPath, updated);
  writeJson(manifestPath, { ...manifest, settingsPath: sPath });

  const wsRelay = relayOrigin.replace(/^http/, "ws");
  process.stdout.write(
    `Claudeogotchi paired.
  settings:  ${sPath}
  claudeogotchi ID:  ${claudeogotchiId}
  relay:     ${relayOrigin}

Start (or restart) Claude Code here; the next turn will light up the Flipper.

No WiFi board? Run the USB bridge on the machine the Flipper is plugged into
(set the FAP's Settings -> Link to USB first):
  node "${deployed["claudeogotchi-usb-bridge.mjs"]}" --claudeogotchi ${claudeogotchiId} --relay ${wsRelay}

Re-run any time to update; 'npx ... --uninstall' to remove cleanly.
`,
  );
}

// Run only when executed (allow tests to import normalizeRelayOrigin).
if (import.meta.url === `file://${process.argv[1]}`) {
  main();
}
