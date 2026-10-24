/**
 * @file
 * @brief The servo bench: the outputs the binding marks as surfaces, commanded
 * together by dragging one horn.
 *
 * A workbench (ui_workbench.h): the strip under the band shows LIVE (the
 * dial, SWEEP, CENTRE, RELEASE), TEST (the AUTOMATIC TEST and LIMIT RUN
 * cards, each opening its run's settings one level down) and SETTINGS
 * (OUTPUT, SWEEP, DUT).  The output card on the right -- the readings, the
 * TARGET row, ARM -- is the same on every task.  A tile opens LIVE; leaving
 * the screen switches it to SAFE, whichever task is shown.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

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

const ui_screen_t *servo_screen(void);

/**
 * Move the horn by @p span_fraction of its travel (-travel to +travel),
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
 */
void servo_screen_knob(float span_fraction);

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

/** Take the pending command, if any.  Cleared by reading. */
bool servo_screen_take(servo_cmd_t *out);

/**
 * A reading of the output encoder (an AS5600 on the horn shaft), each one
 * once, in the order they arrive.  With AS5600 on in SETUP the MEASURED row
 * shows its angle from the centre count, and a run reads it for the angle
 * results in its CSV and report.  A reading that is not valid -- the sensor
 * not answering, or answering and detecting no magnet -- shows "---", and
 * ENC CENTRE takes nothing from it.
 */
void servo_screen_encoder(const servo_test_enc_t *e);

/** What the output is actually doing, from the bench or from the model.
 *  @p current_a is not shown: the CURRENT row reads the servo rail's meter
 *  (servo_screen_source()). */
void servo_screen_feedback(uint16_t position_us, float current_a, bool valid);

/**
 * Whether the bench is armed for the surfaces, so the button can say it.
 * An arm for the motor alone is not one: ARM on this screen arms the
 * surfaces, and a bench armed for the motor disarms before it does.
 *
 * The screen does not decide whether it is armed: arming is asked of the
 * policy and the answer comes back from the bench, the same way MOTOR & ESC
 * learns it.
 *
 * The commanded value follows the bench.  At every arm it becomes the rest
 * the pins drive -- outputs_role_rest() across the range a command carries,
 * 1500 us for STANDARD PWM and 760 us for NARROW 760 -- and without
 * feedback the horn is drawn there at once.  While the bench is disarmed no
 * input changes it: the dial, the knob, CENTRE, SWEEP and
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
 */
void servo_screen_set_armed(bool armed);

/**
 * Whether the bench is armed for any role.  servo_screen_set_armed() is
 * told whether the surfaces are armed, which an arm for the motor alone
 * leaves false; this is what keeps the servo programmer from taking a port
 * under a motor that is armed, as it is kept from one under armed surfaces.
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

/** Commanded pulse width, for the application and for tests. */
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
 * @brief The encoder's angle for a SERVO run's log row, as MEASURED shows it:
 *        AS5600 on in SETUP, a valid reading and the link up.
 *
 * @param deg Receives the horn's angle from the centre count, deg.
 * @return true when an angle is shown; false with @p deg NULL.
 */
bool servo_screen_log_angle(float *deg);

/**
 * @brief The keypad over the left column: a set point's, a settings row's
 *        or TRIM's, which is signed; for tests and the renderer.
 * @return The keypad, open or closed.
 */
const ui_keypad_t *servo_screen_keypad(void);
/** The value of the COMMANDED row as it is drawn: the pulse in us, and
 *  "---" while the servo programmer holds the port and no pulse is on it. */
const char *servo_screen_commanded_text(void);

/** The pulse width the solid arm is drawn at: measured, or without
 *  feedback the drawing's estimate of the output.  For tests. */
uint16_t servo_screen_drawn(void);

/** The frame rate and the type in force, for the application and tests. */
uint16_t servo_screen_frame_hz(void);
const char *servo_screen_type_name(void);

/**
 * One sample of the supply that feeds the servo.  The automatic test reads
 * every one: as its reading in a run on the PD mini or the model, and for
 * the supply's state alone in a run on the INA3221.  While the servo rail's
 * meter is not the INA3221
 * (servo_screen_source()) it is also the rail's reading: the CURRENT row,
 * the line over the plot and one point of the plot.
 */
void servo_screen_supply(const supply_state_t *s);

/**
 * servo_screen_supply() with the meter @p id and its change count
 * @p changes as decided at the poll that took the sample, for samples that
 * waited on a queue.  A sample whose count is ahead of the meter in force
 * brings its meter with it, as servo_screen_source() would.  One whose
 * count is behind was taken under a meter replaced since: the automatic
 * test reads it and it is the supply's last reading on the line and in the
 * row, and it is no point of the plot.
 */
