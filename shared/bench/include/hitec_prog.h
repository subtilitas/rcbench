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
 * Writing (hitec_prog_write.c):
 *
 *   - The values are edited in a draft (hitec_prog_edit()) through the
 *     parameter table of hitec_reg.h: a value no step of its encoding gives
 *     is refused, a value that breaks a soft rule (hitec_prog_rule_t) waits
 *     for a confirmation.  A rule confirmed for a value is asked again for
 *     a value further from the servo's, or on its other side.  A write, a
 *     read and DISCARD EDITS end every confirmation.
 *   - The switch ENABLE WRITE TO SERVO is off at every CONNECT and LEAVE,
 *     when the link goes, at a stop, after a write that did not end
 *     verified, and when a read finds other values than the ones held.
 *     The caller switches it off at every entry of the screen.
 *   - WRITE n VALUES runs hitec_session_write(): D series writes 20ms
 *     apart, the save, 100ms, the reboot, 1000ms, the read-back; classic
 *     per cell the value, then the balance 0x1C, then the read-back of
 *     cells 0x00 to 0x23.  It is refused on an ARMED bench, with STOP
 *     latched, on a STOPPED port, with no servo read, with the switch off
 *     and while a record of an earlier write stands.
 *   - REPAIR writes the classic balance 0x1C alone when the cells 0x00 to
 *     0x1C of the servo read do not sum to 0.
 *   - The record (hitec_card.h): before the first step of a write the
 *     servo's values, the target and the port are recorded, with a card
 *     in its file as well, and the write waits for the card.  A write that
 *     ends verified clears it.  A record that stands is compared with the
 *     next full read (hitec_prog_rec_t); RESTORE writes the values before
 *     the write back, and FORGET discards the record.
 *   - A write that fails once a step went out leaves what the servo holds
 *     unknown: READ ALL comes before the next write.
 *
 * Files and backups (hitec_prog_file.c):
 *
 *   - The session's backup: the first full read after CONNECT, kept in RAM
 *     until LEAVE, the next CONNECT or a lost session.  A read whose model,
 *     name, date or serial differs from it is another servo: that read
 *     becomes the session's backup.
 *   - The backup file: before the first write of a session that sends a
 *     step (WRITE n VALUES, REPAIR, a RESTORE, FACTORY DEFAULT) the session's
 *     backup is saved to a new file, HITEC001 to HITEC999 (hitec_card.h),
 *     and the write waits for the card's answer; a card that refuses it ends
 *     the write with nothing sent.  With no card the write goes and the
 *     backup stays in RAM only.  A save by hand before the first write,
 *     while the servo read equals the backup, counts as that file.
 *   - FILES lists the servo files of the card, 5 a page (hitec_prog_files()),
 *     saves the servo read under a name (hitec_prog_save()) and loads a file
 *     into the draft (hitec_prog_load()): the cells or registers of the
 *     editable parameters, raw, the classic balance computed.  A file of the
 *     other series or layout is refused; a file of another model, or one
 *     whose values break a soft rule, waits for hitec_prog_load_confirm().
 *     The values a file holds outside the editable parameters are counted
 *     and not loaded.  Nothing reaches the servo before WRITE n VALUES.
 *   - RESTORE of the session's backup writes every address a plan writes
 *     (hitec_writable()) back from the backup, raw values included, as a
 *     write with its read-back.
 *   - FACTORY DEFAULT, classic servos only: cells 0x00 to 0x23 from the
 *     servo's own factory cells 0x30 to 0x53 (hitec_c_factory()), as a write.
 *     The D series reset (0x6E = 0x0F0F) is not offered: the values it
 *     leaves are not known before it.
 *
 * Every model is written: the D series with the HFP-30's registers, the
 * classic servos with the HPP-21 Plus's cells (hitec_reg.h).
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
#include <stddef.h>
#include <stdint.h>

#include "hitec_card.h"
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
/** @brief A write waits this long for the card to take its record, ms. */
#define HITEC_PROG_CARD_MS 3000u
/** @brief A file request, the backup file's included, waits this long for
 *         the card's answer, ms. */
