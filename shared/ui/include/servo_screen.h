/**
 * @file
 * @brief The servo bench: the outputs the binding marks as surfaces, commanded
 * together by dragging one horn.
 *
 * SCREEN_SERVO is the SERVO hub: SETUP's grid of 328 x 130px tiles, one
 * tile for each function screen (LIVE, SWEEP, TEST, LIMIT RUN, RUDDER,
 * RUDDER AUTO, KST, HITEC), and the footer with the servo profile's
 * SETTINGS list, which every function shares.  Each function screen
 * (SCREEN_SERVO_LIVE to SCREEN_SERVO_HITEC) shows one function on the left
 * card, the PORT button and the output card on the right, and the footer:
 * the function's name and port line, ARM / DISARM, SETTINGS.  The function
 * screens share one state: the screens differ in what they show, not in
 * what they keep.  Leaving a function screen switches the bench to SAFE
 * and ends a sweep or a run; PROG 5V stays on between function screens
 * through the hub and goes off when the hub is left for any other screen.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sense_link.h"
#include "servo_cmd.h"
#include "servo_source.h"
#include "servo_test_io.h"
#include "supply.h"
#include "ui_keypad.h"
#include "ui_screen.h"

/** Drop the cached chrome, so the next frame repaints it. */
void servo_invalidate(void);

/** @brief The SERVO hub and its function screens, one per tile. */
typedef enum {
    SERVO_FN_HUB = 0, /**< the hub, SCREEN_SERVO                  */
    SERVO_FN_LIVE,    /**< the dial, CENTER, RELEASE              */
    SERVO_FN_SWEEP,   /**< the curve, SWEEP / PAUSE, RELEASE      */
    SERVO_FN_TEST,    /**< the automatic test                     */
    SERVO_FN_LIMRUN,  /**< the limit run                          */
    SERVO_FN_RUDDER,  /**< a control surface's deflection         */
    SERVO_FN_RAUTO,   /**< RUDDER AUTO: end points to a deflection */
    SERVO_FN_KST,     /**< the KST and Chaservo programmer        */
    SERVO_FN_HITEC,   /**< the Hitec programmer                   */
    SERVO_FN_COUNT
} servo_fn_t;

/** @brief The SERVO hub, SCREEN_SERVO. @return its screen */
const ui_screen_t *servo_screen(void);

/**
 * @brief The screen of function @p fn: the hub for SERVO_FN_HUB or a value
 *        out of range.
 * @param fn The function.
 * @return Its screen.
 */
const ui_screen_t *servo_fn_screen(servo_fn_t fn);

/**
 * @brief The function a router screen shows.
 * @param id The screen.
 * @return Its function; SERVO_FN_HUB for SCREEN_SERVO and for every screen
 *         that is not a SERVO function screen.
 */
servo_fn_t servo_fn_of_screen(ui_screen_id_t id);

/**
 * @brief Whether a router screen is one of SERVO's: the hub or a function
 *        screen.  Leaving one for another keeps the ports' 5V on.
 * @param id The screen.
 * @return True for SCREEN_SERVO and SCREEN_SERVO_LIVE to SCREEN_SERVO_HITEC.
 */
bool servo_screen_owns(ui_screen_id_t id);

/**
 * @brief The router screen of function @p fn.
 * @param fn The function.
 * @return Its screen id; SCREEN_SERVO for SERVO_FN_HUB.
 */
ui_screen_id_t servo_fn_screen_id(servo_fn_t fn);

/**
 * @brief The function shown now: the one whose screen was entered last,
 *        SERVO_FN_LIVE after a reset until one is.
 * @return The function.
 */
servo_fn_t servo_screen_fn(void);

struct ui_setlist;
/** @brief The SETTINGS list of the hub and the function screens, for the
 *         tests. @return the list */
const struct ui_setlist *servo_screen_list(void);

/**
 * @brief Function @p fn shown as its screen's enter() shows it, without
 *        leaving the one shown before: the bench, a run and the panels stay.
 *        The function screens' enter() and the host tests use it.
 * @param fn The function.
 */
void servo_screen_show_fn(servo_fn_t fn);

/** @brief The hub's tiles: LIVE, SWEEP, TEST, LIMIT RUN, RUDDER, RUDDER
 *         AUTO, KST, HITEC, each a function after SERVO_FN_HUB. */
#define SERVO_HUB_TILES 8

/**
 * @brief Hub tile @p i's rectangle in the body, SETUP's geometry: 328 x
 *        130px at x 10, 348 and 686, y 10, 150 and 290.
 * @param i The tile, 0 for LIVE.
 * @return Its rectangle; an empty one past the last.
 */
gfx_rect_t servo_hub_tile_rect(int i);

/**
 * @brief Whether hub tile @p i is grey: KST and HITEC while the registry
 *        row of their programmer is not live.
 * @param i   The tile, 0 for LIVE.
 * @param why Takes the reason, "" for none; may be NULL.
 * @param n   Its size.
 * @return True while the tile takes no tap.
 */
bool servo_hub_tile_refused(int i, char *why, size_t n);

/**
 * @brief Hub tile @p i's line under its name.  TEST's names the steps a run
 *        started now takes, as TEST's READY card does: `4.8-6.0V,
 *        BROWN-OUT` with HV SERVO off, 7.4V and 8.4V only with it on, NO
 *        STEP CHOSEN with none.
 * @param i   The tile, 0 for LIVE.
 * @param buf Takes TEST's line.
 * @param n   Its size.
 * @return The line, at most 31 cells; "" past the last tile.
 */
const char *servo_hub_tile_line(int i, char *buf, size_t n);

