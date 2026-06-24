# Set up with Claude Code

After cloning the repo, open Claude Code **in the repo root** and paste the
prompt below. It drives the whole setup (the USB / no-board path), pausing before
anything that flashes hardware or edits your global config.

````text
You are setting up the "Flipper Claudeogotchi" project in the current repo for me.
Goal: get Clawd reacting on my Flipper Zero to this machine's Claude Code activity,
using the USB (no-board) path. Work step by step and STOP to ask me before any
step that flashes hardware, edits my global Claude Code config, or starts a
long-running background process. Show me each command before running it.

Pre-flight (read-only; do these first and report findings):
1. Confirm we're at the repo root (Makefile, README.md, fap/, relay/, installer/).
2. Check tool versions and warn me if anything is missing or too old:
   - node --version   (relay needs Node >= 22.6; warn if lower)
   - python3 --version and pip
   - a C compiler (cc) for the proto check
3. Check whether a Flipper Zero is attached (macOS: `ls /dev/cu.usbmodem*`;
   Linux: `ls /dev/ttyACM*`). Report what you find; do NOT assume one is present.

Install + verify (safe):
4. Run `make setup` (installs relay deps + ufbt). First run may download a large
   SDK — tell me before it starts and let it finish.
5. Run `make test` to confirm the suite is green. If Node < 22.6 and tests fail on
   type-stripping, tell me and pause — don't try to upgrade Node yourself.

Flash the FAP (ASK FIRST — touches hardware):
6. Only if a Flipper is attached AND I confirm: `make flash` to build + upload the
   app. If no Flipper is attached, skip and tell me to plug one in first.

Wire up telemetry (USB no-board path):
7. Start the relay in the background: `make relay` (self-host on :8787). Confirm
   it's listening.
8. Ask me for the 6-character pairing code. Tell me how to get it: open the
   Claudeogotchi app on the Flipper → Settings → read the code (auto-generated).
9. With <CODE>, wire up Claude Code on THIS machine: `make pair ID=<CODE>`. This
   edits ~/.claude/settings.json — show me what it added (reversible with
   `make unpair`; offer `--project` scope if I'm sensitive about global config).
10. Start the USB bridge in the background: `make bridge ID=<CODE>`
    (or `make up ID=<CODE>` to run the relay + bridge together).
11. Tell me to set the Flipper to USB mode: Settings → Link → USB.
12. Tell me to restart Claude Code so the new hooks/statusline load. Within a turn,
    Clawd should react.

Wrap up:
13. Summarize what's running and how to stop it, and how to fully undo everything
    (`make unpair`, stop the background processes).
14. If anything failed, point me at docs/troubleshooting.md and the exact failing
    step. Don't retry hardware/destructive steps without asking.

Constraints: never push commits or modify git history; never contact an external
relay (local `make relay` only); don't edit my settings.json without showing me
the change first.
````

For the WiFi-board path instead, see [setup.md](setup.md).
