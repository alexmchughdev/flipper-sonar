#include "nvs_store.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "nvs_store";
#define NS "claudeogotchi"

void nvs_store_init(void) {
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}

static bool get_str(nvs_handle_t h, const char* key, char* dst, size_t cap) {
    size_t len = cap;
    esp_err_t e = nvs_get_str(h, key, dst, &len);
    if(e != ESP_OK) {
        dst[0] = '\0';
        return false;
    }
    return true;
}

bool nvs_store_load(claudeogotchi_cfg_t* out) {
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    if(nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    get_str(h, "ssid", out->ssid, sizeof(out->ssid));
    get_str(h, "pass", out->pass, sizeof(out->pass));
    get_str(h, "relay", out->relay_url, sizeof(out->relay_url));
    get_str(h, "id", out->claudeogotchi_id, sizeof(out->claudeogotchi_id));
    nvs_close(h);
    bool ok = out->ssid[0] != '\0' && out->claudeogotchi_id[0] != '\0';
    if(ok) ESP_LOGI(TAG, "loaded config for claudeogotchi id %s", out->claudeogotchi_id);
    return ok;
}

bool nvs_store_save(const claudeogotchi_cfg_t* cfg) {
    nvs_handle_t h;
    if(nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = ESP_OK;
    e |= nvs_set_str(h, "ssid", cfg->ssid);
    e |= nvs_set_str(h, "pass", cfg->pass);
    e |= nvs_set_str(h, "relay", cfg->relay_url);
    e |= nvs_set_str(h, "id", cfg->claudeogotchi_id);
    if(e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if(e == ESP_OK) ESP_LOGI(TAG, "saved config for claudeogotchi id %s", cfg->claudeogotchi_id);
    return e == ESP_OK;
}

void nvs_store_clear(void) {
    nvs_handle_t h;
    if(nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}
