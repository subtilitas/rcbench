/**
 * @file
 * @brief The programmer for servos by Hitec as the SERVO screen runs it:
 *        the session with one servo, driven step by step through the
 *        panel's half of the HITEC link page, read only.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * The session runs on the panel (protocols/hitec/include/hitec_session.h):
 * this model holds it, hands each of its steps to the task that owns the
 * link module (hitec_link.h) as a command, and feeds each outcome back.
 * The IOmcu's page runs one raw transaction at a time and holds no
 * session (shared/outputs/hitec_port.h).
 *
 * What it does:
 *
 *   - CONNECT takes the SERVO screen's programming port from PWM (OPEN),
 *     enters programming mode for the series the operator chose (classic
 *     HS-5xxx and HS-7xxx, or D series) and reads the series' full set.
 *     It is refused, and nothing is sent, on an ARMED bench or one asked to
 *     arm, with STOP latched, with the tester on the standard outputs, with
 *     no pin on the port, on an IOmcu without the page, for a D series line
 *     whose receive mode is not held yet, for a classic servo with no PD
 *     mini answering (classic serial mode starts only at power-up), and
 *     with a programming voltage or current above the SUPPLY limits.
 *   - READ ALL reads the full set again: 21 D series registers, or 100
 *     classic cells.  The model is identified from what was read.
 *   - LEAVE ends programming mode (a D series reboot, a classic supply
 *     cycle) and gives the port back to PWM (CLOSE), also after a STOP.
 *   - CLEAR clears the page's STOPPED flag and goes on: the entry again
 *     where the supply went off, or the read.  It is refused while STOP
 *     is latched at the panel: the heartbeat is withheld until a switch to
 *     ARMED, and the IOmcu would refuse the CLEAR as UNSAFE.
 *   - ABORT ends the running operation; the port stays held.
 *   - RELEASE PORT closes a port the page holds and this panel did not
 *     open: one the bench console opened, or one held over a panel restart
 *     or a link loss.  The supply stays as it is.
 *
 * Nothing is written to the servo: no write switch, no journal, no backup.
 *
 * The programming voltage is the SERVO programmer's setting; the page takes
 * LINK_HITEC_SUPPLY_MIN_MV to LINK_HITEC_SUPPLY_MAX_MV for a POWER, so a
 * higher one is taken as LINK_HITEC_SUPPLY_MAX_MV (hitec_prog_mv()).
 *
 * Every refusal, outcome and session result becomes one message
 * (hitec_prog_msg_id_t) the screen has a text for.
 *
 * Pure C.  The caller hands in hitec_prog_feed_t every frame and carries out
 * the commands hitec_prog_take() gives.  One task only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "hitec_link.h"
#include "hitec_reg.h"
#include "hitec_session.h"
#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The programming voltage and current until set otherwise. */
#define HITEC_PROG_SUPPLY_MV 5000u
/** @brief The programming current limit until set otherwise, mA. */
#define HITEC_PROG_SUPPLY_MA 1000u
/** @brief The lowest programming voltage a POWER takes, mV. */
#define HITEC_PROG_MV_MIN LINK_HITEC_SUPPLY_MIN_MV
/** @brief The highest, mV: a higher setting is taken as this one. */
#define HITEC_PROG_MV_MAX LINK_HITEC_SUPPLY_MAX_MV
/** @brief The lowest current limit a POWER takes, mA. */
#define HITEC_PROG_MA_MIN 50u
/** @brief The highest, mA: the PD mini's. */
#define HITEC_PROG_MA_MAX 3000u
/** @brief The highest GPIO of the IOmcu, and a pin that is not known. */
#define HITEC_PROG_PIN_LAST 47u
/** @brief A pin that is not known. */
#define HITEC_PROG_NO_PIN   0xFFu
/** @brief A command the link's task has not answered this long is given
 *         up, ms. */
#define HITEC_PROG_START_MS 2000u
/** @brief A request without an outcome this long after its command was
 *         taken is aborted, ms: HITEC_LINK_RUN_MAX_MS and 5000ms. */