#define HITEC_PROG_FILES_MS 5000u

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
    HITEC_PROG_RECORDING,  /**< a write waits for the card's record       */
    HITEC_PROG_WRITING,    /**< a write runs, with its read-back          */
    HITEC_PROG_BACKUP,     /**< a write waits for the card's backup file  */
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
    HITEC_PROG_MSG_WRITTEN,       /**< a write verified; arg the values    */
    HITEC_PROG_MSG_REPAIRED,      /**< the classic balance written          */
    HITEC_PROG_MSG_RESTORED,      /**< the record's values written back     */
    HITEC_PROG_MSG_WRITE_OFF,     /**< the write switch is off             */
    HITEC_PROG_MSG_NOT_READ,      /**< no servo read                       */
    HITEC_PROG_MSG_MUST_READ,     /**< a write failed: READ ALL first      */
    HITEC_PROG_MSG_NO_CHANGES,    /**< the draft is the servo's            */
    HITEC_PROG_MSG_EDITS_PENDING, /**< edits not written stand             */
    HITEC_PROG_MSG_EDIT_REFUSED,  /**< an edit refused; arg hitec_set_t    */
    HITEC_PROG_MSG_RECORD_FAILED, /**< the card took no record; nothing
                                       was written                         */
    HITEC_PROG_MSG_WRITE_CUT,     /**< a write ended after a step went out;
                                       arg the address (a balance: the cell
                                       it balances), reg the
                                       hitec_step_kind_t of the step out or
                                       HITEC_PROG_CUT_CHECK                */
    HITEC_PROG_MSG_VERIFY_FAIL,   /**< the read-back differs; arg the
                                       addresses that differ               */
    HITEC_PROG_MSG_RECORD,        /**< a record stands, not compared yet;
                                       arg its pin, reg its port           */
    HITEC_PROG_MSG_REC_SAME,      /**< the servo holds the values before
                                       the recorded write; as RECORD       */
    HITEC_PROG_MSG_REC_DONE,      /**< it holds the write's values         */
    HITEC_PROG_MSG_REC_MIX,       /**< it holds both                       */
    HITEC_PROG_MSG_REC_OTHER,     /**< it is another servo; as RECORD      */
    HITEC_PROG_MSG_RECORD_HELD,   /**< a write refused: a record stands    */
    HITEC_PROG_MSG_FORGOTTEN,     /**< the record discarded                */
    HITEC_PROG_MSG_RECORD_BAD,    /**< the card's record does not read; arg
                                       hitec_card_result_t                 */
    HITEC_PROG_MSG_SUM_OK,        /**< REPAIR refused: the sum is right    */
    HITEC_PROG_MSG_SAVED,         /**< a file saved: hitec_prog_card_name() */
    HITEC_PROG_MSG_CARD_NONE,     /**< no card: nothing saved or written;
                                       arg the feed's card_state_t         */
    HITEC_PROG_MSG_CARD_FULL,     /**< no space, or no proposed name left  */
    HITEC_PROG_MSG_CARD_WRITE,    /**< the file was not written whole, or
                                       the card did not answer in time     */
    HITEC_PROG_MSG_CARD_READ,     /**< the card was not read               */
    HITEC_PROG_MSG_CARD_DENIED,   /**< the card takes no file              */
    HITEC_PROG_MSG_NO_FILES,      /**< the card holds no servo file        */
    HITEC_PROG_MSG_NAME_BAD,      /**< the name is not one a file may have */
    HITEC_PROG_MSG_NAME_EXISTS,   /**< a file has the name                 */
    HITEC_PROG_MSG_LOAD_BAD,      /**< the file is no servo file           */
    HITEC_PROG_MSG_LOAD_DAMAGED,  /**< it is cut short or its check fails;
                                       arg hitec_card_result_t             */
    HITEC_PROG_MSG_LOAD_LINE,     /**< a line of it does not read; arg the
                                       line                                */
    HITEC_PROG_MSG_LOAD_SERIES,   /**< it is of the other series; arg its
                                       hitec_family_t                      */
    HITEC_PROG_MSG_LOAD_LAYOUT,   /**< it is of another layout; arg its
                                       hitec_layout_t, reg the servo's     */
    HITEC_PROG_MSG_LOAD_SAME,     /**< it changes no editable value; arg
                                       the values not loaded               */
    HITEC_PROG_MSG_LOAD_CHECK,    /**< it waits for CHECK THIS FILE        */
    HITEC_PROG_MSG_LOADED,        /**< the draft holds it; arg the values
                                       that differ, reg the values not
                                       loaded                              */
    HITEC_PROG_MSG_BACKUP_RESTORED, /**< the session's backup written back
                                       and verified                        */
    HITEC_PROG_MSG_FACTORY_DONE,  /**< the factory values written and
                                       verified                            */
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
    bool     card;            /**< a card is mounted: a write's record
                                   and its backup file go to it           */
    uint8_t  card_state;      /**< card_state_t of the panel's slot: what
                                   the screen says without a card         */
} hitec_prog_feed_t;

