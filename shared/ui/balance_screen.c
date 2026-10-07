/*
 * The balance screen: sensor placement guides and the correction readout.
 *
 * A balance answer is a magnitude and an angle, both derived from two
 * sensors whose placement decides whether the answer is valid.  A vibration
 * sensor on a compliant mount reads a filtered version of the bearing's
 * motion; an index mark on a blade instead of the bell gives one pulse per
 * blade and an angle wrong by a blade spacing.  Neither error is visible on
 * screen, so the screen carries the placement diagrams.
 *
 * SPDX-License-Identifier: MIT
 */

#include "balance_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_tabs.h"
#include "ui_text.h"
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

static const ui_text_id_t k_tabs[] = { TX_BAL_TAB_BALANCE, TX_BAL_TAB_RIG,
                                       TX_BAL_TAB_AIRCRAFT };

/*
 * The rotor, drawn face on.
 *
 * The blade count sets where the blades are drawn, and the screen names the
 * two blades the correction falls between: an angle from the index mark is
 * actionable only relative to a blade.
 */
#define DISC_CX  238
#define DISC_CY  238
#define DISC_R   132
#define HUB_R    26

static struct {
    ui_tabs_t tabs;
    int   blades;                /* 2 to 6                        */
    int   rotor;                 /* propeller or ducted fan       */
    gfx_rect_t rotor_btn[ROTOR_COUNT];
    gfx_rect_t blade_dn, blade_up;
    uint32_t  rev;
    uint32_t  drawn[2];
    unsigned  drawn_mask;
} s;

void balance_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
    s.blades = 2;
    s.rotor  = ROTOR_PROP;
    ui_tabs_init_text(&s.tabs, k_tabs, BALANCE_PANE_COUNT,
                 (gfx_rect_t){ PAD + 3, TAB_Y, 390, TAB_H });

    const int x = RCARD_X + 14;
    const int w = RCARD_W - 28;
    for (int i = 0; i < ROTOR_COUNT; ++i) {
        s.rotor_btn[i] = (gfx_rect_t){ (int16_t)(x + i * (w / 2 + 4)),
                                       (int16_t)(BODY_Y + 34),
                                       (int16_t)(w / 2 - 4), 30 };
    }
    s.blade_dn = (gfx_rect_t){ (int16_t)(x + w - 78), (int16_t)(BODY_Y + 96),
                               34, 28 };
    s.blade_up = (gfx_rect_t){ (int16_t)(x + w - 34), (int16_t)(BODY_Y + 96),
                               34, 28 };
}

int  balance_screen_blades(void) { return s.blades; }
int  balance_screen_rotor(void)  { return s.rotor; }

/*
 * Where the correction goes, modelled until a sensor is fitted.
 *
 * 265 degrees lies between two blades at every count from 2 to 6, so the
 * between-blades wording is exercised.
 */
float balance_screen_angle(void) { return 265.0f; }

static void event(const touch_event_t *evt)
{
    if (ui_tabs_event(&s.tabs, evt)) {
        ++s.rev;
        return;
    }
    if (evt == NULL || evt->type != TOUCH_EVENT_DOWN
        || s.tabs.selected != BALANCE_PANE_MEASURE) {
        return;
    }
    const int px = evt->point.x, py = evt->point.y;
    for (int i = 0; i < ROTOR_COUNT; ++i) {
        if (gfx_rect_contains(s.rotor_btn[i], px, py)) {
            if (s.rotor != i) { s.rotor = i; ++s.rev; }
            return;
        }
    }
    /* Two to six.  Clamped, like every other stepper on this bench. */
    if (gfx_rect_contains(s.blade_dn, px, py)) {
        if (s.blades > 2) { --s.blades; ++s.rev; }
    } else if (gfx_rect_contains(s.blade_up, px, py)) {
        if (s.blades < 6) { ++s.blades; ++s.rev; }
    }
}

/* ----------------------------------------------------------------- drawing */

/*
 * A dot on the part, a leader to a stated place, and the word there.
 *
 * The label position is a parameter rather than derived from the leader
 * direction, so a leader can be routed clear of the propeller.
 */
