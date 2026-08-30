/*
 * Balancing, starting with where the sensors go.
 *
 * The measurement is easy and the mounting is not.  A balance answer is a
 * magnitude and an angle -- "add this much, there" -- and every part of that
 * comes out of two sensors whose placement decides whether the answer is
 * worth anything.  A vibration sensor on a compliant mount reads a filtered
 * version of what the bearing did; an index mark on a blade instead of the
 * spinner reads N pulses a turn and gives an angle that is wrong by a whole
 * blade spacing.  Neither failure looks like a failure on screen.
 *
 * So this screen starts as a diagram, and the numbers come later.
 *
 * SPDX-License-Identifier: MIT
 */

#include "balance_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_tabs.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)

#define PAD     6
#define TAB_Y   8
#define TAB_H   28
#define BODY_Y  44

#define LCARD_W 494
#define RCARD_X (PAD + LCARD_W + 8)
#define RCARD_W (W - RCARD_X - PAD)

/* The rig, drawn side on: a post, an arm, a motor, and the disc it turns. */
#define BASE_Y   356
#define POST_X   150
#define POST_W   40
#define ARM_Y    196
#define ARM_H    28
#define MOTOR_X  300
#define MOTOR_W  62
#define SHAFT_Y  210
#define SPIN_X   406
#define BLADE_L  92

static const char *const k_tabs[] = { "TEST RIG", "AIRCRAFT" };

static struct {
    ui_tabs_t tabs;
    uint32_t  rev;
    uint32_t  drawn[2];
    unsigned  drawn_mask;
} s;

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
    ui_tabs_init(&s.tabs, k_tabs, BALANCE_PANE_COUNT,
                 (gfx_rect_t){ PAD + 3, TAB_Y, 260, TAB_H });
}

static void event(const touch_event_t *evt)
{
    if (ui_tabs_event(&s.tabs, evt)) {
        ++s.rev;
    }
}

/* ----------------------------------------------------------------- drawing */

/* A callout: a dot on the thing, a line away from it, and a word. */
/*
 * A dot on the part, a leader to a stated place, and the word there.
 *
 * The label's position is given rather than worked out: placed automatically
 * from the leader's direction, two of these ran their leaders straight across
 * the propeller they were naming.
 */
static void callout(gfx_canvas_t *c, int px, int py, int lx, int ly,
                    const char *text, gfx_color_t ink)
{
    const int w = (int)strlen(text) * 8;
    /* The leader meets the label at whichever end is nearer the part. */
    const int join_x = (px > lx + w) ? lx + w + 4 : lx - 4;
    gfx_capsule_aa(c, px, py, join_x, ly + 8, 2, ink);
    gfx_fill_circle_aa(c, px, py, 4, ink);
    gfx_text(c, lx, ly, text, UI_FONT_LABEL, ink, 1);
}

