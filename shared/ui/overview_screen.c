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

#define W 800
#define H (480 - UI_BAND_H)   /* the router owns the band */

/*
 * Five columns by two rows: ten 150 x 204 px tiles with 8 px margins under
 * the 48 px band.  A tile's line holds 16 characters of the 8 x 16 font,
 * 128 of its 150 px.
 */
#define COLS 5
#define ROWS 2
#define M    8
#define TW   ((W - (COLS + 1) * M) / COLS)
#define TH   ((H - (ROWS + 1) * M) / ROWS)

typedef struct {
    ui_screen_id_t id;
    const char    *name;    /**< the screen's title: English always     */
    ui_text_id_t   line;
    ui_icon_fn     icon;
    bool           live;    /**< the screen exists                        */
    uint16_t       needs;   /**< the hardware it needs, as link_cap_t     */
    /** Modelled on the panel whatever the coprocessor reports: no link
     *  capability names the hardware.  SUPPLY's holds only until SETUP
     *  enables the PD mini; see overview_screen_set_supply_real(). */
    bool           model_only;
} tile_t;

/*
 * Tiles in order of expected use.  A tile whose screen does not exist leads
 * to a stub that names what blocks it.  "iR" (internal resistance) rather
 * than "IR", which reads as infrared.
 */
static const tile_t k_tiles[] = {
    { SCREEN_MOTOR,      "MOTOR & ESC", TX_OV_MOTOR,         ui_icon_motor,   true,
      LINK_CAP_ESC_DRIVE | LINK_CAP_PACK_SENSE, false },
    { SCREEN_SERVO,      "SERVO",       TX_OV_SERVO,         ui_icon_servo,   true,
      LINK_CAP_SERVO_PWM, false },
    { SCREEN_SUPPLY,     "SUPPLY",      TX_OV_SUPPLY,        ui_icon_supply,  true,
      0, true },   /* modelled unless SETUP enables the PD mini */
    { SCREEN_ANALYSER,   "ANALYSER",    TX_OV_ANALYSER,      ui_icon_chart,   true,
      LINK_CAP_RECEIVER, false },
    { SCREEN_LOGS,       "LOGS",        TX_OV_LOGS,          ui_icon_record,  true,
      0, false },   /* the card is on the panel; nothing needed from the coprocessor */
    { SCREEN_SETUP,      "SETUP",       TX_OV_SETUP,         ui_icon_sliders, true,
      0, false },
    { SCREEN_BATTERY,    "BATTERY",     TX_OV_BATTERY,       ui_icon_battery, true,
      LINK_CAP_CELLS, false },
    { SCREEN_BALANCE,    "BALANCE",     TX_OV_BALANCE,       ui_icon_balance, true,
      LINK_CAP_VIBRATION, false },
    { SCREEN_PROGRAMMER, "PROGRAMMER",  TX_OV_PROGRAMMER,    ui_icon_chip,    true,
      LINK_CAP_PROGRAM, false },
};

#define TILE_COUNT ((int)(sizeof(k_tiles) / sizeof(k_tiles[0])))

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

static int tile_at(int x, int y)
{
    for (int i = 0; i < TILE_COUNT; ++i) {
        if (gfx_rect_contains(tile_rect(i), x, y)) {
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
        /* A press that slid off its tile is not a tap on that tile. */
        if (was >= 0 && hit == was) {
            ui_router_goto(k_tiles[was].id);
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

    for (int i = 0; i < TILE_COUNT; ++i) {
        const tile_t *t = &k_tiles[i];
        const gfx_rect_t r = tile_rect(i);
        const bool down = (i == s.pressed);

        gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, 8,
                            down ? ui_theme_color(UI_C_PANEL_HI)
                                 : ui_theme_color(UI_C_PANEL));
        gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, 8,
                            ui_theme_color(UI_C_EDGE));

        /* The icon dims for a screen that does not exist.  A screen whose
         * hardware is missing is fully usable, so it stays lit. */
        t->icon(c, r.x + r.w / 2, r.y + 64, 30,
                t->live ? ui_theme_color(UI_C_ACCENT)
                        : ui_theme_color(UI_C_TEXT_FAINT));

        /* Clipped to the tile: centring a string wider than its tile puts the
         * overhang on the neighbour. */
        const gfx_rect_t name = { r.x, (int16_t)(r.y + 104), r.w, 22 };
        gfx_text_in(c, name, t->name, &gfx_font_8x16,
                    ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_CENTER);
        const gfx_rect_t line = { r.x, (int16_t)(r.y + 128), r.w, 20 };
        gfx_text_in(c, line, ui_tr(t->line), &gfx_font_8x16,
                    ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_CENTER);

        /*
         * Three states.  SOON: the screen does not exist.  MODELLED: the
         * screen exists and its hardware is not fitted, so its values are
         * modelled.  No badge: the hardware is fitted.
         */
        const uint16_t have = ui_router_status()->capabilities;
        const bool modelled = t->model_only
                              && !(t->id == SCREEN_SUPPLY && s_supply_real);
        const bool fitted = !modelled
                            && ((t->needs == 0)
                                || ((have & t->needs) == t->needs));
        if (!t->live || !fitted) {
            /* 88 px, or as wide as a longer word: the label starts 24 px
             * in and runs to the end, as MODELLED does in 88. */
            const char *word = t->live ? TR(OV_MODELLED) : TR(OV_SOON);
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
