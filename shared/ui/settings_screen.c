/**
 * @file
 * @brief The settings screen: a hub of tiles, and one renderer over the whole
 * schema.
 *
 * SETUP opens on a grid of tiles, the menu's kind: one per category it
 * holds (PANEL, SD CARD, SENSORS), and one per screen of its own that SETUP
 * reaches (OUTPUTS, WI-FI, UPDATES).  A category's tile opens its list across the screen's width;
 * SENSORS' list stands right of a left list of its 8 sensors and shows the
 * selected one's rows (sensors_pane.h), each entry with its own scroll.
 * The list walks the rows shared/settings declares (settings_rows.c) and
 * draws each row by its type: a stepper for a number, a toggle for a bool, a choice for an
 * enum.  A tap on a number's value, between its "-" and "+" keys, opens the
 * keypad over the list (ui_setting_keypad.h); its OK sets the value through
 * settings_set(), as a step does.  A category's strings follow its
 * settings, each a row that shows
 * its text and opens the on-screen keyboard on a tap (SD CARD's two log
 * folders, in the keyboard's folder mode).  A new setting is a schema row,
 * and a new tile is a line of k_tiles (settings_screen_int.h); nothing else
 * here changes.  settings_draw.c draws the screen.
 *
 * The home tag that leaves SETUP with settings unsaved and no save asked
 * is held: a warning lists what differs from the stored values and offers
 * SAVE, DISCARD and STAY.  A tile that opens a screen of its own is not a
 * way out, and no route the application takes is held (ui_screen_t.hold).
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include <stdio.h>
#include <string.h>

#include "sense_link.h"
#include "sensors_pane.h"
#include "sensors_rows.h"
#include "settings.h"
#include "settings_rows_int.h"
#include "settings_sensors.h"
#include "tone_link.h"
#include "ui_board.h"
#include "ui_icons.h"
#include "ui_keypad.h"
#include "ui_pinlist.h"
#include "ui_setting_keypad.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

#include "settings_screen_int.h"

/* The screen's state; settings_screen_int.h names it s. */
stg_state_t stg_s;

/*
 * The rows the IOmcu does not hold, as the control task last said.
 * Outside s: what the IOmcu holds does not change when the screen
 * is reset.
 */
static uint16_t s_sense_unheld;
/** @brief The tap's rows the IOmcu does not hold, TONE_LINK_ROWS_*. */
static uint8_t  s_tone_unheld;

void settings_screen_invalidate(void)
{
    memset(s.chrome_valid, 0, sizeof(s.chrome_valid));
}

void settings_apply_ui(void)
{
    /* The language with the theme: a change repaints every screen in it
     * on the next frame, through the invalidation below. */
    ui_text_set_language((ui_lang_t)settings_get_int(SET_LANGUAGE));
    ui_theme_set((ui_theme_id_t)settings_get_int(SET_THEME));
    ui_theme_set_brightness(settings_get_int(SET_BRIGHTNESS));
    ui_theme_set_contrast(settings_get_int(SET_CONTRAST));
    ui_router_invalidate();
}

void settings_screen_set_unheld(uint16_t sense_rows, uint8_t tone_rows)
{
    if (sense_rows != s_sense_unheld || tone_rows != s_tone_unheld) {
        s_sense_unheld = sense_rows;
        s_tone_unheld  = tone_rows;
        settings_screen_invalidate();
    }
}

const char *settings_screen_help(setting_id_t id)
{
    if (settings_fixed(id) && !settings_screen_unheld(id)) {
        return TR(SET_PIN_SOLDERED);
    }
    if (!settings_screen_unheld(id)) {
        return ui_setting_help(id);
    }
    if (settings_sensors_row(id)) {
        return sensors_rows_why(id);
    }
    return TR(SET_UNHELD);
}

bool settings_screen_unheld(setting_id_t id)
{
    if (settings_sensors_row(id)) {
        return sensors_rows_unheld(id);
    }
    uint16_t sense = 0u;
    uint8_t  tone  = 0u;
    switch (id) {
    case SET_INA228_EN:    sense = SENSE_LINK_ROW_I228;        break;
    case SET_INA228_ADDR:  sense = SENSE_LINK_ROW_I228_ADDR;   break;
    case SET_INA228_UOHM:  sense = SENSE_LINK_ROW_I228_SHUNT;  break;
    case SET_INA228_MAX_A: sense = SENSE_LINK_ROW_I228_MAX;    break;
    case SET_SENSE_SDA:
    case SET_SENSE_SCL:    sense = SENSE_LINK_ROW_PINS;        break;
    case SET_ENC_EN:       sense = SENSE_LINK_ROW_ENC;         break;
    case SET_TONE_EN:
    case SET_TONE_PIN:
    case SET_TONE_F_MIN:
    case SET_TONE_F_MAX:   tone = TONE_LINK_ROWS_TAP;          break;
    case SET_TONE_SPLIT:
    case SET_TONE_GAP:
    case SET_TONE_PERIODS: tone = TONE_LINK_ROWS_BEEP;         break;
    default:
        break;
    }
    return (s_sense_unheld & sense) != 0u || (s_tone_unheld & tone) != 0u;
}

