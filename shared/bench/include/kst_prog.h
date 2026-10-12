/*
 * The programmer for KST and Chaservo servos as the SERVO screen runs it:
 * what is read from a servo, what the operator has changed, and what may
 * be written.
 *
 * The session with the servo runs at the coprocessor and the panel's link
 * module (kst_link.h) carries its operations.  This file sits between that
 * module and the screen:
 *
 *   - It holds the servo's image as last read, a draft the operator edits,
 *     the backup the coprocessor's session holds, and the journal of a
 *     write.
 *   - An edit goes through the field table and the limits of protocols/kst:
 *     a value that breaks a hard rule is not taken, a value that breaks a
 *     soft rule waits for a confirmation.  No limit is stated here.  The
 *     soft rules compare with the servo as the session found it
 *     (kst_prog_soft_base()), and a rule confirmed for a value is asked
 *     again for a value further from that image.
 *   - A write is sent with the switch ENABLE WRITE TO SERVO on only.  The
 *     switch goes off at every entry of the screen, when the link goes, when
 *     the coprocessor starts again, at STOP, and with every servo
 *     connected.
 *   - Every answer of kst_link_start(), every outcome kind, every refusal
 *     of the port and every result of the session becomes one message
 *     (kst_prog_msg_id_t) the screen has a text for.
 *
 * The journal: before a write the image the servo holds and the port are
 * recorded, in memory and in the file text (kst_prog_file_text()).  A write that
 * ends with the servo verified clears it.  A journal that stands after a
 * link loss, a restart or an abort says the servo may hold part of a
 * write; the next read compares the servo with the recorded image.
 *
 * Servo files: the card keeps images of servos in named files.  This file
 * asks for a save, a list and a load (kst_prog_card_take()) and takes what
 * the card's side made of it (kst_prog_card_done()), as images and names:
 * the text of a file, its name's rules and the servo type's rule are the
 * card side's (kst_card.h).
 *   - A save writes the servo's image as last read, under the name the
 *     operator gave.  The first write of a session to a servo waits for a
 *     save nobody asked for, under a preset name, and is not sent when the
 *     card takes no file.
 *   - A load takes a file of the servo's type only (kst_card.h says what
 *     the types are).  Its editable fields become the draft, as edits: a
 *     hard rule refuses the file, soft rules wait for a confirmation, and
 *     nothing is written before the operator writes the draft.  A field of
 *     another access class that differs stays as the servo holds it and is
 *     named (kst_prog_load_skipped()).  A file edited by hand is taken
 *     the same way and named as one.
 *
 * The journal's file text, at most KST_PROG_FILE_MAX bytes:
 *
 *   RCBENCH KST 1
 *   journal=<64 hexadecimal digits>     the image before a write, when
 *                                       one is recorded
 *   target=<64 hexadecimal digits>      what that write was to leave
 *   port=<1 hexadecimal digit>
 *   check=<4 hexadecimal digits>        CRC-16/CCITT of the bytes before
 *
 * Pure C.  The caller hands in kst_prog_feed_t every frame and carries out
 * the commands kst_prog_take() gives.  One task only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kst_limits.h"
#include "kst_link.h"
#include "kst_plan.h"
#include "kst_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The supply an ENTER asks for until it is set otherwise. */
#define KST_PROG_SUPPLY_MV 5000u
#define KST_PROG_SUPPLY_MA 1000u
/** The wait between the end of a session and the first PWM pulse: its
 *  default and the highest value taken. */
#define KST_PROG_WAIT_MS     100u
#define KST_PROG_WAIT_MAX_MS 10000u
/** The file text's size, with its terminator. */
#define KST_PROG_FILE_MAX 400u
/** A command without an answer from the control task for this long is
 *  given up. */
#define KST_PROG_START_MS 2000u
/** An operation without an outcome for this long is aborted.  The time
 *  counts from the last feed in which the port reported it busy. */
#define KST_PROG_RUN_MS 30000u
/** And one the port reports busy for this long from its start is aborted
 *  too.  A chosen value: the longest wait of the port is the 10000 ms of a
 *  LEAVE, and the duration of the longest write is not measured. */
#define KST_PROG_RUN_MAX_MS 120000u
/** The file not written for this long: the write to the servo is not
 *  sent. */
#define KST_PROG_STORE_MS 3000u
/** The time between two fetches of images the port holds. */
#define KST_PROG_FETCH_MS 1000u
/** A file the card did not take is written again after this long. */
#define KST_PROG_FILE_RETRY_MS 5000u
/** A save, a list or a load without an end for this long has failed. */
#define KST_PROG_CARD_MS 5000u
/** The files a list holds. */
#define KST_PROG_FILES_MAX 5u
/** A file's name with its terminator. */
#define KST_PROG_NAME_MAX 33u

/** Where the programmer is. */
typedef enum {
    KST_PROG_DOWN = 0,     /**< no coprocessor answers                    */
    KST_PROG_NO_PAGE,      /**< the coprocessor has no such page         */
    KST_PROG_SYNC,         /**< the page has not been read yet            */
    KST_PROG_PWM,          /**< the tester has the output                 */
    KST_PROG_ENTERING,     /**< the output leaves PWM                     */
    KST_PROG_PROGRAMMING,  /**< the port holds the pin, nothing runs      */
    KST_PROG_WORKING,      /**< an operation runs                         */
    KST_PROG_LEAVING,      /**< the servo rail is switched               */
} kst_prog_state_t;

/**
 * What the screen says.  The four ranges at the end take a code of the
 * link module or the core: KST_PROG_MSG_START + kst_link_start_t,
 * KST_PROG_MSG_OUT + kst_link_outcome_kind_t, KST_PROG_MSG_REF +
 * link_kst_refusal_t and KST_PROG_MSG_SES + the result byte of the
 * outcome (a kst_ses_t or a LINK_KST_RESULT_*).  A message is a
 * number and not an enum for that reason: most of its values lie between
 * the names.
 */