#define HITEC_PROG_RUN_MS (HITEC_LINK_RUN_MAX_MS + 5000u)

/** @brief Where the programmer is. */
typedef enum {
    HITEC_PROG_DOWN = 0,   /**< no IOmcu answers                          */
    HITEC_PROG_NO_PAGE,    /**< the IOmcu has no HITEC page               */
    HITEC_PROG_SYNC,       /**< the page has not been read yet            */
    HITEC_PROG_PWM,        /**< the page holds no port: the tester has it */
    HITEC_PROG_OTHER,      /**< the page holds a port this panel did not
                                open                                      */
    HITEC_PROG_OPENING,    /**< OPEN is on its way                        */
    HITEC_PROG_ENTERING,   /**< the entry into programming mode runs      */
    HITEC_PROG_HELD,       /**< the port is this panel's, nothing runs    */
    HITEC_PROG_READING,    /**< a read runs                               */
    HITEC_PROG_CLEARING,   /**< CLEAR is on its way                       */
    HITEC_PROG_LEAVING,    /**< the leave runs, then CLOSE                */
    HITEC_PROG_RELEASING,  /**< CLOSE of another's port is on its way     */
} hitec_prog_state_t;

/** @brief What the screen says. */
typedef enum {
    HITEC_PROG_MSG_NONE = 0,
    HITEC_PROG_MSG_READ,          /**< read; arg the D series model number,
                                       reg the D series ID or the classic
                                       version                             */
    HITEC_PROG_MSG_DOWN,          /**< no IOmcu answers                    */
    HITEC_PROG_MSG_NO_PAGE,       /**< the IOmcu has no HITEC page         */
    HITEC_PROG_MSG_BUSY,          /**< another operation runs              */
    HITEC_PROG_MSG_NEED_DISARM,   /**< ARMED or asked to arm               */
    HITEC_PROG_MSG_STOP_LATCHED,  /**< STOP is latched at the panel        */
    HITEC_PROG_MSG_NEED_TARGET,   /**< the tester drives the standard
                                       outputs                             */
    HITEC_PROG_MSG_NO_PIN,        /**< the port has no pin                 */
    HITEC_PROG_MSG_RX_PENDING,    /**< the D series receive mode is not
                                       held by the IOmcu yet               */
    HITEC_PROG_MSG_NEED_PD_C,     /**< classic, and no PD mini answers     */
    HITEC_PROG_MSG_SUPPLY_CAPS,   /**< the programming voltage or current
                                       is above the SUPPLY limits          */
    HITEC_PROG_MSG_HELD_KST,      /**< the KST page holds the port         */
    HITEC_PROG_MSG_HELD_OTHER,    /**< the page holds a port this panel did
                                       not open; arg its port              */
    HITEC_PROG_MSG_NO_C,          /**< no classic servo answered           */
    HITEC_PROG_MSG_NO_D,          /**< no D series servo answered          */
    HITEC_PROG_MSG_ENTRY_BAD,     /**< bytes came that do not fit the
                                       series' entry                       */
    HITEC_PROG_MSG_READ_FAIL,     /**< a read failed on every try; arg the
                                       address                             */
    HITEC_PROG_MSG_NOT_IN_MODE,   /**< no servo is in programming mode     */
    HITEC_PROG_MSG_STOPPED,       /**< a stop cut the operation            */
    HITEC_PROG_MSG_ABORTED,       /**< ABORT ended it                      */
    HITEC_PROG_MSG_CLEARED,       /**< CLEAR was taken                     */
    HITEC_PROG_MSG_UNSAFE,        /**< refused: the IOmcu trusts neither
                                       heartbeat nor link                  */
    HITEC_PROG_MSG_LEFT,          /**< the port is back in PWM             */
    HITEC_PROG_MSG_RELEASED,      /**< another's port closed; arg its port */
    HITEC_PROG_MSG_SESSION_ENDED, /**< the port went back to PWM with no
                                       command of this model               */
    HITEC_PROG_MSG_TIMEOUT,       /**< no answer or no outcome in time     */
    HITEC_PROG_MSG_NO_ANSWER,     /**< the link did not answer             */
    HITEC_PROG_MSG_RESTARTED,     /**< the IOmcu started again, or another
                                       writer sent a command               */
    HITEC_PROG_MSG_NACK,          /**< a frame refused; arg the reason     */
    HITEC_PROG_MSG_REFUSED,       /**< refused for a reason with no message
                                       of its own; arg link_hitec_refusal_t */
    HITEC_PROG_MSG_NO_LINE,       /**< OPEN: the IOmcu found no free line  */
    HITEC_PROG_MSG_SUPPLY,        /**< POWER: the supply did not switch    */
    HITEC_PROG_MSG_UNKNOWN,       /**< a code the tables do not hold; arg
                                       the code                            */
    HITEC_PROG_MSG_COUNT
} hitec_prog_msg_id_t;

