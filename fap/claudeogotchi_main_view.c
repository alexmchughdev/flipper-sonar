#include "claudeogotchi_i.h"
#include <gui/elements.h>
#include <math.h>

#define ANIM_PERIOD_MS 120

typedef struct {
    Claudeogotchi* app;
    FuriTimer* timer;
    uint8_t frame; /* animation frame counter */
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

/* ---- Clawd, the Claude Code creature. Flat-topped rectangular body, two tall
 * eye-notches spaced wide, a side tab each side, two thin legs under the eyes.
 * 1-bit (body filled, eyes punched white). Pose reacts to state. cx,cy = centre.
 * Returns the body's right edge x so the caller can place a glyph beside it. ---- */
static int draw_creature(Canvas* c, int cx, int cy, uint8_t state, uint8_t frame) {
    const int bs = 5;
    /* 4-row body: head (2 rows) -> square tab row -> gap below (1 row). */
    static const char* const G[4] = {
        ".#######.",
        ".#######.",
        ".#######.",
        ".#######.",
    };
    int bob = ((frame / 6) % 2) ? 1 : 0;
    int sx = (state == CLAUDEOGOTCHI_STATE_WAITING_APPROVAL) ? ((frame % 2) ? 1 : -1) : 0; /* shake */
    int ox = cx - (9 * bs) / 2 + sx;
    int oy = cy - (4 * bs) / 2 + bob;

    for(int r = 0; r < 4; r++)
        for(int col = 0; col < 9; col++)
            if(G[r][col] == '#') canvas_draw_box(c, ox + col * bs, oy + r * bs, bs, bs);

    /* square side tabs (bs x bs) at row2: head above = 2x tab height, gap below
     * = tab height. */
    canvas_draw_box(c, ox + 0 * bs, oy + 2 * bs, bs, bs);
    canvas_draw_box(c, ox + 8 * bs, oy + 2 * bs, bs, bs);

    /* four legs in two pairs, symmetric (left pair mirrored to the right). */
    int ly = oy + 4 * bs, lh = bs + 1, lw = bs - 1;
    int total = 9 * bs;
    int l1 = ox + 1 * bs, l2 = ox + 2 * bs + 2; /* l1 flush with body edge */
    int r1 = ox + total - (l1 - ox) - lw, r2 = ox + total - (l2 - ox) - lw;
    canvas_draw_box(c, l1, ly, lw, lh);
    canvas_draw_box(c, l2, ly, lw, lh);
    canvas_draw_box(c, r2, ly, lw, lh);
    canvas_draw_box(c, r1, ly, lw, lh);

    /* eyes, punched white, sitting just above the tab row (row1). */
    canvas_set_color(c, ColorWhite);
    int elx = ox + 2 * bs + 1, erx = ox + 6 * bs + 1;
    int ey = oy + bs, ew = 2, eh = bs;
    switch(state) {
    case CLAUDEOGOTCHI_STATE_IDLE:
        if(((frame / 12) % 6) == 0) { /* blink */
            canvas_draw_box(c, elx, ey + eh / 2, ew + 1, 1);
            canvas_draw_box(c, erx, ey + eh / 2, ew + 1, 1);
        } else {
            canvas_draw_box(c, elx, ey, ew, eh);
            canvas_draw_box(c, erx, ey, ew, eh);
        }
        break;
    case CLAUDEOGOTCHI_STATE_WAITING_APPROVAL: /* wide eyes */
        canvas_draw_box(c, elx - 1, ey - 1, ew + 2, eh + 2);
        canvas_draw_box(c, erx - 1, ey - 1, ew + 2, eh + 2);
        break;
    case CLAUDEOGOTCHI_STATE_WAITING_INPUT: /* glance */
        canvas_draw_box(c, elx + 1, ey, ew, eh);
        canvas_draw_box(c, erx + 1, ey, ew, eh);
        break;
    case CLAUDEOGOTCHI_STATE_DONE: { /* happy ">  <" chevron eyes */
        int cw = 3, midy = ey + eh / 2;
        canvas_draw_line(c, ox + 2 * bs, ey, ox + 2 * bs + cw, midy);
        canvas_draw_line(c, ox + 2 * bs + cw, midy, ox + 2 * bs, ey + eh);
        canvas_draw_line(c, ox + 6 * bs + cw, ey, ox + 6 * bs, midy);
        canvas_draw_line(c, ox + 6 * bs, midy, ox + 6 * bs + cw, ey + eh);
        break;
    }
    default: /* working */
        canvas_draw_box(c, elx, ey, ew, eh);
        canvas_draw_box(c, erx, ey, ew, eh);
        break;
    }
    canvas_set_color(c, ColorBlack);
    return ox + 8 * bs; /* right edge of the body (col7) */
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

static void draw_link_glyph(Canvas* c, ClaudeogotchiLink link, uint8_t frame) {
    const int gx = 122, gy = 6;
    switch(link) {
    case ClaudeogotchiLinkOnline:
        canvas_draw_disc(c, gx, gy - 2, 2);
        break;
    case ClaudeogotchiLinkConnecting:
        for(int i = 0; i < (frame % 3) + 1; i++) canvas_draw_dot(c, gx - 4 + i * 3, gy - 2);
        break;
    case ClaudeogotchiLinkStale:
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
    Claudeogotchi* app = vm->app;

    ClaudeogotchiModel m;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    m = app->model;
    furi_mutex_release(app->mutex);

    uint8_t disp_state = m.state;

    canvas_set_font(canvas, FontSecondary);

    /* Header: model + link glyph */
    canvas_draw_str(canvas, 0, 7, m.model[0] ? m.model : "(no model)");
    draw_link_glyph(canvas, m.link, vm->frame);
    canvas_draw_line(canvas, 0, 9, 127, 9);

    /* The done celebration face (> <) shows only briefly, then the face reverts
     * to normal even though the state stays 'done'. */
    uint8_t face_state = disp_state;
    if(disp_state == CLAUDEOGOTCHI_STATE_DONE) {
        bool celebrate = false;
        if(m.state_tick) {
            uint32_t now = furi_get_tick();
            if(now - m.state_tick < furi_ms_to_ticks(3000)) celebrate = true;
        }
        if(!celebrate) face_state = CLAUDEOGOTCHI_STATE_IDLE;
    }

    /* Left: the Claude creature */
    int cright = draw_creature(canvas, 24, 28, face_state, vm->frame);

    /* Attention glyph next to the sprite: ! when input is needed, ? for approval. */
    if(disp_state == CLAUDEOGOTCHI_STATE_WAITING_INPUT || disp_state == CLAUDEOGOTCHI_STATE_WAITING_APPROVAL) {
        if(vm->frame % 2) { /* blink */
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(
                canvas, cright + 2, 24,
                disp_state == CLAUDEOGOTCHI_STATE_WAITING_INPUT ? "!" : "?");
            canvas_set_font(canvas, FontSecondary);
        }
    }

    /* Right: compact usage bars */
    draw_minibar(canvas, 13, "SESS", m.ctx_valid, m.ctx_pct);
    draw_minibar(canvas, 25, "5H", m.five_valid, m.five_pct);
    draw_minibar(canvas, 37, "WEEK", m.seven_valid, m.seven_pct);

    /* Bottom bar: only when there is something to say. Working shows the
     * spinner + verb + timer; input/approval show a status line; idle shows
     * nothing (no bar at all). */
    if(disp_state == CLAUDEOGOTCHI_STATE_WORKING) {
        canvas_draw_line(canvas, 0, 50, 127, 50);
        draw_spark(canvas, 6, 57, (float)vm->frame * 0.45f, 5, 3);

        uint32_t secs = 0;
        if(m.work_start_tick) {
            uint32_t now = furi_get_tick();
            uint32_t freq = furi_kernel_get_tick_frequency();
            if(!freq) freq = 1000;
            if(now >= m.work_start_tick) secs = (now - m.work_start_tick) / freq;
        }
        /* Random verb per work-session, re-rolled ~every 30s. */
        uint32_t key = m.work_start_tick + secs / 30;
        if(key != vm->phrase_key) {
            vm->phrase_key = key;
            vm->phrase_idx = furi_hal_random_get() % PHRASE_COUNT;
        }
        char ph[16];
        strlcpy(ph, PHRASES[vm->phrase_idx], sizeof(ph));
        if(ph[0] >= 'a' && ph[0] <= 'z') ph[0] = (char)(ph[0] - 32);

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
    } else if(disp_state == CLAUDEOGOTCHI_STATE_WAITING_INPUT) {
        canvas_draw_line(canvas, 0, 50, 127, 50);
        canvas_draw_str(canvas, 4, 61, "input needed");
    } else if(disp_state == CLAUDEOGOTCHI_STATE_WAITING_APPROVAL) {
        canvas_draw_line(canvas, 0, 50, 127, 50);
        canvas_draw_str(canvas, 4, 61, "approval needed");
    } else if(disp_state == CLAUDEOGOTCHI_STATE_DONE) {
        canvas_draw_line(canvas, 0, 50, 127, 50);
        canvas_draw_str(canvas, 4, 61, "done");
        if(m.tokens_valid) {
            char tok[24];
            fmt_tokens(tok, sizeof(tok), m.tokens);
            canvas_draw_str(canvas, 70, 61, tok);
        }
    }
    /* idle: no bottom bar */
}

static bool main_input(InputEvent* event, void* ctx) {
    Claudeogotchi* app = ctx;
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
        claudeogotchi_config_save(&app->config);
        return true;
    }
    return false;
}

static void main_timer_cb(void* ctx) {
    View* view = ctx;
    with_view_model(view, MainViewModel * vm, { vm->frame++; }, true);
}

/* Force an immediate redraw — called by the UART worker when a frame arrives, so
 * data lands on screen at once instead of waiting for the next animation tick. */
void claudeogotchi_main_view_refresh(View* view) {
    if(!view) return;
    with_view_model(view, MainViewModel * vm, { (void)vm; }, true);
}

static void main_enter(void* ctx) {
    Claudeogotchi* app = ctx;
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
    Claudeogotchi* app = ctx;
    View* view = app->main_view;
    with_view_model(
        view, MainViewModel * vm, { if(vm->timer) furi_timer_stop(vm->timer); }, false);
}

View* claudeogotchi_main_view_alloc(Claudeogotchi* app) {
    View* view = view_alloc();
    view_allocate_model(view, ViewModelTypeLocking, sizeof(MainViewModel));
    with_view_model(
        view,
        MainViewModel * vm,
        {
            vm->app = app;
            vm->timer = NULL;
            vm->frame = 0;
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

void claudeogotchi_main_view_free(View* view) {
    with_view_model(
        view, MainViewModel * vm, { if(vm->timer) furi_timer_free(vm->timer); }, false);
    view_free(view);
}
