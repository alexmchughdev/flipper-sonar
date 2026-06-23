#include "sonar_i.h"
#include "sonar_uart.h"
#include <expansion/expansion.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_cdc.h>

/* USART on the expansion header = pins 13 (TX) / 14 (RX), the channel that does
 * not fight the Flipper CLI on the USB/LPUART bridge. */
#define SONAR_SERIAL_ID FuriHalSerialIdUsart
#define SONAR_BAUD 115200
#define RX_STREAM_SIZE 512
#define RX_STREAM_TRIGGER 1
#define WORKER_TICK_MS 250
#define STALE_AFTER_MS 30000 /* 3 missed heartbeats */

/* Board mode: GPIO USART RX. ISR context — only the ISR-safe stream send. */
static void rx_isr(FuriHalSerialHandle* handle, FuriHalSerialRxEvent ev, void* ctx) {
    Sonar* app = ctx;
    if(ev & FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(app->rx_stream, &b, 1, 0);
    }
}

/* USB mode: CDC RX. Runs in USB callback context; drain CDC into the stream
 * buffer (ISR-safe send) and nothing else.
 *
 * We use the DUAL CDC config so the Flipper CLI/RPC keeps working on channel 0
 * (qFlipper, ufbt, and `storage` all keep functioning) while Sonar receives its
 * telemetry on channel 1. This makes USB mode non-destructive: exiting restores
 * the single-CDC CLI cleanly, and the host bridge writes to the 2nd serial node. */
#define SONAR_CDC_IF 1
static void cdc_rx(void* ctx) {
    Sonar* app = ctx;
    if(!app->rx_stream) return;
    uint8_t buf[64];
    int32_t n;
    do {
        n = furi_hal_cdc_receive(SONAR_CDC_IF, buf, sizeof(buf));
        if(n > 0) furi_stream_buffer_send(app->rx_stream, buf, (size_t)n, 0);
    } while(n == (int32_t)sizeof(buf));
}

static CdcCallbacks sonar_cdc_cb = {
    .rx_ep_callback = cdc_rx,
};

/* Apply one decoded frame to the shared model under the mutex. Returns the
 * notification transition to fire (old/new state) via out-params; the caller
 * fires haptics OUTSIDE the lock to keep the critical section short. */
static void apply_frame(
    Sonar* app,
    const SonarFrame* f,
    uint8_t* notify_old,
    uint8_t* notify_new) {
    *notify_old = *notify_new = SONAR_STATE_IDLE;
    bool fire = false;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    SonarModel* m = &app->model;
    uint8_t tracked = app->config.tracked_session;

    switch(f->type) {
    case SONAR_T_STATS: {
        SonarStats s;
        if(!sonar_decode_stats(f->payload, f->payload_len, &s)) break;
        m->seen_sessions |= (1u << (s.session & 0x1F));
        m->active_session = s.session;
        m->last_rx_tick = furi_get_tick();
        if(m->link == SonarLinkStale) m->link = SonarLinkOnline;
        if(s.session != tracked) break; /* don't flap the display */
        /* Null-safe: only overwrite a metric when its flag says it's present. */
        if(s.flags & SONAR_F_CTX) {
            m->ctx_pct = s.ctx_pct;
            m->ctx_valid = true;
        }
        if(s.flags & SONAR_F_5H) {
            m->five_pct = s.five_hr_pct;
            m->five_valid = true;
        }
        if(s.flags & SONAR_F_7D) {
            m->seven_pct = s.seven_day_pct;
            m->seven_valid = true;
        }
        /* cost is intentionally not displayed (usage matters, not $ on subs) */
        if(s.flags & SONAR_F_MODEL) strlcpy(m->model, s.model, sizeof(m->model));
        if(s.flags & SONAR_F_TOKENS) {
            m->tokens = (uint32_t)s.tokens_h * 100u;
            m->tokens_valid = true;
        }
        break;
    }
    case SONAR_T_EVENT: {
        SonarEvent e;
        if(!sonar_decode_event(f->payload, f->payload_len, &e)) break;
        m->seen_sessions |= (1u << (e.session & 0x1F));
        m->active_session = e.session;
        m->last_rx_tick = furi_get_tick();
        if(m->link == SonarLinkStale) m->link = SonarLinkOnline;
        if(e.session != tracked) break;
        if(e.state != m->state) {
            *notify_old = m->state;
            *notify_new = e.state;
            fire = true;
            m->state_tick = furi_get_tick(); /* for the done celebration timeout */
            /* Start the run timer when work begins. */
            if(e.state == SONAR_STATE_WORKING) m->work_start_tick = furi_get_tick();
        }
        m->state = e.state;
        strlcpy(m->tool, e.tool, sizeof(m->tool));
        strlcpy(m->project, e.project, sizeof(m->project));
        break;
    }
    case SONAR_T_LINK: {
        uint8_t link;
        if(sonar_decode_link(f->payload, f->payload_len, &link)) {
            m->link = (SonarLink)link;
            if(link == SonarLinkOnline) m->last_rx_tick = furi_get_tick();
        }
        break;
    }
    case SONAR_T_HEARTBEAT:
        m->last_rx_tick = furi_get_tick();
        if(m->link == SonarLinkStale) m->link = SonarLinkOnline;
        break;
    default:
        break;
    }
    furi_mutex_release(app->mutex);

    if(!fire) {
        *notify_old = *notify_new; /* signal "no transition" to caller */
    }
}

