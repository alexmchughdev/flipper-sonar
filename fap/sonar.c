#include "sonar_i.h"
#include <string.h>

enum {
    MenuLiveView,
    MenuWifiSetup,
    MenuSettings,
};

/* Generate a 6-char Crockford base32 pairing code (excludes I/L/O/U), matching
 * the relay's sonar-ID grammar. */
static void gen_sonar_id(char* out) {
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
    return SonarViewMenu;
}

static void menu_callback(void* ctx, uint32_t index) {
    Sonar* app = ctx;
    switch(index) {
    case MenuLiveView:
        view_dispatcher_switch_to_view(app->view_dispatcher, SonarViewMain);
        break;
    case MenuWifiSetup:
        sonar_wifi_setup_start(app);
        break;
    case MenuSettings:
        sonar_settings_build(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, SonarViewSettings);
        break;
    }
}

static Sonar* sonar_alloc(void) {
    Sonar* app = malloc(sizeof(Sonar));
    memset(app, 0, sizeof(Sonar));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    sonar_model_init(&app->model);

    if(!sonar_config_load(&app->config)) {
        /* First run: mint a pairing code and persist a default config. */
    }
    if(app->config.sonar_id[0] == '\0') {
        gen_sonar_id(app->config.sonar_id);
        sonar_config_save(&app->config);
    }

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* Menu */
    app->menu = submenu_alloc();
    submenu_set_header(app->menu, "Flipper Sonar");
    submenu_add_item(app->menu, "Live View", MenuLiveView, menu_callback, app);
    submenu_add_item(app->menu, "WiFi Setup", MenuWifiSetup, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), nav_exit);
    view_dispatcher_add_view(app->view_dispatcher, SonarViewMenu, submenu_get_view(app->menu));

    /* Main bars+sprite view */
    app->main_view = sonar_main_view_alloc(app);
    view_set_previous_callback(app->main_view, nav_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, SonarViewMain, app->main_view);

    /* Settings */
    app->settings_list = variable_item_list_alloc();
    view_set_previous_callback(variable_item_list_get_view(app->settings_list), nav_to_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, SonarViewSettings, variable_item_list_get_view(app->settings_list));

    /* Text input */
    app->text_input = text_input_alloc();
    view_set_previous_callback(text_input_get_view(app->text_input), nav_to_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, SonarViewTextInput, text_input_get_view(app->text_input));

    /* Popup */
    app->popup = popup_alloc();
    view_set_previous_callback(popup_get_view(app->popup), nav_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, SonarViewPopup, popup_get_view(app->popup));

    return app;
}

static void sonar_free(Sonar* app) {
    sonar_worker_stop(app);

    view_dispatcher_remove_view(app->view_dispatcher, SonarViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, SonarViewMain);
    view_dispatcher_remove_view(app->view_dispatcher, SonarViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, SonarViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, SonarViewPopup);

    submenu_free(app->menu);
    sonar_main_view_free(app->main_view);
    variable_item_list_free(app->settings_list);
    text_input_free(app->text_input);
    popup_free(app->popup);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    furi_mutex_free(app->mutex);
    free(app);
}

int32_t sonar_app(void* p) {
    UNUSED(p);
    Sonar* app = sonar_alloc();

    sonar_worker_start(app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SonarViewMain);
    view_dispatcher_run(app->view_dispatcher);

    sonar_free(app);
    return 0;
}