/**
 * @brief Move the horn by @p span_fraction of its travel (-travel to +travel),
 * from the rotary knob (knob.h).  Relative to the commanded angle.  Nothing
 * happens for a zero fraction, on a disarmed bench, under a disarm waiting
 * to be taken, while a finger is on the dial, while the dial is out of
 * sight, or while a sweep (running or paused) or a test run owns the horn:
 * the knob does not take the horn from them as a finger does.  The dial is
 * out of sight on every task but LIVE (TEST with or without a run's
 * settings open, SETTINGS on any page), under any panel (keypad, keyboard,
 * list, warning, question) and under the servo programmer.  A turn that moves
 * nothing is not kept: the first turn after an arm starts from the value the
 * arm set.  It posts the
 * position command a touch does and never arms.  It posts only into an empty
 * slot or over a position: any other pending command (a release, an arm, a
 * disarm) stays and the knob's motion is dropped.
 *
 * @param span_fraction the move as a fraction of the travel, -1 for -travel
 *   to +1 for +travel
 */
void servo_screen_knob(float span_fraction);

/**
 * @brief Whether the rotary knob and the Dial move the horn now: LIVE,
 *        SWEEP, RUDDER or RUDDER AUTO in sight, no panel over the dial, no
 *        sweep, test, limit run or AUTO run owning the horn.
 * @return true then.
 */
bool servo_screen_knob_shown(void);

/** @brief How far each way the horn may go, deg. @return The travel. */
float servo_screen_travel_deg(void);

/**
 * @brief The profile's pulse ends, PULSE MIN and PULSE MAX.
 * @param min_us Receives PULSE MIN, us; may be NULL.
 * @param max_us Receives PULSE MAX, us; may be NULL.
 */
void servo_screen_pulse_ends(uint16_t *min_us, uint16_t *max_us);

/** @brief The commanded angle, deg from center. @return The angle. */
float servo_screen_commanded_deg(void);

/**
 * @brief The Dial's button pressed and let go (dial_ctl.h): CENTER on an
 *        ARMED bench where the knob moves the horn, as the CENTER button's
 *        press; a sweep stops.  Nothing on a SAFE bench, under a pending
 *        command other than a position, or while a test, a limit run or an
 *        AUTO run owns the horn.
 * @return Whether CENTER was posted.
 */
bool servo_screen_dial_press(void);

/**
 * @brief The Dial's button held 1s: on an ARMED bench DISARM and every
 *        output off, as DISARM's press; a start sequence under way ends.
 *        Nothing on a SAFE bench.  It never arms.
 * @return Whether it disarmed or ended a sequence.
 */
bool servo_screen_dial_hold(void);

/**
 * Start of a frame, before its touch events: forget that a finger owned the
 * dial or that the dial was out of sight, a sweep or a test owned the horn.
 * The knob moves nothing in a frame in which a finger owned the dial at any
 * point, including a press and release inside it, or in which the dial was
 * out of sight (another task, a panel, the programmer), a sweep or a test
 * ran at any point, even if it ended before the turn is applied: a tap on
 * LIVE in the frame of the turn does not let the turn through.
 */
void servo_screen_knob_frame(void);

/**
 * Withdraw the knob's position command if it is still waiting to be taken:
 * the commanded angle returns to its value from before the knob, and a
 * position that was pending before the knob moved it is pending again with
 * that angle, and the output is held or idle as it was before the knob.  On
 * a bench that is disarmed by then the output is idle whatever the knob
 * found.  A command posted since, or already taken, is left alone.  A disarm
 * and a stop withdraw it themselves (servo_screen_set_armed(),
 * servo_screen_cancel_arm()).
 */
void servo_screen_knob_cancel(void);

/**
 * @brief Take the pending command, if any.  Cleared by reading.
 *
 * @param out receives the command
 * @return true when a command was pending
 */
bool servo_screen_take(servo_cmd_t *out);

/**
 * @brief Take the CLEAR ARM + START's steps ask (servo_start_seq.h, step
 *        FAILSAFE CLEAR), once.  The application writes CLEAR to the
 *        IOmcu's control page as ARM's own CLEAR, while SAFE, and reads
 *        STATUS straight after it, so the steps see the failsafe go.  A
 *        failure, a stop or a leave drops one not yet taken.
 * @return true once per CLEAR asked
 */
bool servo_screen_take_clear(void);

/**
 * @brief A reading of the angle source, each one once, in the order they arrive:
 * the AS5600 on the horn shaft, or with ANGLE SOURCE IMU the IMU's angle in
 * AS5600 counts from count 0 (servo_angle_enc()).  With AS5600 on in SETUP,
 * or with the IMU as the source, the MEASURED row shows its angle from the
 * source's center (servo_angle_center()), and a run reads it for the angle
 * results in its CSV and report.  A reading that is not valid -- the
 * sensor not answering, or answering and detecting no magnet -- shows
 * "---", and ENC CENTER takes nothing from it nor from any IMU reading.
 *
 * @param e The reading; NULL does nothing.
 */
void servo_screen_encoder(const servo_test_enc_t *e);

/**
 * @brief What the output is actually doing, from the bench or from the model.
 *  @p current_a is not shown: the CURRENT row reads the servo rail's meter
 *  (servo_screen_source()).
 *
 * @param position_us the output's pulse width, us
 * @param current_a the output's current, A
 * @param valid the reading is valid
 */
void servo_screen_feedback(uint16_t position_us, float current_a, bool valid);

