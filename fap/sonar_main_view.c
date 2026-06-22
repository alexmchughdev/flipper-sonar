#include "sonar_i.h"
#include <gui/elements.h>

#define ANIM_PERIOD_MS 150

typedef struct {
    Sonar* app;
    FuriTimer* timer;
    uint8_t frame; /* animation frame counter */
} MainViewModel;

/* ---- sprite: a tiny dolphin/sonar glyph whose pose is driven by state ---- */
static void draw_sprite(Canvas* canvas, int x, int y, uint8_t state, uint8_t frame) {
    /* body: a small rounded blob */
    canvas_draw_circle(canvas, x + 6, y + 6, 5);
    canvas_draw_dot(canvas, x + 9, y + 4); /* eye */

    switch(state) {
    case SONAR_STATE_WORKING: {
        /* ping pulse: expanding arcs to the right, animated by frame */
        int r = 3 + (frame % 4) * 2;
        canvas_draw_circle(canvas, x + 6, y + 6, r > 11 ? 11 : r);
        break;
    }
    case SONAR_STATE_WAITING_APPROVAL: {
        /* alert pose: exclamation above the head, blinking */
        if(frame % 2) {
            canvas_draw_line(canvas, x + 14, y, x + 14, y + 5);
            canvas_draw_dot(canvas, x + 14, y + 7);
        }
        break;
    }
    case SONAR_STATE_WAITING_INPUT: {
        /* gentle question mark */
        canvas_draw_str(canvas, x + 12, y + 6, "?");
        break;
    }
    case SONAR_STATE_DONE: {
        /* check mark */
        canvas_draw_line(canvas, x + 12, y + 6, x + 14, y + 8);
        canvas_draw_line(canvas, x + 14, y + 8, x + 18, y + 2);
        break;
    }
    default: /* idle: a small resting wave */
        canvas_draw_line(canvas, x + 12, y + 8, x + 18, y + 8);
        break;
    }
}

static const char* state_label(uint8_t state) {
    switch(state) {
    case SONAR_STATE_WORKING:
        return "working";
    case SONAR_STATE_WAITING_APPROVAL:
        return "approve?";
    case SONAR_STATE_WAITING_INPUT:
        return "input?";
    case SONAR_STATE_DONE:
        return "done";
    default:
        return "idle";
    }
}

/* One labelled bar. valid=false draws an empty frame + "--" (never zero-flap). */
static void draw_bar(
    Canvas* canvas,
    int y,
    const char* label,
    bool valid,
    uint8_t pct,
    const char* value) {
    const int bx = 20, bw = 70, bh = 7;
    canvas_draw_str(canvas, 0, y + 6, label);
    canvas_draw_frame(canvas, bx, y, bw, bh);
    if(valid) {
        int fill = (bw - 2) * pct / 100;
        if(fill > 0) canvas_draw_box(canvas, bx + 1, y + 1, fill, bh - 2);
    }
    canvas_draw_str(canvas, bx + bw + 3, y + 6, valid ? value : "--");
}

static void draw_link_glyph(Canvas* canvas, SonarLink link, uint8_t frame) {
    const int gx = 120, gy = 7;
    switch(link) {
    case SonarLinkOnline:
        canvas_draw_disc(canvas, gx, gy - 2, 2);
        break;
    case SonarLinkConnecting:
        /* animated dots */
        for(int i = 0; i < (frame % 3) + 1; i++) canvas_draw_dot(canvas, gx - 4 + i * 3, gy - 2);
        break;
    case SonarLinkStale:
        canvas_draw_str(canvas, gx - 4, gy, "x");
        break;
    }
}

