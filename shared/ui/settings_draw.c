/**
 * @file settings_draw.c
 * @brief SETUP's drawing: the hub's tiles, a category's list and its rows, the
 *        SAVE strip under both, and the warning that holds the way out.
 *        settings_screen.c holds the state, the touch handling and the
 *        screen's interface (settings_screen.h); settings_screen_int.h is what
 *        the two share.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "sensors_rows.h"
#include "settings.h"
#include "settings_rows_int.h"
#include "settings_screen.h"
#include "ui_icons.h"
#include "ui_keypad.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

#include "settings_screen_int.h"

/* ------------------------------------------------------------------ render */

/*
 * Four states, and the label is the whole of the feedback: nothing to write
 * (SAVED), something to write (SAVE), something asked for and waiting for a
 * moment to write it in (WHEN IDLE), and an attempt the store refused (NOT
 * SAVED).  WHEN IDLE is seen only while the bench is armed or a transfer is
 * running; disarmed, the write is taken on the next frame.  SAVE takes the
 * accent rather than the warning colour: RESET CATEGORY sits beside it and
 * two amber buttons in a row read as one control.  NOT SAVED takes the
 * danger colour, because the values on this screen are not the values the
 * next boot will load and nothing else on the panel says so.  The same
 * button on the tiles and on a list: the values are one set, not one per
 * category.
 */
static void draw_save(gfx_canvas_t *c)
{
    const bool dirty  = settings_dirty();
    const bool asked  = settings_save_asked();
    const bool failed = settings_save_failed();
    s.drawn_dirty  = dirty;
    s.drawn_asked  = asked;
    s.drawn_failed = failed;
    const char *label = !dirty ? TR(SET_SAVED)
                        : asked ? TR(SET_WHEN_IDLE)
                        : failed ? TR(SET_NOT_SAVED) : TR(SET_SAVE);
    ui_button_font(c, stg_save_rect(), label,
                   dirty ? (asked ? UI_WARN : failed ? UI_DANGER : UI_ACCENT)
                         : UI_PANEL_HI,
                   s.hit_kind == HIT_SAVE, stg_save_offered(), STG_F_LABEL);
}

/** @brief A hub tile's look: its icon, name and line (settings_screen_int.h). */
static const ui_tile_look_t k_hub_look = {
    .icon_dy = HUB_ICON_DY, .icon = HUB_ICON, .name_dy = HUB_NAME_DY,
    .line_dy = HUB_LINE_DY,
    .line_pitch = 0,   /* no second row: the tile has no room for one */
    .name_f = STG_F_BODY, .line_f = STG_F_LABEL,
};

static void draw_hub(gfx_canvas_t *c)
{
    for (int i = 0; i < TILE_COUNT; ++i) {
        const hub_tile_t *t = &k_tiles[i];
        const bool down = (s.hit_kind == HIT_TILE && s.hit_index == i);
        if (t->cat < 0) {
            ui_tile(c, stg_tile_rect(i), &k_hub_look, t->icon,
                    ui_router_title(t->screen), ui_tr(t->line), down);
            continue;
        }
        char count[40];
        snprintf(count, sizeof(count), TR(SET_COUNT),
                 settings_rows_listed((setting_cat_t)t->cat));
        ui_tile(c, stg_tile_rect(i), &k_hub_look, t->icon,
                ui_setting_category((setting_cat_t)t->cat), count, down);
    }
}

/*
 * A string's row: its name and help on the left, its text on the right in
 * the value's colour, against the row's right edge where a setting's "+"
 * is.  No keys: a tap opens the keyboard, and the row's edge takes the
 * accent while it is pressed.  The text is in the body face, or in the
 * label face where it is wider than the gap left of it.
 */
