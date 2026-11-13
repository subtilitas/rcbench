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
#include "supply_screen.h"
#include "settings_rows_int.h"
#include "settings_sensors.h"
#include "tone_link.h"
#include "ui_board.h"
#include "ui_icons.h"
#include "ui_keypad.h"
#include "ui_pinlist.h"
#include "ui_setlist.h"
#include "ui_setting_keypad.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

#include "settings_screen_int.h"

/* The screen's state; settings_screen_int.h names it s. */
stg_state_t stg_s;


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

void settings_apply_all(void)
{
    settings_apply_ui();
    supply_screen_limits_changed();
}

void settings_screen_set_unheld(uint16_t sense_rows, uint8_t tone_rows)
{
    if (sense_rows != s.sense_unheld || tone_rows != s.tone_unheld) {
        s.sense_unheld = sense_rows;
        s.tone_unheld  = tone_rows;
        settings_screen_invalidate();
    }
}

const char *settings_screen_help(setting_id_t id)
{
    if (settings_fixed(id) && !settings_screen_unheld(id)) {
        return TR(SET_PIN_SOLDERED);
    }
    if (id == SET_INA228_UOHM || id == SET_INA228_MAX_A) {
        /* What works with the other row, in place of the general help and of
         * the general mark's line when this pair is why it is refused. */
        if (!settings_screen_unheld(id) || sensors_rows_i228_over()) {
            return sensors_rows_i228_line(id);
        }
        return TR(SET_UNHELD);
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
    return (s.sense_unheld & sense) != 0u || (s.tone_unheld & tone) != 0u;
}

static const ui_setlist_host_t k_host;

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
    return s.sl.open ? (int)s.cat : -1;
}

int stg_list_x(void)
{
    return (s.sl.open && s.cat == SET_CAT_SENSORS) ? PANE_X : HUB_M;
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

/** @brief Finds the hub tile at a point. */
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
    return ui_setlist_strip_rect(col);
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
    return ui_setlist_save_offered();
}

/* DISCARD changes nothing while the bench is ARMED: see the list's
 * release (ui_setlist.c). */
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

bool settings_screen_warning(void)
{
    return s.sl.warn;
}

bool settings_screen_warn_line(int i, char *line, size_t n)
{
    return ui_setlist_warn_line(&s.sl, i, line, n);
}

_Static_assert(SETTINGS_WARN_LINES == UI_SL_WARN_LINES,
               "SETUP's warning is the list's");

/**
 * @brief The home tag leaves for @p to: held while something is unsaved and no
 * save is asked for.  A save asked for is the operator saying keep, and
 * holds nothing; a save refused (NOT SAVED) leaves the values dirty and
 * holds it.
 */
static bool hold(ui_screen_id_t to)
{
    s.warn_to = to;
    return ui_setlist_warn(&s.sl);
}

int stg_row_count(void)
{
    return settings_screen_rows(s.cat);
}

void stg_clamp_scroll(void)
{
    ui_setlist_clamp(&s.sl);
}

/** Screen-space rect of a row, or an empty rect when it is scrolled away. */
gfx_rect_t stg_row_rect(int index)
{
    return ui_setlist_row_rect(&s.sl, index);
}

gfx_rect_t settings_screen_list_rect(void)
{
    return ui_setlist_pane(&s.sl);
}

gfx_rect_t settings_screen_row_rect(int row)
{
    return stg_row_rect(row);
}

gfx_rect_t settings_screen_row_part(int row, settings_row_part_t part)
{
    switch (part) {
    case SETTINGS_ROW_MINUS:
        return ui_setlist_row_part(&s.sl, row, UI_SL_PART_MINUS);
    case SETTINGS_ROW_VALUE:
        return ui_setlist_row_part(&s.sl, row, UI_SL_PART_VALUE);
    case SETTINGS_ROW_PLUS:
    default:
        return ui_setlist_row_part(&s.sl, row, UI_SL_PART_PLUS);
    }
}

/* The entry of row @p index; a string's carries text >= 0. */
settings_row_t stg_row_entry(int index)
{
    return settings_rows_entry(s.cat, index);
}

/* ------------------------------------------------------- the list's host */

/** @brief The rows: the open category's. */
static int host_count(void)
{
    return stg_row_count();
}

