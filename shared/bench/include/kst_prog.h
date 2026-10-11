/*
 * The KST servo programmer as the SERVO screen runs it: what is read from a
 * KST or Chaservo servo, what the operator has changed, and what may be
 * written.
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
 *     soft rule waits for a confirmation.  No limit is stated here.
 *   - A write is sent with the switch ENABLE WRITE TO SERVO on only.  The
 *     switch goes off at every entry of the screen, when the link goes, when
 *     the coprocessor starts again, at STOP, and with every servo
 *     connected.
 *   - Every answer of kst_link_start(), every outcome kind, every refusal
 *     of the port and every result of the session becomes one message
 *     (kst_prog_msg_id_t) the screen has a text for.
 *
 * The journal: before a write the image the servo holds and the channel are
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
 *   channel=<1 hexadecimal digit>
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

/** The file text's size, with its terminator. */
#define KST_PROG_FILE_MAX 400u
/** A command without an answer from the control task for this long is
 *  given up. */
#define KST_PROG_START_MS 2000u
/** An operation without an outcome for this long is aborted. */
#define KST_PROG_RUN_MS 30000u
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
#define KST_PROG_NAME_MAX 16u

/** Where the programmer is. */
typedef enum {
    KST_PROG_DOWN = 0,     /**< no coprocessor answers                    */
    KST_PROG_NO_PAGE,      /**< the coprocessor has no KST port           */
    KST_PROG_SYNC,         /**< the page has not been read yet            */
    KST_PROG_PWM,          /**< the tester has the output                 */
    KST_PROG_ENTERING,     /**< the output leaves PWM                     */
    KST_PROG_PROGRAMMING,  /**< the port holds the channel, nothing runs  */
    KST_PROG_WORKING,      /**< an operation runs                         */
    KST_PROG_LEAVING,      /**< the servo rail is switched               */
} kst_prog_state_t;

/**
 * What the screen says.  The four ranges at the end take a code of the
 * link module or the core: KST_PROG_MSG_START + kst_link_start_t,
 * KST_PROG_MSG_OUT + kst_link_outcome_kind_t, KST_PROG_MSG_REF +
 * link_kst_refusal_t and KST_PROG_MSG_SES + kst_ses_t.  A message is a
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
                                       the journal's channel, from 1        */
    KST_PROG_MSG_JOURNAL_SAME,    /**< the servo holds what it held before  */
    KST_PROG_MSG_JOURNAL_DONE,    /**< the servo holds the write's target   */
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
                                       arg: the journal's channel, from 1   */
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
    KST_PROG_MSG_START = 0x40,
    KST_PROG_MSG_OUT   = 0x50,
    KST_PROG_MSG_REF   = 0x60,
    KST_PROG_MSG_SES   = 0x80,
    KST_PROG_MSG_END   = 0xA0
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
    kst_rules_t soft;   /**< soft limits the restored image passes and the
                             servo's own does not; 0 where the servo's
                             image is not known                            */
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
    uint8_t field;       /**< KST_PROG_CARD_VALUE: the kst_field_id_t,
                              0xFF none                                    */
    uint16_t line;       /**< a refusal's line of the text; 0 none         */
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
    bool stopped;              /**< STOP is latched: no heartbeat */
    bool card;                 /**< a card takes the file */
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
    uint8_t journal_ch;
    kst_image_t journal_img;     /**< the servo before the write   */
    kst_image_t journal_target;  /**< and what the write was to leave */

    bool write_enabled;
    bool written;             /**< a write was sent in this session */
    bool backed_up;           /**< a file holds this session's servo */
    bool no_file;             /**< a write went with no card for one */
    bool held_no_file;        /**< and the held write is such a one  */
    uint8_t channel;

    uint8_t wait;             /**< what is awaited                */
    uint8_t op;               /**< link_kst_op_t on its way       */
    uint8_t writes;           /**< fields the write on its way changes */
    uint8_t restoring;        /**< kst_prog_from_t of that write  */
    bool chain_read;          /**< READ_ALL follows the ENTER     */
    uint32_t wait_ms;
    uint32_t fetch_ms;
    bool fetched;
    bool cmd_fetch;
    uint32_t wait_id;
    bool cmd_ready;
    kst_prog_cmd_t cmd;
    kst_link_request_t held;  /**< the request the file goes before */
    kst_image_t held_target;  /**< and what it is to leave */

    uint8_t card_op;          /**< kst_prog_card_op_t on its way   */
    bool card_ready;          /**< and not taken yet               */
    bool card_auto;           /**< the save a write waits for      */
    bool soft_load;           /**< the edit that waits is a file   */
    kst_field_set_t load_skipped;
    kst_field_set_t soft_skipped;   /**< of the file that waits */
    uint8_t file_types[KST_PROG_FILES_MAX];
    uint8_t load_kind;        /**< kst_prog_file_kind_t            */
    char card_name[KST_PROG_NAME_MAX];  /**< the file saved or loaded */
    char preset[KST_PROG_NAME_MAX];     /**< the next preset name     */
    char backup[KST_PROG_NAME_MAX];     /**< the file of backed_up    */
    uint32_t card_id;
    uint32_t card_ms;
    uint8_t files_n;
    uint16_t files_total;
    uint16_t files_first;     /**< of the last list                */
    uint16_t card_first;      /**< of the list asked for           */
    uint32_t files_seq;       /**< counts the lists that arrived   */
    char files[KST_PROG_FILES_MAX][KST_PROG_NAME_MAX];

    bool soft;
    kst_image_t soft_img;
    kst_rules_t soft_rules;

    bool file_wanted;
    bool file_busy;
    bool file_retry;          /**< the card owes the file          */
    uint32_t file_retry_ms;
    uint8_t file_next;        /**< what the write wanted is for    */
    uint8_t file_for;         /**< what the write on its way is for */

    kst_prog_msg_t msg;
    kst_plan_t plan;          /**< work space; not on the stack */
} kst_prog_t;