static void drop_press(void);

/* ---------------------------------------------------------------- geometry */

bool settings_hub_slot(int index, gfx_rect_t *out)
{
    if (index < 0 || index >= SETTINGS_HUB_SLOTS) {
        return false;
    }
    if (out != NULL) {
        const int col = index % HUB_COLS;
        const int row = index / HUB_COLS;
        *out = gfx_rect_make(HUB_M + col * (HUB_TW + HUB_M),
                             HUB_M + row * (HUB_TH + HUB_M), HUB_TW, HUB_TH);
    }
    return true;
}

int settings_hub_tiles(void)
{
    return stg_tile_count();
}

/** @brief LAYOUT's tile, last on a board whose pins are soldered. */
static const hub_tile_t k_layout_tile =
    HUB_SCREEN(SCREEN_LAYOUT, TX_SET_HUB_LAYOUT, ui_icon_board);

bool settings_screen_opens(ui_screen_id_t id)
{
    for (int i = 0; i < TILE_COUNT; ++i) {
        if (k_tiles[i].cat < 0 && k_tiles[i].screen == id) {
            return true;
        }
    }
    return id == k_layout_tile.screen;
}

int settings_screen_category(void)
{
    return s.list ? (int)s.cat : -1;
}

int stg_list_x(void)
{
    return (s.list && s.cat == SET_CAT_SENSORS) ? PANE_X : HUB_M;
}

gfx_rect_t settings_screen_entry_rect(int entry)
{
    return gfx_rect_make(HUB_M, LIST_Y + entry * ENTRY_PITCH, ENTRY_W, ENTRY_H);
}

gfx_rect_t stg_tile_rect(int i)
{
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    (void)settings_hub_slot(i, &r);
    return r;
}

int stg_tile_count(void)
{
    return TILE_COUNT + ((ui_board_roles() != NULL) ? 1 : 0);
}

const hub_tile_t *stg_hub_tile(int i)
{
    return (i >= TILE_COUNT) ? &k_layout_tile : &k_tiles[i];
}

ui_screen_id_t settings_hub_tile_screen(int i)
{
    if (i < 0 || i >= stg_tile_count()) {
        return SCREEN_COUNT;
    }
    const hub_tile_t *t = stg_hub_tile(i);
    return (t->cat < 0) ? t->screen : SCREEN_SETUP;
}

int settings_hub_cat_tile(setting_cat_t cat)
{
    for (int i = 0; i < TILE_COUNT; ++i) {
        if (k_tiles[i].cat == (int)cat) {
            return i;
        }
    }
    return -1;
}

static int tile_at(int x, int y)
{
    for (int i = 0; i < stg_tile_count(); ++i) {
        if (gfx_rect_contains(stg_tile_rect(i), x, y)) {
            return i;
        }
    }
    return -1;
}

/* A column of the strip, under the hub's column @p col. */
gfx_rect_t stg_strip_rect(int col)
{
    return gfx_rect_make(HUB_M + col * (HUB_TW + HUB_M), STRIP_Y, HUB_TW,
                         STRIP_H);
}

gfx_rect_t stg_reset_rect(void)
{
    return stg_strip_rect(1);
}

gfx_rect_t stg_save_rect(void)
{
    return stg_strip_rect(2);
}

/* The one condition the press, the highlight and the render all read. */
bool stg_save_offered(void)
{
    return settings_dirty() && !settings_save_asked();
}

/*
 * The same condition holds the home tag.  A save asked for is the operator
 * saying keep, and holds nothing; a save refused (NOT SAVED) leaves the
 * values dirty and holds it.
 */
static bool leave_held(void)
{
    return stg_save_offered();
}

/* DISCARD changes nothing while the bench is ARMED: see release(). */
bool stg_bench_armed(void)
{
    return ui_router_status()->armed;
}

gfx_rect_t stg_stay_rect(void)
{
    return stg_strip_rect(0);
}