/**
 * @brief Row @p i of the open category: a setting, or a string.
 * @param i The row.
 * @return The row.
 */
static ui_setlist_row_t host_row(int i)
{
    const settings_row_t e = stg_row_entry(i);
    if (e.text >= 0) {
        return (ui_setlist_row_t){ (uint8_t)UI_SL_TEXT, e.text,
                                   (setting_id_t)0, TX_COUNT, NULL };
    }
    if (i < 0 || i >= stg_row_count()) {
        return ui_setlist_table_row(NULL, 0, 0);
    }
    return (ui_setlist_row_t)UI_SL_ROW_SETTING(e.id);
}

/** @brief The strip's name: the category's. */
static const char *host_name(void)
{
    return ui_setting_category(s.cat);
}

/**
 * @brief RESET names what it resets on SENSORS: the selected entry alone.
 * @param out Takes the label.
 * @param n   Bytes at @p out.
 */
static void host_reset_label(char *out, size_t n)
{
    char entry[24];
    if (s.cat == SET_CAT_SENSORS) {
        snprintf(out, n, TR(SET_RESET_ONE),
                 sensors_pane_name(sensors_pane_selected(), entry,
                                   sizeof(entry)));
    } else {
        snprintf(out, n, "%s", TR(SET_RESET));
    }
}

/** @brief RESET: the category, or SENSORS' selected entry. */
static void host_reset(void)
{
    if (s.cat == SET_CAT_SENSORS) {
        sensors_pane_reset(sensors_pane_selected());
    } else {
        settings_reset(s.cat);
    }
    settings_apply_all();
}

/**
 * @brief A setting changed by a step or by the keypad: the PANEL rows reach
 *        the interface at once.  After RESET and DISCARD every row may have.
 * @param id The setting that changed; SETTING_COUNT for several.
 */
static void host_changed(setting_id_t id)
{
    if ((int)id == SETTING_COUNT) {
        settings_apply_all();
        return;
    }
    const setting_def_t *d = settings_def(id);
    if (d != NULL && d->cat == SET_CAT_APP) {
        settings_apply_ui();      /* language, theme, brightness, contrast */
    }
}

/** @brief The warning's SAVE and DISCARD: the way out it held. */
static void host_way_out(void)
{
    ui_router_goto(s.warn_to);
}

/**
 * @brief A tap on a tile: a category's list, or a screen of its own.
 * @param index The tile.
 */
static void open_tile(int index);

/**
 * @brief A tap on an entry of SENSORS' left list.
 * @param entry The entry.
 */
static void open_entry(int entry);

/**
 * @brief The entry of SENSORS' left list under a point.
 * @param x The point's x, in the screen's coordinates.
 * @param y Its y.
 * @return The entry; -1 off every entry and on any other list.
 */
