/**
 * @file
 * @brief Programming for YGE ESCs (electronic speed controllers) as a screen
 *        runs it: the session, its gate and its messages, through the panel's
 *        half of the YGE_PROG page (yge_link.h).
 *
 * The session runs on the IOmcu.  This model asks and follows:
 *
 *   - CONNECT, yge_prog_connect(): OPEN, ENTER and READ, each after the one
 *     before ended well.  BY HAND stops at HAND_WAIT until
 *     yge_prog_hand_done().  Refused, with nothing sent, on an ARMED bench,
 *     with STOP latched, on an IOmcu without the page, with a pin held
 *     already, and with values out of the page's range.  A screen calls it at
 *     the end of the operator's 2s hold: ENTER carries the key, and no other
 *     path sends ENTER.
 *   - yge_prog_read(): READ again.
 *   - yge_prog_write(): WRITE of a draft made from the read copy, at the end
 *     of the operator's 2s hold.  Refused, with nothing sent, on a table that
 *     is read-only, on a stale read copy, for a draft equal to the copy, and
 *     for a draft the guard of protocols/openyge refuses (yge_prog_bad()).
 *     The first write of a session saves the session's backup first
 *     (yge_prog_file.c); without a card the write goes, and the old values
 *     are kept in RAM only (yge_prog_backup_where()).
 *   - yge_prog_leave(): LEAVE when an ESC answers, then CLOSE.
 *   - yge_prog_clear() clears a stop or a fault; yge_prog_close() gives the
 *     pin back, one another writer holds included.
 *
 * A stop reaches the page by the task that owns the link (yge_link_stop()),
 * not through this model; yge_prog_abort() is the operator's cut.  With a
 * stop latched at the panel only CLOSE, LEAVE and ABORT go
 * (yge_prog_cmd_allowed()).
 *
 * Every refusal and outcome becomes one message (yge_prog_msg_t).  A screen
 * has the texts.  Whether a pin is held counts any writer's: the bench
 * console's, and one held over a panel restart until its CLOSE
 * (yge_prog_holds_port()).
 *
 * Pure C.  The caller hands in yge_prog_feed_t every frame and carries out
 * yge_prog_next_cmd()'s commands on the link's task, one at a time.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_YGE_PROG_H
#define RCBENCH_YGE_PROG_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openyge_file.h"
#include "yge_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A command not carried out this long after it went is given up, ms. */
#define YGE_PROG_CMD_WAIT_MS 2000u
/** @brief A write whose backup the card has not answered this long after it
 *         was asked goes nowhere, ms. */
#define YGE_PROG_CARD_WAIT_MS 10000u

/** @brief The highest voltage SUPPLY takes, mV. */
#define YGE_PROG_MAX_MV 20000u
/** @brief The lowest limit SUPPLY takes, mA. */
#define YGE_PROG_MIN_MA 50u
/** @brief The highest limit SUPPLY takes, mA. */
#define YGE_PROG_MAX_MA 3000u

/** @brief What a command asks the link. */
typedef enum {
    YGE_PROG_CMD_NONE = 0,
    YGE_PROG_CMD_OPEN,
    YGE_PROG_CMD_ENTER,
    YGE_PROG_CMD_HAND_DONE,
    YGE_PROG_CMD_READ,
    YGE_PROG_CMD_WRITE,
    YGE_PROG_CMD_LEAVE,
    YGE_PROG_CMD_CLEAR,
    YGE_PROG_CMD_CLOSE,
    YGE_PROG_CMD_ABORT,
} yge_prog_cmd_kind_t;

/** @brief One command for the link's task. */
typedef struct {
    uint8_t  kind;     /**< a yge_prog_cmd_kind_t                             */
    uint32_t id;       /**< its number, never 0                               */
    uint8_t  port;     /**< OPEN: the programming port                        */
    yge_link_power_t power; /**< ENTER                                        */
    uint16_t reads;    /**< WRITE: LINK_YP_READS                              */
    uint16_t read_crc; /**< WRITE: LINK_YP_READ_CRC                           */
    uint16_t n;        /**< WRITE: values of @c draft                         */
    uint16_t draft[YGE_LINK_VALUES_MAX]; /**< WRITE                           */
} yge_prog_cmd_t;

/** @brief The pass a command needs to be carried out, as esc_prog.h's gate. */
typedef struct {
    _Atomic uint32_t pass; /**< the id that may go; 0 none                    */
} yge_prog_gate_t;

/**
 * @brief Sets up a gate: no pass offered.
 * @param g The gate.
 */
void yge_prog_gate_init(yge_prog_gate_t *g);