static void draw_rig(gfx_canvas_t *c)
{
    const gfx_color_t steel = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const gfx_color_t ink   = ui_theme_color(UI_C_ACCENT);
    const gfx_color_t warn  = ui_theme_color(UI_C_WARN);

    /* The bench itself, drawn quietly: it is the thing everything else is
     * bolted to and nothing here is about it. */
    gfx_fill_round_rect(c, 60, BASE_Y, 380, 18, 4, steel);
    gfx_draw_round_rect(c, 60, BASE_Y, 380, 18, 4, edge);
    for (int x = 70; x < 440; x += 22) {
        gfx_vline(c, x, BASE_Y + 18, 8, ui_theme_color(UI_C_GRID));
    }

    gfx_fill_round_rect(c, POST_X, 150, POST_W, BASE_Y - 150, 4, steel);
    gfx_draw_round_rect(c, POST_X, 150, POST_W, BASE_Y - 150, 4, edge);

    gfx_fill_round_rect(c, POST_X + POST_W, ARM_Y, MOTOR_X - POST_X - POST_W,
                        ARM_H, 4, steel);
    gfx_draw_round_rect(c, POST_X + POST_W, ARM_Y,
                        MOTOR_X - POST_X - POST_W, ARM_H, 4, edge);

    /*
     * An outrunner: the small static end bolted to the arm, and the bell --
     * the whole outer can -- turning in front of it.
     */
    gfx_fill_round_rect(c, MOTOR_X, SHAFT_Y - 22, 22, 44, 4,
                        ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, MOTOR_X, SHAFT_Y - 22, 22, 44, 4, edge);

    const int bell_x = MOTOR_X + 20;
    const int bell_w = 54;
    gfx_fill_round_rect(c, bell_x, SHAFT_Y - 38, bell_w, 76, 8, steel);
    gfx_draw_round_rect(c, bell_x, SHAFT_Y - 38, bell_w, 76, 8, edge);
    /* Its cooling slots, which are also what tells you it is the part that
     * turns. */
    for (int i = 0; i < 4; ++i) {
        gfx_fill_round_rect(c, bell_x + 8 + i * 11, SHAFT_Y - 26, 5, 52, 2,
                            ui_theme_color(UI_C_GRID_STRONG));
    }
    gfx_capsule_aa(c, bell_x + bell_w, SHAFT_Y, SPIN_X, SHAFT_Y, 10, steel);

    /* The disc, edge on: two blades and the hub between them. */
    gfx_capsule_aa(c, SPIN_X, SHAFT_Y - 14, SPIN_X, SHAFT_Y - BLADE_L, 13,
                   ui_theme_color(UI_C_PANEL_HI));
    gfx_capsule_aa(c, SPIN_X, SHAFT_Y + 14, SPIN_X, SHAFT_Y + BLADE_L, 13,
                   ui_theme_color(UI_C_PANEL_HI));
    /* A hub, not a hole: filled light with a dark centre.  Drawn the other
     * way round it read as a gap in the blades. */
    gfx_fill_circle_aa(c, SPIN_X, SHAFT_Y, 18, steel);
    gfx_fill_circle_aa(c, SPIN_X, SHAFT_Y, 6, ui_theme_color(UI_C_PANEL));

    /*
     * The accelerometer, on the arm and hard against the motor.
     *
     * Everything between the bearing and the sensor is a spring, and a spring
     * is a filter you did not choose.  On the arm by the motor is the last
     * place that is still rigid; on the post it would be reading the post.
     */
    gfx_fill_round_rect(c, MOTOR_X - 42, ARM_Y - 22, 34, 22, 3, ink);
    gfx_draw_round_rect(c, MOTOR_X - 42, ARM_Y - 22, 34, 22, 3, edge);
    /* Its sensitive axis, across the shaft rather than along it: that is the
     * direction an out-of-balance disc pulls. */
    const int ax = MOTOR_X - 25;
    gfx_capsule_aa(c, ax, ARM_Y - 54, ax, ARM_Y - 26, 3, ink);
    gfx_capsule_aa(c, ax - 7, ARM_Y - 47, ax, ARM_Y - 56, 3, ink);
    gfx_capsule_aa(c, ax + 7, ARM_Y - 47, ax, ARM_Y - 56, 3, ink);
    gfx_capsule_aa(c, ax, ARM_Y + 30, ax, ARM_Y + 58, 3, ink);
    gfx_capsule_aa(c, ax - 7, ARM_Y + 51, ax, ARM_Y + 60, 3, ink);
    gfx_capsule_aa(c, ax + 7, ARM_Y + 51, ax, ARM_Y + 60, 3, ink);

    /*
     * The mark goes on the bell, and the sensor looks straight up at it.
     *
     * Not the spinner: a rig often runs without one, and a beam aimed at the
     * nose has to come from in front, across the disc.  The bell turns with
     * the shaft, is rigid, is there whichever prop is fitted, and can be
     * watched from underneath where nothing is in the way.
     */
    const int mark_x = bell_x + bell_w / 2;
    /* Drawn dark with a bright edge, because it is a pen line and not a
     * flag: anything stuck on the bell is mass, on the one part whose mass
     * is the thing being measured. */
    gfx_fill_round_rect(c, mark_x - 9, SHAFT_Y + 30, 18, 10, 2,
                        ui_theme_color(UI_C_BG));
    gfx_draw_round_rect(c, mark_x - 9, SHAFT_Y + 30, 18, 10, 2, warn);

    gfx_fill_round_rect(c, mark_x - 17, 296, 34, 24, 3, warn);
    gfx_draw_round_rect(c, mark_x - 17, 296, 34, 24, 3, edge);
    for (int i = 0; i < 5; ++i) {
        gfx_fill_circle_aa(c, mark_x, 292 - i * 10, 2, warn);
    }

    callout(c, MOTOR_X - 25, ARM_Y - 12, 24, 62, "ACCELEROMETER", ink);
    /* Both labels go left, under the arm, which is the one large piece of
     * empty card that no leader has to cross anything to reach. */
    callout(c, mark_x, SHAFT_Y + 36, 196, 282, "ONE MARK", warn);
    callout(c, mark_x, 308, 196, 332, "OPTICAL", warn);
}

