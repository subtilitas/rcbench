/**
 * @file
 * @brief The motor and ESC (electronic speed controller) bench screen.
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
#include <stddef.h>
#include <stdint.h>

#include "bench_state.h"
#include "endpoints_link.h"
#include "gfx.h"
#include "settings.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_textkey.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The views of LIVE. */
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

/** @brief A command of the screen for the application. */
typedef struct {
    motor_cmd_kind_t kind;  /**< the command */
    float            value;  /**< the command's value, a percentage for MOTOR_CMD_THROTTLE */
    /** @brief MOTOR_CMD_ARM: its stamp (ui_value_new_ask()), named back by
     *        motor_screen_arm_refused() when the bench refuses it; 0 on
     *        every other command. */
    uint32_t         ask;
} motor_cmd_t;

/**
 * @brief Called per sample, so the plot's time base is the sample rate.  The
 *  readouts take every sample; the plot takes them only while armed.
 *
 * @param b the bench's sample
 */
void motor_screen_push(const bench_state_t *b);

/**
 * @brief What the bench is doing, which is what bounds a run.
 *
 * Arming clears the plot and starts it; disarming holds it as it stood.  The
 * caller reports the bench's own arm state, not a request made of it: a
 * disarm that has been asked for and not yet answered must not erase the run
 * it is ending.
 *
 * The throttle follows the bench, through ui_value_gate.h.  Outside
 * UI_VALUE_LIVE no control changes the value: the slider, its -1 and +1
 * buttons and motor_screen_knob() post nothing and move nothing, from the
 * first frame to the bench's answer to an ARM, and from a MOTOR_CMD_DISARM
 * being posted or motor_screen_cancel_arm() on an armed bench until this
 * is called with false.  The change to disarmed returns the slider to 0%, drops a
 * throttle command waiting to be polled and ends a drag under way.
 *
 * The hold that posts MOTOR_CMD_ARM does the same.  The change to armed
 * does it for an arm that hold did not ask for; after the hold's ask it
 * leaves the slider, which only motor_screen_set_throttle() can have moved
 * since, as it is.  Reporting the same state again changes nothing.
 *
 * @param armed true when the bench is armed
 */
void motor_screen_set_armed(bool armed);

/** @brief The state of the plot's trace. */
typedef enum {
    MOTOR_PLOT_EMPTY = 0,   /**< no run recorded since the screen reset */
    MOTOR_PLOT_RECORDING,   /**< the bench is armed; the trace advances */
    MOTOR_PLOT_HELD,        /**< the last run, held as it ended         */
} motor_plot_state_t;

/**
 * @brief Gives the state of the plot's trace.
 *
 * @return the state
 */
motor_plot_state_t motor_screen_plot_state(void);

/**
 * @brief Samples in the trace, capped at UI_PLOT_HISTORY.  The plot is narrower
 *  than that, so a longer run is drawn truncated to its newest columns.
 *
 * @return the samples
 */
int motor_screen_plot_samples(void);

/**
 * @brief Whether channel @p channel's peak is drawn as a value -- 0 voltage, 1
 * current, 2 power, 3rpm: a valid reading of it has arrived since the run
 * started or the peaks were reset.  It stays drawn when the live reading
 * goes empty, as when a source stops answering mid-run and the run's peaks
 * it measured stand.
 *
 * @param channel 0 voltage, 1 current, 2 power, 3rpm
 * @return true while that peak is drawn as a value
 */
bool motor_screen_peak_shown(int channel);

/** Abandon a hold that is under way, because the bench has been stopped.
 *  A stop on a bench that was not armed changes nothing about whether it is
 *  armed, and the gesture must end all the same. */
void motor_screen_cancel_arm(void);

/**
 * @brief The bench refused the ARM stamped @p ask (motor_cmd_t.ask), for
 *        the panel's frame to pass on every frame.
 *
 * An ARM this screen still waits on under that stamp ends: the screen reads
 * SAFE, and the TARGET row, the Idle pulse and Full pulse rows and ARM take
 * a press again.  A hold of ARM under way ends with it, so a finger still
 * resting on ARM asks for nothing.  The band's alert, which says why, is
 * the panel's.  Any other stamp changes nothing: an ARM answered, dropped
 * by a DISARM, a STOP or leaving the screen, followed by a newer ARM, or
 * asked on another screen.  Nothing is posted, and no STOP latch moves.
 *
 * @param ask The stamp of the ARM the bench refused last.
 */
