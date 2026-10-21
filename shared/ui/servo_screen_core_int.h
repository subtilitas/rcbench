/**
 * @file servo_screen_core_int.h
 * @brief The SERVO screen's seam between its own five files.
 *
 * The SERVO screen's own seam, between the five files the screen itself
 * is: servo_screen.c, the state, the profile, the command and the
 * screen's hooks; servo_screen_sweep.c, the sweep and the frame step;
 * servo_screen_link.c, what the panel reports to the screen and takes
 * from it; servo_screen_touch.c, the presses, the dial and the knob; and
 * servo_screen_draw.c, the drawing.  It holds the dial's geometry, the
 * conversions between angle, pulse and command, the rail's reading, and
 * the functions one of the five calls of another.  Nothing outside these
 * five files includes this header; the seam with the screen's pages and
 * panels is servo_screen_int.h, which this header includes.
 *
 * The conversions are static inline, as servo_screen_int.h's helpers
 * are: they were static in the one file, and GCC inlines them in each
 * file as it did.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "outputs.h"
#include "servo_screen_int.h"

/* The output shaft, which everything on the left is drawn around.  It sits
 * on the top face of the case, where a servo's output is. */
#define SHAFT_X 300
/* Integer division on purpose: this is a pixel row, and H is even. */
/* NOLINTNEXTLINE(bugprone-integer-division) */
#define SHAFT_Y ((H) / 2)          /* centred in the card, not under it */
#define ARC_R   140
#define HORN_L  92
#define BODY_W  260
#define BODY_H  88
/*
 * The mounting flanges run the full width of the case end, as on a servo.
 *
 * The three clearances are the chosen numbers.  The flange width is what
 * holds a bore with TAB_MARGIN of material outside it and TAB_INNER between
 * it and the case, and the hole positions follow from the same margin top
 * and bottom.
 */
#define TAB_HOLE_R  12
#define TAB_MARGIN  6                  /* material outside the bore     */
#define TAB_INNER   8                  /* between the bore and the case */
#define TAB_OVER    6                  /* how far the case laps over it */
#define TAB_W       (TAB_MARGIN + 2 * TAB_HOLE_R + TAB_INNER + TAB_OVER)
#define TAB_H       BODY_H
#define TAB_HOLE_DY (BODY_H / 2 - TAB_MARGIN - TAB_HOLE_R)

/*
 * Screen angle from servo angle.
 *
 * The case lies along the card with its output at the right-hand end; zero
 * is the arm pointing straight out, and positive is counter-clockwise, the
 * convention every other angle on the bench uses.
 */
#define PHI(a) (a)

/* The power plot's series: the servo rail's voltage, current and power. */
enum { PS_V = 0, PS_A, PS_W, PS_COUNT };

/* ------------------------------------------------------------- conversions */

/*
 * Each side of centre runs to its own end: MIN, CENTRE and MAX are set one
 * by one, so -90 deg is MIN and +90 deg is MAX however far each lies from
 * the centre.
 */
static inline float half_travel_us(bool below)
{
    return below ? (float)s.centre_us - (float)s.min_us
                 : (float)s.max_us - (float)s.centre_us;
}

static inline float deg_to_us_f(float deg)
{
    const float d = s.reverse ? -deg : deg;
    float us = (float)s.centre_us + (float)s.trim_us
               + d / 90.0f * half_travel_us(d < 0.0f);
    if (us < (float)s.min_us) { us = (float)s.min_us; }
    if (us > (float)s.max_us) { us = (float)s.max_us; }
    return us;
}

static inline uint16_t deg_to_us(float deg)
{
    return (uint16_t)(deg_to_us_f(deg) + 0.5f);
}

/* The mapping in force now. */
static inline servo_map_t map_now(void)
{
    const servo_map_t m = { s.min_us, s.centre_us, s.max_us, s.trim_us,
                            s.reverse, s.travel_deg };
    return m;
}

/* A command of @p m's range as a pulse, and a pulse as @p m's angle: what
 * cmd_to_us() and us_to_deg_f() are for the mapping in force. */
static inline float map_cmd_to_us(const servo_map_t *m, float cmd)
{
    const unsigned below = (unsigned)m->centre_us - (unsigned)m->min_us;
    const unsigned above = (unsigned)m->max_us - (unsigned)m->centre_us;
    const float half = (float)((below > above) ? below : above);
    return (float)m->centre_us - half + 2.0f * half * cmd / (float)OUT_SPAN;
}

static inline float map_us_to_deg(const servo_map_t *m, float us)
{
    const float off = us - (float)m->centre_us - (float)m->trim_us;
    const float half = (off < 0.0f)
                       ? (float)m->centre_us - (float)m->min_us
                       : (float)m->max_us - (float)m->centre_us;
    if (half <= 0.0f) {
        return 0.0f;
    }
    float d = off * 90.0f / half;
    if (m->reverse) {
        d = -d;
    }
    if (d < -m->travel_deg) { d = -m->travel_deg; }
    if (d >  m->travel_deg) { d =  m->travel_deg; }
    return d;
}

/* A pulse as the far end's command, 0..OUT_SPAN of the range a command
 * carries, and back: the units it slews in. */
