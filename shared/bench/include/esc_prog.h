/**
 * @file
 * @brief The programmer for an ESC (electronic speed controller) over its signal
 *        wire as a screen runs it: the session, its gate and its messages,
 *        through the panel's half of the ESC_WIRE page (esc_link.h).
 *
 * The session runs on the IOmcu.  This model asks and follows:
 *
 *   - CONNECT, esc_prog_connect(): OPEN, ENTER and READ, each after the one
 *     before ended well.  BY HAND stops at HAND_WAIT until esc_prog_powered().
 *     Refused, with nothing sent, on an ARMED bench, with STOP latched, on an
 *     IOmcu without the page, with a pin held already, and with the supply's
 *     values out of the page's range.  The voltage and the limit are the
 *     caller's: this model has no default.
 *   - esc_prog_read(): READ again.
 *   - esc_prog_write(): WRITE of a draft made from the read copy, after the
 *     session's backup is settled (esc_prog_file.c).  Refused on a page that is
 *     not written, on a stale read copy, and for a draft equal to the copy.
 *   - esc_prog_leave(): LEAVE, when the ESC is in its bootloader, then CLOSE.
 *   - esc_prog_clear() clears a stop or a fault; esc_prog_close() gives the pin
 *     back, one another writer holds included.
 *
 * A stop reaches the page by the task that owns the link (esc_link_stop()), not
 * through this model; esc_prog_abort() is the operator's cut.
 *
 * Every refusal and outcome becomes one message (esc_prog_msg_t).  A screen has
 * the texts.  Whether a pin is held counts any writer's: the bench console's, and
 * one held over a panel restart until its CLOSE (esc_prog_holds_port()).
 *
 * Pure C.  The caller hands in esc_prog_feed_t every frame and carries out
 * esc_prog_next_cmd()'s commands on the link's task, one at a time.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_ESC_PROG_H
#define RCBENCH_ESC_PROG_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esc_link.h"
#include "ew_file.h"
#include "ew_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A command not carried out this long after it went is given up, ms. */
#define ESC_PROG_CMD_WAIT_MS 2000u

/** @brief The highest voltage SUPPLY takes, mV. */
#define ESC_PROG_MAX_MV 20000u
/** @brief The lowest limit SUPPLY takes, mA. */
#define ESC_PROG_MIN_MA 50u
/** @brief The highest limit SUPPLY takes, mA. */
#define ESC_PROG_MAX_MA 3000u

/** @brief What a command asks the link. */
typedef enum {
    ESC_PROG_CMD_NONE = 0,
    ESC_PROG_CMD_OPEN,
    ESC_PROG_CMD_ENTER,
    ESC_PROG_CMD_POWERED,
    ESC_PROG_CMD_READ,
    ESC_PROG_CMD_WRITE,
    ESC_PROG_CMD_RESTORE,
    ESC_PROG_CMD_LEAVE,
    ESC_PROG_CMD_CLEAR,
    ESC_PROG_CMD_CLOSE,
    ESC_PROG_CMD_ABORT,
} esc_prog_cmd_kind_t;

/** @brief One command for the link's task. */
typedef struct {
    uint8_t  kind;     /**< an esc_prog_cmd_kind_t                            */
    uint32_t id;       /**< its number, never 0                               */
    uint16_t target;   /**< OPEN: LINK_EW_W_TARGET's value                    */
    esc_link_power_t power; /**< ENTER                                        */
    uint16_t reads;    /**< WRITE, RESTORE: LINK_EW_READS                     */
    uint16_t read_crc; /**< WRITE, RESTORE: LINK_EW_READ_CRC                  */
    uint16_t len;      /**< WRITE, RESTORE: bytes of @c draft                 */
    uint8_t  draft[ESC_LINK_PAGE_MAX];    /**< WRITE, RESTORE                 */
    uint8_t  identity[LINK_ED_BLOCK_BYTES]; /**< RESTORE: block LINK_ED_BACKUP */
} esc_prog_cmd_t;