void motor_screen_arm_refused(uint32_t ask);
/**
 * @brief The kV the connected ESC reports, or 0 when it reports none.  Preferred
 * over the SET_MOTOR_KV setting when it is non-zero.
 *
 * @param kv the kV; 0 for none
 */
void motor_screen_set_esc_kv(int kv);

/**
 * @brief The GPIOs the throttle drives now: every pin bound as MOTOR PWM or DShot,
 * bit n for GPIO n, from the binding the application last read.  @p known is
 * false while no binding was read; the mask is then taken as empty.  The
 * CONTROL card's TARGET row names them, or says there are none or that the
 * binding is not read.
 *
 * @param gpio_mask the pins, bit n for GPIO n
 * @param known the binding was read
 */
void motor_screen_set_throttle_pins(uint64_t gpio_mask, bool known);

/** The throttle's pins are bound as more than one protocol. */
#define MOTOR_PROTO_MIXED 0xFFu

/**
 * @brief The protocol the throttle's pins are bound as, an index into
 * outbind_protos() (outbind_role_proto()): 0 when none is known,
 * MOTOR_PROTO_MIXED when they are bound as several.  The TARGET row puts its
 * name before the pins.
 *
 * @param proto the protocol, an index into outbind_protos(); 0 for none known
 */
void motor_screen_set_throttle_protocol(uint8_t proto);

/**
 * @brief The TARGET row's value, `protocol pins` ("DSHOT300 GPIO 0 GPIO 1"),
 *        in at most @p cells characters.
 *
 * The protocol's catalogue name for @p proto, MIXED for MOTOR_PROTO_MIXED,
 * the pins alone for a protocol not known, and as many pins as fit, then
 * "+N" (ui_target_text()).  The DShot programming ports the throttle
 * reaches (motor_dshot_ports()) follow the pins as PORT n.  A port alone is
 * `DSHOT300 PROG PORT 2 GPIO 14` where that fits, `DSHOT300 PORT 2 GPIO 14`
 * where that fits (the CONTROL card's 25 cells), and `DSHOT300 PORT 2`
 * where neither does; slots and ports of different rates read MIXED.
 *
 * @param out    takes the text
 * @param size   bytes at @p out
 * @param proto  the throttle's protocol, a catalogue index or
 *               MOTOR_PROTO_MIXED; 0 for none known
 * @param pins   the throttle pins, bit n for GPIO n
 * @param known  whether the binding is read
 * @param cells  the characters the value box holds
 * @return true when the value is a warning (the binding is not read, or
 *         neither a pin nor a port is bound as a motor), which the row
 *         draws in the warning colour
 */
bool motor_throttle_target(char *out, size_t size, uint8_t proto,
                           uint64_t pins, bool known, unsigned cells);

/**
 * @brief The programming ports the bench throttle drives, bit n for port n: FUNC
 *  DSHOT150 to DSHOT600 and a pin the IOmcu holds, as port_view.h last
 *  read them.
 *
 * @return the ports, bit n for port n
 */
uint8_t motor_dshot_ports(void);

/**
 * @brief The TARGET row's value box in 8-pixel cells (ui_target_row_cells()).
 *
 * @return the cells
 */
unsigned motor_target_cells(void);

/**
 * @brief The output strip a full-width session on PROGRAM shows in the strip row,
 * in place of the tabs and the output card, as one line:
 * `TARGET protocol pins   SUPPLY v V  i A   SAFE` ("ARMED" for an armed
 * bench), the target as motor_throttle_target() puts it in 26 cells, the
 * supply as it reads now (supply_screen_reading()): its voltage and current
 * with the output on, OFF, ON without both readings, "--" while it does not
 * answer.  Into @p out of @p size bytes; @p warn (may be NULL) gets whether
 * the target is a warning.  Returns the length written.
 *
 * @param out receives the line
 * @param size its size, bytes
 * @param proto the throttle's protocol
 * @param pins the throttle pins, bit n for GPIO n
 * @param known the binding is read
 * @param armed the bench is armed
 * @param warn receives whether the target is a warning; may be NULL
 * @return the length written
 */
