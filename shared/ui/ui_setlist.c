/**
 * @file
 * @brief The settings list's rows, touch handling and geometry (ui_setlist.h);
 *        ui_setlist_draw.c draws it.
 *
 * SETUP's category list, lifted out of settings_screen.c so every screen's
 * SETTINGS opens the same list: the rows, the keys and their repeat, the
 * scroll, the keypad of a number, the keyboard of a string, a choice list
 * of a custom row, the strip's RESET and SAVE and the leave warning.
 *
 * SPDX-License-Identifier: MIT
 */

/* ui_text.h first: ui_theme.h defines UI_TEXT as a colour, the table's
 * entry macro of the same name. */
#include "ui_text.h"
#include "ui_setlist.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "ui_pinlist.h"
#include "ui_screen.h"
#include "ui_setting_keypad.h"
#include "ui_widgets.h"

#include "ui_setlist_int.h"

/* ---------------------------------------------------------------- strings */

/**
 * @brief Per string setting: the name for the warning, a row and its
 *        keyboard's title, and the row's help line.  The DUT name has no
 *        row on SETUP.
 */
static const struct {
    ui_text_id_t label;  /**< the string's name */
    ui_text_id_t help;   /**< the row's help line, TX_COUNT for none */
} k_text_ui[SETTING_TEXT_COUNT] = {
    [SET_TEXT_DUT_NAME]     = { TX_SET_DUT_NAME, TX_COUNT },
    [SET_TEXT_LOG_AUTO_DIR] = { TX_SET_AUTO_DIR, TX_SET_AUTO_DIR_HELP },
    [SET_TEXT_LOG_USER_DIR] = { TX_SET_USER_DIR, TX_SET_USER_DIR_HELP },
};

const char *ui_setlist_text_label(setting_text_id_t id)
{
    if ((int)id < 0 || id >= SETTING_TEXT_COUNT) {
        return "";
    }
    return ui_tr(k_text_ui[id].label);
}

const char *sl_text_help(setting_text_id_t id)
{
    if ((int)id < 0 || id >= SETTING_TEXT_COUNT
        || k_text_ui[id].help == TX_COUNT) {
        return "";
    }
    return ui_tr(k_text_ui[id].help);
}

/* ----------------------------------------------------------- host's hooks */

static void invalidate(const ui_setlist_t *sl)
{
    if (sl->host->invalidate != NULL) {
        sl->host->invalidate();
    }
}

int sl_left(const ui_setlist_t *sl)
{
    return (sl->host->left != NULL) ? sl->host->left() : SL_M;
}

ui_setlist_row_t sl_row(const ui_setlist_t *sl, int i)
{
    return sl->host->row(i);
}

const char *sl_help(const ui_setlist_t *sl, setting_id_t id)
{
    return (sl->host->help != NULL) ? sl->host->help(id)
                                    : ui_setting_help(id);
}

bool sl_unheld(const ui_setlist_t *sl, setting_id_t id)
{
    return sl->host->unheld != NULL && sl->host->unheld(id);
}

const char *sl_locked(const ui_setlist_t *sl, setting_id_t id)
{
    return (sl->host->locked != NULL) ? sl->host->locked(id) : NULL;
}

bool sl_armed(void)
{
    return ui_router_status()->armed;
}

/**
 * @brief Why custom row @p r takes no press, NULL while it takes one.
 * @param r The row.
 * @return The reason.
 */
static const char *custom_locked(const ui_setlist_row_t *r)
{
    return (r->custom != NULL && r->custom->locked != NULL)
               ? r->custom->locked() : NULL;
}

/* --------------------------------------------------------------- geometry */

ui_setlist_row_t ui_setlist_table_row(const ui_setlist_row_t *rows, int n,
                                      int i)
{
    if (rows == NULL || i < 0 || i >= n) {
        return (ui_setlist_row_t){ (uint8_t)UI_SL_NONE, -1, (setting_id_t)0,
                                   TX_COUNT, NULL };
    }
    return rows[i];
}

int ui_setlist_count(const ui_setlist_t *sl)
{
    return sl->host->count();
}

gfx_rect_t ui_setlist_strip_rect(int col)
{
    return ui_footer_rect((ui_footer_slot_t)col);
}

gfx_rect_t ui_setlist_pane(const ui_setlist_t *sl)
{
    const int x = sl_left(sl);
    return gfx_rect_make(x, LIST_Y, LIST_R - x, LIST_H);
}

/**
 * @brief Row @p i's height: a heading's or a row's.
 * @param sl The list.
 * @param i  The row.
 * @return The height, px.
 */
static int row_h(const ui_setlist_t *sl, int i)
{
    if (!sl->host->headings) {
        return ROW_H;
    }
    return (sl_row(sl, i).kind == UI_SL_HEADING) ? HEAD_H : ROW_H;
}