static void callout(gfx_canvas_t *c, int px, int py, int lx, int ly,
                    const char *text, gfx_color_t ink)
{
    const int w = gfx_text_width(UI_FONT_LABEL, text, 1);
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

    /* The bench itself, in the faint colour: everything else is bolted to
     * it and nothing here is about it. */
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
    /* A hub, not a hole: filled light with a dark centre.  A dark disc with a
     * light centre reads as a gap in the blades. */
    gfx_fill_circle_aa(c, SPIN_X, SHAFT_Y, 18, steel);
    gfx_fill_circle_aa(c, SPIN_X, SHAFT_Y, 6, ui_theme_color(UI_C_PANEL));

    /*
     * The accelerometer, on the arm and hard against the motor: everything
     * between the bearing and the sensor is a spring, and the arm by the
     * motor is the last rigid place.
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
     * the shaft, is rigid, is present whichever prop is fitted, and can be
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

    callout(c, MOTOR_X - 25, ARM_Y - 12, 24, 62, TR(BAL_ACCELEROMETER), ink);
    /* Both labels go left, under the arm, which is the one large piece of
     * empty card that no leader has to cross anything to reach. */
    callout(c, mark_x, SHAFT_Y + 36, 196, 282, TR(BAL_ONE_MARK), warn);
    callout(c, mark_x, 308, 196, 332, TR(BAL_OPTICAL), warn);
}

/*
 * The same two sensors on a complete aircraft, where the rigid faces are
 * wherever the designer put them, the cowl is in the way, and the airframe
 * rocks unless it is tied down.
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
     * The fuselage: a deep nose and a tapering boom, recognisable as an
     * aeroplane.  The subject is the sensor placement.
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
     * The cowl, drawn as a separate piece: a fairing screwed to a former,
     * often on rubber, and free to move relative to the motor mount.  Drawn
     * short, because on most electric models the outrunner stands proud of
     * it.
     */
    gfx_fill_round_rect(c, fw, axis - 36, 62, 72, 12,
                        ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, fw, axis - 36, 62, 72, 12, edge);

    /* The firewall behind it: the one rigid face at this end of the model. */
    for (int y = axis - 32; y < axis + 32; y += 8) {
        gfx_vline(c, fw, y, 5, ink);
    }

    /* The bell, ahead of the cowl and turning. */
    const int bell_x = fw + 58;
    const int bell_w = 50;
    gfx_fill_round_rect(c, bell_x, axis - 32, bell_w, 64, 8, skin);
    gfx_draw_round_rect(c, bell_x, axis - 32, bell_w, 64, 8, edge);
    for (int i = 0; i < 4; ++i) {
        gfx_fill_round_rect(c, bell_x + 7 + i * 10, axis - 22, 5, 44, 2,
                            ui_theme_color(UI_C_GRID_STRONG));
    }

    /* Spinner and disc. */
    gfx_capsule_aa(c, bell_x + bell_w, axis, prop - 6, axis, 9, skin);
    gfx_capsule_aa(c, prop, axis - 12, prop, axis - 86, 12, skin);
    gfx_capsule_aa(c, prop, axis + 12, prop, axis + 86, 12, skin);
    gfx_fill_circle_aa(c, prop, axis, 15, skin);
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
     * The mark on the bell here too: a spinner comes off, and every time it
     * is refitted it lands at a new angle, taking the phase reference of the
     * previous balance with it.  The bell is part of the rotor.
     */
    const int mark_x = bell_x + bell_w / 2;
    gfx_fill_round_rect(c, mark_x - 9, axis + 24, 18, 10, 2,
                        ui_theme_color(UI_C_BG));
    gfx_draw_round_rect(c, mark_x - 9, axis + 24, 18, 10, 2, warn);

    gfx_fill_round_rect(c, mark_x - 15, 300, 30, 24, 3, warn);
    gfx_draw_round_rect(c, mark_x - 15, 300, 30, 24, 3, edge);
    gfx_capsule_aa(c, mark_x, 324, mark_x, ground, 4, faint);
    for (int i = 0; i < 4; ++i) {
        gfx_fill_circle_aa(c, mark_x, 294 - i * 10, 2, warn);
    }

    /* Tied down at the tail: a free-standing airframe is a spring in series
     * with the measurement. */
    gfx_capsule_aa(c, 60, axis + 16, 52, ground, 3, warn);
    gfx_capsule_aa(c, 42, ground - 4, 62, ground - 4, 6, warn);

    callout(c, ax, axis - 34, 24, 62, TR(BAL_ACCELEROMETER), ink);
    callout(c, fw, axis + 18, 132, 288, TR(BAL_FIREWALL), ink);
    /* Down and left: between the bell and the disc there is no gap for a
     * leader to climb through. */
    callout(c, mark_x, axis + 30, 236, 344, TR(BAL_ONE_MARK), warn);
    callout(c, 52, ground - 12, 96, 344, TR(BAL_TIED_DOWN), warn);
}

