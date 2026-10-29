/**
 * @file
 * @brief The feature menu: a grid of tiles under the band.
 *
 * SPDX-License-Identifier: MIT
 */

#include "overview_screen.h"

#include <string.h>

#include "ui_band.h"
#include "ui_help.h"
#include "ui_icons.h"
#include "link_pages.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The panel's width, UI_PANEL_W px. */
#define W UI_PANEL_W
/** @brief The body's height under the band, UI_BODY_H px. */
#define H UI_BODY_H   /* the router owns the band */

/**
 * @brief The grid: four columns by two rows of 243 x 255px tiles (36.5 x
 *        38.3mm at 0.150mm a pixel) with 10px margins under the 60px
 *        band.  COLS and ROWS are the grid, M its margin, TW and TH a
 *        tile's size.
 */
#define COLS 4 /**< the grid's columns */
#define ROWS 2 /**< the grid's rows */
#define M    10 /**< the margin around and between tiles, px */
#define TW   ((W - (COLS + 1) * M) / COLS) /**< a tile's width, px: 243 */
#define TH   ((H - (ROWS + 1) * M) / ROWS) /**< a tile's height, px: 255 */

/**
 * @brief Inside a tile, from its top: the 40px icon box at 30, the name
 *        in the head face (20 x 36) at 84, the line in the label face
 *        (10 x 20) at 128, a broken line's second row 24px under it.  A
 *        row of the line holds 22 characters, 220 of the 227px inside the
 *        tile's UI_TILE_PAD.
 */
#define T_ICON_DY  30 /**< the icon box's top, px */
#define T_ICON     40 /**< the icon box's side, px */
#define T_NAME_DY  84 /**< the name's top, px */
#define T_LINE_DY  128 /**< the line's top, px */
#define T_LINE_PITCH 24 /**< a second row under the first, px */

/** @brief The menu tile's look, from the figures above. */
static const ui_tile_look_t k_look = {
    .icon_dy = T_ICON_DY, .icon = T_ICON, .name_dy = T_NAME_DY,
    .line_dy = T_LINE_DY, .line_pitch = T_LINE_PITCH,
    .name_f = &gfx_font_20x36, .line_f = &gfx_font_10x20,
};

/**
 * @brief The SIMULATED mark: at least 120 x 32px, 184px under the tile's
 *        top, its word in the label face (10 x 20) MARK_INSET px from its
 *        left edge, past the dot, and 8px from its right edge.  On the
 *        lower row it ends at panel y 551, 5px above the alert band, so an
 *        alert hides no mark.
 */
#define MARK_W     120 /**< its least width, px */
#define MARK_H     32 /**< its height, px */
#define MARK_INSET UI_PILL_INSET /**< its word's start, past the dot, px */
#define MARK_DY    184 /**< its top from the tile's, px */

_Static_assert(T_ICON_DY + T_ICON <= T_NAME_DY,
               "the icon ends above the name");
_Static_assert(T_LINE_DY + T_LINE_PITCH + 20 <= MARK_DY,
               "a line's second row ends above the SIMULATED mark");
_Static_assert(UI_BAND_H + (ROWS - 1) * (TH + M) + M + MARK_DY + MARK_H
                   < UI_ALERT_Y,
               "the lower row's marks end above the alert band");

typedef struct {
    ui_screen_id_t id;      /**< the screen the tile opens               */
    const char    *name;    /**< the screen's title: English always     */
    ui_text_id_t   line;    /**< the line under the name                */
    ui_icon_fn     icon;    /**< the icon over the name                 */
    ui_text_id_t   help;    /**< what the screen does, for the guide    */
    uint16_t       needs;   /**< the hardware it needs, as link_cap_t     */
    /** Modelled on the panel whatever the IOmcu reports: no link
     *  capability names the hardware.  SUPPLY's holds only until SETUP
     *  enables the PD mini; see overview_screen_set_supply_real(). */
    bool           model_only;
} tile_t;

