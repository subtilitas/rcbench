/**
 * @file settings_screen_int.h
 * @brief SETUP's private seam between settings_screen.c, the state, the touch
 *        handling and the screen's interface, and settings_draw.c, its
 *        drawing: the layout both work on, the hub's tiles, the state and the
 *        functions one calls in the other.  Nothing outside shared/ui includes
 *        this header; the screen's interface is settings_screen.h.
 *
 * Included last: its layout macros have short names (ROW_H, BTN_W) and it
 * names the state s, which no header included after it may meet.  The
 * functions one file calls in the other carry the prefix stg_.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "sensors_pane.h"
#include "settings.h"
#include "settings_rows_int.h"
#include "settings_screen.h"
#include "ui_icons.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"

/** @brief The screen's width, UI_PANEL_W px. */
#define SCREEN_W UI_PANEL_W
/* The router owns the top UI_BAND_H px and the home tag; this screen gets
 * the body. */
/** @brief The screen's height, UI_BODY_H px: the body under the band. */
#define SCREEN_H UI_BODY_H
/** @brief Frame buffers the router renders into. */
#define MAX_FBS  3

/**
 * @brief The hub: three columns by three rows of 328 x 130px tiles
 *        (49.2 x 19.5mm) with 10px margins, and under them the strip
 *        that holds SAVE.
 *
 * Coordinates are in the 540px body the router hands this screen; the
 * band and the home tag are above it.  The list has the same strip, so
 * SAVE stays where it is between the two.  The strip ends 4px above the
 * router's alert band, which covers the bottom UI_ALERT_H px of the body:
 * nothing that acts sits under it.
 */
#define HUB_COLS 3 /**< the hub's columns */
#define HUB_ROWS 3 /**< the hub's rows */
#define HUB_M 10 /**< the margin around and between tiles, px */
#define HUB_TW ((SCREEN_W - (HUB_COLS + 1) * HUB_M) / HUB_COLS) /**< a tile's width, px: 328 */
#define HUB_TH 130 /**< a tile's height, px */

/**
 * @brief Inside a hub tile, from its top: the 36px icon box at 14, the
 *        name in the body face (12 x 24) at 60, the line in the label face
 *        (10 x 20) at 94, which leaves 16px under the line.  A line holds
 *        32 characters on one row; the tile has no room for a second.
 */
#define HUB_ICON_DY 14 /**< the icon box's top, px */
#define HUB_ICON 36 /**< the icon box's side, px */
#define HUB_NAME_DY 60 /**< the name's top, px */
#define HUB_LINE_DY 94 /**< the line's top, px */

/** @brief The faces of the list and the strip: a row's name and a
 *         heading in the head face (20 x 36); a value and the warning's
 *         sentences in the body face (12 x 24); help lines, units, keys,
 *         buttons and the warning's change lines in the label face
 *         (10 x 20). */
#define STG_F_HEAD (&gfx_font_20x36) /**< names and headings: 20 x 36 */
#define STG_F_BODY (&gfx_font_12x24) /**< values and sentences: 12 x 24 */
#define STG_F_LABEL (&gfx_font_10x20) /**< help, units, keys, buttons, change lines: 10 x 20 */

/** @brief The strip under the grid and under the list: three columns,
 *         the hub's, UI_BTN_H (48px) tall. */
#define STRIP_H UI_BTN_H /**< the strip's height, px */
#define STRIP_Y (SCREEN_H - UI_ALERT_H - 4 - STRIP_H) /**< the strip's top, px: 444 */

/**
 * @brief The list: from the first column's left edge to the scroll bar,
 *        from under the band to 8px above the strip.  A row is 64px
 *        tall at a 70px pitch: 6 rows show whole.  The scroll bar is
 *        SCROLL_W px wide at the third column's right edge and takes no
 *        touch.
 */
#define SCROLL_W 12 /**< the scroll bar's width, px */
#define SCROLL_X (SCREEN_W - HUB_M - SCROLL_W) /**< the scroll bar's left edge, px: 1002 */
#define LIST_R (SCROLL_X - 8) /**< the list's right edge, px: 994 */
#define LIST_X stg_list_x() /**< the list's left edge, px: 10, on SENSORS 240 */
#define LIST_W (LIST_R - LIST_X) /**< the list's width, px: 984, on SENSORS 754 */
#define LIST_Y 8 /**< the list's top, px */
#define LIST_H (STRIP_Y - 8 - LIST_Y) /**< the list's height, px: 428 */
#define ROW_H 64 /**< a row's height, px */
#define ROW_GAP 6 /**< the gap between two rows, px */
#define ROW_PITCH (ROW_H + ROW_GAP) /**< a row's pitch, px: 70 */
#define ROW_NAME_DY 3 /**< the name's top, px */
#define ROW_HELP_DY 40 /**< the help line's top, px */