/** @brief The level of a message. */
typedef enum {
    HITEC_PROG_INFO = 0,
    HITEC_PROG_WARN,
    HITEC_PROG_DANGER,
} hitec_prog_level_t;

/** @brief The last message, its level, its arguments and its number. */
typedef struct {
    uint8_t  id;     /**< hitec_prog_msg_id_t      */
    uint8_t  level;  /**< hitec_prog_level_t       */
    uint16_t reg;    /**< a second argument        */
    uint16_t arg;    /**< the argument, as id says */
    uint32_t seq;    /**< counts the messages      */
} hitec_prog_msg_t;

/** @brief The kinds of command for the task that owns the link module. */
typedef enum {
    HITEC_PROG_CMD_OPEN = 0,  /**< hitec_link_open()                     */
    HITEC_PROG_CMD_STEP,      /**< hitec_link_step()                     */
    HITEC_PROG_CMD_CLEAR,     /**< hitec_link_clear()                    */
    HITEC_PROG_CMD_CLOSE,     /**< hitec_link_close()                    */
    HITEC_PROG_CMD_ABORT,     /**< hitec_link_abort(); answered by none   */
} hitec_prog_cmd_kind_t;

/** @brief One call for the task that owns the link module. */
typedef struct {
    uint8_t  kind;            /**< hitec_prog_cmd_kind_t                 */
    uint8_t  port;            /**< the programming port                  */
    uint8_t  family;          /**< OPEN: hitec_family_t                  */
    uint8_t  drive;           /**< OPEN: hitec_drive_t                   */
    uint16_t mv;              /**< STEP POWER: the supply voltage, mV    */
    uint16_t ma;              /**< STEP POWER: its current limit, mA     */
    uint32_t id;              /**< comes back in the feed's answer_id and
                                   in the outcome's id; 0 for an ABORT   */
    hitec_ses_step_t step;    /**< STEP: the session's step              */
} hitec_prog_cmd_t;

/**
 * @brief The pass a command needs to be carried out, as kst_prog.h's gate.
 *
 * hitec_prog_take() offers the pass of each command it hands out; the
 * link's task claims it when the command comes out of its queue and carries
 * the command out only with the pass; the model revokes the pass of one
 * nobody answered within HITEC_PROG_START_MS.  Each pass is taken once,
 * by the one or by the other: a command whose pass the model revoked is
 * never carried out.  Each call is one atomic exchange of a 32-bit word.
 */
typedef struct {
    _Atomic uint32_t pass;    /**< the id of the command offered; 0 none */
} hitec_prog_gate_t;

/**
 * @brief Sets up a gate: no pass offered.
 *
 * @param g The gate.
 */
void hitec_prog_gate_init(hitec_prog_gate_t *g);
/**
 * @brief The pass of command @p id is there to be taken.
 *
 * @param g  The gate.
 * @param id The command's id.
 */
void hitec_prog_gate_offer(hitec_prog_gate_t *g, uint32_t id);
/**
 * @brief The link's task, with command @p id out of its queue.
 *
 * @param g  The gate.
 * @param id The command's id.
 * @return true when the command is to be carried out; false: it is
 *         dropped, with no answer.
 */
