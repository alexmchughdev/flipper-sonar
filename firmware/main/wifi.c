#include "wifi.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

static const char* TAG = "wifi";
static EventGroupHandle_t s_events;
#define BIT_CONNECTED BIT0

/* Bounded exponential backoff on association failure. */
static int s_backoff_ms = 1000;
#define BACKOFF_MAX_MS 30000

static void on_wifi(void* arg, esp_event_base_t base, int32_t id, void* data) {
    (void)arg;
    (void)data;
    if(base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, BIT_CONNECTED);
        ESP_LOGW(TAG, "disconnected; retry in %d ms", s_backoff_ms);
        vTaskDelay(pdMS_TO_TICKS(s_backoff_ms));
        s_backoff_ms = s_backoff_ms * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : s_backoff_ms * 2;
        esp_wifi_connect();
    } else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_backoff_ms = 1000; /* reset backoff on success */
        xEventGroupSetBits(s_events, BIT_CONNECTED);
        ESP_LOGI(TAG, "got IP, connected");
    }
}

void wifi_init(void) {
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

void wifi_connect(const char* ssid, const char* pass) {
    wifi_config_t wc = {0};
    strlcpy((char*)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char*)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    s_backoff_ms = 1000;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "connecting to SSID '%s'", ssid);
}

bool wifi_is_connected(void) {
    return s_events && (xEventGroupGetBits(s_events) & BIT_CONNECTED);
}