static inline float us_to_cmd(float us)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return (hi > lo) ? (us - (float)lo) * (float)OUT_SPAN / (float)(hi - lo)
                     : (float)OUT_SPAN / 2.0f;
}

static inline float cmd_to_us(float cmd)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return (float)lo + (float)(hi - lo) * cmd / (float)OUT_SPAN;
}

/*
 * The angle the pin drives on an armed bench while nothing commands it: the
 * outputs layer's rest for a surface, in the range a command carries.  The
 * midpoint of that range, so PULSE CENTRE without the trim: 1500 us for
 * STANDARD PWM, 760 us for NARROW 760.
 */
static inline float rest_deg(void)
{
    return us_to_deg_f(cmd_to_us((float)outputs_role_rest(OUT_ROLE_SURFACE)));
}

/*
 * SPEED as the bench's slew, in channel-span units a second.
 *
 * The horn's full travel is 180 degrees and a channel's span covers it, so
 * SPEED_FULL_SPAN_S, two spans a second, is 360 degrees a second.  Below
 * 100% the bench ramps the command at that fraction of it, and a servo
 * asked for 30% takes three times as long to cross as one asked for 90%.
 *
 * 100% is immediate rather than two spans a second, at the far end and in
 * the drawing (slew_per_ms()).  It is the value the screen starts at, so
 * anybody who never touches the slider gets the servo at its own rate,
 * with nothing in front of it.
 */
#define SPEED_FULL_SPAN_S (2u * OUT_SPAN)

static inline uint16_t slew_of(int pct)
{
    if (pct >= 100) {
        return 0u;
    }
    if (pct < 1) {
        pct = 1;
    }
    return (uint16_t)((unsigned)SPEED_FULL_SPAN_S * (unsigned)pct / 100u);
}

/* What the servo rail's meter reads, at the precision it is drawn with. */
typedef struct {
    bool    v_ok, i_ok;
    float   v;        /**< V                                              */
    float   i;        /**< A, signed                                      */
    float   p;        /**< W                                              */
    int32_t q_a;      /**< the current in hundredths of an amp, signed    */
    bool    clipped;  /**< a sample read an end of the meter's range      */
} rail_t;

/* What the CURRENT row shows: the dashes, or the digits and their colour. */
#define CUR_KEY_NONE INT32_MIN

/* --- the functions one file calls of another ---------------------------- */

/* servo_screen.c */

/**
 * @brief A position from an input, refused while the value is not live.
 * @param deg the angle asked for, degrees; held to TRAVEL
 * @return true when the position was posted
 */
bool svs_command(float deg);
/** @brief A motion command still waiting is dropped as the bench stops
 *         driving; a release, an arm and a disarm stay. */
void svs_drop_motion(void);
/** @brief DISARM asked of an armed bench: the value leaves LIVE now. */
void svs_ask_disarm(void);

/* servo_screen_sweep.c */

/** @brief The sweep paused where the output has got to, as PAUSE does. */
void svs_hold_sweep(void);
/** @brief The sweep started over under the curve the settings give now. */
void svs_restart_sweep(void);
/** @brief SWEEP's tap: start, pause or resume, or undo a tap whose
 *         command has not left. */
void svs_toggle_sweep(void);
/**
 * @brief Whether two curves are the same: kind, rate, amplitude and dwell.
 * @param a one curve
 * @param b the other
 * @return true when all four match
 */
bool svs_same_sweep(const sweep_cfg_t *a, const sweep_cfg_t *b);
/** @brief Whether SPEED is slower than the fastest change the curve asks
 *         for, so the slew and not the curve shapes the sweep. */
bool svs_speed_limits_sweep(void);
/**
 * @brief The frame step: the holds, the supply followed, the sweep drawn
 *        and the drawn output moved towards the command at SPEED.
 * @param dt_s the time since the last frame, seconds
 */
void svs_tick_body(float dt_s);

/* servo_screen_link.c */

/**
 * @brief @p x in units of 1/@p per, rounded half away from zero, held to
 *        +-10^8.
 * @param x the reading
 * @param per the units in one of @p x's
 * @return the count
 */
int32_t svs_fixed_of(float x, float per);
/** @brief The servo rail as its meter reads it: what arrived, at the
 *         precision it is drawn with. */
rail_t svs_rail_now(void);

/* servo_screen_touch.c */

/** @brief Whether ARM takes no press: the programmer or a limit run holds
 *         the port and the bench is not armed. */
bool svs_arm_locked(void);
/**
 * @brief Whether a DOWN at (x, y) lands on a control whose press stops
 *        what the bench drives.
 * @param x the press, panel px
 * @param y the press, panel px
 * @return true when the DOWN goes to that control first
 */
bool svs_safe_at(int x, int y);
/** @brief The owners the knob yields to, latched for the frame's turn. */
void svs_knob_note_owner(void);
/** @brief The knob's position command withdrawn, with the value it
 *         replaced put back. */
void svs_knob_withdraw(void);
/**
 * @brief A touch event on the screen's own controls and pages.
 * @param evt the event
 */
void svs_event_body(const touch_event_t *evt);

/* servo_screen_draw.c */

/**
 * @brief The frame: what changed since @p buffer_index was last drawn.
 * @param c the canvas
 * @param buffer_index the frame buffer drawn into
 */
void svs_render(gfx_canvas_t *c, int buffer_index);