bool hitec_prog_gate_claim(hitec_prog_gate_t *g, uint32_t id);
/**
 * @brief The model gives command @p id up.
 *
 * @param g  The gate.
 * @param id The command's id.
 * @return true when the command is not carried out and never is.
 */
bool hitec_prog_gate_revoke(hitec_prog_gate_t *g, uint32_t id);

/** @brief What the task that owns the link module publishes, every frame. */
typedef struct {
    uint32_t now_ms;          /**< the link task's clock, ms              */
    hitec_link_readout_t rd;  /**< the page as the module holds it        */
    uint32_t answer_id;       /**< the last command carried out; 0 none   */
    uint8_t  answer;          /**< what the module said to it:
                                   hitec_link_start_t                     */
    uint32_t outcomes;        /**< counts the outcomes handed out         */
    hitec_link_outcome_t outcome;  /**< the last one                      */
    bool     stopped;         /**< STOP is latched at the panel           */
    bool     armed;           /**< the panel's outputs are armed          */
    uint8_t  target;          /**< what the SERVO screen's tester drives as
                                   the IOmcu reads it: 0 the standard
                                   servo ports, 1 + n programming port n  */
    uint8_t  port_pin[LINK_PG_PORTS_MAX]; /**< the GPIO port n drives, when
                                              bit n of port_pin_known is
                                              set                         */
    uint8_t  port_pin_known;  /**< bit n set: port_pin[n] holds a GPIO     */
    uint8_t  rx_mark;         /**< hitec_rx_link_mark_t of the D series
                                   receive mode                           */
    bool     supply_read;     /**< out_mv and out_ma are a reading of the
                                   PD mini, on                            */
    uint16_t out_mv;          /**< its output, mV                         */
    uint16_t out_ma;          /**< the current it gives, mA               */
    bool     supply_held;     /**< the SUPPLY page says a programming port
                                   holds it (LINK_SP_PORT)                */
} hitec_prog_feed_t;

/** @brief The programmer's state. */
typedef struct {
    hitec_prog_feed_t f;      /**< this frame's feed                      */
    bool     fed;             /**< a feed came                            */
    uint32_t outcomes_seen;   /**< the feed's outcome count taken         */
    uint32_t next_id;         /**< the last command id given              */

    hitec_session_t ses;      /**< the session with the servo             */
    uint8_t  series;          /**< hitec_family_t the operator chose      */
    uint8_t  port;            /**< the port the session works on          */
    uint8_t  st;              /**< the request phase, private             */
    uint8_t  op;              /**< the session operation, private         */
    bool     owned;           /**< this model opened the held port        */
    bool     lost;            /**< a lost link took the session: the page's
                                   port is said when it comes back        */
    bool     armed;           /**< hitec_prog_bench_armed()               */
    bool     reenter;         /**< after CLEAR: the entry, not the read   */
    uint16_t supply_mv;       /**< the programming voltage asked, mV      */
    uint16_t supply_ma;       /**< the current limit asked, mA            */
    uint16_t caps_mv;         /**< the SUPPLY limit, mV; 0 none           */
    uint16_t caps_ma;         /**< the SUPPLY limit, mA; 0 none           */
    uint16_t mv_open;         /**< the voltage of the session's POWERs    */
    uint16_t ma_open;         /**< the current of the session's POWERs    */

    bool     have_servo;      /**< a full read ended well                 */
    hitec_image_t servo;      /**< what that read found                   */
    hitec_model_t model;      /**< its model                              */

    uint8_t  wait;            /**< what is awaited, private               */
    uint32_t wait_id;         /**< the command awaited                    */
    uint32_t wait_ms;         /**< when the wait began                    */
    bool     cmd_ready;       /**< cmd is not taken yet                   */
    hitec_prog_cmd_t cmd;     /**< the command for the link's task        */
    bool     abort_ready;     /**< an ABORT is owed to the link's task    */
    hitec_link_outcome_t last; /**< the outcome of the last step          */
    bool     have_last;       /**< last holds one                         */
    hitec_prog_gate_t *gate;  /**< hitec_prog_gate_set()                  */
    hitec_prog_msg_t msg;     /**< the last message                       */
} hitec_prog_t;

