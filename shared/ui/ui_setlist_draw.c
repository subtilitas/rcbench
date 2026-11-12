/**
 * @file
 * @brief The settings list's drawing (ui_setlist.h): its rows, the scroll bar,
 *        the strip with SAVE, the choice list and the leave warning.
 *        ui_setlist.c holds the rows, the touch handling and the geometry.
 *
 * SPDX-License-Identifier: MIT
 */

/* ui_text.h first: ui_theme.h defines UI_TEXT as a colour, the table's
 * entry macro of the same name. */
#include "ui_text.h"
#include "ui_setlist.h"

#include <stdio.h>

#include "settings.h"
#include "ui_footer.h"
#include "ui_keypad.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

#include "ui_setlist_int.h"

/**
 * @brief Four states, and the label is the whole of the feedback: nothing to
 *        write (SAVED), something to write (SAVE), something asked for and
 *        waiting for a moment to write it in (WHEN IDLE), and an attempt the
 *        store refused (NOT SAVED).  WHEN IDLE is seen only while the bench
 *        is armed or a transfer is running; disarmed, the write is taken on
 *        the next frame.  SAVE is green while a press writes, WHEN IDLE
 *        amber, SAVED neutral; RESET beside it is neutral, an edit.  NOT
 *        SAVED has its label in the danger colour, because the values on
 *        this screen are not the values the next boot will load and nothing
 *        else on the panel says so.  The same button on SETUP's
 *        tiles and on every list: the values are one set.
 */
void ui_setlist_draw_save(ui_setlist_t *sl, gfx_canvas_t *c)
{
    const bool dirty  = settings_dirty();
    const bool asked  = settings_save_asked();
    const bool failed = settings_save_failed();
    sl->drawn_dirty  = dirty;
    sl->drawn_asked  = asked;
    sl->drawn_failed = failed;
    const char *label = !dirty ? TR(SET_SAVED)
                        : asked ? TR(SET_WHEN_IDLE)
                        : failed ? TR(SET_NOT_SAVED) : TR(SET_SAVE);
    /* Green while a press saves, amber while it waits for a SAFE bench,
     * the label red on a neutral button after a failed write. */
    if (dirty && failed && !asked) {
        ui_button_ink(c, ui_setlist_strip_rect(2), label, UI_PANEL_HI,
                      UI_DANGER, sl->hit_kind == UI_SL_HIT_SAVE,
                      ui_setlist_save_offered(), SL_F_LABEL);
        return;
    }
    ui_button_font(c, ui_setlist_strip_rect(2), label,
                   dirty ? (asked ? UI_WARN : UI_OK) : UI_PANEL_HI,
                   sl->hit_kind == UI_SL_HIT_SAVE, ui_setlist_save_offered(),
                   SL_F_LABEL);
}

/**
 * @brief A string's row: its name and help on the left, its text on the
 *        right in the value's colour, against the row's right edge where a
 *        setting's "+" is.  No keys: a tap opens the keyboard, and the row's
 *        edge takes the accent while it is pressed.  The text is in the body
 *        face, or in the label face where it is wider than the gap left of
 *        it.
 */
static void draw_text_row(const ui_setlist_t *sl, gfx_canvas_t *c, int index,
                          gfx_rect_t r, setting_text_id_t id)
{
    const bool down = (sl->hit_kind == UI_SL_HIT_TEXT && sl->hit_index == index);
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             down ? UI_ACCENT : UI_EDGE);

    const char *label = ui_setlist_text_label(id);
    const char *help = sl_text_help(id);
    const int right = PLUS_X + BTN_W;
    int left = r.x + 16 + gfx_text_width(SL_F_HEAD, label, 1);
    const int help_end = r.x + 16 + gfx_text_width(SL_F_LABEL, help, 1);
    left = ((help_end > left) ? help_end : left) + 16;

    const char *value = settings_text(id);
    const gfx_font_t *vf = SL_F_BODY;
    int vw = gfx_text_width(vf, value, 1);
    if (vw > right - left) {
        vf = SL_F_LABEL;
        vw = gfx_text_width(vf, value, 1);
    }
    const int vx = right - vw;

    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(r.x, r.y, vx - 10 - r.x, r.h));
    gfx_text(c, r.x + 16, r.y + ROW_NAME_DY, label, SL_F_HEAD, UI_TEXT, 1);
    if (help[0] != '\0') {
        gfx_text(c, r.x + 16, r.y + ROW_HELP_DY, help, SL_F_LABEL,
                 UI_TEXT_FAINT, 1);
    }
    c->clip = saved;
    gfx_text(c, vx, r.y + (ROW_H - gfx_text_height(vf, 1)) / 2, value, vf,
             UI_ACCENT, 1);
}

