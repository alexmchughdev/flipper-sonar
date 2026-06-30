#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_store.h"
#include "wifi.h"
#include "uart_link.h"
#include "ws_client.h"
#include "claudeogotchi_uart.h"

static const char* TAG = "claudeogotchi";

static claudeogotchi_cfg_t s_pending;
static SemaphoreHandle_t s_pending_lock;
static SemaphoreHandle_t s_reconfigure; /* given when new config arrives */
static bool s_running; /* a connection is currently active */

/* Called from the UART RX task when the FAP sends provisioning. */
static void on_provision(const ClaudeogotchiProvision* p) {
    claudeogotchi_cfg_t cfg = {0};
    strlcpy(cfg.ssid, p->ssid, sizeof(cfg.ssid));
    strlcpy(cfg.pass, p->pass, sizeof(cfg.pass));
    strlcpy(cfg.relay_url, p->relay_url, sizeof(cfg.relay_url));
    strlcpy(cfg.claudeogotchi_id, p->claudeogotchi_id, sizeof(cfg.claudeogotchi_id));

    if(!nvs_store_save(&cfg)) ESP_LOGE(TAG, "failed to persist provisioning");

    xSemaphoreTake(s_pending_lock, portMAX_DELAY);
    s_pending = cfg;
    xSemaphoreGive(s_pending_lock);
    xSemaphoreGive(s_reconfigure); /* wake the control task */
}

static void apply_config(const claudeogotchi_cfg_t* cfg) {
    if(cfg->ssid[0] == '\0' || cfg->claudeogotchi_id[0] == '\0') {
        ESP_LOGW(TAG, "incomplete config; waiting for provisioning");
        uart_link_send_link(CLAUDEOGOTCHI_LINK_CONNECTING);
        return;
    }
    if(s_running) ws_client_stop();
    uart_link_send_link(CLAUDEOGOTCHI_LINK_CONNECTING);
    wifi_connect(cfg->ssid, cfg->pass);
    ws_client_start(cfg->relay_url, cfg->claudeogotchi_id);
    s_running = true;
}

void app_main(void) {
    s_pending_lock = xSemaphoreCreateMutex();
    s_reconfigure = xSemaphoreCreateBinary();

    nvs_store_init();
    uart_link_init(on_provision);
    wifi_init();

    claudeogotchi_cfg_t cfg;
    if(nvs_store_load(&cfg)) {
        apply_config(&cfg);
    } else {
        ESP_LOGI(TAG, "no stored config; waiting for FAP provisioning over UART");
        uart_link_send_link(CLAUDEOGOTCHI_LINK_CONNECTING);
    }

    /* Control loop: re-apply on new provisioning. Heartbeat keeps the FAP's
     * stale-detection honest even if the relay is quiet. */
    for(;;) {
        if(xSemaphoreTake(s_reconfigure, pdMS_TO_TICKS(5000)) == pdTRUE) {
            claudeogotchi_cfg_t next;
            xSemaphoreTake(s_pending_lock, portMAX_DELAY);
            next = s_pending;
            xSemaphoreGive(s_pending_lock);
            ESP_LOGI(TAG, "applying new provisioning");
            apply_config(&next);
        } else {
            /* Idle tick: emit a UART heartbeat so the FAP's stale-detection
             * stays honest even when the relay is quiet. */
            uart_link_send_heartbeat();
        }
    }
}