/**
 * @brief The link's task takes the pass of @p id.
 * @param g  The gate.
 * @param id The command's id.
 * @return true when it may be carried out.
 */
bool yge_prog_gate_claim(yge_prog_gate_t *g, uint32_t id);

/** @brief What the model is fed every frame. */
typedef struct {
    uint32_t now_ms;      /**< the time, ms                                   */
    yge_link_readout_t rd; /**< the page                                      */
    yge_link_image_t image; /**< the read copy                                */
    uint32_t answer_id;   /**< the last command carried out; 0 none           */
    uint8_t  answer;      /**< what the link said: a yge_link_start_t         */
    uint32_t outcomes;    /**< counts the outcomes handed out                 */
    yge_link_outcome_t outcome; /**< the last of them                         */
    bool     stopped;     /**< STOP is latched at the panel                   */
    bool     armed;       /**< the bench is ARMED or asked to arm             */
} yge_prog_feed_t;

/** @brief What happened last; a screen has the text. */
typedef enum {
    YGE_PROG_MSG_NONE = 0,
    YGE_PROG_MSG_ARMED,         /**< refused: the bench is ARMED              */
    YGE_PROG_MSG_STOPPED,       /**< refused: STOP is latched                 */
    YGE_PROG_MSG_NO_LINK,       /**< no IOmcu answers                         */
    YGE_PROG_MSG_NO_PAGE,       /**< the IOmcu has no YGE_PROG page           */
    YGE_PROG_MSG_BUSY,          /**< another operation runs                   */
    YGE_PROG_MSG_HELD,          /**< a pin is held already                    */
    YGE_PROG_MSG_RANGE,         /**< a value of CONNECT is out of range       */
    YGE_PROG_MSG_NOT_READY,     /**< no ESC has answered                      */
    YGE_PROG_MSG_NOT_READ,      /**< the table was not read                   */
    YGE_PROG_MSG_READ_ONLY,     /**< the table is not written: detail, the
                                     oy_ref_t of why                          */
    YGE_PROG_MSG_STALE,         /**< the draft is of an older read copy       */
    YGE_PROG_MSG_NO_CHANGE,     /**< the draft equals the read copy           */
    YGE_PROG_MSG_GUARD,         /**< the guard refuses the draft: detail, an
                                     oy_ref_t; yge_prog_bad() the index       */
    YGE_PROG_MSG_REFUSED,       /**< the page refused: detail, a
                                     link_yge_ref_t                           */
    YGE_PROG_MSG_NO_LINE,       /**< OPEN: no resources for the line          */
    YGE_PROG_MSG_SUPPLY,        /**< the supply failed                        */
    YGE_PROG_MSG_TIMEOUT,       /**< BY HAND: no HAND_DONE in time            */
    YGE_PROG_MSG_CUT,           /**< a stop ended it                          */
    YGE_PROG_MSG_SESSION,       /**< the session: detail, an oy_res_t         */
    YGE_PROG_MSG_NOT_BACK,      /**< the port did not take the pin back       */
    YGE_PROG_MSG_NO_ANSWER,     /**< the IOmcu or the card did not answer     */
    YGE_PROG_MSG_RESTARTED,     /**< the IOmcu started again, or another
                                     writer                                   */
    YGE_PROG_MSG_NACK,          /**< a frame refused: detail, a link_nack_t   */
    YGE_PROG_MSG_DROPPED,       /**< a stop came before the command went      */
    YGE_PROG_MSG_ABORTED,       /**< ABORT ended it; the pin stays held       */
    YGE_PROG_MSG_HAND_WAIT,     /**< BY HAND: connect the ESC's battery       */
    YGE_PROG_MSG_READ_DONE,     /**< the table is read                        */
    YGE_PROG_MSG_WRITE_DONE,    /**< written and read back, as asked          */
    YGE_PROG_MSG_LEFT,          /**< the session ended, the pin back          */
    YGE_PROG_MSG_CLEARED,       /**< stop or fault cleared                    */
    YGE_PROG_MSG_BACKUP_RAM,    /**< the backup did not reach the card: the
                                     old values are kept in RAM only; detail,
                                     a yge_prog_card_t                        */
    YGE_PROG_MSG_FILE_REFUSED,  /**< a file refused: detail, an oy_file_err_t */
    YGE_PROG_MSG_LOAD_REFUSED,  /**< a load refused: detail, an oy_load_t     */
    YGE_PROG_MSG_LOADED,        /**< a file is in the draft: detail, the
                                     values it changes                        */
} yge_prog_msg_t;