/**
 * @brief Whether the bench is armed for the surfaces, so the button can say
 *        it. An arm for the motor alone is not one: ARM on this screen arms
 *        the surfaces, and a bench armed for the motor disarms before it does.
 *
 * The screen does not decide whether it is armed: arming is asked of the
 * policy and the answer comes back from the bench, the same way MOTOR & ESC
 * learns it.
 *
 * The commanded value follows the bench.  At every arm it becomes the rest
 * the pins drive -- outputs_role_rest() across the range a command carries,
 * 1500us for STANDARD PWM and 760us for NARROW 760 -- and without
 * feedback the horn is drawn there at once.  While the bench is disarmed no
 * input changes it: the dial, the knob, CENTER, SWEEP and
 * servo_screen_set_commanded() post nothing and move nothing, and the value
 * shown is the one last driven.  At a disarm a drag ends, and a position, a
 * sweep or a hold still waiting to be taken is not sent; the knob's is
 * withdrawn with the value it replaced.
 *
 * RELEASE on an armed bench is the arm's rule again: the value becomes the
 * rest, a drag ends, and the next input starts from there -- for the
 * button, the release a run's end posts, and the one a change of profile
 * posts on a resting bench.  On a disarmed bench RELEASE is posted and the
 * value stays.
 *
 * The state is ui_value_gate.h's.  From a SERVO_CMD_DISARM being posted,
 * taken or not, or servo_screen_cancel_arm() on an armed bench, until this
 * is called with false, the value is not live: the inputs are refused as
 * on a disarmed bench, and a sweep, a pause, a held position and a run of
 * the automatic test end where it was asked.
 *
 * @param armed true when the surfaces are armed
 */
void servo_screen_set_armed(bool armed);

/**
 * @brief Whether the bench is armed for any role.  servo_screen_set_armed() is
 * told whether the surfaces are armed, which an arm for the motor alone
 * leaves false; this is what keeps the servo programmer from taking a port
 * under a motor that is armed, as it is kept from one under armed surfaces.
 *
 * @param armed true when the bench is armed for any role
 */
void servo_screen_set_bench_armed(bool armed);

/**
 * Abandon a hold that is under way, because the bench has been stopped.
 *
 * Separate from servo_screen_set_armed() because a stop on a bench that was
 * not armed changes nothing about whether it is armed, and the gesture must
 * end all the same.  A drag ends, and a position, a sweep or a hold still
 * waiting to be taken is not sent, as at a disarm.
 */
void servo_screen_cancel_arm(void);

/**
 * @brief The bench refused the ARM stamped @p ask (servo_cmd_t.ask), for
 *        the panel's frame to pass on every frame.
 *
 * An ARM this screen still waits on under that stamp ends: the screen reads
 * SAFE, ARM takes a press again and a servo programmer may take its port.
 * A hold of ARM under way ends with it.  The band's alert, which says why,
 * is the panel's.  Any other stamp changes nothing: an ARM answered,
 * dropped by a DISARM, a STOP or leaving the screen, followed by a newer
 * ARM, or asked on another screen.  Nothing is posted, and no STOP latch
 * moves.
 *
 * @param ask The stamp of the ARM the bench refused last.
 */
void servo_screen_arm_refused(uint32_t ask);

/**
 * @brief Commanded pulse width, for the application and for tests.
 *
 * @return the pulse width, us
 */
uint16_t servo_screen_commanded(void);

/**
 * @brief The commanded pulse for a SERVO run's log row, as COMMANDED shows
 *        it: none while a programming session holds the port.
 *
 * @param us Receives the pulse, us.
 * @return true when a pulse is commanded; false with @p us NULL.
 */
bool servo_screen_log_pulse(uint16_t *us);

/**
 * @brief The angle source's angle for a SERVO run's log row, as MEASURED
 *        shows it: AS5600 on in SETUP or the IMU as the source, a valid
 *        reading and the link up.
 *
 * @param deg Receives the horn's angle from the source's center, deg.
 * @return true when an angle is shown; false with @p deg NULL.
 */
bool servo_screen_log_angle(float *deg);

/**
 * @brief The keypad over the left column: a set point's, a settings row's
 *        or TRIM's, which is signed; for tests and the renderer.
 * @return The keypad, open or closed.
 */
const ui_keypad_t *servo_screen_keypad(void);
/**
 * @brief The value of the COMMANDED row as it is drawn: the pulse in us, and
 *  "---" while the servo programmer holds the port and no pulse is on it.
 *
 * @return the text
 */
const char *servo_screen_commanded_text(void);
/**
 * @brief The steps of an automatic test as TEST's card names them: each
 *        voltage with its unit, then BROWN-OUT, comma-separated, as
 *        "4.8V, 6.0V, BROWN-OUT"; "" with no step and no brown-out.
 * @param out  takes the text
 * @param size bytes at @p out
 * @param v    the steps' set points, V, in order
 * @param n    how many, 0 to SERVO_TEST_STEPS_MAX
 * @param brownout whether the brown-out walk follows them
 */
void servo_screen_plan_text(char *out, size_t size, const float *v,
                            uint8_t n, bool brownout);

/**
 * @brief The AUTOMATIC TEST card's summary over START TEST, as it is drawn:
 *        the steps a run started now takes and the length of each, as
 *        "4.8V, 6.0V, BROWN-OUT, 60s EACH", in the language in force.
 * @param out  takes the line
 * @param size bytes at @p out
 */
void servo_screen_test_summary(char *out, size_t size);

/**
 * @brief The pulse width the solid arm is drawn at: measured, or without
 *  feedback the drawing's estimate of the output.  For tests.
 *
 * @return the pulse width, us
 */
uint16_t servo_screen_drawn(void);

/**
 * @brief The frame rate and the type in force, for the application and tests.
 *
 * @return the frame rate, Hz
 */
uint16_t servo_screen_frame_hz(void);
const char *servo_screen_type_name(void);