/**
 * @brief SENSORS' left list: 8 entries of 220 x 50px at a 54px pitch, down the
 *        list's 428px.  The name in the body face at 3px, the state in the
 *        8 x 16 face at 29px: 25 cells end short of the entry's edge.  The list
 *        pane starts 10px right of it.
 */
#define ENTRY_W 220 /**< an entry's width, px */
#define ENTRY_H 50 /**< an entry's height, px */
#define ENTRY_PITCH 54 /**< an entry's pitch, px */
#define ENTRY_NAME_DY 3 /**< the name's top, px */
#define ENTRY_STATE_DY 29 /**< the state's top, px */
#define PANE_X (HUB_M + ENTRY_W + HUB_M) /**< the pane's left edge, px: 240 */

/**
 * @brief A row's "-" and "+" keys: 56 x 48px (8.4 x 7.2mm), the "+" 12px
 *        in from the row's right edge, 166px between them: the value's
 *        150px and 8px on each side.
 */
#define BTN_W 56 /**< a key's width, px */
#define BTN_H UI_TAP_MIN /**< a key's height, px: 48 */
_Static_assert(BTN_H <= ROW_H, "a row's buttons fit in the row");
#define VALUE_FIELD 150 /**< the value's field, px */
#define PLUS_X (LIST_R - 12 - BTN_W) /**< the "+" key's left edge, px: 926 */
#define MINUS_X (PLUS_X - 8 - VALUE_FIELD - 8 - BTN_W) /**< the "-" key's left edge, px: 704 */
#define VALUE_R (PLUS_X - 8) /**< the value's and its unit's right edge, px */
/** @brief The left edge of a number's value, px: the 166px between its
 *         "-" and its "+" key, over the row's height, where a tap opens
 *         the keypad. */
#define VALUE_X  (MINUS_X + BTN_W)
/** @brief The width of a number's value, px: 166. */
#define VALUE_W  (PLUS_X - VALUE_X)

/*
 * A "-" or "+" key is a tap: the press arms it and the release steps it
 * once.  The release counts when the same contact makes it inside the key,
 * no further than DRAG_SLOP from the press in x and in y, with no scroll
 * and no repeat in between.  The list scrolls by a drag that starts
 * anywhere on it, the keys included, so a key that stepped on the press
 * would step under every scroll that starts on one.
 *
 * A key held repeats: a 0 to 30,000mAh range is 300 taps otherwise.  The
 * first step comes REPEAT_DELAY_S after the press, then REPEAT_SLOW_HZ, and
 * REPEAT_FAST_HZ once the key is held REPEAT_FAST_S longer.  The time runs
 * only while the contact stays on the key within the slop, and one frame
 * counts for at most UI_HOLD_MAX_CREDIT_S (0.25s); the release of a key
 * that has repeated adds no step.
 */
#define REPEAT_DELAY_S 0.45f            /**< delay before the first repeat, 0.45s */
#define REPEAT_SLOW_HZ 8.0f             /**< repeat rate after the delay, 8Hz */
/** @brief Time from the first repeat until the fast rate, 1.8s */
#define REPEAT_FAST_S  1.8f
#define REPEAT_FAST_HZ 30.0f            /**< repeat rate once fast, 30Hz */

/* A press that moves further than this is a scroll, not a tap. */
/**
 * @brief A press that moves further than DRAG_SLOP px, 8px, is a scroll and no
 *        tap.
 */
#define DRAG_SLOP 8

enum { HIT_NONE = 0, HIT_TILE, HIT_MINUS, HIT_PLUS, HIT_RESET, HIT_LIST,
       HIT_SAVE, HIT_STAY, HIT_DISCARD, HIT_LEAVE_SAVE, HIT_TEXT,
       HIT_VALUE, HIT_ENTRY };

/**
 * @brief The warning that holds a way out: a card over the body down to
 *        the strip.
 *
 * Its title in the head face, up to two lines in the body face on what
 * unsaved means, then one line per change in the label face and up to two
 * notes in the body face under them.  STAY, DISCARD and SAVE take the
 * strip's three columns, SAVE where SAVE is on the tiles.
 * SETTINGS_WARN_LINES lines of WARN_PITCH px fit; more changes than that
 * leave the last line to their count.  Offsets are from WARN_Y.
 */