/** @brief What CONNECT asks. */
typedef struct {
    uint8_t port;           /**< the programming port, 0 to 3                 */
    yge_link_power_t power; /**< the power, with the caller's values          */
} yge_prog_connect_t;

/** @brief The session's backup: the table as the session's first read found
 *         it. */
typedef enum {
    YGE_PROG_BACKUP_NONE = 0, /**< no read yet in this session                */
    YGE_PROG_BACKUP_HELD,     /**< read and held here; not saved yet          */
    YGE_PROG_BACKUP_CARD,     /**< saved to the card                          */
    YGE_PROG_BACKUP_RAM,      /**< not on the card: kept in RAM only          */
} yge_prog_backup_t;

/** @brief How the card took the backup.  The numbers are stable. */
typedef enum {
    YGE_PROG_CARD_OK = 0,
    YGE_PROG_CARD_NO_CARD,     /**< no card                                   */
    YGE_PROG_CARD_NO_FAT,      /**< the card is not FAT32 or exFAT            */
    YGE_PROG_CARD_UNREADABLE,  /**< the card does not read                    */
    YGE_PROG_CARD_FULL,        /**< no space, or 001 to 999 are taken         */
    YGE_PROG_CARD_WRITE,       /**< a write failed, or the card takes no file */
    YGE_PROG_CARD_FORMAT,      /**< the table gives no file                   */
    YGE_PROG_CARD_LATE,        /**< no answer in YGE_PROG_CARD_WAIT_MS        */
} yge_prog_card_t;

/** @brief The state of the model. */
typedef struct {
    yge_prog_feed_t f;       /**< the last feed                               */
    yge_prog_gate_t *gate;   /**< yge_prog_gate_set()                         */
    uint32_t next_id;        /**< the next command's id                       */
    uint32_t wait_id;        /**< the command out, 0 for none                 */
    uint32_t wait_ms;        /**< when it went                                */
    uint32_t outcomes_seen;  /**< outcomes taken                              */
    uint8_t  flow;           /**< what the commands are part of               */
    uint8_t  out_new;        /**< out holds a command to hand over            */
    yge_prog_cmd_t out;      /**< it                                          */
    yge_prog_connect_t conn; /**< CONNECT's values                            */
    uint16_t flags_seen;     /**< LINK_YP_F_FAULT and _STOPPED at the last feed */
    uint16_t limit_mv;       /**< SUPPLY's upper voltage, mV; 0 for none      */
    uint16_t limit_ma;       /**< SUPPLY's upper limit, mA; 0 for none        */
    uint8_t  aborted;        /**< ABORT went: the outcome it cuts is its own  */
    yge_prog_msg_t msg;      /**< the last message                            */
    uint16_t detail;         /**< its code                                    */
    uint16_t bad;            /**< YGE_PROG_MSG_GUARD: the index refused       */
    uint16_t file_line;      /**< YGE_PROG_MSG_FILE_REFUSED: the line, from 1 */

    /* The session's backup (yge_prog_file.c). */
    uint8_t  backup_state;   /**< a yge_prog_backup_t                         */
    uint8_t  backup_why;     /**< YGE_PROG_BACKUP_RAM: a yge_prog_card_t      */
    uint16_t backup_n;       /**< values of backup                            */
    uint16_t backup[YGE_LINK_VALUES_MAX]; /**< the table as first read        */
    uint8_t  card_owed;      /**< the card is to be asked for the backup      */
    uint8_t  card_out;       /**< it was asked and has not answered           */
    uint32_t card_seq;       /**< the number of the last request to the card  */
    uint32_t card_want;      /**< the request whose answer counts; 0 none     */
    uint32_t card_stale;     /**< answers dropped as not that request's      */
    uint32_t card_ms;        /**< when the write asked for it                 */
    uint16_t pend_n;         /**< the write that waits for the card: values   */
    uint16_t pend[YGE_LINK_VALUES_MAX]; /**< its draft                        */
    uint16_t pend_reads;     /**< the read copy it was made from              */
    uint16_t pend_crc;       /**< and its CRC                                 */

    /* A file as read and what it does to the read copy. */
    oy_file_t loaded;        /**< the file                                    */
    oy_file_load_t load;     /**< the draft it gives                          */
    uint8_t  load_ok;        /**< load holds a draft                          */
} yge_prog_t;

/**
 * @brief Sets up the model.
 * @param p The model.
 */
void yge_prog_init(yge_prog_t *p);

/**
 * @brief The gate the link's task claims each command at; NULL for none.
 * @param p The model.
 * @param g The gate.
 */
void yge_prog_gate_set(yge_prog_t *p, yge_prog_gate_t *g);