void servo_screen_supply_at(const supply_state_t *s, servo_source_id_t id,
                            uint32_t changes);

/**
 * The servo rail's meter, as servo_source.h decides it, every frame before
 * that frame's windows and supply samples.  @p changes is
 * servo_source_changes() of the same poll: the meter is taken unless the one
 * in force was decided at a later count, which a window's or a supply
 * sample's can be (servo_screen_window(), servo_screen_supply_at()).
 *
 * The CURRENT row, the line over the plot and the plot show that meter's
 * reading.  SERVO_SOURCE_INA3221: the last CH1 window -- mean current, mean
 * bus voltage at the load side of the shunt and their product -- under the
 * label INA3221 CH1.  SERVO_SOURCE_PDMINI and SERVO_SOURCE_MODEL: the
 * supply's last sample under the label PD MINI, the PD mini's reading or the
 * model's; with the model (supply_screen_model()) the MODELLED pill sits on
 * the plot's top-left corner.  SERVO_SOURCE_MODEL until the first call.  The
 * name takes its cells from the readings beside it: beside INA3221 CH1 they
 * are 60, 64 and 56 px wide, and a power wider than that is drawn in whole
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
 */
void servo_screen_source(servo_source_id_t id, uint32_t changes);

/**
 * @p told is called with true when a run of the automatic test starts and
 * with false when it ends, inside the call that starts or ends it -- a
 * touch event, a window, a supply sample, the meter, the link, a tick --
 * and not at the next frame.  The panel's control task writes no bench log
 * row between the two (log_cadence_bench_run()).  NULL for none.  It is
 * called from the task that runs the screen and must not call back into
 * it.
 */
void servo_screen_on_testing(void (*told)(bool running));

/**
 * Why the INA3221 is not the servo rail's meter now (servo_source_why()),
 * every frame with servo_screen_source().  A run started on the PD mini
 * with the INA3221 on in SETUP states it in its report.
 */
void servo_screen_source_why(servo_source_why_t why);

/**
 * Why the INA3221 last stopped being the meter, and the change count of
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
 */
void servo_screen_source_dropped(servo_source_why_t why, uint32_t at);

/**
 * One 50 ms window of INA3221 CH1 (sense_link_take_win()), each one once, in
 * the order they arrive, with the meter @p id and its change count
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
 */
void servo_screen_window(const sense_link_win_t *w, servo_source_id_t id,
                         uint32_t changes);

/** How many points the power plot holds, and its series @p series (0 the
 *  voltage in V, 1 the current in A, 2 the power in W) @p back points
 *  before the newest: ui_plot_sample().  For tests. */
int   servo_screen_power_points(void);
float servo_screen_power_sample(int series, int back);

/**
 * The OUTPUT page's note while a rate is SERVO_RATE_IN_FORCE: two lines into
 * @p l1 and @p l2 of @p size bytes each, for the rate @p hz, the drive
 * @p target / @p gpio / @p pwm (as servo_screen_set_drive()) and the PWM
 * TARGET setting @p setting (SET_SERVO_TARGET: 0 OUTPUTS, 1 PROG PORT, the
 * target the panel asks for).  The first line names the target the
 * IOmcu holds, not the setting: "In force: OUTPUTS run at 50 Hz.",
 * "In force: PROG PORT GP13 runs at 560 Hz.", the port without a known pin,
 * or the target not known (the page not known).  With no link it names no
 * rate: "No link: the rate in force is not known.".  The second line
 * says "The TARGET chosen is not active." while the setting and the
 * drive differ, "PORT PWM is OFF" while the port renders no pulse, else
 * that a PPM output keeps its own frame.  Returns whether the note takes
 * the warning colour: the setting and the drive differ, no link, the
 * target not known, a port without a known pin, or a port with its pulse
 * off.  Neither line is written with a NULL buffer or a @p size of 0, and
 * false is returned.
 */
bool servo_in_force_note(char *l1, char *l2, size_t size, uint16_t hz,
                         uint8_t target, uint8_t gpio, bool pwm, int setting);

/**
 * What the panel last heard about the frame rate @p hz.  Called every frame;
 * a state about a rate other than the one the screen shows reads as
 * SERVO_RATE_UNSENT for it.
 */
void servo_screen_rate(servo_rate_state_t st, uint16_t hz);

/** Whether the IOmcu can sweep: protocol 4.2 or later.  SWEEP is
 *  offered only then. */
