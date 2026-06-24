#include "claudeogotchi_i.h"
#include <string.h>

enum {
    MenuLiveView,
    MenuWifiSetup,
    MenuSettings,
};

/* Generate a 6-char Crockford base32 pairing code (excludes I/L/O/U), matching
 * the relay's claudeogotchi-ID grammar. */
static void gen_claudeogotchi_id(char* out) {
    static const char* A = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    for(int i = 0; i < 6; i++) out[i] = A[furi_hal_random_get() % 32];
    out[6] = '\0';
}

static uint32_t nav_exit(void* ctx) {
    UNUSED(ctx);
    return VIEW_NONE;
}
static uint32_t nav_to_menu(void* ctx) {
    UNUSED(ctx);
    return ClaudeogotchiViewMenu;
}

static void menu_callback(void* ctx, uint32_t index) {
    Claudeogotchi* app = ctx;
    switch(index) {
    case MenuLiveView:
        view_dispatcher_switch_to_view(app->view_dispatcher, ClaudeogotchiViewMain);
        break;
    case MenuWifiSetup:
        claudeogotchi_wifi_setup_start(app);
        break;
    case MenuSettings:
        claudeogotchi_settings_build(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, ClaudeogotchiViewSettings);
        break;
    }
}

static Claudeogotchi* claudeogotchi_alloc(void) {
    Claudeogotchi* app = malloc(sizeof(Claudeogotchi));
    memset(app, 0, sizeof(Claudeogotchi));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    claudeogotchi_model_init(&app->model);

    /* config_load fills defaults internally; on first run mint a pairing code. */
    claudeogotchi_config_load(&app->config);
    if(app->config.claudeogotchi_id[0] == '\0') {
        gen_claudeogotchi_id(app->config.claudeogotchi_id);
        claudeogotchi_config_save(&app->config);
    }

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* Menu */
    app->menu = submenu_alloc();
    submenu_set_header(app->menu, "Flipper Claudeogotchi");
    submenu_add_item(app->menu, "Live View", MenuLiveView, menu_callback, app);
    submenu_add_item(app->menu, "WiFi Setup", MenuWifiSetup, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), nav_exit);
    view_dispatcher_add_view(app->view_dispatcher, ClaudeogotchiViewMenu, submenu_get_view(app->menu));

    /* Main bars+sprite view */
    app->main_view = claudeogotchi_main_view_alloc(app);
    view_set_previous_callback(app->main_view, nav_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, ClaudeogotchiViewMain, app->main_view);

    /* Settings */
    app->settings_list = variable_item_list_alloc();
    view_set_previous_callback(variable_item_list_get_view(app->settings_list), nav_to_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, ClaudeogotchiViewSettings, variable_item_list_get_view(app->settings_list));

    /* Text input */
    app->text_input = text_input_alloc();
    view_set_previous_callback(text_input_get_view(app->text_input), nav_to_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, ClaudeogotchiViewTextInput, text_input_get_view(app->text_input));

    /* Popup */
    app->popup = popup_alloc();
    view_set_previous_callback(popup_get_view(app->popup), nav_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, ClaudeogotchiViewPopup, popup_get_view(app->popup));

    return app;
}

static void claudeogotchi_free(Claudeogotchi* app) {
    claudeogotchi_worker_stop(app);

    view_dispatcher_remove_view(app->view_dispatcher, ClaudeogotchiViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ClaudeogotchiViewMain);
    view_dispatcher_remove_view(app->view_dispatcher, ClaudeogotchiViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, ClaudeogotchiViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, ClaudeogotchiViewPopup);

    submenu_free(app->menu);
    claudeogotchi_main_view_free(app->main_view);
    variable_item_list_free(app->settings_list);
    text_input_free(app->text_input);
    popup_free(app->popup);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    furi_mutex_free(app->mutex);
    free(app);
}

int32_t claudeogotchi_app(void* p) {
    UNUSED(p);
    Claudeogotchi* app = claudeogotchi_alloc();

    claudeogotchi_worker_start(app);

    view_dispatcher_switch_to_view(app->view_dispatcher, ClaudeogotchiViewMain);
    view_dispatcher_run(app->view_dispatcher);

    claudeogotchi_free(app);
    return 0;
}