/* A point on the disc, measured from the index mark and drawn with zero at
 * the top so the picture agrees with the number. */
static void disc_at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    const float a = (90.0f - deg) * k;
    *x = DISC_CX + (int)((float)r * cosf(a) + 0.5f);
    *y = DISC_CY - (int)((float)r * sinf(a) + 0.5f);
}

static void draw_measure(gfx_canvas_t *c)
{
    const gfx_color_t skin  = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const gfx_color_t ink   = ui_theme_color(UI_C_ACCENT);
    const gfx_color_t warn  = ui_theme_color(UI_C_WARN);
    const gfx_color_t faint = ui_theme_color(UI_C_TEXT_FAINT);

    /* A duct, if there is one.  It is the reason an EDF is a different job:
     * you cannot reach a blade tip, so the correction goes on the hub. */
    /*
     * The rings are pairs of filled discs, not thin arcs.  An arc closed on
     * itself lays some pixels twice and misses others, and at 1 or 2 px wide
     * that stipples.  They are safe to fill because they are drawn before
     * the blades.
     */
    const gfx_color_t card = ui_theme_color(UI_C_PANEL);
    if (s.rotor == ROTOR_EDF) {
        gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R + 26, edge);
        gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R + 22, card);
        gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R + 13,
                           ui_theme_color(UI_C_GRID));
        gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R + 11, card);
    }
    gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R,
                       ui_theme_color(UI_C_GRID));
    gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R - 1, card);

    for (int i = 0; i < s.blades; ++i) {
        const float a = (float)i * 360.0f / (float)s.blades;
        int rx, ry, tx, ty;
        /* Two strokes of falling width: a blade is wide at the root and
         * narrow at the tip, and a single capsule reads as a stick. */
        int mx2, my2;
        disc_at(a, HUB_R - 4, &rx, &ry);
        disc_at(a, (DISC_R + HUB_R) / 2, &mx2, &my2);
        disc_at(a, DISC_R - 6, &tx, &ty);
        gfx_capsule_aa(c, rx, ry, mx2, my2, 20, skin);
        gfx_capsule_aa(c, mx2, my2, tx, ty, 13, skin);
        /* Numbered from the index mark, which is what makes an angle into an
         * instruction rather than a reading. */
        char n[16];
        snprintf(n, sizeof(n), "%d", (i + 1) & 0xFF);
        int lx, ly;
        disc_at(a, DISC_R + (s.rotor == ROTOR_EDF ? 38 : 18), &lx, &ly);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(lx - 12), (int16_t)(ly - 8),
                                     24, 16 },
                    n, UI_FONT_LABEL, faint, 1, GFX_ALIGN_CENTER);
    }

    gfx_fill_circle_aa(c, DISC_CX, DISC_CY, HUB_R, skin);
    gfx_fill_circle_aa(c, DISC_CX, DISC_CY, 8, ui_theme_color(UI_C_PANEL));

    /* The index mark, at zero, because every angle here is from it. */
    int mx, my;
    disc_at(0.0f, HUB_R - 8, &mx, &my);
    gfx_fill_round_rect(c, mx - 5, my - 4, 10, 8, 2, ui_theme_color(UI_C_BG));
    gfx_draw_round_rect(c, mx - 5, my - 4, 10, 8, 2, warn);

    /* And where the mass goes. */
    const float ang = balance_screen_angle();
    int ax, ay, bx, by;
    disc_at(ang, HUB_R + 6, &ax, &ay);
    disc_at(ang, DISC_R - 14, &bx, &by);
    gfx_capsule_aa(c, ax, ay, bx, by, 3, ink);
    /* A band of arc at the answer, so the eye finds it before the numbers. */
    gfx_arc(c, DISC_CX, DISC_CY, DISC_R - 14, 8, 90.0f - ang - 16.0f,
            90.0f - ang + 16.0f, ink);
    gfx_fill_circle_aa(c, bx, by, 9, ink);
    gfx_fill_circle_aa(c, bx, by, 4, ui_theme_color(UI_C_PANEL));
}

