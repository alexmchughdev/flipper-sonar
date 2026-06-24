# Flipper Claudeogotchi installer

Configures the **Claude Code side**. Run it on whatever machine Claude Code runs
on — your laptop, an SSH box, or inside a dev container — and it edits *that*
environment's Claude Code settings. Zero runtime dependencies (Node stdlib only).

```bash
npx github:alexmchughdev/flipper-claudeogotchi --pair AB12CD
npx github:alexmchughdev/flipper-claudeogotchi --pair AB12CD --relay https://my.relay
npx github:alexmchughdev/flipper-claudeogotchi --uninstall
```

## What it writes

Into `~/.claude/settings.json` (or `./.claude/settings.json` with `--project`):

- **Hooks** (command type) for `PreToolUse`, `PostToolUse`, `Stop`,
  `Notification` (`permission_prompt` → waiting-approval, `idle_prompt` →
  waiting-input), and `SessionStart` — each invoking the deployed
  `claudeogotchi-hook.mjs` poster.
- **Statusline** wrapper (`claudeogotchi-statusline.mjs`) that prints your normal status
  line *and* posts stats to the relay.
- **Attribution** stripping: `attribution: { commit: "", pr: "" }` and
  `sessionUrl: false` (only if you didn't already set them).

Runtime scripts are deployed to `~/.claude/flipper-claudeogotchi/`, and an
`install.json` manifest records exactly what was added so `--uninstall` is
precise.

## Privacy

The poster and statusline enforce the privacy contract **on the host, before
anything is sent**:

- `cwd` is reduced to its **basename** (`project`). The full path never leaves.
- `transcript_path` is never read or transmitted.

The relay re-enforces both as defense in depth.

## Idempotent

Re-running updates in place and never duplicates hooks (managed entries are
identified by the poster path and stripped before re-adding). Unrelated hooks
and settings keys are preserved.

## Dev container

Bake it into the image or `postCreateCommand`:

```jsonc
// devcontainer.json
"postCreateCommand": "npx -y github:alexmchughdev/flipper-claudeogotchi --pair AB12CD"
```

## Tests

```bash
node --test test/*.test.js
```