static void check_stale(Sonar* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->model.link == SonarLinkOnline && app->model.last_rx_tick != 0 &&
       furi_get_tick() - app->model.last_rx_tick > furi_ms_to_ticks(STALE_AFTER_MS)) {
        app->model.link = SonarLinkStale;
    }
    furi_mutex_release(app->mutex);
}

static int32_t worker_thread(void* ctx) {
    Sonar* app = ctx;
    SonarParser parser;
    sonar_parser_reset(&parser);
    uint8_t chunk[64];
    SonarFrame frame;

    while(app->worker_running) {
        /* Blocks until bytes arrive (the ISR wakes us) or the tick elapses, so
         * stale-detection still runs on a quiet link. */
        size_t n = furi_stream_buffer_receive(
            app->rx_stream, chunk, sizeof(chunk), furi_ms_to_ticks(WORKER_TICK_MS));
        for(size_t i = 0; i < n; i++) {
            if(sonar_parser_push(&parser, chunk[i], &frame)) {
                uint8_t old_s, new_s;
                apply_frame(app, &frame, &old_s, &new_s);
                if(old_s != new_s) sonar_notify_transition(app, old_s, new_s);
            }
        }
        check_stale(app);
    }
    return 0;
}

void sonar_worker_start(Sonar* app) {
    app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_SIZE, RX_STREAM_TRIGGER);
    app->worker_running = true;
    app->worker = furi_thread_alloc_ex("SonarWorker", 2048, worker_thread, app);
    furi_thread_start(app->worker);

    if(app->config.link_mode == SONAR_LINK_MODE_USB) {
        /* No board: take over USB CDC and receive frames from a host bridge.
         * This drops the USB CLI/RPC while the app runs (that's expected). */
        app->usb_prev = furi_hal_usb_get_config();
        furi_hal_usb_unlock();
        if(furi_hal_usb_set_config(&usb_cdc_dual, NULL)) {
            furi_hal_cdc_set_callbacks(SONAR_CDC_IF, &sonar_cdc_cb, app);
        }
        return;
    }

    /* Board mode. The Expansion Module service owns USART (pins 13/14) by
     * default to detect add-on boards. Disable it so we can use the port; we
     * re-enable on exit. */
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);
    furi_record_close(RECORD_EXPANSION);

    /* Acquire gracefully: if the port is still busy, run without UART rather
     * than aborting the whole system (a failed furi_check reboots the Flipper).
     * The link simply stays "connecting" until the port frees up. */
    app->serial = furi_hal_serial_control_acquire(SONAR_SERIAL_ID);
    if(app->serial) {
        furi_hal_serial_init(app->serial, SONAR_BAUD);
        furi_hal_serial_async_rx_start(app->serial, rx_isr, app, false);
    }
}

void sonar_worker_stop(Sonar* app) {
    /* Stop the byte source first so nothing pushes to a freed stream. */
    if(app->serial) { /* board mode */
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
        app->serial = NULL;

        Expansion* expansion = furi_record_open(RECORD_EXPANSION);
        expansion_enable(expansion); /* hand USART back to the expansion service */
        furi_record_close(RECORD_EXPANSION);
    }
    if(app->usb_prev) { /* USB mode: restore the previous USB config (CLI/RPC) */
        furi_hal_cdc_set_callbacks(SONAR_CDC_IF, NULL, NULL);
        furi_hal_usb_set_config(app->usb_prev, NULL);
        app->usb_prev = NULL;
    }
    if(app->worker) {
        /* The worker re-checks worker_running every WORKER_TICK_MS, so it exits
         * within one tick of clearing the flag. */
        app->worker_running = false;
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }
    if(app->rx_stream) {
        furi_stream_buffer_free(app->rx_stream);
        app->rx_stream = NULL;
    }
}

void sonar_worker_send_provision(Sonar* app) {
    if(!app->serial) return;
    uint8_t buf[SONAR_MAX_FRAME];
    size_t n = sonar_build_provision(
        app->ssid_buf, app->pass_buf, app->config.relay_url, app->config.sonar_id, buf,
        sizeof(buf));
    if(n) furi_hal_serial_tx(app->serial, buf, n);
}
