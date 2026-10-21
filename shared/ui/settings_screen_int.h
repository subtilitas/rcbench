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
#include "settings.h"
#include "settings_rows_int.h"
#include "settings_screen.h"
#include "ui_icons.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"

#define SCREEN_W UI_PANEL_W
/* The router owns the top 48 and the home tag; this screen gets the body. */
#define SCREEN_H UI_BODY_H
#define MAX_FBS  3

/*
 * Coordinates are in the 432 px body the router hands this screen; the band
 * and the home tag are above it.
 *
 * The hub: three columns by three rows of 256 x 108 px tiles with 8 px
 * margins, and under them the strip that holds SAVE.  The list has the same
 * strip, so SAVE stays where it is between the two.  The strip ends 4 px
 * above the router's alert band, which covers the bottom UI_ALERT_H px of
 * the body: nothing that acts sits under it.
 */
#define HUB_COLS 3
#define HUB_ROWS 3
#define HUB_M    8
#define HUB_TW   ((SCREEN_W - (HUB_COLS + 1) * HUB_M) / HUB_COLS)
#define HUB_TH   108
/* The icon's top in a tile.  Icon, name and line take 76 px from it, which
 * leaves 16 px under the line. */
#define HUB_ICON_DY 16

/* The strip under the grid and under the list: three columns, the hub's. */
#define STRIP_H  34
#define STRIP_Y  (SCREEN_H - UI_ALERT_H - 4 - STRIP_H)

/* The list: the body's width less the margins and the scroll bar, from
 * under the band to the strip. */
#define LIST_X   8
#define LIST_W   776
#define LIST_Y   6
#define LIST_H   (STRIP_Y - 6 - LIST_Y)
#define ROW_H    54
#define ROW_GAP  4
#define ROW_PITCH (ROW_H + ROW_GAP)

#define BTN_W    44
#define BTN_H    36
_Static_assert(BTN_H <= ROW_H, "a row's buttons fit in the row");
#define PLUS_X   (LIST_X + LIST_W - 14 - BTN_W)
#define MINUS_X  (PLUS_X - 8 - 110 - 8 - BTN_W)
#define VALUE_R  (PLUS_X - 8)

/*
 * A "-" or "+" key is a tap: the press arms it and the release steps it
 * once.  The release counts when the same contact makes it inside the key,
 * no further than DRAG_SLOP from the press in x and in y, with no scroll
 * and no repeat in between.  The list scrolls by a drag that starts
 * anywhere on it, the keys included, so a key that stepped on the press
 * would step under every scroll that starts on one.
 *
 * A key held repeats: a 0 to 30,000 mAh range is 300 taps otherwise.  The
 * first step comes REPEAT_DELAY_S after the press, then REPEAT_SLOW_HZ, and
 * REPEAT_FAST_HZ once the key is held REPEAT_FAST_S longer.  The time runs
 * only while the contact stays on the key within the slop, and one frame
 * counts for at most UI_HOLD_MAX_CREDIT_S (0.25 s); the release of a key
 * that has repeated adds no step.
 */
#define REPEAT_DELAY_S 0.45f
#define REPEAT_SLOW_HZ 8.0f
#define REPEAT_FAST_S  1.8f
#define REPEAT_FAST_HZ 30.0f

/* A press that moves further than this is a scroll, not a tap. */
#define DRAG_SLOP 8

enum { HIT_NONE = 0, HIT_TILE, HIT_MINUS, HIT_PLUS, HIT_RESET, HIT_LIST,
       HIT_SAVE, HIT_STAY, HIT_DISCARD, HIT_LEAVE_SAVE, HIT_TEXT };

/*
 * The warning that holds a way out: a card over the body down to the
 * strip, its title, up to two lines on what unsaved means, then one line
 * per change and up to two notes under them.  STAY, DISCARD and SAVE take
 * the strip's three columns, SAVE where SAVE is on the tiles.
 * SETTINGS_WARN_LINES lines of 21 px fit; more changes than that leave the
 * last line to their count.
 */
#define WARN_X      HUB_M
#define WARN_Y      HUB_M
#define WARN_W      (SCREEN_W - 2 * HUB_M)
#define WARN_H      (STRIP_Y - HUB_M - WARN_Y)
#define WARN_WHY_Y  46     /* from WARN_Y, 18 px a line */
#define WARN_LINE_Y 96
#define WARN_PITCH  21
#define WARN_NOTE_Y (WARN_LINE_Y + SETTINGS_WARN_LINES * WARN_PITCH + 4)
#define WARN_NOTE_PITCH 18

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
typedef struct {
    ui_icon_fn     icon;
    int8_t         cat;      /**< the category its list shows, or -1      */
    ui_screen_id_t screen;   /**< else the screen it opens                */
    ui_text_id_t   line;     /**< the line under the name                 */
} hub_tile_t;

#define HUB_CATEGORY(cat_, icon_) \
    { (icon_), (int8_t)(cat_), SCREEN_SETUP, TX_SET_COUNT }
#define HUB_SCREEN(screen_, line_, icon_) \
    { (icon_), -1, (screen_), (line_) }