/**
 * @brief Row @p i's top in the content, px: the pitches of the rows above.
 * @param sl The list.
 * @param i  The row.
 * @return The top.
 */
static int row_top(const ui_setlist_t *sl, int i)
{
    if (!sl->host->headings) {
        return i * ROW_PITCH;
    }
    int y = 0;
    for (int k = 0; k < i; ++k) {
        y += row_h(sl, k) + ROW_GAP;
    }
    return y;
}

int sl_pitch_sum(const ui_setlist_t *sl)
{
    const int n = ui_setlist_count(sl);
    return (n > 0) ? row_top(sl, n) : ROW_PITCH;
}

void ui_setlist_clamp(ui_setlist_t *sl)
{
    /* n rows are their heights plus the n-1 gaps between them.  A trailing
     * gap would give a 7-row list a 4px scroll range, and a scroll bar
     * beside a list that fits. */
    const int n = ui_setlist_count(sl);
    const int content = (n > 0) ? row_top(sl, n - 1) + row_h(sl, n - 1) : 0;
    sl->scroll_max = (content > LIST_H) ? (content - LIST_H) : 0;
    if (sl->scroll < 0) { sl->scroll = 0; }
    if (sl->scroll > sl->scroll_max) { sl->scroll = sl->scroll_max; }
}

void ui_setlist_scroll_to(ui_setlist_t *sl, int i)
{
    sl->scroll = row_top(sl, i);
    ui_setlist_clamp(sl);
    invalidate(sl);
}

gfx_rect_t ui_setlist_row_rect(const ui_setlist_t *sl, int i)
{
    const gfx_rect_t p = ui_setlist_pane(sl);
    return gfx_rect_make(p.x, LIST_Y + row_top(sl, i) - sl->scroll, p.w,
                         row_h(sl, i));
}

gfx_rect_t ui_setlist_row_part(const ui_setlist_t *sl, int i,
                               ui_sl_part_t part)
{
    const gfx_rect_t r = ui_setlist_row_rect(sl, i);
    switch (part) {
    case UI_SL_PART_MINUS:
        return gfx_rect_make(MINUS_X, r.y, BTN_W, ROW_H);
    case UI_SL_PART_VALUE:
        return gfx_rect_make(VALUE_X, r.y, VALUE_W, ROW_H);
    case UI_SL_PART_PLUS:
    default:
        return gfx_rect_make(PLUS_X, r.y, BTN_W, ROW_H);
    }
}

int ui_setlist_row_at(const ui_setlist_t *sl, int x, int y)
{
    const gfx_rect_t p = ui_setlist_pane(sl);
    if (!gfx_rect_contains(p, x, y)) {
        return -1;
    }
    const int n = ui_setlist_count(sl);
    if (!sl->host->headings) {
        const int idx = (y - LIST_Y + sl->scroll) / ROW_PITCH;
        return (idx >= 0 && idx < n) ? idx : -1;
    }
    /* A row owns its pitch, its gap under it included, as at the 70px
     * pitch. */
    const int cy = y - LIST_Y + sl->scroll;
    int top = 0;
    for (int i = 0; i < n; ++i) {
        const int pitch = row_h(sl, i) + ROW_GAP;
        if (cy >= top && cy < top + pitch) {
            return i;
        }
        top += pitch;
    }
    return -1;
}

gfx_rect_t sl_choice_box(const ui_setlist_t *sl)
{
    if (sl->choice_row < 0) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    const ui_setlist_row_t r = sl_row(sl, sl->choice_row);
    int n = (r.custom != NULL && r.custom->choices != NULL)
                ? r.custom->choices() : 0;
    if (n > UI_SL_CHOICES_MAX) {
        n = UI_SL_CHOICES_MAX;
    }
    /* Under the row where it fits, else above it, inside the pane. */
    const gfx_rect_t row = ui_setlist_row_rect(sl, sl->choice_row);
    const int h = n * CHOICE_H + 8;
    int y = row.y + row.h;
    if (y + h > LIST_Y + LIST_H) {
        y = row.y - h;
    }
    if (y < LIST_Y) {
        y = LIST_Y;
    }
    return gfx_rect_make(LIST_R - CHOICE_W, y, CHOICE_W, h);
}

gfx_rect_t ui_setlist_choice_rect(const ui_setlist_t *sl, int k)
{
    const gfx_rect_t b = sl_choice_box(sl);
    if (b.w == 0 || k < 0 || 4 + (k + 1) * CHOICE_H > b.h) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    return gfx_rect_make(b.x + 4, b.y + 4 + k * CHOICE_H, b.w - 8, CHOICE_H);
}

/* ------------------------------------------------------------------ rows */

/**
 * @brief Whether row @p i's setting takes no keys: a string, a heading, a
 *        custom row without step(), a setting derived from another, fixed by
 *        the board or locked by the host.
 * @param sl The list.
 * @param i  The row.
 * @return True when it has none.
 */
