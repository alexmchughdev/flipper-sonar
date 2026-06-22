# Troubleshooting

## Link glyph stays "connecting" / shows stale

The FAP header shows link state: a filled dot (online), animated dots
(connecting), or `x` (stale, no data for ~30s).

- **Connecting forever:** the ESP32 can't reach the relay.
  - Wrong Wi-Fi password or SSID → re-run **WiFi Setup**.
  - Captive portal network (see below).
  - Self-hosted relay unreachable / wrong URL → re-provision with the correct
    base URL, confirm the relay is up (`curl https://your-relay/healthz`).
- **Goes stale after working:** Wi-Fi dropped or the relay/CC host stopped.
  Heartbeats every ~10s; three missed (~30s) → stale. It self-heals on
  reconnect.

## Captive portals (hotels, cafés, conferences)

A headless ESP32 **cannot** complete a web sign-in page, so these networks will
never connect. This is surfaced on the FAP (Settings → *Captive portal?*) and
fails loudly rather than hanging silently.

**Fix:** give the board an upstream with no portal:

- A **phone hotspot** (cellular has no captive portal), or
- A **travel router** that handles the portal upstream and presents a clean WPA
  network to the board.

Then run **WiFi Setup** against that network.

## Bars show "--" or look empty

That's the null-safe path, not a bug:

- `rate_limits` is absent on some Claude Code versions/routes → the 5h/7d bars
  show `--`.
- `current_usage` is null before the first API call and right after `/compact`
  → context/cost hold their last value until the next call. They never flicker
  to zero.

## No chime / vibration

- Check **Settings → Haptics / Sound** are ON.
- Only transitions into **waiting-approval** and **done** buzz; rapid tool churn
  is intentionally debounced (1.5s) so it doesn't rattle.

## The wrong session is shown

With several Claude Code sessions on one sonar ID, each gets a slot. On the Live
View press **Left/Right** to cycle the tracked session, or set **Settings →
Session**. The display never switches on its own, so it won't flap.

## Nothing happens after pairing

- Confirm you ran the installer **where Claude Code runs**, not on a different
  machine.
- Restart Claude Code so it reloads `settings.json`.
- Verify hooks landed: look for `sonar-hook.mjs` entries in the target
  `~/.claude/settings.json`.
- Re-run the installer (idempotent). Use `--project` if you keep settings in the
  repo's `.claude/`.

## Commits show AI attribution

They shouldn't — the project `.claude/settings.json` sets
`attribution: { commit: "", pr: "" }` and the installer adds the same to your
config. If you see a trailer, confirm those keys are present and your Claude Code
version honors the `attribution` block.

## USB conflicts

Sonar uses **USART on pins 13/14**, not the USB/LPUART CLI bridge, so the
Flipper CLI and qFlipper keep working over USB while Sonar runs.