gfx_rect_t stg_discard_rect(void)
{
    return stg_strip_rect(1);
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

bool settings_screen_warning(void)
{
    return s.warn;
}

bool settings_screen_warn_line(int i, char *line, size_t n)
{
    if (line == NULL || n == 0u) {
        return false;
    }
    line[0] = '\0';
    const int changes = settings_changes();
    const bool more = changes > SETTINGS_WARN_LINES;
    const int shown = more ? SETTINGS_WARN_LINES - 1 : changes;
    if (!s.warn || i < 0 || i > shown || (i == shown && !more)) {
        return false;
    }
    if (i == shown) {
        snprintf(line, n, TR(SET_LEAVE_MORE), changes - shown);
    } else {
        change_line(i, line, n);
    }
    return true;
}

/* The home tag leaves for @p to: held while something is unsaved. */
static bool hold(ui_screen_id_t to)
{
    drop_press();
    if (!leave_held()) {
        return false;
    }
    s.warn = true;
    s.warn_to = to;
    settings_screen_invalidate();
    return true;
}

int stg_row_count(void)
{
    return settings_screen_rows(s.cat);
}

void stg_clamp_scroll(void)
{
    /* n rows are n*ROW_H plus the n-1 gaps between them.  A trailing gap
     * would give a 7-row category a 4px scroll range, and a scroll indicator
     * beside a list that fits. */
    int n = stg_row_count();
    int content = (n > 0) ? (n * ROW_H + (n - 1) * ROW_GAP) : 0;
    s.scroll_max = (content > LIST_H) ? (content - LIST_H) : 0;
    if (s.scroll < 0) { s.scroll = 0; }
    if (s.scroll > s.scroll_max) { s.scroll = s.scroll_max; }
}

/** Screen-space rect of a row, or an empty rect when it is scrolled away. */
gfx_rect_t stg_row_rect(int index)
{
    int y = LIST_Y + index * ROW_PITCH - s.scroll;
    return gfx_rect_make(LIST_X, y, LIST_W, ROW_H);
}

gfx_rect_t settings_screen_list_rect(void)
{
    return gfx_rect_make(LIST_X, LIST_Y, LIST_W, LIST_H);
}

gfx_rect_t settings_screen_row_rect(int row)
{
    return stg_row_rect(row);
}

gfx_rect_t settings_screen_row_part(int row, settings_row_part_t part)
{
    const gfx_rect_t r = stg_row_rect(row);
    switch (part) {
    case SETTINGS_ROW_MINUS:
        return gfx_rect_make(MINUS_X, r.y, BTN_W, ROW_H);
    case SETTINGS_ROW_VALUE:
        return gfx_rect_make(VALUE_X, r.y, VALUE_W, ROW_H);
    case SETTINGS_ROW_PLUS:
    default:
        return gfx_rect_make(PLUS_X, r.y, BTN_W, ROW_H);
    }
}

static int row_at(int x, int y)
{
    if (x < LIST_X || x >= LIST_X + LIST_W || y < LIST_Y || y >= LIST_Y + LIST_H) {
        return -1;
    }
    int idx = (y - LIST_Y + s.scroll) / ROW_PITCH;
    return (idx >= 0 && idx < stg_row_count()) ? idx : -1;
}

/* The entry of row @p index; a string's carries text >= 0. */
settings_row_t stg_row_entry(int index)
{
    return settings_rows_entry(s.cat, index);
}

/* The string of row @p index, or -1 for a setting. */
static int row_text(int index)
{
    return stg_row_entry(index).text;
}

static setting_id_t row_setting(int index)
{
    return stg_row_entry(index).id;
}

/*
 * Whether (x, y) is on the "-" or the "+" key of row @p index: the key's
 * column over the row's own height.  The gap between two rows belongs to
 * neither key, so the keys of a column are apart and a press in a gap
 * scrolls and steps nothing.
 */
static bool on_key(int kind, int index, int x, int y)
{
    const int kx = (kind == HIT_MINUS) ? MINUS_X : PLUS_X;
    if (x < kx || x >= kx + BTN_W || row_at(x, y) != index) {
        return false;
    }
    /* A row whose value follows another setting has no keys: a press
     * there is a press on the list.  Neither has a string's. */
    if (row_text(index) >= 0
        || settings_derived(row_setting(index))
        || settings_fixed(row_setting(index))) {
        return false;
    }
    return gfx_rect_contains(stg_row_rect(index), x, y);
}

/**
 * @brief Whether (x, y) is on the value of row @p index: the gap between
 *        its "-" and "+" keys, over the row's own height, on a number the
 *        keypad takes (ui_setting_keypad_offered()).
 * @param index The row.
 * @param x The point's x, in the screen's coordinates.
 * @param y The point's y.
 * @return True where a tap opens the row's keypad.
 */
static bool on_value(int index, int x, int y)
{
    if (x < VALUE_X || x >= VALUE_X + VALUE_W || row_at(x, y) != index
        || row_text(index) >= 0) {
        return false;
    }
    return ui_setting_keypad_offered(row_setting(index))
           && gfx_rect_contains(stg_row_rect(index), x, y);
}

/* ------------------------------------------------------------------- input */

/**
 * @brief A setting changed by a step or by the keypad: the PANEL rows reach
 *        the interface at once, and the list is drawn again.
 * @param id The setting that changed.
 */
static void value_changed(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d != NULL && d->cat == SET_CAT_APP) {
        settings_apply_ui();      /* language, theme, brightness, contrast */
    }
    settings_screen_invalidate();
}