#define WARN_X HUB_M /**< the card's left edge, px */
#define WARN_Y HUB_M /**< the card's top, px */
#define WARN_W (SCREEN_W - 2 * HUB_M) /**< the card's width, px: 1004 */
#define WARN_H (STRIP_Y - HUB_M - WARN_Y) /**< the card's height, px: 424 */
#define WARN_TITLE_Y 12 /**< the title's top */
#define WARN_WHY_Y 56 /**< the first line on what unsaved means */
#define WARN_WHY_PITCH 26 /**< those lines' pitch, px */
#define WARN_LINE_Y 116 /**< the first change line */
#define WARN_PITCH 24 /**< the change lines' pitch, px */
#define WARN_NOTE_Y (WARN_LINE_Y + SETTINGS_WARN_LINES * WARN_PITCH + 4) /**< the first note */
#define WARN_NOTE_PITCH 26 /**< the notes' pitch, px */

/*
 * The hub's tiles, in their order on the grid, row by row.  A category's
 * tile names the category and counts its settings; a screen's tile names
 * the screen by its title, English always, as the menu does.  The home tag
 * of a screen a tile opens leads back here (settings_screen_opens()).
 * The hub holds the bench-wide tools only: ESC / BENCH's rows are MOTOR &
 * ESC > SETTINGS', beside the throttle they set.
 *
 * Nine fit the grid.  A tenth needs a second page, through ui_tabs in the
 * strip's free columns, which is not built: the build stops at a tenth
 * entry (the assertion under the table), and so does test_settings_pins_hub.
 */
/**
 * @brief One tile of the hub: a category's list or a screen it opens.
 */
typedef struct {
    ui_icon_fn     icon;                /**< icon the tile draws */
    int8_t         cat;      /**< the category its list shows, or -1      */
    ui_screen_id_t screen;   /**< else the screen it opens                */
    ui_text_id_t   line;     /**< the line under the name                 */
} hub_tile_t;

/**
 * @brief Tile that opens screen @p screen_ with icon @p icon_ and the line @p
 *        line_ under its name.
 */
#define HUB_SCREEN(screen_, line_, icon_) \
    { (icon_), -1, (screen_), (line_) }
/**
 * @brief Tile that opens category @p cat_ with icon @p icon_ and the line
 *        @p line_ under its name.
 */
#define HUB_CATEGORY_LINE(cat_, line_, icon_) \
    { (icon_), (int8_t)(cat_), SCREEN_SETUP, (line_) }

/** @brief The hub's tiles in grid order, row by row. */
static const hub_tile_t k_tiles[] = {
    HUB_CATEGORY_LINE(SET_CAT_APP, TX_SET_HUB_PANEL, ui_icon_sliders),
    HUB_CATEGORY_LINE(SET_CAT_SENSORS, TX_SET_HUB_SENSORS, ui_icon_supply),
    HUB_SCREEN(SCREEN_OUTPUTS, TX_SET_HUB_OUTPUTS, ui_icon_list),
    HUB_CATEGORY_LINE(SET_CAT_CARD, TX_SET_HUB_CARD, ui_icon_chart),
    HUB_SCREEN(SCREEN_UPDATES, TX_SET_HUB_UPDATES, ui_icon_record),
};

/** @brief Tiles of every board; a board that solders its pins adds LAYOUT. */
#define TILE_COUNT ((int)(sizeof(k_tiles) / sizeof(k_tiles[0])))

_Static_assert(TILE_COUNT + 1 <= SETTINGS_HUB_SLOTS,
               "a tenth SETUP tile needs the hub's second page");
_Static_assert(SETTINGS_HUB_SLOTS == HUB_COLS * HUB_ROWS,
               "the hub's slots are its grid");
_Static_assert(HUB_M + HUB_ROWS * (HUB_TH + HUB_M) <= STRIP_Y,
               "the grid ends above the strip");
_Static_assert(LIST_Y + LIST_H <= STRIP_Y, "the list ends above the strip");
_Static_assert(SENS_ENTRY_COUNT * ENTRY_PITCH - (ENTRY_PITCH - ENTRY_H) <= LIST_H,
               "SENSORS' left list ends where the list ends");
_Static_assert(ENTRY_H >= UI_TAP_MIN, "an entry is a tap target");
_Static_assert(STRIP_Y + STRIP_H <= SCREEN_H - UI_ALERT_H,
               "the strip ends above the alert band");