/** @brief The pass a command needs to be carried out, as hitec_prog.h's gate. */
typedef struct {
    _Atomic uint32_t pass; /**< the id that may go; 0 none                    */
} esc_prog_gate_t;

/**
 * @brief Sets up a gate: no pass offered.
 * @param g The gate.
 */
void esc_prog_gate_init(esc_prog_gate_t *g);

/**
 * @brief The link's task takes the pass of @p id.
 * @param g  The gate.
 * @param id The command's id.
 * @return true when it may be carried out.
 */
bool esc_prog_gate_claim(esc_prog_gate_t *g, uint32_t id);

/** @brief What the model is fed every frame. */
typedef struct {
    uint32_t now_ms;      /**< the time, ms                                   */
    esc_link_readout_t rd; /**< the page                                      */
    esc_link_image_t image; /**< the read copy                                */
    uint32_t answer_id;   /**< the last command carried out; 0 none           */
    uint8_t  answer;      /**< what the link said: an esc_link_start_t        */
    uint32_t outcomes;    /**< counts the outcomes handed out                 */
    esc_link_outcome_t outcome; /**< the last of them                         */
    bool     stopped;     /**< STOP is latched at the panel                   */
    bool     armed;       /**< the bench is ARMED or asked to arm             */
} esc_prog_feed_t;

/** @brief What happened last; a screen has the text. */
typedef enum {
    ESC_PROG_MSG_NONE = 0,
    ESC_PROG_MSG_ARMED,         /**< refused: the bench is ARMED              */
    ESC_PROG_MSG_STOPPED,       /**< refused: STOP is latched                 */
    ESC_PROG_MSG_NO_LINK,       /**< no IOmcu answers                         */
    ESC_PROG_MSG_NO_PAGE,       /**< the IOmcu has no ESC_WIRE page           */
    ESC_PROG_MSG_BUSY,          /**< another operation runs                   */
    ESC_PROG_MSG_HELD,          /**< a pin is held already                    */
    ESC_PROG_MSG_RANGE,         /**< the supply's values are out of range     */
    ESC_PROG_MSG_NOT_IN_MODE,   /**< no ESC has answered                      */
    ESC_PROG_MSG_NOT_READ,      /**< the page was not read                    */
    ESC_PROG_MSG_READ_ONLY,     /**< this page is not written                 */
    ESC_PROG_MSG_ERASED,        /**< AM32 settings erased: only a restore     */
    ESC_PROG_MSG_STALE,         /**< the draft is of an older read copy       */
    ESC_PROG_MSG_NO_CHANGE,     /**< the draft equals the read copy           */
    ESC_PROG_MSG_BACKUP_FIRST,  /**< the session's backup is not settled      */
    ESC_PROG_MSG_REFUSED,       /**< the page refused: detail, a link_esc_ref_t */
    ESC_PROG_MSG_NO_LINE,       /**< OPEN: no resources for the line          */
    ESC_PROG_MSG_SUPPLY,        /**< the supply failed                        */
    ESC_PROG_MSG_TIMEOUT,       /**< BY HAND: no DONE in time                 */
    ESC_PROG_MSG_CUT,           /**< a stop ended it                          */
    ESC_PROG_MSG_SESSION,       /**< the session: detail, an ew_ses_t         */
    ESC_PROG_MSG_NOT_BACK,      /**< the output did not take the pin back     */
    ESC_PROG_MSG_NO_ANSWER,     /**< the IOmcu did not answer                 */
    ESC_PROG_MSG_RESTARTED,     /**< the IOmcu started again, or another writer */
    ESC_PROG_MSG_NACK,          /**< a frame refused: detail, a link_nack_t   */
    ESC_PROG_MSG_DROPPED,       /**< a stop came before the command went      */
    ESC_PROG_MSG_ABORTED,       /**< ABORT ended it; the pin stays held       */
    ESC_PROG_MSG_HAND_WAIT,     /**< BY HAND: connect the ESC's power         */
    ESC_PROG_MSG_READ_DONE,     /**< the page is read                         */
    ESC_PROG_MSG_WRITE_DONE,    /**< written and read back                    */
    ESC_PROG_MSG_RESTORE_DONE,  /**< restored and read back                   */
    ESC_PROG_MSG_LEFT,          /**< the ESC left, the pin back               */
    ESC_PROG_MSG_CLEARED,       /**< stop or fault cleared                    */
    ESC_PROG_MSG_FILE_REFUSED,  /**< a file refused: detail, an ew_file_err_t */
    ESC_PROG_MSG_LOAD_REFUSED,  /**< a load refused: detail, an ew_load_t     */
    ESC_PROG_MSG_RESTORE_REFUSED, /**< detail, an ew_restore_t                */
    ESC_PROG_MSG_LOADED,        /**< a file is in the draft                   */
} esc_prog_msg_t;

