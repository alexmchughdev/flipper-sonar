#include "sonar_i.h"
#include <string.h>

/*
 * WiFi provisioning flow: SSID -> password -> push provisioning frame to the
 * ESP32 over UART -> show the sonar ID. Captive portals cannot be completed by
 * a headless ESP32, so this is explicitly a "join WPA network" path; the portal
 * workaround (phone hotspot / travel router) is surfaced as on-device help.
 */

static void text_input_done_cb(void* ctx) {
    Sonar* app = ctx;
    sonar_wifi_setup_text_done(app);
}

static void start_text_input(Sonar* app, const char* header) {
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, header);
    text_input_set_result_callback(
        app->text_input, text_input_done_cb, app, app->text_buf, sizeof(app->text_buf), true);
    view_dispatcher_switch_to_view(app->view_dispatcher, SonarViewTextInput);
}

void sonar_wifi_setup_start(Sonar* app) {
    app->setup_stage = SetupStageSsid;
    app->text_buf[0] = '\0';
    start_text_input(app, "WPA Wi-Fi SSID");
}

void sonar_wifi_setup_text_done(Sonar* app) {
    if(app->setup_stage == SetupStageSsid) {
        strlcpy(app->ssid_buf, app->text_buf, sizeof(app->ssid_buf));
        app->setup_stage = SetupStagePass;
        app->text_buf[0] = '\0';
        start_text_input(app, "Wi-Fi password");
        return;
    }
    if(app->setup_stage == SetupStagePass) {
        strlcpy(app->pass_buf, app->text_buf, sizeof(app->pass_buf));
        app->setup_stage = SetupStageNone;

        /* Push creds + relay URL + sonar ID to the ESP32, persist prefs. */
        sonar_worker_send_provision(app);
        sonar_config_save(&app->config);

        sonar_show_paired_popup(app);
    }
}

static void popup_back_to_menu(void* ctx) {
    Sonar* app = ctx;
    view_dispatcher_switch_to_view(app->view_dispatcher, SonarViewMenu);
}

void sonar_show_paired_popup(Sonar* app) {
    static char msg[96];
    snprintf(
        msg,
        sizeof(msg),
        "ID: %s\nRun installer:\nnpx ... --pair %s",
        app->config.sonar_id,
        app->config.sonar_id);
    popup_reset(app->popup);
    popup_set_header(app->popup, "Paired", 64, 4, AlignCenter, AlignTop);
    popup_set_text(app->popup, msg, 64, 20, AlignCenter, AlignTop);
    popup_set_callback(app->popup, popup_back_to_menu);
    popup_set_context(app->popup, app);
    popup_set_timeout(app->popup, 8000);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, SonarViewPopup);
}

void sonar_show_portal_help(Sonar* app) {
    popup_reset(app->popup);
    popup_set_header(app->popup, "Captive portal", 64, 2, AlignCenter, AlignTop);
    popup_set_text(
        app->popup,
        "Hotels/cafes need a\nweb sign-in the board\ncan't do. Use a phone\nhotspot or travel router.",
        64,
        16,
        AlignCenter,
        AlignTop);
    popup_set_callback(app->popup, popup_back_to_menu);
    popup_set_context(app->popup, app);
    popup_set_timeout(app->popup, 10000);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, SonarViewPopup);
}