typedef unsigned kst_prog_msg_id_t;

enum {
    KST_PROG_MSG_NONE = 0,
    KST_PROG_MSG_READ,            /**< all registers read                   */
    KST_PROG_MSG_READ_ONLY,       /**< read; the fingerprint fails          */
    KST_PROG_MSG_WRITTEN,         /**< arg values written and verified      */
    KST_PROG_MSG_VERIFIED,        /**< the servo holds what was read        */
    KST_PROG_MSG_VERIFY_DIFF,     /**< arg registers differ                 */
    KST_PROG_MSG_RESTORED,
    KST_PROG_MSG_RELEASED,
    KST_PROG_MSG_LEFT,            /**< the tester has the output again      */
    KST_PROG_MSG_SAVED,           /**< the servo's image is in the file
                                         kst_prog_card_name()                 */
    KST_PROG_MSG_CARD_NONE,       /**< no card: no file, nothing written    */
    KST_PROG_MSG_STORE_FAILED,    /**< the card took no journal; nothing was
                                       written to the servo                 */
    KST_PROG_MSG_FILE_BAD,        /**< the journal's file is not readable   */
    KST_PROG_MSG_JOURNAL,         /**< a write was interrupted; read.  arg:
                                       the journal's port, from 1           */
    KST_PROG_MSG_JOURNAL_SAME,    /**< the servo holds what the record says
                                       it held before the write; the record
                                       stands and asks whether this is the
                                       servo.  arg: the record's port, from
                                       1                                    */
    KST_PROG_MSG_JOURNAL_DONE,    /**< the same for the write's target      */
    KST_PROG_MSG_JOURNAL_DIFF,    /**< the servo holds something else       */
    KST_PROG_MSG_FORGOTTEN,       /**< the journal was discarded            */
    KST_PROG_MSG_EDIT_HARD,       /**< arg: the kst_rule_t that refuses     */
    KST_PROG_MSG_EDIT_LOCKED,     /**< the field is not editable            */
    KST_PROG_MSG_NO_CHANGES,
    KST_PROG_MSG_NEED_ENABLE,     /**< ENABLE WRITE TO SERVO is off         */
    KST_PROG_MSG_NEED_READ,       /**< read the servo first                 */
    KST_PROG_MSG_NEED_PORT,       /**< no servo is connected                */
    KST_PROG_MSG_NO_RESTORE,      /**< the servo holds that image already   */
    KST_PROG_MSG_RESTORE_REFUSED, /**< arg: the kst_rule_t that refuses     */
    KST_PROG_MSG_STOPPED,         /**< STOP: the operation was aborted      */
    KST_PROG_MSG_TIMEOUT,         /**< no outcome within KST_PROG_RUN_MS    */
    KST_PROG_MSG_JOURNAL_OTHER,   /**< the servo read is not the journal's;
                                       arg: the journal's port, from 1      */
    KST_PROG_MSG_CARD_FULL,      /**< no space on the card, or no preset
                                         name is left                         */
    KST_PROG_MSG_CARD_WRITE,      /**< the file was not written whole       */
    KST_PROG_MSG_CARD_READ,       /**< the card was not read                */
    KST_PROG_MSG_NO_FILES,        /**< the card holds no servo file         */
    KST_PROG_MSG_NAME_BAD,        /**< the card's side does not take the
                                         name                                 */
    KST_PROG_MSG_NAME_EXISTS,     /**< a file has the name; none is replaced */
    KST_PROG_MSG_LOAD_BAD,        /**< the file is not whole; arg: its line,
                                         0 none                               */
    KST_PROG_MSG_LOAD_CHECKSUM,   /**< the check value does not fit the text */
    KST_PROG_MSG_LOAD_NEWER,      /**< the file's format is a later one     */
    KST_PROG_MSG_LOAD_TYPE,       /**< the file is of another servo type;
                                         arg: the file's type, and above bit 7
                                         the servo's                          */
    KST_PROG_MSG_LOAD_VALUE,      /**< a value of the file is not taken;
                                         reg: its kst_field_id_t, 0xFF none;
                                         arg: its line, 0 none                */
    KST_PROG_MSG_LOAD_REFUSED,    /**< the file breaks a hard rule; arg: the
                                         kst_rule_t                           */
    KST_PROG_MSG_LOAD_SAME,       /**< the file changes nothing; arg: locked
                                         fields that differ and stay          */
    KST_PROG_MSG_LOAD_SOFT,       /**< the file waits for
                                         kst_prog_soft_confirm()              */
    KST_PROG_MSG_LOADED,          /**< the draft holds the file; arg: fields
                                         it changes, and above bit 7 the
                                         locked fields that differ and stay   */
    KST_PROG_MSG_LOADED_EDITED,   /**< the same for a file edited by hand   */
    KST_PROG_MSG_LOADED_UNTYPED,  /**< and for one that names no servo type */
    KST_PROG_MSG_NEED_TARGET,     /**< the SERVO screen's target is not the
                                       programming port                     */
    KST_PROG_MSG_LEAVE_SUPPLY,    /**< a PD mini answers and the coprocessor
                                       is below link 4.15: the port has no
                                       way back to PWM that leaves the
                                       supply on                            */
    KST_PROG_MSG_LEFT_NO_WAIT,    /**< the port is back in PWM by a
                                       coprocessor below link 4.15: no wait
                                       was applied                          */
    KST_PROG_MSG_LEFT_DOUBT,      /**< the port is back in PWM from a
                                       session in doubt, no PD mini
                                       answering: no wait was applied       */
    KST_PROG_MSG_LEAVE_CUT,       /**< the wait of a LEAVE was cut: an
                                       abort, a disarm or the supply off    */
    KST_PROG_MSG_NEED_DISARM,     /**< the bench is armed or asked to arm:
                                       the port is not taken and no frame
                                       is sent                              */
    KST_PROG_MSG_BACKUP_OTHER,    /**< the servo read is not the one the
                                       session's backup was read from: the
                                       backup is not restored to it         */
    KST_PROG_MSG_SESSION_ENDED,   /**< the port is in PWM and no command of
                                       this model put it there: the supply
                                       went off or the coprocessor started
                                       again.  The session's values are
                                       gone                                 */
    KST_PROG_MSG_UNKNOWN,         /**< a result, a refusal or an outcome the
                                       tables do not hold; arg: the byte    */
    KST_PROG_MSG_SUPPLY_CAPS,     /**< the programming voltage or current is
                                       above the supply's limits: the port
                                       is not taken                         */
    KST_PROG_MSG_CARD_DENIED,     /**< the card takes no file: it is write
                                       protected, or a file has the name of
                                       the directory                        */
    KST_PROG_MSG_START = 0x40,
    KST_PROG_MSG_OUT   = 0x50,
    KST_PROG_MSG_REF   = 0x60,
    KST_PROG_MSG_SES   = 0x80,
    KST_PROG_MSG_END   = 0xB0
};