/** @brief What CONNECT asks. */
typedef struct {
    uint16_t target;       /**< LINK_EW_W_TARGET's value                      */
    esc_link_power_t power; /**< the power, with the caller's voltage and limit */
} esc_prog_connect_t;

/** @brief The session's backup, from the first full read. */
typedef enum {
    ESC_PROG_BACKUP_NONE = 0, /**< no read yet                                */
    ESC_PROG_BACKUP_OWED,     /**< read; the file has not gone to the card    */
    ESC_PROG_BACKUP_SETTLED,  /**< saved, or the operator goes without a card */
} esc_prog_backup_t;

/** @brief The state of the model. */
typedef struct {
    esc_prog_feed_t f;       /**< the last feed                               */
    esc_prog_gate_t *gate;   /**< esc_prog_gate_set()                         */
    uint32_t next_id;        /**< the next command's id                       */
    uint32_t wait_id;        /**< the command out, 0 for none                 */
    uint32_t wait_ms;        /**< when it went                                */
    uint32_t outcomes_seen;  /**< outcomes taken                              */
    uint8_t  flow;           /**< what the commands are part of               */
    uint8_t  out_new;        /**< out holds a command to hand over            */
    esc_prog_cmd_t out;      /**< it                                          */
    esc_prog_connect_t conn; /**< CONNECT's values                            */
    uint16_t flags_seen;     /**< LINK_EW_F_FAULT and _STOPPED at the last feed */
    uint8_t  aborted;        /**< ABORT went: the outcome it cuts is its own  */
    esc_prog_msg_t msg;      /**< the last message                            */
    uint16_t detail;         /**< its code                                    */
    uint8_t  backup_state;   /**< an esc_prog_backup_t                        */
    uint32_t backup_gen;     /**< the image it was taken from                 */
    ew_file_t file;          /**< the session's backup                        */
    ew_file_t loaded;        /**< a file as read                              */
    ew_file_load_t load;     /**< what it does to the read copy               */
} esc_prog_t;

/**
 * @brief Sets up the model.
 * @param p The model.
 */
void esc_prog_init(esc_prog_t *p);

/**
 * @brief The gate the link's task claims each command at; NULL for none.
 * @param p The model.
 * @param g The gate.
 */
void esc_prog_gate_set(esc_prog_t *p, esc_prog_gate_t *g);

/**
 * @brief The frame's feed: outcomes taken, and the flow's next command made.
 * @param p The model.
 * @param f The feed.
 */
void esc_prog_feed(esc_prog_t *p, const esc_prog_feed_t *f);

/**
 * @brief The command to carry out, once.
 * @param p   The model.
 * @param out Receives it.
 * @return true when there is one.
 */
bool esc_prog_next_cmd(esc_prog_t *p, esc_prog_cmd_t *out);

/**
 * @brief CONNECT.
 * @param p The model.
 * @param c The target and the power.
 * @return ESC_PROG_MSG_NONE when it goes, else why not.
 */
esc_prog_msg_t esc_prog_connect(esc_prog_t *p, const esc_prog_connect_t *c);