static void main_draw(Canvas* canvas, void* model_v) {
    MainViewModel* vm = model_v;
    Sonar* app = vm->app;

    /* Snapshot the shared model under the lock; draw from the copy so the
     * critical section stays tiny and the worker is never blocked on rendering. */
    SonarModel m;
    uint16_t cost_cap;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    m = app->model;
    cost_cap = app->config.cost_cap_cents;
    furi_mutex_release(app->mutex);

    canvas_set_font(canvas, FontSecondary);

    /* Header: model name + link glyph */
    const char* model_name = m.model[0] ? m.model : "(no model)";
    canvas_draw_str(canvas, 0, 7, model_name);
    draw_link_glyph(canvas, m.link, vm->frame);
    canvas_draw_line(canvas, 0, 9, 127, 9);

    /* Bars */
    char buf[16];
    snprintf(buf, sizeof(buf), "%u%%", m.ctx_pct);
    draw_bar(canvas, 11, "CTX", m.ctx_valid, m.ctx_pct, buf);
    snprintf(buf, sizeof(buf), "%u%%", m.five_pct);
    draw_bar(canvas, 20, "5H", m.five_valid, m.five_pct, buf);
    snprintf(buf, sizeof(buf), "%u%%", m.seven_pct);
    draw_bar(canvas, 29, "7D", m.seven_valid, m.seven_pct, buf);

    /* Cost bar: proportional to the user's soft cap. */
    uint8_t cost_pct = 0;
    if(cost_cap > 0 && m.cost_valid) {
        uint32_t p = (uint32_t)m.cost_cents * 100u / cost_cap;
        cost_pct = p > 100 ? 100 : (uint8_t)p;
    }
    snprintf(buf, sizeof(buf), "$%u.%02u", m.cost_cents / 100, m.cost_cents % 100);
    draw_bar(canvas, 38, "$", m.cost_valid, cost_pct, buf);

    /* State + sprite */
    canvas_draw_line(canvas, 0, 48, 127, 48);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 60, state_label(m.state));
    canvas_set_font(canvas, FontSecondary);
    if(m.tool[0] && m.state == SONAR_STATE_WORKING) {
        canvas_draw_str(canvas, 44, 60, m.tool);
    }
    draw_sprite(canvas, 104, 50, m.state, vm->frame);
}

static bool main_input(InputEvent* event, void* ctx) {
    Sonar* app = ctx;
    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        /* Cycle the tracked session among those actually seen, so concurrent
         * sessions are selectable and the display never flaps on its own. */
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        uint32_t seen = app->model.seen_sessions;
        uint8_t cur = app->config.tracked_session;
        if(seen) {
            for(int i = 1; i <= 32; i++) {
                uint8_t cand =
                    (event->key == InputKeyRight) ? (cur + i) % 32 : (cur + 32 - i) % 32;
                if(seen & (1u << cand)) {
                    app->config.tracked_session = cand;
                    break;
                }
            }
        }
        furi_mutex_release(app->mutex);
        sonar_config_save(&app->config);
        return true;
    }
    /* Back / Ok fall through to the dispatcher (navigates to the menu). */
    return false;
}

static void main_timer_cb(void* ctx) {
    View* view = ctx;
    with_view_model(view, MainViewModel * vm, { vm->frame++; }, true);
}

/* enter/exit receive the view's context (the Sonar app); reach the View through
 * app->main_view. */
static void main_enter(void* ctx) {
    Sonar* app = ctx;
    View* view = app->main_view;
    with_view_model(
        view,
        MainViewModel * vm,
        {
            if(!vm->timer) {
                vm->timer = furi_timer_alloc(main_timer_cb, FuriTimerTypePeriodic, view);
            }
            furi_timer_start(vm->timer, furi_ms_to_ticks(ANIM_PERIOD_MS));
        },
        false);
}

static void main_exit(void* ctx) {
    Sonar* app = ctx;
    View* view = app->main_view;
    with_view_model(
        view, MainViewModel * vm, { if(vm->timer) furi_timer_stop(vm->timer); }, false);
}

View* sonar_main_view_alloc(Sonar* app) {
    View* view = view_alloc();
    view_allocate_model(view, ViewModelTypeLocking, sizeof(MainViewModel));
    with_view_model(
        view,
        MainViewModel * vm,
        {
            vm->app = app;
            vm->timer = NULL;
            vm->frame = 0;
        },
        false);
    view_set_context(view, app);
    view_set_draw_callback(view, main_draw);
    view_set_input_callback(view, main_input);
    view_set_enter_callback(view, main_enter);
    view_set_exit_callback(view, main_exit);
    view_set_previous_callback(view, NULL); /* set by app to SonarViewMenu */
    return view;
}

void sonar_main_view_free(View* view) {
    with_view_model(
        view, MainViewModel * vm, { if(vm->timer) furi_timer_free(vm->timer); }, false);
    view_free(view);
}