typedef enum {
    KST_PROG_INFO = 0,
    KST_PROG_WARN,
    KST_PROG_DANGER,
} kst_prog_level_t;

typedef struct {
    uint8_t id;        /**< kst_prog_msg_id_t                       */
    uint8_t level;     /**< kst_prog_level_t                        */
    uint8_t reg;       /**< the register a result names; 0xFF none  */
    uint16_t arg;
    uint32_t seq;      /**< counts the messages                     */
} kst_prog_msg_t;

/** What an edit came to. */
typedef enum {
    KST_PROG_EDIT_OK = 0,   /**< the draft holds the value                 */
    KST_PROG_EDIT_SOFT,     /**< it waits for kst_prog_soft_confirm()      */
    KST_PROG_EDIT_REFUSED,  /**< not taken; kst_prog_message() says why    */
} kst_prog_edit_t;

/** Where a restore takes its image from. */
typedef enum {
    KST_PROG_FROM_NONE = 0,
    KST_PROG_FROM_SESSION,  /**< the backup of the coprocessor's session */
    KST_PROG_FROM_JOURNAL,  /**< the image recorded before a write       */
    KST_PROG_FROM_COUNT
} kst_prog_from_t;

/** A restore as kst_prog_restore_plan() prepared it. */
typedef struct {
    uint8_t from;       /**< kst_prog_from_t                               */
    uint8_t writes;     /**< fields, or for the session registers, that
                             change                                        */
    uint8_t stay;       /**< fields that differ and are not written        */
    kst_rules_t soft;   /**< soft limits the restored image passes as a
                             step from the servo as last read; 0 where
                             the servo's image is not known               */
    bool    unchecked;  /**< the session's restore has no checked order    */
} kst_prog_restore_t;

/** What the card's side is asked for. */
typedef enum {
    KST_PROG_CARD_IDLE = 0,
    KST_PROG_CARD_SAVE,   /**< the image into a new file                   */
    KST_PROG_CARD_LIST,   /**< the names of the files and the next preset  */
    KST_PROG_CARD_LOAD,   /**< the file with the name, against the servo   */
} kst_prog_card_op_t;

/** How it went. */
typedef enum {
    KST_PROG_CARD_OK = 0,
    KST_PROG_CARD_NO_CARD,
    KST_PROG_CARD_FULL,       /**< no space, or no preset name is left     */
    KST_PROG_CARD_WRITE_ERR,
    KST_PROG_CARD_READ_ERR,   /**< the directory or the file does not read */
    KST_PROG_CARD_NAME,       /**< the name is not one a file may have     */
    KST_PROG_CARD_EXISTS,     /**< a file has the name; nothing written    */
    KST_PROG_CARD_BAD,        /**< the text is not a whole servo file      */
    KST_PROG_CARD_CHECKSUM,   /**< its check value does not fit            */
    KST_PROG_CARD_NEWER,      /**< a format this code does not read        */
    KST_PROG_CARD_TYPE,       /**< the file is from another servo type     */
    KST_PROG_CARD_VALUE,      /**< a value of the file is not taken        */
    KST_PROG_CARD_LIMIT,      /**< the file breaks a hard rule no single
                                   line of it is at fault for              */
    KST_PROG_CARD_DENIED,     /**< the card takes no file: it is write
                                   protected, or a file has the name of
                                   the directory                           */
    KST_PROG_CARD_RESULTS
} kst_prog_card_result_t;

/** What a file that loads is. */
typedef enum {
    KST_PROG_FILE_VERIFIED = 0,  /**< as written, its check value fits  */
    KST_PROG_FILE_EDITED,        /**< no check value; names its type    */
    KST_PROG_FILE_UNTYPED,       /**< no check value and no servo type  */
    KST_PROG_FILE_KINDS
} kst_prog_file_kind_t;

typedef struct {
    uint8_t op;          /**< kst_prog_card_op_t                           */
    bool preset;         /**< KST_PROG_CARD_SAVE: the card's side names it */
    uint32_t id;         /**< goes back in the answer                      */
    char name[KST_PROG_NAME_MAX];  /**< the file to save or to load        */
    uint16_t first;      /**< KST_PROG_CARD_LIST: the first file wanted,
                              counted from 0                               */
    kst_image_t image;   /**< the servo's image: what a save writes, what
                              a load is compared with                      */
} kst_prog_card_req_t;