static bool keyless(const ui_setlist_t *sl, int i)
{
    const ui_setlist_row_t r = sl_row(sl, i);
    if (r.kind == UI_SL_CUSTOM) {
        return r.custom == NULL || r.custom->step == NULL
               || custom_locked(&r) != NULL;
    }
    if (r.kind != UI_SL_SETTING) {
        return true;
    }
    return settings_derived(r.id) || settings_fixed(r.id)
           || sl_locked(sl, r.id) != NULL;
}

/*
 * Whether (x, y) is on the "-" or the "+" key of row @p index: the key's
 * column over the row's own height.  The gap between two rows belongs to
 * neither key, so the keys of a column are apart and a press in a gap
 * scrolls and steps nothing.
 */
static bool on_key(const ui_setlist_t *sl, int kind, int index, int x, int y)
{
    const int kx = (kind == UI_SL_HIT_MINUS) ? MINUS_X : PLUS_X;
    if (x < kx || x >= kx + BTN_W || ui_setlist_row_at(sl, x, y) != index) {
        return false;
    }
    /* A row whose value follows another setting has no keys: a press
     * there is a press on the list.  Neither has a string's. */
    if (keyless(sl, index)) {
        return false;
    }
    return gfx_rect_contains(ui_setlist_row_rect(sl, index), x, y);
}

/**
 * @brief Whether (x, y) is on the value of row @p index: the gap between
 *        its "-" and "+" keys, over the row's own height, on a number the
 *        keypad takes (ui_setting_keypad_offered()).
 * @param sl    The list.
 * @param index The row.
 * @param x     The point's x.
 * @param y     The point's y.
 * @return True where a tap opens the row's keypad.
 */
static bool on_value(const ui_setlist_t *sl, int index, int x, int y)
{
    if (x < VALUE_X || x >= VALUE_X + VALUE_W
        || ui_setlist_row_at(sl, x, y) != index) {
        return false;
    }
    const ui_setlist_row_t r = sl_row(sl, index);
    if (r.kind != UI_SL_SETTING || sl_locked(sl, r.id) != NULL) {
        return false;
    }
    return ui_setting_keypad_offered(r.id)
           && gfx_rect_contains(ui_setlist_row_rect(sl, index), x, y);
}

/**
 * @brief Whether row @p index is a custom row a tap acts on: tap() or a
 *        choice list, and not locked.
 * @param sl    The list.
 * @param index The row.
 * @return True when it is.
 */
static bool tappable(const ui_setlist_t *sl, int index)
{
    const ui_setlist_row_t r = sl_row(sl, index);
    if (r.kind != UI_SL_CUSTOM || r.custom == NULL || custom_locked(&r)) {
        return false;
    }
    return r.custom->tap != NULL
           || (r.custom->choices != NULL && r.custom->choices() > 0);
}

/**
 * @brief A setting changed by a step or by the keypad: the host hears it,
 *        and the list is drawn again.
 * @param sl The list.
 * @param id The setting that changed; SETTING_COUNT for several.
 */
static void value_changed(ui_setlist_t *sl, setting_id_t id)
{
    if (sl->host->changed != NULL) {
        sl->host->changed(id);
    }
    invalidate(sl);
}

/**
 * @brief The "-" or "+" key of row @p index stepped @p steps times through
 *        settings_adjust(), or a custom row's step(); a string's row takes
 *        none.
 * @param sl    The list.
 * @param index The row.
 * @param steps The steps, negative for "-".
 */
static void step_row(ui_setlist_t *sl, int index, int steps)
{
    const ui_setlist_row_t r = sl_row(sl, index);
    if (r.kind == UI_SL_CUSTOM) {
        if (r.custom != NULL && r.custom->step != NULL
            && custom_locked(&r) == NULL) {
            r.custom->step(steps < 0 ? -1 : 1);
            invalidate(sl);
        }
        return;
    }
    if (r.kind != UI_SL_SETTING || settings_def(r.id) == NULL
        || sl_locked(sl, r.id) != NULL) {
        return;
    }
    settings_adjust(r.id, steps);
    value_changed(sl, r.id);
}

/* ---------------------------------------------------------------- overlays */

/**
 * @brief The keypad closes, with nothing typed taken.
 * @param sl The list.
 */
static void close_keypad(ui_setlist_t *sl)
{
    if (sl->kp.open) {
        ui_keypad_close(&sl->kp);
        invalidate(sl);
    }
}

/**
 * @brief A tap on the value of row @p index: the keypad of its number,
 *        over the list and the strip.
 * @param sl    The list.
 * @param index The row.
 */
static void open_keypad(ui_setlist_t *sl, int index)
{
    const setting_id_t id = sl_row(sl, index).id;
    ui_setting_keypad_open(&sl->kp, TK_AREA, id);
    sl->kp_id = id;
    invalidate(sl);
}