/** @brief The soft rules of an edit: bit n of a rule set is rule n. */
typedef enum {
    HITEC_RULE_SPEED = 0,  /**< SPEED below 30%                        */
    HITEC_RULE_DEAD_BAND,  /**< DEAD BAND below the servo's            */
    HITEC_RULE_OVERLOAD,   /**< OVERLOAD PROTECTION switched off       */
    HITEC_RULE_END,        /**< an end point differs from the servo's  */
    HITEC_RULE_CENTRE,     /**< NEUTRAL or CENTRE differs from it      */
    HITEC_RULE_DIRECTION,  /**< DIRECTION reversed                     */
    HITEC_RULE_FS_FAR,     /**< a failsafe position more than 300us
                                from 1500us                            */
    HITEC_RULE_COUNT
} hitec_prog_rule_t;

/** @brief The bit of rule @p r in a rule set. */
#define HITEC_RULE_BIT(r) ((uint16_t)(1u << (r)))

/** @brief HITEC_PROG_MSG_WRITE_CUT's kind of a cut in the read-back. */
#define HITEC_PROG_CUT_CHECK 0xFFu

/** @brief The speed below which SPEED asks, %. */
#define HITEC_PROG_SLOW_PCT 30
/** @brief How far from 1500us a failsafe position asks, us. */
#define HITEC_PROG_FS_FAR_US 300

/** @brief What an edit came to. */
typedef enum {
    HITEC_PROG_EDIT_OK = 0,   /**< the draft holds it                    */
    HITEC_PROG_EDIT_CHECK,    /**< it waits for CHECK THIS CHANGE        */
    HITEC_PROG_EDIT_REFUSED,  /**< refused; the message says why         */
} hitec_prog_edit_t;

/** @brief What the servo read holds of a record. */
typedef enum {
    HITEC_PROG_REC_NONE = 0,  /**< no record, or not compared            */
    HITEC_PROG_REC_SAME,      /**< the values before the write           */
    HITEC_PROG_REC_DONE,      /**< the values the write was to leave     */
    HITEC_PROG_REC_MIX,       /**< some of each                          */
    HITEC_PROG_REC_OTHER,     /**< another servo: another series, port,
                                   model, or a value that is neither     */
} hitec_prog_rec_t;

