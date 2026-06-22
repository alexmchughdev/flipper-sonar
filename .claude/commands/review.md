---
description: Deterministic pre-commit review gate for Flipper Sonar
---

# /review — Flipper Sonar review gate

Review the **staged changes** (`git diff --cached`) for this commit. If nothing is
staged, review the working-tree diff (`git diff`). This gate runs before every
commit and must be deterministic across Claude Code versions, so do not rely on
any built-in reviewer behaviour — apply the checklist below directly.

Produce findings, then **fix every finding** before the commit proceeds. Do not
ask the human to review; you are the reviewer.

## What to check

### 1. Logic errors and edge cases
- Off-by-one, wrong comparison, inverted condition, unhandled branch.
- State machines: every state reachable and exitable; no dead transitions.
- Reconnect/backoff: bounded, no busy loop, resets on success.

### 2. Security
- **No injected credentials.** No WiFi passwords, API keys, tokens, or relay
  secrets committed to the repo. Grep the diff for obvious secret shapes.
- **Relay must not leak `cwd` or transcript.** The only path data allowed to
  leave the host is a **basename** of `cwd`. `transcript_path` contents must
  never be read or transmitted. Reject any code that sends `transcript_path`,
  full `cwd`, or reads transcript files.
- **TLS present where required.** Ingest endpoint must require TLS. The ESP32
  relay client must use `wss://` / TLS. The Flipper must never attempt TLS.
- Command injection / shell interpolation in installer and hooks.

### 3. Null-safety against the section 4 telemetry contract
- Every field from SPEC section 4 is optional. Code must tolerate missing
  `rate_limits`, `rate_limits.five_hour`, `rate_limits.seven_day`,
  `current_usage`, `cost`, `context_window`, `model`.
- Missing data must hold the last value or render empty — never crash, never
  flicker to zero.

### 4. Resource and thread-safety (FAP)
- The UART worker thread and the view must access the shared state struct only
  under the mutex.
- No allocation in the UART RX hot path that can leak on malformed frames.
- Malformed UART frames are dropped silently and the parser resyncs on `0x7E`.
- Threads and streams are released on app exit (no leaked FuriThread, mutex,
  stream buffer, or NotificationApp record).

### 5. Git hygiene
- Commit message is conventional-commit style, imperative, **no AI attribution**
  of any kind: no co-author trailer, no "generated with", no AI mention.

## Output
List findings as `FINDING: <file>:<line> — <issue>`. If none, print
`REVIEW CLEAN`. Then apply fixes for all findings and re-run the relevant
checks before committing.