/**
 * @brief The "-" or "+" key of row @p index stepped @p steps times through
 *        settings_adjust(); a string's row takes none.
 * @param index The row.
 * @param steps The steps, negative for "-".
 */
static void step_row(int index, int steps)
{
    if (row_text(index) >= 0) {
        return;
    }
    setting_id_t id = row_setting(index);
    const setting_def_t *d = settings_def(id);
    if (!d) {
        return;
    }
    settings_adjust(id, steps);
    value_changed(id);
}

/**
 * @brief The keypad closes, with nothing typed taken.
 */
static void close_keypad(void)
{
    if (s.kp.open) {
        ui_keypad_close(&s.kp);
        settings_screen_invalidate();
    }
}

/**
 * @brief A tap on the value of row @p index: the keypad of its number,
 *        over the list and the strip.
 * @param index The row.
 */
static void open_keypad(int index)
{
    const setting_id_t id = row_setting(index);
    ui_setting_keypad_open(&s.kp, KP_AREA, id);
    s.kp_id = id;
    settings_screen_invalidate();
}

/* No press in progress, and no repeat running for one. */
static void drop_press(void)
{
    s.have_press = false;
    s.hit_kind  = HIT_NONE;
    s.hit_index = -1;
    s.dragging  = false;
    s.held_for  = 0.0f;
    s.repeating = false;
}

/**
 * @brief The screen's state cleared: the tiles, PANEL's list kept for the
 *        first tap, no press, the keyboard and the keypad closed.
 */
static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.cat = SET_CAT_APP;
    s.hit_index = -1;
    s.tk.pressed = -1;
    s.kp.pressed = -1;
    sensors_pane_select(SENS_ENTRY_BUS);
    stg_clamp_scroll();
}

/**
 * @brief SETUP shown: it opens on its tiles, from the menu and from a
 *        screen a tile opened alike.  The list keeps its category and its
 *        scroll; the keyboard and the keypad are closed.
 */
static void enter(void)
{
    s.list = false;
    s.opened = false;
    s.warn = false;
    ui_textkey_close(&s.tk);
    ui_keypad_close(&s.kp);
    drop_press();
    stg_clamp_scroll();
    settings_screen_invalidate();
}

/**
 * @brief SETUP left: the warning, the keyboard and the keypad close, and
 *        what was typed is not taken.
 */
static void leave(void)
{
    /*
     * Nothing is written here.  The SAVE button asks, and the application
     * takes it when the bench is idle.  A request outlives this screen on
     * purpose: the operator has said what they want kept.
     *
     * The warning closes with any way out.  A route the application takes
     * while it shows drops the way out it held, and the values stay as
     * they are, unsaved.
     */
    s.warn = false;
    ui_textkey_close(&s.tk);     /* what was typed is not taken */
    ui_keypad_close(&s.kp);
    drop_press();
}

/**
 * @brief The home tag: on a list it goes back to the tiles; on the tiles, or
 *        on a list another screen opened (settings_screen_open_on()), it
 *        leaves, through hold().  Over the warning it is STAY, over the
 *        keyboard and the keypad CANCEL.
 * @return True when it closed something or climbed to the tiles; false to
 *         leave SETUP.
 */
static bool back(void)
{
    drop_press();
    if (s.kp.open) {
        close_keypad();
        return true;
    }
    if (s.tk.open) {
        ui_textkey_close(&s.tk);
        settings_screen_invalidate();
        return true;
    }
    if (s.warn) {
        s.warn = false;
        settings_screen_invalidate();
        return true;
    }
    if (!s.list || s.opened) {
        return false;
    }
    s.list = false;
    settings_screen_invalidate();
    return true;
}

