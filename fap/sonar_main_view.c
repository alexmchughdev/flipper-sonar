#include "sonar_i.h"
#include <gui/elements.h>
#include <math.h>

#define ANIM_PERIOD_MS 120

typedef struct {
    Sonar* app;
    FuriTimer* timer;
    uint8_t frame; /* animation frame counter */
    int preview; /* -1 = live state; 0..4 = forced state for sprite preview (OK key) */
    uint32_t phrase_key; /* identity of the current phrase slot (session + 3s bucket) */
    uint16_t phrase_idx; /* currently shown spinner verb */
} MainViewModel;

/* Claude Code's spinner verbs (the full default set). A random one is shown
 * while working and re-rolled every few seconds. Stored lowercase (ASCII so the
 * default font renders them); the first letter is capitalised at draw time. */
static const char* const PHRASES[] = {
    "beaming",     "booping",     "bouncing",    "brewing",      "bubbling",
    "chasing",     "churning",    "coalescing",  "conjuring",    "cooking",
    "crafting",    "crunching",   "cuddling",    "dancing",      "dazzling",
    "discovering", "doodling",    "dreaming",    "drifting",     "enchanting",
    "exploring",   "finding",     "floating",    "fluttering",   "foraging",
    "forging",     "frolicking",  "gathering",   "giggling",     "gliding",
    "greeting",    "growing",     "hatching",    "herding",      "honking",
    "hopping",     "hugging",     "humming",     "imagining",    "inventing",
    "jingling",    "juggling",    "jumping",     "kindling",     "knitting",
    "launching",   "leaping",     "mapping",     "marinating",   "meandering",
    "mixing",      "moseying",    "munching",    "napping",      "nibbling",
    "noodling",    "orbiting",    "painting",    "percolating",  "petting",
    "plotting",    "pondering",   "popping",     "prancing",     "purring",
    "puzzling",    "questing",    "riding",      "roaming",      "rolling",
    "sauteing",    "scribbling",  "seeking",     "shimmying",    "singing",
    "skipping",    "sleeping",    "snacking",    "sniffing",     "snuggling",
    "soaring",     "sparking",    "spinning",    "splashing",    "sprouting",
    "squishing",   "stargazing",  "stirring",    "strolling",    "swimming",
    "swinging",    "tickling",    "tinkering",   "toasting",     "tumbling",
    "twirling",    "waddling",    "wandering",   "watching",     "weaving",
    "whistling",   "wibbling",    "wiggling",    "wishing",      "wobbling",
    "wondering",   "yawning",     "zooming",
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

/* ---- the Claude Code creature: chunky pixel body, ear bumps, eye notches,
 * side tabs and four stubby legs. Drawn 1-bit (body filled, eyes punched). Pose
 * reacts to state. cx,cy = centre. ---- */
static void draw_creature(Canvas* c, int cx, int cy, uint8_t state, uint8_t frame) {
    const int bs = 4; /* block size */
    /* Clawd: solid chunky body (no ears). Slightly inset top corners, a wide mid
     * with side tabs, and four legs. The two black rects are EYES, punched out. */
    static const char* const G[6] = {
        ".#######.",
        "#########",
        "#########",
        "#########",
        "#########",
        ".#.#.#.#.",
    };
    int bob = ((frame / 6) % 2) ? 1 : 0;
    int sx = (state == SONAR_STATE_WAITING_APPROVAL) ? ((frame % 2) ? 1 : -1) : 0; /* shake */
    int ox = cx - (9 * bs) / 2 + sx;
    int oy = cy - (6 * bs) / 2 + bob;

    /* body */
    for(int r = 0; r < 6; r++)
        for(int col = 0; col < 9; col++)
            if(G[r][col] == '#') canvas_draw_box(c, ox + col * bs, oy + r * bs, bs, bs);
    /* side tabs (little arms), mid height */
    canvas_draw_box(c, ox - bs, oy + 2 * bs, bs, bs * 2);
    canvas_draw_box(c, ox + 9 * bs, oy + 2 * bs, bs, bs * 2);

    /* eyes: punched out of the body (white) — tall vertical notches like the ref */
    canvas_set_color(c, ColorWhite);
    int ey = oy + bs + 1;
    int eh = bs + 3;
    int elx = ox + 3 * bs + 1;
    int erx = ox + 5 * bs + 1;
    switch(state) {
    case SONAR_STATE_IDLE:
        if(((frame / 12) % 6) == 0) { /* blink */
            canvas_draw_box(c, elx, ey + eh / 2, bs - 1, 1);
            canvas_draw_box(c, erx, ey + eh / 2, bs - 1, 1);
        } else {
            canvas_draw_box(c, elx, ey, bs - 2, eh);
            canvas_draw_box(c, erx, ey, bs - 2, eh);
        }
        break;
    case SONAR_STATE_WAITING_APPROVAL: /* wide */
        canvas_draw_box(c, elx - 1, ey - 1, bs, eh + 1);
        canvas_draw_box(c, erx - 1, ey - 1, bs, eh + 1);
        break;
    case SONAR_STATE_WAITING_INPUT: /* glance right */
        canvas_draw_box(c, elx + 1, ey, bs - 2, eh);
        canvas_draw_box(c, erx + 1, ey, bs - 2, eh);
        break;
    case SONAR_STATE_DONE: /* small happy eyes + smile notch */
        canvas_draw_box(c, elx, ey, bs - 2, bs - 1);
        canvas_draw_box(c, erx, ey, bs - 2, bs - 1);
        canvas_draw_box(c, ox + 4 * bs, oy + 4 * bs, bs, 2);
        break;
    default: /* working */
        canvas_draw_box(c, elx, ey, bs - 2, eh);
        canvas_draw_box(c, erx, ey, bs - 2, eh);
        break;
    }
    canvas_set_color(c, ColorBlack);

    /* state flourishes */
    if(state == SONAR_STATE_WAITING_APPROVAL && (frame % 2))
        canvas_draw_str(c, ox + 9 * bs + 1, oy + 4, "!");
    else if(state == SONAR_STATE_WAITING_INPUT)
        canvas_draw_str(c, ox + 9 * bs + 1, oy + 8, "?");
    else if(state == SONAR_STATE_IDLE && ((frame / 16) % 4) == 0)
        canvas_draw_str(c, ox + 9 * bs, oy - 1, "z");
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
    const int lx = 52, bx = 80, bw = 27, bh = 6;
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

    /* Left: the Claude creature */
    draw_creature(canvas, 24, 28, disp_state, vm->frame);

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
        /* Pick a random verb per work-session, re-rolled every 3s. The key
         * changes when the session restarts (work_start_tick) or the 3s bucket
         * advances, so it never sticks on one word and varies each time. */
        uint32_t key = m.work_start_tick + secs / 30 + (vm->preview >= 0 ? vm->frame / 24 : 0);
        if(key != vm->phrase_key) {
            vm->phrase_key = key;
            vm->phrase_idx = furi_hal_random_get() % PHRASE_COUNT;
        }
        char ph[16];
        strlcpy(ph, PHRASES[vm->phrase_idx], sizeof(ph));
        if(ph[0] >= 'a' && ph[0] <= 'z') ph[0] = (char)(ph[0] - 32); /* capitalise */

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
            vm->phrase_key = 0xFFFFFFFF;
            vm->phrase_idx = 0;
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