/** @brief What a write is. */
typedef enum {
    HITEC_PROG_WR_EDITS = 0,  /**< WRITE n VALUES                        */
    HITEC_PROG_WR_REPAIR,     /**< REPAIR: the classic balance           */
    HITEC_PROG_WR_RESTORE,    /**< the record's values back              */
    HITEC_PROG_WR_BACKUP,     /**< the session's backup back             */
    HITEC_PROG_WR_FACTORY,    /**< classic: the factory cells            */
} hitec_prog_wr_t;

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

    /* --- the write side, hitec_prog_write.c --------------------------- */
    hitec_image_t draft;      /**< the servo read with the edits          */
    hitec_image_t wr_target;  /**< what the running write is to leave     */
    bool     write_on;        /**< ENABLE WRITE TO SERVO                  */
    bool     must_read;       /**< a write failed: READ ALL comes first   */
    bool     wrote;           /**< a write of this CONNECT went out       */
    uint8_t  wr_kind;         /**< hitec_prog_wr_t of the running write   */
    uint8_t  wr_values;       /**< the values it changes                  */
    bool     wr_own_rec;      /**< it made the record that stands         */
    uint64_t soft_has;        /**< bit id: soft_val[id] is confirmed      */
    int32_t  soft_val[HITEC_P_COUNT]; /**< the values confirmed           */
    bool     pend;            /**< an edit waits for a confirmation       */
    uint8_t  pend_id;         /**< its hitec_param_t                      */
    int32_t  pend_value;      /**< its value                              */
    uint16_t pend_rules;      /**< the rules it breaks                    */
    bool     rec;             /**< a record of a write stands             */
    bool     rec_open;        /**< and no servo was compared with it      */
    uint8_t  rec_verdict;     /**< hitec_prog_rec_t of the last compare   */
    hitec_card_record_t rec_r; /**< the record                            */
    bool     file_wanted;     /**< the record's text is due on the card   */
    bool     file_busy;       /**< and out                                */
    /** @brief The text out holds the record of the write that waits for
     *         it: a text taken before that write began is not its record. */
    bool     file_rec;
    uint32_t file_ms;         /**< when a write began to wait for it      */

    /* --- files and backups, hitec_prog_file.c ------------------------- */
    bool     have_bk;         /**< the session's backup is held           */
    bool     bk_saved;        /**< and a file on the card holds it        */
    bool     bk_no_file;      /**< a write went with no card for it       */
    uint32_t bk_gen;          /**< counts the backups: one per session and
                                   per servo that replaces it             */
    uint32_t card_gen;        /**< bk_gen when the request out was asked;
                                   a save or a load answered under
                                   another bk_gen is of another session */
    hitec_image_t bk;         /**< the session's backup                   */
    char     bk_name[HITEC_CARD_NAME_MAX]; /**< the file of bk_saved     */
    uint8_t  card_op;         /**< hitec_card_op_t awaited; IDLE none     */
    bool     card_ready;      /**< and not taken yet                      */
    bool     card_auto;       /**< the request is the backup file's       */
    uint16_t card_first;      /**< LIST: the first file asked for         */
    uint32_t card_id;         /**< the last request's id                  */
    uint32_t card_ms;         /**< when it was asked                      */
    char     card_name[HITEC_CARD_NAME_MAX]; /**< the file saved or loaded */
    uint8_t  files_n;         /**< files in the last list                 */
    uint16_t files_total;     /**< files the card holds                   */
    uint16_t files_first;     /**< the first file of the last list        */
    uint32_t files_seq;       /**< counts the lists that arrived          */
    char     files[HITEC_CARD_FILES][HITEC_CARD_NAME_MAX]; /**< names     */
    char     models[HITEC_CARD_FILES][HITEC_CARD_MODEL_MAX + 1u]; /**< their
                                   model lines                            */
    char     preset[HITEC_CARD_NAME_MAX]; /**< the next proposed name     */
    bool     load_pend;       /**< a load waits for CHECK THIS FILE       */
    bool     load_other;      /**< its file is of another model           */
    uint16_t load_rules;      /**< the soft rules it breaks               */
    uint8_t  load_skipped;    /**< values of the file not loaded          */
    hitec_image_t load_img;   /**< the draft it leaves                    */
    char     load_model[HITEC_CARD_MODEL_MAX + 1u]; /**< its model line   */
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
 * @brief The series of the next CONNECT; taken in PWM only.  A change of
 *        series drops what the last read found: hitec_prog_servo() and
 *        hitec_prog_model() return NULL until the next read.
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

/* --- the write side (hitec_prog_write.c) ---------------------------------- */

/**
 * @brief The draft: the servo read with the edits; NULL with no servo read.
 *
 * @param p The programmer.
 * @return The draft.
 */
const hitec_image_t *hitec_prog_draft(const hitec_prog_t *p);

/**
 * @brief Whether an edit is taken now: the port held and idle, the servo in
 *        programming mode and read, the port not stopped.
 *
 * @param p The programmer.
 * @return true when it is.
 */
bool hitec_prog_can_edit(const hitec_prog_t *p);

/**
 * @brief Whether parameter @p id of the servo's layout takes an edit.
 *
 * @param p  The programmer.
 * @param id hitec_param_t.
 * @return true for a writable parameter of the layout.
 */
bool hitec_prog_editable(const hitec_prog_t *p, uint8_t id);

/**
 * @brief Edit the draft: @p value in the unit of parameter @p id.
 *
 * A value equal to the draft's changes nothing.  A value hitec_param_set()
 * refuses is refused (HITEC_PROG_MSG_EDIT_REFUSED, arg its hitec_set_t).  A
 * value that breaks a soft rule not confirmed for it waits:
 * hitec_prog_pending() names it, hitec_prog_confirm() takes it,
 * hitec_prog_cancel() drops it.
 *
 * @param p     The programmer.
 * @param id    hitec_param_t.
 * @param value The value.
 * @return What the edit came to.
 */
hitec_prog_edit_t hitec_prog_edit(hitec_prog_t *p, uint8_t id,
                                  int32_t value);

