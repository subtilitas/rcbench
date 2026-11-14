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
#include "wireless_pane.h"
#include "settings.h"
#include "settings_rows_int.h"
#include "settings_screen.h"
#include "ui_icons.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_footer.h"
#include "ui_setlist.h"

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

/** @brief The faces of the hub and SENSORS' left list: a tile's name in
 *         the body face (12 x 24), its line in the label face (10 x 20). */
#define STG_F_BODY (&gfx_font_12x24) /**< tile names, entries: 12 x 24 */
#define STG_F_LABEL (&gfx_font_10x20) /**< tile lines: 10 x 20 */

/** @brief The strip under the grid and under the list: three columns,
 *         the hub's, UI_BTN_H (48px) tall; the list's (ui_setlist.h). */
#define STRIP_H UI_BTN_H /**< the strip's height, px */
#define STRIP_Y (SCREEN_H - UI_ALERT_H - 4 - STRIP_H) /**< the strip's top, px: 444 */
_Static_assert(STRIP_Y == UI_FOOTER_Y && STRIP_H == UI_FOOTER_H,
               "SETUP's strip is the footer's slots");

/** @brief The list's top, px: the settings list's (ui_setlist.h). */
#define LIST_Y UI_SL_LIST_Y

/**
 * @brief The split panes' left list, SENSORS' and WIRELESS': up to 9
 *        entries of 220 x 48px at a 48px pitch, from the list's top to 4px
 *        above the strip, 432px.  Each entry takes
 *        touches over its 48px and draws its card 1px in from its top and
 *        bottom, 2px between two cards.  The name in the body face at 3px,
 *        the state in the 8 x 16 face at 28px: 25 cells end short of the
 *        entry's edge.  The list pane starts 10px right of it.
 */
#define ENTRY_W 220 /**< an entry's width, px */
#define ENTRY_H 48 /**< an entry's height, px: the tap target */
#define ENTRY_PITCH 48 /**< an entry's pitch, px */
#define ENTRY_INSET 1 /**< the card's inset from the entry's top and bottom, px */
#define ENTRY_NAME_DY 3 /**< the name's top, px */
#define ENTRY_STATE_DY 28 /**< the state's top, px */
#define PANE_X (HUB_M + ENTRY_W + HUB_M) /**< the pane's left edge, px: 240 */

/** @brief A press that moves further than DRAG_SLOP px, 8px, is a scroll and
 *         no tap: an entry of a split pane's left list is given up. */
#define DRAG_SLOP 8

/** @brief SETUP's own presses, past the list's (ui_setlist.h). */
enum { HIT_TILE = UI_SL_HIT_HOST, /**< a hub tile                       */
       HIT_ENTRY };               /**< an entry of a split pane's list  */

/** @brief The split panes: SENSORS' and WIRELESS'. @{ */
#define STG_PANES 2
#define STG_PANE_SENSORS  0 /**< SET_CAT_SENSORS */
#define STG_PANE_WIRELESS 1 /**< SET_CAT_WIRELESS */
/** @brief The most entries a pane's left list has. */
#define STG_PANE_ENTRIES 9
/** @} */
_Static_assert(SENS_ENTRY_COUNT <= STG_PANE_ENTRIES
               && WL_ENTRY_COUNT <= STG_PANE_ENTRIES,
               "each pane's entries fit its list");

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
    HUB_SCREEN(SCREEN_WIFI, TX_SET_HUB_WIFI, ui_icon_wifi),
    HUB_SCREEN(SCREEN_UPDATES, TX_SET_HUB_UPDATES, ui_icon_record),
    HUB_SCREEN(SCREEN_IMU, TX_SET_HUB_IMU, ui_icon_chip),
    HUB_CATEGORY_LINE(SET_CAT_WIRELESS, TX_SET_HUB_WIRELESS, ui_icon_dial),
};

/** @brief Tiles of every board; a board that solders its pins adds LAYOUT. */
#define TILE_COUNT ((int)(sizeof(k_tiles) / sizeof(k_tiles[0])))

_Static_assert(TILE_COUNT + 1 <= SETTINGS_HUB_SLOTS,
               "a tenth SETUP tile needs the hub's second page");
_Static_assert(SETTINGS_HUB_SLOTS == HUB_COLS * HUB_ROWS,
               "the hub's slots are its grid");
_Static_assert(HUB_M + HUB_ROWS * (HUB_TH + HUB_M) <= STRIP_Y,
               "the grid ends above the strip");
_Static_assert(LIST_Y + STG_PANE_ENTRIES * ENTRY_PITCH <= STRIP_Y - 4,
               "a split pane's left list ends 4px above the strip");
