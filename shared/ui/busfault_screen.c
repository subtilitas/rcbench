/*
 * busfault_screen.h says what this screen is for.
 *
 * SPDX-License-Identifier: MIT
 */

#include "busfault_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

/* The red bar across the top, and the verdict under it. */
#define BAR_H     56
#define VERDICT_Y (BAR_H + 16)
#define MEANING_Y (VERDICT_Y + 34)

/* The two columns: what to do on the left, what was measured on the right. */
#define COL_X     24
#define COL_W     470
#define NUM_X     (COL_X + COL_W + 18)
#define NUM_W     (W - NUM_X - 24)

#define HEAD_Y    150
#define LIST_Y    (HEAD_Y + 26)
#define LIST_PITCH 27
#define LIST_MAX  5

/* What acknowledging leaves behind, under the list. */
#define NOTE_Y    322

/* The acknowledgement, across the bottom. */
#define ACK_H     52
#define ACK_Y     (H - ACK_H - 18)
#define ACK_X     COL_X
#define ACK_W     (W - 2 * COL_X)

static struct {
    busfault_report_t r;
    bool              have;

    float    held_s;      /**< how long the acknowledgement has been held */
    bool     fired;       /**< the hold completed; do not repeat it       */
    int      flash_left;  /**< frames of the flash still to draw          */
    bool     ack;         /**< latched for the application                */
    bool     pressed;
    uint8_t  press_id;
    bool     have_press;

    unsigned drawn_mask;  /**< the static half, per framebuffer */
} s;

void busfault_screen_invalidate(void) { s.drawn_mask = 0; }

static void reset(void)
{
    memset(&s, 0, sizeof(s));
}

void busfault_screen_set(const busfault_report_t *r)
{
    if (r == NULL) {
        memset(&s.r, 0, sizeof(s.r));
        s.have = false;
    } else {
        s.r = *r;
        s.have = true;
    }
    s.held_s = 0.0f;
    s.fired = false;
    s.flash_left = 0;
    s.drawn_mask = 0;
}

const busfault_report_t *busfault_screen_report(void)
{
    return s.have ? &s.r : NULL;
}

bool busfault_screen_take_ack(void)
{
    const bool was = s.ack;
    s.ack = false;
    return was;
}

/* ------------------------------------------------------------ what to say */

/*
 * One line of what the verdict means, in the operator's terms rather than
 * the controller's.  can_selftest_text() says what the test saw; this says
 * what it is.
 */
static const char *meaning_of(can_selftest_verdict_t v)
{
    switch (v) {
    case CAN_SELFTEST_SILENT:
        return "Nothing answered on the bus. The two boards are not talking.";
    case CAN_SELFTEST_CORRUPT:
        return "Frames cross and arrive changed. The wire or the bit timing.";
    case CAN_SELFTEST_LOSSY:
        return "Frames cross, some are lost, and the bus reported errors.";
    case CAN_SELFTEST_DROPPED:
        return "Frames arrived intact and were dropped unread. Not the wire.";
    default:
        return "";
    }
}

/*
 * What to check, cheapest first: a list that starts with the meter is a list
 * nobody finishes.
 *
 * Terminators are last under SILENT and first under CORRUPT on purpose. A bus
 * with no terminator at all usually still carries frames over half a metre;
 * one with a terminator missing reflects, and reflection is what alters a
 * frame rather than stopping it.
 */