/**
 * @brief The edit that waits for a confirmation.
 *
 * @param p     The programmer.
 * @param id    Receives its hitec_param_t; may be NULL.
 * @param value Receives its value; may be NULL.
 * @return Its rule set; 0 with none waiting.
 */
uint16_t hitec_prog_pending(const hitec_prog_t *p, uint8_t *id,
                            int32_t *value);

/**
 * @brief Take the edit that waits: its rules are confirmed for its value.
 *
 * @param p The programmer.
 * @return What the edit came to; HITEC_PROG_EDIT_REFUSED with none waiting.
 */
hitec_prog_edit_t hitec_prog_confirm(hitec_prog_t *p);

/**
 * @brief Drop the edit that waits; the draft stays as it was.
 *
 * @param p The programmer.
 */
void hitec_prog_cancel(hitec_prog_t *p);

/**
 * @brief The soft rules @p value of parameter @p id breaks against the
 *        servo read.
 *
 * @param p     The programmer.
 * @param id    hitec_param_t.
 * @param value The value.
 * @return The rule set; 0 with no servo read.
 */
uint16_t hitec_prog_rules(const hitec_prog_t *p, uint8_t id, int32_t value);

/**
 * @brief Whether parameter @p id differs in @p img from the servo read: in
 *        its decoded value or in its stored value.
 *
 * @param p   The programmer.
 * @param img The image; the draft for NULL.
 * @param id  hitec_param_t.
 * @return true when it differs.
 */
bool hitec_prog_differs(const hitec_prog_t *p, const hitec_image_t *img,
                        uint8_t id);

/**
 * @brief The writable parameters in which the draft differs from the
 *        servo read.
 *
 * @param p The programmer.
 * @return The count; 0 with no servo read.
 */
unsigned hitec_prog_changes(const hitec_prog_t *p);

/**
 * @brief DISCARD EDITS: the draft is the servo read again, and every
 *        confirmation ends.
 *
 * @param p The programmer.
 */
void hitec_prog_discard(hitec_prog_t *p);

/**
 * @brief ENABLE WRITE TO SERVO.
 *
 * Off is always taken.  On needs the state hitec_prog_can_edit() needs, a
 * SAFE bench and no STOP latched.
 *
 * @param p  The programmer.
 * @param on The switch.
 * @return HITEC_PROG_MSG_NONE when taken, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_write_enable(hitec_prog_t *p, bool on);

/**
 * @brief Whether ENABLE WRITE TO SERVO is on.
 *
 * @param p The programmer.
 * @return true when on.
 */
bool hitec_prog_write_on(const hitec_prog_t *p);

/**
 * @brief Why a write of @p kind is not taken now, the switch included.
 *
 * @param p    The programmer.
 * @param kind hitec_prog_wr_t.
 * @return HITEC_PROG_MSG_NONE when it is taken, else the reason.
 */
hitec_prog_msg_id_t hitec_prog_write_gate(const hitec_prog_t *p,
                                          uint8_t kind);

/**
 * @brief WRITE n VALUES: the draft to the servo, then its read-back.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_write(hitec_prog_t *p);

/**
 * @brief Whether the classic servo read has cells 0x00 to 0x1C that do not
 *        sum to 0.
 *
 * @param p The programmer.
 * @return true when REPAIR has something to write.
 */
bool hitec_prog_sum_bad(const hitec_prog_t *p);

/**
 * @brief REPAIR: the classic balance 0x1C alone, then the read-back.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_repair(hitec_prog_t *p);

/**
 * @brief Whether the first write of this CONNECT has gone out.
 *
 * @param p The programmer.
 * @return true after it.
 */
bool hitec_prog_first_done(const hitec_prog_t *p);

/**
 * @brief The progress of the running write.
 *
 * @param p     The programmer.
 * @param done  Receives the plan's steps done.
 * @param total Receives its steps.
 * @param kind  Receives the hitec_step_kind_t of the step out; 0xFF in the
 *              read-back.
 * @return true while a write runs.
 */
bool hitec_prog_write_progress(const hitec_prog_t *p, unsigned *done,
                               unsigned *total, uint8_t *kind);

/**
 * @brief Whether a record of a write stands.
 *
 * @param p The programmer.
 * @return true when one does.
 */
bool hitec_prog_record(const hitec_prog_t *p);

/**
 * @brief What the last servo read holds of the record.
 *
 * @param p The programmer.
 * @return hitec_prog_rec_t; HITEC_PROG_REC_NONE while not compared.
 */
