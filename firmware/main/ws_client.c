#include "ws_client.h"
#include <string.h>
#include <stdlib.h>
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "uart_link.h"
#include "claudeogotchi_uart.h"

static const char* TAG = "ws_client";
static esp_websocket_client_handle_t s_client;

/* Null-safe int extractor: returns -1 if key absent, null, or non-numeric. */
static int json_pct(const cJSON* root, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, key);
    if(!v || cJSON_IsNull(v) || !cJSON_IsNumber(v)) return -1;
    int n = (int)(v->valuedouble + 0.5);
    if(n < 0) n = 0;
    if(n > 100) n = 100;
    return n;
}

static int json_cost_cents(const cJSON* root, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, key);
    if(!v || cJSON_IsNull(v) || !cJSON_IsNumber(v)) return -1;
    double c = v->valuedouble;
    if(c < 0) return -1;
    int cents = (int)(c * 100.0 + 0.5);
    return cents > 65535 ? 65535 : cents;
}

static const char* json_str(const cJSON* root, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, key);
    return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : NULL;
}

static int json_int(const cJSON* root, const char* key, int dflt) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, key);
    return (v && cJSON_IsNumber(v)) ? (int)v->valuedouble : dflt;
}

static uint8_t state_from_str(const char* s) {
    if(!s) return CLAUDEOGOTCHI_STATE_IDLE;
    if(!strcmp(s, "working")) return CLAUDEOGOTCHI_STATE_WORKING;
    if(!strcmp(s, "waiting-approval")) return CLAUDEOGOTCHI_STATE_WAITING_APPROVAL;
    if(!strcmp(s, "waiting-input")) return CLAUDEOGOTCHI_STATE_WAITING_INPUT;
    if(!strcmp(s, "done")) return CLAUDEOGOTCHI_STATE_DONE;
    return CLAUDEOGOTCHI_STATE_IDLE;
}

/* Parse one relay JSON message and emit the matching UART frame. */
static void handle_message(const char* data, int len) {
    cJSON* root = cJSON_ParseWithLength(data, len);
    if(!root) {
        ESP_LOGW(TAG, "bad JSON from relay");
        return;
    }
    const char* type = json_str(root, "type");
    uint8_t buf[CLAUDEOGOTCHI_MAX_FRAME];
    size_t n = 0;
    uint8_t session = (uint8_t)json_int(root, "session", 0);

    if(type && !strcmp(type, "stats")) {
        /* tokens arrive as an absolute count; the frame carries units of 100. */
        int tokens_h = -1;
        {
            const cJSON* tv = cJSON_GetObjectItemCaseSensitive(root, "tokens");
            if(tv && cJSON_IsNumber(tv) && tv->valuedouble >= 0)
                tokens_h = (int)(tv->valuedouble / 100.0 + 0.5);
        }
        n = claudeogotchi_build_stats(
            session,
            json_pct(root, "ctxPct"),
            json_pct(root, "fiveHrPct"),
            json_pct(root, "sevenDayPct"),
            json_cost_cents(root, "costUsd"),
            json_str(root, "model"),
            tokens_h,
            buf,
            sizeof(buf));
    } else if(type && !strcmp(type, "event")) {
        n = claudeogotchi_build_event(
            session,
            state_from_str(json_str(root, "state")),
            json_str(root, "tool"),
            json_str(root, "project"),
            buf,
            sizeof(buf));
    } else if(type && !strcmp(type, "heartbeat")) {
        n = claudeogotchi_build_heartbeat(buf, sizeof(buf));
    }

    if(n) uart_link_send(buf, n);
    cJSON_Delete(root);
}

static void on_ws_event(void* arg, esp_event_base_t base, int32_t id, void* event_data) {
    (void)arg;
    (void)base;
    esp_websocket_event_data_t* d = (esp_websocket_event_data_t*)event_data;
    switch(id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "relay connected");
        uart_link_send_link(CLAUDEOGOTCHI_LINK_ONLINE);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "relay disconnected");
        uart_link_send_link(CLAUDEOGOTCHI_LINK_STALE);
        break;
    case WEBSOCKET_EVENT_DATA:
        /* Only text frames (opcode 0x1) carry JSON. Ignore ping/pong/close. */
        if(d->op_code == 0x01 && d->data_len > 0 && d->payload_offset == 0 &&
           d->data_len == d->payload_len) {
            handle_message((const char*)d->data, d->data_len);
        }
        break;
    default:
        break;
    }
}

void ws_client_start(const char* relay_url, const char* claudeogotchi_id) {
    /* Build wss://host/egress/<id> from the provisioned base URL. */
    static char uri[256];
    size_t blen = strlen(relay_url);
    while(blen > 0 && relay_url[blen - 1] == '/') blen--; /* trim trailing slash */
    snprintf(uri, sizeof(uri), "%.*s/egress/%s", (int)blen, relay_url, claudeogotchi_id);
    ESP_LOGI(TAG, "connecting to %s", uri);

    uart_link_send_link(CLAUDEOGOTCHI_LINK_CONNECTING);

    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .crt_bundle_attach = esp_crt_bundle_attach, /* verify relay TLS cert */
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,
        .disable_auto_reconnect = false, /* client reconnects with backoff */
    };
    s_client = esp_websocket_client_init(&cfg);
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws_event, NULL);
    esp_websocket_client_start(s_client);
}

void ws_client_stop(void) {
    if(s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
}
