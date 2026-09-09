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

/** True when a command was waiting; clears it. */
bool motor_screen_poll_cmd(motor_cmd_t *out);

void motor_invalidate(void);
const ui_screen_t *motor_screen(void);

#ifdef __cplusplus
}
#endif
