/*
 * The SUPPLY screen: a programmable supply set, switched and watched.
 *
 * MOTOR & ESC's layout with a supply's controls: the plot of voltage, current
 * and power on the left, the readouts on the right, the two set points below
 * the plot and the output switch beside them.  The output comes on with a
 * hold, as ARM does, and goes off with a tap.
 *
 * A set point is shown beside its reading everywhere: in brackets on the
 * rail's cards and dashed in the plot.  A tap on a card or on a set point's
 * value opens a keypad over the left column.  SETTINGS, top right, opens the
 * operator's limits over the same column -- caps on the set points, the set
 * points after a restart, and the trips -- kept in the settings model.  The
 * right column stays live under both, so OUTPUT OFF is always a tap away.
 *
 * Owns no hardware and performs no I/O (input/output): it is handed a
 * supply_state_t and touch events, and commands are read back out.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "supply.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SUPPLY_PANE_PLOT = 0,
    SUPPLY_PANE_TABLE,
    SUPPLY_PANE_COUNT
} supply_pane_t;

/**
 * What the screen asks the application to do since the last poll.  Both an
 * OFF and a RESET PEAKS can be pending at once.  An OFF outranks an ON: one
 * that is pending drops any ON asked after it.
 *
 * The set points are not in it.  They are a level, read with
 * supply_screen_set_v() and supply_screen_set_i(), so an application that
 * loses a command does not lose the voltage the screen shows.
 */
typedef struct {
    bool on;           /**< switch the output on                  */
    bool off;          /**< switch it off                         */
    bool reset;        /**< start the extremes again              */
} supply_cmd_t;

/** One sample, for the readouts and, while the output is on, the plot. */
void supply_screen_push(const supply_state_t *s);

/**
 * Whether the supply's output is on, as the supply reports it.  That bounds
 * a run: switching on clears the plot and starts it, switching off holds it.
 */
void supply_screen_set_output(bool on);

/** What the set points may be; the sliders and steps follow it. */
void supply_screen_set_caps(const supply_caps_t *caps);

/** Whether the numbers come from the panel's model rather than a supply. */
void supply_screen_set_model(bool model);

/** The set points as the screen holds them, snapped to the caps. */
float supply_screen_set_v(void);
float supply_screen_set_i(void);

/**
 * The settings have been loaded: take the limits from them and start the
 * set points at the start values.  Called once after settings_init(); the
 * screen reads nothing from the settings before it.
 */
void supply_screen_settings_loaded(void);

/** The operator's limits as the screen last read them from the settings. */
supply_limits_t supply_screen_limits(void);

/** A hold under way is abandoned and a pending ON dropped: STOP. */
void supply_screen_cancel_on(void);

/**
 * Whether an ON this screen asked for can still be on its way: false once
 * the application has taken every ON it sent and the output is not on, so
 * an ON it dropped -- stale, or lost to a full queue -- stops counting as
 * live.  An ON this screen holds and has not handed over still counts.
 */
void supply_screen_set_on_coming(bool coming);

/** True when anything was asked; clears it. */
bool supply_screen_poll_cmd(supply_cmd_t *out);

void supply_invalidate(void);
const ui_screen_t *supply_screen(void);

#ifdef __cplusplus
}
#endif