/**
 * @brief The frame's feed: outcomes taken, and the flow's next command made.
 * @param p The model.
 * @param f The feed.
 */
void yge_prog_feed(yge_prog_t *p, const yge_prog_feed_t *f);

/**
 * @brief The command to carry out, once.
 * @param p   The model.
 * @param out Receives it.
 * @return true when there is one.
 */
bool yge_prog_next_cmd(yge_prog_t *p, yge_prog_cmd_t *out);

/**
 * @brief CONNECT, at the end of the operator's 2s hold.
 * @param p The model.
 * @param c The port and the power.
 * @return YGE_PROG_MSG_NONE when it goes, else why not.
 */
yge_prog_msg_t yge_prog_connect(yge_prog_t *p, const yge_prog_connect_t *c);

/**
 * @brief BY HAND: the ESC has its power.
 * @param p The model.
 * @return YGE_PROG_MSG_NONE when it goes, else why not.
 */
yge_prog_msg_t yge_prog_hand_done(yge_prog_t *p);

/**
 * @brief ENTER again on a pin held in OPEN, at the end of the operator's 2s
 *        hold: after a LEAVE, a stop or an error.
 * @param p The model.
 * @param c The values; its port is not used, as the pin stays held.
 * @return YGE_PROG_MSG_NONE when it goes, else why not.
 */
yge_prog_msg_t yge_prog_enter_again(yge_prog_t *p, const yge_prog_connect_t *c);

/**
 * @brief READ the ESC's table again.
 * @param p The model.
 * @return YGE_PROG_MSG_NONE when it goes, else why not.
 */
yge_prog_msg_t yge_prog_read(yge_prog_t *p);

/**
 * @brief WRITE a draft of the read copy, at the end of the operator's 2s hold.
 * @param p     The model.
 * @param draft The draft, index 0 first.
 * @param n     Its values, the read copy's entries.
 * @return YGE_PROG_MSG_NONE when it goes, else why not.
 */
yge_prog_msg_t yge_prog_write(yge_prog_t *p, const uint16_t *draft, uint16_t n);

/**
 * @brief LEAVE, then CLOSE; CLOSE alone when no ESC answers.
 * @param p The model.
 * @return YGE_PROG_MSG_NONE when it goes, else why not.
 */
yge_prog_msg_t yge_prog_leave(yge_prog_t *p);

/**
 * @brief CLEAR a stop or fault.
 * @param p The model.
 * @return YGE_PROG_MSG_NONE when it goes, else why not.
 */
yge_prog_msg_t yge_prog_clear(yge_prog_t *p);

/**
 * @brief CLOSE: the pin back, whoever opened it.
 * @param p The model.
 * @return YGE_PROG_MSG_NONE when it goes, else why not.
 */
yge_prog_msg_t yge_prog_close(yge_prog_t *p);

/**
 * @brief The operator's cut: ABORT, at once and past whatever is out.
 * @param p The model.
 */
void yge_prog_abort(yge_prog_t *p);

/**
 * @brief SUPPLY's upper limits for this board and setting: a connect above
 *        them goes nowhere.
 * @param p      The model.
 * @param max_mv The voltage, mV; 0 for only the module's.
 * @param max_ma The limit, mA; 0 for only the module's.
 */
void yge_prog_limits(yge_prog_t *p, uint16_t max_mv, uint16_t max_ma);

/**
 * @brief The last message, with its code.
 * @param p      The model.
 * @param detail Receives the code; may be NULL.
 * @return The message.
 */
yge_prog_msg_t yge_prog_message(const yge_prog_t *p, uint16_t *detail);

/**
 * @brief The index of the last YGE_PROG_MSG_GUARD.
 * @param p The model.
 * @return The index; 0 for none.
 */
uint16_t yge_prog_bad(const yge_prog_t *p);

/**
 * @brief Whether the page holds a pin, or is asked to: any writer's.  ARM, the
 *        runs that arm the bench and the band's SAFE chip wait for it.
 * @param p The model.
 * @return true then.
 */
bool yge_prog_holds_port(const yge_prog_t *p);

/**
 * @brief Whether a flow runs or a command is out.
 * @param p The model.
 * @return true then.
 */
bool yge_prog_busy(const yge_prog_t *p);

/**
 * @brief Whether a command may go with a stop latched at the panel: CLOSE and
 *        LEAVE give the pin back, and ABORT cuts; nothing else starts under a
 *        stop.
 * @param kind    A yge_prog_cmd_kind_t.
 * @param stopped STOP is latched.
 * @return true when it may go.
 */
bool yge_prog_cmd_allowed(uint8_t kind, bool stopped);