size_t motor_output_strip(char *out, size_t size, uint8_t proto,
                          uint64_t pins, bool known, bool armed, bool *warn);

/**
 * @brief The task shown.  A tile opens LIVE; in PROGRAM's session the home
 *        tag climbs it one level (back()).
 * @return A ui_wb_task_t: LIVE or PROGRAM.
 */
int motor_screen_task(void);

/**
 * @brief Whether PROGRAM's session is shown, full width with the output strip
 *  over it.
 *
 * @return true while it is
 */
bool motor_screen_session_shown(void);

/**
 * @brief LIVE's view shown, a motor_pane_t: PLOT or TABLE.
 *
 * @return PLOT or TABLE
 */
int motor_screen_pane(void);

/**
 * @brief Whether the TARGET row takes no press now: the bench is not SAFE (an ARM
 *  held, asked or in force, or a DISARM not yet answered), or the servo
 *  programmer holds its port.
 *
 * @return true when it takes no press
 */
bool motor_screen_target_locked(void);

/** @brief Whether the SETTINGS list the footer opens shows, over the body.
 *  @return true while it does */
bool motor_screen_settings_shown(void);

/** @brief The setting the SETTINGS list's keypad types.
 *  @return The setting, -1 while the keypad is closed. */
int motor_screen_keypad_setting(void);

/**
 * @brief SETTINGS' keypad, open or closed, for a test to read what it
 *        opened with and where its keys are.
 * @return The keypad; never NULL.
 */
const ui_keypad_t *motor_screen_keypad(void);

/**
 * @brief The SETTINGS list's settings in their order.
 * @param out Takes at most @p max of them; NULL counts them.
 * @param max The room at @p out.
 * @return How many there are.
 */
int motor_settings_rows(setting_id_t *out, int max);
/**
 * @brief The heading a setting sits under.
 * @param row The setting's index among the list's settings.
 * @return A ui_text_id_t; -1 out of range.
 */
int motor_settings_heading(int row);
/**
 * @brief A setting's row in the list, headings counted.
 * @param row The setting's index among the list's settings.
 * @return The list row; -1 out of range.
 */
int motor_settings_list_row(int row);
/**
 * @brief A setting's value at the list's scroll, where a tap opens the
 *        keypad (ui_setlist_row_part()).
 * @param row The setting's index among the list's settings.
 * @return The rect in the body; empty out of range.
 */
gfx_rect_t motor_settings_box(int row);
struct ui_setlist;
/** @brief The SETTINGS list, for a test to read its geometry and state.
 *  @return The list. */
const struct ui_setlist *motor_settings_list(void);

/**
 * @brief What the alert band says for a switch to ARMED refused because the pair
 *  of Idle pulse and Full pulse is not in force at the IOmcu, by
 *  endpoints_block(); NULL for ENDPOINTS_BLOCK_NONE and for a value outside
 *  the enum.
 *
 * @param why why the switch was refused
 * @return the line
 */
const char *motor_endpoints_alert(endpoints_block_t why);

/**
 * @brief Gives the slider's value.
 *
 * @return the value, percent
 */
float motor_screen_throttle(void);
/**
 * @brief The application's hook: sets the slider on an armed or a disarmed bench
 *  and posts nothing.  Not a control, and not held to the armed state.
 *
 * @param pct the value, percent
 */
void motor_screen_set_throttle(float pct);

/**
 * @brief Move the throttle by @p span_fraction of the slider's span, from the
 * rotary knob (knob.h).  Relative: the value changes by how far the knob
 * turned.  Nothing happens for a zero fraction, while a finger is dragging
 * the slider, while the bench is not armed, or on a task other than LIVE,
 * where the throttle is out of sight: a turn made then is not kept for the
 * next arm.  It posts the same throttle command a touch does and
 * never arms.  It posts only into an empty slot or over a throttle: any other
 * pending command (an arm, a disarm, a peak reset) stays and the knob's
 * motion is dropped.
 *
 * @param span_fraction the turn as a fraction of the slider's span
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

/**
 * @brief The Dial's button pressed and let go (dial_ctl.h): the throttle
 *        to 0% on LIVE of an ARMED bench, as the "-" key's command, a drag
 *        of the slider ended.  Nothing on a SAFE bench, off LIVE, under a
 *        pending command other than a throttle, or at 0% already.
 * @return Whether a throttle of 0% was posted.
 */