/* The placement rules, numbered. */
typedef struct { ui_text_id_t head, line1, line2; } rule_t;

static const rule_t k_rig_rules[] = {
    { TX_BAL_RIG_1, TX_BAL_RIG_1A, TX_BAL_RIG_1B },
    { TX_BAL_RIG_2, TX_BAL_RIG_2A, TX_BAL_RIG_2B },
    { TX_BAL_RIG_3, TX_BAL_RIG_3A, TX_BAL_RIG_3B },
    { TX_BAL_RIG_4, TX_BAL_RIG_4A, TX_BAL_RIG_4B },
    { TX_BAL_RIG_5, TX_BAL_RIG_5A, TX_BAL_RIG_5B },
    { TX_BAL_RIG_6, TX_BAL_RIG_6A, TX_BAL_RIG_6B },
};
#define RIG_RULES ((int)(sizeof(k_rig_rules) / sizeof(k_rig_rules[0])))

#define AIR_RULES 6
static const rule_t k_air_rules[] = {
    { TX_BAL_AIR_1, TX_BAL_AIR_1A, TX_BAL_AIR_1B },
    { TX_BAL_AIR_2, TX_BAL_AIR_2A, TX_BAL_AIR_2B },
    { TX_BAL_AIR_3, TX_BAL_AIR_3A, TX_BAL_AIR_3B },
    { TX_BAL_AIR_4, TX_BAL_AIR_4A, TX_BAL_AIR_4B },
    { TX_BAL_AIR_5, TX_BAL_AIR_5A, TX_BAL_AIR_5B },
    { TX_BAL_AIR_6, TX_BAL_AIR_6A, TX_BAL_AIR_6B },
};

/* Which blades the correction falls between, which is the whole reason the
 * count is asked for.  The arithmetic is public and takes the count as an
 * argument so it can be checked on its own, away from the screen's state. */
void balance_nearest_blades(float angle, int blades, int *lo, int *hi)
{
    if (blades < 1) { blades = 1; }
    const float step = 360.0f / (float)blades;
    int before = (int)(angle / step);
    if (before < 0) { before = 0; }
    *lo = (before % blades) + 1;
    *hi = ((before + 1) % blades) + 1;
}