/**
 * @brief One sample of the supply that feeds the servo.  The automatic test reads
 * every one: as its reading in a run on the PD mini or the model, and for
 * the supply's state alone in a run on the INA3221.  While the servo rail's
 * meter is not the INA3221
 * (servo_screen_source()) it is also the rail's reading: the CURRENT row,
 * the line over the plot and one point of the plot.
 *
 * @param s the sample
 */
void servo_screen_supply(const supply_state_t *s);

/**
 * @brief servo_screen_supply() with the meter @p id and its change count
 * @p changes as decided at the poll that took the sample, for samples that
 * waited on a queue.  A sample whose count is ahead of the meter in force
 * brings its meter with it, as servo_screen_source() would.  One whose
 * count is behind was taken under a meter replaced since: the automatic
 * test reads it and it is the supply's last reading on the line and in the
 * row, and it is no point of the plot.
 *
 * @param s the sample
 * @param id the meter decided in the poll that took it
 * @param changes that meter's change count
 */
void servo_screen_supply_at(const supply_state_t *s, servo_source_id_t id,
                            uint32_t changes);

/**
 * @brief The servo rail's meter, as servo_source.h decides it, every frame
 *        before that frame's windows and supply samples.  @p changes is
 *        servo_source_changes() of the same poll: the meter is taken unless
 *        the one in force was decided at a later count, which a window's or a
 *        supply sample's can be (servo_screen_window(),
 *        servo_screen_supply_at()).
 *
 * The CURRENT row, the line over the plot and the plot show that meter's
 * reading.  SERVO_SOURCE_INA3221: the last CH1 window -- mean current, mean
 * bus voltage at the load side of the shunt and their product -- under the
 * label INA3221 CH1.  SERVO_SOURCE_PDMINI and SERVO_SOURCE_MODEL: the
 * supply's last sample under the label PD MINI, the PD mini's reading or the
 * model's; with the model (supply_screen_model()) the MODELLED pill sits on
 * the plot's top-left corner.  SERVO_SOURCE_MODEL until the first call.  The
 * name takes its cells from the readings beside it: beside INA3221 CH1 they
 * are 60, 64 and 56px wide, and a power wider than that is drawn in whole
 * watts.
 *
 * The row is `---` and a reading on the line `--` while the meter has no
 * reading: no window since the link came up, a window without samples of
 * that quantity, a supply that does not answer.  A current is signed and
 * drawn in hundredths of an amp; one from a window with a sample at an end
 * of the INA3221's range is drawn in the warning colour.  The row is
 * repainted, alone, when its digits or its colour change.
 *
 * A change to or from the INA3221 empties the plot: its trace is of the
 * meter its label names.  So does a count more than one ahead of the one
 * in force, with the same meter or another: changes went by unseen, and
 * another meter may have been the rail's in between.  The INA3221's last
 * window is then forgotten as well.  The plot's scale starts at 0, so a negative
 * current is drawn on its lower edge.
 *
 * @param id the meter
 * @param changes the change count of the poll
 */
void servo_screen_source(servo_source_id_t id, uint32_t changes);

/**
 * @brief @p told is called with true when a run of the automatic test starts and
 * with false when it ends, inside the call that starts or ends it -- a
 * touch event, a window, a supply sample, the meter, the link, a tick --
 * and not at the next frame.  The panel's control task writes no bench log
 * row between the two (log_cadence_bench_run()).  NULL for none.  It is
 * called from the task that runs the screen and must not call back into
 * it.
 *
 * @param told the callback; NULL for none
 */
void servo_screen_on_testing(void (*told)(bool running));

/**
 * @brief Why the INA3221 is not the servo rail's meter now (servo_source_why()),
 * every frame with servo_screen_source().  A run started on the PD mini
 * with the INA3221 on in SETUP states it in its report.
 *
 * @param why why it is not the meter
 */
void servo_screen_source_why(servo_source_why_t why);

/**
 * @brief Why the INA3221 last stopped being the meter, and the change count of
 * that step (servo_source_dropped()), as decided at the same poll as the
 * meter handed over next: before servo_screen_source(), and before each
 * servo_screen_window() and servo_screen_supply_at().  SERVO_SOURCE_WHY_NONE
 * says nothing.
 *
 * The automatic test reads the meter that was the rail's at its start
 * until it ends (servo_test_meter_now()).  A run on the INA3221 ends when a
 * meter handed over is not the INA3221 or carries another change count
 * than the run's start: ABORTED, with the condition this call named for a
 * change after the start, the output asked off and the servo let go.  A
 * run on the PD mini or the model goes on with it whatever the meter
 * becomes.
 *
 * @param why why it stopped
 * @param at the change count of that step
 */
void servo_screen_source_dropped(servo_source_why_t why, uint32_t at);

/**
 * @brief One 50ms window of INA3221 CH1 (sense_link_take_win()), each one
 *        once, in the order they arrive, with the meter @p id and its change
 *        count
 * @p changes as decided in the poll that took the window.
 *
 * The windows wait on a queue and the meter of servo_screen_source() is read
 * from a snapshot, so either can be the older.  A window whose count is
 * behind the meter in force was taken under a meter replaced since and is
 * dropped.  One whose count is ahead brings its meter with it, as
 * servo_screen_source() would.  While the INA3221 is the rail's meter each
 * window taken is one point of the plot and the reading of the row and the
 * line.  One that arrives while the link is down is dropped, and a link
 * that goes down takes the last one with it (servo_screen_set_link()).
 *
 * A run of the automatic test that started with the INA3221 as the meter
 * reads these windows and no supply sample: each one taken under the meter
 * in force is its reading.
 *
 * @param w the window
 * @param id the meter decided in the poll that took it
 * @param changes that meter's change count
 */
