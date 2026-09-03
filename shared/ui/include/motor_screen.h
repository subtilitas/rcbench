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

void motor_screen_set(const bench_state_t *b);
/** Called per sample, so the plot's time base is the sample rate. */
void motor_screen_push(const bench_state_t *b);
void motor_screen_set_armed(bool armed);
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