hitec_prog_rec_t hitec_prog_record_verdict(const hitec_prog_t *p);

/**
 * @brief RESTORE: the values before the recorded write back to the servo,
 *        as a write; the record stays until it ends verified.
 *
 * Taken once the servo read is compared and is not another servo.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_restore(hitec_prog_t *p);

/**
 * @brief The image RESTORE writes: the servo read with the record's values
 *        before the write.
 *
 * @param p   The programmer.
 * @param out Receives it.
 * @return false with no record or no servo read.
 */
bool hitec_prog_restore_image(const hitec_prog_t *p, hitec_image_t *out);

/**
 * @brief FORGET: discard the record without a write.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when taken; BUSY while a write runs.
 */
hitec_prog_msg_id_t hitec_prog_forget(hitec_prog_t *p);

/**
 * @brief Whether the record's text is to go to the card.
 *
 * @param p The programmer.
 * @return true while one is due and none is out.
 */
bool hitec_prog_file_due(const hitec_prog_t *p);

/**
 * @brief The record's text for the card: fill @p out and return its
 *        length; 0 while none is due, no card is mounted or @p n is under
 *        HITEC_CARD_TEXT_MAX.
 *
 * @param p   The programmer.
 * @param out Receives the text.
 * @param n   Bytes at @p out.
 * @return The length.
 */
size_t hitec_prog_file_take(hitec_prog_t *p, char *out, size_t n);

/**
 * @brief How the card took the text hitec_prog_file_take() gave.
 *
 * Only the answer to the text that holds the record of the write waiting
 * in HITEC_PROG_RECORDING releases that write, or ends it when the card
 * refused the text.  The answer to a text taken before that write began,
 * the record of an aborted or timed-out write or the text that clears a
 * record, only frees the card for the next text.
 *
 * @param p  The programmer.
 * @param ok true when the card holds it whole.
 */
void hitec_prog_file_done(hitec_prog_t *p, bool ok);

/**
 * @brief The record's text as the card held it at the start.
 *
 * @param p    The programmer.
 * @param text The text.
 * @param len  Its length in bytes.
 * @return false, with HITEC_PROG_MSG_RECORD_BAD, for a text that does not
 *         parse.
 */
bool hitec_prog_file_load(hitec_prog_t *p, const char *text, size_t len);

/* --- files and backups (hitec_prog_file.c) ------------------------------- */

/**
 * @brief Why FILES, a save or a load is not taken now.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when taken: the port held and idle, the
 *         bench SAFE with no STOP latched and the port not stopped, the
 *         servo in programming mode and read, no read owed, no request of
 *         the card out, a card mounted.
 */
hitec_prog_msg_id_t hitec_prog_files_gate(const hitec_prog_t *p);

/**
 * @brief FILES: ask the card for the files from @p first on.  The list
 *        arrives as hitec_prog_files_seq() counts up.
 *
 * @param p     The programmer.
 * @param first The first file, from 0.
 * @return HITEC_PROG_MSG_NONE when asked, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_files(hitec_prog_t *p, unsigned first);

/**
 * @brief Save the servo read to a new file @p name.
 *
 * @param p    The programmer.
 * @param name The base name, 1 to 32 characters.
 * @return HITEC_PROG_MSG_NONE when asked, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_save(hitec_prog_t *p, const char *name);

/**
 * @brief Load the file @p name into the draft.
 *
 * The gate of hitec_prog_files_gate(), taken again when the card answers
 * and at hitec_prog_load_confirm().
 *
 * @param p    The programmer.
 * @param name The base name.
 * @return HITEC_PROG_MSG_NONE when asked, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_load(hitec_prog_t *p, const char *name);

/**
 * @brief The load that waits: its rules and whether its file is of another
 *        model.
 *
 * @param p     The programmer.
 * @param other Receives true for another model; may be NULL.
 * @return Its rule set; 0 with none waiting or none broken.
 */
uint16_t hitec_prog_load_pending(const hitec_prog_t *p, bool *other);

/**
 * @brief Whether a load waits for CHECK THIS FILE.
 *
 * @param p The programmer.
 * @return true while one waits.
 */
bool hitec_prog_load_waits(const hitec_prog_t *p);

/**
 * @brief The image the load that waits leaves in the draft.
 *
 * @param p The programmer.
 * @return The image; NULL with none waiting.
 */