bool motor_screen_dial_press(void);

/**
 * @brief The Dial's button held 1s: DISARM while the bench is ARMED or an
 *        ARM is on its way (posted, or collected and not yet confirmed),
 *        so the bench never arms after the HOLD; an ESC STICK run ends as
 *        ABORT ends it.  Nothing else on a SAFE bench.  It never arms.
 * @return Whether a DISARM was posted or a run ended.
 */
bool motor_screen_dial_hold(void);

/**
 * @brief Whether the rotary knob and the Dial act on the throttle now:
 *        LIVE, no settings panel and no log name keyboard over it.
 * @return true then.
 */
bool motor_screen_knob_shown(void);

/**
 * @brief The plot's full scales, the values at the top of the VOLT, CURR
 *        and RPM traces, for the Dial's rings.  They autorange with the
 *        run and stand at their floors before one.
 * @param volt Receives the voltage's, V; may be NULL.
 * @param curr Receives the current's, A; may be NULL.
 * @param rpm  Receives the speed's, rpm; may be NULL.
 */
void motor_screen_plot_scales(float *volt, float *curr, float *rpm);

/**
 * @brief True when a command was waiting; clears it.
 *
 * @param out receives the command
 * @return true when one was waiting
 */
bool motor_screen_poll_cmd(motor_cmd_t *out);

/*
 * RENAME LOG, in the strip row on LIVE and PROGRAM's list: the MOTOR log
 * this bench closed since power-on given a label behind its number,
 * MOTOR042.CSV to MOTOR042_kv1200.CSV (fm_logname.h).  A tap, live only
 * while the bench is SAFE with ARM not held, no log file is open, and that
 * log is on the card mounted now; a tap on the dimmed button says why in
 * the alert band.  The keyboard it opens covers the body, ARM with it, and
 * types the label alone, up to LOG_LABEL_MAX bytes; OK on an empty field
 * gives the plain name back.  The bench leaving SAFE, the home tag and a
 * way off the screen close it.
 *
 * The screen makes no card call: the panel hands the log in each frame
 * and takes the label OK asked for (motor_screen_log_take()); the logger
 * renames and the answer comes back through motor_screen_log_done().
 */

/**
 * @brief The MOTOR log this bench closed since power-on, as the logger
 *        records it.
 * @param name     Its name now; "" or NULL for none since power-on.
 * @param on_card  The card it was written on is the one mounted now.
 * @param logging  The logger has a file open: a run, a test's CSV or a
 *                 report.
 */
void motor_screen_set_last_log(const char *name, bool on_card, bool logging);

/**
 * @brief The label OK asked for, handed over once.  Until
 *        motor_screen_log_done() the button says the card is busy.
 * @param label Takes the label; "" for the plain name.
 * @param n     Bytes at @p label: LOG_LABELLED_SIZE or more.
 * @return True when a request was waiting.
 */
bool motor_screen_log_take(char *label, size_t n);

/**
 * @brief The answer to the request motor_screen_log_take() handed over, as
 *        the band's notice: the new name, or why not.
 * @param err  FM_OK, or the refusal or failure (fm_err_t).
 * @param name The new name the rename asked for; "" when none was built.
 */
void motor_screen_log_done(int err, const char *name);

/**
 * @brief Whether RENAME LOG's keyboard is open over the body.
 * @return True while it is.
 */
bool motor_screen_logname_shown(void);

/**
 * @brief Whether RENAME LOG is live: a tap opens the keyboard.
 * @return False while it is dimmed.
 */
bool motor_screen_logname_live(void);

/**
 * @brief RENAME LOG's keyboard, open or closed, for a test to read its
 *        title and text and where its keys are.
 * @return The keyboard; never NULL.
 */
const ui_textkey_t *motor_screen_logname_keyboard(void);

/** @brief Marks the whole screen to be drawn again. */
void motor_invalidate(void);
/**
 * @brief Gives the screen's table.
 *
 * @return the table
 */
const ui_screen_t *motor_screen(void);

#ifdef __cplusplus
}
#endif
