/*
 * sonar_uart.h — Flipper Sonar UART framing (SPEC section 5).
 *
 * THE wire contract between the ESP32-S2 bridge and the Flipper STM32. Both
 * /firmware and /fap include this exact header so the two ends can never drift.
 * A TypeScript mirror (sonar_uart.ts) reproduces it byte-for-byte for the mock
 * bridge / e2e tests.
 *
 * Frame layout (byte-oriented, length-prefixed, resync-safe):
 *
 *     0x7E | LEN | TYPE | PAYLOAD[LEN-1] | CRC8
 *
 *   LEN  = number of bytes in (TYPE + PAYLOAD), i.e. payload length + 1.
 *          Range 1..255. LEN == 0 is invalid and aborts the frame.
 *   CRC8 = CRC-8, polynomial 0x31, MSB-first, init 0x00, computed over the LEN
 *          bytes (TYPE + PAYLOAD). NOT over 0x7E or LEN.
 *
 * Resync: malformed frames are dropped silently; the parser returns to scanning
 * for 0x7E. Heartbeats (every ~10s) guarantee the link self-heals.
 *
 * All multi-byte integers are little-endian.
 *
 * Header-only: define SONAR_UART_IMPL in exactly one translation unit per side
 * if you want non-inline copies; by default everything is `static inline`.
 */
#ifndef SONAR_UART_H
#define SONAR_UART_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SONAR_SOF 0x7E

/* Frame types. */
#define SONAR_T_STATS 0x01     /* ESP32 -> FAP */
#define SONAR_T_EVENT 0x02     /* ESP32 -> FAP */
#define SONAR_T_LINK 0x03      /* ESP32 -> FAP */
#define SONAR_T_HEARTBEAT 0x04 /* ESP32 -> FAP */
#define SONAR_T_PROVISION 0x10 /* FAP -> ESP32 */

/* Session-state enum (event payload). */
#define SONAR_STATE_IDLE 0
#define SONAR_STATE_WORKING 1
#define SONAR_STATE_WAITING_APPROVAL 2
#define SONAR_STATE_WAITING_INPUT 3
#define SONAR_STATE_DONE 4

/* Link-status enum (link payload). */
#define SONAR_LINK_CONNECTING 0
#define SONAR_LINK_ONLINE 1
#define SONAR_LINK_STALE 2

/* Stats payload validity flags. */
#define SONAR_F_CTX 0x01
#define SONAR_F_5H 0x02
#define SONAR_F_7D 0x04
#define SONAR_F_COST 0x08
#define SONAR_F_MODEL 0x10

#define SONAR_MAX_PAYLOAD 254
#define SONAR_MAX_FRAME (1 + 1 + 1 + SONAR_MAX_PAYLOAD + 1) /* SOF+LEN+TYPE+pl+CRC */
#define SONAR_MAX_STR 32