typedef struct {
    uint32_t id;
    uint8_t result;      /**< kst_prog_card_result_t                       */
    char name[KST_PROG_NAME_MAX];  /**< KST_PROG_CARD_SAVE: the file
                              written; KST_PROG_CARD_LIST: the next preset
                              name, "" when none is left                   */
    /* KST_PROG_CARD_LOAD */
    uint8_t kind;        /**< kst_prog_file_kind_t                         */
    uint8_t type;        /**< the file's servo type, a number the card's
                              side gives and the screen names; 0 none      */
    uint8_t servo_type;  /**< KST_PROG_CARD_TYPE: the servo's              */
    kst_field_set_t skipped;  /**< fields that differ from the file and
                              are never written                            */
    uint8_t named;       /**< fields the file gives a value for            */
    uint8_t fields;      /**< named fields a file has; 0: not counted      */
    uint8_t field;       /**< KST_PROG_CARD_VALUE: the kst_field_id_t,
                              0xFF none                                    */
    uint16_t line;       /**< a refusal's line of the text; 0 none         */
    uint8_t rule;        /**< KST_PROG_CARD_LIMIT: the kst_rule_t           */
    kst_image_t image;   /**< the servo's image with the editable fields
                              of the file                                  */
    /* KST_PROG_CARD_LIST */
    uint8_t count;       /**< entries of names[]                           */
    uint16_t total;      /**< the servo files on the card                  */
    uint8_t types[KST_PROG_FILES_MAX];  /**< each file's servo type; 0 for
                              a file that names none or does not read      */
    char names[KST_PROG_FILES_MAX][KST_PROG_NAME_MAX];
} kst_prog_card_done_t;

typedef enum {
    KST_PROG_CMD_START = 0,   /**< kst_link_start(req)  */
    KST_PROG_CMD_ABORT,       /**< kst_link_abort()     */
    KST_PROG_CMD_FETCH,       /**< kst_link_fetch(): the port holds images
                                   this panel does not, after a start of
                                   the panel on a held port */
} kst_prog_cmd_kind_t;

/** One call for the task that owns the link module. */
typedef struct {
    kst_prog_cmd_kind_t kind;
    uint32_t id;               /**< a start's comes back in
                                    kst_prog_feed_t.answer_id */
    kst_link_request_t req;
} kst_prog_cmd_t;

/**
 * The pass a START needs to be carried out.  The model's task and the task
 * that owns the link module share one of these.  kst_prog_take() offers the
 * pass of each START it hands out; the link's task claims it when the
 * command comes out of its queue and carries the command out only with the
 * pass; the model revokes the pass of a START nobody answered.  Each pass
 * is taken once, by the one or by the other: a START whose pass the model
 * revoked is never carried out, however late it leaves the queue.
 *
 * The three calls are safe from two tasks without a lock: each is one
 * atomic exchange of a 32-bit word.
 */
typedef struct {
    uint32_t pass;             /**< the id of the START offered; 0 none */
} kst_prog_gate_t;

void kst_prog_gate_init(kst_prog_gate_t *g);
/** The pass of START @p id is there to be taken. */
void kst_prog_gate_offer(kst_prog_gate_t *g, uint32_t id);
/** The link's task, with a START out of its queue: true when the command
 *  is to be carried out.  False: it is dropped, with no answer. */
bool kst_prog_gate_claim(kst_prog_gate_t *g, uint32_t id);
/** The model: true when START @p id is not carried out and never is. */
bool kst_prog_gate_revoke(kst_prog_gate_t *g, uint32_t id);

/** What a result of the session says of a write that ended with it. */
typedef enum {
    KST_PROG_RES_OK = 0,   /**< the write ended as asked                    */
    KST_PROG_RES_CLEAN,    /**< with no step counted done the servo holds
                                the image the write started from: no frame
                                went, or every write was undone and read
                                back                                        */
    KST_PROG_RES_DOUBT,    /**< the servo may hold part of the write        */
} kst_prog_res_t;

/** The class of @p result, a kst_ses_t or a LINK_KST_RESULT_ value.  A
 *  value that is neither is KST_PROG_RES_DOUBT. */
kst_prog_res_t kst_prog_result_class(unsigned result);

/** What the task that owns the link module publishes, every frame. */
typedef struct {
    uint32_t now_ms;
    kst_link_readout_t rd;
    bool have_image;
    kst_image_t image;
    bool have_backup;
    kst_image_t backup;
    uint32_t answer_id;        /**< the last command carried out; 0 none */
    kst_link_start_t answer;   /**< what kst_link_start() said */
    uint32_t outcomes;         /**< counts kst_link_outcome() handing one out */
    kst_link_outcome_t outcome;
    bool     supply_read;      /**< out_mv and out_ma are a reading of the
                                    PD mini                               */
    uint16_t out_mv;           /**< its output, mV                        */
    uint16_t out_ma;           /**< and the current it gives, mA          */
    uint16_t set_mv;           /**< and the voltage it is set to, mV      */
    bool stopped;              /**< STOP is latched: no heartbeat */
    bool armed;                /**< the panel's outputs are armed */
    bool card;                 /**< a card takes the file */
    uint8_t target;            /**< what the SERVO screen's tester drives
                                    as the coprocessor reads it: 0 the
                                    standard servo ports, 1 + n
                                    programming port n */
} kst_prog_feed_t;