/* A tap on the row of string @p id: its keyboard, on the text in force.
 * The log folders take the folder mode (ui_textkey.h). */
static void open_text(ui_setlist_t *sl, setting_text_id_t id)
{
    if ((int)id < 0 || id >= SETTING_TEXT_COUNT) {
        return;
    }
    ui_textkey_open_folder(&sl->tk, TK_AREA, ui_tr(k_text_ui[id].label),
                           settings_text(id));
    sl->tk_text = id;
    invalidate(sl);
}

/* No press in progress, and no repeat running for one. */
static void drop_press(ui_setlist_t *sl)
{
    sl->have_press = false;
    sl->hit_kind  = UI_SL_HIT_NONE;
    sl->hit_index = -1;
    sl->dragging  = false;
    sl->held_for  = 0.0f;
    sl->repeating = false;
}

void ui_setlist_init(ui_setlist_t *sl, const ui_setlist_host_t *host)
{
    memset(sl, 0, sizeof(*sl));
    sl->host = host;
    sl->hit_index = -1;
    sl->tk.pressed = -1;
    sl->kp.pressed = -1;
    sl->choice_row = -1;
}

void ui_setlist_drop(ui_setlist_t *sl)
{
    sl->warn = false;
    sl->choice_row = -1;
    ui_textkey_close(&sl->tk);  /* what was typed is not taken */
    ui_keypad_close(&sl->kp);
    drop_press(sl);
}

void ui_setlist_open(ui_setlist_t *sl)
{
    ui_setlist_drop(sl);
    sl->open = true;
    sl->scroll = 0;
    ui_setlist_clamp(sl);
    invalidate(sl);
}

void ui_setlist_close(ui_setlist_t *sl)
{
    ui_setlist_drop(sl);
    sl->open = false;
    invalidate(sl);
}

bool ui_setlist_back(ui_setlist_t *sl)
{
    drop_press(sl);
    if (sl->kp.open) {
        close_keypad(sl);
        return true;
    }
    if (sl->tk.open) {
        ui_textkey_close(&sl->tk);
        invalidate(sl);
        return true;
    }
    if (sl->choice_row >= 0) {
        sl->choice_row = -1;
        invalidate(sl);
        return true;
    }
    if (sl->warn) {
        sl->warn = false;
        invalidate(sl);
        return true;
    }
    return false;
}

bool ui_setlist_save_offered(void)
{
    return settings_dirty() && !settings_save_asked();
}

bool ui_setlist_warn(ui_setlist_t *sl)
{
    /* A save asked for is the operator saying keep, and holds nothing; a
     * save refused (NOT SAVED) leaves the values dirty and holds it. */
    drop_press(sl);
    if (!ui_setlist_save_offered()) {
        return false;
    }
    sl->warn = true;
    sl->choice_row = -1;
    invalidate(sl);
    return true;
}

bool ui_setlist_covered(const ui_setlist_t *sl)
{
    return sl->kp.open || sl->tk.open || sl->warn || sl->choice_row >= 0;
}

int ui_setlist_keypad_setting(const ui_setlist_t *sl)
{
    return sl->kp.open ? (int)sl->kp_id : -1;
}

/* ------------------------------------------------------------ the warning */

/**
 * @brief A value of @p id as the leave question says it: with its unit,
 *        joined to a symbol ("6.00V"), a space before a word ("2 cells")
 *        and the pin word before its number ("GPIO 4"); no unit after OFF,
 *        a SENSORS shunt's 0; and a pin's -1 in the words of the row's
 *        help, "open".
 * @param id    the setting
 * @param value its value
 * @param out   takes the text
 * @param n     bytes at @p out
 */
static void leave_value(setting_id_t id, float value, char *out, size_t n)
{
    const setting_def_t *d = settings_def(id);
    const char *unit = (d != NULL && d->unit != NULL) ? d->unit : "";
    if (d != NULL && ui_unit_is_prefix(unit) && value < 0.0f) {
        snprintf(out, n, "%s", TR(SET_LEAVE_PIN_OPEN));
        return;
    }
    const bool off = d != NULL && d->off_below > 0.0f && value == 0.0f;
    /* A pin is named as every screen names it ("GPIO 4", never a count of
     * four GPIOs); every other unit of the schema joins its number (6S,
     * 6.00V, 250uΩ). */
    if (!off && ui_unit_is_prefix(unit)) {
        (void)ui_pin_name(out, n, (unsigned)value);
        return;
    }
    char v[32];
    (void)ui_setting_value_of(id, value, v, sizeof(v));
    snprintf(out, n, "%s%s", v, off ? "" : unit);
}

/* Change @p k, in schema order and the strings after the values, into
 * @p line as "name: saved -> now", each value with its unit. */