_Static_assert(ENTRY_STATE_DY + 16 <= ENTRY_H - ENTRY_INSET,
               "an entry's state ends inside its card");
_Static_assert(ENTRY_H >= UI_TAP_MIN, "an entry is a tap target");
_Static_assert(STRIP_Y + STRIP_H <= SCREEN_H - UI_ALERT_H,
               "the strip ends above the alert band");
_Static_assert(HUB_ICON_DY + HUB_ICON <= HUB_NAME_DY
               && HUB_NAME_DY + 24 <= HUB_LINE_DY
               && HUB_LINE_DY + 20 <= HUB_TH,
               "a hub tile's icon, name and line stack inside it");

/**
 * @brief SETUP's state: the category list (the settings list widget, which
 *        also holds the press, the warning, the keyboard and the keypad), the
 *        category it shows, SENSORS' scrolls, and what the last frame drew.
 */
typedef struct {
    /** @brief The list; its open is false while the hub shows */
    ui_setlist_t  sl;
    setting_cat_t cat;                  /**< the category the list shows */
    /** @brief The scroll each split pane's entry had when last left, px */
    int      entry_scroll[STG_PANES][STG_PANE_ENTRIES];
    bool     opened;   /**< another screen opened the list: no tiles above it */
    ui_screen_id_t warn_to;             /**< screen the warning's way out leads to */

    bool     chrome_valid[MAX_FBS];     /**< per frame buffer: whether its chrome is current */
    bool     drawn_list;                /**< whether the list showed at the last draw */
    setting_cat_t drawn_cat;            /**< category the last draw showed */
    bool     drawn_warn;                /**< whether the warning was drawn */
    /** @brief The rows the IOmcu does not hold, as the control task last
     *         said.  A reset of the screen keeps them: what the IOmcu holds
     *         does not change with it. */
    uint16_t sense_unheld;
    uint8_t  tone_unheld;   /**< the tap's rows the IOmcu does not hold, TONE_LINK_ROWS_*; kept as sense_unheld */
} stg_state_t;

/* Defined in settings_screen.c. */
/** @brief SETUP's state, in settings_screen.c. */
extern stg_state_t stg_s;
/** @brief Short name for stg_s inside the SETUP files. */
#define s stg_s

/* --- settings_panes.c: the split panes, SENSORS' and WIRELESS' ------------ */

/**
 * @brief The split pane of @p cat.
 * @param cat The category.
 * @return STG_PANE_SENSORS, STG_PANE_WIRELESS, or -1 for a plain list.
 */
int stg_pane(setting_cat_t cat);
/**
 * @brief The entries of pane @p pane's left list.
 * @param pane The pane, from stg_pane().
 * @return The count; 0 for -1.
 */
int stg_pane_count(int pane);
/**
 * @brief The entry pane @p pane shows.
 * @param pane The pane.
 * @return The entry; 0 for -1.
 */
int stg_pane_selected(int pane);
/**
 * @brief Select entry @p entry of pane @p pane.
 * @param pane The pane.  @param entry The entry.
 */
void stg_pane_select(int pane, int entry);
/**
 * @brief An entry's name.
 * @param pane The pane.  @param entry The entry.
 * @param buf Takes the name.  @param n Its size.
 * @return @p buf.
 */
const char *stg_pane_name(int pane, int entry, char *buf, size_t n);
/**
 * @brief An entry's state line.
 * @param pane The pane.  @param entry The entry.
 * @param buf Takes the words.  @param n Its size.
 * @param ink Takes their colour.
 * @return @p buf.
 */
const char *stg_pane_state(int pane, int entry, char *buf, size_t n,
                           gfx_color_t *ink);
/**
 * @brief RESET of an entry: its rows back to their defaults.
 * @param pane The pane.  @param entry The entry.
 */
void stg_pane_reset(int pane, int entry);

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

/* --- help_setup.c -------------------------------------------------------- */

struct ui_help_page;

/**
 * @brief The screen guide's pages of SETUP: two on the tiles, two on a
 *        list; none while the warning, the keyboard or the keypad is open.
 * @return The count.
 */
int stg_help_count(void);

/**
 * @brief Page @p i of SETUP's guide.
 * @param i   The page, from 0.
 * @param out The page, filled.
 * @return False past the last page.
 */
bool stg_help_page(int i, struct ui_help_page *out);

/* --- settings_draw.c ---------------------------------------------------- */

/**
 * @brief SETUP drawn: the keypad alone while it is open, else the warning,
 *        or the tiles or a list over the strip, and the keyboard over them.
 * @param c The canvas.
 * @param buffer_index The framebuffer, whose cached chrome it follows.
 */
void stg_render(gfx_canvas_t *c, int buffer_index);
