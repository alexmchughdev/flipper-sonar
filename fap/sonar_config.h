#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Persisted preferences (SD card). Survives reboot. */
typedef struct {
    char sonar_id[16]; /* pairing code */
    char relay_url[128]; /* base wss URL */
    bool haptics;
    bool sound;
    uint8_t tracked_session; /* which session slot to display */
} SonarConfig;

#define SONAR_CONFIG_PATH APP_DATA_PATH("sonar.conf")

void sonar_config_default(SonarConfig* c);
bool sonar_config_load(SonarConfig* c);
bool sonar_config_save(const SonarConfig* c);
