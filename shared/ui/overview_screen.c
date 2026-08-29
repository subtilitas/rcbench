#include "overview_screen.h"

#include <string.h>

#include "ui_icons.h"
#include "link_pages.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)   /* the router owns the band */

/*
 * Four columns by two rows.  Four because eight tiles is what the feature set
 * needs today and three columns would leave two orphans on a second row; two
 * rows because 193 px tiles do not fit three deep under a 48 px band.
 */
#define COLS 4
#define ROWS 2
#define M    8
#define TW   ((W - (COLS + 1) * M) / COLS)
#define TH   ((H - (ROWS + 1) * M) / ROWS)

typedef struct {
    ui_screen_id_t id;
    const char    *name;
    const char    *line;
    ui_icon_fn     icon;
    bool           live;    /**< the screen exists                        */
    uint16_t       needs;   /**< the hardware it needs, as link_cap_t     */
} tile_t;

/*
 * Order is by how often a bench gets used, not by how much of it is built.
 * The three that are not live still appear, and say why on the screen they
 * lead to: a stub that says "coming soon" teaches nobody anything; one that
 * names its blocker is a to-do list somebody can answer.
 */
/* iR, not IR: this bench also measures temperature with an infrared part, and
 * the two would sit two tiles apart under the same abbreviation. */
static const tile_t k_tiles[] = {
    { SCREEN_MOTOR,      "MOTOR & ESC", "throttle, V/A/W, rpm",    ui_icon_motor,   true,
      LINK_CAP_ESC_DRIVE | LINK_CAP_PACK_SENSE },
    { SCREEN_SERVO,      "SERVO",       "pulse, travel, current",  ui_icon_servo,   true,
      LINK_CAP_SERVO_PWM },
    { SCREEN_ANALYSER,   "ANALYSER",    "buses, frames, raw",      ui_icon_chart,   true,
      LINK_CAP_RECEIVER },
    { SCREEN_LOGS,       "LOGS",        "record and read back",    ui_icon_record,  true,
      0 },   /* the card is on the panel, so this needs nothing of the far end */
    { SCREEN_SETUP,      "SETUP",       "pack, output, theme",     ui_icon_sliders, true,
      0 },
    { SCREEN_BATTERY,    "BATTERY",     "cells, iR, capacity",     ui_icon_battery, false,
      LINK_CAP_CELLS },
    { SCREEN_BALANCE,    "BALANCE",     "vibration and phase",     ui_icon_balance, false,
      LINK_CAP_VIBRATION },
    { SCREEN_PROGRAMMER, "PROGRAMMER",  "ESC and servo settings",  ui_icon_chip,    true,
      LINK_CAP_PROGRAM },
};

#define TILE_COUNT ((int)(sizeof(k_tiles) / sizeof(k_tiles[0])))

static struct {
    unsigned drawn_mask;
    int      pressed;      /**< index, or -1                              */
    uint8_t  press_id;
    bool     have_press;
} s;

void overview_invalidate(void) { s.drawn_mask = 0; }

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
        gfx_text_in(c, line, t->line, &gfx_font_8x16,
                    ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_CENTER);

        /*
         * Three states, not two.
         *
         * SOON is a screen that does not exist.  MODELLED is one that does,
         * whose hardware is not fitted -- it opens, it works, and everything
         * in it is invented, which is worth knowing from the menu rather than
         * after walking into it.  Nothing at all means the part is on and the
         * numbers are real.
         *
         * The second state used to be indistinguishable from the third, so
         * the menu quietly promised measurements the bench could not take.
         */
        const uint16_t have = ui_router_status()->capabilities;
        const bool fitted = (t->needs == 0)
                            || ((have & t->needs) == t->needs);
        if (!t->live || !fitted) {
            const gfx_rect_t badge = { (int16_t)(r.x + r.w / 2 - 44),
                                       (int16_t)(r.y + r.h - 30), 88, 20 };
            ui_pill(c, badge, t->live ? "MODELLED" : "SOON", 0,
                    ui_theme_color(UI_C_PANEL_SUNK));
        }
    }
}

static const ui_screen_t k_screen = {
    .title  = "rcbench",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = event,
    .render = render,
};

const ui_screen_t *overview_screen(void) { return &k_screen; }