/**
 * @brief A frame: the save button's inputs followed, a keypad whose row is
 *        held since it opened closed, and a held "-" or "+" repeated.
 * @param dt_s The frame's time, s.
 */
static void tick(float dt_s)
{
    if (s.drawn_dirty != settings_dirty()
        || s.drawn_asked != settings_save_asked()
        || s.drawn_failed != settings_save_failed()
        || ((s.warn || s.list) && s.drawn_armed != stg_bench_armed())) {
        settings_screen_invalidate();
    }
    /* A row held or derived since its keypad opened takes no value: the
     * keypad goes, as the row's keys have gone. */
    if (s.kp.open && !ui_setting_keypad_offered(s.kp_id)) {
        close_keypad();
    }

    if (s.hit_kind != HIT_MINUS && s.hit_kind != HIT_PLUS) {
        s.held_for = 0.0f;
        s.repeating = false;
        return;
    }
    float before = s.held_for;
    /* A stalled frame is worth UI_HOLD_MAX_CREDIT_S (0.25s) and no more,
     * so a 1s stall steps at most 3 times at 8Hz and 8 times at 30Hz. */
    s.held_for += ui_hold_credit(dt_s);
    if (s.held_for < REPEAT_DELAY_S) {
        return;
    }
    float hz = (s.held_for > REPEAT_DELAY_S + REPEAT_FAST_S) ? REPEAT_FAST_HZ
                                                             : REPEAT_SLOW_HZ;
    /* One step as the delay ends, then one on each crossing of the repeat
     * period, so the rate is honest even if a frame runs long. */
    int fired_before = (before < REPEAT_DELAY_S)
                           ? 0 : 1 + (int)((before - REPEAT_DELAY_S) * hz);
    int fired_now = 1 + (int)((s.held_for - REPEAT_DELAY_S) * hz);
    for (int i = fired_before; i < fired_now; ++i) {
        step_row(s.hit_index, (s.hit_kind == HIT_PLUS) ? 1 : -1);
        s.repeating = true;
    }
}

/**
 * @brief A category's list shown, at the scroll SENSORS' entry had or at its
 *        top.
 * @param cat The category.
 */
static void open_list(setting_cat_t cat)
{
    if (s.cat != cat) {
        if (s.cat == SET_CAT_SENSORS) {
            s.entry_scroll[sensors_pane_selected()] = s.scroll;
        }
        s.cat = cat;
        s.scroll = (s.cat == SET_CAT_SENSORS)
                       ? s.entry_scroll[sensors_pane_selected()] : 0;
    }
    s.list = true;
    stg_clamp_scroll();
    settings_screen_invalidate();
}

/* A tap on tile @p index: a category's list, or a screen of its own. */
static void open_tile(int index)
{
    const hub_tile_t *t = stg_hub_tile(index);
    if (t->cat < 0) {
        ui_router_goto(t->screen);
        return;
    }
    open_list((setting_cat_t)t->cat);
}

/**
 * @brief A tap on an entry of SENSORS' left list: its rows, at the scroll the
 *        entry had.  What is unsaved stays unsaved and stays in the warning.
 * @param entry The entry.
 */
static void open_entry(int entry)
{
    s.entry_scroll[sensors_pane_selected()] = s.scroll;
    sensors_pane_select(entry);
    s.scroll = s.entry_scroll[sensors_pane_selected()];
    stg_clamp_scroll();
    settings_screen_invalidate();
}

void settings_screen_open_on(setting_cat_t cat, int entry)
{
    open_list(cat);
    if (cat == SET_CAT_SENSORS) {
        open_entry(entry);
    }
    s.opened = true;
}

/**
 * @brief The entry of SENSORS' left list under a point.
 * @param x The point's x, in the screen's coordinates.
 * @param y Its y.
 * @return The entry; -1 off every entry and on any other list.
 */
static int entry_at(int x, int y)
{
    if (!s.list || s.cat != SET_CAT_SENSORS) {
        return -1;
    }
    for (int e = 0; e < SENS_ENTRY_COUNT; ++e) {
        if (gfx_rect_contains(settings_screen_entry_rect(e), x, y)) {
            return e;
        }
    }
    return -1;
}

/**
 * @brief Whether a contact pressed on entry @p entry still is a tap on it:
 *        on the entry and no further than DRAG_SLOP from the press.
 * @param entry The entry.
 * @param x The contact's x, in the screen's coordinates.
 * @param y Its y.
 * @return True where its release selects the entry.
 */
