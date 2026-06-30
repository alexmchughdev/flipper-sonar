/* TS mirror of gen_frames.c. Output must match byte-for-byte. */
import * as u from "../claudeogotchi_uart.ts";

function emit(label: string, buf: Uint8Array): void {
  let hex = "";
  for (const b of buf) hex += b.toString(16).padStart(2, "0");
  process.stdout.write(`${label} ${hex}\n`);
}

emit(
  "stats_full",
  u.buildStats({
    session: 0,
    ctxPct: 42,
    fiveHrPct: 28,
    sevenDayPct: 18,
    costUsd: 4.83,
    model: "Opus 4.8",
    tokens: 7300,
  }),
);
emit(
  "stats_sparse",
  u.buildStats({
    session: 1,
    ctxPct: 55,
    fiveHrPct: null,
    sevenDayPct: null,
    costUsd: null,
  }),
);
emit(
  "event_approval",
  u.buildEvent({ session: 0, state: "waiting-approval", tool: "Bash", project: "myrepo" }),
);
emit("event_done", u.buildEvent({ session: 2, state: "done" }));
emit("link_online", u.buildLink("online"));
emit("heartbeat", u.buildHeartbeat());
emit(
  "provision",
  u.buildProvision("HomeWiFi", "s3cret-pass", "wss://relay.example/egress", "AB12CD"),
);
