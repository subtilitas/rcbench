/**
 * @file
 * @brief SETUP CODE: the state, the touches, the drawing and the screen's
 *        table.  See scanner_code.h.
 *
 * The body below the band, 1024px wide.  Left, at x 16 and y 12: the white
 * square of 348px, the code's 252px inside it at 48px from each edge.
 * Right, from x 392, 616px wide: SERIAL OUTPUT in the heading face at
 * y 12; the 3 steps from y 56 in the label face, 24px a line; the state
 * at y 264; HOLD 2s: FACTORY RESET 400 x 64px at y 336; BACK 400 x 56px at
 * y 416.
 *
 * SPDX-License-Identifier: MIT
 */

#include "scanner_code.h"

#include <stdio.h>
#include <string.h>

#include "scanner_codes.h"
#include "scanner_pane.h"
#include "settings.h"
#include "settings_screen.h"
#include "touch_types.h"
#include "ui_layout.h"
/* ui_text.h before ui_theme.h: ui_text.def ends by undefining UI_TEXT. */
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define SC_X      16   /**< the white square's left edge, px */
#define SC_Y      12   /**< its top, px */
#define SC_SIDE   ((int)SCANNER_CODE_MODULES * SCANNER_CODE_PX \
                   + 2 * SCANNER_CODE_QUIET) /**< its side, px */
#define SC_RX     392  /**< the right column's edge, px */
#define SC_RW     616  /**< its width, px */
#define SC_HEAD_Y 12   /**< SERIAL OUTPUT's top, px */
#define SC_STEP_Y 56   /**< the first step's top, px */
#define SC_STEP_P 24   /**< the steps' pitch, px */
#define SC_STATE_Y 264 /**< the state's top, px */
#define SC_HOLD_Y 336  /**< the hold's top, px */
#define SC_HOLD_W 400  /**< its width, px */
#define SC_HOLD_H 64   /**< its height, px */
#define SC_BACK_Y 416  /**< BACK's top, px */
#define SC_BACK_H 56   /**< its height, px */
/** @brief The body row 4px above the alert band. */
#define SC_FLOOR  (UI_ALERT_Y - UI_BAND_H - 4)

_Static_assert((int)SCANNER_CODE_MODULES * SCANNER_CODE_PX == 252,
               "the code is 252px: 21 modules of 12px");
_Static_assert(SC_Y + SC_SIDE <= SC_FLOOR,
               "the code and its white end over the alert band");
_Static_assert(SC_X + SC_SIDE < SC_RX, "the code ends left of the column");
_Static_assert(SC_RX + SC_RW <= UI_PANEL_W - 16,
               "the column ends 16px from the right edge");
_Static_assert(SC_BACK_Y + SC_BACK_H <= SC_FLOOR,
               "BACK ends over the alert band");
_Static_assert(SC_HOLD_H >= UI_TAP_MIN && SC_BACK_H >= UI_TAP_MIN,
               "the hold and BACK are tap targets");

/** @brief What a press landed on. */
enum { HIT_NONE = -1, HIT_HOLD = 0, HIT_BACK };

/** @brief The screen's state. */
typedef struct {
    int       hit;          /**< the control the press began on */
    uint8_t   press_id;     /**< the press's contact */
    bool      have_press;   /**< a press is on a control */
    ui_hold_t hold;         /**< HOLD 2s: FACTORY RESET */
    unsigned  drawn_mask;   /**< the buffers that hold the current frame */
    uint32_t  drawn_view;   /**< the state and rate drawn */
    bool      drawn_ready;  /**< scanner_code_hold_ready() drawn */
} scanner_code_t;

/** @brief The screen, the frame task's. */
static scanner_code_t v;

void scanner_code_invalidate(void) { v.drawn_mask = 0u; }

gfx_rect_t scanner_code_part(scanner_part_t part)
{
    switch (part) {
    case SCANNER_PART_CODE:
        return gfx_rect_make(SC_X, SC_Y, SC_SIDE, SC_SIDE);
    case SCANNER_PART_HOLD:
        return gfx_rect_make(SC_RX, SC_HOLD_Y, SC_HOLD_W, SC_HOLD_H);
    case SCANNER_PART_BACK:
        return gfx_rect_make(SC_RX, SC_BACK_Y, SC_HOLD_W, SC_BACK_H);
    default:
        return gfx_rect_make(0, 0, 0, 0);
    }
}