static void draw_aircraft(gfx_canvas_t *c)
{
    gfx_text_in(c, (gfx_rect_t){ PAD, (int16_t)(BODY_Y + 120),
                                 LCARD_W, 16 },
                "not drawn yet", UI_FONT_HEAD,
                ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_CENTER);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(PAD + 40),
                                 (int16_t)(BODY_Y + 156),
                                 (int16_t)(LCARD_W - 80), 16 },
                "a firewall has one rigid face and the",
                UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_CENTER);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(PAD + 40),
                                 (int16_t)(BODY_Y + 176),
                                 (int16_t)(LCARD_W - 80), 16 },
                "cowl is not it",
                UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_CENTER);
}

/* The rules, numbered, because each one is a way of getting it wrong. */
typedef struct { const char *head; const char *line1; const char *line2; }
    rule_t;

static const rule_t k_rig_rules[] = {
    { "ACROSS THE SHAFT",
      "An unbalanced disc pulls",
      "sideways, not along it." },
    { "AS CLOSE AS IT GOES",
      "Every joint before the sensor",
      "is a spring you did not pick." },
    { "BOLTED, NOT TAPED",
      "Foam eats the frequencies",
      "you came to measure." },
    { "ONE MARK A TURN",
      "On the motor bell. A blade",
      "gives one pulse per blade." },
    { "A PEN, NOT TAPE",
      "Anything stuck on is mass",
      "you would then measure." },
    { "OUT OF THE WASH",
      "A lead that flaps is a",
      "second accelerometer." },
};
#define RIG_RULES ((int)(sizeof(k_rig_rules) / sizeof(k_rig_rules[0])))

static void draw_rules(gfx_canvas_t *c)
{
    const int x = RCARD_X + 14;
    gfx_text(c, x, BODY_Y + 12, "GETTING IT WRONG", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);

    for (int i = 0; i < RIG_RULES; ++i) {
        const int y = BODY_Y + 32 + i * 58;
        char n[8];
        snprintf(n, sizeof(n), "%d", i + 1);
        gfx_fill_round_rect(c, x, y - 2, 20, 20, 4,
                            ui_theme_color(UI_C_PANEL_HI));
        gfx_text_in(c, (gfx_rect_t){ (int16_t)x, (int16_t)(y + 1), 20, 16 },
                    n, UI_FONT_LABEL, ui_theme_color(UI_C_ACCENT), 1,
                    GFX_ALIGN_CENTER);
        gfx_text(c, x + 28, y, k_rig_rules[i].head, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, x + 28, y + 20, k_rig_rules[i].line1, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, x + 28, y + 36, k_rig_rules[i].line2, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        s.drawn_mask |= bit;
    }
    if (s.drawn[buf] == s.rev) {
        return;
    }
    s.drawn[buf] = s.rev;
    gfx_clear(c, ui_theme_color(UI_C_BG));

    ui_card(c, (gfx_rect_t){ PAD, BODY_Y - 4, LCARD_W,
                             (int16_t)(H - BODY_Y - 2) },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ RCARD_X, BODY_Y - 4, RCARD_W,
                             (int16_t)(H - BODY_Y - 2) },
            ui_theme_color(UI_C_PANEL));
    ui_tabs_render(&s.tabs, c);

    if (s.tabs.selected == BALANCE_PANE_RIG) {
        draw_rig(c);
        draw_rules(c);
    } else {
        draw_aircraft(c);
        draw_rules(c);
    }
}

static const ui_screen_t k_screen = {
    .title  = "BALANCE",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = event,
    .render = render,
};

const ui_screen_t *balance_screen(void) { return &k_screen; }
