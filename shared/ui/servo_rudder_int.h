/**
 * @file
 * @brief The SERVO workbench's RUDDER task (servo_rudder_panel.c): its
 *        state in the screen's one object, its controls' places, and the
 *        calls the screen's files make of it.
 *
 * RUDDER drives the servo the PORT button names and shows the control
 * surface's deflection on the angle the screen takes in
 * (servo_screen_encoder()): degrees from ZERO and, with LENGTH set, the
 * trailing edge's travel in mm (servo_rudder.h).  Its buttons drive
 * 1000us, CENTER and 2000us, and the knob moves the horn as on LIVE;
 * without a knob that answers, -10us and +10us step the pulse.  All of
 * them only while the value is live (ARMED).  The output card, the PORT
 * button and the footer's ARM stay where they are on every function
 * screen.
 *
 * The left card, top to bottom, in the screen's coordinates, every control
 * inside x 18 to 518 so the guide's dock on the right leaves it in view:
 *
 *   - the readout, y 14 to 114: DEFLECTION and TRAILING EDGE over their
 *     numerals in the 32 x 44 face, the MAX of each side under them;
 *   - LENGTH's row, y 120 to 168, a tap opens the keypad;
 *   - 1000us, CENTER and 2000us, y 180 to 228, 160px each, 10px apart;
 *   - ZERO and RESET MAX, y 240 to 288, 160px each, and right of them
 *     -10us and +10us, x 358 to 433 and 443 to 518, while SETUP > PANEL's
 *     Rotary knob is OFF or the knob does not answer
 *     (servo_screen_set_knob());
 *   - the state lines, x 18 to 418, y 300 to 396: the angle source, the
 *     pulse, the zero and ARM FIRST;
 *   - beside them the side view of the surface (servo_rudder_flap.h),
 *     x 428 to 618, y 292 to 436: no control, and no touch on it.
 *
 * RUDDER AUTO, a screen of its own, shows AUTO's box
 * (servo_rudder_auto_int.h) in the place of LENGTH, the three drive
 * buttons and the state lines.  The readout, the ZERO row and the side
 * view stay.
 *
 * Every control is UI_TAP_MIN tall.  The header holds no helper that reads
 * the state: servo_screen_int.h includes it before the state is declared.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "servo_rudder.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief RUDDER's controls, as a press and the layout name them. */
typedef enum {
    RUD_C_NONE = 0,  /**< no control                                  */
    RUD_C_LENGTH,    /**< LENGTH's row: the keypad                    */
    RUD_C_1000,      /**< 1000us                                      */
    RUD_C_CENTER,    /**< CENTER: PULSE CENTER plus TRIM              */
    RUD_C_2000,      /**< 2000us                                      */
    RUD_C_ZERO,      /**< ZERO: the angle now is the neutral          */
    RUD_C_RESET,     /**< RESET MAX                                   */
    RUD_C_MINUS,     /**< -10us: the pulse RUD_STEP_US shorter        */
    RUD_C_PLUS,      /**< +10us: the pulse RUD_STEP_US longer         */
    RUD_C_COUNT      /**< the count, RUD_C_NONE included              */
} rudder_ctl_t;

/** @brief What the angle source gives now, as the state line names it. */
typedef enum {
    RUD_SRC_LIVE = 0,  /**< a reading on show                        */
    RUD_SRC_OFF,       /**< the source is off in SETUP               */
    RUD_SRC_NO_LINK,   /**< no link to the IOmcu                     */
    RUD_SRC_NO_MAGNET, /**< the AS5600 sees no magnet                */
    RUD_SRC_STALE,     /**< on, linked, and no reading               */
} rudder_src_t;

/** @brief RUDDER's state, the member `rud` of the screen's state. */
typedef struct {
    /** @brief The zero and the MAX of each side, in the source's own
     *         sign: REVERSE flips what is shown, not what is kept. */
    servo_rudder_t geo;
    /** @brief The source @ref geo is kept against: 0 none yet, 1 the
     *         AS5600, 2 + 2 x AXIS + SIGN the IMU, plus 16 x the IMU's
     *         zero generation.  Another source in force drops the zero
     *         and the MAX. */
    uint32_t geo_src;
    uint8_t  pressed;       /**< rudder_ctl_t a tap is on, RUD_C_NONE   */
    uint8_t  press_id;      /**< that contact's track id                */
    float    held_s;        /**< how long -10us or +10us is held, s     */
    uint32_t drawn_card[2]; /**< rudder_card_sig() as each buffer drew it */
    uint32_t drawn_read[2]; /**< rudder_read_sig() as each buffer drew it */
    uint32_t drawn_flap[2]; /**< the side view's sig as each buffer drew it,
                             *   its angle left out                     */
    int16_t  drawn_dd[2];   /**< the flap's angle each buffer drew, 0.1° */
} rudder_t;