static bool still_on_entry(int entry, int x, int y)
{
    int dx = x - s.press_x, dy = y - s.press_y;
    if (dx < 0) { dx = -dx; }
    if (dy < 0) { dy = -dy; }
    return dx <= DRAG_SLOP && dy <= DRAG_SLOP && entry_at(x, y) == entry;
}

/* A tap on the row of string @p id: its keyboard, on the text in force.
 * The log folders take the folder mode (ui_textkey.h). */
static void open_text(setting_text_id_t id)
{
    if (id < 0 || id >= SETTING_TEXT_COUNT) {
        return;
    }
    const gfx_rect_t area = gfx_rect_make(WARN_X, WARN_Y, WARN_W, TK_AREA_H);
    ui_textkey_open_folder(&s.tk, area, ui_tr(k_text_ui[id].label),
                           settings_text(id));
    s.tk_text = id;
    settings_screen_invalidate();
}

/**
 * @brief The DOWN of the contact that holds the screen: what it presses.
 * @param evt The DOWN.
 * @param x Its x, in the screen's coordinates.
 * @param y Its y.
 */
static void press(const touch_event_t *evt, int x, int y)
{
    /* A DOWN the tracker made for a jump arms nothing that throws values
     * away, as it arms no key: a flick down the list arrives as one, and its
     * lift is a tap.  SAVE, STAY and a tile take it. */
    const bool jump = (evt->flags & TOUCH_FLAG_JUMP) != 0u;
    if (s.warn) {
        /* The warning covers the screen: its three buttons and nothing
         * under them.  DISCARD presses only while it is offered. */
        s.hit_kind = gfx_rect_contains(stg_stay_rect(), x, y)   ? HIT_STAY
                     : gfx_rect_contains(stg_save_rect(), x, y) ? HIT_LEAVE_SAVE
                     : (gfx_rect_contains(stg_discard_rect(), x, y)
                        && !stg_bench_armed() && !jump)         ? HIT_DISCARD
                                                            : HIT_NONE;
        if (s.hit_kind != HIT_NONE) {
            settings_screen_invalidate();
        }
        return;
    }
    if (!s.list) {
        const int tile = tile_at(x, y);
        if (tile >= 0) {
            s.hit_kind = HIT_TILE;
            s.hit_index = tile;
            settings_screen_invalidate();
            return;
        }
    } else if (!jump && gfx_rect_contains(stg_reset_rect(), x, y)) {
        s.hit_kind = HIT_RESET;
        settings_screen_invalidate();
        return;
    }
    /*
     * Only while it is offered.  With nothing to write, a button that
     * presses says a save happened; with a write already asked for, it
     * lights a button the render draws as inert.
     */
    if (stg_save_offered() && gfx_rect_contains(stg_save_rect(), x, y)) {
        s.hit_kind = HIT_SAVE;
        settings_screen_invalidate();
        return;
    }

    /* An entry arms here and selects on its release, as a row's key. */
    const int entry = jump ? -1 : entry_at(x, y);
    if (entry >= 0) {
        s.hit_kind = HIT_ENTRY;
        s.hit_index = entry;
        settings_screen_invalidate();
        return;
    }

    const int row = s.list ? row_at(x, y) : -1;
    if (row >= 0) {
        /* A key is armed here and steps nothing: its release steps it, or
         * the repeat while it is held. */
        s.hit_index = row;
        s.hit_kind = jump                          ? HIT_LIST
                     : row_text(row) >= 0           ? HIT_TEXT
                     : on_key(HIT_MINUS, row, x, y) ? HIT_MINUS
                     : on_key(HIT_PLUS, row, x, y)  ? HIT_PLUS
                     : on_value(row, x, y)          ? HIT_VALUE
                                                    : HIT_LIST;
        if (s.hit_kind != HIT_LIST) {
            settings_screen_invalidate();   /* the key draws pressed */
        }
        return;
    }
    s.hit_kind = HIT_NONE;
}

/**
 * @brief The UP of a tap the finger made: what it activates.
 * @param kind The HIT_ kind the press took.
 * @param index The tile or row it took.
 * @param repeated Whether a held key has repeated, which adds no step.
 * @param x The UP's x, in the screen's coordinates.
 * @param y Its y.
 */
