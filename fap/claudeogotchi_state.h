#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "claudeogotchi_uart.h"

/* Link state mirrors CLAUDEOGOTCHI_LINK_* from the wire protocol. */
typedef enum {
    ClaudeogotchiLinkConnecting = CLAUDEOGOTCHI_LINK_CONNECTING,
    ClaudeogotchiLinkOnline = CLAUDEOGOTCHI_LINK_ONLINE,
    ClaudeogotchiLinkStale = CLAUDEOGOTCHI_LINK_STALE,
} ClaudeogotchiLink;

/*
 * The single rendered state object. The three update sources (events, stats,
 * heartbeat) collapse into this. Every metric carries a validity flag so the
 * view can hold the last value or draw empty — never crash, never flicker to
 * zero (SPEC §4 null-safety).
 *
 * Accessed by the UART worker thread (writer) and the view (reader); guard ALL
 * access with the app mutex.
 */
typedef struct {
    /* header */
    char model[CLAUDEOGOTCHI_MAX_STR + 1];
    ClaudeogotchiLink link;

    /* bars */
    uint8_t ctx_pct;
    bool ctx_valid;
    uint8_t five_pct;
    bool five_valid;
    uint8_t seven_pct;
    bool seven_valid;
    uint32_t tokens; /* absolute output tokens */
    bool tokens_valid;

    /* state + sprite */
    uint8_t state; /* CLAUDEOGOTCHI_STATE_* */
    char tool[CLAUDEOGOTCHI_MAX_STR + 1];
    char project[CLAUDEOGOTCHI_MAX_STR + 1];
    uint32_t work_start_tick; /* furi tick when 'working' began (for the run timer) */
    uint32_t state_tick; /* furi tick of the last state change (e.g. done celebration) */

    /* freshness */
    uint32_t last_rx_tick; /* furi_get_tick() of last frame */
    uint8_t active_session; /* last session that sent anything */
    uint32_t seen_sessions; /* bitmask of session slots seen */
} ClaudeogotchiModel;

static inline void claudeogotchi_model_init(ClaudeogotchiModel* m) {
    m->model[0] = '\0';
    m->link = ClaudeogotchiLinkConnecting;
    m->ctx_pct = m->five_pct = m->seven_pct = 0;
    m->ctx_valid = m->five_valid = m->seven_valid = false;
    m->tokens = 0;
    m->tokens_valid = false;
    m->state = CLAUDEOGOTCHI_STATE_IDLE;
    m->tool[0] = '\0';
    m->project[0] = '\0';
    m->work_start_tick = 0;
    m->state_tick = 0;
    m->last_rx_tick = 0;
    m->active_session = 0;
    m->seen_sessions = 0;
}