static void change_line(int k, char *line, size_t n)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        if (!settings_differs(id) || k-- > 0) {
            continue;
        }
        char was[40], now[40];
        leave_value(id, settings_stored(id), was, sizeof(was));
        leave_value(id, settings_get(id), now, sizeof(now));
        snprintf(line, n, "%s: %s -> %s", ui_setting_label(id), was, now);
        return;
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        const setting_text_id_t id = (setting_text_id_t)i;
        if (!settings_text_differs(id) || k-- > 0) {
            continue;
        }
        snprintf(line, n, "%s: %s -> %s", ui_tr(k_text_ui[i].label),
                 settings_stored_text(id), settings_text(id));
        return;
    }
    line[0] = '\0';
}

bool ui_setlist_warn_line(const ui_setlist_t *sl, int i, char *line,
                          size_t n)
{
    if (line == NULL || n == 0u) {
        return false;
    }
    line[0] = '\0';
    const int changes = settings_changes();
    const bool more = changes > UI_SL_WARN_LINES;
    const int shown = more ? UI_SL_WARN_LINES - 1 : changes;
    if (!sl->warn || i < 0 || i > shown || (i == shown && !more)) {
        return false;
    }
    if (i == shown) {
        snprintf(line, n, TR(SET_LEAVE_MORE), changes - shown);
    } else {
        change_line(i, line, n);
    }
    return true;
}

/* ------------------------------------------------------------------- input */

void ui_setlist_tick(ui_setlist_t *sl, float dt_s)
{
    if (sl->drawn_dirty != settings_dirty()
        || sl->drawn_asked != settings_save_asked()
        || sl->drawn_failed != settings_save_failed()
        || ((sl->warn || sl->open) && sl->drawn_armed != sl_armed())) {
        invalidate(sl);
    }
    /* A row held, derived or locked since its keypad opened takes no
     * value: the keypad goes, as the row's keys have gone. */
    if (sl->kp.open && (!ui_setting_keypad_offered(sl->kp_id)
                        || sl_locked(sl, sl->kp_id) != NULL)) {
        close_keypad(sl);
    }

    if (sl->hit_kind != UI_SL_HIT_MINUS && sl->hit_kind != UI_SL_HIT_PLUS) {
        sl->held_for = 0.0f;
        sl->repeating = false;
        return;
    }
    const float before = sl->held_for;
    /* A stalled frame is worth UI_HOLD_MAX_CREDIT_S (0.25s) and no more,
     * so a 1s stall steps at most 3 times at 8Hz and 8 times at 30Hz. */
    sl->held_for += ui_hold_credit(dt_s);
    if (sl->held_for < REPEAT_DELAY_S) {
        return;
    }
    const float hz = (sl->held_for > REPEAT_DELAY_S + REPEAT_FAST_S)
                         ? REPEAT_FAST_HZ : REPEAT_SLOW_HZ;
    /* One step as the delay ends, then one on each crossing of the repeat
     * period, so the rate is honest even if a frame runs long. */
    const int fired_before = (before < REPEAT_DELAY_S)
                                 ? 0 : 1 + (int)((before - REPEAT_DELAY_S) * hz);
    const int fired_now = 1 + (int)((sl->held_for - REPEAT_DELAY_S) * hz);
    for (int i = fired_before; i < fired_now; ++i) {
        step_row(sl, sl->hit_index, (sl->hit_kind == UI_SL_HIT_PLUS) ? 1 : -1);
        sl->repeating = true;
    }
}

/**
 * @brief The DOWN on the open choice list: a row of it, or anywhere else,
 *        which closes it on its release.
 * @param sl The list.
 * @param x  The DOWN's x.
 * @param y  Its y.
 */
static void press_choice(ui_setlist_t *sl, int x, int y)
{
    for (int k = 0; k < UI_SL_CHOICES_MAX; ++k) {
        if (gfx_rect_contains(ui_setlist_choice_rect(sl, k), x, y)) {
            sl->hit_kind = UI_SL_HIT_CHOICE;
            sl->hit_index = k;
            invalidate(sl);
            return;
        }
    }
    sl->hit_kind = UI_SL_HIT_CHOICE;
    sl->hit_index = -1;
}

/**
 * @brief The DOWN of the contact that holds the list: what it presses.
 * @param sl  The list.
 * @param evt The DOWN.
 * @param x   Its x.
 * @param y   Its y.
 */