/**
 * @brief A row's "-" and "+" keys, or "<" and ">", centered in its height.
 * @param sl     The list.
 * @param c      The canvas.
 * @param index  The row.
 * @param r      Its rectangle.
 * @param cycles "<" and ">".
 */
static void draw_keys(const ui_setlist_t *sl, gfx_canvas_t *c, int index,
                      gfx_rect_t r, bool cycles)
{
    const bool minus_down = (sl->hit_kind == UI_SL_HIT_MINUS
                             && sl->hit_index == index);
    const bool plus_down  = (sl->hit_kind == UI_SL_HIT_PLUS
                             && sl->hit_index == index);
    const int by = r.y + (ROW_H - BTN_H) / 2;
    ui_button_font(c, gfx_rect_make(MINUS_X, by, BTN_W, BTN_H),
                   cycles ? "<" : "-", UI_EDGE_HI, minus_down, true,
                   SL_F_BODY);
    ui_button_font(c, gfx_rect_make(PLUS_X, by, BTN_W, BTN_H),
                   cycles ? ">" : "+", UI_EDGE_HI, plus_down, true,
                   SL_F_BODY);
}

/**
 * @brief A setting's row: its name and help, its value and unit, and its
 *        keys.  Its edge takes the accent while its value is pressed.
 * @param sl    The list.
 * @param c     The canvas.
 * @param index The row.
 * @param r     Its rectangle.
 * @param id    The setting.
 */
static void draw_setting_row(const ui_setlist_t *sl, gfx_canvas_t *c,
                             int index, gfx_rect_t r, setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (!d) {
        return;
    }

    /* A value the IOmcu does not hold: the row's edge, its value and one
     * line in place of its help, in the colour of a write that waits.  A
     * row the host locks: its reason in place of the help. */
    const bool unheld = sl_unheld(sl, id);
    const char *lock = sl_locked(sl, id);
    /* A press on the value, which opens the keypad on its release, lights
     * the row's edge as a press on a string's row does. */
    const bool value_down = (sl->hit_kind == UI_SL_HIT_VALUE
                             && sl->hit_index == index);
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             unheld ? UI_WARN
                                    : value_down ? UI_ACCENT : UI_EDGE);

    /* Label and help are clipped short of the keys rather than allowed to
     * run under them. */
    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(r.x, r.y, MINUS_X - 10 - r.x, r.h));
    gfx_text(c, r.x + 16, r.y + ROW_NAME_DY, ui_setting_label(id),
             SL_F_HEAD, UI_TEXT, 1);
    const char *help = (lock != NULL) ? lock : sl_help(sl, id);
    if (help[0] != '\0') {
        gfx_text(c, r.x + 16, r.y + ROW_HELP_DY, help, SL_F_LABEL,
                 (unheld || lock != NULL) ? UI_WARN : UI_TEXT_FAINT, 1);
    }
    c->clip = saved;

    /* Value, then the unit after it (a pin word before it: "GPIO 4"), both
     * right-aligned against the + key and centered in the row's height.  A
     * value takes the body face, a number and a switch's or a choice's word
     * alike.  A value wider than the gap takes the label face, because a
     * clipped value is worse than a small one; no value of the present
     * schema needs it (test_settings_rows). */
    char buf[32];
    ui_setting_value(id, buf, sizeof(buf));
    int ux = VALUE_R;
    const bool has_unit = d->unit && d->unit[0]
        && !(d->off_below > 0.0f && settings_get(id) == 0.0f);
    const bool before = has_unit && ui_unit_is_prefix(d->unit);
    const int uw = has_unit ? gfx_text_width(SL_F_LABEL, d->unit, 1) : 0;
    if (has_unit && !before) {
        gfx_text(c, VALUE_R - uw,
                 r.y + (ROW_H - gfx_text_height(SL_F_LABEL, 1)) / 2 + 2,
                 d->unit, SL_F_LABEL, UI_TEXT_DIM, 1);
        ux = VALUE_R - uw - 6;
    }
    const gfx_font_t *vf = SL_F_BODY;
    int vw = gfx_text_width(vf, buf, 1);
    const int avail = ux - (MINUS_X + BTN_W + 8) - (before ? uw + 6 : 0);
    if (vw > avail) {
        vf = SL_F_LABEL;
        vw = gfx_text_width(vf, buf, 1);
    }
    if (before) {
        /* A pin word leads its number: "GPIO 4". */
        gfx_text(c, ux - vw - 6 - uw,
                 r.y + (ROW_H - gfx_text_height(SL_F_LABEL, 1)) / 2 + 2,
                 d->unit, SL_F_LABEL, UI_TEXT_DIM, 1);
    }
    const int vy = r.y + (ROW_H - gfx_text_height(vf, 1)) / 2;
    gfx_text(c, ux - vw, vy, buf, vf, unheld ? UI_WARN : UI_ACCENT, 1);

    if (settings_derived(id) || settings_fixed(id) || settings_held(id)
        || lock != NULL) {
        return;                 /* shown, not set here: no keys */
    }
    draw_keys(sl, c, index, r, settings_cycles(id));
}

