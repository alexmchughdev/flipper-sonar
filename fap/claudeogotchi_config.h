#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Where the FAP receives telemetry from. */
#define CLAUDEOGOTCHI_LINK_MODE_BOARD 0 /* GPIO USART <- ESP32 WiFi board (standalone) */
#define CLAUDEOGOTCHI_LINK_MODE_USB 1 /* USB CDC <- host software bridge (no board) */

/* Persisted preferences (SD card). Survives reboot. */
typedef struct {
    char claudeogotchi_id[16]; /* pairing code */
    char relay_url[128]; /* base wss URL */
    bool haptics;
    bool sound;
    uint8_t link_mode; /* CLAUDEOGOTCHI_LINK_MODE_BOARD | CLAUDEOGOTCHI_LINK_MODE_USB */
    uint8_t tracked_session; /* which session slot to display */
} ClaudeogotchiConfig;

#define CLAUDEOGOTCHI_CONFIG_PATH APP_DATA_PATH("claudeogotchi.conf")

void claudeogotchi_config_default(ClaudeogotchiConfig* c);
bool claudeogotchi_config_load(ClaudeogotchiConfig* c);
bool claudeogotchi_config_save(const ClaudeogotchiConfig* c);