/**
 * @brief Whether the page is known free with nothing asked of it, or the
 *        IOmcu has none.  Fails closed: a page not read yet, a command out and
 *        a CLOSE under way all say no.
 * @param p The model.
 * @return true then.
 */
bool yge_prog_idle(const yge_prog_t *p);

/**
 * @brief Whether the readout says a pin is held: the link's task's word for
 *        ARM, the PROG page and the supply's error rule.
 * @param rd The readout.
 * @return true then.
 */
bool yge_prog_port_taken(const yge_link_readout_t *rd);

/**
 * @brief The programming port the readout says is held, for the band's SAFE
 *        chip.
 * @param rd The readout.
 * @return 0 to 3; -1 when none is held or the page names none.
 */
int yge_prog_taken_port(const yge_link_readout_t *rd);

/* --- the session's backup and files (yge_prog_file.c) -------------------- */

/**
 * @brief The session's backup, from the read copy: taken at the first whole
 *        read of a session, and called by yge_prog_feed().  A write that
 *        waits for the card past YGE_PROG_CARD_WAIT_MS, or under a stop, goes
 *        nowhere.
 * @param p The model.
 */
void yge_prog_backup_follow(yge_prog_t *p);

/**
 * @brief Where the session's backup stands.
 * @param p The model.
 * @return A yge_prog_backup_t.
 */
yge_prog_backup_t yge_prog_backup_where(const yge_prog_t *p);

/**
 * @brief The backup the card is asked for, once: its text in the format of
 *        openyge_file.h, and its tag, the type code, for the name
 *        `YGE001-5712`.
 * @param p        The model.
 * @param out      Takes the text, without a terminator.
 * @param size     Bytes at @p out; OY_FILE_MAX always fits.
 * @param len      Receives the bytes written.
 * @param tag      Takes the tag with its terminator.
 * @param tag_size Bytes at @p tag; 6 always fits.
 * @param id       Receives the request's number, never 0, which its answer
 *                 carries back to yge_prog_card_done().
 * @return true when the card is to save it; false when nothing is asked.
 */
bool yge_prog_card_take(yge_prog_t *p, char *out, size_t size, size_t *len,
                        char *tag, size_t tag_size, uint32_t *id);

/**
 * @brief The card's answer to yge_prog_card_take(): the write that waited
 *        goes.  With anything but YGE_PROG_CARD_OK the old values are kept in
 *        RAM only, and the message says so.  An answer whose @p id is not
 *        the request out, one of a session that has ended among them, is
 *        dropped and counted (yge_prog_card_stale()).
 * @param p      The model.
 * @param id     The number yge_prog_card_take() gave the request.
 * @param result A yge_prog_card_t.
 */
void yge_prog_card_done(yge_prog_t *p, uint32_t id, uint8_t result);

/**
 * @brief The card's answers dropped because no request out was theirs.
 * @param p The model.
 * @return The count, modulo 2^32.
 */
uint32_t yge_prog_card_stale(const yge_prog_t *p);

/**
 * @brief The text of the session's backup, for a save by hand.
 * @param p    The model.
 * @param out  Takes the text, without a terminator.
 * @param size Bytes at @p out; OY_FILE_MAX always fits.
 * @return Bytes written; 0 when no backup is held.
 */
size_t yge_prog_backup_text(const yge_prog_t *p, char *out, size_t size);

/**
 * @brief A file against the read copy: what a load changes.
 * @param p    The model.
 * @param text The file.
 * @param len  Its bytes.
 * @param line Receives the line of a refusal; may be NULL.
 * @return YGE_PROG_MSG_LOADED, YGE_PROG_MSG_FILE_REFUSED,
 *         YGE_PROG_MSG_NOT_READ or YGE_PROG_MSG_LOAD_REFUSED, with the code
 *         in the message's detail.
 */
yge_prog_msg_t yge_prog_file_load(yge_prog_t *p, const char *text, size_t len,
                                  unsigned *line);

/**
 * @brief The draft of the last yge_prog_file_load() that was LOADED.
 * @param p The model.
 * @param n Receives its values; may be NULL.
 * @return The draft, NULL for none.
 */
const uint16_t *yge_prog_file_draft(const yge_prog_t *p, uint16_t *n);

/**
 * @brief The guard of protocols/openyge as the read copy sets it: the table,
 *        its read number and CRC as the page has them.
 * @param p The model.
 * @param g Takes it.
 * @return false with no read copy of this session.
 */
bool yge_prog_guard_of(const yge_prog_t *p, oy_guard_t *g);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_YGE_PROG_H */