void servo_screen_window(const sense_link_win_t *w, servo_source_id_t id,
                         uint32_t changes);

/**
 * @brief How many points the power plot holds.  For tests.
 *
 * @return the points
 */
int   servo_screen_power_points(void);
float servo_screen_power_sample(int series, int back);

/**
 * @brief The OUTPUT page's note while a rate is SERVO_RATE_IN_FORCE: two lines
 *        into
 * @p l1 and @p l2 of @p size bytes each, for the rate @p hz, the drive
 * @p target / @p gpio / @p pwm (as servo_screen_set_drive()) and the PWM
 * TARGET setting @p setting (SET_SERVO_TARGET: 0 OUTPUTS, 1 PROG PORT, the
 * target the panel asks for).  The first line names the target the
 * IOmcu holds, not the setting: "In force: OUTPUTS run at 50Hz.",
 * "In force: PROG PORT GPIO 13 runs at 560Hz.", the port without a known pin,
 * or the target not known (the page not known).  With no link it names no
 * rate: "No link: the rate in force is not known.".  The second line
 * says "The PORT chosen is not active." while the setting and the
 * drive differ, "PORT PWM is OFF" while the port renders no pulse, else
 * that a PPM output keeps its own frame.  Returns whether the note takes
 * the warning colour: the setting and the drive differ, no link, the
 * target not known, a port without a known pin, or a port with its pulse
 * off.  Neither line is written with a NULL buffer or a @p size of 0, and
 * false is returned.
 *
 * @param l1 out: the first line
 * @param l2 out: the second line
 * @param size the size of each, bytes
 * @param hz the rate in force
 * @param target the drive's target, as servo_screen_set_drive()
 * @param gpio the drive's pin, as servo_screen_set_drive()
 * @param pwm whether the port renders a pulse, as servo_screen_set_drive()
 * @param setting SET_SERVO_TARGET: 0 OUTPUTS, 1 PROG PORT
 * @return whether the note takes the warning colour
 */
bool servo_in_force_note(char *l1, char *l2, size_t size, uint16_t hz,
                         uint8_t target, uint8_t gpio, bool pwm, int setting);

/**
 * @brief What the panel last heard about the frame rate @p hz.  Called every frame;
 * a state about a rate other than the one the screen shows reads as
 * SERVO_RATE_UNSENT for it.
 *
 * @param st what the panel last heard
 * @param hz the rate it is about, Hz
 */
void servo_screen_rate(servo_rate_state_t st, uint16_t hz);

/**
 * @brief Whether the IOmcu can sweep: protocol 4.2 or later.  SWEEP is
 *  offered only then.
 *
 * @param able true when the IOmcu can sweep
 */
void servo_screen_set_sweep(bool able);

/**
 * @brief Whether a surface is bound, as the panel last read the binding: SWEEP
 *  is offered only then, since a sweep of nothing never starts.
 *
 * @param any true when a surface is bound
 */
void servo_screen_set_surfaces(bool any);

/**
 * @brief Whether the IOmcu holds the output encoder enabled, as SENSE's
 *  ENABLE register was last read or written.  The SENSE set-up is written
 *  only while the bank is disarmed: a run started with the AS5600 setting on
 *  and this false has no angles, and takes no angle columns.
 *
 * @param held true when the IOmcu holds it enabled
 */
void servo_screen_set_enc_held(bool held);

/**
 * @brief Whether the rotary knob answers: the MT6701 at 0x06 acknowledged
 *        its last poll (knob_task.h), every 10ms while it answers and
 *        every 100ms while it does not.  RUDDER shows its step keys while
 *        SETUP > PANEL's Rotary knob is OFF or this is false; the panel
 *        tells it every frame, and the screen starts at false.
 * @param answers the last poll answered, with the knob's setting on
 */
void servo_screen_set_knob(bool answers);

/**
 * @brief The IMU's AXIS and SIGN, as SETUP > SENSORS > IMU sets them: the
 *        name a run with ANGLE SOURCE IMU gives its report and CSV
 *        ("IMU X+").  The angle itself comes in through
 *        servo_screen_encoder(), in AS5600 counts from count 0 and in
 *        degrees as read (servo_angle_enc()).
 * @param axis 0 X, 1 Y, 2 Z.
 * @param neg  The SIGN negative.
 */
void servo_screen_set_imu_axis(uint8_t axis, bool neg);

/**
 * @brief The IMU's zero generation, which ZERO, CLEAR ZERO and IMU OFF with
 *        a zero kept raise (imu_ctl_t's zero_gen).  Another generation
 *        moves every IMU angle: RUDDER drops a zero and MAX taken on the
 *        IMU before it, as for another AXIS or SIGN.
 * @param gen The generation.
 */
void servo_screen_set_imu_zero(uint32_t gen);

/**
 * @brief How much longer the IMU's filtered angle can take to settle after
 *        the surface stopped (imu_filter_settle_ms()): AUTO on RUDDER adds
 *        it to SERVO_RAUTO_MOVE_MS with ANGLE SOURCE IMU.
 * @param ms The time, ms; 0 for IMU FILTER 1.
 */
void servo_screen_set_imu_settle(uint32_t ms);

/** The pin servo_screen_set_drive() takes for a port without a known pin. */
#define SERVO_SCREEN_NO_PIN 0xFFu
/** The target servo_screen_set_drive() takes while the IOmcu's page
 *  is there and not known: the tester sends no command. */
#define SERVO_SCREEN_TARGET_UNKNOWN 0xFEu
/** The target servo_screen_set_drive() takes with no IOmcu
 *  answering. */
#define SERVO_SCREEN_TARGET_NO_LINK 0xFFu