static void draw_text_row(gfx_canvas_t *c, int index, gfx_rect_t r,
                          setting_text_id_t id)
{
    const bool down = (s.hit_kind == HIT_TEXT && s.hit_index == index);
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             down ? UI_ACCENT : UI_EDGE);

    const char *label = ui_tr(k_text_ui[id].label);
    const char *help = (k_text_ui[id].help != TX_COUNT)
                           ? ui_tr(k_text_ui[id].help) : "";
    const int right = PLUS_X + BTN_W;
    int left = r.x + 16 + gfx_text_width(STG_F_HEAD, label, 1);
    const int help_end = r.x + 16 + gfx_text_width(STG_F_LABEL, help, 1);
    left = ((help_end > left) ? help_end : left) + 16;

    const char *value = settings_text(id);
    const gfx_font_t *vf = STG_F_BODY;
    int vw = gfx_text_width(vf, value, 1);
    if (vw > right - left) {
        vf = STG_F_LABEL;
        vw = gfx_text_width(vf, value, 1);
    }
    const int vx = right - vw;

    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(r.x, r.y, vx - 10 - r.x, r.h));
    gfx_text(c, r.x + 16, r.y + ROW_NAME_DY, label, STG_F_HEAD, UI_TEXT, 1);
    if (help[0] != '\0') {
        gfx_text(c, r.x + 16, r.y + ROW_HELP_DY, help, STG_F_LABEL,
                 UI_TEXT_FAINT, 1);
    }
    c->clip = saved;
    gfx_text(c, vx, r.y + (ROW_H - gfx_text_height(vf, 1)) / 2, value, vf,
             UI_ACCENT, 1);
}

/**
 * @brief List row @p index: a heading, a string, or a setting with its
 *        value and its keys.  A setting's edge takes the accent while its
 *        value is pressed.
 * @param c The canvas.
 * @param index The row.
 */
static void draw_row(gfx_canvas_t *c, int index)
{
    gfx_rect_t r = stg_row_rect(index);
    if (r.y + r.h <= LIST_Y || r.y >= LIST_Y + LIST_H) {
        return;
    }
    const settings_row_t e = stg_row_entry(index);
    if (e.heading >= 0 || e.part >= 0) {
        /* The heading stands on the row's lower edge, over a rule; a
         * part's carries its state on the right (sensors_rows.h). */
        const gfx_font_t *hf = STG_F_HEAD;
        char title[24];
        const char *t = (e.part >= 0)
                            ? sensors_rows_title(e.part, title, sizeof(title))
                            : ui_tr(settings_rows_heading_text(e.heading));
        gfx_text(c, r.x + 16, r.y + ROW_H - gfx_text_height(hf, 1) - 6, t,
                 hf, UI_ACCENT, 1);
        if (e.part >= 0) {
            const char *st = sensors_rows_state(e.part);
            const gfx_font_t *sf = STG_F_LABEL;
            gfx_text(c, PLUS_X + BTN_W - gfx_text_width(sf, st, 1),
                     r.y + ROW_H - gfx_text_height(sf, 1) - 6, st, sf,
                     sensors_rows_state_color(e.part), 1);
        }
        ui_rule(c, r.x, r.y + ROW_H - 2, r.w, UI_EDGE);
        return;
    }
    if (e.text >= 0) {
        draw_text_row(c, index, r, (setting_text_id_t)e.text);
        return;
    }
    setting_id_t id = e.id;
    const setting_def_t *d = settings_def(id);
    if (!d) {
        return;
    }

    /* A value the IOmcu does not hold, or PD mini TX and RX on one pin:
     * the row's edge, its value and one line in place of its help, in the
     * colour of a write that waits. */
    const bool same_pin = (id == SET_PDMINI_TX || id == SET_PDMINI_RX)
                          && settings_pdmini_same_pin();
    const bool unheld = settings_screen_unheld(id) || same_pin;
    /* A press on the value, which opens the keypad on its release, lights
     * the row's edge as a press on a string's row does. */
    const bool value_down = (s.hit_kind == HIT_VALUE && s.hit_index == index);
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             unheld ? UI_WARN
                                    : value_down ? UI_ACCENT : UI_EDGE);

    /* Label and help are clipped short of the keys rather than allowed to
     * run under them. */
    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(r.x, r.y, MINUS_X - 10 - r.x, r.h));
    gfx_text(c, r.x + 16, r.y + ROW_NAME_DY, ui_setting_label(id),
             STG_F_HEAD, UI_TEXT, 1);
    const char *help = settings_screen_help(id);
    if (help[0] != '\0') {
        gfx_text(c, r.x + 16, r.y + ROW_HELP_DY, help, STG_F_LABEL,
                 unheld ? UI_WARN : UI_TEXT_FAINT, 1);
    }
    c->clip = saved;

    /* Value, then the unit after it, both right-aligned against the + key
     * and centred in the row's height.  A value takes the body face, a
     * number and a switch's or a choice's word alike.  A value wider than
     * the gap takes the label face, because a clipped value is worse than
     * a small one; no value of the present schema needs it
     * (test_settings_rows). */
    char buf[32];
    ui_setting_value(id, buf, sizeof(buf));
    int ux = VALUE_R;
    if (d->unit && d->unit[0]
        && !(d->off_below > 0.0f && settings_get(id) == 0.0f)) {
        const int uw = gfx_text_width(STG_F_LABEL, d->unit, 1);
        gfx_text(c, VALUE_R - uw,
                 r.y + (ROW_H - gfx_text_height(STG_F_LABEL, 1)) / 2 + 2,
                 d->unit, STG_F_LABEL, UI_TEXT_DIM, 1);
        ux = VALUE_R - uw - 6;
    }
    const gfx_font_t *vf = STG_F_BODY;
    int vw = gfx_text_width(vf, buf, 1);
    const int avail = ux - (MINUS_X + BTN_W + 8);
    if (vw > avail) {
        vf = STG_F_LABEL;
        vw = gfx_text_width(vf, buf, 1);
    }
    int vy = r.y + (ROW_H - gfx_text_height(vf, 1)) / 2;
    gfx_text(c, ux - vw, vy, buf, vf, unheld ? UI_WARN : UI_ACCENT, 1);

    if (settings_derived(id) || settings_held(id)) {
        return;                 /* shown, not set here: no keys */
    }
    bool minus_down = (s.hit_kind == HIT_MINUS && s.hit_index == index);
    bool plus_down  = (s.hit_kind == HIT_PLUS  && s.hit_index == index);
    int by = r.y + (ROW_H - BTN_H) / 2;
    const bool cycles = settings_cycles(id);
    ui_button_font(c, gfx_rect_make(MINUS_X, by, BTN_W, BTN_H),
                   cycles ? "<" : "-", UI_EDGE_HI, minus_down, true,
                   STG_F_BODY);
    ui_button_font(c, gfx_rect_make(PLUS_X, by, BTN_W, BTN_H),
                   cycles ? ">" : "+", UI_EDGE_HI, plus_down, true,
                   STG_F_BODY);
}