/**
 * @brief A custom row: its name and help, its value between its keys, or
 *        at the right with a ">" where a tap acts on it.  Its edge takes the
 *        accent while a tap on it is pressed.
 * @param sl    The list.
 * @param c     The canvas.
 * @param index The row.
 * @param r     Its rectangle.
 * @param cu    The row's hooks.
 */
static void draw_custom_row(const ui_setlist_t *sl, gfx_canvas_t *c,
                            int index, gfx_rect_t r,
                            const ui_setlist_custom_t *cu)
{
    if (cu == NULL) {
        return;
    }
    const char *lock = (cu->locked != NULL) ? cu->locked() : NULL;
    const bool keys = cu->step != NULL && lock == NULL;
    const bool taps = lock == NULL
                      && (cu->tap != NULL
                          || (cu->choices != NULL && cu->choices() > 0));
    const bool down = (sl->hit_kind == UI_SL_HIT_ROW && sl->hit_index == index)
                      || sl->choice_row == index;
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             down ? UI_ACCENT : UI_EDGE);

    char value[48] = "";
    if (cu->value_text != NULL) {
        cu->value_text(value, sizeof(value));
    }
    /* With keys the value sits between them, as a setting's; without, at
     * the row's right edge, a ">" after it where a tap acts and no key
     * does. */
    const bool chev = taps && !keys;
    const int chev_w = chev ? gfx_text_width(SL_F_BODY, ">", 1) + 8 : 0;
    const int right = keys ? VALUE_R : PLUS_X + BTN_W - chev_w;
    const gfx_font_t *vf = SL_F_BODY;
    int vw = gfx_text_width(vf, value, 1);
    const int avail = keys ? VALUE_R - (MINUS_X + BTN_W + 8) : right - MINUS_X;
    if (vw > avail) {
        vf = SL_F_LABEL;
        vw = gfx_text_width(vf, value, 1);
    }
    const int vx = right - vw;
    const int name_r = keys ? MINUS_X - 10 : vx - 10;

    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(r.x, r.y, name_r - r.x, r.h));
    gfx_text(c, r.x + 16, r.y + ROW_NAME_DY, ui_tr(cu->label), SL_F_HEAD,
             UI_TEXT, 1);
    const char *help = (lock != NULL) ? lock
                       : (cu->help != TX_COUNT) ? ui_tr(cu->help) : "";
    if (help[0] != '\0') {
        gfx_text(c, r.x + 16, r.y + ROW_HELP_DY, help, SL_F_LABEL,
                 (lock != NULL) ? UI_WARN : UI_TEXT_FAINT, 1);
    }
    c->clip = saved;
    gfx_text(c, vx, r.y + (ROW_H - gfx_text_height(vf, 1)) / 2, value, vf,
             UI_ACCENT, 1);
    if (chev) {
        gfx_text(c, PLUS_X + BTN_W - chev_w + 8,
                 r.y + (ROW_H - gfx_text_height(SL_F_BODY, 1)) / 2, ">",
                 SL_F_BODY, UI_TEXT_DIM, 1);
    }
    if (keys) {
        draw_keys(sl, c, index, r, cu->cycles);
    }
}

/**
 * @brief A heading over a section: its name in the body face in the accent,
 *        its line in the label face after it, a rule under both.
 * @param c  The canvas.
 * @param r  Its rectangle.
 * @param e  The row.
 */