void servo_screen_set_sweep(bool able);

/** Whether a surface is bound, as the panel last read the binding: SWEEP
 *  is offered only then, since a sweep of nothing never starts. */
void servo_screen_set_surfaces(bool any);

/** Whether the IOmcu holds the output encoder enabled, as SENSE's
 *  ENABLE register was last read or written.  The SENSE set-up is written
 *  only while the bank is disarmed: a run started with the AS5600 setting on
 *  and this false has no angles, and takes no angle columns. */
void servo_screen_set_enc_held(bool held);

/** The pin servo_screen_set_drive() takes for a port without a known pin. */
#define SERVO_SCREEN_NO_PIN 0xFFu
/** The target servo_screen_set_drive() takes while the IOmcu's page
 *  is there and not known: the tester sends no command. */
#define SERVO_SCREEN_TARGET_UNKNOWN 0xFEu
/** The target servo_screen_set_drive() takes with no IOmcu
 *  answering. */
#define SERVO_SCREEN_TARGET_NO_LINK 0xFFu

/** Where the tester's commands go now (prog_link_drive()): @p target 0 for
 *  the standard outputs, 1 + n for programming port n,
 *  SERVO_SCREEN_TARGET_UNKNOWN or SERVO_SCREEN_TARGET_NO_LINK; @p gpio that
 *  port's GPIO or SERVO_SCREEN_NO_PIN, and @p pwm whether the port renders
 *  its pulse.  The TARGET row follows it, not the TARGET setting.
 *  @p gpio and @p pwm are ignored without a port target. */
void servo_screen_set_drive(uint8_t target, uint8_t gpio, bool pwm);

/** The GPIOs bound as SERVO PWM outputs (bit n is GPIO n), from the binding
 *  the tester drives, and whether a binding has been read.  The TARGET row
 *  names them while the drive uses the outputs: as many pins as the row's
 *  23 cells hold, then "+N" (ui_pinlist()). */
void servo_screen_set_pins(uint64_t gpio_mask, bool known);

/**
 * The TARGET row's value in the current language, into @p out of @p size
 * bytes, for the drive @p target / @p gpio / @p pwm (as
 * servo_screen_set_drive()) and the SERVO PWM pins @p pins / @p known (as
 * servo_screen_set_pins()): `protocol pins`.  Returns whether the line
 * takes the warning colour: a port target, no SERVO PWM pin, no binding
 * read, the target not known, or no link.  The outputs read
 * "SERVO PWM GP2 GP4", the protocol name the same in every language: as
 * many pins as the 23 cells hold after it, then a count
 * ("SERVO PWM GP2 +2").  A port target reads "PROG PORT GPn", or
 * "PROG PORT" without a known pin, each with " PWM OFF" after it while the
 * port renders no pulse: 22 characters at most, of the 23 cells the row's
 * value holds.
 *
 * The row is the control that picks the target: a tap opens SETUP >
 * OUTPUTS on what the row shows -- the PROG PORT entry while the IOmcu
 * drives the port, SERVO PWM while it drives the outputs, the overview
 * while the target is not known -- where SERVO TESTER sets it.  It takes no press
 * while servo_screen_target_locked().
 */
bool servo_target_line(char *out, size_t size, uint8_t target, uint8_t gpio,
                       bool pwm, uint64_t pins, bool known);

/** The task the strip shows, a ui_wb_task_t.  For tests. */
int servo_screen_task(void);

/** The page SETTINGS shows: 0 OUTPUT, 1 SWEEP, 2 DUT.  For tests. */
int servo_screen_settings_page(void);

/** Which run's settings TEST shows, one level down from its card: 0 none,
 *  1 the automatic test's, 2 the limit run's.  For tests. */
int servo_screen_run_settings(void);

/**
 * Whether the TARGET row takes no press: the bench ARMED, a sweep (running
 * or paused) or a run under way, the servo programmer holding the port.
 * The row names the target all the same.
 */
bool servo_screen_target_locked(void);

/**
 * Whether OUTPUTS' SERVO TESTER segment (the TARGET setting) takes no
 * press: servo_screen_target_locked(), or the bench ARMED for the motor.
 */
bool servo_screen_tester_target_locked(void);

/**
 * Whether OUTPUTS' PORT PWM segment takes no press: OUTPUTS as the
 * tester's target, the servo programmer holding the port, or a sweep or a
 * run under way; and to switch it on, the bench ARMED or asked to arm.
 */
bool servo_screen_port_pwm_locked(void);

