#include "sonar_i.h"
#include <string.h>

/* Cost soft-cap choices (cents). The cost bar fills against this. */
static const uint16_t COST_CAPS[] = {500, 1000, 2000, 5000, 10000};
static const char* const COST_CAP_LABELS[] = {"$5", "$10", "$20", "$50", "$100"};
#define COST_CAP_COUNT (sizeof(COST_CAPS) / sizeof(COST_CAPS[0]))

#define IDX_ID 0
#define IDX_WIFI 1
#define IDX_COST 2
#define IDX_HAPTICS 3
#define IDX_SOUND 4
#define IDX_SESSION 5
#define IDX_PORTAL 6

static const char* const ONOFF[] = {"OFF", "ON"};

static void cost_changed(VariableItem* item) {
    Sonar* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, COST_CAP_LABELS[i]);
    app->config.cost_cap_cents = COST_CAPS[i];
    sonar_config_save(&app->config);
}

static void haptics_changed(VariableItem* item) {
    Sonar* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, ONOFF[i]);
    app->config.haptics = i != 0;
    sonar_config_save(&app->config);
}

static void sound_changed(VariableItem* item) {
    Sonar* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, ONOFF[i]);
    app->config.sound = i != 0;
    sonar_config_save(&app->config);
}

static void session_changed(VariableItem* item) {
    Sonar* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    char buf[4];
    snprintf(buf, sizeof(buf), "%u", i);
    variable_item_set_current_value_text(item, buf);
    /* The worker reads tracked_session under app->mutex; guard the write too. */
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->config.tracked_session = i;
    furi_mutex_release(app->mutex);
    sonar_config_save(&app->config);
}

static void settings_enter(void* ctx, uint32_t index) {
    Sonar* app = ctx;
    if(index == IDX_WIFI) {
        sonar_wifi_setup_start(app);
    } else if(index == IDX_PORTAL) {
        sonar_show_portal_help(app);
    }
}

void sonar_settings_build(Sonar* app) {
    VariableItemList* list = app->settings_list;
    variable_item_list_reset(list);
    VariableItem* item;

    /* Sonar ID (read-only display) */
    item = variable_item_list_add(list, "Sonar ID", 1, NULL, app);
    variable_item_set_current_value_text(
        item, app->config.sonar_id[0] ? app->config.sonar_id : "------");

    /* WiFi setup action */
    variable_item_list_add(list, "WiFi Setup", 0, NULL, app);

    /* Cost cap */
    item = variable_item_list_add(list, "Cost Cap", COST_CAP_COUNT, cost_changed, app);
    {
        uint8_t idx = 2; /* default $20 */
        for(uint8_t i = 0; i < COST_CAP_COUNT; i++)
            if(COST_CAPS[i] == app->config.cost_cap_cents) idx = i;
        variable_item_set_current_value_index(item, idx);
        variable_item_set_current_value_text(item, COST_CAP_LABELS[idx]);
    }

    /* Haptics */
    item = variable_item_list_add(list, "Haptics", 2, haptics_changed, app);
    variable_item_set_current_value_index(item, app->config.haptics ? 1 : 0);
    variable_item_set_current_value_text(item, ONOFF[app->config.haptics ? 1 : 0]);

    /* Sound */
    item = variable_item_list_add(list, "Sound", 2, sound_changed, app);
    variable_item_set_current_value_index(item, app->config.sound ? 1 : 0);
    variable_item_set_current_value_text(item, ONOFF[app->config.sound ? 1 : 0]);

    /* Tracked session */
    item = variable_item_list_add(list, "Session", 8, session_changed, app);
    variable_item_set_current_value_index(item, app->config.tracked_session);
    {
        char buf[4];
        snprintf(buf, sizeof(buf), "%u", app->config.tracked_session);
        variable_item_set_current_value_text(item, buf);
    }

    /* Captive portal help */
    variable_item_list_add(list, "Captive portal?", 0, NULL, app);

    variable_item_list_set_enter_callback(list, settings_enter, app);
}
