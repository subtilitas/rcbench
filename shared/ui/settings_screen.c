/*
 * The settings screen: one renderer over the whole schema.
 *
 * There is no code here that knows what any particular setting is.  It walks
 * the categories and rows shared/settings declares and draws each by its type
 * -- a stepper for a number, a toggle for a bool, a choice for an enum -- so a
 * new setting appears by adding a row to the schema and nothing here changes.
 * That is the same data-driven move the programmer screen makes, for the same
 * reason: a screen with one control per type cannot drift the way a screen
 * with one control per setting does.
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "ui_widgets.h"

#define SCREEN_W 800
/* The router owns the top 48 and the home tag; this screen gets the body. */
#define SCREEN_H (480 - UI_BAND_H)
#define MAX_FBS  3

#define CAT_X    16
#define CAT_W    208
/*
 * Everything sits 40 px higher than it did: the layout was drawn for a screen
 * that owned all 480 and painted its own home tag in the top 40.  Without the
 * shift the list ran from 56 to 458 in a canvas 432 tall.
 */
#define CAT_Y    16
#define CAT_H    64
#define CAT_GAP  8

#define LIST_X   236
#define LIST_W   548
#define LIST_Y   16
#define LIST_H   402
#define ROW_H    54
#define ROW_GAP  4
#define ROW_PITCH (ROW_H + ROW_GAP)

#define BTN_W    44
#define BTN_H    36
#define PLUS_X   (LIST_X + LIST_W - 14 - BTN_W)
#define MINUS_X  (PLUS_X - 8 - 110 - 8 - BTN_W)
#define VALUE_R  (PLUS_X - 8)

#define RESET_Y  358
#define MAX_ROWS 32

/* Held +/- repeats, or a 0..30000 mAh range would be three hundred taps. */
#define REPEAT_DELAY_S 0.45f
#define REPEAT_SLOW_HZ 8.0f
#define REPEAT_FAST_S  1.8f
#define REPEAT_FAST_HZ 30.0f

/* A press that moves further than this is a scroll, not a tap. */
#define DRAG_SLOP 8

enum { HIT_NONE = 0, HIT_CAT, HIT_MINUS, HIT_PLUS, HIT_RESET, HIT_LIST };

static struct {
    setting_cat_t cat;
    int      scroll;
    int      scroll_max;

    int      hit_kind;
    int      hit_index;      /* category index, or row index */
    bool     dragging;
    int      press_x, press_y;
    int      last_y;

    float    held_for;
    bool     repeating;

    bool     chrome_valid[MAX_FBS];
    setting_cat_t drawn_cat;
} s;

void settings_screen_invalidate(void)
{
    memset(s.chrome_valid, 0, sizeof(s.chrome_valid));
}

void settings_apply_ui(void)
{
    ui_theme_set((ui_theme_id_t)settings_get_int(SET_THEME));
    ui_theme_set_brightness(settings_get_int(SET_BRIGHTNESS));
    ui_theme_set_contrast(settings_get_int(SET_CONTRAST));
    ui_router_invalidate();
}

/* ---------------------------------------------------------------- geometry */

static int row_count(void)
{
    return settings_in_category(s.cat, NULL, 0);
}

static void clamp_scroll(void)
{
    /* n rows are n*ROW_H plus the n-1 gaps *between* them.  Counting a
     * trailing gap gave the 7-row categories a 4 px scroll range, which drew
     * a scroll indicator beside a list that already fits and let a drag push
     * the last row off the bottom. */
    int n = row_count();
    int content = (n > 0) ? (n * ROW_H + (n - 1) * ROW_GAP) : 0;
    s.scroll_max = (content > LIST_H) ? (content - LIST_H) : 0;
    if (s.scroll < 0) { s.scroll = 0; }
    if (s.scroll > s.scroll_max) { s.scroll = s.scroll_max; }
}

static gfx_rect_t cat_rect(int i)
{
    return gfx_rect_make(CAT_X, CAT_Y + i * (CAT_H + CAT_GAP), CAT_W, CAT_H);
}