/** A sweep command found no surface to sweep and was not sent: the screen
 *  stops waiting for its start and ends the sweep. */
void servo_screen_sweep_refused(void);

/** Whether a sweep is running, for the application and tests. */
bool servo_screen_sweeping(void);

/** Whether a sweep is paused: PAUSE tapped, the hold in force, nothing
 *  else commanded since.  For tests. */
bool servo_screen_paused(void);

/**
 * The panel let go of pause @p pause_seq -- a HOLD the far end had already
 * ended, or one answered late -- released the surfaces to their centre and
 * dropped the commands asked during that pause.  Taken only while the
 * screen's commands still derive from that pause; one that has moved on
 * since was sent, and the surfaces follow it.
 */
void servo_screen_released(uint16_t pause_seq);

/**
 * The IOmcu started the sweep with its curve's phase 0 @p age_ms ago,
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
 */
void servo_screen_sweep_started(uint16_t start_seq, uint32_t age_ms,
                                servo_sweep_from_t from, uint32_t since_ms);

/**
 * The IOmcu took the HOLD of pause @p pause_seq (servo_cmd_t) and kept
 * its curve @p kept_ms in (servo_phase_held()).  The curve ran on until
 * then, so the paused phase drawn here is set to it, not to the tap's.
 * Nothing unless that pause still stands: one resumed before this arrives
 * is rebased by its SERVO_SWEEP_RESUMED, and another pause's is not this
 * one's.
 */
void servo_screen_sweep_held(uint16_t pause_seq, uint32_t kept_ms);

/** How far the sweep's curve clock is from its phase 0, paused or not.
 *  For tests, which stand in for the panel's timing. */
uint32_t servo_screen_curve_ms(void);

/**
 * Set the commanded angle without a touch event, for the application and
 * for tests.  A position like the dial's: taken on an armed bench, and
 * refused with nothing changed on a disarmed one.
 */
void servo_screen_set_commanded(float deg);

/* ------------------------------------------------- the automatic test */

/*
 * START TEST on TEST's AUTOMATIC TEST card runs servo_test.h's automatic
 * test from this screen: a two-second hold, or HV SERVOS ONLY held for two
 * seconds when a step is above 6.0 V.  It needs an armed bench and a supply that answers,
 * and it commands the servo and SUPPLY's set points and output itself.
 * Progress and the result are shown on its card and in LIVE's box.
 */

/**
 * The panel's millisecond clock, every frame before the tick: the clock the
 * supply's readings are stamped with (supply_state_t.taken_ms), so a travel
 * time is measured on one clock.  Without it the screen's own is used.
 */
void servo_screen_clock(uint32_t now_ms);

/** Whether the link to the IOmcu is up; a run ends when it goes. */
void servo_screen_set_link(bool up);

/** Whether a run is under way, for the application and tests. */
bool servo_screen_testing(void);

/** Whether a limit run (servo_limrun.h) is under way, for the tests. */
bool servo_screen_limit_running(void);
/** The limit run's phase (servo_limrun_phase_t), for the tests. */
int servo_screen_limit_phase(void);

/**
 * The run's next item for the card, without taking it (servo_test_peek()),
 * and taking it once it is on its way.  The application drains these every
 * frame, whichever screen is up.  A run started with USER LOGS off on
 * SETUP > SD CARD has no item: its items are dropped here.
 */
servo_test_out_t servo_screen_test_peek(const char **text);
void servo_screen_test_pop(void);

/**
 * The number of the files the run's items went to -- ATESTnnn.CSV and,
 * with REPORT, ATESTnnn.TXT, or a limit run's LIMITnnn.TXT -- or -1 when
 * the card took none; and whether the card took the .TXT whole.  Ignored
 * for a run started with USER LOGS off, which sent the logger nothing.
 */
void servo_screen_test_files(int number, bool report);
/** Whether the logger still holds an OPEN or an END the outbox gave it,
 *  or an OPEN waits for the queue.  While it does no automatic test and no
 *  limit run starts (LAST REPORT STILL WRITING), so the number and report
 *  state servo_screen_test_files() passes go to the run that sent them.
 *  Every frame, after servo_screen_test_files(). */
void servo_screen_test_logger_busy(bool busy);

/**
 * Every frame, whichever screen is up: a run's end is seen, and SUPPLY's
 * set points go back to what they were before it once its OFF has gone, a
 * sample taken after that shows the output off, no ON is on its way and
 * OUTPUT ON is not being held, here or on SUPPLY -- unless they were set
 * since it ended.
 */
void servo_screen_service(void);