static void draw_readings(gfx_canvas_t *c)
{
    const int x = RCARD_X + 14;
    const int w = RCARD_W - 28;

    gfx_text(c, x, BODY_Y + 12, "ROTOR", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    for (int i = 0; i < ROTOR_COUNT; ++i) {
        ui_button(c, s.rotor_btn[i], (i == ROTOR_PROP) ? "PROPELLER" : "EDF",
                  (s.rotor == i) ? ui_theme_color(UI_C_ACCENT)
                                 : ui_theme_color(UI_C_PANEL_HI),
                  false, true);
    }

    char buf[32];
    gfx_text(c, x, BODY_Y + 102, TR(BAL_BLADES), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    snprintf(buf, sizeof(buf), "%d", s.blades & 0xFF);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + 60), (int16_t)(BODY_Y + 102),
                                 60, 16 },
                buf, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
    ui_button(c, s.blade_dn, "-", ui_theme_color(UI_C_PANEL_HI),
              false, s.blades > 2);
    ui_button(c, s.blade_up, "+", ui_theme_color(UI_C_PANEL_HI),
              false, s.blades < 6);

    gfx_hline(c, x, BODY_Y + 142, w, ui_theme_color(UI_C_EDGE));

    const float ang = balance_screen_angle();
    char v_amp[32], v_ang[32], v_mass[32], v_where[32];
    snprintf(v_amp,  sizeof(v_amp),  "0.42 g");
    snprintf(v_ang,  sizeof(v_ang),  "%d deg", (int)ang);
    snprintf(v_mass, sizeof(v_mass), "0.35 g");
    int lo, hi;
    balance_nearest_blades(ang, s.blades, &lo, &hi);
    if (s.rotor == ROTOR_EDF) {
        /* A blade tip inside a duct is not reachable, so the answer is an
         * angle on the hub rather than a blade to tape. */
        snprintf(v_where, sizeof(v_where), TR(BAL_HUB),
                 (int)ang & 0x1FF);
    } else {
            /* Bounded so the compiler can see it: both are 1..6, but the
         * format is not told so, and the render tool inlines this far
         * enough to notice. */
        snprintf(v_where, sizeof(v_where), TR(BAL_BETWEEN),
                 lo & 0xFF, hi & 0xFF);
    }
    const struct { const char *k; const char *v; } rows[] = {
        { TR(BAL_VIBRATION), v_amp },
        { TR(BAL_ANGLE),     v_ang },
        { TR(BAL_ADD),       v_mass },
        { TR(BAL_WHERE),     v_where },
    };
    for (int i = 0; i < 4; ++i) {
        const int y = BODY_Y + 158 + i * 30;
        gfx_text(c, x, y, rows[i].k, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + 74), (int16_t)y,
                                     (int16_t)(w - 74), 16 },
                    rows[i].v, UI_FONT_LABEL,
                    (i >= 2) ? ui_theme_color(UI_C_ACCENT)
                             : ui_theme_color(UI_C_TEXT), 1,
                    GFX_ALIGN_RIGHT);
    }

    gfx_hline(c, x, BODY_Y + 288, w, ui_theme_color(UI_C_EDGE));
    gfx_text(c, x, BODY_Y + 300, TR(BAL_NOTE_1), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text(c, x, BODY_Y + 318, TR(BAL_NOTE_2), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text(c, x, BODY_Y + 342, TR(BAL_NOTE_3), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
}

static void draw_rules(gfx_canvas_t *c, const rule_t *rules, int count)
{
    const int x = RCARD_X + 14;
    gfx_text(c, x, BODY_Y + 12, TR(BAL_WRONG), UI_FONT_LABEL,
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
        gfx_text(c, x + 28, y, ui_tr(rules[i].head), UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, x + 28, y + 20, ui_tr(rules[i].line1), UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, x + 28, y + 36, ui_tr(rules[i].line2), UI_FONT_LABEL,
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

    if (s.tabs.selected == BALANCE_PANE_MEASURE) {
        draw_measure(c);
        draw_readings(c);
    } else if (s.tabs.selected == BALANCE_PANE_RIG) {
        draw_rig(c);
        draw_rules(c, k_rig_rules, RIG_RULES);
    } else {
        draw_aircraft(c);
        draw_rules(c, k_air_rules, AIR_RULES);
    }
}

/*
 * The panel could not hand over every touch event, so this screen's record
 * of the glass is stale.  The tab row is the one control here that holds
 * state between a press and its release: a press it kept would own a track
 * id the controller reuses, and a later contact that began elsewhere and
 * lifts over the tab would switch the pane on a tap nobody made.
 */
static void cancel(void)
{
    ui_tabs_cancel(&s.tabs);
    /* The tab row may already be drawn pressed in both framebuffers; a
     * cleared press with the same revision would leave it painted held. */
    ++s.rev;
}

static const ui_screen_t k_screen = {
    .title  = "BALANCE",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *balance_screen(void) { return &k_screen; }
