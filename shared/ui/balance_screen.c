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

/*
 * The same two sensors on a complete aircraft, where almost nothing is
 * chosen: the rigid faces are wherever the designer put them, the cowl is in
 * the way, and the whole machine is free to rock unless you stop it.
 */
/*
 * The same two sensors on a complete aircraft, where almost nothing is
 * chosen: the rigid faces are wherever the designer put them, the cowl is in
 * the way, and the whole machine is free to rock unless you stop it.
 */
static void draw_aircraft(gfx_canvas_t *c)
{
    const gfx_color_t skin  = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const gfx_color_t ink   = ui_theme_color(UI_C_ACCENT);
    const gfx_color_t warn  = ui_theme_color(UI_C_WARN);
    const gfx_color_t faint = ui_theme_color(UI_C_TEXT_FAINT);

    const int ground = 372;
    const int axis   = 214;              /* the thrust line          */
    const int fw     = 268;              /* the firewall             */
    const int prop   = 388;              /* the disc, edge on        */

    gfx_hline(c, 24, ground, 452, faint);
    for (int x = 28; x < 476; x += 16) {
        gfx_capsule_aa(c, x, ground + 2, x - 6, ground + 10, 1, faint);
    }

    /*
     * The fuselage: a deep nose and a boom that tapers away from it, rather
     * than one box.  It only has to be recognisable as an aeroplane -- the
     * subject is where two sensors go, not the model.
     */
    gfx_fill_round_rect(c, 28, axis - 16, 120, 34, 12, skin);
    gfx_draw_round_rect(c, 28, axis - 16, 120, 34, 12, edge);
    gfx_fill_round_rect(c, 100, axis - 32, fw - 100, 64, 16, skin);
    gfx_draw_round_rect(c, 100, axis - 32, fw - 100, 64, 16, edge);

    /* Wing at the root, tailplane and fin at the back. */
    gfx_fill_round_rect(c, 120, axis + 24, 130, 12, 5,
                        ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, 120, axis + 24, 130, 12, 5, edge);
    gfx_fill_round_rect(c, 24, axis - 4, 56, 10, 4,
                        ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, 24, axis - 4, 56, 10, 4, edge);
    gfx_capsule_aa(c, 34, axis - 14, 50, axis - 58, 13, skin);

    /*
     * The cowl, drawn as the separate piece it is.  It is a fairing: screwed
     * to a former, often on rubber, and free to move relative to the thing
     * whose vibration you want.
     */
    gfx_fill_round_rect(c, fw, axis - 36, 96, 72, 12,
                        ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, fw, axis - 36, 96, 72, 12, edge);
    gfx_fill_round_rect(c, fw + 20, axis - 20, 22, 16, 3,
                        ui_theme_color(UI_C_BG));

    /* The firewall behind it: the one rigid face at this end of the model. */
    for (int y = axis - 32; y < axis + 32; y += 8) {
        gfx_vline(c, fw, y, 5, ink);
    }
    /* The motor inside, shown faintly because you cannot get at it. */
    gfx_fill_round_rect(c, fw + 16, axis - 17, 50, 34, 6,
                        gfx_lerp(ui_theme_color(UI_C_PANEL), skin, 90));

    /* Spinner and disc, clear of the cowl. */
    gfx_capsule_aa(c, fw + 96, axis, prop - 8, axis, 10, skin);
    gfx_capsule_aa(c, prop, axis - 12, prop, axis - 86, 12, skin);
    gfx_capsule_aa(c, prop, axis + 12, prop, axis + 86, 12, skin);
    gfx_fill_circle_aa(c, prop, axis, 16, skin);
    gfx_fill_circle_aa(c, prop, axis, 5, ui_theme_color(UI_C_PANEL));

    /*
     * The accelerometer on the firewall, flat.
     *
     * Flat is fine and worth saying: the chip has three axes, and mounting it
     * against the firewall puts two of them in the firewall's plane, which is
     * across the shaft.  Use one of those and ignore the third.
     */
    gfx_fill_round_rect(c, fw - 34, axis - 44, 32, 20, 3, ink);
    gfx_draw_round_rect(c, fw - 34, axis - 44, 32, 20, 3, edge);
    const int ax = fw - 18;
    gfx_capsule_aa(c, ax, axis - 76, ax, axis - 50, 3, ink);
    gfx_capsule_aa(c, ax - 7, axis - 69, ax, axis - 78, 3, ink);
    gfx_capsule_aa(c, ax + 7, axis - 69, ax, axis - 78, 3, ink);

    /*
     * The mark and its sensor.  With a cowl on there is nothing else to look
     * at, and the spinner will do: an index only has to give one pulse a turn
     * at a steady phase, and unlike the accelerometer it does not care how
     * rigidly it is held.
     */
    gfx_fill_round_rect(c, prop + 8, axis - 5, 10, 10, 2,
                        ui_theme_color(UI_C_BG));
    gfx_draw_round_rect(c, prop + 8, axis - 5, 10, 10, 2, warn);
    gfx_fill_round_rect(c, 424, 250, 30, 24, 3, warn);
    gfx_draw_round_rect(c, 424, 250, 30, 24, 3, edge);
    gfx_capsule_aa(c, 439, 274, 439, ground, 4, faint);
    for (int i = 0; i < 4; ++i) {
        const float t = (float)i / 4.0f;
        gfx_fill_circle_aa(c, 424 - (int)(16.0f * t),
                           248 - (int)(26.0f * t), 2, warn);
    }

    /* Tied down at the tail, because a machine free to rock is a spring
     * nobody chose. */
    gfx_capsule_aa(c, 60, axis + 16, 52, ground, 3, warn);
    gfx_capsule_aa(c, 42, ground - 4, 62, ground - 4, 6, warn);

    callout(c, ax, axis - 34, 24, 62, "ACCELEROMETER", ink);
    callout(c, fw, axis + 18, 150, 322, "FIREWALL", ink);
    /* Out to the right of the disc: any leader that leaves the mark upward
     * runs the length of a blade. */
    callout(c, prop + 13, axis, 418, 148, "ONE MARK", warn);
    callout(c, 52, ground - 12, 132, 288, "TIED DOWN", warn);
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

#define AIR_RULES 6
static const rule_t k_air_rules[] = {
    { "THE FIREWALL, NOT THE COWL",
      "A cowl is a fairing. It moves",
      "relative to everything." },
    { "FLAT IS FINE",
      "Mount it flat and use an axis",
      "in the firewall's plane." },
    { "NEAR THE BOLTS",
      "Same rule as a rig: joints",
      "between it and the bearing." },
    { "THE MARK CAN MOVE",
      "An index only needs one pulse",
      "a turn, at a steady phase." },
    { "TIE THE TAIL DOWN",
      "An aircraft free to rock is",
      "a spring you did not want." },
    { "LEAD OUT OF THE WASH",
      "And secured. A flapping lead",
      "is a second accelerometer." },
};

static void draw_rules(gfx_canvas_t *c, const rule_t *rules, int count)
{
    const int x = RCARD_X + 14;
    gfx_text(c, x, BODY_Y + 12, "GETTING IT WRONG", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);

    for (int i = 0; i < count; ++i) {
        const int y = BODY_Y + 32 + i * 58;
        /* Bounded so the compiler can see it: count is small, but the
         * format is not told so. */
        char n[16];
        snprintf(n, sizeof(n), "%d", (i + 1) & 0xFF);
        gfx_fill_round_rect(c, x, y - 2, 20, 20, 4,
                            ui_theme_color(UI_C_PANEL_HI));
        gfx_text_in(c, (gfx_rect_t){ (int16_t)x, (int16_t)(y + 1), 20, 16 },
                    n, UI_FONT_LABEL, ui_theme_color(UI_C_ACCENT), 1,
                    GFX_ALIGN_CENTER);
        gfx_text(c, x + 28, y, rules[i].head, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, x + 28, y + 20, rules[i].line1, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, x + 28, y + 36, rules[i].line2, UI_FONT_LABEL,
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
        draw_rules(c, k_rig_rules, RIG_RULES);
    } else {
        draw_aircraft(c);
        draw_rules(c, k_air_rules, AIR_RULES);
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
