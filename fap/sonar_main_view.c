#include "sonar_i.h"
#include <gui/elements.h>
#include <math.h>

#define ANIM_PERIOD_MS 120

typedef struct {
    Sonar* app;
    FuriTimer* timer;
    uint8_t frame; /* animation frame counter */
    int preview; /* -1 = live state; 0..4 = forced state for sprite preview (OK key) */
} MainViewModel;

/* Claude's working words. Rotates while working, like the CLI. ASCII only so the
 * default font renders them. */
static const char* const PHRASES[] = {
    "Spelunking",   "Sauteing",   "Channelling", "Herding",      "Vibing",
    "Pondering",    "Noodling",   "Conjuring",   "Percolating",  "Marinating",
    "Ruminating",   "Tinkering",  "Brewing",     "Finagling",    "Cogitating",
    "Wrangling",    "Simmering",  "Mulling",     "Computing",    "Incubating",
    "Synthesizing", "Galloping",  "Whirring",    "Schlepping",
};
#define PHRASE_COUNT (sizeof(PHRASES) / sizeof(PHRASES[0]))

/* ---- the Claude "spark": a small radial sunburst, used as the working spinner ---- */
static void draw_spark(Canvas* c, int cx, int cy, float angle, int rlong, int rshort) {
    const int rays = 12;
    for(int i = 0; i < rays; i++) {
        float a = angle + (float)i * (2.0f * (float)M_PI / (float)rays);
        int len = (i % 2 == 0) ? rlong : rshort;
        canvas_draw_line(c, cx, cy, cx + (int)lroundf(cosf(a) * len), cy + (int)lroundf(sinf(a) * len));
    }
    canvas_draw_disc(c, cx, cy, 1);
}

