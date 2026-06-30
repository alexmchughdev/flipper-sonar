# /skill (optional)

Optional Claude Code skill bits for Claudeogotchi.

## `/review` command

The deterministic pre-commit review gate lives at
[`../.claude/commands/review.md`](../.claude/commands/review.md) so it applies to
every session in this repo regardless of Claude Code version. It reviews staged
changes for logic/edge cases, security (injected creds, relay leaking
`cwd`/transcript, missing TLS), null-safety against the §4 telemetry fields, and
FAP UART thread-safety. `/code-review` and `/security-review` are the built-in
equivalents if you prefer them.

To make it available globally (not just in this repo), copy it:

```bash
cp ../.claude/commands/review.md ~/.claude/commands/review.md
```

## Three-option decision pairing (optional)

claupper's three-option decision skill can label the three choices at a
`waiting-approval` prompt. If you use claupper alongside Claudeogotchi, that skill's
output can be surfaced; Claudeogotchi's `waiting-approval` state already drives the alert
pose and chime. Wiring the specific option labels onto the 128×64 display is left
as an opt-in because it depends on claupper's skill output format. The event
payload has room (`tool`/`project` fields) to carry a short label if you extend
the hook poster to include it.