static void release(int kind, int index, bool repeated, int x, int y)
{
    if (kind == HIT_MINUS || kind == HIT_PLUS) {
        /* One step for a tap.  A key that has repeated has stepped on the
         * timer and its release adds none.  A MOVE has given the key up
         * past the slop or outside it; the release is asked again because
         * it can be the first event to carry the point. */
        int dx = x - s.press_x, dy = y - s.press_y;
        if (dx < 0) { dx = -dx; }
        if (dy < 0) { dy = -dy; }
        if (!repeated && dx <= DRAG_SLOP && dy <= DRAG_SLOP
            && on_key(kind, index, x, y)) {
            step_row(index, (kind == HIT_PLUS) ? 1 : -1);
        }
    } else if (kind == HIT_VALUE && s.list && on_value(index, x, y)) {
        open_keypad(index);
    } else if (kind == HIT_TEXT && s.list && row_at(x, y) == index) {
        const int text = row_text(index);
        if (text >= 0) {
            open_text((setting_text_id_t)text);
        }
    } else if (kind == HIT_TILE && !s.list
               && gfx_rect_contains(stg_tile_rect(index), x, y)) {
        open_tile(index);
    } else if (kind == HIT_ENTRY && still_on_entry(index, x, y)) {
        open_entry(index);
    } else if (kind == HIT_RESET && s.list
               && gfx_rect_contains(stg_reset_rect(), x, y)) {
        if (s.cat == SET_CAT_SENSORS) {
            sensors_pane_reset(sensors_pane_selected());
        } else {
            settings_reset(s.cat);
        }
        settings_apply_ui();
    } else if (kind == HIT_SAVE && stg_save_offered()
               && gfx_rect_contains(stg_save_rect(), x, y)) {
        /* Asked for, not written.  The application decides when the bench
         * can afford to stop for it. */
        settings_request_save();
        settings_screen_invalidate();
    } else if (kind == HIT_STAY && s.warn
               && gfx_rect_contains(stg_stay_rect(), x, y)) {
        /* The tiles again; the list keeps its category and its scroll. */
        s.warn = false;
        settings_screen_invalidate();
    } else if (kind == HIT_LEAVE_SAVE && s.warn
               && gfx_rect_contains(stg_save_rect(), x, y)) {
        /* The SAVE button's request, then the way out.  leave() closes
         * the warning. */
        settings_request_save();
        ui_router_goto(s.warn_to);
    } else if (kind == HIT_DISCARD && s.warn && !stg_bench_armed()
               && gfx_rect_contains(stg_discard_rect(), x, y)) {
        /*
         * Every changed value back to its stored one, each by the path an
         * edit by hand takes: the observer, which sends a pin or a port to
         * the IOmcu, and settings_apply_ui() for the PANEL
         * rows.  Never while ARMED, asked again at the release: a bench
         * that armed while the finger was down keeps its values.  After a
         * refused save settings_discard() also asks for a write.
         */
        (void)settings_discard();
        settings_apply_ui();
        ui_router_goto(s.warn_to);
    }
}

/* An event while the keyboard is open goes to the keyboard alone.  OK
 * sets the string, as an edit by hand does; SAVE keeps it, as it keeps a
 * value. */
static void textkey_event(const touch_event_t *evt)
{
    char text[SETTINGS_TEXT_MAX];
    if (ui_textkey_event(&s.tk, evt, text, sizeof(text)) == UI_TEXTKEY_OK) {
        settings_set_text(s.tk_text, text);
    }
    if (s.tk.revision != s.drawn_tk_rev || s.tk.open != s.drawn_tk) {
        settings_screen_invalidate();
    }
}

/**
 * @brief An event while the keypad is open goes to the keypad alone.  OK
 *        sets the value through settings_set(), as a step does, and SAVE
 *        keeps it; a value the steps never reach keeps the keypad open,
 *        refused (ui_setting_keypad_ok()).
 * @param evt The event.
 */
static void keypad_event(const touch_event_t *evt)
{
    float v = 0.0f;
    if (ui_keypad_event(&s.kp, evt, &v) == UI_KEYPAD_OK
        && ui_setting_keypad_ok(&s.kp, s.kp_id, v) == UI_SETKEY_SET) {
        value_changed(s.kp_id);
    }
    if (s.kp.revision != s.drawn_kp_rev || s.kp.open != s.drawn_kp) {
        settings_screen_invalidate();
    }
}

/**
 * @brief One touch event: to the keypad or the keyboard while one is open,
 *        else to the press, the scroll and the release it ends in.
 * @param evt The event, in the screen's coordinates.
 */