static gfx_rect_t reset_rect(void)
{
    return gfx_rect_make(CAT_X, RESET_Y, CAT_W, 40);
}

/** Screen-space rect of a row, or an empty rect when it is scrolled away. */
static gfx_rect_t row_rect(int index)
{
    int y = LIST_Y + index * ROW_PITCH - s.scroll;
    return gfx_rect_make(LIST_X, y, LIST_W, ROW_H);
}

static int row_at(int x, int y)
{
    if (x < LIST_X || x >= LIST_X + LIST_W || y < LIST_Y || y >= LIST_Y + LIST_H) {
        return -1;
    }
    int idx = (y - LIST_Y + s.scroll) / ROW_PITCH;
    return (idx >= 0 && idx < row_count()) ? idx : -1;
}

static setting_id_t row_setting(int index)
{
    setting_id_t ids[MAX_ROWS];
    int n = settings_in_category(s.cat, ids, MAX_ROWS);
    return (index >= 0 && index < n) ? ids[index] : (setting_id_t)0;
}

/* ------------------------------------------------------------------- input */

static void step_row(int index, int steps)
{
    setting_id_t id = row_setting(index);
    const setting_def_t *d = settings_def(id);
    if (!d) {
        return;
    }
    settings_adjust(id, steps);
    if (d->cat == SET_CAT_APP) {
        settings_apply_ui();      /* theme, brightness and contrast are live */
    }
    settings_screen_invalidate();
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.cat = SET_CAT_ESC;
    clamp_scroll();
}

static void enter(void)
{
    s.hit_kind = HIT_NONE;
    s.dragging = false;
    s.repeating = false;
    s.held_for = 0.0f;
    clamp_scroll();
    settings_screen_invalidate();
}

static void leave(void)
{
    if (settings_dirty()) {
        settings_save();
    }
}

static void tick(float dt_s)
{
    if (s.hit_kind != HIT_MINUS && s.hit_kind != HIT_PLUS) {
        s.held_for = 0.0f;
        s.repeating = false;
        return;
    }
    float before = s.held_for;
    s.held_for += dt_s;
    if (s.held_for < REPEAT_DELAY_S) {
        return;
    }
    float hz = (s.held_for > REPEAT_DELAY_S + REPEAT_FAST_S) ? REPEAT_FAST_HZ
                                                             : REPEAT_SLOW_HZ;
    /* Fire on each crossing of the repeat period, so the rate is honest even
     * if a frame runs long. */
    int fired_before = (int)((before - REPEAT_DELAY_S) * hz);
    int fired_now = (int)((s.held_for - REPEAT_DELAY_S) * hz);
    for (int i = fired_before; i < fired_now; ++i) {
        step_row(s.hit_index, (s.hit_kind == HIT_PLUS) ? 1 : -1);
        s.repeating = true;
    }
}