/* ---- CRC-8 (poly 0x31, MSB-first, init 0x00) ---- */
static inline uint8_t sonar_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) {
            if(crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x31);
            else
                crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/*
 * Encode a frame from a TYPE + raw payload into out. Returns total bytes
 * written, or 0 on overflow / invalid args. out must hold SONAR_MAX_FRAME.
 */
static inline size_t
    sonar_encode(uint8_t type, const uint8_t* payload, size_t payload_len, uint8_t* out, size_t out_cap) {
    if(payload_len > SONAR_MAX_PAYLOAD) return 0;
    size_t total = 4 + payload_len; /* SOF+LEN+TYPE+payload+CRC */
    if(out_cap < total) return 0;
    uint8_t len = (uint8_t)(payload_len + 1); /* type + payload */
    out[0] = SONAR_SOF;
    out[1] = len;
    out[2] = type;
    if(payload_len) memcpy(out + 3, payload, payload_len);
    /* CRC over TYPE + PAYLOAD == out[2 .. 2+len-1] */
    out[3 + payload_len] = sonar_crc8(out + 2, len);
    return total;
}

/* Decoded frame view (points into the parser's buffer; valid until next push). */
typedef struct {
    uint8_t type;
    const uint8_t* payload;
    uint8_t payload_len;
} SonarFrame;

/* Streaming, resync-safe parser. Zero-initialize before use. */
typedef struct {
    uint8_t state; /* 0 wait_sof, 1 len, 2 data, 3 crc */
    uint8_t len; /* type+payload length */
    uint8_t idx; /* bytes of (type+payload) collected */
    uint8_t buf[SONAR_MAX_PAYLOAD + 1]; /* type + payload */
} SonarParser;

static inline void sonar_parser_reset(SonarParser* p) {
    p->state = 0;
    p->len = 0;
    p->idx = 0;
}

/*
 * Feed one byte. Returns 1 and fills *out when a valid frame completes,
 * otherwise 0. Malformed frames are dropped; parser resyncs on the next 0x7E.
 */
static inline int sonar_parser_push(SonarParser* p, uint8_t b, SonarFrame* out) {
    switch(p->state) {
    case 0: /* wait_sof */
        if(b == SONAR_SOF) p->state = 1;
        return 0;
    case 1: /* len */
        if(b == 0) { /* invalid length; resync */
            p->state = 0;
            return 0;
        }
        p->len = b;
        p->idx = 0;
        p->state = 2;
        return 0;
    case 2: /* data (type + payload) */
        p->buf[p->idx++] = b;
        if(p->idx >= p->len) p->state = 3;
        return 0;
    case 3: { /* crc */
        uint8_t want = sonar_crc8(p->buf, p->len);
        p->state = 0; /* always return to scanning */
        if(want == b) {
            out->type = p->buf[0];
            out->payload = p->buf + 1;
            out->payload_len = (uint8_t)(p->len - 1);
            return 1;
        }
        /* Bad CRC: drop silently, wait for next SOF. */
        return 0;
    }
    default:
        p->state = 0;
        return 0;
    }
}

/* ---- Typed builders (return total frame bytes, 0 on overflow) ---- */

/*
 * Stats. Pass -1 for any percentage that is absent (flag cleared). cost_cents
 * < 0 means absent. model may be NULL/empty.
 */
static inline size_t sonar_build_stats(
    uint8_t session,
    int ctx_pct,
    int five_hr_pct,
    int seven_day_pct,
    int cost_cents,
    const char* model,
    uint8_t* out,
    size_t out_cap) {
    uint8_t pl[8 + SONAR_MAX_STR];
    size_t n = 0;
    uint8_t flags = 0;
    size_t flags_idx;

    pl[n++] = session;
    flags_idx = n;
    pl[n++] = 0; /* flags placeholder */
    pl[n++] = (ctx_pct >= 0) ? (uint8_t)(ctx_pct > 100 ? 100 : ctx_pct) : 0;
    if(ctx_pct >= 0) flags |= SONAR_F_CTX;
    pl[n++] = (five_hr_pct >= 0) ? (uint8_t)(five_hr_pct > 100 ? 100 : five_hr_pct) : 0;
    if(five_hr_pct >= 0) flags |= SONAR_F_5H;
    pl[n++] = (seven_day_pct >= 0) ? (uint8_t)(seven_day_pct > 100 ? 100 : seven_day_pct) : 0;
    if(seven_day_pct >= 0) flags |= SONAR_F_7D;
    {
        uint16_t cc = (cost_cents >= 0) ? (uint16_t)(cost_cents > 65535 ? 65535 : cost_cents) : 0;
        pl[n++] = (uint8_t)(cc & 0xFF);
        pl[n++] = (uint8_t)((cc >> 8) & 0xFF);
        if(cost_cents >= 0) flags |= SONAR_F_COST;
    }
    {
        uint8_t slen = 0;
        if(model && model[0]) {
            size_t ml = strlen(model);
            if(ml > SONAR_MAX_STR) ml = SONAR_MAX_STR;
            slen = (uint8_t)ml;
            flags |= SONAR_F_MODEL;
        }
        pl[n++] = slen;
        if(slen) {
            memcpy(pl + n, model, slen);
            n += slen;
        }
    }
    pl[flags_idx] = flags;
    return sonar_encode(SONAR_T_STATS, pl, n, out, out_cap);
}

static inline size_t sonar_build_event(
    uint8_t session,
    uint8_t state,
    const char* tool,
    const char* project,
    uint8_t* out,
    size_t out_cap) {
    uint8_t pl[2 + 2 + 2 * SONAR_MAX_STR];
    size_t n = 0;
    pl[n++] = session;
    pl[n++] = state;
    {
        size_t tl = (tool && tool[0]) ? strlen(tool) : 0;
        if(tl > SONAR_MAX_STR) tl = SONAR_MAX_STR;
        pl[n++] = (uint8_t)tl;
        if(tl) {
            memcpy(pl + n, tool, tl);
            n += tl;
        }
    }
    {
        size_t prl = (project && project[0]) ? strlen(project) : 0;
        if(prl > SONAR_MAX_STR) prl = SONAR_MAX_STR;
        pl[n++] = (uint8_t)prl;
        if(prl) {
            memcpy(pl + n, project, prl);
            n += prl;
        }
    }
    return sonar_encode(SONAR_T_EVENT, pl, n, out, out_cap);
}

static inline size_t sonar_build_link(uint8_t link, uint8_t* out, size_t out_cap) {
    uint8_t pl[1] = {link};
    return sonar_encode(SONAR_T_LINK, pl, 1, out, out_cap);
}

static inline size_t sonar_build_heartbeat(uint8_t* out, size_t out_cap) {
    return sonar_encode(SONAR_T_HEARTBEAT, NULL, 0, out, out_cap);
}

static inline size_t sonar_build_provision(
    const char* ssid,
    const char* pass,
    const char* relay_url,
    const char* sonar_id,
    uint8_t* out,
    size_t out_cap) {
    uint8_t pl[4 + 4 * 64];
    size_t n = 0;
    const char* fields[4] = {ssid, pass, relay_url, sonar_id};
    for(int f = 0; f < 4; f++) {
        const char* s = fields[f];
        size_t l = (s && s[0]) ? strlen(s) : 0;
        if(l > 63) l = 63;
        if(n + 1 + l > sizeof(pl)) return 0;
        pl[n++] = (uint8_t)l;
        if(l) {
            memcpy(pl + n, s, l);
            n += l;
        }
    }
    return sonar_encode(SONAR_T_PROVISION, pl, n, out, out_cap);
}

#ifdef __cplusplus
}
#endif

#endif /* SONAR_UART_H */