/** @brief The pulse one tap of -10us or +10us moves, us: 10us, about
 *         0.9° on a servo with 11us a degree, the limit run's step. */
#define RUD_STEP_US 10

/** @brief The width the state lines, and AUTO's START and its lines,
 *         take left of the side view: x 18 to 418, px. */
#define RUD_SIDE_W 400

/** @brief The task at its start: no zero, no MAX, no press. */
void rudder_init(void);

/**
 * @brief The angle the screen took in (servo_screen_encoder()), into the
 *        MAX of its side while a reading is on show.  On every task: MAX
 *        holds the largest since ZERO or RESET MAX.
 */
void rudder_angle(void);

/**
 * @brief Whether RUDDER's card is in sight and takes presses: the task
 *        shown, no panel and no programmer over the left column.
 * @return true when it is
 */
bool rudder_shown(void);

/**
 * @brief The angle source's state.
 * @return the state the state line names
 */
rudder_src_t rudder_source(void);

/**
 * @brief The deflection on show, degrees, in the commanded direction.
 * @param deg where it goes; untouched while none is on show
 * @return false while the source gives no reading, or for NULL
 */
bool rudder_defl(float *deg);

/**
 * @brief A control's place.
 * @param k the control
 * @return its rectangle in the screen's coordinates; empty for
 *         RUD_C_NONE and RUD_C_COUNT, for -10us and +10us while
 *         rudder_steps_shown() is false, and for LENGTH and the drive
 *         buttons in AUTO's view
 */
gfx_rect_t rudder_rect(rudder_ctl_t k);

/** @brief LENGTH's value box, at the right end of its row.
 *  @return its rectangle */
gfx_rect_t rudder_len_value_rect(void);

/** @brief The readout: the two readings and the MAX lines.
 *  @return its rectangle */
gfx_rect_t rudder_readout_rect(void);

/** @brief The state lines. @return their rectangle */
gfx_rect_t rudder_state_rect(void);

/** @brief The side view's area, x 428 to 618, y 340 to 492, above the
 *         alert band.  @return its rectangle */
gfx_rect_t rudder_flap_rect(void);

/**
 * @brief State line @p i as drawn now.
 * @param i the line, from 0
 * @param buf where it goes
 * @param n the size of @p buf
 * @return @p buf; "" for a line not drawn now and past the last
 */
const char *rudder_line(int i, char *buf, size_t n);

/**
 * @brief The zero and the MAX as kept, in the source's own sign: for the
 *        tests.
 * @return the task's geometry state
 */
const servo_rudder_t *rudder_geo(void);

/** @brief The state lines a card draws at most. */
#define RUD_LINES 4

/**
 * @brief A DOWN on the page area while RUDDER is shown.  1000us, CENTER
 *        and 2000us act on it while the value is live, and end a held
 *        -10us or +10us.  -10us and +10us step on it while the value is
 *        live, take the press and repeat while it is held; a DOWN with the
 *        press's own id ends it, its UP lost.  LENGTH, ZERO and RESET MAX
 *        take the press and act on its release over them.
 * @param evt the DOWN
 */
void rudder_down(const touch_event_t *evt);

/**
 * @brief The moves and the release of the press rudder_down() took.
 * @param evt the event
 * @return whether it was that press's
 */
bool rudder_rest(const touch_event_t *evt);

/**
 * @brief Whether -10us and +10us are on the card: RUDDER, not AUTO's
 *        view, with SETUP > PANEL's Rotary knob OFF or a knob that does
 *        not answer (servo_screen_set_knob()).
 * @return true when they are
 */
bool rudder_steps_shown(void);

/**
 * @brief A frame's time for a held -10us or +10us: its repeats
 *        (ui_key_repeat()).  The press ends when the keys go, the card
 *        goes or the value leaves LIVE.
 * @param dt_s the frame's time, s
 */
void rudder_tick(float dt_s);

/** @brief ZERO as its button takes it: the angle on show is the neutral;
 *         nothing without a reading or while AUTO runs. */
void rudder_zero(void);

/** @brief A press under way dropped: the task left, the screen left,
 *         touch events lost. */
void rudder_cancel(void);

/**
 * @brief RUDDER's card whole into @p c, and what it shows noted for
 *        buffer @p buf.
 * @param c the canvas
 * @param buf the buffer, 0 or 1
 */
void rudder_render(gfx_canvas_t *c, int buf);

/**
 * @brief What changed since buffer @p buf last drew the card: the card
 *        whole when a control's state did, else the readout and the state
 *        lines alone when a reading did, and the side view whole when its
 *        MAX or targets did, or for a new angle alone the boxes of the
 *        flap as drawn and as now (rudder_flap_bounds()).
 * @param c the canvas
 * @param buf the buffer, 0 or 1
 */
void rudder_render_part(gfx_canvas_t *c, int buf);


#ifdef __cplusplus
}
#endif