/**
 * @brief BY HAND: the ESC has its power.
 * @param p The model.
 * @return ESC_PROG_MSG_NONE when it goes, else why not.
 */
esc_prog_msg_t esc_prog_powered(esc_prog_t *p);

/**
 * @brief READ the ESC's page again.
 * @param p The model.
 * @return ESC_PROG_MSG_NONE when it goes, else why not.
 */
esc_prog_msg_t esc_prog_read(esc_prog_t *p);

/**
 * @brief ENTER again on a pin held in OPEN.  The power is the caller's, what its page
 *        shows: the model keeps none of its own for this.
 * @param p The model.
 * @param c The values; its target is not used, as the pin stays held.
 * @return ESC_PROG_MSG_NONE when it goes, else why not.
 */
esc_prog_msg_t esc_prog_enter_again(esc_prog_t *p, const esc_prog_connect_t *c);

/**
 * @brief WRITE a draft of the read copy.
 * @param p     The model.
 * @param draft The draft.
 * @param len   Its bytes, the read copy's.
 * @return ESC_PROG_MSG_NONE when it goes, else why not.
 */
esc_prog_msg_t esc_prog_write(esc_prog_t *p, const uint8_t *draft, uint16_t len);

/**
 * @brief LEAVE, then CLOSE.
 * @param p The model.
 * @return ESC_PROG_MSG_NONE when it goes, else why not.
 */
esc_prog_msg_t esc_prog_leave(esc_prog_t *p);

/**
 * @brief CLEAR a stop or fault.
 * @param p The model.
 * @return ESC_PROG_MSG_NONE when it goes, else why not.
 */
esc_prog_msg_t esc_prog_clear(esc_prog_t *p);

/**
 * @brief CLOSE: the pin back, whoever opened it.
 * @param p The model.
 * @return ESC_PROG_MSG_NONE when it goes, else why not.
 */
esc_prog_msg_t esc_prog_close(esc_prog_t *p);

/**
 * @brief The operator's cut: ABORT, at once and past whatever is out.
 * @param p The model.
 */
void esc_prog_abort(esc_prog_t *p);

/**
 * @brief The last message, with its code.
 * @param p      The model.
 * @param detail Receives the code; may be NULL.
 * @return The message.
 */
esc_prog_msg_t esc_prog_message(const esc_prog_t *p, uint16_t *detail);

/**
 * @brief Whether the page holds a pin, or is asked to: any writer's.  ARM, the
 *        motor's TARGET row, a run that arms the bench wait for it.
 * @param p The model.
 * @return true then.
 */
bool esc_prog_holds_port(const esc_prog_t *p);

/**
 * @brief Whether a flow runs or a command is out.
 * @param p The model.
 * @return true then.
 */
bool esc_prog_busy(const esc_prog_t *p);

/**
 * @brief Whether a command may go with a stop latched at the panel: CLOSE and
 *        LEAVE give the pin back, and ABORT cuts; nothing else starts under a
 *        stop.
 * @param kind    An esc_prog_cmd_kind_t.
 * @param stopped STOP is latched.
 * @return true when it may go.
 */
bool esc_prog_cmd_allowed(uint8_t kind, bool stopped);

/**
 * @brief Whether the page is known free with nothing asked of it, or the IOmcu
 *        has none.  Fails closed: a page not read yet, a command out and a
 *        CLOSE under way all say no.
 * @param p The model.
 * @return true then.
 */
bool esc_prog_idle(const esc_prog_t *p);

/**
 * @brief Whether the readout says a pin is held: the link's task's word for the
 *        PROG page and the supply's error rule.
 * @param rd The readout.
 * @return true then.
 */
bool esc_prog_port_taken(const esc_link_readout_t *rd);

/* --- the session's backup and files (esc_prog_file.c) -------------------- */

/**
 * @brief The session's backup, from the read copy: taken at the first full read
 *        of a session, and called by esc_prog_feed().
 * @param p The model.
 */