/*
 * The tiles by kind, each in a fixed place.  Row 1, the first COLS - 1
 * columns: the workbenches, which drive outputs and can ARM.  Row 2, the
 * same columns: the instruments, which only read.  The last column: LOGS
 * over SETUP, data and setup.  A maker, a protocol, a run or a setting
 * never adds a tile; only a new device class or instrument does, with the
 * owner's word, and it sets COLS to 5.  "iR" (internal resistance) rather
 * than "IR", which reads as infrared.
 */
static const tile_t k_workbenches[] = {
    { SCREEN_MOTOR,      "MOTOR & ESC", TX_OV_MOTOR,         ui_icon_motor,
      TX_HG_OV_MOTOR,
      LINK_CAP_ESC_DRIVE | LINK_CAP_PACK_SENSE, false },
    { SCREEN_SERVO,      "SERVO",       TX_OV_SERVO,         ui_icon_servo,
      TX_HG_OV_SERVO,
      LINK_CAP_SERVO_PWM, false },
    { SCREEN_SUPPLY,     "SUPPLY",      TX_OV_SUPPLY,        ui_icon_supply,
      TX_HG_OV_SUPPLY,
      0, true },   /* modelled unless SETUP enables the PD mini */
};

static const tile_t k_instruments[] = {
    { SCREEN_ANALYSER,   "RECEIVER",    TX_OV_RECEIVER,      ui_icon_chart,
      TX_HG_OV_RECEIVER,
      LINK_CAP_RECEIVER, false },
    { SCREEN_BATTERY,    "BATTERY",     TX_OV_BATTERY,       ui_icon_battery,
      TX_HG_OV_BATTERY,
      LINK_CAP_CELLS, false },
    { SCREEN_BALANCE,    "BALANCE",     TX_OV_BALANCE,       ui_icon_balance,
      TX_HG_OV_BALANCE,
      LINK_CAP_VIBRATION, false },
};

/* The last column, top down. */
static const tile_t k_last_column[] = {
    { SCREEN_LOGS,       "LOGS",        TX_OV_LOGS,          ui_icon_record,
      TX_HG_OV_LOGS,
      0, false },   /* the card is on the panel; nothing needed from the IOmcu */
    { SCREEN_SETUP,      "SETUP",       TX_OV_SETUP,         ui_icon_sliders,
      TX_HG_OV_SETUP,
      0, false },
};

#define N_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

_Static_assert(N_OF(k_workbenches) <= COLS - 1,
               "a workbench past column COLS - 1 needs COLS 5 (owner's word)");
_Static_assert(N_OF(k_instruments) <= COLS - 1,
               "an instrument past column COLS - 1 needs COLS 5 (owner's word)");
_Static_assert(N_OF(k_last_column) == ROWS,
               "the last column holds LOGS over SETUP");

#define SLOTS (COLS * ROWS)

/* The tile in slot @p i (row by row from the top left), or NULL for a slot
 * its kind leaves empty. */
static const tile_t *tile_in(int i)
{
    if (i < 0 || i >= SLOTS) {
        return NULL;
    }
    const int col = i % COLS;
    const int row = i / COLS;
    if (col == COLS - 1) {
        return &k_last_column[row];
    }
    if (row == 0) {
        return (col < N_OF(k_workbenches)) ? &k_workbenches[col] : NULL;
    }
    return (col < N_OF(k_instruments)) ? &k_instruments[col] : NULL;
}

ui_screen_id_t overview_screen_tile(int slot)
{
    const tile_t *t = tile_in(slot);
    return (t != NULL) ? t->id : SCREEN_COUNT;
}

const char *overview_screen_tile_name(int slot)
{
    const tile_t *t = tile_in(slot);
    return (t != NULL) ? t->name : NULL;
}

const char *overview_screen_tile_line(int slot)
{
    const tile_t *t = tile_in(slot);
    return (t != NULL) ? ui_tr(t->line) : NULL;
}

int overview_screen_columns(void) { return COLS; }

static struct {
    unsigned drawn_mask;
    int      pressed;      /**< index, or -1                              */
    uint8_t  press_id;
    bool     have_press;
} s;

