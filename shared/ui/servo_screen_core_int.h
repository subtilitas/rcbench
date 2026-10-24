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
 * rail's reading, and the functions one of the five calls of another,
 * prefixed svs_: among them the conversions between angle, pulse and
 * command in servo_screen.c.  Nothing outside these five files includes
 * this header; the seam with the screen's pages and panels is
 * servo_screen_int.h, which this header includes.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "outputs.h"
#include "servo_screen_int.h"

/**
 * @brief The output shaft, which everything on LIVE's card is drawn around.
 *
 * It sits on the top face of the case, where a servo's output is, 392px
 * into the card: the lead's left end, 298px left of it, and the +45
 * label's right edge, 200px right of it, stay inside the card.  In y it is
 * midway between the card's top and the readings' labels.
 */
#define SHAFT_X (PAD + 392)
/* Integer division on purpose: this is a pixel row. */
/* NOLINTNEXTLINE(bugprone-integer-division) */
#define SHAFT_Y ((CARD_Y + READ_LBL_Y) / 2) /**< The shaft's row, px. */
#define ARC_R   150 /**< The dial's radius, px: 22.5mm. */
#define HORN_L  98  /**< The horn's length, shaft to tip, px. */
#define BODY_W  260 /**< The case's length, px. */
#define BODY_H  88  /**< The case's width, px. */
_Static_assert(SHAFT_X - BODY_W + 24 - 62 >= PAD + 12,
               "the servo's lead starts inside LIVE's card");
_Static_assert(SHAFT_X + ARC_R + 26 + 24 <= PAD + LCARD_W - 12,
               "the dial's labels end inside LIVE's card");
_Static_assert(SHAFT_Y - ARC_R - 36 >= CARD_Y + 4,
               "the dial's +90 label ends inside LIVE's card");
_Static_assert(SHAFT_Y + ARC_R + 36 <= READ_LBL_Y - 4,
               "the dial's -90 label ends above the readings");
_Static_assert(PAD + 10 + BOX_W <= SHAFT_X - 36,
               "the run's box stays clear of the horn and its rings");
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
/** @brief The angle @p deg as a pulse, us, through the profile, the trim,
 *         REVERSE and the ends; svs_deg_to_us() rounds it. */
float svs_deg_to_us_f(float deg);
uint16_t svs_deg_to_us(float deg);
/** @brief The mapping in force now. */
servo_map_t svs_map_now(void);
/** @brief A command of @p m's range as a pulse, us. */
float svs_map_cmd_to_us(const servo_map_t *m, float cmd);
/** @brief A pulse as @p m's angle, degrees, held to its TRAVEL. */
float svs_map_us_to_deg(const servo_map_t *m, float us);
/** @brief A pulse as the far end's command, 0..OUT_SPAN of the range a
 *         command carries, and back. */
float svs_us_to_cmd(float us);
float svs_cmd_to_us(float cmd);
/** @brief The angle the pin drives on an armed bench while nothing
 *         commands it: the outputs layer's rest for a surface. */
float svs_rest_deg(void);
/** @brief SPEED @p pct as the bench's slew, channel-span units a second;
 *         0 at 100%, which is no slew. */
uint16_t svs_slew_of(int pct);

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
 * @brief The drag's frame: the filtered target moves towards the finger's
 *        by the share a frame of @p dt_s takes of SV_DRAG_TAU_MS, and is
 *        commanded; nothing while the value is not live.
 * @param dt_s the time since the last frame, seconds
 */
void svs_drag_tick(float dt_s);
/**
 * @brief The drag's filter ends where it has got to and commands nothing
 *        more.  The one way it ends: a command, a value no longer live,
 *        its arrival, and every way the drag ends (svs_drag_end()).
 */
void svs_drag_filter_end(void);
/**
 * @brief The dial's drag ends before its finger lifted -- a press, a
 *        release the router or the tracker made, a touch loss, leaving,
 *        an arm edge -- and its filter with it (svs_drag_filter_end()).
 */
void svs_drag_end(void);
/**
 * @brief A post of @p kind at @p us ends the drag's filter, unless it is
 *        the filter's own or reissue() saying the held position again.
 *        A knob command ends it even at the pulse the filter last sent.
 * @param kind the command posted
 * @param us its pulse, us
 */
void svs_drag_on_post(servo_cmd_kind_t kind, uint16_t us);
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