/* ---- the pixel alien (Tamagotchi-style), pose driven by state ---- */
static void draw_alien(Canvas* c, int cx, int cy, uint8_t state, uint8_t frame) {
    int bob = ((frame / 6) % 2) ? 1 : 0;
    cy += bob;
    if(state == SONAR_STATE_WAITING_APPROVAL) cx += (frame % 2) ? 1 : -1; /* shake */

    /* antennae (wiggle) */
    int wig = (frame % 2) ? 1 : 0;
    canvas_draw_line(c, cx - 6, cy - 10, cx - 9 - wig, cy - 16);
    canvas_draw_disc(c, cx - 9 - wig, cy - 17, 1);
    canvas_draw_line(c, cx + 6, cy - 10, cx + 9 + wig, cy - 16);
    canvas_draw_disc(c, cx + 9 + wig, cy - 17, 1);

    /* head + legs */
    canvas_draw_rframe(c, cx - 12, cy - 10, 24, 20, 6);
    canvas_draw_line(c, cx - 5, cy + 10, cx - 5, cy + 13);
    canvas_draw_line(c, cx + 5, cy + 10, cx + 5, cy + 13);
    canvas_draw_line(c, cx - 7, cy + 13, cx - 3, cy + 13);
    canvas_draw_line(c, cx + 3, cy + 13, cx + 7, cy + 13);

    const int ex = 6, ey = cy - 2;
    switch(state) {
    case SONAR_STATE_DONE: /* happy ^ ^ + smile */
        canvas_draw_line(c, cx - ex - 2, ey, cx - ex, ey - 2);
        canvas_draw_line(c, cx - ex, ey - 2, cx - ex + 2, ey);
        canvas_draw_line(c, cx + ex - 2, ey, cx + ex, ey - 2);
        canvas_draw_line(c, cx + ex, ey - 2, cx + ex + 2, ey);
        canvas_draw_line(c, cx - 4, cy + 5, cx, cy + 7);
        canvas_draw_line(c, cx, cy + 7, cx + 4, cy + 5);
        break;
    case SONAR_STATE_WAITING_APPROVAL: /* wide eyes + open mouth + ! */
        canvas_draw_disc(c, cx - ex, ey, 3);
        canvas_draw_disc(c, cx + ex, ey, 3);
        canvas_draw_circle(c, cx, cy + 6, 2);
        if(frame % 2) {
            canvas_draw_line(c, cx + 13, cy - 12, cx + 13, cy - 8);
            canvas_draw_dot(c, cx + 13, cy - 6);
        }
        break;
    case SONAR_STATE_WAITING_INPUT: /* glance to side + ? */
        canvas_draw_disc(c, cx - ex + 1, ey, 2);
        canvas_draw_disc(c, cx + ex + 1, ey, 2);
        canvas_draw_line(c, cx - 3, cy + 6, cx + 3, cy + 6);
        canvas_draw_str(c, cx + 12, cy - 6, "?");
        break;
    case SONAR_STATE_WORKING: /* focused */
        canvas_draw_disc(c, cx - ex, ey, 2);
        canvas_draw_disc(c, cx + ex, ey, 2);
        canvas_draw_line(c, cx - 2, cy + 6, cx + 2, cy + 6);
        break;
    default: { /* idle: blink + occasional z */
        bool blink = ((frame / 12) % 6) == 0;
        if(blink) {
            canvas_draw_line(c, cx - ex - 2, ey, cx - ex + 2, ey);
            canvas_draw_line(c, cx + ex - 2, ey, cx + ex + 2, ey);
        } else {
            canvas_draw_disc(c, cx - ex, ey, 2);
            canvas_draw_disc(c, cx + ex, ey, 2);
        }
        canvas_draw_line(c, cx - 2, cy + 6, cx + 2, cy + 6);
        if(((frame / 16) % 4) == 0) canvas_draw_str(c, cx + 11, cy - 10, "z");
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

/* Compact bar on the right column. */
static void draw_minibar(Canvas* c, int y, const char* label, bool valid, uint8_t pct) {
    const int lx = 52, bx = 74, bw = 32, bh = 6;
    canvas_draw_str(c, lx, y + 6, label);
    canvas_draw_frame(c, bx, y, bw, bh);
    if(valid) {
        int f = (bw - 2) * pct / 100;
        if(f > 0) canvas_draw_box(c, bx + 1, y + 1, f, bh - 2);
    }
    char v[12];
    if(valid)
        snprintf(v, sizeof(v), "%u", pct);
    else
        snprintf(v, sizeof(v), "--");
    canvas_draw_str(c, bx + bw + 3, y + 6, v);
}

static void draw_link_glyph(Canvas* c, SonarLink link, uint8_t frame) {
    const int gx = 122, gy = 6;
    switch(link) {
    case SonarLinkOnline:
        canvas_draw_disc(c, gx, gy - 2, 2);
        break;
    case SonarLinkConnecting:
        for(int i = 0; i < (frame % 3) + 1; i++) canvas_draw_dot(c, gx - 4 + i * 3, gy - 2);
        break;
    case SonarLinkStale:
        canvas_draw_str(c, gx - 4, gy, "x");
        break;
    }
}

static void fmt_tokens(char* out, size_t n, uint32_t t) {
    if(t > 9999999u) t = 9999999u; /* bound output width */
    if(t < 1000)
        snprintf(out, n, "%u", (unsigned)t);
    else
        snprintf(out, n, "%u.%uk", (unsigned)(t / 1000), (unsigned)((t % 1000) / 100));
}

static void main_draw(Canvas* canvas, void* model_v) {
    MainViewModel* vm = model_v;
    Sonar* app = vm->app;

    SonarModel m;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    m = app->model;
    furi_mutex_release(app->mutex);

    uint8_t disp_state = vm->preview >= 0 ? (uint8_t)vm->preview : m.state;

    canvas_set_font(canvas, FontSecondary);

    /* Header: model + link glyph */
    canvas_draw_str(canvas, 0, 7, m.model[0] ? m.model : "(no model)");
    draw_link_glyph(canvas, m.link, vm->frame);
    canvas_draw_line(canvas, 0, 9, 127, 9);

    /* Left: the alien character */
    draw_alien(canvas, 24, 29, disp_state, vm->frame);

    /* Right: compact usage bars */
    draw_minibar(canvas, 13, "SESS", m.ctx_valid, m.ctx_pct);
    draw_minibar(canvas, 25, "5H", m.five_valid, m.five_pct);
    draw_minibar(canvas, 37, "WEEK", m.seven_valid, m.seven_pct);

    /* Bottom strip */
    canvas_draw_line(canvas, 0, 50, 127, 50);
    if(disp_state == SONAR_STATE_WORKING) {
        draw_spark(canvas, 6, 57, (float)vm->frame * 0.45f, 5, 3);

        /* run timer from when work began */
        uint32_t secs = 0;
        if(m.work_start_tick) {
            uint32_t now = furi_get_tick();
            uint32_t freq = furi_kernel_get_tick_frequency();
            if(!freq) freq = 1000;
            if(now >= m.work_start_tick) secs = (now - m.work_start_tick) / freq;
        }
        const char* ph = PHRASES[(secs / 3) % PHRASE_COUNT];

        char line[72];
        if(m.tokens_valid) {
            char tok[24];
            fmt_tokens(tok, sizeof(tok), m.tokens);
            snprintf(
                line, sizeof(line), "%s %lu:%02lu %s", ph, (unsigned long)(secs / 60),
                (unsigned long)(secs % 60), tok);
        } else {
            snprintf(
                line, sizeof(line), "%s %lu:%02lu", ph, (unsigned long)(secs / 60),
                (unsigned long)(secs % 60));
        }
        canvas_draw_str(canvas, 14, 61, line);
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 62, state_label(disp_state));
        canvas_set_font(canvas, FontSecondary);
        if(vm->preview >= 0) canvas_draw_str(canvas, 70, 61, "demo");
        else if(disp_state == SONAR_STATE_DONE && m.tokens_valid) {
            char tok[24];
            fmt_tokens(tok, sizeof(tok), m.tokens);
            canvas_draw_str(canvas, 70, 61, tok);
        }
    }
}

static bool main_input(InputEvent* event, void* ctx) {
    Sonar* app = ctx;
    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyLeft || event->key == InputKeyRight) {
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
        with_view_model(
            app->main_view,
            MainViewModel * vm,
            { vm->preview = (vm->preview >= SONAR_STATE_DONE) ? -1 : vm->preview + 1; },
            true);
        return true;
    }
    return false;
}

static void main_timer_cb(void* ctx) {
    View* view = ctx;
    with_view_model(view, MainViewModel * vm, { vm->frame++; }, true);
}

static void main_enter(void* ctx) {
    Sonar* app = ctx;
    View* view = app->main_view;
    with_view_model(
        view,
        MainViewModel * vm,
        {
            if(!vm->timer) vm->timer = furi_timer_alloc(main_timer_cb, FuriTimerTypePeriodic, view);
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
    return view;
}

void sonar_main_view_free(View* view) {
    with_view_model(
        view, MainViewModel * vm, { if(vm->timer) furi_timer_free(vm->timer); }, false);
    view_free(view);
}