/* Outside s, which entering the screen clears: the control task's word on
 * the supply holds whichever screen is up. */
static bool s_supply_real;

void overview_invalidate(void) { s.drawn_mask = 0; }

void overview_screen_set_supply_real(bool real)
{
    if (s_supply_real != real) {
        s_supply_real = real;
        overview_invalidate();
    }
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.pressed = -1;
}

static gfx_rect_t tile_rect(int i)
{
    const int col = i % COLS;
    const int row = i / COLS;
    const gfx_rect_t r = {
        (int16_t)(M + col * (TW + M)),
        (int16_t)(M + row * (TH + M)),
        (int16_t)TW, (int16_t)TH,
    };
    return r;
}

gfx_rect_t overview_screen_tile_rect(int slot) { return tile_rect(slot); }

/**
 * @brief The SIMULATED mark of slot @p i: shown while the tile's hardware
 *        is not fitted or its values are modelled.
 * @param i   The slot.
 * @param out Where the mark is, in the screen's coordinates; may be NULL.
 * @return True when the tile shows the mark.
 */
static bool mark_rect(int i, gfx_rect_t *out)
{
    const tile_t *t = tile_in(i);
    if (t == NULL) {
        return false;
    }
    /* Two states.  MODELLED: the hardware is not fitted, so the values are
     * modelled.  No mark: the hardware is fitted. */
    const uint16_t have = ui_router_status()->capabilities;
    const bool modelled = t->model_only
                          && !(t->id == SCREEN_SUPPLY && s_supply_real);
    const bool fitted = !modelled
                        && ((t->needs == 0) || ((have & t->needs) == t->needs));
    if (fitted) {
        return false;
    }
    /* MARK_W, or as wide as a longer word needs. */
    int bw = MARK_INSET + gfx_text_width(&gfx_font_10x20, TR(OV_MODELLED), 1)
             + 8;
    bw = (bw < MARK_W) ? MARK_W : bw;
    const gfx_rect_t r = tile_rect(i);
    if (out != NULL) {
        *out = (gfx_rect_t){ (int16_t)(r.x + (r.w - bw) / 2),
                             (int16_t)(r.y + MARK_DY), (int16_t)bw, MARK_H };
    }
    return true;
}

static int tile_at(int x, int y)
{
    for (int i = 0; i < SLOTS; ++i) {
        if (tile_in(i) != NULL && gfx_rect_contains(tile_rect(i), x, y)) {
            return i;
        }
    }
    return -1;
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int hit = tile_at(evt->point.x, evt->point.y);

    if (evt->type == TOUCH_EVENT_DOWN && hit >= 0) {
        s.have_press = true;
        s.press_id   = evt->point.id;
        s.pressed    = hit;
        s.drawn_mask = 0;
        return;
    }
    if (!s.have_press || evt->point.id != s.press_id) {
        return;   /* a second finger cannot steal the first one's release */
    }
    if (evt->type == TOUCH_EVENT_UP) {
        const int was = s.pressed;
        s.have_press = false;
        s.pressed    = -1;
        s.drawn_mask = 0;
        /* A press that slid off its tile is not a tap on that tile, and
         * neither is a release the finger did not make. */
        if (was >= 0 && hit == was && touch_event_is_tap_up(evt)) {
            ui_router_goto(tile_in(was)->id);
        }
    }
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((s.drawn_mask & bit) != 0) {
        return;   /* tiles are chrome: painted once per framebuffer */
    }
    s.drawn_mask |= bit;

    gfx_clear(c, ui_theme_color(UI_C_BG));

    for (int i = 0; i < SLOTS; ++i) {
        const tile_t *t = tile_in(i);
        if (t == NULL) {
            continue;
        }
        const gfx_rect_t r = tile_rect(i);
        const bool down = (i == s.pressed);

        /* A screen whose hardware is missing is fully usable, so its icon
         * stays lit. */
        ui_tile(c, r, &k_look, t->icon, t->name, ui_tr(t->line), down);

        gfx_rect_t badge;
        if (mark_rect(i, &badge)) {
            ui_pill_font(c, badge, TR(OV_MODELLED), 0,
                         ui_theme_color(UI_C_PANEL_SUNK), MARK_INSET,
                         &gfx_font_10x20);
        }
    }
}

