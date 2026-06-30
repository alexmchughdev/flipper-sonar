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
#include "claudeogotchi_state.h"
#include "claudeogotchi_config.h"

typedef enum {
    ClaudeogotchiViewMenu,
    ClaudeogotchiViewMain,
    ClaudeogotchiViewSettings,
    ClaudeogotchiViewTextInput,
    ClaudeogotchiViewPopup,
} ClaudeogotchiViewId;

typedef enum {
    SetupStageNone,
    SetupStageSsid,
    SetupStagePass,
} SetupStage;

typedef struct Claudeogotchi {
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
    FuriHalSerialHandle* serial; /* board mode: GPIO USART handle */
    FuriHalUsbInterface* usb_prev; /* USB mode: USB config to restore on exit */
    FuriStreamBuffer* rx_stream;
    volatile bool worker_running;

    /* shared state */
    FuriMutex* mutex;
    ClaudeogotchiModel model;
    ClaudeogotchiConfig config;

    /* notification transition tracking (owned by worker) */
    uint32_t last_haptic_tick;

    /* wifi setup scratch */
    SetupStage setup_stage;
    char ssid_buf[64];
    char pass_buf[64];
    char text_buf[64];
} Claudeogotchi;

/* main view */
View* claudeogotchi_main_view_alloc(Claudeogotchi* app);
void claudeogotchi_main_view_free(View* view);
void claudeogotchi_main_view_refresh(View* view); /* force redraw on data arrival */

/* settings */
void claudeogotchi_settings_build(Claudeogotchi* app);

/* wifi setup flow */
void claudeogotchi_wifi_setup_start(Claudeogotchi* app);
void claudeogotchi_wifi_setup_text_done(Claudeogotchi* app);
void claudeogotchi_show_paired_popup(Claudeogotchi* app);
void claudeogotchi_show_portal_help(Claudeogotchi* app);

/* worker */
void claudeogotchi_worker_start(Claudeogotchi* app);
void claudeogotchi_worker_stop(Claudeogotchi* app);
void claudeogotchi_worker_send_provision(Claudeogotchi* app);

/* notifications */
void claudeogotchi_notify_transition(Claudeogotchi* app, uint8_t old_state, uint8_t new_state);