static const hub_tile_t k_tiles[] = {
    HUB_CATEGORY(SET_CAT_APP,   ui_icon_sliders),
    HUB_CATEGORY(SET_CAT_IFACE, ui_icon_chip),
    HUB_CATEGORY(SET_CAT_CARD,  ui_icon_chart),
    HUB_CATEGORY(SET_CAT_SENSORS, ui_icon_supply),
    HUB_SCREEN(SCREEN_OUTPUTS, TX_SET_HUB_OUTPUTS, ui_icon_list),
    HUB_SCREEN(SCREEN_PICKER,  TX_SET_HUB_PICKER,  ui_icon_board),
    HUB_SCREEN(SCREEN_WIFI,    TX_SET_HUB_WIFI,    ui_icon_wifi),
    HUB_SCREEN(SCREEN_UPDATES, TX_SET_HUB_UPDATES, ui_icon_record),
};

#define TILE_COUNT ((int)(sizeof(k_tiles) / sizeof(k_tiles[0])))

_Static_assert(TILE_COUNT <= SETTINGS_HUB_SLOTS,
               "a tenth SETUP tile needs the hub's second page");
_Static_assert(SETTINGS_HUB_SLOTS == HUB_COLS * HUB_ROWS,
               "the hub's slots are its grid");
_Static_assert(HUB_M + HUB_ROWS * (HUB_TH + HUB_M) <= STRIP_Y,
               "the grid ends above the strip");
_Static_assert(LIST_Y + LIST_H <= STRIP_Y, "the list ends above the strip");
_Static_assert(STRIP_Y + STRIP_H <= SCREEN_H - UI_ALERT_H,
               "the strip ends above the alert band");
_Static_assert(WARN_NOTE_Y + 2 * WARN_NOTE_PITCH <= WARN_Y + WARN_H - 4,
               "the warning's notes end inside its card");

/* The strings' names, for the warning, a row and its keyboard's title,
 * and the help line of a row; the DUT name has no row on SETUP. */
static const struct { ui_text_id_t label; ui_text_id_t help; }
    k_text_ui[SETTING_TEXT_COUNT] = {
    [SET_TEXT_DUT_NAME]     = { TX_SET_DUT_NAME, TX_COUNT },
    [SET_TEXT_LOG_AUTO_DIR] = { TX_SET_AUTO_DIR, TX_SET_AUTO_DIR_HELP },
    [SET_TEXT_LOG_USER_DIR] = { TX_SET_USER_DIR, TX_SET_USER_DIR_HELP },
};

/*
 * The keyboard a string's row opens: over the list and the strip, the
 * warning's card.  Nothing under it presses while it is open; OK and
 * CANCEL are its own, and the home tag closes it as CANCEL does.
 */
#define TK_AREA_H (STRIP_Y + STRIP_H - WARN_Y)

typedef struct {
    /* The list of cat is open; the hub shows while this is false. */
    bool     list;
    setting_cat_t cat;
    int      scroll;
    int      scroll_max;

    /* One contact holds the screen from its DOWN to its UP; any other
     * presses nothing and ends nothing. */
    bool     have_press;
    uint8_t  press_id;

    int      hit_kind;
    int      hit_index;      /* tile index, or row index */
    bool     dragging;
    int      press_x, press_y;
    int      last_y;

    float    held_for;
    bool     repeating;

    bool     chrome_valid[MAX_FBS];
    bool     drawn_list;
    setting_cat_t drawn_cat;

    /*
     * The save button's three inputs as the last frame drew them.  The
     * write is taken by settings_save_tick() outside this screen, so a
     * pending write completing -- or being refused -- moves them with no
     * touch to invalidate the cached framebuffers.  Without this, a
     * refusal keeps drawing WHEN IDLE until something else repaints.
     */
    bool     drawn_dirty;
    bool     drawn_asked;
    bool     drawn_failed;

    /* The warning is open and holds the way out to warn_to.  drawn_* are
     * what the last frame drew of it. */
    bool     warn;
    ui_screen_id_t warn_to;
    bool     drawn_warn;
    bool     drawn_armed;

    /* The keyboard of string tk_text, and its revision as last drawn. */
    ui_textkey_t tk;
    setting_text_id_t tk_text;
    bool     drawn_tk;
    uint32_t drawn_tk_rev;
} stg_state_t;

/* Defined in settings_screen.c. */
extern stg_state_t stg_s;
#define s stg_s

/* --- settings_screen.c: the geometry and the rows settings_draw.c reads -- */

gfx_rect_t stg_tile_rect(int i);
gfx_rect_t stg_strip_rect(int col);
gfx_rect_t stg_reset_rect(void);
gfx_rect_t stg_save_rect(void);
bool stg_save_offered(void);
bool stg_bench_armed(void);
gfx_rect_t stg_stay_rect(void);
gfx_rect_t stg_discard_rect(void);
int stg_row_count(void);
void stg_clamp_scroll(void);
gfx_rect_t stg_row_rect(int index);
settings_row_t stg_row_entry(int index);

/* --- settings_draw.c ---------------------------------------------------- */

void stg_render(gfx_canvas_t *c, int buffer_index);