static int checks_for(can_selftest_verdict_t v, const char *out[LIST_MAX])
{
    switch (v) {
    case CAN_SELFTEST_SILENT:
        out[0] = "Is the coprocessor powered? Its own LED, not the panel's.";
        out[1] = "CANH to CANH and CANL to CANL, not crossed.";
        out[2] = "Both boards flashed from the same release.";
        out[3] = "Terminators: about 60 ohms across CANH and CANL,";
        out[4] = "  measured with the bench switched off.";
        return 5;
    case CAN_SELFTEST_CORRUPT:
        out[0] = "Terminators: about 60 ohms across CANH and CANL with the";
        out[1] = "  bench off. 120 ohms means one of the two is missing.";
        out[2] = "Keep the branch to each board under 30 cm.";
        out[3] = "CANH and CANL twisted together, away from motor leads.";
        return 4;
    case CAN_SELFTEST_LOSSY:
        out[0] = "Terminators: about 60 ohms across CANH and CANL, bench off.";
        out[1] = "Total bus under 5 m, and each branch under 30 cm.";
        out[2] = "CANH and CANL twisted together, away from motor leads.";
        out[3] = "A connector that is only nearly seated behaves like this.";
        return 4;
    case CAN_SELFTEST_DROPPED:
        out[0] = "The wiring is not the fault: the frames arrived intact.";
        out[1] = "Something stopped reading them in time. Please report this";
        out[2] = "  with the numbers on the right.";
        return 3;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ input */

static bool in_ack(int16_t x, int16_t y)
{
    return gfx_rect_contains(gfx_rect_make(ACK_X, ACK_Y, ACK_W, ACK_H), x, y);
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    switch (evt->type) {
    case TOUCH_EVENT_DOWN:
        if (!s.have_press && in_ack(evt->point.x, evt->point.y)) {
            s.have_press = true;
            s.press_id   = evt->point.id;
            s.pressed    = true;
            s.held_s     = 0.0f;
            s.fired      = false;
        }
        break;
    case TOUCH_EVENT_MOVE:
        /* A finger that slides off the button abandons the hold, as ARM's
         * does: the gesture is contact with the control, not with the
         * panel. */
        if (s.have_press && evt->point.id == s.press_id
            && !in_ack(evt->point.x, evt->point.y)) {
            s.pressed = false;
            s.held_s  = 0.0f;
        }
        break;
    case TOUCH_EVENT_UP:
    default:
        if (s.have_press && evt->point.id == s.press_id) {
            s.have_press = false;
            s.pressed    = false;
            s.held_s     = 0.0f;
        }
        break;
    }
}

static void tick(float dt_s)
{
    if (s.pressed && !s.fired) {
        s.held_s += dt_s;
        if (s.held_s >= UI_HOLD_S) {
            /*
             * The hold is the acknowledgement: it completes here rather than
             * on the release, so letting go early acknowledges nothing.
             */
            s.held_s     = UI_HOLD_S;
            s.fired      = true;
            s.ack        = true;
            s.flash_left = UI_HOLD_FLASH_FRAMES;
        }
    }
}

/* ---------------------------------------------------------------- drawing */

/*
 * Danger red while it is waiting to be held, fading to the OK green it
 * settles on, and the same two-frame flash ARM gives as the command goes.
 * The direction is the other way round from ARM because what it does is the
 * other way round: this one leaves a fault behind rather than entering one.
 */
static gfx_color_t ack_fill(void)
{
    if (s.flash_left > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_OK), s.flash_left);
    }
    return ui_hold_fill(ui_theme_color(UI_C_DANGER),
                        ui_theme_color(UI_C_OK), s.held_s);
}

static void flash_advance(void)
{
    if (s.flash_left > 0) {
        --s.flash_left;
    }
}

