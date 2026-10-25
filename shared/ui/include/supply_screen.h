/**
 * @file supply_screen.h
 * @brief The SUPPLY screen: a programmable supply set, switched and
 *        watched.
 *
 * MOTOR & ESC's layout with a supply's controls: the plot of voltage, current
 * and power on the left, the readouts on the right, the two set points below
 * the plot and the output switch beside them.  The output comes on with a
 * hold, as ARM does, and goes off with a tap.
 *
 * A workbench (ui_workbench.h): the strip at the top shows LIVE, the plot
 * and the set points, and SETTINGS, the operator's limits in the same
 * column -- caps on the set points, the set points after a restart, the
 * trips and the questions -- kept in the settings model.  The menu's tile
 * opens LIVE; a tab switch is no navigation level.
 *
 * A set point is shown beside its reading everywhere: in brackets on the
 * rail's cards and dashed in the plot.  A tap on a card or on a set point's
 * value opens a keypad over the left column.  The right column stays live
 * on both tasks and under the keypad and the question, so OUTPUT OFF is
 * always a tap away.
 *
 * Owns no hardware and performs no I/O (input/output): it is handed a
 * supply_state_t and touch events, and commands are read back out.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "supply.h"
#include "ui_keypad.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The screen whose ON the output is.  Leaving that screen switches the
 * output off (supply_screen_left()); no other screen names a live output,
 * so none is left on behind one that is not shown.
 */
typedef enum {
    SUPPLY_BY_NONE = 0,     /* off, or on and asked by nobody here */
    SUPPLY_BY_SUPPLY,       /* OUTPUT ON on SUPPLY                  */
    SUPPLY_BY_SERVO,        /* SERVO: OUTPUT ON, a test, the programmer */
    SUPPLY_BY_STICK,        /* a stick run on ESC STICK             */
} supply_by_t;

/** @brief LIVE's views of the readings: the plot or the table. */
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

/**
 * The task shown, a ui_wb_task_t: LIVE or SETTINGS.  A tile opens LIVE; a
 * tap on the other tab switches, and takes an open keypad with it.
 */
int supply_screen_task(void);

/** One sample, for the readouts and, while the output is on, the plot. */
void supply_screen_push(const supply_state_t *s);

/** The newest sample supply_screen_push() took, as it took it: zeroed, so
 *  not online, before the first. */
const supply_state_t *supply_screen_reading(void);

/**
 * Whether the supply's output is on, as the supply reports it.  That bounds
 * a run: switching on clears the plot and starts it, switching off holds it.
 */
void supply_screen_set_output(bool on);

/** What the set points may be; the sliders and steps follow it. */
void supply_screen_set_caps(const supply_caps_t *caps);

/** Whether the numbers come from the panel's model rather than a supply. */
void supply_screen_set_model(bool model);
/**
 * @brief Whether the numbers come from the panel's model.
 * @return True for the model, false for a supply.
 */
bool supply_screen_model(void);

/**
 * The PD mini's UART (universal asynchronous receiver-transmitter) rate in
 * baud, shown after ONLINE in the header ("ONLINE 38400"); 0 shows none.
 * Not shown for the model, nor while the module does not answer.
 */
void supply_screen_set_baud(uint32_t baud);

/**
 * @brief The PD mini's UART pins, IOmcu GPIO numbers, named in the header
 *        ("PD MINI TX GPIO 12 RX GPIO 13").
 *
 * With either pin not set, or both on one GPIO, the header says "PD MINI
 * NO PINS": the page holds no pins then.  Not shown for the model.
 *
 * @param tx  the TX pin; negative is not set
 * @param rx  the RX pin; negative is not set
 */
void supply_screen_set_pins(int tx, int rx);

/**
 * @brief The left text of the header strip: the supply and its pins, the
 *        answer and the rate ("PD MINI TX GPIO 12 RX GPIO 13   ONLINE
 *        38400"), 47 characters at most.
 * @param out   takes the text
 * @param size  bytes at @p out
 */
void supply_screen_header_text(char *out, size_t size);

/** The set points as the screen holds them, snapped to the caps. */
float supply_screen_set_v(void);
/**
 * @brief The current limit's set point as the screen holds it, snapped to
 *        the caps.
 * @return The set point, A.
 */
