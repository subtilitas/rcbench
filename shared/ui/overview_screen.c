/*
 * SPDX-License-Identifier: MIT
 */

#include "overview_screen.h"

#include <string.h>

#include "ui_icons.h"
#include "link_pages.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W UI_PANEL_W
#define H UI_BODY_H   /* the router owns the band */

/*
 * Four columns by two rows: eight 190 x 204 px tiles with 8 px margins
 * under the 48 px band.  A tile's line holds 23 characters of the 8 x 16
 * font, 184 of its 190 px, on each of two rows: the second ends 8 px above
 * the SIMULATED badge.
 */
#define COLS 4
#define ROWS 2
#define M    8
#define TW   ((W - (COLS + 1) * M) / COLS)
#define TH   ((H - (ROWS + 1) * M) / ROWS)

typedef struct {
    ui_screen_id_t id;
    const char    *name;    /**< the screen's title: English always     */
    ui_text_id_t   line;
    ui_icon_fn     icon;
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
      LINK_CAP_ESC_DRIVE | LINK_CAP_PACK_SENSE, false },
    { SCREEN_SERVO,      "SERVO",       TX_OV_SERVO,         ui_icon_servo,
      LINK_CAP_SERVO_PWM, false },
    { SCREEN_SUPPLY,     "SUPPLY",      TX_OV_SUPPLY,        ui_icon_supply,
      0, true },   /* modelled unless SETUP enables the PD mini */
};

static const tile_t k_instruments[] = {
    { SCREEN_ANALYSER,   "RECEIVER",    TX_OV_RECEIVER,      ui_icon_chart,
      LINK_CAP_RECEIVER, false },
    { SCREEN_BATTERY,    "BATTERY",     TX_OV_BATTERY,       ui_icon_battery,
      LINK_CAP_CELLS, false },
    { SCREEN_BALANCE,    "BALANCE",     TX_OV_BALANCE,       ui_icon_balance,
      LINK_CAP_VIBRATION, false },
};

/* The last column, top down. */
static const tile_t k_last_column[] = {
    { SCREEN_LOGS,       "LOGS",        TX_OV_LOGS,          ui_icon_record,
      0, false },   /* the card is on the panel; nothing needed from the IOmcu */
    { SCREEN_SETUP,      "SETUP",       TX_OV_SETUP,         ui_icon_sliders,
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
        ui_tile(c, r, 64, t->icon, t->name, ui_tr(t->line), down);

        /*
         * Two states.  MODELLED: the hardware is not fitted, so the values
         * are modelled.  No badge: the hardware is fitted.
         */
        const uint16_t have = ui_router_status()->capabilities;
        const bool modelled = t->model_only
                              && !(t->id == SCREEN_SUPPLY && s_supply_real);
        const bool fitted = !modelled
                            && ((t->needs == 0)
                                || ((have & t->needs) == t->needs));
        if (!fitted) {
            /* 88 px, or as wide as a longer word: the label starts 24 px
             * in and runs to the end, as MODELLED does in 88. */
            const char *word = TR(OV_MODELLED);
            int bw = 24 + gfx_text_width(&gfx_font_8x16, word, 1);
            bw = (bw < 88) ? 88 : bw;
            const gfx_rect_t badge = { (int16_t)(r.x + (r.w - bw) / 2),
                                       (int16_t)(r.y + r.h - 30),
                                       (int16_t)bw, 20 };
            ui_pill(c, badge, word, 0, ui_theme_color(UI_C_PANEL_SUNK));
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

static const ui_screen_t k_screen = {
    .title  = "rcbench",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *overview_screen(void) { return &k_screen; }