/**
 * @brief Where the tester's commands go now (prog_link_drive()): @p target 0 for
 *  the standard outputs, 1 + n for programming port n,
 *  SERVO_SCREEN_TARGET_UNKNOWN or SERVO_SCREEN_TARGET_NO_LINK; @p gpio that
 *  port's GPIO or SERVO_SCREEN_NO_PIN, and @p pwm whether the port renders
 *  its pulse.  The TARGET row follows it, not the TARGET setting.
 *  @p gpio and @p pwm are ignored without a port target.
 *
 * @param target 0 for the outputs, 1 + n for programming port n, or a
 *   SERVO_SCREEN_TARGET_* value
 * @param gpio that port's GPIO
 * @param pwm that port renders its pulse
 */
void servo_screen_set_drive(uint8_t target, uint8_t gpio, bool pwm);

/**
 * @brief The GPIOs bound as SERVO PWM outputs (bit n is GPIO n), from the binding
 *  the tester drives, and whether a binding has been read.  The TARGET row
 *  names them while the drive uses the outputs: as many pins as the row's
 *  23 cells hold, then "+N" (ui_pinlist()).
 *
 * @param gpio_mask the GPIOs, bit n = GPIO n
 * @param known a binding was read
 */
void servo_screen_set_pins(uint64_t gpio_mask, bool known);

/**
 * @brief The TARGET row's value in the current language: `protocol pins`.
 *
 * The outputs read "SERVO PWM GPIO 2 GPIO 4", the protocol name the same in
 * every language: as many pins as the 25 cells hold after it, then a count
 * ("SERVO PWM GPIO 2 +2").  A port target reads "PROG PORT 1 GPIO 13", or
 * "PROG PORT 1" without a known pin; while the port renders no pulse
 * "PORT 1 GPIO 13 PWM OFF" or "PROG PORT 1 PWM OFF": 22 characters at most,
 * of the 25 cells the row's value holds.
 *
 * The row is the control that picks the target: a tap opens SETUP >
 * OUTPUTS on what the row shows -- the PROG PORT entry while the IOmcu
 * drives the port, SERVO PWM while it drives the outputs, the overview
 * while the target is not known -- where SERVO TESTER sets it.  It takes no
 * press while servo_screen_target_locked().
 *
 * @param out     takes the text
 * @param size    bytes at @p out
 * @param target  the drive's target, as servo_screen_set_drive(): 0 the
 *                outputs, 1 to 4 a port, or SERVO_SCREEN_TARGET_UNKNOWN or
 *                SERVO_SCREEN_TARGET_NO_LINK
 * @param gpio    the port's pin, or SERVO_SCREEN_NO_PIN
 * @param pwm     whether the port renders its pulse
 * @param pins    the SERVO PWM pins, bit n for GPIO n, as
 *                servo_screen_set_pins()
 * @param known   whether the binding is read
 * @return whether the line takes the warning colour: a port target, no
 *         SERVO PWM pin, no binding read, the target not known, or no link
 */
bool servo_target_line(char *out, size_t size, uint8_t target, uint8_t gpio,
                       bool pwm, uint64_t pins, bool known);

/**
 * @brief The task the strip shows, a ui_wb_task_t.  For tests.
 *
 * @return the task
 */
int servo_screen_task(void);

/**
 * @brief The PORT button's line, for tests: what the function screens
 *        drive, as servo_target_line() words it for the screen's drive.
 * @param out  Takes the line.
 * @param n    Its size.
 * @return Whether it takes the warning colour.
 */
bool servo_screen_port_line(char *out, size_t n);

/**
 * @brief The frame rate's note, for tests: whether the rate reached the
 *        pins, what the IOmcu runs at it, in two lines.  The hub list's
 *        OUTPUT heading shows one of them.
 * @param l1 Takes the first line.
 * @param l2 Takes the second.
 * @param n  The size of each.
 * @return Whether it takes the warning colour.
 */
bool servo_screen_rate_note(char *l1, char *l2, size_t n);

/**
 * @brief The PORT button's choice, for tests.
 * @return Its entries' count while it shows (OUTPUTS, then PROG PORT 1 to
 *         4), -1 while it does not.
 */
int servo_screen_port_choices(void);

/**
 * @brief Entry @p i of the PORT button's choice, for tests.
 * @param i       The entry, 0 for OUTPUTS.
 * @param refused Takes whether the carrier refuses it; may be NULL.
 * @return Its label, the refusal line on a refused one; "" past the last.
 */
const char *servo_screen_port_choice(int i, bool *refused);

/**
 * @brief Entry @p i's rectangle in the body, of the PORT list or the SERVO
 *        list (one column each), for tests.
 * @param i The entry.
 * @return Its rectangle.
 */
gfx_rect_t servo_screen_port_choice_rect(int i);

/**
 * @brief Which page of rows shows, one level down from its screen.  For
 *        tests.
 * @return 0 none, 1 KST's EXPERT MODE.  TEST's and LIMIT RUN's settings
 *         are lists (servo_screen_list())
 */
int servo_screen_run_settings(void);

/**
 * @brief Whether the TARGET row takes no press: the bench ARMED, a sweep (running
 * or paused) or a run under way, the servo programmer holding the port.
 * The row names the target all the same.
 *
 * @return true when it takes no press
 */
bool servo_screen_target_locked(void);

/**
 * @brief Whether an ARM was posted and not answered.
 * @return true then
 */
bool servo_screen_arm_asked(void);

/**
 * @brief Whether a finger holds ARM, or RUDDER AUTO's ARM + START, and its
 *        2s hold has not fired yet, or the ARM + START sequence is under
 *        way: the output it will arm is the one shown when the hold began,
 *        so the PORT list takes no open and no pick meanwhile.
 * @return true then
 */
