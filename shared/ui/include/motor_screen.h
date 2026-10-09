/*
 * The motor and ESC (electronic speed controller) bench screen.
 *
 * Owns no hardware and performs no I/O (input/output).  It is handed a
 * bench_state_t and touch events, and commands are read back out, so the
 * same code renders to a PNG (Portable Network Graphics) file on the host
 * and is tested for what it decides as well as for what it draws.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "bench_state.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOTOR_PANE_PLOT = 0,
    MOTOR_PANE_TABLE,
    MOTOR_PANE_COUNT
} motor_pane_t;

/** What the screen asks the application to do.  One slot, coalescing. */
typedef enum {
    MOTOR_CMD_NONE = 0,
    MOTOR_CMD_ARM,
    MOTOR_CMD_DISARM,
    MOTOR_CMD_THROTTLE,   /**< value carries the percentage */
    MOTOR_CMD_RESET_PEAKS,
} motor_cmd_kind_t;

typedef struct {
    motor_cmd_kind_t kind;
    float            value;
} motor_cmd_t;

/** Called per sample, so the plot's time base is the sample rate.  The
 *  readouts take every sample; the plot takes them only while armed. */
void motor_screen_push(const bench_state_t *b);

/**
 * What the bench is doing, which is what bounds a run.
 *
 * Arming clears the plot and starts it; disarming holds it as it stood.  The
 * caller reports the bench's own arm state, not a request made of it: a
 * disarm that has been asked for and not yet answered must not erase the run
 * it is ending.
 */
void motor_screen_set_armed(bool armed);

typedef enum {
    MOTOR_PLOT_EMPTY = 0,   /**< no run recorded since the screen reset */
    MOTOR_PLOT_RECORDING,   /**< the bench is armed; the trace advances */
    MOTOR_PLOT_HELD,        /**< the last run, held as it ended         */
} motor_plot_state_t;

motor_plot_state_t motor_screen_plot_state(void);

/** Samples in the trace, capped at UI_PLOT_HISTORY.  The plot is narrower
 *  than that, so a longer run is drawn truncated to its newest columns. */
int motor_screen_plot_samples(void);

/**
 * Whether channel @p channel's peak is drawn as a value -- 0 voltage, 1
 * current, 2 power, 3 rpm: a valid reading of it has arrived since the run
 * started or the peaks were reset.  It stays drawn when the live reading
 * goes empty, as when a source stops answering mid-run and the run's peaks
 * it measured stand.
 */
bool motor_screen_peak_shown(int channel);

/** Abandon a hold that is under way, because the bench has been stopped.
 *  A stop on a bench that was not armed changes nothing about whether it is
 *  armed, and the gesture must end all the same. */
void motor_screen_cancel_arm(void);
/**
 * The kV the connected ESC reports, or 0 when it reports none.  Preferred
 * over the SET_MOTOR_KV setting when it is non-zero.
 */
void motor_screen_set_esc_kv(int kv);

float motor_screen_throttle(void);
void motor_screen_set_throttle(float pct);

/**
 * Move the throttle by @p span_fraction of the slider's span, from the
 * rotary knob (knob.h).  Relative: the value changes by how far the knob
 * turned.  Nothing happens for a zero fraction or while a finger is
 * dragging the slider.  It posts the same throttle command a touch does and
 * never arms.  It posts only into an empty slot or over a throttle: any other
 * pending command (an arm, a disarm, a peak reset) stays and the knob's
 * motion is dropped.
 */
void motor_screen_knob(float span_fraction);

/**
 * Start of a frame, before its touch events: forget that a finger owned the
 * slider.  The knob moves nothing in a frame in which a finger owned the
 * slider at any point, including a press and release inside it, because the
 * touch has already set the value the knob would move.
 */
void motor_screen_knob_frame(void);

/**
 * Withdraw the knob's throttle command if it is still waiting to be taken:
 * the slider returns to its value from before the knob, and a throttle that
 * was pending before the knob moved it is pending again with that value.  A
 * command posted by a touch since, or already taken, is left alone.
 */
void motor_screen_knob_cancel(void);

/** True when a command was waiting; clears it. */
bool motor_screen_poll_cmd(motor_cmd_t *out);

void motor_invalidate(void);
const ui_screen_t *motor_screen(void);

#ifdef __cplusplus
}
#endif