static void draw_list(gfx_canvas_t *c)
{
    /* The list is clipped rather than masked, so a part-scrolled row is cut
     * cleanly at the pane edge instead of drawing over the strip. */
    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_set(c, gfx_rect_make(LIST_X, LIST_Y, LIST_W, LIST_H));
    /* SENSORS' list changes length with a FITTED edit or a reset: the
     * scroll follows before a row is placed. */
    stg_clamp_scroll();
    int n = stg_row_count();
    for (int i = 0; i < n; ++i) {
        draw_row(c, i);
    }
    c->clip = saved;

    /* Scroll position, drawn only when there is somewhere to scroll to. */
    if (s.scroll_max > 0) {
        int track_h = LIST_H;
        int knob_h = track_h * LIST_H / ((n > 0 ? n : 1) * ROW_PITCH);
        if (knob_h < 32) { knob_h = 32; }
        int knob_y = LIST_Y + (track_h - knob_h) * s.scroll / s.scroll_max;
        gfx_fill_round_rect(c, SCROLL_X, LIST_Y, SCROLL_W, track_h,
                            SCROLL_W / 2, UI_PANEL_SUNK);
        gfx_fill_round_rect(c, SCROLL_X, knob_y, SCROLL_W, knob_h,
                            SCROLL_W / 2, UI_ACCENT);
    }

    /* Which category is open, under the first column: in the head face
     * where it fits, as the rows' names, a longer name in the body face. */
    const gfx_rect_t nr = stg_strip_rect(0);
    const char *name = ui_setting_category(s.cat);
    const bool big = gfx_text_width(STG_F_HEAD, name, 1) <= nr.w - 16;
    const gfx_font_t *nf = big ? STG_F_HEAD : STG_F_BODY;
    gfx_text(c, LIST_X + 16, nr.y + (nr.h - gfx_text_height(nf, 1)) / 2,
             name, nf, UI_TEXT, 1);

    ui_button_font(c, stg_reset_rect(), TR(SET_RESET), UI_WARN,
                   s.hit_kind == HIT_RESET, true, STG_F_LABEL);
}

/*
 * The warning: what differs from the stored values, one line each as
 * "name: saved -> now" with the values as the list shows them, and the
 * three answers.  DISCARD is drawn inert while the bench is ARMED, and a
 * note says why; another says that the last save failed.
 */