/**
 * @brief Sets up the programmer: nothing fed, D series, the default supply.
 *
 * @param p The programmer.
 */
void hitec_prog_init(hitec_prog_t *p);

/**
 * @brief The gate the link's task claims each command at; NULL for none.
 *
 * With none, a command nobody answers within HITEC_PROG_START_MS counts as
 * one that may have run: an ABORT follows it.
 *
 * @param p The programmer.
 * @param g The gate.
 */
void hitec_prog_gate_set(hitec_prog_t *p, hitec_prog_gate_t *g);

/**
 * @brief The feed of this frame: answers and outcomes are taken, the
 *        session's next step goes out, and the state follows the page.
 *
 * @param p The programmer.
 * @param f The feed.
 */
void hitec_prog_feed(hitec_prog_t *p, const hitec_prog_feed_t *f);

/**
 * @brief The next command for the link's task.
 *
 * @param p   The programmer.
 * @param out Receives it.
 * @return true when one was taken.
 */
bool hitec_prog_take(hitec_prog_t *p, hitec_prog_cmd_t *out);

/**
 * @brief Where the programmer is.
 *
 * @param p The programmer.
 * @return The state.
 */
hitec_prog_state_t hitec_prog_state(const hitec_prog_t *p);

/**
 * @brief The bench is armed or asked to arm.
 *
 * @param p     The programmer.
 * @param armed true while it is.
 */
void hitec_prog_bench_armed(hitec_prog_t *p, bool armed);

/**
 * @brief The series of the next CONNECT; taken in PWM only.
 *
 * @param p      The programmer.
 * @param series hitec_family_t.
 * @return true when taken.
 */
bool hitec_prog_series_set(hitec_prog_t *p, uint8_t series);

/**
 * @brief The series: the session's while the port is held, else the one the
 *        next CONNECT uses.
 *
 * @param p The programmer.
 * @return hitec_family_t.
 */
uint8_t hitec_prog_series(const hitec_prog_t *p);

/**
 * @brief The programming voltage and current limit the next CONNECT asks.
 *
 * @param p  The programmer.
 * @param mv The voltage, mV.
 * @param ma The current limit, mA.
 */
void hitec_prog_supply_set(hitec_prog_t *p, uint16_t mv, uint16_t ma);

/**
 * @brief The SUPPLY screen's limits, 0 for none.
 *
 * @param p      The programmer.
 * @param max_mv The voltage limit, mV.
 * @param max_ma The current limit, mA.
 */
void hitec_prog_supply_caps(hitec_prog_t *p, uint16_t max_mv,
                            uint16_t max_ma);

/**
 * @brief The voltage a POWER of the next CONNECT asks: the setting, at most
 *        HITEC_PROG_MV_MAX and at least HITEC_PROG_MV_MIN.
 *
 * @param p The programmer.
 * @return mV.
 */
uint16_t hitec_prog_mv(const hitec_prog_t *p);

/**
 * @brief The current limit a POWER of the next CONNECT asks, HITEC_PROG_MA_MIN
 *        to HITEC_PROG_MA_MAX.
 *
 * @param p The programmer.
 * @return mA.
 */
uint16_t hitec_prog_ma(const hitec_prog_t *p);

/**
 * @brief Whether the setting is above HITEC_PROG_MV_MAX and is taken as it.
 *
 * @param p The programmer.
 * @return true when capped.
 */
bool hitec_prog_mv_capped(const hitec_prog_t *p);

/**
 * @brief CONNECT on the SERVO screen's programming port.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_connect(hitec_prog_t *p);

/**
 * @brief READ ALL.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_read(hitec_prog_t *p);

/**
 * @brief LEAVE: programming mode ends and the port goes back to PWM.
 *
 * Taken while the port is this panel's and nothing runs, STOPPED or not.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_leave(hitec_prog_t *p);

/**
 * @brief CLEAR the page's STOPPED flag, then the entry or the read.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_clear(hitec_prog_t *p);

/**
 * @brief ABORT the running operation; the port stays held.
 *
 * @param p The programmer.
 */