static void press(ui_setlist_t *sl, const touch_event_t *evt, int x, int y)
{
    /* A DOWN the tracker made for a jump arms nothing that throws values
     * away, as it arms no key: a flick down the list arrives as one, and its
     * lift is a tap.  SAVE, STAY and a host's tile take it. */
    const bool jump = (evt->flags & TOUCH_FLAG_JUMP) != 0u;
    if (sl->warn) {
        /* The warning covers the screen: its three buttons and nothing
         * under them.  DISCARD presses only while it is offered. */
        sl->hit_kind =
            gfx_rect_contains(ui_setlist_strip_rect(0), x, y) ? UI_SL_HIT_STAY
            : gfx_rect_contains(ui_setlist_strip_rect(2), x, y)
                ? UI_SL_HIT_LEAVE_SAVE
            : (gfx_rect_contains(ui_setlist_strip_rect(1), x, y)
               && !sl_armed() && !jump)
                ? UI_SL_HIT_DISCARD
                : UI_SL_HIT_NONE;
        if (sl->hit_kind != UI_SL_HIT_NONE) {
            invalidate(sl);
        }
        return;
    }
    if (sl->choice_row >= 0) {
        press_choice(sl, x, y);
        return;
    }
    if (sl->host->press != NULL) {
        int index = -1;
        const int kind = sl->host->press(x, y, jump, &index);
        if (kind >= UI_SL_HIT_HOST) {
            sl->hit_kind = kind;
            sl->hit_index = index;
            invalidate(sl);
            return;
        }
    }
    if (sl->open && !jump
        && gfx_rect_contains(ui_setlist_strip_rect(1), x, y)) {
        sl->hit_kind = UI_SL_HIT_RESET;
        invalidate(sl);
        return;
    }
    /*
     * Only while it is offered.  With nothing to write, a button that
     * presses says a save happened; with a write already asked for, it
     * lights a button the render draws as inert.
     */
    if (ui_setlist_save_offered()
        && gfx_rect_contains(ui_setlist_strip_rect(2), x, y)) {
        sl->hit_kind = UI_SL_HIT_SAVE;
        invalidate(sl);
        return;
    }

    const int row = sl->open ? ui_setlist_row_at(sl, x, y) : -1;
    if (row >= 0) {
        /* A key is armed here and steps nothing: its release steps it, or
         * the repeat while it is held. */
        const ui_setlist_row_t r = sl_row(sl, row);
        sl->hit_index = row;
        sl->hit_kind = jump                       ? UI_SL_HIT_LIST
                       : r.kind == UI_SL_TEXT      ? UI_SL_HIT_TEXT
                       : on_key(sl, UI_SL_HIT_MINUS, row, x, y)
                           ? UI_SL_HIT_MINUS
                       : on_key(sl, UI_SL_HIT_PLUS, row, x, y)
                           ? UI_SL_HIT_PLUS
                       : on_value(sl, row, x, y)   ? UI_SL_HIT_VALUE
                       : tappable(sl, row)         ? UI_SL_HIT_ROW
                                                   : UI_SL_HIT_LIST;
        if (sl->hit_kind != UI_SL_HIT_LIST) {
            invalidate(sl);   /* the key draws pressed */
        }
        return;
    }
    sl->hit_kind = UI_SL_HIT_NONE;
}

/**
 * @brief A tap on custom row @p index: its tap(), or its choice list.
 * @param sl    The list.
 * @param index The row.
 */
static void tap_row(ui_setlist_t *sl, int index)
{
    const ui_setlist_row_t r = sl_row(sl, index);
    if (!tappable(sl, index)) {
        return;
    }
    if (r.custom->choices != NULL && r.custom->choices() > 0) {
        sl->choice_row = index;
        invalidate(sl);
        return;
    }
    r.custom->tap();
    invalidate(sl);
}

/**
 * @brief The release on the open choice list: a choice picked, or the list
 *        closed by a tap off it.
 * @param sl    The list.
 * @param index The choice pressed, -1 off the list.
 * @param x     The UP's x.
 * @param y     Its y.
 */
static void release_choice(ui_setlist_t *sl, int index, int x, int y)
{
    const int row = sl->choice_row;
    const bool on = index >= 0
                    && gfx_rect_contains(ui_setlist_choice_rect(sl, index), x, y);
    sl->choice_row = -1;
    invalidate(sl);
    if (!on || row < 0) {
        return;
    }
    const ui_setlist_row_t r = sl_row(sl, row);
    if (r.custom != NULL && r.custom->pick != NULL
        && custom_locked(&r) == NULL) {
        r.custom->pick(index);
    }
}

/**
 * @brief The UP of a tap the finger made: what it activates.
 * @param sl       The list.
 * @param kind     The hit kind the press took.
 * @param index    The row or the host's index it took.
 * @param repeated Whether a held key has repeated, which adds no step.
 * @param x        The UP's x.
 * @param y        Its y.
 */