static void event(const touch_event_t *evt)
{
    int x = evt->point.x;
    int y = evt->point.y;

    if (s.kp.open) {
        keypad_event(evt);
        return;
    }
    if (s.tk.open) {
        textkey_event(evt);
        return;
    }

    if (evt->type == TOUCH_EVENT_DOWN) {
        /* A DOWN with the id of the press held says that contact has gone
         * and its release with it: the press is this one's. */
        if (s.have_press && evt->point.id != s.press_id) {
            return;
        }
    } else if (!s.have_press || evt->point.id != s.press_id) {
        return;
    }

    switch (evt->type) {
    case TOUCH_EVENT_DOWN:
        s.have_press = true;
        s.press_id = evt->point.id;
        s.press_x = x;
        s.press_y = y;
        s.last_y = y;
        s.dragging = false;
        s.repeating = false;
        s.held_for = 0.0f;
        press(evt, x, y);
        break;

    case TOUCH_EVENT_MOVE: {
        int dy = y - s.last_y;
        s.last_y = y;
        if (s.hit_kind == HIT_NONE) {
            break;
        }
        if (s.hit_kind == HIT_ENTRY) {
            /* Slid off: the press ends and this contact selects nothing and
             * scrolls nothing. */
            if (!still_on_entry(s.hit_index, x, y)) {
                s.hit_kind = HIT_NONE;
                settings_screen_invalidate();
            }
            break;
        }
        int moved = y - s.press_y;
        if (moved < 0) { moved = -moved; }
        if (!s.dragging && moved > DRAG_SLOP &&
            (s.hit_kind == HIT_LIST || s.hit_kind == HIT_MINUS ||
             s.hit_kind == HIT_PLUS || s.hit_kind == HIT_TEXT ||
             s.hit_kind == HIT_VALUE)) {
            /* Turned into a scroll: give up the button. */
            s.dragging = true;
            s.hit_kind = HIT_LIST;
            settings_screen_invalidate();
        }
        int side = x - s.press_x;
        if (side < 0) { side = -side; }
        if ((s.hit_kind == HIT_MINUS || s.hit_kind == HIT_PLUS)
            && (side > DRAG_SLOP
                || !on_key(s.hit_kind, s.hit_index, x, y))) {
            /* Off the key sideways, or out of it within the slop: the
             * press on the key ends, and the repeat with it.  The contact
             * can still scroll. */
            s.hit_kind = HIT_LIST;
            settings_screen_invalidate();
        }
        if (s.dragging) {
            s.scroll -= dy;
            stg_clamp_scroll();
            settings_screen_invalidate();
        }
        break;
    }

    case TOUCH_EVENT_UP:
        /* A contact that scrolled activates nothing, and neither does a
         * release the finger did not make: it ends the press.  The press
         * is dropped before it acts, so what it opens starts clean. */
        if (!s.dragging && touch_event_is_tap_up(evt)) {
            const int kind = s.hit_kind;
            const int index = s.hit_index;
            const bool repeated = s.repeating;
            drop_press();
            settings_screen_invalidate();
            release(kind, index, repeated, x, y);
        } else {
            drop_press();
            settings_screen_invalidate();
        }
        break;
    }
}

/**
 * @brief Touch events were lost between two frames, so this screen's record
 *        of what is on the glass cannot be trusted.
 *
 * A hit held open acts on its release and a drag owns its track id, and the
 * GT911 reuses ids: a later contact that began somewhere else would be taken
 * for this one and press a control nobody touched.  The repeat on the plus
 * and minus keys runs on the frame timer while a hit stands, so it stops
 * here too.  A key held on the keyboard or the keypad is let go; what was
 * typed stays.
 */
static void cancel(void)
{
    drop_press();
    ui_textkey_cancel_press(&s.tk);
    ui_keypad_cancel_press(&s.kp);
    settings_screen_invalidate();
}

static const ui_screen_t s_screen = {
    .title = "SETUP",
    .reset = reset,
    .enter = enter,
    .leave = leave,
    .tick = tick,
    .event = event,
    .cancel = cancel,
    .render = stg_render,
    .back = back,
    .hold = hold,
};

const ui_screen_t *settings_screen(void)
{
    return &s_screen;
}

int settings_screen_scroll(int *max)
{
    if (max != NULL) {
        *max = s.scroll_max;
    }
    return s.scroll;
}

const ui_textkey_t *settings_screen_textkey(void)
{
    return &s.tk;
}

const ui_keypad_t *settings_screen_keypad(void)
{
    return &s.kp;
}

int settings_screen_keypad_setting(void)
{
    return s.kp.open ? (int)s.kp_id : -1;
}