bool servo_screen_arm_held(void);

/**
 * @brief Whether ARM's hold on LIVE, SWEEP or RUDDER switches a part on
 *        before it arms: PORT PWM on a PROG PORT, the carrier's PROG 5V,
 *        the PD mini's output where it feeds the servo.
 * @return true while one of them reads off; false on any other screen
 */
bool servo_screen_arm_switches(void);

/**
 * @brief Whether OUTPUTS' SERVO TESTER segment (the TARGET setting) takes no
 * press: servo_screen_target_locked(), or the bench ARMED for the motor.
 *
 * @return true when it takes no press
 */
bool servo_screen_tester_target_locked(void);

/**
 * @brief Whether OUTPUTS' PORT PWM segment takes no press: OUTPUTS as the
 * tester's target, the servo programmer holding the port, or a sweep or a
 * run under way; and to switch it on, the bench ARMED or asked to arm.
 *
 * @return true when it takes no press
 */
bool servo_screen_port_pwm_locked(void);

/** A sweep command found no surface to sweep and was not sent: the screen
 *  stops waiting for its start and ends the sweep. */
void servo_screen_sweep_refused(void);

/**
 * @brief Whether a sweep is running, for the application and tests.
 *
 * @return true while a sweep runs
 */
bool servo_screen_sweeping(void);

/**
 * @brief Whether OUTPUT ON of the output card's SET row, which no function
 *        screen shows, is being held, from an output that is off, or a
 *        question about the supply stands: a set point typed on the SET
 *        row of LIVE, SWEEP or RUDDER (SUPPLY's question or the HV
 *        warning), the HV warning before an ON (ASK_ON), a test's HV
 *        steps, or the
 *        restore of the operator's set points (servo_screen_asks_restore()).
 *        An ON either sends lands at the set points it was opened on.
 * @return true while the hold runs or the question stands
 */
bool servo_screen_output_held(void);

/**
 * @brief Whether SERVO asks to restore the operator's set points onto the
 *        live output a programming port left on
 *        (supply_screen_restore_offer()), as the question or, after YES to
 *        a voltage above 6.0V, as the HV warning.
 * @return true while it asks
 */
bool servo_screen_asks_restore(void);

/**
 * @brief Whether a sweep is paused: PAUSE tapped, the hold in force, nothing
 *  else commanded since.  For tests.
 *
 * @return true while a sweep is paused
 */
bool servo_screen_paused(void);

/**
 * @brief The panel let go of pause @p pause_seq.
 *
 * A HOLD the far end had already ended, or one answered late: the panel
 * released the surfaces to their center and dropped the commands asked
 * during that pause.  Taken only while the screen's commands still derive
 * from that pause; one that has moved on since was sent, and the surfaces
 * follow it.
 *
 * @param pause_seq The pause the panel let go of.
 */
void servo_screen_released(uint16_t pause_seq);

/**
 * @brief The IOmcu started the sweep with its curve's phase 0 @p age_ms ago,
 * its output starting from @p from.  Until this, a sweep asked for is not
 * drawn: the far end is not known to move.  From it the horn is drawn along
 * the curve, and without feedback worked on from where that output was
 * when the far end began to move.  @p since_ms is, for
 * SERVO_SWEEP_FROM_FROZEN, how long ago the far end froze, and for
 * SERVO_SWEEP_RESUMED how long ago the resume was acknowledged; for a
 * resume @p age_ms is the panel's timing of the far end's phase 0, moved on
 * by the hold, whatever this screen took the pause's phase to be.
 * @p start_seq is the sweep command whose write it acknowledges
 * (servo_cmd_t): a start or resume waited for is ended only by its own.
 *
 * @param start_seq the sweep command whose write it acknowledges
 * @param age_ms how long ago the curve's phase 0 was, ms
 * @param from where the output starts from
 * @param since_ms how long ago the far end froze or the resume was
 *   acknowledged, ms
 */
void servo_screen_sweep_started(uint16_t start_seq, uint32_t age_ms,
                                servo_sweep_from_t from, uint32_t since_ms);

/**
 * @brief The IOmcu took the HOLD of pause @p pause_seq (servo_cmd_t) and kept
 * its curve @p kept_ms in (servo_phase_held()).  The curve ran on until
 * then, so the paused phase drawn here is set to it, not to the tap's.
 * Nothing unless that pause still stands: one resumed before this arrives
 * is rebased by its SERVO_SWEEP_RESUMED, and another pause's is not this
 * one's.
 *
 * @param pause_seq the pause whose HOLD was taken
 * @param kept_ms how far into its curve the far end kept it, ms
 */
void servo_screen_sweep_held(uint16_t pause_seq, uint32_t kept_ms);

/**
 * @brief How far the sweep's curve clock is from its phase 0, paused or not.
 *  For tests, which stand in for the panel's timing.
 *
 * @return the curve clock, ms
 */
uint32_t servo_screen_curve_ms(void);

/**
 * @brief Set the commanded angle without a touch event, for the application and
 * for tests.  A position like the dial's: taken on an armed bench, and
 * refused with nothing changed on a disarmed one.
 *
 * @param deg the angle, degrees from center
 */
void servo_screen_set_commanded(float deg);

/* ------------------------------------------------- the automatic test */

/*
 * START TEST on TEST's AUTOMATIC TEST card runs servo_test.h's automatic
 * test from this screen: a two-second hold, or HV SERVOS ONLY held for two
 * seconds when a step is above 6.0V.  It needs an armed bench and a supply that answers,
 * and it commands the servo and SUPPLY's set points and output itself.
 * Progress and the result are shown on its card and in LIVE's box.
 */