typedef struct {
    kst_prog_feed_t f;
    bool fed;
    uint32_t outcomes_seen;
    uint32_t next_id;

    bool have_servo;
    bool fp_ok;
    kst_image_t servo;        /**< the servo as last read          */
    kst_image_t draft;        /**< what the operator made of it    */
    bool journal;             /**< an image is recorded            */
    bool journal_open;        /**< and the servo was not compared  */
    bool journal_other;       /**< the servo read is not the journal's */
    bool need_read;           /**< the servo's image is in doubt   */
    uint8_t journal_port;
    kst_image_t journal_img;     /**< the servo before the write   */
    kst_image_t journal_target;  /**< and what the write was to leave */

    bool write_enabled;
    bool written;             /**< a write was sent in this session */
    bool backed_up;           /**< a file holds this session's servo */
    bool no_file;             /**< a write went with no card for one */
    bool held_no_file;        /**< and the held write is such a one  */
    uint8_t port;
    uint16_t supply_mv;       /**< what an ENTER asks of the PD mini */
    uint16_t supply_ma;
    uint16_t caps_mv;         /**< the supply's limits, 0 for none   */
    uint16_t caps_ma;
    uint16_t floor_mv;        /**< the lowest voltage of a session; 0
                                   for none                          */
    uint16_t pwm_wait_ms;     /**< from the end of a session to PWM */

    uint8_t wait;             /**< what is awaited                */
    uint8_t op;               /**< link_kst_op_t on its way       */
    uint8_t writes;           /**< fields the write on its way changes */
    uint8_t restoring;        /**< kst_prog_from_t of that write  */
    bool chain_read;          /**< READ_ALL follows the ENTER     */
    bool armed;               /**< kst_prog_bench_armed()         */
    kst_field_set_t wrote;    /**< fields the session's writes set
                                   out to change: of the writes
                                   whose command was taken          */
    kst_field_set_t held_wrote;  /**< those of the write that is
                                      held, not taken yet           */
    kst_field_set_t wrote_before; /**< wrote before the last write
                                       was taken: what stands when
                                       that write ends with no
                                       register written              */
    bool wrote_known;         /**< the session began under this
                                   model: wrote is all of them      */
    bool leave_doubt;         /**< the exit runs without LEAVE: the
                                   port asks for a read or holds a
                                   servo in doubt, no PD mini answers */
    uint32_t wait_ms;
    uint32_t run_ms;          /**< when the wait for the outcome began */
    uint32_t fetch_ms;
    bool fetched;
    bool cmd_fetch;
    uint32_t wait_id;
    bool cmd_ready;
    kst_prog_cmd_t cmd;
    kst_link_request_t held;  /**< the request the file goes before */
    kst_image_t held_target;  /**< and what it is to leave */
    bool held_journal;        /**< the held write recorded the journal */
    bool held_over;           /**< over the record of an earlier write,
                                   whose target is in held_before      */
    kst_image_t held_before;
    kst_prog_gate_t *gate;    /**< kst_prog_gate_set()             */
    bool cmd_out;             /**< the START awaited was handed out */
    bool cmd_run;             /**< and its pass was taken: it is carried
                                   out or was                       */
    bool keep;                /**< keep_draft holds the pending values
                                   of before a restore that is not
                                   started yet                      */
    bool keep_soft;
    bool keep_soft_load;
    kst_image_t keep_draft;

    uint8_t card_op;          /**< kst_prog_card_op_t on its way   */
    bool card_ready;          /**< and not taken yet               */
    bool card_auto;           /**< the save a write waits for      */
    bool soft_load;           /**< the edit that waits is a file   */
    kst_field_set_t load_skipped;
    kst_field_set_t soft_skipped;   /**< of the file that waits */
    uint8_t file_types[KST_PROG_FILES_MAX];
    uint8_t load_kind;        /**< kst_prog_file_kind_t            */
    uint8_t load_named;       /**< fields the file gives a value for */
    uint8_t load_fields;      /**< of this many; 0 not counted     */
    char card_name[KST_PROG_NAME_MAX];  /**< the file saved or loaded */
    char preset[KST_PROG_NAME_MAX];     /**< the next preset name     */
    char backup[KST_PROG_NAME_MAX];     /**< the file of backed_up    */
    uint32_t card_id;
    uint32_t card_ms;
    uint32_t late_id;         /**< the save given up at KST_PROG_CARD_MS;
                                   0 for none                       */
    kst_image_t late_img;     /**< and the image it was asked with */
    uint8_t files_n;
    uint16_t files_total;
    uint16_t files_first;     /**< of the last list                */
    uint16_t card_first;      /**< of the list asked for           */
    uint32_t files_seq;       /**< counts the lists that arrived   */
    char files[KST_PROG_FILES_MAX][KST_PROG_NAME_MAX];

    bool soft;
    kst_image_t soft_img;
    kst_rules_t soft_rules;
    kst_field_set_t conf_set;       /**< fields with a confirmed value   */
    uint16_t conf_raw[KST_F_COUNT]; /**< the value a soft rule was
                                         confirmed for                   */
    uint8_t plan_n;           /**< registers the write on its way writes;
                                   0 not known                          */

    bool file_wanted;
    bool file_busy;
    bool file_stale;          /**< the text on its way is of before the
                                   write that waits: its answer is not
                                   that write's                     */
    bool file_retry;          /**< the card owes the file          */
    uint32_t file_retry_ms;
    uint8_t file_next;        /**< what the write wanted is for    */
    uint8_t file_for;         /**< what the write on its way is for */

    kst_prog_msg_t msg;
    kst_plan_t plan;          /**< work space; not on the stack */
} kst_prog_t;

void kst_prog_init(kst_prog_t *p);

/**
 * The gate the link's task claims each START at.  With one set, a START
 * nobody answers within KST_PROG_START_MS is revoked and was never carried
 * out.  With none set, such a START counts as one that may have run: the
 * servo is read before anything else.  kst_prog_init() sets none.
 */
void kst_prog_gate_set(kst_prog_t *p, kst_prog_gate_t *g);

/** The published state of this frame.  Outcomes and answers are taken
 *  here. */
void kst_prog_feed(kst_prog_t *p, const kst_prog_feed_t *f);

