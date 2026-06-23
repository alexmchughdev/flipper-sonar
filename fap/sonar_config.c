#include "sonar_config.h"
#include <string.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <furi.h>

#define CONFIG_HEADER "Flipper Claudeogotchi Config"
#define CONFIG_VERSION 1

void sonar_config_default(SonarConfig* c) {
    memset(c, 0, sizeof(*c));
    /* Placeholder hosted relay; overridden by the installer's --relay or by
     * re-provisioning. See docs/blocked.md. */
    strlcpy(c->relay_url, "wss://relay.flipper-sonar.dev", sizeof(c->relay_url));
    c->haptics = true;
    c->sound = true;
    c->link_mode = SONAR_LINK_MODE_BOARD;
    c->tracked_session = 0;
}

bool sonar_config_load(SonarConfig* c) {
    sonar_config_default(c);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    FuriString* tmp = furi_string_alloc();

    do {
        uint32_t version;
        if(!flipper_format_file_open_existing(ff, SONAR_CONFIG_PATH)) break;
        if(!flipper_format_read_header(ff, tmp, &version)) break;
        if(furi_string_cmp_str(tmp, CONFIG_HEADER) != 0) break;

        if(flipper_format_read_string(ff, "sonar_id", tmp))
            strlcpy(c->sonar_id, furi_string_get_cstr(tmp), sizeof(c->sonar_id));
        if(flipper_format_read_string(ff, "relay_url", tmp))
            strlcpy(c->relay_url, furi_string_get_cstr(tmp), sizeof(c->relay_url));

        uint32_t u32;
        if(flipper_format_read_uint32(ff, "haptics", &u32, 1)) c->haptics = u32 != 0;
        if(flipper_format_read_uint32(ff, "sound", &u32, 1)) c->sound = u32 != 0;
        if(flipper_format_read_uint32(ff, "link_mode", &u32, 1)) c->link_mode = (uint8_t)u32;
        if(flipper_format_read_uint32(ff, "tracked_session", &u32, 1))
            c->tracked_session = (uint8_t)u32;
        ok = true;
    } while(false);

    furi_string_free(tmp);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool sonar_config_save(const SonarConfig* c) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;

    do {
        if(!flipper_format_file_open_always(ff, SONAR_CONFIG_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, CONFIG_HEADER, CONFIG_VERSION)) break;
        if(!flipper_format_write_string_cstr(ff, "sonar_id", c->sonar_id)) break;
        if(!flipper_format_write_string_cstr(ff, "relay_url", c->relay_url)) break;
        uint32_t u32 = c->haptics ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "haptics", &u32, 1)) break;
        u32 = c->sound ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "sound", &u32, 1)) break;
        u32 = c->link_mode;
        if(!flipper_format_write_uint32(ff, "link_mode", &u32, 1)) break;
        u32 = c->tracked_session;
        if(!flipper_format_write_uint32(ff, "tracked_session", &u32, 1)) break;
        ok = true;
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
