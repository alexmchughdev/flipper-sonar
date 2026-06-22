/* Emits hex of a fixed set of frames using sonar_uart.h. The TS generator must
 * produce byte-identical output, proving both ends share one wire contract. */
#include <stdio.h>
#include "../sonar_uart.h"

static void emit(const char* label, const uint8_t* buf, size_t n) {
    printf("%s ", label);
    for(size_t i = 0; i < n; i++) printf("%02x", buf[i]);
    printf("\n");
}

int main(void) {
    uint8_t buf[SONAR_MAX_FRAME];
    size_t n;

    n = sonar_build_stats(0, 42, 28, 18, 483, "Opus 4.8", buf, sizeof(buf));
    emit("stats_full", buf, n);

    /* Null-safe: rate limits + cost absent (-1), model absent. */
    n = sonar_build_stats(1, 55, -1, -1, -1, NULL, buf, sizeof(buf));
    emit("stats_sparse", buf, n);

    n = sonar_build_event(0, SONAR_STATE_WAITING_APPROVAL, "Bash", "myrepo", buf, sizeof(buf));
    emit("event_approval", buf, n);

    n = sonar_build_event(2, SONAR_STATE_DONE, NULL, NULL, buf, sizeof(buf));
    emit("event_done", buf, n);

    n = sonar_build_link(SONAR_LINK_ONLINE, buf, sizeof(buf));
    emit("link_online", buf, n);

    n = sonar_build_heartbeat(buf, sizeof(buf));
    emit("heartbeat", buf, n);

    n = sonar_build_provision("HomeWiFi", "s3cret-pass", "wss://relay.example/egress", "AB12CD", buf, sizeof(buf));
    emit("provision", buf, n);

    return 0;
}