static void release(ui_setlist_t *sl, int kind, int index, bool repeated,
                    int x, int y)
{
    if (kind >= UI_SL_HIT_HOST) {
        if (sl->host->release != NULL) {
            sl->host->release(kind, index, x, y);
        }
        return;
    }
    if (kind == UI_SL_HIT_MINUS || kind == UI_SL_HIT_PLUS) {
        /* One step for a tap.  A key that has repeated has stepped on the
         * timer and its release adds none.  A MOVE has given the key up
         * past the slop or outside it; the release is asked again because
         * it can be the first event to carry the point. */
        int dx = x - sl->press_x, dy = y - sl->press_y;
        if (dx < 0) { dx = -dx; }
        if (dy < 0) { dy = -dy; }
        if (!repeated && dx <= DRAG_SLOP && dy <= DRAG_SLOP
            && on_key(sl, kind, index, x, y)) {
            step_row(sl, index, (kind == UI_SL_HIT_PLUS) ? 1 : -1);
        }
    } else if (kind == UI_SL_HIT_VALUE && sl->open
               && on_value(sl, index, x, y)) {
        open_keypad(sl, index);
    } else if (kind == UI_SL_HIT_TEXT && sl->open
               && ui_setlist_row_at(sl, x, y) == index) {
        const ui_setlist_row_t r = sl_row(sl, index);
        if (r.kind == UI_SL_TEXT && r.text >= 0) {
            open_text(sl, (setting_text_id_t)r.text);
        }
    } else if (kind == UI_SL_HIT_ROW && sl->open
               && ui_setlist_row_at(sl, x, y) == index) {
        tap_row(sl, index);
    } else if (kind == UI_SL_HIT_CHOICE) {
        release_choice(sl, index, x, y);
    } else if (kind == UI_SL_HIT_RESET && sl->open
               && gfx_rect_contains(ui_setlist_strip_rect(1), x, y)) {
        if (sl->host->reset != NULL) {
            sl->host->reset();
            invalidate(sl);
            return;
        }
        /* The list's own settings and no other. */
        setting_id_t ids[64];
        int n = 0;
        for (int i = 0; i < ui_setlist_count(sl) && n < 64; ++i) {
            const ui_setlist_row_t r = sl_row(sl, i);
            if (r.kind == UI_SL_SETTING) {
                ids[n++] = r.id;
            }
        }
        settings_reset_rows(ids, n);
        value_changed(sl, (setting_id_t)SETTING_COUNT);
    } else if (kind == UI_SL_HIT_SAVE && ui_setlist_save_offered()
               && gfx_rect_contains(ui_setlist_strip_rect(2), x, y)) {
        /* Asked for, not written.  The application decides when the bench
         * can afford to stop for it. */
        settings_request_save();
        invalidate(sl);
    } else if (kind == UI_SL_HIT_STAY && sl->warn
               && gfx_rect_contains(ui_setlist_strip_rect(0), x, y)) {
        sl->warn = false;
        invalidate(sl);
    } else if (kind == UI_SL_HIT_LEAVE_SAVE && sl->warn
               && gfx_rect_contains(ui_setlist_strip_rect(2), x, y)) {
        /* The SAVE button's request, then the way out. */
        settings_request_save();
        sl->warn = false;
        if (sl->host->way_out != NULL) {
            sl->host->way_out();
        }
    } else if (kind == UI_SL_HIT_DISCARD && sl->warn && !sl_armed()
               && gfx_rect_contains(ui_setlist_strip_rect(1), x, y)) {
        /*
         * Every changed value back to its stored one, each by the path an
         * edit by hand takes: the observer, which sends a pin or a port to
         * the IOmcu, and the host's changed().  Never while ARMED, asked
         * again at the release: a bench that armed while the finger was
         * down keeps its values.  After a refused save settings_discard()
         * also asks for a write.
         */
        (void)settings_discard();
        sl->warn = false;
        if (sl->host->changed != NULL) {
            sl->host->changed((setting_id_t)SETTING_COUNT);
        }
        if (sl->host->way_out != NULL) {
            sl->host->way_out();
        }
    }
}

/* An event while the keyboard is open goes to the keyboard alone.  OK
 * sets the string, as an edit by hand does; SAVE keeps it, as it keeps a
 * value. */
static void textkey_event(ui_setlist_t *sl, const touch_event_t *evt)
{
    char text[SETTINGS_TEXT_MAX];
    if (ui_textkey_event(&sl->tk, evt, text, sizeof(text)) == UI_TEXTKEY_OK) {
        settings_set_text(sl->tk_text, text);
    }
    if (sl->tk.revision != sl->drawn_tk_rev || sl->tk.open != sl->drawn_tk) {
        invalidate(sl);
    }
}

/**
 * @brief An event while the keypad is open goes to the keypad alone.  OK
 *        sets the value through settings_set(), as a step does, and SAVE
 *        keeps it; a value the steps never reach keeps the keypad open,
 *        refused (ui_setting_keypad_ok()).
 * @param sl  The list.
 * @param evt The event.
 */
