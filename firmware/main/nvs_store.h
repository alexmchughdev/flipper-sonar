#pragma once
#include <stdbool.h>

/* Provisioned configuration, persisted in NVS. Never hard-coded. */
typedef struct {
    char ssid[64];
    char pass[64];
    char relay_url[128]; /* base, e.g. wss://relay.example  (no trailing slash) */
    char claudeogotchi_id[16]; /* 6-char pairing code */
} claudeogotchi_cfg_t;

void nvs_store_init(void);

/* Load config from NVS. Returns true if a usable config (ssid + claudeogotchi_id) is
 * present. On false, the bridge waits for provisioning over UART. */
bool nvs_store_load(claudeogotchi_cfg_t* out);

/* Persist config. Returns true on success. */
bool nvs_store_save(const claudeogotchi_cfg_t* cfg);