_Static_assert(WARN_WHY_Y + 2 * WARN_WHY_PITCH <= WARN_LINE_Y,
               "the warning's two lines on unsaved end above the changes");
_Static_assert(WARN_NOTE_Y + 2 * WARN_NOTE_PITCH <= WARN_H - 4,
               "the warning's notes end inside its card");
_Static_assert(HUB_ICON_DY + HUB_ICON <= HUB_NAME_DY
               && HUB_NAME_DY + 24 <= HUB_LINE_DY
               && HUB_LINE_DY + 20 <= HUB_TH,
               "a hub tile's icon, name and line stack inside it");

/* The strings' names, for the warning, a row and its keyboard's title,
 * and the help line of a row; the DUT name has no row on SETUP. */
/**
 * @var k_text_ui
 * @brief Per string setting: the name for the warning, a row and its
 *        keyboard's title, and the row's help line.  The DUT name has no
 *        row on SETUP.
 */
static const struct {
    ui_text_id_t label;  /**< the string's name for the warning, a row and its keyboard's title */
    ui_text_id_t help;   /**< the row's help line, TX_COUNT for none */
} k_text_ui[SETTING_TEXT_COUNT] = {
    [SET_TEXT_DUT_NAME]     = { TX_SET_DUT_NAME, TX_COUNT },
    [SET_TEXT_LOG_AUTO_DIR] = { TX_SET_AUTO_DIR, TX_SET_AUTO_DIR_HELP },
    [SET_TEXT_LOG_USER_DIR] = { TX_SET_USER_DIR, TX_SET_USER_DIR_HELP },
};

/*
 * The keyboard a string's row opens: over the list and the strip, the
 * warning's card.  Nothing under it presses while it is open; OK and
 * CANCEL are its own, and the home tag closes it as CANCEL does.
 */
/**
 * @brief Height of the keyboard's area, px: from the warning's top to the
 *        strip's foot.
 */
#define TK_AREA_H (STRIP_Y + STRIP_H - WARN_Y)

/**
 * @brief The keypad a number's value opens: the keyboard's area, 1004 x
 *        482px, its foot 4px above the alert band.  Nothing under it presses while it
 *        is open; OK, CANCEL and the home tag close it as they close the
 *        keyboard.
 */
#define KP_AREA gfx_rect_make(WARN_X, WARN_Y, WARN_W, TK_AREA_H)

/**
 * @brief SETUP's state: the open list and its scroll, the contact that
 *        holds the screen, the key repeat, the warning, the keyboard and
 *        the keypad, and what the last frame drew of each.
 */
typedef struct {
    /* The list of cat is open; the hub shows while this is false. */
    /** @brief Whether the list of cat is open; the hub shows while false */
    bool     list;
    setting_cat_t cat;                  /**< the category the list shows */
    int      scroll;                    /**< list scroll, px */
    /** @brief The scroll each SENSORS entry had when last left, px */
    int      entry_scroll[SENS_ENTRY_COUNT];
    bool     opened;   /**< another screen opened the list: no tiles above it */
    int      scroll_max;                /**< largest scroll, px */

    /* One contact holds the screen from its DOWN to its UP; any other
     * presses nothing and ends nothing. */
    bool     have_press;                /**< whether a contact holds the screen */
    uint8_t  press_id;                  /**< touch id of that contact */

    int      hit_kind;                  /**< what the press hit, HIT_* */
    int      hit_index;                 /**< tile index, or row index */
    bool     dragging;                  /**< whether the press became a scroll */
    int press_x,                            /**< x where the press began, px */
        press_y;                            /**< y where the press began, px */
    int      last_y;                    /**< y of the last move, px */

    float    held_for;                  /**< seconds a key has been held within the slop */
    bool     repeating;                 /**< whether the held key has begun to repeat */

    bool     chrome_valid[MAX_FBS];     /**< per frame buffer: whether its chrome is current */
    bool     drawn_list;                /**< whether the list showed at the last draw */
    setting_cat_t drawn_cat;            /**< category the last draw showed */

    /*
     * The save button's three inputs as the last frame drew them.  The
     * write is taken by settings_save_tick() outside this screen, so a
     * pending write completing -- or being refused -- moves them with no
     * touch to invalidate the cached framebuffers.  Without this, a
     * refusal keeps drawing WHEN IDLE until something else repaints.
     */
    bool     drawn_dirty;               /**< whether the save button read dirty at the last draw */
    bool     drawn_asked;               /**< whether it read save asked */
    bool     drawn_failed;              /**< whether it read failed */

    /* The warning is open and holds the way out to warn_to.  drawn_* are
     * what the last frame drew of it. */
    bool     warn;                      /**< whether the warning is open */
    ui_screen_id_t warn_to;             /**< screen the warning's way out leads to */
    bool     drawn_warn;                /**< whether the warning was drawn */
    bool     drawn_armed;    /**< ARMED at the last full draw: the
                                  warning's DISCARD and a list's SAFE-only
                                  rows */

    /* The keyboard of string tk_text, and its revision as last drawn. */
    ui_textkey_t tk;                    /**< the keyboard of string tk_text */
    setting_text_id_t tk_text;          /**< string the keyboard types */
    bool     drawn_tk;                  /**< whether the keyboard was open at the last draw */
    uint32_t drawn_tk_rev;              /**< its revision at the last draw */

    /* The keypad of number kp_id, and its revision as last drawn. */
    ui_keypad_t  kp;            /**< the keypad a number's value opens */
    setting_id_t kp_id;         /**< the number it types               */
    bool     drawn_kp;          /**< whether it was open, as last drawn */
    uint32_t drawn_kp_rev;      /**< its revision, as last drawn       */
} stg_state_t;