/**
 * @brief The panel's millisecond clock, every frame before the tick: the clock the
 * supply's readings are stamped with (supply_state_t.taken_ms), so a travel
 * time is measured on one clock.  Without it the screen's own is used.
 *
 * @param now_ms the clock, ms
 */
void servo_screen_clock(uint32_t now_ms);

/**
 * @brief Whether the link to the IOmcu is up; a run ends when it goes.
 *
 * @param up true when the link is up
 */
void servo_screen_set_link(bool up);

/**
 * @brief Whether a run is under way, for the application and tests.
 *
 * @return true while a run is under way
 */
bool servo_screen_testing(void);

/**
 * @brief Whether a limit run (servo_limrun.h) is under way, for the tests.
 *
 * @return true while one is under way
 */
bool servo_screen_limit_running(void);
/**
 * @brief The limit run's phase (servo_limrun_phase_t), for the tests.
 *
 * @return the phase
 */
int servo_screen_limit_phase(void);
/** @brief Whether AUTO on RUDDER (servo_rudder_auto.h) is under way.
 *  @return true while its run runs */
bool servo_screen_rauto_running(void);
/** @brief AUTO's phase, for the tests.
 *  @return a servo_rauto_phase_t, as an int */
int servo_screen_rauto_phase(void);
/** @brief The servo family a limit run started now takes, LIMIT RUN's
 *         own SERVO (SET_LIMIT_FAMILY), for the tests.
 *  @return a servo_limrun_family_t, as an int */
int servo_screen_limit_family(void);
/** @brief The servo family a RUDDER AUTO run started now takes, RUDDER
 *         AUTO's own SERVO (SET_RAUTO_FAMILY), for the tests.
 *  @return a servo_rauto_family_t, as an int */
int servo_screen_rauto_family(void);
/** @brief The choice list a READY card's SERVO row (LIMIT RUN, RUDDER
 *         AUTO) or SERIES row (HITEC) opens, for the tests.
 *  @return its entries' count while it shows, -1 while it does not */
int servo_screen_family_choices(void);
/** @brief Entry @p i of that list, for the tests; its rectangle is
 *         servo_screen_port_choice_rect()'s.
 *  @param i the entry, 0 for KST/CHASERVO or D SERIES
 *  @return its label, "" past the last */
const char *servo_screen_family_choice(int i);
/** @brief Why the SERVO row of the screen shown takes no pick now, for
 *         the tests.
 *  @return the reason, NULL while it takes one */
const char *servo_screen_family_refused(void);
/** @brief SERVO PULSES for the session, for the tests.
 *  @return true while it reads 1000-2000 */
bool servo_screen_limit_pulses(void);
/** @brief Why a limit run cannot start now, ARM + START's steps aside:
 *         the gate's first text, for the tests.
 *  @return the text, or NULL */
const char *servo_screen_limit_blocked(void);
/** @brief SEARCH + PROGRAM chosen in LIMIT RUN's list, for the tests.
 *  @return true while MODE reads SEARCH + PROGRAM */
bool servo_screen_limit_prog(void);
/**
 * @brief LIMIT RUN's left card, its state line @p i as it reads now, for
 *        the tests.
 * @param i   the line, from 0
 * @param buf takes it
 * @param n   its size
 * @return @p buf; "" past the last
 */
const char *servo_screen_limit_line(int i, char *buf, size_t n);

/**
 * @brief The run's next item for the card, without taking it
 *        (servo_test_peek()), and taking it once it is on its way.
 *
 * The application drains these every frame, whichever screen is up.  A run
 * started with USER LOGS off on SETUP > SD CARD has no item: its items are
 * dropped here.
 * @param text receives the item's text
 * @return the item; SERVO_TEST_OUT_NONE for none
 */
servo_test_out_t servo_screen_test_peek(const char **text);
/**
 * @brief The writer of the outbox servo_screen_test_peek() last handed out:
 *        the AUTOMATIC TEST's, the LIMIT RUN's or AUTO's on RUDDER.
 * @return LOG_PREFIX_ATEST, LOG_PREFIX_LIMIT or LOG_PREFIX_RUDDR
 *         (log_name.h), as an int
 */
int servo_screen_test_prefix(void);
/** @brief Takes the item servo_screen_test_peek() handed out. */
void servo_screen_test_pop(void);

/**
 * @brief The number of the files the run's items went to, and whether the
 *        card took the .TXT whole.
 *
 * The files are ATESTnnn.CSV and, with REPORT, ATESTnnn.TXT; a limit run's
 * LIMITnnn.TXT; or AUTO's RUDDRnnn.CSV and RUDDRnnn.TXT.  Ignored for a
 * run started with USER LOGS off, which sent the logger nothing.
 * @param number the number; -1 when the card took none
 * @param report whether the card took the .TXT whole
 */
void servo_screen_test_files(int number, bool report);
/**
 * @brief Whether the logger still holds an OPEN or an END the outbox gave
 *        it, or an OPEN waits for the queue.
 *
 * While it does, no automatic test and no limit run starts (REPORT STILL
 * BEING WRITTEN), so the number and report state
 * servo_screen_test_files() passes go to the run that sent them.  Every
 * frame, after servo_screen_test_files().
 * @param busy whether the logger holds a file operation or one waits
 */
void servo_screen_test_logger_busy(bool busy);

/**
 * Every frame, whichever screen is up: a run's end is seen, and SUPPLY's
 * set points go back to what they were before it once its OFF has gone, a
 * sample taken after that shows the output off, no ON is on its way and
 * OUTPUT ON is not being held, here or on SUPPLY -- unless they were set
 * since it ended.
 */
void servo_screen_service(void);
