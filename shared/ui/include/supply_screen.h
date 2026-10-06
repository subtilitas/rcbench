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
    bool module_reset; /**< restart the PD mini                   */
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
bool supply_screen_model(void);

/**
 * The PD mini's UART (universal asynchronous receiver-transmitter) rate in
 * baud, shown after ONLINE in the header ("ONLINE 38400"); 0 shows none.
 * Not shown for the model, nor while the module does not answer.
 */
void supply_screen_set_baud(uint32_t baud);

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

/** The caps were changed in the settings from elsewhere -- the SERVO
 *  screen's LIMITS page -- so read them again. */
void supply_screen_limits_changed(void);

/*
 * For another screen that sets the supply: SERVO, beside the servo the
 * supply feeds.  The set points and the output stay this screen's, one of
 * each; what SERVO types or switches lands here as if it were done here.
 */

/** The set points' range: the supply's caps narrowed by the operator's. */
supply_caps_t supply_screen_caps(void);

/**
 * Whether a set point typed now waits for a question before it reaches the
 * supply: the output live or an ON on its way, and SETTINGS, CONFIRM WHILE
 * ON, KEYPAD on.
 */
bool supply_screen_typed_asks(void);

/** Both set points, typed elsewhere and confirmed where they needed it;
 *  snapped to the caps. */
void supply_screen_put(float v, float i);

/** The output as the supply last reported it. */
bool supply_screen_output_on(void);

/** The output on, or an ON asked for and still on its way: what a set
 *  point change is asked about. */
bool supply_screen_output_live(void);

/** How many times the output has been reported going off.  A question
 *  about a live output records it and stands only while it is unchanged:
 *  an OFF and a new ON between two frames end the run it was about. */
uint32_t supply_screen_off_count(void);

/** OUTPUT ON's hold completed elsewhere, as it does here: an ON is posted
 *  unless an OFF already is. */
void supply_screen_ask_on(void);

/** OUTPUT OFF tapped elsewhere: an OFF is posted, and any ON dropped. */
void supply_screen_ask_off(void);

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
