#!/usr/bin/env node
/**
 * Flipper Sonar installer. Runs on the machine where Claude Code runs (local,
 * SSH box, or inside a dev container) and edits THAT environment's Claude Code
 * settings — never a remote laptop's.
 *
 *   npx github:alexmchughdev/flipper-sonar --pair AB12CD
 *   npx github:alexmchughdev/flipper-sonar --pair AB12CD --relay https://my.relay
 *   npx github:alexmchughdev/flipper-sonar --uninstall
 *
 * Flags:
 *   --pair <ID>     sonar pairing code (6 Crockford base32 chars)
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
const DEFAULT_RELAY = "https://relay.flipper-sonar.dev";
const SONAR_ID_RE = /^[0-9A-HJKMNP-TV-Z]{6}$/;

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
  return path.join(os.homedir(), ".claude", "flipper-sonar");
}

function readJson(file) {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8"));
  } catch {
    return null;
  }
}

function writeJson(file, obj) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, JSON.stringify(obj, null, 2) + "\n");
}

function deployRuntime() {
  const dir = runtimeDir();
  fs.mkdirSync(dir, { recursive: true });
  const files = ["sonar-hook.mjs", "sonar-statusline.mjs", "sonar-usb-bridge.mjs"];
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
    `Flipper Sonar installer

  --pair <ID>     sonar pairing code (6 chars), required for install
  --relay <url>   relay origin (https/wss/host); default ${DEFAULT_RELAY}
  --project       edit ./.claude/settings.json instead of ~/.claude
  --uninstall     remove everything the installer added
  --help

Examples:
  npx github:alexmchughdev/flipper-sonar --pair AB12CD
  npx github:alexmchughdev/flipper-sonar --pair AB12CD --relay https://my.relay
  npx github:alexmchughdev/flipper-sonar --uninstall
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
      "sonar-hook.mjs",
      "sonar-statusline.mjs",
      "sonar-usb-bridge.mjs",
      "install.json",
    ]) {
      try {
        fs.rmSync(path.join(runtimeDir(), f));
      } catch {
        /* ignore */
      }
    }
    process.stdout.write(`Uninstalled Sonar from ${targetPath}\n`);
    return;
  }

  const sonarId = args.pair;
  if (!sonarId || !SONAR_ID_RE.test(sonarId)) {
    process.stderr.write(
      "Error: --pair <ID> required (6 chars, A-Z0-9 excluding I/L/O/U).\n\n",
    );
    help();
    process.exit(1);
  }

  const relayOrigin = normalizeRelayOrigin(args.relay);
  const deployed = deployRuntime();
  const settings = readJson(sPath) || {};

  const { settings: updated, manifest } = applyInstall(settings, {
    nodeBin: process.execPath,
    posterPath: deployed["sonar-hook.mjs"],
    statuslinePath: deployed["sonar-statusline.mjs"],
    sonarId,
    relayOrigin,
  });

  writeJson(sPath, updated);
  writeJson(manifestPath, { ...manifest, settingsPath: sPath });

  const wsRelay = relayOrigin.replace(/^http/, "ws");
  process.stdout.write(
    `Sonar paired.
  settings:  ${sPath}
  sonar ID:  ${sonarId}
  relay:     ${relayOrigin}

Start (or restart) Claude Code here; the next turn will light up the Flipper.

No WiFi board? Run the USB bridge on the machine the Flipper is plugged into
(set the FAP's Settings -> Link to USB first):
  node "${deployed["sonar-usb-bridge.mjs"]}" --sonar ${sonarId} --relay ${wsRelay}

Re-run any time to update; 'npx ... --uninstall' to remove cleanly.
`,
  );
}

// Run only when executed (allow tests to import normalizeRelayOrigin).
if (import.meta.url === `file://${process.argv[1]}`) {
  main();
}