static int entry_at(int x, int y)
{
    if (!s.sl.open || s.cat != SET_CAT_SENSORS) {
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
    int dx = x - s.sl.press_x, dy = y - s.sl.press_y;
    if (dx < 0) { dx = -dx; }
    if (dy < 0) { dy = -dy; }
    return dx <= DRAG_SLOP && dy <= DRAG_SLOP && entry_at(x, y) == entry;
}

/**
 * @brief A DOWN on SETUP's own: a tile on the hub, an entry of SENSORS'
 *        left list.  An entry arms here and selects on its release, as a
 *        row's key; a DOWN the tracker made for a jump arms none.
 */
static int host_press(int x, int y, bool jump, int *index)
{
    if (!s.sl.open) {
        const int tile = tile_at(x, y);
        if (tile >= 0) {
            *index = tile;
            return HIT_TILE;
        }
        return 0;
    }
    const int entry = jump ? -1 : entry_at(x, y);
    if (entry >= 0) {
        *index = entry;
        return HIT_ENTRY;
    }
    return 0;
}

/** @brief A MOVE: an entry's press ends where the contact slides off it. */
static bool host_still(int kind, int index, int x, int y)
{
    return kind != HIT_ENTRY || still_on_entry(index, x, y);
}

/** @brief The tap UP of a tile's or an entry's press. */
static void host_release(int kind, int index, int x, int y)
{
    if (kind == HIT_TILE && !s.sl.open
        && gfx_rect_contains(stg_tile_rect(index), x, y)) {
        open_tile(index);
    } else if (kind == HIT_ENTRY && still_on_entry(index, x, y)) {
        open_entry(index);
    }
}

/** @brief SETUP's side of its list. */
static const ui_setlist_host_t k_host = {
    .count       = host_count,
    .row         = host_row,
    .headings    = false,
    .left        = stg_list_x,
    .name        = host_name,
    .reset_label = host_reset_label,
    .reset       = host_reset,
    .help        = settings_screen_help,
    .unheld      = settings_screen_unheld,
    .changed     = host_changed,
    .invalidate  = settings_screen_invalidate,
    .way_out     = host_way_out,
    .press       = host_press,
    .still       = host_still,
    .release     = host_release,
};

/* ------------------------------------------------------------------- input */

/**
 * @brief The screen's state cleared: the tiles, PANEL's list kept for the
 *        first tap, no press, the keyboard and the keypad closed.
 */
static void reset(void)
{
    const uint16_t sense_unheld = s.sense_unheld;
    const uint8_t tone_unheld = s.tone_unheld;

    memset(&s, 0, sizeof(s));
    s.sense_unheld = sense_unheld;
    s.tone_unheld = tone_unheld;
    ui_setlist_init(&s.sl, &k_host);
    s.cat = SET_CAT_APP;
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
    ui_setlist_drop(&s.sl);
    s.sl.open = false;
    s.opened = false;
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
    ui_setlist_drop(&s.sl);
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
    if (ui_setlist_back(&s.sl)) {
        return true;
    }
    if (!s.sl.open || s.opened) {
        return false;
    }
    s.sl.open = false;
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
    ui_setlist_tick(&s.sl, dt_s);
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
            s.entry_scroll[sensors_pane_selected()] = s.sl.scroll;
        }
        s.cat = cat;
        s.sl.scroll = (s.cat == SET_CAT_SENSORS)
                          ? s.entry_scroll[sensors_pane_selected()] : 0;
    }
    s.sl.open = true;
    stg_clamp_scroll();
    settings_screen_invalidate();
}

static void open_tile(int index)
{
    const hub_tile_t *t = stg_hub_tile(index);
    if (t->cat < 0) {
        ui_router_goto(t->screen);
        return;
    }
    open_list((setting_cat_t)t->cat);
}

/*
 * A tap on an entry of SENSORS' left list: its rows, at the scroll the
 * entry had.  What is unsaved stays unsaved and stays in the warning.
 */
static void open_entry(int entry)
{
    s.entry_scroll[sensors_pane_selected()] = s.sl.scroll;
    sensors_pane_select(entry);
    s.sl.scroll = s.entry_scroll[sensors_pane_selected()];
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
 * @brief One touch event: to the list (ui_setlist.h), which hands a tile's
 *        and an entry's press back through the host's hooks.
 * @param evt The event, in the screen's coordinates.
 */
static void event(const touch_event_t *evt)
{
    ui_setlist_event(&s.sl, evt);
}

/**
 * @brief Touch events were lost between two frames, so this screen's record
 *        of what is on the glass cannot be trusted: the press drops, a key
 *        held on the keyboard or the keypad is let go (ui_setlist_cancel()).
 */
static void cancel(void)
{
    ui_setlist_cancel(&s.sl);
}

/**
 * @brief ui_screen_t's covered: a keypad, a keyboard or the leave warning.
 * @return true while one is up
 */
static bool covered(void) { return ui_setlist_covered(&s.sl); }

/** @brief The screen's table. */
static const ui_screen_t s_screen = {
    .covered = covered,
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
    .help_count = stg_help_count,
    .help_page = stg_help_page,
};

const ui_screen_t *settings_screen(void)
{
    return &s_screen;
}

int settings_screen_scroll(int *max)
{
    if (max != NULL) {
        *max = s.sl.scroll_max;
    }
    return s.sl.scroll;
}

const ui_textkey_t *settings_screen_textkey(void)
{
    return &s.sl.tk;
}

const ui_keypad_t *settings_screen_keypad(void)
{
    return &s.sl.kp;
}

int settings_screen_keypad_setting(void)
{
    return ui_setlist_keypad_setting(&s.sl);
}