void hitec_prog_abort(hitec_prog_t *p);

/**
 * @brief RELEASE PORT: CLOSE the port another session holds.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_release(hitec_prog_t *p);

/**
 * @brief The last message.
 *
 * @param p The programmer.
 * @return The message.
 */
const hitec_prog_msg_t *hitec_prog_message(const hitec_prog_t *p);

/**
 * @brief The level of message @p id.
 *
 * @param id hitec_prog_msg_id_t.
 * @return hitec_prog_level_t.
 */
hitec_prog_level_t hitec_prog_level(unsigned id);

/**
 * @brief The programming port the overlay names: the session's while one
 *        holds it, the held port of another session, else the tester's;
 *        0 while the tester drives the standard outputs.
 *
 * @param p The programmer.
 * @return The port, from 0.
 */
uint8_t hitec_prog_port(const hitec_prog_t *p);

/**
 * @brief The GPIO that port drives: the HITEC page's while it holds the
 *        line, else the PROG page's.
 *
 * @param p The programmer.
 * @return The pin, HITEC_PROG_NO_PIN when not known.
 */
uint8_t hitec_prog_pin(const hitec_prog_t *p);

/**
 * @brief Whether the line receives by PRECHARGE: the open line's mode while
 *        the page holds one, else the mode its next OPEN takes.
 *
 * @param p The programmer.
 * @return true for PRECHARGE.
 */
bool hitec_prog_precharge(const hitec_prog_t *p);

/**
 * @brief Whether the SERVO screen's tester drives a programming port.
 *
 * @param p The programmer.
 * @return true when it does.
 */
bool hitec_prog_on_port(const hitec_prog_t *p);

/**
 * @brief Whether the session or another one holds a port, or a request of
 *        this model is on its way: no other programmer opens and the
 *        bench takes no arm.
 *
 * @param p The programmer.
 * @return true while held.
 */
bool hitec_prog_holds_port(const hitec_prog_t *p);

/**
 * @brief The link task's view: the page holds a port, or a request runs.
 *
 * @param rd The module's readout.
 * @return true while the bench takes no arm and no supply change.
 */
bool hitec_prog_port_taken(const hitec_link_readout_t *rd);

/**
 * @brief Whether the servo is in programming mode.
 *
 * @param p The programmer.
 * @return true after an entry, until a leave.
 */
bool hitec_prog_in_mode(const hitec_prog_t *p);

/**
 * @brief Whether the page's STOPPED flag is set on this panel's port.
 *
 * @param p The programmer.
 * @return true while stopped.
 */
bool hitec_prog_stopped(const hitec_prog_t *p);

/**
 * @brief Whether a PD mini answers the page (LINK_HT_F_RAIL).
 *
 * @param p The programmer.
 * @return true when one does.
 */
bool hitec_prog_rail(const hitec_prog_t *p);

/**
 * @brief The servo as the last full read found it; NULL with none.
 *
 * @param p The programmer.
 * @return The image.
 */
const hitec_image_t *hitec_prog_servo(const hitec_prog_t *p);

/**
 * @brief The model of that servo; NULL with none.
 *
 * @param p The programmer.
 * @return The model.
 */
const hitec_model_t *hitec_prog_model(const hitec_prog_t *p);

/**
 * @brief The layout the values are decoded with: the model's, or the
 *        series' measured one before a read.
 *
 * @param p The programmer.
 * @return hitec_layout_t.
 */
uint8_t hitec_prog_layout(const hitec_prog_t *p);

/**
 * @brief The progress of the running read.
 *
 * @param p     The programmer.
 * @param done  Receives the addresses read.
 * @param total Receives the addresses it reads.
 * @return true while a read runs.
 */
bool hitec_prog_progress(const hitec_prog_t *p, unsigned *done,
                         unsigned *total);

/**
 * @brief The decoder's code of the last failed try of the running
 *        transaction, while it goes again.
 *
 * @param p The programmer.
 * @return hitec_rx_t, 0 while no try failed.
 */
unsigned hitec_prog_retry_code(const hitec_prog_t *p);

#ifdef __cplusplus
}
#endif