static void draw_numbers(gfx_canvas_t *c)
{
    int y = VERDICT_Y;
    gfx_text(c, NUM_X, y, "MEASURED", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    y += 24;

    char line[48];
    snprintf(line, sizeof(line), "sent      %lu", (unsigned long)s.r.sent);
    gfx_text(c, NUM_X, y, line, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += 20;
    snprintf(line, sizeof(line), "returned  %lu", (unsigned long)s.r.echoed);
    gfx_text(c, NUM_X, y, line, UI_FONT_LABEL,
             s.r.echoed > 0 ? ui_theme_color(UI_C_OK)
                            : ui_theme_color(UI_C_DANGER), 1);
    y += 20;
    snprintf(line, sizeof(line), "altered   %lu", (unsigned long)s.r.corrupt);
    gfx_text(c, NUM_X, y, line, UI_FONT_LABEL,
             s.r.corrupt > 0 ? ui_theme_color(UI_C_DANGER)
                             : ui_theme_color(UI_C_TEXT), 1);
    y += 20;
    snprintf(line, sizeof(line), "lost      %lu", (unsigned long)s.r.lost);
    gfx_text(c, NUM_X, y, line, UI_FONT_LABEL,
             s.r.lost > 0 ? ui_theme_color(UI_C_WARN)
                          : ui_theme_color(UI_C_TEXT), 1);
    y += 30;

    gfx_text(c, NUM_X, y, "THIS PANEL", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    y += 22;
    snprintf(line, sizeof(line), "tx err %lu  rx err %lu",
             (unsigned long)s.r.tx_errors, (unsigned long)s.r.rx_errors);
    gfx_text(c, NUM_X, y, line, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += 20;
    snprintf(line, sizeof(line), "bus err %lu", (unsigned long)s.r.bus_errors);
    gfx_text(c, NUM_X, y, line, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += 20;
    if (s.r.bus_off) {
        gfx_text(c, NUM_X, y, "BUS OFF", UI_FONT_LABEL,
                 ui_theme_color(UI_C_DANGER), 1);
    }
    y += 30;

    gfx_text(c, NUM_X, y, "COPROCESSOR", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    y += 22;
    if (!s.r.have_remote) {
        /*
         * Not an error line of its own: a far end that cannot be asked is
         * already the whole of what SILENT says, and repeating it in red
         * would read as a second fault.
         */
        gfx_text(c, NUM_X, y, "did not answer", UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        return;
    }
    gfx_text(c, NUM_X, y, s.r.remote_up ? "CAN up" : "CAN DOWN",
             UI_FONT_LABEL,
             s.r.remote_up ? ui_theme_color(UI_C_OK)
                           : ui_theme_color(UI_C_DANGER), 1);
    y += 20;
    snprintf(line, sizeof(line), "tx err %u  rx err %u",
             (unsigned)s.r.remote_tx_errors, (unsigned)s.r.remote_rx_errors);
    gfx_text(c, NUM_X, y, line, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += 20;
    snprintf(line, sizeof(line), "flags 0x%02X", (unsigned)s.r.remote_flags);
    gfx_text(c, NUM_X, y, line, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += 20;
    if (s.r.remote_overflows > 0u) {
        snprintf(line, sizeof(line), "dropped %u", s.r.remote_overflows);
        gfx_text(c, NUM_X, y, line, UI_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
    }
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));

        /* The heading, on the danger colour: this is the one screen that
         * says the bench is not usable as it stands. */
        gfx_fill_rect(c, 0, 0, W, BAR_H, ui_theme_color(UI_C_DANGER));
        gfx_text(c, COL_X, 14, "CAN BUS FAULT", UI_FONT_HEAD, GFX_WHITE, 1);

        /* The verdict is the heading of the page, not a caption: it is what
         * the operator repeats when they ask somebody about it. */
        gfx_text(c, COL_X, VERDICT_Y, can_selftest_text(s.r.verdict),
                 UI_FONT_HEAD, ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, COL_X, MEANING_Y, meaning_of(s.r.verdict),
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);

        gfx_text(c, COL_X, HEAD_Y, "CHECK, IN THIS ORDER",
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1);

        const char *checks[LIST_MAX] = { 0 };
        const int n = checks_for(s.r.verdict, checks);
        for (int i = 0; i < n; ++i) {
            /* A continuation line is indented and carries no bullet: it is
             * the same instruction, not the next one. */
            const bool cont = (checks[i][0] == ' ');
            if (!cont) {
                gfx_fill_rect(c, COL_X + 2, LIST_Y + i * LIST_PITCH + 6, 5, 5,
                              ui_theme_color(UI_C_ACCENT));
            }
            gfx_text(c, COL_X + 16, LIST_Y + i * LIST_PITCH, checks[i],
                     UI_FONT_LABEL,
                     cont ? ui_theme_color(UI_C_TEXT_DIM)
                          : ui_theme_color(UI_C_TEXT), 1);
        }

        /*
         * And what acknowledging costs, because the button is not a
         * dismissal: the bench is still on a bus that does not work.
         */
        gfx_text(c, COL_X, NOTE_Y,
                 "Acknowledging leaves the bench in simulation. It will show",
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, COL_X, NOTE_Y + 20,
                 "numbers, and nothing will drive an output.",
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, COL_X, NOTE_Y + 44,
                 "The test runs again at every start-up.",
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1);

        ui_rule(c, COL_X, ACK_Y - 20, ACK_W, ui_theme_color(UI_C_EDGE));
        draw_numbers(c);
        s.drawn_mask |= bit;
    }

    /*
     * The button is the only thing that moves, and it is repainted every
     * frame: the hold fades across two seconds, so a frame that skipped it
     * would be a step in the fade.
     */
    ui_button(c, gfx_rect_make(ACK_X, ACK_Y, ACK_W, ACK_H),
              s.fired ? "ACKNOWLEDGED" : "HOLD 2 s TO ACKNOWLEDGE",
              ack_fill(), false, true);
    flash_advance();
}

static const ui_screen_t k_screen = {
    .title  = "CAN BUS FAULT",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = tick,
    .event  = event,
    .render = render,
};

const ui_screen_t *busfault_screen(void) { return &k_screen; }