static void draw_heading(gfx_canvas_t *c, gfx_rect_t r,
                         const ui_setlist_row_t *e)
{
    const char *name = ui_tr(e->heading);
    gfx_text(c, r.x + 16, r.y + HEAD_NAME_DY, name, SL_F_BODY, UI_ACCENT, 1);
    char line[64] = "";
    if (e->custom != NULL && e->custom->value_text != NULL) {
        e->custom->value_text(line, sizeof(line));
    }
    /* A heading whose locked() names something draws its line in the
     * warning colour. */
    const bool warn = e->custom != NULL && e->custom->locked != NULL
                      && e->custom->locked() != NULL;
    if (line[0] != '\0') {
        const int lx = r.x + 16 + gfx_text_width(SL_F_BODY, name, 1) + 24;
        gfx_text(c, lx, r.y + HEAD_NAME_DY + 3, line, SL_F_LABEL,
                 warn ? UI_WARN : UI_TEXT_DIM, 1);
    }
    ui_rule(c, r.x + 16, r.y + r.h - 3, r.w - 32, UI_EDGE);
}

/**
 * @brief List row @p index by its kind.
 * @param sl    The list.
 * @param c     The canvas.
 * @param index The row.
 */
static void draw_row(const ui_setlist_t *sl, gfx_canvas_t *c, int index)
{
    const gfx_rect_t r = ui_setlist_row_rect(sl, index);
    if (r.y + r.h <= LIST_Y || r.y >= LIST_Y + LIST_H) {
        return;
    }
    const ui_setlist_row_t e = sl_row(sl, index);
    switch (e.kind) {
    case UI_SL_TEXT:
        if (e.text >= 0) {
            draw_text_row(sl, c, index, r, (setting_text_id_t)e.text);
        }
        break;
    case UI_SL_SETTING:
        draw_setting_row(sl, c, index, r, e.id);
        break;
    case UI_SL_CUSTOM:
        draw_custom_row(sl, c, index, r, e.custom);
        break;
    case UI_SL_HEADING:
        draw_heading(c, r, &e);
        break;
    default:
        break;
    }
}

void ui_setlist_draw_rows(ui_setlist_t *sl, gfx_canvas_t *c)
{
    const gfx_rect_t pane = ui_setlist_pane(sl);
    /* The list is clipped rather than masked, so a part-scrolled row is cut
     * cleanly at the pane edge instead of drawing over the strip. */
    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, pane);
    /* A list that changes length with an enable row or a reset: the
     * scroll follows before a row is placed. */
    ui_setlist_clamp(sl);
    const int n = ui_setlist_count(sl);
    for (int i = 0; i < n; ++i) {
        draw_row(sl, c, i);
    }
    c->clip = saved;

    /* Scroll position, drawn only when there is somewhere to scroll to. */
    if (sl->scroll_max > 0) {
        const int track_h = LIST_H;
        int knob_h = track_h * LIST_H / sl_pitch_sum(sl);
        if (knob_h < 32) { knob_h = 32; }
        const int knob_y = LIST_Y + (track_h - knob_h) * sl->scroll
                                    / sl->scroll_max;
        gfx_fill_round_rect(c, SCROLL_X, LIST_Y, SCROLL_W, track_h,
                            SCROLL_W / 2, UI_PANEL_SUNK);
        gfx_fill_round_rect(c, SCROLL_X, knob_y, SCROLL_W, knob_h,
                            SCROLL_W / 2, UI_ACCENT);
    }
}

void ui_setlist_draw_strip(ui_setlist_t *sl, gfx_canvas_t *c)
{
    const char *name = (sl->host->name != NULL) ? sl->host->name() : "";
    char line[48] = "";
    bool warn = false;
    if (sl->host->name_line == NULL
        || !sl->host->name_line(line, sizeof(line), &warn)) {
        line[0] = '\0';
    }
    ui_footer_name(c, ui_setlist_strip_rect(0), name, line, warn);

    char reset[48];
    if (sl->host->reset_label != NULL) {
        sl->host->reset_label(reset, sizeof(reset));
    } else {
        snprintf(reset, sizeof(reset), TR(SET_RESET_ONE), name);
    }
    ui_button_font(c, ui_setlist_strip_rect(1), reset, UI_PANEL_HI,
                   sl->hit_kind == UI_SL_HIT_RESET, true, SL_F_LABEL);
}

/**
 * @brief The open choice list over the rows, if one is open.
 * @param sl The list.
 * @param c  The canvas.
 */
static void ui_setlist_draw_choices(const ui_setlist_t *sl, gfx_canvas_t *c)
{
    if (sl->choice_row < 0) {
        return;
    }
    const ui_setlist_row_t r = sl_row(sl, sl->choice_row);
    if (r.custom == NULL || r.custom->choices == NULL
        || r.custom->choice == NULL) {
        return;
    }
    ui_choice_list(c, sl_choice_box(sl));
    const int chosen = (r.custom->chosen != NULL) ? r.custom->chosen() : -1;
    for (int k = 0; k < r.custom->choices() && k < UI_SL_CHOICES_MAX; ++k) {
        ui_choice_row(c, ui_setlist_choice_rect(sl, k), r.custom->choice(k),
                      k == chosen,
                      sl->hit_kind == UI_SL_HIT_CHOICE && sl->hit_index == k);
    }
}