bool scanner_code_hold_ready(void)
{
    return settings_get_bool(SET_SCANNER_ENABLE);
}

/** @brief The control at a point of the body. */
static int hit_at(int x, int y)
{
    if (gfx_rect_contains(scanner_code_part(SCANNER_PART_BACK), x, y)) {
        return HIT_BACK;
    }
    if (scanner_code_hold_ready()
        && gfx_rect_contains(scanner_code_part(SCANNER_PART_HOLD), x, y)) {
        return HIT_HOLD;
    }
    return HIT_NONE;
}

/** @brief The press is over. */
static void drop_press(void)
{
    v.hit = HIT_NONE;
    v.have_press = false;
}

/** @brief Takes a touch event. */
static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (v.have_press) {
            return;
        }
        v.hit = hit_at(x, y);
        if (v.hit != HIT_NONE) {
            v.have_press = true;
            v.press_id = evt->point.id;
            if (v.hit == HIT_HOLD) {
                ui_hold_begin(&v.hold, TX_HOLD_HINT_SCN_RESET);
            }
        }
        scanner_code_invalidate();
        return;
    }
    if (!v.have_press || evt->point.id != v.press_id) {
        return;
    }
    const gfx_rect_t hold = scanner_code_part(SCANNER_PART_HOLD);
    if (evt->type == TOUCH_EVENT_MOVE) {
        if (v.hit == HIT_HOLD && !gfx_rect_contains(hold, x, y)
            && ui_hold_leave(&v.hold)) {
            drop_press();
            scanner_code_invalidate();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = v.hit;
    drop_press();
    scanner_code_invalidate();
    if (hit == HIT_HOLD) {
        (void)ui_hold_end(&v.hold, evt, hold);
        return;
    }
    if (hit == HIT_BACK && touch_event_is_tap_up(evt)
        && hit_at(x, y) == HIT_BACK) {
        ui_router_goto(ui_router_parent(SCREEN_SCAN_CODE));
    }
}

/** @brief A frame: the hold, and the state shown. */
static void tick(float dt_s)
{
    const scanner_view_t *sv = scanner_pane_view();
    const uint32_t shown = ((uint32_t)sv->state << 24) ^ sv->baud;
    const bool ready = scanner_code_hold_ready();
    if (v.hold.down || v.hold.flash_ms > 0 || shown != v.drawn_view
        || ready != v.drawn_ready) {
        v.drawn_view = shown;
        v.drawn_ready = ready;
        scanner_code_invalidate();
    }
    ui_hold_flash_step(&v.hold, dt_s);
    if (!ready) {
        if (v.hit == HIT_HOLD && ui_hold_left(&v.hold)) {
            drop_press();
        }
        return;
    }
    if (ui_hold_tick(&v.hold, dt_s)) {
        scanner_pane_ask(SCANNER_CMD_RESET);
        ui_hold_reached(&v.hold);
        scanner_code_invalidate();
    }
}

/** @brief Touch events were lost: the press goes. */
static void cancel(void)
{
    if (v.hit == HIT_HOLD) {
        (void)ui_hold_left(&v.hold);
    }
    drop_press();
    scanner_code_invalidate();
}

/** @brief The screen's state at start. */
static void reset(void)
{
    memset(&v, 0, sizeof(v));
    v.hit = HIT_NONE;
    ui_hold_reset(&v.hold);
}

/** @brief Shown: nothing held. */
static void enter(void)
{
    drop_press();
    ui_hold_reset(&v.hold);
    scanner_code_invalidate();
}

/** @brief Left: back on SETUP the list of SCANNER shows as it was left. */
static void leave(void)
{
    drop_press();
    ui_hold_reset(&v.hold);
    if (ui_router_leaving_to() == SCREEN_SETUP) {
        settings_screen_resume();
    }
}

/** @brief The code, dark on white, in its square. */
static void draw_code(gfx_canvas_t *c)
{
    gfx_fill_rect(c, SC_X, SC_Y, SC_SIDE, SC_SIDE, GFX_WHITE);
    const int x0 = SC_X + SCANNER_CODE_QUIET, y0 = SC_Y + SCANNER_CODE_QUIET;
    for (int y = 0; y < (int)SCANNER_CODE_MODULES; ++y) {
        for (int x = 0; x < (int)SCANNER_CODE_MODULES; ++x) {
            if (scanner_code_dark(scanner_code_serial, x, y)) {
                gfx_fill_rect(c, x0 + x * SCANNER_CODE_PX,
                              y0 + y * SCANNER_CODE_PX, SCANNER_CODE_PX,
                              SCANNER_CODE_PX, GFX_BLACK);
            }
        }
    }
}

/** @brief One line of text, left aligned, cut to the column. */
static void line(gfx_canvas_t *c, int y, const char *t, const gfx_font_t *f,
                 gfx_color_t ink)
{
    gfx_text_in(c, gfx_rect_make(SC_RX, y, SC_RW, gfx_text_height(f, 1)), t,
                f, ink, 1, GFX_ALIGN_LEFT);
}

/** @brief The right column: the steps, the state, the hold and BACK. */
static void draw_side(gfx_canvas_t *c)
{
    line(c, SC_HEAD_Y, TR(SCN_CODE_HEAD), UI_FONT_HEAD, UI_ACCENT);
    const ui_text_id_t steps[] = { TX_SCN_STEP_1, TX_SCN_STEP_2,
                                   TX_SCN_STEP_3 };
    int y = SC_STEP_Y;
    for (size_t i = 0u; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        const int rows = ui_text_wrap(
            c, gfx_rect_make(SC_RX, y, SC_RW, gfx_text_height(UI_FONT_LABEL,
                                                               1)),
            SC_STEP_P, ui_tr(steps[i]), UI_FONT_LABEL, UI_TEXT);
        y += rows * SC_STEP_P + SC_STEP_P / 2;
    }
    char buf[64];
    const bool ready = scanner_code_hold_ready();
    if (ready) {
        (void)snprintf(buf, sizeof(buf), TR(SCN_CODE_STATE),
                       scanner_pane_state_name(scanner_pane_view()->state));
    } else {
        (void)snprintf(buf, sizeof(buf), "%s", TR(SCN_LOCK_OFF));
    }
    line(c, SC_STATE_Y, buf, UI_FONT_BODY, ready ? UI_TEXT : UI_TEXT_DIM);
    ui_hold_button(c, scanner_code_part(SCANNER_PART_HOLD),
                   TR(SCN_RESET_HOLD), UI_PANEL_HI, UI_OK, &v.hold,
                   v.have_press && v.hit == HIT_HOLD, ready, UI_FONT_LABEL);
    ui_button_font(c, scanner_code_part(SCANNER_PART_BACK), TR(SBK_BACK),
                   UI_PANEL_HI, v.have_press && v.hit == HIT_BACK, true,
                   UI_FONT_LABEL);
}

/** @brief Drawn whole once per framebuffer and through the hold. */
static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((v.drawn_mask & bit) != 0u) {
        return;
    }
    gfx_clear(c, UI_BG);
    draw_code(c);
    draw_side(c);
    if (v.hold.flash_ms > 0 || v.hold.down) {
        v.drawn_mask = 0u;
    } else {
        v.drawn_mask |= bit;
    }
}

/** @brief Nothing covers the screen. @return false. */
static bool covered(void) { return false; }

/** @brief The screen, for the router. */
static const ui_screen_t k_screen = {
    .title   = "SETUP CODE",
    .covered = covered,
    .reset   = reset,
    .enter   = enter,
    .leave   = leave,
    .tick    = tick,
    .event   = event,
    .cancel  = cancel,
    .render  = render,
};

const ui_screen_t *scanner_code_screen(void) { return &k_screen; }

void scanner_code_open(void)
{
    if (ui_router_current() != SCREEN_SCAN_CODE) {
        ui_router_goto(SCREEN_SCAN_CODE);
    }
}