static void event(const touch_event_t *evt)
{
    int x = evt->point.x;
    int y = evt->point.y;

    switch (evt->type) {
    case TOUCH_EVENT_DOWN: {
        s.press_x = x;
        s.press_y = y;
        s.last_y = y;
        s.dragging = false;
        s.repeating = false;
        s.held_for = 0.0f;

        for (int i = 0; i < SET_CAT_COUNT; ++i) {
            if (gfx_rect_contains(cat_rect(i), x, y)) {
                s.hit_kind = HIT_CAT;
                s.hit_index = i;
                settings_screen_invalidate();
                return;
            }
        }
        if (gfx_rect_contains(reset_rect(), x, y)) {
            s.hit_kind = HIT_RESET;
            settings_screen_invalidate();
            return;
        }

        int row = row_at(x, y);
        if (row >= 0) {
            gfx_rect_t r = row_rect(row);
            if (x >= MINUS_X && x < MINUS_X + BTN_W) {
                s.hit_kind = HIT_MINUS;
                s.hit_index = row;
                step_row(row, -1);
                return;
            }
            if (x >= PLUS_X && x < PLUS_X + BTN_W) {
                s.hit_kind = HIT_PLUS;
                s.hit_index = row;
                step_row(row, 1);
                return;
            }
            (void)r;
            s.hit_kind = HIT_LIST;
            s.hit_index = row;
            return;
        }
        s.hit_kind = HIT_NONE;
        break;
    }

    case TOUCH_EVENT_MOVE: {
        int dy = y - s.last_y;
        s.last_y = y;
        if (s.hit_kind == HIT_NONE) {
            break;
        }
        int moved = y - s.press_y;
        if (moved < 0) { moved = -moved; }
        if (!s.dragging && moved > DRAG_SLOP &&
            (s.hit_kind == HIT_LIST || s.hit_kind == HIT_MINUS ||
             s.hit_kind == HIT_PLUS)) {
            /* Turned into a scroll: give up the button. */
            s.dragging = true;
            s.hit_kind = HIT_LIST;
        }
        if (s.dragging) {
            s.scroll -= dy;
            clamp_scroll();
            settings_screen_invalidate();
        }
        break;
    }

    case TOUCH_EVENT_UP: {
        int kind = s.hit_kind;
        int index = s.hit_index;
        bool dragged = s.dragging;
        s.hit_kind = HIT_NONE;
        s.dragging = false;
        s.held_for = 0.0f;
        s.repeating = false;
        settings_screen_invalidate();

        if (dragged) {
            break;
        }
        if (kind == HIT_CAT && gfx_rect_contains(cat_rect(index), x, y)) {
            if (s.cat != (setting_cat_t)index) {
                s.cat = (setting_cat_t)index;
                s.scroll = 0;
                clamp_scroll();
            }
        } else if (kind == HIT_RESET && gfx_rect_contains(reset_rect(), x, y)) {
            settings_reset(s.cat);
            settings_apply_ui();
        }
        break;
    }
    }
}

/* ------------------------------------------------------------------ render */

static void draw_categories(gfx_canvas_t *c)
{
    for (int i = 0; i < SET_CAT_COUNT; ++i) {
        gfx_rect_t r = cat_rect(i);
        bool active = (s.cat == (setting_cat_t)i);
        bool pressed = (s.hit_kind == HIT_CAT && s.hit_index == i);

        gfx_color_t fill = active ? UI_ACCENT
                                  : (pressed ? UI_PANEL_HI : UI_PANEL);
        gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0, fill);
        if (!active) {
            gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0, UI_EDGE);
        }

        gfx_text(c, r.x + 14, r.y + 10, settings_category_name((setting_cat_t)i),
                 UI_FONT_HEAD, active ? UI_TEXT_ON_LIGHT : UI_TEXT, 1);

        char buf[24];
        snprintf(buf, sizeof(buf), "%d SETTINGS",
                 settings_in_category((setting_cat_t)i, NULL, 0));
        gfx_text(c, r.x + 14, r.y + 40, buf, UI_FONT_LABEL,
                 active ? UI_TEXT_ON_LIGHT : UI_TEXT_FAINT, 1);
    }

    gfx_rect_t rr = reset_rect();
    ui_button(c, rr, "RESET CATEGORY", UI_WARN, s.hit_kind == HIT_RESET, true);

    const char *state = settings_dirty() ? "UNSAVED" : "SAVED";
    gfx_fill_rect(c, CAT_X, RESET_Y + 48, CAT_W, 18, UI_BG);
    gfx_text(c, CAT_X, RESET_Y + 48, state, UI_FONT_LABEL,
             settings_dirty() ? UI_WARN : UI_TEXT_FAINT, 1);
    gfx_text(c, CAT_X + 88, RESET_Y + 48, "ON LEAVING", UI_FONT_LABEL,
             UI_TEXT_FAINT, 1);
}