/* Defined in settings_screen.c. */
/** @brief SETUP's state, in settings_screen.c. */
extern stg_state_t stg_s;
/** @brief Short name for stg_s inside the SETUP files. */
#define s stg_s

/* --- settings_screen.c: the geometry and the rows settings_draw.c reads -- */

/**
 * @brief Returns the rectangle of hub tile @p i.
 * @param i tile index, 0 to TILE_COUNT - 1
 * @return the tile's rectangle
 */
gfx_rect_t stg_tile_rect(int i);
/**
 * @brief Hub tile @p i: past the table's tiles, LAYOUT's, which a board whose
 *        pins are soldered shows last.
 * @param i The tile, 0 to stg_tile_count() - 1.
 * @return The tile.
 */
const hub_tile_t *stg_hub_tile(int i);

/**
 * @brief The tiles the hub shows: TILE_COUNT, and LAYOUT last on a board that
 *        solders its pins.
 * @return their number
 */
int stg_tile_count(void);
/**
 * @brief Returns column @p col of the strip under the grid and the list.
 * @param col strip column, 0 to 2
 * @return the column's rectangle
 */
gfx_rect_t stg_strip_rect(int col);
/**
 * @brief Returns the rectangle of RESET, the strip's middle column.
 * @return the rectangle
 */
gfx_rect_t stg_reset_rect(void);
/**
 * @brief Returns the rectangle of SAVE, the strip's last column.
 * @return the rectangle
 */
gfx_rect_t stg_save_rect(void);
/**
 * @brief Returns whether SAVE is offered: a setting is changed and no save is
 *        asked for.
 * @return true when SAVE is enabled
 */
bool stg_save_offered(void);
/**
 * @brief Returns whether the bench is ARMED; DISCARD changes nothing then.
 * @return true when ARMED
 */
bool stg_bench_armed(void);
/**
 * @brief Returns the rectangle of STAY, the warning's first column.
 * @return the rectangle
 */
gfx_rect_t stg_stay_rect(void);
/**
 * @brief Returns the rectangle of DISCARD, the warning's middle column.
 * @return the rectangle
 */
gfx_rect_t stg_discard_rect(void);
/**
 * @brief Returns the number of rows in the open category's list.
 * @return the row count
 */
int stg_row_count(void);
/**
 * @brief Limits the list's scroll to 0 to the content's height less the
 *        list's.
 */
void stg_clamp_scroll(void);
/**
 * @brief Returns the rectangle of list row @p index, from the list's scroll.
 * @param index row index
 * @return the row's rectangle
 */
gfx_rect_t stg_row_rect(int index);
/**
 * @brief The list's left edge: right of the left list on SENSORS.
 * @return The edge, px.
 */
int stg_list_x(void);
/**
 * @brief Returns the entry of row @p index; a string's carries text >= 0.
 * @param index row index
 * @return the entry
 */
settings_row_t stg_row_entry(int index);

/* --- settings_draw.c ---------------------------------------------------- */

/**
 * @brief SETUP drawn: the keypad alone while it is open, else the warning,
 *        or the tiles or a list over the strip, and the keyboard over them.
 * @param c The canvas.
 * @param buffer_index The framebuffer, whose cached chrome it follows.
 */
void stg_render(gfx_canvas_t *c, int buffer_index);
