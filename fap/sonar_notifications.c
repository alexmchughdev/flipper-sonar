#include "sonar_i.h"
#include <notification/notification_messages.h>

/* Debounce window: rapid PreToolUse/PostToolUse churn must not buzz. Only true
 * transitions into waiting-approval / done chime, and at most once per window. */
#define HAPTIC_DEBOUNCE_MS 1500

/* Distinct approval alert: two rising beeps + a longer vibro. */
static const NotificationSequence seq_approval = {
    &message_vibro_on,
    &message_note_c5,
    &message_delay_100,
    &message_note_e5,
    &message_delay_100,
    &message_vibro_off,
    &message_sound_off,
    NULL,
};

/* Success: single chime + single short vibro. */
static const NotificationSequence seq_done = {
    &message_vibro_on,
    &message_note_g5,
    &message_delay_50,
    &message_vibro_off,
    &message_note_c6,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

/* Sound-only variants when vibration is disabled but sound is on. */
static const NotificationSequence seq_approval_sound = {
    &message_note_c5,
    &message_delay_100,
    &message_note_e5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

static const NotificationSequence seq_done_sound = {
    &message_note_g5,
    &message_delay_50,
    &message_note_c6,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

/* Vibro-only when sound is disabled but haptics on. */
static const NotificationSequence seq_approval_vibro = {
    &message_vibro_on,
    &message_delay_250,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence seq_done_vibro = {
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    NULL,
};

void sonar_notify_transition(Sonar* app, uint8_t old_state, uint8_t new_state) {
    if(old_state == new_state) return; /* no transition: no haptics */

    /* Only these two transitions are notable per SPEC §3.3. */
    bool to_approval = new_state == SONAR_STATE_WAITING_APPROVAL;
    bool to_done = new_state == SONAR_STATE_DONE;
    if(!to_approval && !to_done) return;

    uint32_t now = furi_get_tick();
    if(now - app->last_haptic_tick < furi_ms_to_ticks(HAPTIC_DEBOUNCE_MS)) return;
    app->last_haptic_tick = now;

    bool haptics = app->config.haptics;
    bool sound = app->config.sound;
    if(!haptics && !sound) return;

    const NotificationSequence* seq;
    if(to_approval) {
        seq = (haptics && sound) ? &seq_approval :
              haptics            ? &seq_approval_vibro :
                                   &seq_approval_sound;
    } else {
        seq = (haptics && sound) ? &seq_done :
              haptics            ? &seq_done_vibro :
                                   &seq_done_sound;
    }
    notification_message(app->notifications, seq);
}