static void draw_warning(gfx_canvas_t *c)
{
    const bool armed = stg_bench_armed();
    s.drawn_armed  = armed;
    s.drawn_dirty  = settings_dirty();
    s.drawn_asked  = settings_save_asked();
    s.drawn_failed = settings_save_failed();

    gfx_fill_rect(c, WARN_X, WARN_Y, WARN_W, WARN_H, UI_PANEL);
    for (int i = 0; i < 3; ++i) {
        gfx_draw_rect(c, WARN_X + i, WARN_Y + i, WARN_W - 2 * i,
                      WARN_H - 2 * i, UI_WARN);
    }
    const gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(WARN_X + 4, WARN_Y + 4, WARN_W - 8,
                                        WARN_H - 8));
    gfx_text(c, WARN_X + 16, WARN_Y + WARN_TITLE_Y, TR(SET_LEAVE_TITLE),
             STG_F_HEAD, UI_WARN, 1);
    (void)ui_text_wrap(c, gfx_rect_make(WARN_X + 16, WARN_Y + WARN_WHY_Y,
                                        WARN_W - 32, WARN_WHY_PITCH),
                       WARN_WHY_PITCH, TR(SET_LEAVE_WHY), STG_F_BODY,
                       UI_TEXT_DIM);
    char line[128];
    for (int i = 0; settings_screen_warn_line(i, line, sizeof(line)); ++i) {
        gfx_text(c, WARN_X + 16, WARN_Y + WARN_LINE_Y + i * WARN_PITCH, line,
                 STG_F_LABEL, UI_TEXT, 1);
    }
    int note_y = WARN_Y + WARN_NOTE_Y;
    if (s.drawn_failed) {
        gfx_text(c, WARN_X + 16, note_y, TR(SET_LEAVE_FAILED), STG_F_BODY,
                 UI_DANGER, 1);
        note_y += WARN_NOTE_PITCH;
    }
    if (armed) {
        gfx_text(c, WARN_X + 16, note_y, TR(SET_LEAVE_ARMED), STG_F_BODY,
                 UI_WARN, 1);
    }
    c->clip = saved;

    ui_button_font(c, stg_stay_rect(), TR(SET_STAY), UI_PANEL_HI,
                   s.hit_kind == HIT_STAY, true, STG_F_LABEL);
    ui_button_font(c, stg_discard_rect(), TR(SET_DISCARD), UI_WARN,
                   s.hit_kind == HIT_DISCARD, !armed, STG_F_LABEL);
    ui_button_font(c, stg_save_rect(), TR(SET_SAVE), UI_ACCENT,
                   s.hit_kind == HIT_LEAVE_SAVE, true, STG_F_LABEL);
}

void stg_render(gfx_canvas_t *c, int buffer_index)
{
    /* The keyboard and the keypad move on their own events: every change
     * bumps a revision, and a buffer drawn under another one is stale. */
    if (s.tk.revision != s.drawn_tk_rev || s.tk.open != s.drawn_tk) {
        settings_screen_invalidate();
        s.drawn_tk_rev = s.tk.revision;
        s.drawn_tk = s.tk.open;
    }
    if (s.kp.revision != s.drawn_kp_rev || s.kp.open != s.drawn_kp) {
        settings_screen_invalidate();
        s.drawn_kp_rev = s.kp.revision;
        s.drawn_kp = s.kp.open;
    }
    bool stale = true;
    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        stale = !s.chrome_valid[buffer_index] || s.drawn_cat != s.cat
                || s.drawn_list != s.list || s.drawn_warn != s.warn;
    }
    if (!stale) {
        return;
    }

    /* A list draws a SAFE-only row without keys while ARMED
     * (settings_safe_only()), so an arm or a disarm redraws it. */
    s.drawn_armed = stg_bench_armed();
    gfx_clear(c, UI_BG);
    if (s.kp.open) {
        /* The keypad covers the list and the strip: nothing under it is
         * drawn, as nothing under it presses. */
        ui_keypad_render(&s.kp, c);
    } else if (s.warn) {
        draw_warning(c);
    } else {
        if (s.list) {
            draw_list(c);
        } else {
            draw_hub(c);
        }
        draw_save(c);
    }
    ui_textkey_render(&s.tk, c);

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.chrome_valid[buffer_index] = true;
        s.drawn_cat = s.cat;
        s.drawn_list = s.list;
        s.drawn_warn = s.warn;
    }
}
