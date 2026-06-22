#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <gui/modules/popup.h>
#include <notification/notification.h>
#include "sonar_state.h"
#include "sonar_config.h"

typedef enum {
    SonarViewMenu,
    SonarViewMain,
    SonarViewSettings,
    SonarViewTextInput,
    SonarViewPopup,
} SonarViewId;

/* Custom ViewDispatcher events. */
typedef enum {
    SonarEventRedraw = 100,
} SonarCustomEvent;

typedef enum {
    SetupStageNone,
    SetupStageSsid,
    SetupStagePass,
} SetupStage;

typedef struct Sonar {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    NotificationApp* notifications;

    Submenu* menu;
    View* main_view; /* custom bars+sprite view */
    VariableItemList* settings_list;
    TextInput* text_input;
    Popup* popup;

    /* UART worker */
    FuriThread* worker;
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
    volatile bool worker_running;

    /* shared state */
    FuriMutex* mutex;
    SonarModel model;
    SonarConfig config;

    /* notification transition tracking (owned by worker) */
    uint8_t last_notified_state;
    uint32_t last_haptic_tick;

    /* wifi setup scratch */
    SetupStage setup_stage;
    char ssid_buf[64];
    char pass_buf[64];
    char text_buf[64];

    /* animation frame counter (owned by view) */
    uint8_t anim_frame;
} Sonar;

/* main view */
View* sonar_main_view_alloc(Sonar* app);
void sonar_main_view_free(View* view);

/* settings */
void sonar_settings_build(Sonar* app);

/* wifi setup flow */
void sonar_wifi_setup_start(Sonar* app);
void sonar_wifi_setup_text_done(Sonar* app);
void sonar_show_paired_popup(Sonar* app);
void sonar_show_portal_help(Sonar* app);

/* worker */
void sonar_worker_start(Sonar* app);
void sonar_worker_stop(Sonar* app);
void sonar_worker_send_provision(Sonar* app);

/* notifications */
void sonar_notify_transition(Sonar* app, uint8_t old_state, uint8_t new_state);
