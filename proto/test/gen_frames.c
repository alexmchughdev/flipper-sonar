/* Emits hex of a fixed set of frames using claudeogotchi_uart.h. The TS generator must
 * produce byte-identical output, proving both ends share one wire contract. */
#include <stdio.h>
#include "../claudeogotchi_uart.h"

static void emit(const char* label, const uint8_t* buf, size_t n) {
    printf("%s ", label);
    for(size_t i = 0; i < n; i++) printf("%02x", buf[i]);
    printf("\n");
}

int main(void) {
    uint8_t buf[CLAUDEOGOTCHI_MAX_FRAME];
    size_t n;

    n = claudeogotchi_build_stats(0, 42, 28, 18, 483, "Opus 4.8", 73, buf, sizeof(buf));
    emit("stats_full", buf, n);

    /* Null-safe: rate limits + cost + tokens absent (-1), model absent. */
    n = claudeogotchi_build_stats(1, 55, -1, -1, -1, NULL, -1, buf, sizeof(buf));
    emit("stats_sparse", buf, n);

    n = claudeogotchi_build_event(0, CLAUDEOGOTCHI_STATE_WAITING_APPROVAL, "Bash", "myrepo", buf, sizeof(buf));
    emit("event_approval", buf, n);

    n = claudeogotchi_build_event(2, CLAUDEOGOTCHI_STATE_DONE, NULL, NULL, buf, sizeof(buf));
    emit("event_done", buf, n);

    n = claudeogotchi_build_link(CLAUDEOGOTCHI_LINK_ONLINE, buf, sizeof(buf));
    emit("link_online", buf, n);

    n = claudeogotchi_build_heartbeat(buf, sizeof(buf));
    emit("heartbeat", buf, n);

    n = claudeogotchi_build_provision("HomeWiFi", "s3cret-pass", "wss://relay.example/egress", "AB12CD", buf, sizeof(buf));
    emit("provision", buf, n);

    return 0;
}