/** The next command for the task that owns the link module; false for
 *  none. */
bool kst_prog_take(kst_prog_t *p, kst_prog_cmd_t *out);

/** The screen was entered: ENABLE WRITE TO SERVO goes off. */
void kst_prog_screen_entry(kst_prog_t *p);

kst_prog_state_t kst_prog_state(const kst_prog_t *p);
/** The operation on its way or running; LINK_KST_OP_NONE for none. */
link_kst_op_t kst_prog_op(const kst_prog_t *p);
/** A write, a restore or a release is on its way or runs under this
 *  model: the record of it is no interrupted write yet. */
bool kst_prog_writing(const kst_prog_t *p);
/** The registers that write writes, as this model planned it; 0 when no
 *  write runs or the coprocessor alone plans it (a restore with no checked
 *  order, a release).  The register under way is not on the link page. */
unsigned kst_prog_write_steps(const kst_prog_t *p);
/** The coprocessor plans the order of a write's or a restore's writes: no
 *  frame goes to the servo yet. */
bool kst_prog_planning(const kst_prog_t *p);
/** The last message. */
const kst_prog_msg_t *kst_prog_message(const kst_prog_t *p);

/* --- what is held --------------------------------------------------------- */

bool kst_prog_have_servo(const kst_prog_t *p);
/** A servo was read, its image passes the fingerprint and is not in
 *  doubt: it may be edited and written. */
bool kst_prog_editable(const kst_prog_t *p);
const kst_image_t *kst_prog_servo(const kst_prog_t *p);
const kst_image_t *kst_prog_draft(const kst_prog_t *p);
bool kst_prog_write_enabled(const kst_prog_t *p);
uint8_t kst_prog_port(const kst_prog_t *p);
/** A write was interrupted and the servo was not compared since. */
bool kst_prog_journal(const kst_prog_t *p);
/** A record of an interrupted write is held, read back or not: the one
 *  RESTORE offers the values of. */
bool kst_prog_journal_held(const kst_prog_t *p);
/**
 * The servo read cannot be the one of the interrupted write: it is on
 * another port, or a register of it holds neither the record's value nor
 * the write's target.  The low register of a field in two registers that
 * the write changes is exempt: a plan may set it to a third value.
 * Nothing is restored to it and nothing is written to it until the journal
 * is discarded.
 */
bool kst_prog_journal_other(const kst_prog_t *p);
/**
 * The servo read holds, register for register, the record's image
 * (KST_PROG_MSG_JOURNAL_SAME) or the write's target
 * (KST_PROG_MSG_JOURNAL_DONE), and the record stands: a second servo with
 * the same settings is not told apart from the one of the write.  Nothing
 * is written to the servo until kst_prog_forget() discards the record.
 * KST_PROG_MSG_NONE otherwise.
 */
kst_prog_msg_id_t kst_prog_journal_asks(const kst_prog_t *p);
/** The servo read is not the one the session's backup was read from: the
 *  two differ in a field no write of the session set out to change.  The
 *  backup is not offered and not restored until a new session reads its
 *  own.  Two servos that agree in every such field are not told apart.
 *  Never true for a session this model did not see begin, after a link
 *  loss or a start of the panel under a session: its writes are not
 *  known. */
bool kst_prog_backup_other(const kst_prog_t *p);
/** The port of the interrupted write, from 0. */
uint8_t kst_prog_journal_port(const kst_prog_t *p);
/** The servo may hold part of a write: a journal stands, the port asks for
 *  a read, an operation timed out, or the session is locked. */
bool kst_prog_in_doubt(const kst_prog_t *p);
/** No write was sent since the servo was connected: the next asks for the
 *  free horn. */
bool kst_prog_first_write(const kst_prog_t *p);
/** A file holds the servo as this session found it: the next write waits
 *  for no save. */
bool kst_prog_backed_up(const kst_prog_t *p);
/** That file's name; "" while there is none. */
const char *kst_prog_backup_name(const kst_prog_t *p);
/** A write of this session started with no card in the slot, and no file
 *  holds the servo as the session found it: the session's backup is the
 *  one copy.  Ends with the next file a write saves. */
bool kst_prog_no_file(const kst_prog_t *p);
/** The next write saves the servo as the session found it to a file
 *  first.  A mounted card that does not take the file ends the write.
 *  False while a record of an interrupted write is held: the servo may be
 *  half written, and the record holds it as found. */
bool kst_prog_save_first(const kst_prog_t *p);
/** That write goes without the file: no card is mounted, or the servo may
 *  hold other values than the session found and the session's backup, the
 *  image the file takes then, is not fetched. */
bool kst_prog_save_none(const kst_prog_t *p);

/** The fields in which the draft differs from the servo, register 01's
 *  copy of Max. Duty left out. */
kst_field_set_t kst_prog_changed(const kst_prog_t *p);
unsigned kst_prog_changes(const kst_prog_t *p);
/** The rules the draft breaks as an edit of the servo's image. */
kst_rules_t kst_prog_rules(const kst_prog_t *p);

/* --- editing ---------------------------------------------------------------- */

/**
 * The raw values of @p id the hard rules accept in the draft as it is, as
 * the lowest and the highest.  False when none is accepted, the field is
 * not editable or no servo was read.
 */
bool kst_prog_range(const kst_prog_t *p, kst_field_id_t id, uint16_t *lo,
                    uint16_t *hi);

/** Set a field of the draft to @p raw. */
kst_prog_edit_t kst_prog_edit(kst_prog_t *p, kst_field_id_t id, uint16_t raw);
/** One raw count up (@p dir above 0) or down. */
kst_prog_edit_t kst_prog_step(kst_prog_t *p, kst_field_id_t id, int dir);
/** The soft rules the edit that waits asks a confirmation for; 0 while
 *  none waits.  They are the rules it breaks and the draft does not, and
 *  the rules of a field it moves further from kst_prog_soft_base() than
 *  the value last confirmed for that field, or to its other side.  A value
 *  the servo holds counts as confirmed. */