static void keypad_event(ui_setlist_t *sl, const touch_event_t *evt)
{
    float v = 0.0f;
    if (ui_keypad_event(&sl->kp, evt, &v) == UI_KEYPAD_OK) {
        if (sl_locked(sl, sl->kp_id) != NULL) {
            ui_keypad_close(&sl->kp);
        } else {
            if (sl->host->typed != NULL) {
                v = sl->host->typed(sl->kp_id, v);
            }
            if (ui_setting_keypad_ok(&sl->kp, sl->kp_id, v) == UI_SETKEY_SET) {
                value_changed(sl, sl->kp_id);
            }
        }
    }
    if (sl->kp.revision != sl->drawn_kp_rev || sl->kp.open != sl->drawn_kp) {
        invalidate(sl);
    }
}

void ui_setlist_event(ui_setlist_t *sl, const touch_event_t *evt)
{
    if (sl == NULL || evt == NULL) {
        return;
    }
    const int x = evt->point.x;
    const int y = evt->point.y;

    if (sl->kp.open) {
        keypad_event(sl, evt);
        return;
    }
    if (sl->tk.open) {
        textkey_event(sl, evt);
        return;
    }

    if (evt->type == TOUCH_EVENT_DOWN) {
        /* A DOWN with the id of the press held says that contact has gone
         * and its release with it: the press is this one's. */
        if (sl->have_press && evt->point.id != sl->press_id) {
            return;
        }
    } else if (!sl->have_press || evt->point.id != sl->press_id) {
        return;
    }

    switch (evt->type) {
    case TOUCH_EVENT_DOWN:
        sl->have_press = true;
        sl->press_id = evt->point.id;
        sl->press_x = x;
        sl->press_y = y;
        sl->last_y = y;
        sl->dragging = false;
        sl->repeating = false;
        sl->held_for = 0.0f;
        press(sl, evt, x, y);
        break;

    case TOUCH_EVENT_MOVE: {
        const int dy = y - sl->last_y;
        sl->last_y = y;
        if (sl->hit_kind == UI_SL_HIT_NONE || sl->hit_kind == UI_SL_HIT_CHOICE) {
            break;
        }
        if (sl->hit_kind >= UI_SL_HIT_HOST) {
            /* Slid off: the press ends and this contact selects nothing and
             * scrolls nothing. */
            if (sl->host->still != NULL
                && !sl->host->still(sl->hit_kind, sl->hit_index, x, y)) {
                sl->hit_kind = UI_SL_HIT_NONE;
                invalidate(sl);
            }
            break;
        }
        int moved = y - sl->press_y;
        if (moved < 0) { moved = -moved; }
        if (!sl->dragging && moved > DRAG_SLOP
            && (sl->hit_kind == UI_SL_HIT_LIST || sl->hit_kind == UI_SL_HIT_MINUS
                || sl->hit_kind == UI_SL_HIT_PLUS || sl->hit_kind == UI_SL_HIT_TEXT
                || sl->hit_kind == UI_SL_HIT_VALUE
                || sl->hit_kind == UI_SL_HIT_ROW)) {
            /* Turned into a scroll: give up the button. */
            sl->dragging = true;
            sl->hit_kind = UI_SL_HIT_LIST;
            invalidate(sl);
        }
        int side = x - sl->press_x;
        if (side < 0) { side = -side; }
        if ((sl->hit_kind == UI_SL_HIT_MINUS || sl->hit_kind == UI_SL_HIT_PLUS)
            && (side > DRAG_SLOP
                || !on_key(sl, sl->hit_kind, sl->hit_index, x, y))) {
            /* Off the key sideways, or out of it within the slop: the
             * press on the key ends, and the repeat with it.  The contact
             * can still scroll. */
            sl->hit_kind = UI_SL_HIT_LIST;
            invalidate(sl);
        }
        if (sl->dragging) {
            sl->scroll -= dy;
            ui_setlist_clamp(sl);
            invalidate(sl);
        }
        break;
    }

    case TOUCH_EVENT_UP:
        /* A contact that scrolled activates nothing, and neither does a
         * release the finger did not make: it ends the press.  The press
         * is dropped before it acts, so what it opens starts clean. */
        if (!sl->dragging && touch_event_is_tap_up(evt)) {
            const int kind = sl->hit_kind;
            const int index = sl->hit_index;
            const bool repeated = sl->repeating;
            drop_press(sl);
            invalidate(sl);
            release(sl, kind, index, repeated, x, y);
        } else {
            drop_press(sl);
            invalidate(sl);
        }
        break;
    }
}

void ui_setlist_cancel(ui_setlist_t *sl)
{
    /*
     * A hit held open acts on its release and a drag owns its track id, and
     * the GT911 reuses ids: a later contact that began somewhere else would
     * be taken for this one and press a control nobody touched.  The repeat
     * on the plus and minus keys runs on the frame timer while a hit
     * stands, so it stops here too.  A key held on the keyboard or the
     * keypad is let go; what was typed stays.
     */
    drop_press(sl);
    ui_textkey_cancel_press(&sl->tk);
    ui_keypad_cancel_press(&sl->kp);
    invalidate(sl);
}