float supply_screen_set_i(void);

/**
 * The settings have been loaded: take the limits from them and start the
 * set points at the start values.  Called once after settings_init(); the
 * screen reads nothing from the settings before it.
 */
void supply_screen_settings_loaded(void);

/** The operator's limits as the screen last read them from the settings. */
supply_limits_t supply_screen_limits(void);

/** The caps were changed in the settings from elsewhere, so read them
 *  again. */
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
/**
 * Whether a servo programming port is out of PWM.  The IOmcu then
 * takes an OFF from the panel and no other command (supply_page.h), and
 * this screen offers the same: OUTPUT OFF and RESET PEAKS work, OUTPUT ON,
 * the sliders, the steps and the set points' keypads take no press and are
 * drawn dimmed, supply_screen_put() and supply_screen_ask_on() do nothing,
 * and the MODE card says who holds the supply.  What was under way when
 * the port left PWM -- a hold on OUTPUT ON, a drag, a keypad of a set
 * point, a question -- ends unanswered.
 */
void supply_screen_set_port_held(bool held);
/**
 * @brief Whether a programming port holds the supply
 *        (supply_screen_set_port_held()).
 * @return True while it holds it.
 */
bool supply_screen_port_held(void);

/** The set points a programming port gave the supply, onto the screen:
 *  supply_screen_put() for the one caller that reports and does not ask. */
void supply_screen_adopt(float v, float i);

/**
 * @brief Whether the output is on as this screen believes it: what the
 *        supply reported, with an OFF this screen asked for and has not
 *        yet seen answered counted as off.
 * @return True for on.
 */
bool supply_screen_output_on(void);

/** The output on, or an ON asked for and still on its way: what a set
 *  point change is asked about. */
bool supply_screen_output_live(void);

/** How many times the output has been reported going off.  A question
 *  about a live output records it and stands only while it is unchanged:
 *  an OFF and a new ON between two frames end the run it was about. */
uint32_t supply_screen_off_count(void);

/** OUTPUT ON's hold completed elsewhere, as it does here: an ON is posted
 *  unless an OFF already is, and the output is @p by's from then on. */
void supply_screen_ask_on(supply_by_t by);

/** The screen whose ON the output is, or SUPPLY_BY_NONE: off, no ON on
 *  its way, or an OFF asked since. */
supply_by_t supply_screen_owner(void);

/**
 * An output reported on that nobody here asked for becomes @p by's: the
 * servo programmer's CONNECT, which switches a PD mini on through the
 * IOmcu and not through this screen.  An owned output keeps its
 * owner.
 */
void supply_screen_claim(supply_by_t by);

/**
 * Screen @p by is left.  An output it owns -- on, or an ON on its way --
 * is switched off.  While a servo programming port holds the supply
 * (supply_screen_set_port_held()) an OFF would end the port's session and
 * cut a write on the wire: the OFF is owed instead, and sent when the port
 * lets go, unless supply_screen_entered() came first.  An output another
 * screen owns, or none, stays as it is.
 */
void supply_screen_left(supply_by_t by);

/** Screen @p by is shown again: an OFF owed for leaving it is dropped. */
void supply_screen_entered(supply_by_t by);

/** Whether an OFF is owed for a screen left while a port held the supply. */
bool supply_screen_off_owed(void);

/** OUTPUT OFF tapped elsewhere: an OFF is posted, and any ON dropped. */
void supply_screen_ask_off(void);

/** Whether OUTPUT ON is being held on this screen: a press on the switch
 *  that began with the output off. */
bool supply_screen_output_held(void);

/** Whether an OFF is posted and not yet taken by supply_screen_poll_cmd(). */
bool supply_screen_off_pending(void);

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

/** @brief Everything SUPPLY draws is drawn again on its next frame. */
void supply_invalidate(void);
/**
 * @brief The SUPPLY screen as the router drives it.
 * @return The screen; never NULL.
 */
const ui_screen_t *supply_screen(void);

/**
 * @brief The keypad of the set points and of SETTINGS' rows, open or
 *        closed, for a test to read what it opened with and where its
 *        keys are.
 * @return The keypad; never NULL.
 */
const ui_keypad_t *supply_screen_keypad(void);

#ifdef __cplusplus
}
#endif