kst_rules_t kst_prog_soft_rules(const kst_prog_t *p);
/** The image the soft rules compare with: the backup of the coprocessor's
 *  session, the servo as the session first read it.  The servo as last
 *  read while that backup is not held or is another servo's
 *  (kst_prog_backup_other()). */
const kst_image_t *kst_prog_soft_base(const kst_prog_t *p);
/** The fields of @p img that differ from the servo and are at fault for a
 *  soft rule: the rule goes when the field takes the value of
 *  kst_prog_soft_base(). */
kst_field_set_t kst_prog_soft_fields(const kst_prog_t *p,
                                     const kst_image_t *img);
/** The image the edit that waits would leave. */
const kst_image_t *kst_prog_soft_image(const kst_prog_t *p);
void kst_prog_soft_confirm(kst_prog_t *p);
void kst_prog_soft_cancel(kst_prog_t *p);
/** The draft becomes the servo's image again, and no value counts as
 *  confirmed. */
void kst_prog_discard(kst_prog_t *p);

/* --- operations: KST_PROG_MSG_NONE when taken, else the message set ------- */

/** Take programming port @p port, from 0, with the supply set by
 *  kst_prog_supply_set().  KST_PROG_MSG_NEED_TARGET while the feed's target
 *  is not that port, KST_PROG_MSG_NEED_DISARM on an armed bench. */
kst_prog_msg_id_t kst_prog_connect(kst_prog_t *p, uint8_t port);
/** The bench is armed or asked to arm.  Programming and an armed bench are
 *  exclusive: while this is set or the feed reports armed outputs no port
 *  is taken and no operation that sends frames starts
 *  (KST_PROG_MSG_NEED_DISARM), and the caller takes no arm while a port is
 *  held. */
void kst_prog_bench_armed(kst_prog_t *p, bool armed);
/** Whether the bench takes no arm by what the link module reads: an
 *  operation is on its way or runs, the port is read and is not in PWM, or
 *  the page answers and is not read yet (KST_LINK_SYNC).  False where no
 *  page answers: nothing can hold a port there. */
bool kst_prog_port_taken(const kst_link_readout_t *rd);
/** Whether nothing but the programmer reaches the port: an operation of
 *  this model is on its way or a session is open (ENTERING, PROGRAMMING,
 *  WORKING, LEAVING), or kst_prog_port_taken() says so of the readout last
 *  fed.  The tester's target and the port's pulse stay as they are for as
 *  long (settings_held(), prog_link_hold()). */
bool kst_prog_holds_port(const kst_prog_t *p);
/** What an ENTER asks of the PD mini: @p mv and its current limit @p ma.
 *  The port checks the range.  KST_PROG_SUPPLY_MV and KST_PROG_SUPPLY_MA
 *  after kst_prog_init(). */
void kst_prog_supply_set(kst_prog_t *p, uint16_t mv, uint16_t ma);
/** The highest voltage and current limit the operator lets the supply
 *  give (the SUPPLY screen's limits), 0 for none.  kst_prog_connect() is
 *  KST_PROG_MSG_SUPPLY_CAPS and asks nothing while a value of
 *  kst_prog_supply_set() is above its limit: a session never runs at
 *  values other than the ones shown.  None after kst_prog_init(). */
void kst_prog_supply_caps(kst_prog_t *p, uint16_t max_mv, uint16_t max_ma);
/** The lowest voltage a session runs at, mV; 0 for none.  An ENTER lowers
 *  a PD mini output that is on to the session's voltage or leaves it, and
 *  never raises it: kst_prog_connect() is KST_PROG_MSG_REF +
 *  LINK_KST_REF_SUPPLY_LOW and asks nothing while the feed reads an output
 *  that is on (supply_read) and set (set_mv) under @p min_mv.  The port of
 *  link version 4.15 refuses the same ENTER by that name; a coprocessor
 *  built before the refusal takes it, so the panel holds the floor for
 *  every peer.  None after kst_prog_init(). */
void kst_prog_supply_floor(kst_prog_t *p, uint16_t min_mv);
uint16_t kst_prog_supply_mv(const kst_prog_t *p);
uint16_t kst_prog_supply_ma(const kst_prog_t *p);
kst_prog_msg_id_t kst_prog_read(kst_prog_t *p);
kst_prog_msg_id_t kst_prog_verify(kst_prog_t *p);
/** The switch.  On is taken with a servo read that passes the
 *  fingerprint, and by a locked session that holds its backup. */
kst_prog_msg_id_t kst_prog_enable(kst_prog_t *p, bool on);
/** What kst_prog_enable() would refuse the switch for, and nothing said;
 *  KST_PROG_MSG_NONE where it goes on. */
kst_prog_msg_id_t kst_prog_enable_check(const kst_prog_t *p);
/** The session is locked and its backup is held: the switch is taken with
 *  no servo read, and the restore of that backup is the one write that
 *  goes.  Every other write waits for a read and for the lock to end. */
bool kst_prog_restore_only(const kst_prog_t *p);
/** Whether kst_prog_write() would be sent, without sending it. */
kst_prog_msg_id_t kst_prog_write_check(kst_prog_t *p);
/** Write the draft.  The first write of a session saves the servo's image
 *  to a file before it and is not sent without one; the journal goes to its
 *  file before every write. */