void kst_prog_init(kst_prog_t *p);

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
uint8_t kst_prog_channel(const kst_prog_t *p);
/** A write was interrupted and the servo was not compared since. */
bool kst_prog_journal(const kst_prog_t *p);
/**
 * The servo read cannot be the one of the interrupted write: it is on
 * another channel, or it differs in a field that neither the write nor an
 * edit changes.  Nothing is restored to it and nothing is written to it
 * until the journal is discarded.  Two servos that agree in every such
 * field and sit on the same channel are not told apart.
 */
bool kst_prog_journal_other(const kst_prog_t *p);
/** The channel of the interrupted write, from 0. */
uint8_t kst_prog_journal_channel(const kst_prog_t *p);
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
/** The next write saves the servo's image to a file first.  A mounted card
 *  that does not take the file ends the write; with no card mounted the
 *  write goes without a file. */
bool kst_prog_save_first(const kst_prog_t *p);

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
/** The soft rules the edit that waits breaks beyond those of the draft; 0
 *  while none waits. */
kst_rules_t kst_prog_soft_rules(const kst_prog_t *p);
/** The image the edit that waits would leave. */
const kst_image_t *kst_prog_soft_image(const kst_prog_t *p);
void kst_prog_soft_confirm(kst_prog_t *p);
void kst_prog_soft_cancel(kst_prog_t *p);
/** The draft becomes the servo's image again. */
void kst_prog_discard(kst_prog_t *p);

/* --- operations: KST_PROG_MSG_NONE when taken, else the message set ------- */

kst_prog_msg_id_t kst_prog_connect(kst_prog_t *p, uint8_t channel);
kst_prog_msg_id_t kst_prog_read(kst_prog_t *p);
kst_prog_msg_id_t kst_prog_verify(kst_prog_t *p);
/** The switch.  On is taken with a servo read that passes the
 *  fingerprint. */
kst_prog_msg_id_t kst_prog_enable(kst_prog_t *p, bool on);
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
/** Discard the journal. */
void kst_prog_forget(kst_prog_t *p);
/** The servo was without power: the output goes back to the tester. */
kst_prog_msg_id_t kst_prog_power_cycled(kst_prog_t *p);
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
/** The fields in which the file in the draft differs from the servo and
 *  that are never written; 0 once the servo is read again or the draft is
 *  discarded. */
kst_field_set_t kst_prog_load_skipped(const kst_prog_t *p);
/** The edit that waits for kst_prog_soft_confirm() is a file. */
bool kst_prog_soft_is_load(const kst_prog_t *p);

/** A request for the card's side; false while there is none. */
bool kst_prog_card_take(kst_prog_t *p, kst_prog_card_req_t *out);
/** What the card's side made of it.  An answer to a request that is no
 *  longer awaited changes nothing. */
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
