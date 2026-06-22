#include "sonar_i.h"
#include <gui/elements.h>
#include <math.h>

#define ANIM_PERIOD_MS 150

typedef struct {
    Sonar* app;
    FuriTimer* timer;
    uint8_t frame; /* animation frame counter */
    int preview; /* -1 = live state; 0..4 = forced state for sprite preview (OK key) */
} MainViewModel;

/*
 * The Claude "spark" — a radial sunburst — rendered 1-bit and animated by state.
 * Drawn procedurally (12 rays, alternating long/short) so it rotates/breathes
 * crisply at any size. There is no official Claude Code sprite bitmap to copy;
 * to trace an exact pixel reference, drop a PNG and we convert it to an XBM.
 */
static void draw_spark(Canvas* canvas, int cx, int cy, float angle, int rlong, int rshort) {
    const int rays = 12;
    for(int i = 0; i < rays; i++) {
        float a = angle + (float)i * (2.0f * (float)M_PI / (float)rays);
        int len = (i % 2 == 0) ? rlong : rshort;
        int x2 = cx + (int)lroundf(cosf(a) * (float)len);
        int y2 = cy + (int)lroundf(sinf(a) * (float)len);
        canvas_draw_line(canvas, cx, cy, x2, y2);
    }
    canvas_draw_disc(canvas, cx, cy, 1); /* solid core */
}

/* cx,cy = spark centre. Each state gets a distinct animation. */
static void draw_sprite(Canvas* canvas, int cx, int cy, uint8_t state, uint8_t frame) {
    switch(state) {
    case SONAR_STATE_WORKING:
        /* spinning spark — the "thinking" motion */
        draw_spark(canvas, cx, cy, (float)frame * 0.45f, 7, 4);
        break;
    case SONAR_STATE_WAITING_APPROVAL:
        draw_spark(canvas, cx, cy, 0.26f, 7, 4);
        if(frame % 2) { /* blinking alert */
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, cx + 11, cy + 4, "!");
            canvas_set_font(canvas, FontSecondary);
        }
        break;
    case SONAR_STATE_WAITING_INPUT:
        draw_spark(canvas, cx, cy, 0.26f, 7, 4);
        canvas_draw_str(canvas, cx + 10, cy + 4, "?");
        break;
    case SONAR_STATE_DONE:
        draw_spark(canvas, cx, cy, 0.26f, 7, 4);
        canvas_draw_line(canvas, cx + 9, cy + 1, cx + 12, cy + 4); /* check */
        canvas_draw_line(canvas, cx + 12, cy + 4, cx + 16, cy - 3);
        break;
    default: { /* idle: slow breathe + slow drift */
        int b = 6 + (int)((frame / 4) % 3); /* 6..8 */
        draw_spark(canvas, cx, cy, (float)frame * 0.05f, b, b - 3);
        break;
    }
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
    int h,
    const char* label,
    bool valid,
    uint8_t pct,
    const char* value) {
    const int bx = 34, bw = 60;
    int texty = y + h - 1; /* baseline near the bar's vertical centre */
    canvas_draw_str(canvas, 0, texty, label);
    canvas_draw_frame(canvas, bx, y, bw, h);
    if(valid) {
        int fill = (bw - 2) * pct / 100;
        if(fill > 0) canvas_draw_box(canvas, bx + 1, y + 1, fill, h - 2);
    }
    canvas_draw_str(canvas, bx + bw + 3, texty, valid ? value : "--");
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
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    m = app->model;
    furi_mutex_release(app->mutex);

    canvas_set_font(canvas, FontSecondary);

    /* Header: model name + link glyph */
    const char* model_name = m.model[0] ? m.model : "(no model)";
    canvas_draw_str(canvas, 0, 7, model_name);
    draw_link_glyph(canvas, m.link, vm->frame);
    canvas_draw_line(canvas, 0, 9, 127, 9);

    /* Three usage bars: session (context fill), 5h limit, weekly (7d) limit.
     * No cost — usage is what matters regardless of plan. */
    char buf[16];
    snprintf(buf, sizeof(buf), "%u%%", m.ctx_pct);
    draw_bar(canvas, 12, 9, "SESS", m.ctx_valid, m.ctx_pct, buf);
    snprintf(buf, sizeof(buf), "%u%%", m.five_pct);
    draw_bar(canvas, 24, 9, "5H", m.five_valid, m.five_pct, buf);
    snprintf(buf, sizeof(buf), "%u%%", m.seven_pct);
    draw_bar(canvas, 36, 9, "WEEK", m.seven_valid, m.seven_pct, buf);

    /* State + sprite. preview (OK key) forces a state so every sprite animation
     * can be checked on-device before the telemetry pipeline is live. */
    uint8_t disp_state = vm->preview >= 0 ? (uint8_t)vm->preview : m.state;
    canvas_draw_line(canvas, 0, 48, 127, 48);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 60, state_label(disp_state));
    canvas_set_font(canvas, FontSecondary);
    if(vm->preview >= 0) {
        canvas_draw_str(canvas, 60, 60, "demo");
    } else if(m.tool[0] && m.state == SONAR_STATE_WORKING) {
        canvas_draw_str(canvas, 44, 60, m.tool);
    }
    draw_sprite(canvas, 110, 56, disp_state, vm->frame);
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

    if(event->key == InputKeyOk) {
        /* Cycle the sprite preview: idle->working->approval->input->done->live. */
        with_view_model(
            app->main_view,
            MainViewModel * vm,
            { vm->preview = (vm->preview >= SONAR_STATE_DONE) ? -1 : vm->preview + 1; },
            true);
        return true;
    }

    /* Back falls through to the dispatcher (navigates to the menu). */
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
            vm->preview = -1;
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