kst_prog_msg_id_t kst_prog_write(kst_prog_t *p);
/** The same for kst_prog_release(). */
kst_prog_msg_id_t kst_prog_release_check(kst_prog_t *p);
kst_prog_msg_id_t kst_prog_release(kst_prog_t *p);
/** Whether @p from holds an image that differs from the servo. */
bool kst_prog_restore_offered(const kst_prog_t *p, kst_prog_from_t from);
/**
 * Prepare a restore.  For KST_PROG_FROM_JOURNAL the draft becomes the
 * servo's image with the editable fields of the source.
 */
kst_prog_msg_id_t kst_prog_restore_plan(kst_prog_t *p, kst_prog_from_t from,
                                        kst_prog_restore_t *out);
/** Run the restore prepared. */
kst_prog_msg_id_t kst_prog_restore(kst_prog_t *p,
                                   const kst_prog_restore_t *plan);
/** Discard the journal.  KST_PROG_MSG_START + KST_LINK_START_BUSY, and
 *  nothing discarded, while an operation is on its way. */
void kst_prog_forget(kst_prog_t *p);
/** End the session: the port goes back to PWM and the supply stays as it
 *  is.  The one place a session is ended by the operator.  While a PD mini
 *  answers the port has no command for it and the answer is
 *  KST_PROG_MSG_LEAVE_SUPPLY. */
kst_prog_msg_id_t kst_prog_leave(kst_prog_t *p);
/** The wait between the end of a session and the first PWM pulse, up to
 *  KST_PROG_WAIT_MAX_MS.  False, and nothing set, above it. */
bool kst_prog_wait_set(kst_prog_t *p, uint32_t ms);
uint16_t kst_prog_wait_ms(const kst_prog_t *p);
/** The operator's cancel. */
void kst_prog_abort(kst_prog_t *p);

/* --- servo files ------------------------------------------------------------- */

/** Save the servo's image to a new file with @p name.  The card's side
 *  says whether the name is one a file may have; a file is never
 *  replaced. */
kst_prog_msg_id_t kst_prog_save(kst_prog_t *p, const char *name);
/** Ask for the list of files from file @p first on, counted from 0: up to
 *  KST_PROG_FILES_MAX of them.  kst_prog_files_seq() moves when it is
 *  there.  Refused with KST_PROG_MSG_NEED_READ while the servo is in
 *  doubt. */
kst_prog_msg_id_t kst_prog_files(kst_prog_t *p, unsigned first);
/** The number of the last list's first file among the card's. */
unsigned kst_prog_files_first(const kst_prog_t *p);
/**
 * Load the file with @p name into the draft.  Taken with a servo read that
 * passes the fingerprint and is not in doubt.  A file of another servo
 * type, a file with a value that is not taken, a file that breaks a hard
 * rule and a file that is not whole change nothing.  A file that breaks
 * soft rules waits for kst_prog_soft_confirm().
 */
kst_prog_msg_id_t kst_prog_load(kst_prog_t *p, const char *name);
/** A save, a list or a load is on its way. */
bool kst_prog_card_busy(const kst_prog_t *p);
uint32_t kst_prog_files_seq(const kst_prog_t *p);
/** The names of the last list, @p i from 0; "" beyond them. */
const char *kst_prog_file_name(const kst_prog_t *p, unsigned i);
/** That file's servo type as the card's side gave it; 0 none. */
unsigned kst_prog_file_type(const kst_prog_t *p, unsigned i);
/** The entries of the last list and, when @p total is not NULL, the servo
 *  files on the card. */
unsigned kst_prog_file_count(const kst_prog_t *p, unsigned *total);
/** The next preset name as the last list gave it; "" when none is left. */
const char *kst_prog_preset_name(const kst_prog_t *p);
/** The file last saved or asked for. */
const char *kst_prog_card_name(const kst_prog_t *p);
/** What the file last loaded is. */
kst_prog_file_kind_t kst_prog_load_kind(const kst_prog_t *p);
/** The fields the file last loaded gives a value for, and through
 *  @p fields, when not NULL, the named fields a file has: 0 where the
 *  card's side did not count them. */
unsigned kst_prog_load_named(const kst_prog_t *p, unsigned *fields);
/** The fields in which the file in the draft differs from the servo and
 *  that are never written; 0 once the servo is read again or the draft is
 *  discarded. */
kst_field_set_t kst_prog_load_skipped(const kst_prog_t *p);
/** The edit that waits for kst_prog_soft_confirm() is a file. */
bool kst_prog_soft_is_load(const kst_prog_t *p);

/** A request for the card's side; false while there is none. */
bool kst_prog_card_take(kst_prog_t *p, kst_prog_card_req_t *out);
/** What the card's side made of it.  An answer to a request that is no
 *  longer awaited changes nothing, but for one: the OK of a save given up
 *  at KST_PROG_CARD_MS names its file and, for the servo as it was saved,
 *  is the session's backup. */
void kst_prog_card_done(kst_prog_t *p, const kst_prog_card_done_t *done);

/* --- the journal's file ------------------------------------------------------ */

/** The file text is to be written: fill @p out and return its length;
 *  0 while nothing is to be written or @p n is too small. */
size_t kst_prog_file_take(kst_prog_t *p, char *out, size_t n);
/** How the write of the text kst_prog_file_take() gave went. */
void kst_prog_file_done(kst_prog_t *p, bool ok);
/** Whether @p text is a whole file: its version, its lines and its check
 *  value.  Changes nothing and says nothing. */
bool kst_prog_file_whole(const char *text, size_t len);
/** The file as found at start.  False, with KST_PROG_MSG_FILE_BAD, for a
 *  text that is not whole. */
bool kst_prog_file_load(kst_prog_t *p, const char *text, size_t len);
/** The text of the state as it is, for a caller that only reads. */
size_t kst_prog_file_text(const kst_prog_t *p, char *out, size_t n);

#ifdef __cplusplus
}
#endif