const hitec_image_t *hitec_prog_load_image(const hitec_prog_t *p);

/**
 * @brief Take the load that waits: the draft holds it, its rules are
 *        confirmed for its values.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when taken, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_load_confirm(hitec_prog_t *p);

/**
 * @brief Drop the load that waits; the draft stays as it was.
 *
 * @param p The programmer.
 */
void hitec_prog_load_cancel(hitec_prog_t *p);

/**
 * @brief Whether a request of the card is out.
 *
 * @param p The programmer.
 * @return true while one waits for its answer.
 */
bool hitec_prog_card_busy(const hitec_prog_t *p);

/**
 * @brief The next request for the card's side.
 *
 * @param p   The programmer.
 * @param out Receives it.
 * @return true when one was taken.
 */
bool hitec_prog_card_take(hitec_prog_t *p, hitec_card_req_t *out);

/**
 * @brief The card's side's answer to a request taken.  An answer to a
 *        request given up (HITEC_PROG_FILES_MS) is taken only where it
 *        saved the backup file.
 *
 * @param p The programmer.
 * @param d The answer.
 */
void hitec_prog_card_done(hitec_prog_t *p, const hitec_card_done_t *d);

/**
 * @brief Counts the lists that arrived.
 *
 * @param p The programmer.
 * @return The count.
 */
uint32_t hitec_prog_files_seq(const hitec_prog_t *p);

/**
 * @brief The files of the last list.
 *
 * @param p     The programmer.
 * @param total Receives the files the card holds; may be NULL.
 * @param first Receives the first file's place, from 0; may be NULL.
 * @return The files in it.
 */
unsigned hitec_prog_file_count(const hitec_prog_t *p, unsigned *total,
                               unsigned *first);

/**
 * @brief File @p i of the last list.
 *
 * @param p     The programmer.
 * @param i     From 0.
 * @param model Receives its model line, "" for a file that does not parse;
 *              may be NULL.
 * @return Its base name; "" past the last.
 */
const char *hitec_prog_file_name(const hitec_prog_t *p, unsigned i,
                                 const char **model);

/**
 * @brief The next proposed name of the last list.
 *
 * @param p The programmer.
 * @return The name; "" for none.
 */
const char *hitec_prog_preset_name(const hitec_prog_t *p);

/**
 * @brief The file last saved or loaded.
 *
 * @param p The programmer.
 * @return The base name.
 */
const char *hitec_prog_card_name(const hitec_prog_t *p);

/**
 * @brief The session's backup file.
 *
 * @param p The programmer.
 * @return Its base name; "" while no file holds the backup.
 */
const char *hitec_prog_backup_name(const hitec_prog_t *p);

/**
 * @brief Whether a write of this session went with no card, so the
 *        backup is in RAM only.
 *
 * @param p The programmer.
 * @return true after such a write while no file holds the backup.
 */
bool hitec_prog_backup_ram_only(const hitec_prog_t *p);

/**
 * @brief Whether the next write saves the backup file first: a card is
 *        mounted and no file holds the backup.
 *
 * @param p The programmer.
 * @return true when it does.
 */
bool hitec_prog_backup_due(const hitec_prog_t *p);

/**
 * @brief The image RESTORE of the session's backup writes: the servo read
 *        with every writable address of the backup.
 *
 * @param p   The programmer.
 * @param out Receives it.
 * @return false with no backup or no servo read.
 */
bool hitec_prog_backup_image(const hitec_prog_t *p, hitec_image_t *out);

/**
 * @brief Whether the session's backup differs from the servo read in an
 *        address a write sends: RESTORE offers it.
 *
 * @param p The programmer.
 * @return true when it does.
 */
bool hitec_prog_backup_differs(const hitec_prog_t *p);

/**
 * @brief RESTORE of the session's backup, as a write.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_restore_backup(hitec_prog_t *p);

/**
 * @brief The image FACTORY DEFAULT writes: the classic servo read with
 *        cells 0x00 to 0x23 from its factory cells.
 *
 * @param p   The programmer.
 * @param out Receives it.
 * @return false for a D series servo, no servo read or a factory cell not
 *         known.
 */
bool hitec_prog_factory_image(const hitec_prog_t *p, hitec_image_t *out);

/**
 * @brief FACTORY DEFAULT, classic servos only, as a write.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hitec_prog_factory(hitec_prog_t *p);

#ifdef __cplusplus
}
#endif