void esc_prog_backup_follow(esc_prog_t *p);

/**
 * @brief Where the session's backup stands.
 * @param p The model.
 * @return An esc_prog_backup_t.
 */
esc_prog_backup_t esc_prog_backup_state(const esc_prog_t *p);

/**
 * @brief The backup's text for the card (shared/logfile/servo_store.h, stem
 *        EW_FILE_STEM).
 * @param p    The model.
 * @param out  Takes the text, without a terminator.
 * @param size Bytes at @p out; EW_FILE_MAX always fits.
 * @return Bytes written; 0 when no backup is held.
 */
size_t esc_prog_backup_text(const esc_prog_t *p, char *out, size_t size);

/**
 * @brief The backup is settled: the card has it, or the operator goes without one.
 * @param p The model.
 */
void esc_prog_backup_settled(esc_prog_t *p);

/** @brief Why the session has no backup to make. */
typedef enum {
    ESC_PROG_NO_BACKUP_HAS = 0,   /**< one can be made                        */
    ESC_PROG_NO_BACKUP_NOT_READ,  /**< no read copy                           */
    ESC_PROG_NO_BACKUP_ERASED,    /**< an erased page is no backup            */
    ESC_PROG_NO_BACKUP_MUSIC,     /**< `music` holds a byte the format has not */
    ESC_PROG_NO_BACKUP_FORMAT,    /**< the read copy gives no file            */
} esc_prog_no_backup_t;

/**
 * @brief Why no backup can be made of the read copy.
 * @param p The model.
 * @return ESC_PROG_NO_BACKUP_HAS, or why not.
 */
esc_prog_no_backup_t esc_prog_no_backup(const esc_prog_t *p);

/** @brief The first line of the record of a write. */
#define ESC_PROG_RECORD_HEAD "RCBENCH ESCW RECORD 1"
/** @brief Bytes of the record of a write: a line per ESCape32 key changed in front
 *         of a whole file. */
#define ESC_PROG_RECORD_MAX (EW_FILE_MAX + (size_t)EW_E32_KEYS * 72u + 64u)

/**
 * @brief The record of a write for the card, ESCW.TXT: a first line, the target
 *        and its GPIO, one line `NAME: old -> new` per field the draft changes,
 *        `before`, then the ESC as read in the text of ew_file.h.
 * @param p     The model.
 * @param draft The draft, of the read copy's length.
 * @param out   Takes the text, without a terminator.
 * @param size  Bytes at @p out; ESC_PROG_RECORD_MAX always fits.
 * @return Bytes written; 0 with no read copy, or for a @p size too small.
 */
size_t esc_prog_record_text(const esc_prog_t *p, const uint8_t *draft, char *out,
                            size_t size);

/**
 * @brief A file against the ESC: what a load changes.
 * @param p    The model.
 * @param text The file.
 * @param len  Its bytes.
 * @param line Receives the line of a refusal; may be NULL.
 * @return ESC_PROG_MSG_LOADED, ESC_PROG_MSG_FILE_REFUSED or
 *         ESC_PROG_MSG_LOAD_REFUSED, with the code in the message's detail.
 */
esc_prog_msg_t esc_prog_file_load(esc_prog_t *p, const char *text, size_t len,
                                  unsigned *line);

/**
 * @brief The draft of the last esc_prog_file_load() that was LOADED.
 * @param p   The model.
 * @param len Receives its bytes; may be NULL.
 * @return The draft, NULL for none.
 */
const uint8_t *esc_prog_file_draft(const esc_prog_t *p, uint16_t *len);

/**
 * @brief RESTORE a file onto an AM32 whose settings read erased.
 * @param p    The model.
 * @param text The file.
 * @param len  Its bytes.
 * @return ESC_PROG_MSG_NONE when it goes, else why not, with the code in the
 *         message's detail.
 */
esc_prog_msg_t esc_prog_file_restore(esc_prog_t *p, const char *text, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_PROG_H */