/**
 * @brief The warning: what differs from the stored values, one line each as
 *        "name: saved -> now" with the values as the list shows them, and the
 *        three answers.  DISCARD is drawn inert while the bench is ARMED, and
 *        a note says why; another says that the last save failed.
 */
void ui_setlist_draw_warning(ui_setlist_t *sl, gfx_canvas_t *c)
{
    const bool armed = sl_bench_armed();
    sl->drawn_armed  = armed;
    sl->drawn_dirty  = settings_dirty();
    sl->drawn_asked  = settings_save_asked();
    sl->drawn_failed = settings_save_failed();

    gfx_fill_rect(c, WARN_X, WARN_Y, WARN_W, WARN_H, UI_PANEL);
    for (int i = 0; i < 3; ++i) {
        gfx_draw_rect(c, WARN_X + i, WARN_Y + i, WARN_W - 2 * i,
                      WARN_H - 2 * i, UI_WARN);
    }
    const gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(WARN_X + 4, WARN_Y + 4, WARN_W - 8,
                                        WARN_H - 8));
    gfx_text(c, WARN_X + 16, WARN_Y + WARN_TITLE_Y, TR(SET_LEAVE_TITLE),
             SL_F_HEAD, UI_WARN, 1);
    (void)ui_text_wrap(c, gfx_rect_make(WARN_X + 16, WARN_Y + WARN_WHY_Y,
                                        WARN_W - 32, WARN_WHY_PITCH),
                       WARN_WHY_PITCH, TR(SET_LEAVE_WHY), SL_F_BODY,
                       UI_TEXT_DIM);
    char line[128];
    for (int i = 0; ui_setlist_warn_line(sl, i, line, sizeof(line)); ++i) {
        gfx_text(c, WARN_X + 16, WARN_Y + WARN_LINE_Y + i * WARN_PITCH, line,
                 SL_F_LABEL, UI_TEXT, 1);
    }
    int note_y = WARN_Y + WARN_NOTE_Y;
    if (sl->drawn_failed) {
        gfx_text(c, WARN_X + 16, note_y, TR(SET_LEAVE_FAILED), SL_F_BODY,
                 UI_DANGER, 1);
        note_y += WARN_NOTE_PITCH;
    }
    if (armed) {
        gfx_text(c, WARN_X + 16, note_y, TR(SET_LEAVE_ARMED), SL_F_BODY,
                 UI_WARN, 1);
    }
    c->clip = saved;

    ui_button_font(c, ui_setlist_strip_rect(0), TR(SET_STAY), UI_PANEL_HI,
                   sl->hit_kind == UI_SL_HIT_STAY, true, SL_F_LABEL);
    ui_button_font(c, ui_setlist_strip_rect(1), TR(SET_DISCARD), UI_PANEL_HI,
                   sl->hit_kind == UI_SL_HIT_DISCARD, !armed, SL_F_LABEL);
    ui_button_font(c, ui_setlist_strip_rect(2), TR(SET_SAVE), UI_OK,
                   sl->hit_kind == UI_SL_HIT_LEAVE_SAVE, true, SL_F_LABEL);
}

void ui_setlist_render(ui_setlist_t *sl, gfx_canvas_t *c)
{
    /* The keyboard and the keypad move on their own events: their
     * revisions as drawn. */
    sl->drawn_tk_rev = sl->tk.revision;
    sl->drawn_tk = sl->tk.open;
    sl->drawn_kp_rev = sl->kp.revision;
    sl->drawn_kp = sl->kp.open;
    /* A list draws a SAFE-only row without keys while ARMED
     * (settings_safe_only()), so an arm or a disarm redraws it. */
    sl->drawn_armed = sl_bench_armed();
    gfx_fill_rect(c, 0, 0, UI_PANEL_W, UI_BODY_H, UI_BG);
    if (sl->kp.open) {
        /* The keypad covers the list and the strip: nothing under it is
         * drawn, as nothing under it presses. */
        ui_keypad_render(&sl->kp, c);
        return;
    }
    if (sl->warn) {
        ui_setlist_draw_warning(sl, c);
    } else {
        ui_setlist_draw_rows(sl, c);
        ui_setlist_draw_strip(sl, c);
        ui_setlist_draw_save(sl, c);
        ui_setlist_draw_choices(sl, c);
    }
    ui_textkey_render(&sl->tk, c);
}