static void draw_row(gfx_canvas_t *c, int index)
{
    gfx_rect_t r = row_rect(index);
    if (r.y + r.h <= LIST_Y || r.y >= LIST_Y + LIST_H) {
        return;
    }
    setting_id_t id = row_setting(index);
    const setting_def_t *d = settings_def(id);
    if (!d) {
        return;
    }

    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0, UI_EDGE);

    /* Label and help are clipped short of the keys rather than allowed to
     * run under them. */
    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(r.x, r.y, MINUS_X - 10 - r.x, r.h));
    gfx_text(c, r.x + 16, r.y + 3, d->label, UI_FONT_HEAD, UI_TEXT, 1);
    if (d->help && d->help[0]) {
        gfx_text(c, r.x + 16, r.y + 33, d->help, UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    }
    c->clip = saved;

    /* Value, then the unit after it, both right-aligned against the + key.
     * Numbers get the big face; enum words get the small one, because
     * "BLHELI SERIAL" at 16 px a character does not fit in the gap and a
     * clipped setting name is worse than a small one. */
    char buf[32];
    settings_value_text(id, buf, sizeof(buf));
    int ux = VALUE_R;
    if (d->unit && d->unit[0]) {
        int uw = gfx_text_width(UI_FONT_LABEL, d->unit, 1);
        gfx_text(c, VALUE_R - uw, r.y + 26, d->unit, UI_FONT_LABEL, UI_TEXT_DIM, 1);
        ux = VALUE_R - uw - 6;
    }
    bool wordy = (d->type == SET_TYPE_ENUM || d->type == SET_TYPE_BOOL);
    const gfx_font_t *vf = wordy ? UI_FONT_LABEL : UI_FONT_HEAD;
    int vw = gfx_text_width(vf, buf, 1);
    int avail = ux - (MINUS_X + BTN_W + 8);
    if (vw > avail && vf == UI_FONT_HEAD) {
        vf = UI_FONT_LABEL;
        vw = gfx_text_width(vf, buf, 1);
    }
    int vy = r.y + (ROW_H - gfx_text_height(vf, 1)) / 2;
    gfx_text(c, ux - vw, vy, buf, vf, UI_ACCENT, 1);

    bool minus_down = (s.hit_kind == HIT_MINUS && s.hit_index == index);
    bool plus_down  = (s.hit_kind == HIT_PLUS  && s.hit_index == index);
    int by = r.y + (ROW_H - BTN_H) / 2;
    ui_button(c, gfx_rect_make(MINUS_X, by, BTN_W, BTN_H),
              (d->type == SET_TYPE_BOOL || d->type == SET_TYPE_ENUM) ? "<" : "-",
              UI_EDGE_HI, minus_down, true);
    ui_button(c, gfx_rect_make(PLUS_X, by, BTN_W, BTN_H),
              (d->type == SET_TYPE_BOOL || d->type == SET_TYPE_ENUM) ? ">" : "+",
              UI_EDGE_HI, plus_down, true);
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    bool stale = true;
    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        stale = !s.chrome_valid[buffer_index] || s.drawn_cat != s.cat;
    }
    if (!stale) {
        return;
    }

    gfx_clear(c, UI_BG);
    draw_categories(c);

    /* The list is clipped rather than masked, so a part-scrolled row is cut
     * cleanly at the pane edge instead of drawing over the categories. */
    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_set(c, gfx_rect_make(LIST_X, LIST_Y, LIST_W, LIST_H));
    int n = row_count();
    for (int i = 0; i < n; ++i) {
        draw_row(c, i);
    }
    c->clip = saved;

    /* Scroll position, drawn only when there is somewhere to scroll to. */
    if (s.scroll_max > 0) {
        int track_h = LIST_H;
        int knob_h = track_h * LIST_H / (n * ROW_PITCH);
        if (knob_h < 24) { knob_h = 24; }
        int knob_y = LIST_Y + (track_h - knob_h) * s.scroll / s.scroll_max;
        gfx_fill_rect(c, LIST_X + LIST_W + 4, LIST_Y, 3, track_h, UI_PANEL_SUNK);
        gfx_fill_rect(c, LIST_X + LIST_W + 4, knob_y, 3, knob_h, UI_ACCENT);
    }

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.chrome_valid[buffer_index] = true;
        s.drawn_cat = s.cat;
    }
}

static const ui_screen_t s_screen = {
    .title = "SETTINGS",
    .reset = reset,
    .enter = enter,
    .leave = leave,
    .tick = tick,
    .event = event,
    .render = render,
};

const ui_screen_t *settings_screen(void)
{
    return &s_screen;
}