/*
 * Touch events were lost between two frames, so this screen's record of what
 * is on the glass cannot be trusted.  A press held open owns its track id,
 * and the GT911 reuses ids: a later contact that began somewhere else would
 * be taken for this one's release and act on a control nobody pressed.
 */
static void cancel(void)
{
    s.have_press = false;
    /* And the tile drawn pressed: with no contact to release it, it would
     * stay pressed on the glass until something else redrew the screen. */
    s.pressed    = -1;
    s.drawn_mask = 0;
}

/* ---------------------------------------------------------- the guide */

/*
 * Three pages.  The left two columns with the dock on the right, the right
 * two with the dock on the left, then the band and the SIMULATED mark with
 * the dock on the left.  The menu has no control that stops an output and
 * names no pin, so every page shows while ARMED as well.
 */
/** @brief The slots of page 1, the left two columns, and of page 2. */
static const int8_t k_page_slots[2][4] = { { 0, 1, 4, 5 }, { 2, 3, 6, 7 } };

/**
 * @brief The menu's guide pages.
 * @return 3, in every state.
 */
static int help_count(void) { return 3; }

/**
 * @brief A rect of the band, in panel coordinates, in the screen's.
 * @param r The rect.
 * @return @p r moved up by the band's height.
 */
static gfx_rect_t band_item(gfx_rect_t r)
{
    r.y = (int16_t)(r.y - UI_BAND_H);
    return r;
}

/**
 * @brief Page @p i of the menu's guide.
 * @param i   The page, from 0.
 * @param out The page, filled.
 * @return False past the last page.
 */
static bool help_page(int i, ui_help_page_t *out)
{
    if (i < 0 || i >= help_count() || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->armed_ok = true;
    static const ui_text_id_t k_title[3] = { TX_HG_OV_TITLE_LEFT,
                                             TX_HG_OV_TITLE_RIGHT,
                                             TX_HG_OV_TITLE_BAND };
    out->title = k_title[i];
    if (i < 2) {
        out->side = (i == 0) ? UI_HELP_DOCK_RIGHT : UI_HELP_DOCK_LEFT;
        for (int k = 0; k < 4; ++k) {
            const int slot = k_page_slots[i][k];
            const tile_t *t = tile_in(slot);
            if (t == NULL) {
                continue;
            }
            out->item[out->count++] = (ui_help_item_t){
                t->name, t->help, tile_rect(slot) };
        }
        return true;
    }
    out->side = UI_HELP_DOCK_LEFT;
    const ui_bench_status_t *st = ui_router_status();
    out->item[out->count++] = (ui_help_item_t){
        "STOP", TX_HG_OV_STOP, band_item(ui_band_stop_rect()) };
    gfx_rect_t chip;
    if (ui_band_chip_rect(st, UI_BAND_CHIP_ARMED, &chip)) {
        out->item[out->count++] = (ui_help_item_t){
            st->armed ? ui_band_armed_label(st->armed_roles) : TR(BAND_SAFE),
            TX_HG_OV_ARM, band_item(chip) };
    }
    out->item[out->count++] = (ui_help_item_t){
        "?", TX_HG_OV_GUIDE, band_item(ui_band_guide_rect()) };
    /* The first mark of the right half, top down by column. */
    static const int8_t k_mark_slots[4] = { 2, 6, 3, 7 };
    for (int k = 0; k < 4; ++k) {
        gfx_rect_t mark;
        if (mark_rect(k_mark_slots[k], &mark)) {
            out->item[out->count++] = (ui_help_item_t){
                TR(OV_MODELLED), TX_HG_OV_MARK, mark };
            break;
        }
    }
    return true;
}

static const ui_screen_t k_screen = {
    .title  = "rcbench",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = event,
    .cancel = cancel,
    .render = render,
    .help_count = help_count,
    .help_page  = help_page,
};

const ui_screen_t *overview_screen(void) { return &k_screen; }
