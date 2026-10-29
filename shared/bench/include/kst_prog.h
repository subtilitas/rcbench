/**
 * @file
 * @brief The programmer for KST and Chaservo servos as the SERVO screen runs
 *        it: what is read from a servo, what the operator has changed, and what
 *        may be written.
 *
 * The session with the servo runs at the IOmcu and the panel's link
 * module (kst_link.h) carries its operations.  This file sits between that
 * module and the screen:
 *
 *   - It holds the servo's image as last read, a draft the operator edits,
 *     the backup the IOmcu's session holds, and the journal of a
 *     write.
 *   - An edit goes through the field table and the limits of protocols/kst:
 *     a value that breaks a hard rule is not taken, a value that breaks a
 *     soft rule waits for a confirmation.  No limit is stated here.  The
 *     soft rules compare with the servo as the session found it
 *     (kst_prog_soft_base()), and a rule confirmed for a value is asked
 *     again for a value further from that image.
 *   - A write is sent with the switch ENABLE WRITE TO SERVO on only.  The
 *     switch goes off at every entry of the screen, when the link goes, when
 *     the IOmcu starts again, at STOP, and with every servo
 *     connected.
 *   - Every answer of kst_link_start(), every outcome kind, every refusal
 *     of the port and every result of the session becomes one message
 *     (kst_prog_msg_id_t) the screen has a text for.
 *
 * The journal: before a write the image the servo holds, the port and the
 * GPIO that port drives are recorded, in memory and in the file text
 * (kst_prog_file_text()).  A write that
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
 *   RCBENCH KST 2
 *   journal=<64 hexadecimal digits>     the image before a write, when
 *                                       one is recorded
 *   target=<64 hexadecimal digits>      what that write was to leave
 *   port=<1 hexadecimal digit>
 *   pin=<2 hexadecimal digits>          the GPIO the port drove, FF not
 *                                       known
 *   check=<4 hexadecimal digits>        CRC-16/CCITT of the bytes before
 *
 * A text of version 1 ("RCBENCH KST 1", no pin line) is read as one whose
 * pin is not known.  A pin above KST_PROG_PIN_LAST reads as not known: the
 * record stands and names its port.  The text written is version 2.
 *
 * Pure C.  The caller hands in kst_prog_feed_t every frame and carries out
 * the commands kst_prog_take() gives.  One task only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdatomic.h>
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

/** @brief The supply an ENTER asks for until it is set otherwise. */
#define KST_PROG_SUPPLY_MV 5000u
#define KST_PROG_SUPPLY_MA 1000u
/**
 * @brief The wait between the end of a session and the first PWM pulse: its
 *        default and the highest value taken.
 */
#define KST_PROG_WAIT_MS     100u
#define KST_PROG_WAIT_MAX_MS 10000u
/** @brief The file text's size, with its terminator. */
#define KST_PROG_FILE_MAX 400u
/**
 * @brief The highest GPIO of the IOmcu (GP0 to GP47), and the pin that is  not
 *        known.
 */
#define KST_PROG_PIN_LAST 47u
#define KST_PROG_NO_PIN   0xFFu
/**
 * @brief A command without an answer from the control task for this long is
 *        given up.
 */
#define KST_PROG_START_MS 2000u
/**
 * @brief An operation without an outcome for this long is aborted.
 *
 * The time
 *  counts from the last feed in which the port reported it busy.
 */
#define KST_PROG_RUN_MS 30000u
/**
 * @brief And one the port reports busy for this long from its start is aborted
 *        too.
 *
 * A chosen value: the longest wait of the port is the 10000ms of a
 *  LEAVE, and the duration of the longest write is not measured.
 */
#define KST_PROG_RUN_MAX_MS 120000u
/**
 * @brief The file not written for this long: the write to the servo is not
 *        sent.
 */
#define KST_PROG_STORE_MS 3000u
/** @brief The time between two fetches of images the port holds. */
#define KST_PROG_FETCH_MS 1000u
/** @brief A file the card did not take is written again after this long. */
#define KST_PROG_FILE_RETRY_MS 5000u
/** @brief A save, a list or a load without an end for this long has failed. */
#define KST_PROG_CARD_MS 5000u
/** @brief The files a list holds. */
#define KST_PROG_FILES_MAX 5u
/** @brief A file's name with its terminator. */
#define KST_PROG_NAME_MAX 33u

/** @brief Where the programmer is. */
typedef enum {
    KST_PROG_DOWN = 0,     /**< no IOmcu answers                    */
    KST_PROG_NO_PAGE,      /**< the IOmcu has no such page         */
    KST_PROG_SYNC,         /**< the page has not been read yet            */
    KST_PROG_PWM,          /**< the tester has the output                 */
    KST_PROG_ENTERING,     /**< the output leaves PWM                     */
    KST_PROG_PROGRAMMING,  /**< the port holds the pin, nothing runs      */
    KST_PROG_WORKING,      /**< an operation runs                         */
    KST_PROG_LEAVING,      /**< the servo rail is switched               */
} kst_prog_state_t;

/**
 * @brief What the screen says.
 *
 * The four ranges at the end take a code of the
 * link module or the core: KST_PROG_MSG_START + kst_link_start_t,
 * KST_PROG_MSG_OUT + kst_link_outcome_kind_t, KST_PROG_MSG_REF +
 * link_kst_refusal_t and KST_PROG_MSG_SES + the result byte of the
 * outcome (a kst_ses_t or a LINK_KST_RESULT_*).  A message is a
 * number and not an enum for that reason: most of its values lie between
 * the names.
 */
typedef unsigned kst_prog_msg_id_t;

/**
 * @brief The message ids of the programmer's screen.
 */
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
                                       kst_prog_journal_arg()               */
    KST_PROG_MSG_JOURNAL_SAME,    /**< the servo holds what the record says
                                       it held before the write; the record
                                       stands and asks whether this is the
                                       servo.  arg: as for
                                       KST_PROG_MSG_JOURNAL                 */
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
                                       arg: as for KST_PROG_MSG_JOURNAL     */
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
    KST_PROG_MSG_LEAVE_SUPPLY,    /**< a PD mini answers and the IOmcu
                                       is below link 4.15: the port has no
                                       way back to PWM that leaves the
                                       supply on                            */
    KST_PROG_MSG_LEFT_NO_WAIT,    /**< the port is back in PWM by an
                                       IOmcu below link 4.15: no wait
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
                                       went off or the IOmcu started
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
    KST_PROG_MSG_NEED_TYPE,       /**< a load waits for the servo type the
                                       operator sets (kst_prog_type_set())  */
    KST_PROG_MSG_CYCLE_CUT,       /**< a supply cycle of CONNECT or LEAVE
                                       was stopped: the supply stays off    */
    KST_PROG_MSG_LEFT_CYCLED,     /**< the port is back in PWM after a
                                       supply cycle: the written values act */
    KST_PROG_MSG_LEFT_NOTE,       /**< the port is back in PWM, no PD mini
                                       answering, after a write: the values
                                       act after the servo's supply is
                                       switched off and on                  */
    KST_PROG_MSG_LEFT_CYCLED_OLD, /**< as KST_PROG_MSG_LEFT_CYCLED, by an
                                       IOmcu below link 4.30: the wait
                                       before PWM was not applied           */
    KST_PROG_MSG_START = 0x40,
    KST_PROG_MSG_OUT   = 0x50,
    KST_PROG_MSG_REF   = 0x60,
    KST_PROG_MSG_SES   = 0x80,
    KST_PROG_MSG_END   = 0xB0
};

/**
 * @brief The level of a message: information, warning or danger.
 */
typedef enum {
    KST_PROG_INFO = 0,
    KST_PROG_WARN,
    KST_PROG_DANGER,
} kst_prog_level_t;

/**
 * @brief The last message the programmer says, with its id, level and argument.
 */
typedef struct {
    uint8_t id;        /**< kst_prog_msg_id_t                       */
    uint8_t level;     /**< kst_prog_level_t                        */
    uint8_t reg;       /**< the register a result names; 0xFF none  */
    uint16_t arg;                       /**< the message's argument, as its id says */
    uint32_t seq;      /**< counts the messages                     */
} kst_prog_msg_t;

/** @brief What an edit came to. */
typedef enum {
    KST_PROG_EDIT_OK = 0,   /**< the draft holds the value                 */
    KST_PROG_EDIT_SOFT,     /**< it waits for kst_prog_soft_confirm()      */
    KST_PROG_EDIT_REFUSED,  /**< not taken; kst_prog_message() says why    */
} kst_prog_edit_t;

/** @brief Where a restore takes its image from. */
typedef enum {
    KST_PROG_FROM_NONE = 0,
    KST_PROG_FROM_SESSION,  /**< the backup of the IOmcu's session */
    KST_PROG_FROM_JOURNAL,  /**< the image recorded before a write       */
    KST_PROG_FROM_RUN,      /**< the servo before a limit run's first
                                 write (kst_prog_run_hold())             */
    KST_PROG_FROM_COUNT
} kst_prog_from_t;

/** @brief A restore as kst_prog_restore_plan() prepared it. */
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

/** @brief What the card's side is asked for. */
typedef enum {
    KST_PROG_CARD_IDLE = 0,
    KST_PROG_CARD_SAVE,   /**< the image into a new file                   */
    KST_PROG_CARD_LIST,   /**< the names of the files and the next preset  */
    KST_PROG_CARD_LOAD,   /**< the file with the name, against the servo   */
} kst_prog_card_op_t;

/** @brief How it went. */
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

/** @brief What a file that loads is. */
typedef enum {
    KST_PROG_FILE_VERIFIED = 0,  /**< as written, its check value fits  */
    KST_PROG_FILE_EDITED,        /**< no check value; names its type    */
    KST_PROG_FILE_UNTYPED,       /**< no check value and no servo type  */
    KST_PROG_FILE_KINDS
} kst_prog_file_kind_t;

/**
 * @brief A request to the card side.
 */
typedef struct {
    uint8_t op;          /**< kst_prog_card_op_t                           */
    bool preset;         /**< KST_PROG_CARD_SAVE: the card's side names it */
    uint32_t id;         /**< goes back in the answer                      */
    char name[KST_PROG_NAME_MAX];  /**< the file to save or to load        */
    uint8_t type;        /**< the servo type the operator set, 6 or 8; 0
                              not set.  A save states it in the file, a
                              load holds the file to it                    */
    uint16_t first;      /**< KST_PROG_CARD_LIST: the first file wanted,
                              counted from 0                               */
    kst_image_t image;   /**< the servo's image: what a save writes, what
                              a load is compared with                      */
} kst_prog_card_req_t;

/**
 * @brief The card side's answer to a request.
 */
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
    uint8_t servo_type;  /**< KST_PROG_CARD_TYPE: the servo's, as the
                              operator set it                              */
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

/**
 * @brief The kinds of command the programmer hands to the link module.
 */
typedef enum {
    KST_PROG_CMD_START = 0,   /**< kst_link_start(req)  */
    KST_PROG_CMD_ABORT,       /**< kst_link_abort()     */
    KST_PROG_CMD_FETCH,       /**< kst_link_fetch(): the port holds images
                                   this panel does not, after a start of
                                   the panel on a held port */
} kst_prog_cmd_kind_t;

/** @brief One call for the task that owns the link module. */
typedef struct {
    kst_prog_cmd_kind_t kind;
    uint32_t id;               /**< a start's comes back in
                                    kst_prog_feed_t.answer_id */
    kst_link_request_t req;             /**< the request for kind */
} kst_prog_cmd_t;

/**
 * @brief The pass a START needs to be carried out.
 *
 * The model's task and the task that owns the link module share one of these.
 * kst_prog_take() offers the pass of each START it hands out; the link's task
 * claims it when the command comes out of its queue and carries the command out
 * only with the pass; the model revokes the pass of a START nobody answered.
 * Each pass is taken once, by the one or by the other: a START whose pass the
 * model revoked is never carried out, however late it leaves the queue.
 *
 * The three calls are safe from two tasks without a lock: each is one
 * atomic exchange of a 32-bit word.
 */
typedef struct {
    _Atomic uint32_t pass;     /**< the id of the START offered; 0 none */
} kst_prog_gate_t;

/**
 * @brief Sets up a START gate.
 *
 * @param g The gate to set up.
 */
void kst_prog_gate_init(kst_prog_gate_t *g);
/**
 * @brief The pass of START @p id is there to be taken.
 *
 * @param g  The gate.
 * @param id The START's id.
 */
void kst_prog_gate_offer(kst_prog_gate_t *g, uint32_t id);
/**
 * @brief The link's task, with a START out of its queue: true when the command
 *        is to be carried out.
 *
 * False: it is dropped, with no answer.
 *
 * @param g  The gate.
 * @param id The START's id.
 * @return true when the command is to be carried out.
 */
bool kst_prog_gate_claim(kst_prog_gate_t *g, uint32_t id);
/**
 * @brief The model: true when START @p id is not carried out and never is.
 *
 * @param g  The gate.
 * @param id The START's id.
 * @return true when START id is not carried out and never is.
 */
bool kst_prog_gate_revoke(kst_prog_gate_t *g, uint32_t id);

/** @brief What a result of the session says of a write that ended with it. */
typedef enum {
    KST_PROG_RES_OK = 0,   /**< the write ended as asked                    */
    KST_PROG_RES_CLEAN,    /**< with no step counted done the servo holds
                                the image the write started from: no frame
                                went, or every write was undone and read
                                back                                        */
    KST_PROG_RES_DOUBT,    /**< the servo may hold part of the write        */
} kst_prog_res_t;

/**
 * @brief The class of @p result, a kst_ses_t or a LINK_KST_RESULT_ value.
 *
 * A
 *  value that is neither is KST_PROG_RES_DOUBT.
 *
 * @param result The result code.
 * @return The class of the result, as kst_prog_res_t.
 */
kst_prog_res_t kst_prog_result_class(unsigned result);

/** @brief What the task that owns the link module publishes, every frame. */
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
    kst_link_outcome_t outcome;         /**< the outcome handed out */
    bool     supply_read;      /**< out_mv and out_ma are a reading of the
                                    PD mini                               */
    uint16_t out_mv;           /**< its output, mV                        */
    uint16_t out_ma;           /**< and the current it gives, mA          */
    uint16_t set_mv;           /**< and the voltage it is set to, mV      */
    uint16_t set_ma;           /**< and the current limit it is set to,
                                    mA; 0 not known                       */
    bool     fixed_on;         /**< the ports' own fixed supply (the rbmod
                                    carrier's 5V, GPIO 21) reads on; no
                                    reading of its voltage or current      */
    bool stopped;              /**< STOP is latched: no heartbeat */
    bool armed;                /**< the panel's outputs are armed */
    bool card;                 /**< a card takes the file */
    uint8_t card_state;        /**< card_state_t of the panel's slot, for
                                    what the screen says without a card:
                                    0 no card, 2 no FAT volume, 3
                                    unreadable */
    uint8_t target;            /**< what the SERVO screen's tester drives
                                    as the IOmcu reads it: 0 the
                                    standard servo ports, 1 + n
                                    programming port n */
    uint8_t port_pin[LINK_PG_PORTS_MAX]; /**< the GPIO programming port n
                                              drives, when bit n of
                                              port_pin_known is set */
    uint8_t port_pin_known;             /**< bit n set: port_pin[n] holds a GPIO */
} kst_prog_feed_t;

/**
 * @brief The state of the servo programmer.
 */
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
    /** @brief The programming port the journal's image was read on */
    uint8_t journal_port;
    uint8_t journal_pin;      /**< the GPIO of journal_port at the write,
                                   KST_PROG_NO_PIN not known       */
    kst_image_t journal_img;     /**< the servo before the write   */
    kst_image_t journal_target;  /**< and what the write was to leave */

    bool write_enabled;
    bool written;             /**< a write was sent in this session */
    bool backed_up;           /**< a file holds this session's servo */
    bool no_file;             /**< a write went with no card for one */
    bool held_no_file;        /**< and the held write is such a one  */
    uint8_t port;                       /**< the programming port the session works on */
    uint16_t supply_mv;       /**< what an ENTER asks of the PD mini */
    uint16_t supply_ma;                 /**< what an ENTER asks of the PD mini, mA */
    uint16_t caps_mv;         /**< the supply's limits, 0 for none   */
    uint16_t caps_ma;                   /**< the supply's current limit, mA; 0 for none */
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
    uint8_t cycle;            /**< the supply cycle under way, KST_PROG_
                                   CYCLE_*: a CONNECT's before its
                                   ENTER, or a LEAVE's after a write  */
    bool cycle_cut;           /**< the operator's ABORT came during
                                   it: no ENTER follows               */
    bool cycle_off;           /**< the CONNECT's REBOOT was asked to
                                   end with the supply off            */
    bool cycle_old;           /**< the IOmcu is below link 4.30: the
                                   LEAVE's REBOOT went without the
                                   wait after its cycle               */
    uint16_t cycle_mv;        /**< what the ENTER after a CONNECT's
                                   cycle asks: the lower of the
                                   programming voltage and the one the
                                   output was set to, mV              */
    uint16_t cycle_ma;        /**< and the lower current limit, mA    */
    uint8_t type;             /**< the servo type the operator set, 6
                                   or 8; 0 while not set.  Held until
                                   the session ends                   */
    uint32_t wait_ms;                   /**< when the wait for the port began, ms */
    uint32_t run_ms;          /**< when the wait for the outcome began */
    uint32_t fetch_ms;                  /**< when the backup was last fetched, ms */
    bool fetched;                       /**< whether a backup was fetched this session */
    bool cmd_fetch;                     /**< whether a FETCH command is owed to the link task */
    uint32_t wait_id;                   /**< id of the start the session waits for */
    bool cmd_ready;                     /**< whether cmd holds a command not yet taken */
    kst_prog_cmd_t cmd;                 /**< the command waiting to be taken */
    kst_link_request_t held;  /**< the request the file goes before */
    kst_image_t held_target;  /**< and what it is to leave */
    bool held_journal;        /**< the held write recorded the journal */
    bool held_over;           /**< over the record of an earlier write,
                                   whose target is in held_before      */
    kst_image_t held_before;            /**< the target of the earlier write's record */
    kst_prog_gate_t *gate;    /**< kst_prog_gate_set()             */
    bool cmd_out;             /**< the START awaited was handed out */
    bool cmd_run;             /**< and its pass was taken: it is carried
                                   out or was                       */
    bool keep;                /**< keep_draft holds the pending values
                                   of before a restore that is not
                                   started yet                      */
    bool keep_soft;                     /**< the soft limits kept with keep_draft */
    bool keep_soft_load;                /**< whether the kept edit is a file */
    kst_image_t keep_draft;             /**< the pending values kept before a restore */

    uint8_t card_op;          /**< kst_prog_card_op_t on its way   */
    bool card_ready;          /**< and not taken yet               */
    bool card_auto;           /**< the save a write waits for      */
    bool soft_load;           /**< the edit that waits is a file   */
    kst_field_set_t load_skipped;       /**< fields of the loaded file the servo's model skipped */
    kst_field_set_t soft_skipped;   /**< of the file that waits */
    uint8_t file_types[KST_PROG_FILES_MAX];  /**< per listed file, its kst_prog_file_kind_t */
    uint8_t load_kind;        /**< kst_prog_file_kind_t            */
    uint8_t load_named;       /**< fields the file gives a value for */
    uint8_t load_fields;      /**< of this many; 0 not counted     */
    char card_name[KST_PROG_NAME_MAX];  /**< the file saved or loaded */
    char preset[KST_PROG_NAME_MAX];     /**< the next preset name     */
    char backup[KST_PROG_NAME_MAX];     /**< the file of backed_up    */
    /** @brief Number of the card operation on its way; 0 is never used */
    uint32_t card_id;
    uint32_t card_ms;                   /**< when the card operation was started, ms */
    uint32_t late_id;         /**< the save given up at KST_PROG_CARD_MS;
                                   0 for none                       */
    kst_image_t late_img;     /**< and the image it was asked with */
    uint8_t files_n;                    /**< files in the list */
    uint16_t files_total;               /**< files the card holds in all */
    uint16_t files_first;     /**< of the last list                */
    uint16_t card_first;      /**< of the list asked for           */
    uint32_t files_seq;       /**< counts the lists that arrived   */
    char files[KST_PROG_FILES_MAX][KST_PROG_NAME_MAX];  /**< the listed files' names */

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
    uint32_t file_retry_ms;             /**< when the card last owed the file, ms */
    uint8_t file_next;        /**< what the write wanted is for    */
    uint8_t file_for;         /**< what the write on its way is for */

    /* The limit run (servo_limrun.h): the servo as its first session read
     * it, kept across its sessions and after it, until the next run. */
    bool run_hold;
    bool run_own;             /**< the run is under way: its sessions
                                   take the file of its first write  */
    bool run_written;         /**< the run's first write was sent */
    kst_image_t run_img;
    char run_backup[KST_PROG_NAME_MAX];  /**< the file of run_img; "" none */

    kst_prog_msg_t msg;
    kst_plan_t plan;          /**< work space; not on the stack */
} kst_prog_t;

/**
 * @brief Sets up the programmer state.
 *
 * @param p The programmer state to set up.
 */
void kst_prog_init(kst_prog_t *p);

/**
 * @brief The gate the link's task claims each START at.
 *
 * With one set, a START nobody answers within KST_PROG_START_MS is revoked and
 * was never carried out.  With none set, such a START counts as one that may
 * have run: the servo is read before anything else.  kst_prog_init() sets none.
 *
 * @param p The programmer state.
 * @param g The gate, or NULL for none.
 */
void kst_prog_gate_set(kst_prog_t *p, kst_prog_gate_t *g);

/**
 * @brief The published state of this frame.
 *
 * Outcomes and answers are taken
 *  here.
 *
 * @param p The programmer state.
 * @param f The inputs of this frame.
 */
void kst_prog_feed(kst_prog_t *p, const kst_prog_feed_t *f);

/**
 * @brief The next command for the task that owns the link module; false for
 *        none.
 *
 * @param p   The programmer state.
 * @param out Receives the command.
 * @return true when a command is taken.
 */
bool kst_prog_take(kst_prog_t *p, kst_prog_cmd_t *out);

/**
 * @brief The screen was entered: ENABLE WRITE TO SERVO goes off.
 *
 * @param p The programmer state.
 */
void kst_prog_screen_entry(kst_prog_t *p);

/**
 * @brief The programmer state.
 *
 * @param p The programmer state.
 * @return The state, as kst_prog_state_t.
 */
kst_prog_state_t kst_prog_state(const kst_prog_t *p);
/**
 * @brief The operation on its way or running; LINK_KST_OP_NONE for none.
 *
 * @param p The programmer state.
 * @return The operation, as link_kst_op_t.
 */
link_kst_op_t kst_prog_op(const kst_prog_t *p);
/**
 * @brief A write, a restore or a release is on its way or runs under this
 *        model: the record of it is no interrupted write yet.
 *
 * @param p The programmer state.
 * @return true while a write, restore or release is on its way or runs.
 */
bool kst_prog_writing(const kst_prog_t *p);
/**
 * @brief The registers that write writes, as this model planned it; 0 when no
 *        write runs or the IOmcu alone plans it (a restore with no checked
 *        order, a release).
 *
 * The register under way is not on the link page.
 *
 * @param p The programmer state.
 * @return The registers the write writes, or 0.
 */
unsigned kst_prog_write_steps(const kst_prog_t *p);
/**
 * @brief The IOmcu plans the order of a write's or a restore's writes: no
 *        frame goes to the servo yet.
 *
 * @param p The programmer state.
 * @return true while the IOmcu plans the order of the writes.
 */
bool kst_prog_planning(const kst_prog_t *p);
/**
 * @brief The last message.
 *
 * @param p The programmer state.
 * @return The last message, or NULL.
 */
const kst_prog_msg_t *kst_prog_message(const kst_prog_t *p);

/* --- what is held --------------------------------------------------------- */

/**
 * @brief Whether a servo was read.
 *
 * @param p The programmer state.
 * @return true when a servo was read.
 */
bool kst_prog_have_servo(const kst_prog_t *p);
/**
 * @brief A servo was read, its image passes the fingerprint and is not in
 *        doubt: it may be edited and written.
 *
 * @param p The programmer state.
 * @return true when the servo may be edited and written.
 */
bool kst_prog_editable(const kst_prog_t *p);
/**
 * @brief The servo's image as last read.
 *
 * @param p The programmer state.
 * @return The servo's image.
 */
const kst_image_t *kst_prog_servo(const kst_prog_t *p);
/**
 * @brief The draft image being edited.
 *
 * @param p The programmer state.
 * @return The draft image.
 */
const kst_image_t *kst_prog_draft(const kst_prog_t *p);
/**
 * @brief Whether the write switch is on.
 *
 * @param p The programmer state.
 * @return true when ENABLE WRITE TO SERVO is on.
 */
bool kst_prog_write_enabled(const kst_prog_t *p);
/**
 * @brief The programming port.
 *
 * @param p The programmer state.
 * @return The port the session holds, from 0.
 */
uint8_t kst_prog_port(const kst_prog_t *p);
/**
 * @brief A write was interrupted and the servo was not compared since.
 *
 * @param p The programmer state.
 * @return true while a write was interrupted and the servo was not compared
 *         since.
 */
bool kst_prog_journal(const kst_prog_t *p);
/**
 * @brief A record of an interrupted write is held, read back or not: the one
 *        RESTORE offers the values of.
 *
 * @param p The programmer state.
 * @return true when a record of an interrupted write is held.
 */
bool kst_prog_journal_held(const kst_prog_t *p);
/**
 * @brief The servo read cannot be the one of the interrupted write: it is on
 *        another port, or a register of it holds neither the record's value nor
 *        the write's target.
 *
 * The low register of a field in two registers that
 * the write changes is exempt: a plan may set it to a third value.
 * Nothing is restored to it and nothing is written to it until the journal
 * is discarded.
 *
 * @param p The programmer state.
 * @return true when the servo read cannot be the interrupted write's servo.
 */
bool kst_prog_journal_other(const kst_prog_t *p);
/**
 * @brief The servo read holds, register for register, the record's image
 *        (KST_PROG_MSG_JOURNAL_SAME) or the write's target
 *        (KST_PROG_MSG_JOURNAL_DONE), and the record stands: a second servo
 *        with the same settings is not told apart from the one of the write.
 *
 * Nothing is written to the servo until kst_prog_forget() discards the record.
 * KST_PROG_MSG_NONE otherwise.
 *
 * @param p The programmer state.
 * @return The message the journal asks for, or KST_PROG_MSG_NONE.
 */
kst_prog_msg_id_t kst_prog_journal_asks(const kst_prog_t *p);
/**
 * @brief The servo read is not the one the session's backup was read from: the
 *        two differ in a field no write of the session set out to change.
 *
 * The
 *  backup is not offered and not restored until a new session reads its
 *  own.  Two servos that agree in every such field are not told apart.
 *  Never true for a session this model did not see begin, after a link
 *  loss or a start of the panel under a session: its writes are not
 *  known.
 *
 * @param p The programmer state.
 * @return true when the servo read is not the one the session's backup was read
 *         from.
 */
bool kst_prog_backup_other(const kst_prog_t *p);
/**
 * @brief The port of the interrupted write, from 0.
 *
 * @param p The programmer state.
 * @return The port of the interrupted write, from 0.
 */
uint8_t kst_prog_journal_port(const kst_prog_t *p);
/**
 * @brief The GPIO that port drove at the write, or KST_PROG_NO_PIN: not known,
 *        or a record of version 1.
 *
 * @param p The programmer state.
 * @return The GPIO that port drove, or KST_PROG_NO_PIN.
 */
uint8_t kst_prog_journal_pin(const kst_prog_t *p);
/**
 * @brief The argument of the journal's messages: the port + 1 in bits 0-7, the
 *        pin + 1 in bits 8-15, 0 there when the pin is not known.
 *
 * @param p The programmer state.
 * @return The argument of the journal's messages.
 */
unsigned kst_prog_journal_arg(const kst_prog_t *p);
/**
 * @brief The servo may hold part of a write: a journal stands, the port asks
 *        for  a read, an operation timed out, or the session is locked.
 *
 * @param p The programmer state.
 * @return true when the servo may hold part of a write.
 */
bool kst_prog_in_doubt(const kst_prog_t *p);
/**
 * @brief No write was sent since the servo was connected: the next asks for the
 *        free horn.
 *
 * @param p The programmer state.
 * @return true when no write was sent since the servo was connected.
 */
bool kst_prog_first_write(const kst_prog_t *p);
/**
 * @brief A file holds the servo as this session found it: the next write waits
 *        for no save.
 *
 * @param p The programmer state.
 * @return true when a file holds the servo as the session found it.
 */
bool kst_prog_backed_up(const kst_prog_t *p);
/**
 * @brief That file's name; "" while there is none.
 *
 * @param p The programmer state.
 * @return The backup file's name, or an empty string.
 */
const char *kst_prog_backup_name(const kst_prog_t *p);
/**
 * @brief A write of this session started with no card in the slot, and no file
 *        holds the servo as the session found it: the session's backup is the
 *        one copy.
 *
 * Ends with the next file a write saves.
 *
 * @param p The programmer state.
 * @return true when the session's backup is the one copy.
 */
bool kst_prog_no_file(const kst_prog_t *p);
/**
 * @brief The next write saves the servo as the session found it to a file
 *        first.
 *
 * A mounted card that does not take the file ends the write.
 *  False while a record of an interrupted write is held: the servo may be
 *  half written, and the record holds it as found.
 *
 * @param p The programmer state.
 * @return true when the next write saves the servo to a file first.
 */
bool kst_prog_save_first(const kst_prog_t *p);
/**
 * @brief That write goes without the file: no card is mounted, or the servo may
 *        hold other values than the session found and the session's backup, the
 *        image the file takes then, is not fetched.
 *
 * @param p The programmer state.
 * @return true when that write goes without the file.
 */
bool kst_prog_save_none(const kst_prog_t *p);

/**
 * @brief The fields in which the draft differs from the servo, register 01's
 *        copy of Max.
 *
 * Duty left out.
 *
 * @param p The programmer state.
 * @return The fields in which the draft differs from the servo.
 */
kst_field_set_t kst_prog_changed(const kst_prog_t *p);
/**
 * @brief The number of fields the draft differs in.
 *
 * @param p The programmer state.
 * @return The number of changed fields.
 */
unsigned kst_prog_changes(const kst_prog_t *p);
/**
 * @brief The rules the draft breaks as an edit of the servo's image.
 *
 * @param p The programmer state.
 * @return The rules the draft breaks as an edit of the servo image.
 */
kst_rules_t kst_prog_rules(const kst_prog_t *p);

/* --- editing ---------------------------------------------------------------- */

/**
 * @brief The raw values of @p id the hard rules accept in the draft as it is,
 *        as the lowest and the highest.
 *
 * False when none is accepted, the field is
 * not editable or no servo was read.
 *
 * @param p  The programmer state.
 * @param id The field.
 * @param lo Receives the lowest raw value accepted.
 * @param hi Receives the highest raw value accepted.
 * @return false when none is accepted, the field is not editable or no servo
 *         was read.
 */
bool kst_prog_range(const kst_prog_t *p, kst_field_id_t id, uint16_t *lo,
                    uint16_t *hi);

/**
 * @brief Set a field of the draft to @p raw.
 *
 * @param p   The programmer state.
 * @param id  The field.
 * @param raw The raw value to set.
 * @return The result of the edit.
 */
kst_prog_edit_t kst_prog_edit(kst_prog_t *p, kst_field_id_t id, uint16_t raw);
/**
 * @brief One raw count up (@p dir above 0) or down.
 *
 * @param p   The programmer state.
 * @param id  The field.
 * @param dir The direction: above 0 counts up, else down.
 * @return The result of the edit.
 */
kst_prog_edit_t kst_prog_step(kst_prog_t *p, kst_field_id_t id, int dir);
/**
 * @brief The soft rules the edit that waits asks a confirmation for; 0 while
 *        none waits.
 *
 * They are the rules it breaks and the draft does not, and
 *  the rules of a field it moves further from kst_prog_soft_base() than
 *  the value last confirmed for that field, or to its other side.  A value
 *  the servo holds counts as confirmed.
 *
 * @param p The programmer state.
 * @return The soft rules the waiting edit asks a confirmation for, or 0.
 */
kst_rules_t kst_prog_soft_rules(const kst_prog_t *p);
/**
 * @brief The image the soft rules compare with: the backup of the IOmcu's
 *        session, the servo as the session first read it.
 *
 * The servo as last
 *  read while that backup is not held or is another servo's
 *  (kst_prog_backup_other()).
 *
 * @param p The programmer state.
 * @return The image the soft rules compare with.
 */
const kst_image_t *kst_prog_soft_base(const kst_prog_t *p);
/**
 * @brief The fields of @p img that differ from the servo and are at fault for a
 *        soft rule: the rule goes when the field takes the value of
 *        kst_prog_soft_base().
 *
 * @param p   The programmer state.
 * @param img The image to check.
 * @return The fields of img at fault for a soft rule.
 */
kst_field_set_t kst_prog_soft_fields(const kst_prog_t *p,
                                     const kst_image_t *img);
/**
 * @brief The image the edit that waits would leave.
 *
 * @param p The programmer state.
 * @return The image the waiting edit would leave.
 */
const kst_image_t *kst_prog_soft_image(const kst_prog_t *p);
/**
 * @brief Confirms the waiting soft edit.
 *
 * @param p The programmer state.
 */
void kst_prog_soft_confirm(kst_prog_t *p);
/**
 * @brief Drops the waiting soft edit.
 *
 * @param p The programmer state.
 */
void kst_prog_soft_cancel(kst_prog_t *p);
/**
 * @brief The draft becomes the servo's image again, and no value counts as
 *        confirmed.
 *
 * @param p The programmer state.
 */
void kst_prog_discard(kst_prog_t *p);

/* --- operations: KST_PROG_MSG_NONE when taken, else the message set ------- */

/**
 * @brief Take programming port @p port, from 0, with the supply set by
 *        kst_prog_supply_set().
 *
 * KST_PROG_MSG_NEED_TARGET while the feed's target
 *  is not that port, KST_PROG_MSG_NEED_DISARM on an armed bench.
 *
 * A servo enters programming mode only from a power-up.  With a PD mini
 * answering and its output on (kst_prog_connect_cycles()) the port's
 * REBOOT switches the output off and keeps it off for KST_PORT_RAIL_OFF_MS
 * from the read that shows it off; an ABORT sent while the port reads
 * RAIL_OFF keeps it off after that, and the ENTER that follows switches it
 * on at the lower of the programming voltage and the voltage it was set
 * to, and the lower of the two current limits, and sends the sync
 * KST_PORT_RAIL_WAIT_MS after it reads on.  With the output off the ENTER
 * powers the servo up as it is.  Without a PD mini the servo's supply is
 * not the bench's, and the ENTER goes as it is: the operator powers the
 * servo up before CONNECT.  A STOP, the operator's ABORT, an ARM or a
 * target change while the output is off sends no ENTER, and the output
 * stays off.
 *
 * @param p    The programmer state.
 * @param port The programming port, from 0.
 * @return The message for the connect attempt.
 */
kst_prog_msg_id_t kst_prog_connect(kst_prog_t *p, uint8_t port);
/**
 * @brief Whether CONNECT switches the supply off and on first: a PD mini
 *        answers and the panel reads its output on.
 *
 * @param p The programmer state.
 * @return true when CONNECT cycles the supply before the ENTER.
 */
bool kst_prog_connect_cycles(const kst_prog_t *p);
/**
 * @brief The bench is armed or asked to arm.
 *
 * Programming and an armed bench are
 *  exclusive: while this is set or the feed reports armed outputs no port
 *  is taken and no operation that sends frames starts
 *  (KST_PROG_MSG_NEED_DISARM), and the caller takes no arm while a port is
 *  held.
 *
 * @param p     The programmer state.
 * @param armed true when the bench is armed or asked to arm.
 */
void kst_prog_bench_armed(kst_prog_t *p, bool armed);
/**
 * @brief Whether the bench takes no arm by what the link module reads: an
 *        operation is on its way or runs, the port is read and is not in PWM,
 *        or  the page answers and is not read yet (KST_LINK_SYNC).
 *
 * False where no
 *  page answers: nothing can hold a port there.
 *
 * @param rd The link module's readout.
 * @return true when the bench takes no arm by what the link module reads.
 */
bool kst_prog_port_taken(const kst_link_readout_t *rd);
/**
 * @brief Whether nothing but the programmer reaches the port: an operation of
 *        this model is on its way or a session is open (ENTERING, PROGRAMMING,
 *        WORKING, LEAVING), or kst_prog_port_taken() says so of the readout
 *        last  fed.
 *
 * The tester's target and the port's pulse stay as they are for as
 *  long (settings_held(), prog_link_hold()).
 *
 * @param p The programmer state.
 * @return true when nothing but the programmer reaches the port.
 */
bool kst_prog_holds_port(const kst_prog_t *p);
/**
 * @brief What an ENTER asks of the PD mini: @p mv and its current limit @p ma.
 *
 * The port checks the range.  KST_PROG_SUPPLY_MV and KST_PROG_SUPPLY_MA
 *  after kst_prog_init().
 *
 * @param p  The programmer state.
 * @param mv The voltage an ENTER asks for, in mV.
 * @param ma The current limit an ENTER asks for, in mA.
 */
void kst_prog_supply_set(kst_prog_t *p, uint16_t mv, uint16_t ma);
/**
 * @brief The highest voltage and current limit the operator lets the supply
 *        give (the SUPPLY screen's limits), 0 for none.
 *
 * kst_prog_connect() is
 *  KST_PROG_MSG_SUPPLY_CAPS and asks nothing while a value of
 *  kst_prog_supply_set() is above its limit: a session never runs at
 *  values other than the ones shown.  None after kst_prog_init().
 *
 * @param p      The programmer state.
 * @param max_mv The highest voltage the supply may give, in mV, or 0 for none.
 * @param max_ma The highest current limit, in mA, or 0 for none.
 */
void kst_prog_supply_caps(kst_prog_t *p, uint16_t max_mv, uint16_t max_ma);
/**
 * @brief The lowest voltage a session runs at, mV; 0 for none.
 *
 * An ENTER lowers
 *  a PD mini output that is on to the session's voltage or leaves it, and
 *  never raises it: kst_prog_connect() is KST_PROG_MSG_REF +
 *  LINK_KST_REF_SUPPLY_LOW and asks nothing while the feed reads an output
 *  that is on (supply_read) and set (set_mv) under @p min_mv.  The port of
 *  link version 4.15 refuses the same ENTER by that name; an IOmcu
 *  built before the refusal takes it, so the panel holds the floor for
 *  every peer.  None after kst_prog_init().
 *
 * @param p      The programmer state.
 * @param min_mv The lowest voltage a session runs at, in mV, or 0 for none.
 */
void kst_prog_supply_floor(kst_prog_t *p, uint16_t min_mv);
/**
 * @brief The voltage an ENTER asks for, in mV.
 *
 * @param p The programmer state.
 * @return The voltage, in mV.
 */
uint16_t kst_prog_supply_mv(const kst_prog_t *p);
/**
 * @brief The current limit an ENTER asks for, in mA.
 *
 * @param p The programmer state.
 * @return The current limit, in mA.
 */
uint16_t kst_prog_supply_ma(const kst_prog_t *p);
/**
 * @brief Asks for a read of the servo.
 *
 * @param p The programmer state.
 * @return The message for the read.
 */
kst_prog_msg_id_t kst_prog_read(kst_prog_t *p);
/**
 * @brief Asks for a check of the servo's image.
 *
 * @param p The programmer state.
 * @return The message for the check.
 */
kst_prog_msg_id_t kst_prog_verify(kst_prog_t *p);
/**
 * @brief The switch.
 *
 * On is taken with a servo read that passes the
 *  fingerprint, and by a locked session that holds its backup.
 *
 * @param p  The programmer state.
 * @param on true to switch the write switch on.
 * @return The message for the switch.
 */
kst_prog_msg_id_t kst_prog_enable(kst_prog_t *p, bool on);
/**
 * @brief What kst_prog_enable() would refuse the switch for, and nothing said;
 *        KST_PROG_MSG_NONE where it goes on.
 *
 * @param p The programmer state.
 * @return What enable would refuse the switch for, or KST_PROG_MSG_NONE.
 */
kst_prog_msg_id_t kst_prog_enable_check(const kst_prog_t *p);
/**
 * @brief The session is locked and its backup is held: the switch is taken with
 *        no servo read, and the restore of that backup is the one write that
 *        goes.
 *
 * Every other write waits for a read and for the lock to end.
 *
 * @param p The programmer state.
 * @return true when the switch is taken with no servo read and only the restore
 *         goes.
 */
bool kst_prog_restore_only(const kst_prog_t *p);
/**
 * @brief Whether kst_prog_write() would be sent, without sending it.
 *
 * @param p The programmer state.
 * @return The message the write would give.
 */
kst_prog_msg_id_t kst_prog_write_check(kst_prog_t *p);
/**
 * @brief Write the draft.
 *
 * The first write of a session saves the servo's image
 *  to a file before it and is not sent without one; the journal goes to its
 *  file before every write.
 *
 * @param p The programmer state.
 * @return The message for the write.
 */
kst_prog_msg_id_t kst_prog_write(kst_prog_t *p);
/**
 * @brief The same for kst_prog_release().
 *
 * @param p The programmer state.
 * @return The message the release would give.
 */
kst_prog_msg_id_t kst_prog_release_check(kst_prog_t *p);
/**
 * @brief Releases the servo.
 *
 * @param p The programmer state.
 * @return The message for the release.
 */
kst_prog_msg_id_t kst_prog_release(kst_prog_t *p);
/**
 * @brief Whether @p from holds an image that differs from the servo.
 *
 * @param p    The programmer state.
 * @param from The source of the image.
 * @return true when from holds an image that differs from the servo.
 */
bool kst_prog_restore_offered(const kst_prog_t *p, kst_prog_from_t from);
/**
 * @brief Prepare a restore.
 *
 * For KST_PROG_FROM_JOURNAL the draft becomes the
 * servo's image with the editable fields of the source.
 *
 * @param p    The programmer state.
 * @param from The source of the image.
 * @param out  Receives the plan.
 * @return The message for the plan.
 */
kst_prog_msg_id_t kst_prog_restore_plan(kst_prog_t *p, kst_prog_from_t from,
                                        kst_prog_restore_t *out);
/**
 * @brief Run the restore prepared.
 *
 * @param p    The programmer state.
 * @param plan The plan from kst_prog_restore_plan().
 * @return The message for the restore.
 */
kst_prog_msg_id_t kst_prog_restore(kst_prog_t *p,
                                   const kst_prog_restore_t *plan);
/**
 * @brief Discard the journal.
 *
 * KST_PROG_MSG_START + KST_LINK_START_BUSY, and
 *  nothing discarded, while an operation is on its way.
 *
 * @param p The programmer state.
 */
void kst_prog_forget(kst_prog_t *p);
/* --- the limit run ----------------------------------------------------------- */

/*
 * A limit run (servo_limrun.h) programs LEFT ANGLE and RIGHT ANGLE, and
 * PULSE LOWER and PULSE UPPER where its SERVO PULSES setting is 1000-2000,
 * over many sessions: a session per write.  These four are the run's
 * fields.  The run keeps the servo as its first session read it:
 *
 *   - RESTORE offers it (KST_PROG_FROM_RUN) while the servo read differs
 *     from it in the run's fields and in no other: an ordinary write of
 *     those fields, checked as every write is.
 *   - A later session of the run whose servo differs from it in the run's
 *     fields at most saves no file before its write: the file of the
 *     first write holds the servo as the run found it.
 *
 * The hold is in memory: a start of the panel forgets it.
 */

/**
 * @brief Hold the servo as read now as the run's.
 *
 * False, and nothing held, with
 *  no servo read.
 *
 * @param p The programmer state.
 * @return true when the hold was taken, false with no servo read.
 */
bool kst_prog_run_hold(kst_prog_t *p);
/**
 * @brief The run's first write was sent, whether it ended well or not: the file
 *        it saved, if any, holds the servo as held.
 *
 * @param p The programmer state.
 */
void kst_prog_run_written(kst_prog_t *p);
/**
 * @brief The run is over: the servo stays held for RESTORE, and a later
 *        session's first write saves its own file again.
 *
 * @param p The programmer state.
 */
void kst_prog_run_close(kst_prog_t *p);
/**
 * @brief Nothing held.
 *
 * @param p The programmer state.
 */
void kst_prog_run_drop(kst_prog_t *p);
/**
 * @brief The servo the run held, or NULL.
 *
 * @param p The programmer state.
 * @return The held image, or NULL when no run holds one.
 */
const kst_image_t *kst_prog_run_image(const kst_prog_t *p);
/**
 * @brief The file of that servo; "" for none: no card, or the first write not
 *        ended yet.
 *
 * @param p The programmer state.
 * @return The file name the run wrote, or an empty string.
 */
const char *kst_prog_run_backup(const kst_prog_t *p);
/**
 * @brief The servo read differs from the run's in LEFT ANGLE, RIGHT ANGLE,
 *        PULSE LOWER and PULSE UPPER at most.
 *
 * @param p The programmer state.
 * @return true when the servo differs from the held image only in the run
 *         fields.
 */
bool kst_prog_run_matches(const kst_prog_t *p);

/**
 * @brief End the session: the port goes back to PWM.
 *
 * The one place a session is ended by the operator.  Where
 * kst_prog_leave_cycles() holds, the port's REBOOT switches the supply off
 * for KST_PORT_RAIL_OFF_MS and on again, holds the line low for
 * KST_PORT_RAIL_WAIT_MS and gives the pin to PWM: a written value acts
 * after the servo's next power-up.  Otherwise LEAVE leaves the supply as it
 * is.  An IOmcu below link 4.15 has no LEAVE: with a PD mini answering
 * and no cycle owed the answer is KST_PROG_MSG_LEAVE_SUPPLY.
 *
 * @param p The programmer state.
 * @return The message id.
 */
kst_prog_msg_id_t kst_prog_leave(kst_prog_t *p);
/**
 * @brief Whether a LEAVE now switches the supply off and on before PWM.
 *
 * True in programming mode with a PD mini answering, its output read on,
 * the port asking for no read and not locked, and a write owed its power
 * cycle (kst_prog_cycle_owed()).
 *
 * @param p The programmer state.
 * @return true when kst_prog_leave() cycles the supply.
 */
bool kst_prog_leave_cycles(const kst_prog_t *p);
/**
 * @brief Whether a write of this session waits for the servo's next power-up
 *        to act: a write, restore or pairing release was sent, or the session
 *        began before this model saw it and its writes are not known.
 *
 * @param p The programmer state.
 * @return true in programming mode with such a write.
 */
bool kst_prog_cycle_owed(const kst_prog_t *p);
/**
 * @brief The wait between the end of a session and the first PWM pulse, up to
 *        KST_PROG_WAIT_MAX_MS.
 *
 * False, and nothing set, above it.
 *
 * @param p  The programmer state.
 * @param ms The wait, in ms.
 * @return false when ms is above KST_PROG_WAIT_MAX_MS.
 */
bool kst_prog_wait_set(kst_prog_t *p, uint32_t ms);
/**
 * @brief The wait before the first PWM pulse.
 *
 * @param p The programmer state.
 * @return The wait, in ms.
 */
uint16_t kst_prog_wait_ms(const kst_prog_t *p);
/**
 * @brief The operator's cancel.
 *
 * @param p The programmer state.
 */
void kst_prog_abort(kst_prog_t *p);

/* --- servo files ------------------------------------------------------------- */

/**
 * @brief Save the servo's image to a new file with @p name.
 *
 * The card's side
 *  says whether the name is one a file may have; a file is never
 *  replaced.
 *
 * @param p    The programmer state.
 * @param name The file name.
 * @return The message id.
 */
kst_prog_msg_id_t kst_prog_save(kst_prog_t *p, const char *name);
/**
 * @brief Ask for the list of files from file @p first on, counted from 0: up to
 *        KST_PROG_FILES_MAX of them.
 *
 * kst_prog_files_seq() moves when it is
 *  there.  Refused with KST_PROG_MSG_NEED_READ while the servo is in
 *  doubt.
 *
 * @param p     The programmer state.
 * @param first The index of the first file to list.
 * @return The message id.
 */
kst_prog_msg_id_t kst_prog_files(kst_prog_t *p, unsigned first);
/**
 * @brief The number of the last list's first file among the card's.
 *
 * @param p The programmer state.
 * @return The index.
 */
unsigned kst_prog_files_first(const kst_prog_t *p);
/**
 * @brief Load the file with @p name into the draft.
 *
 * Taken with a servo read that passes the fingerprint and is not in doubt,
 * and with the servo type set (kst_prog_type_set()):
 * KST_PROG_MSG_NEED_TYPE before it.  A file that states another type than
 * the one set, a file with a value that is not taken, a file that breaks a
 * hard rule and a file that is not whole change nothing.  A file that breaks
 * soft rules waits for kst_prog_soft_confirm().
 *
 * @param p    The programmer state.
 * @param name The file name.
 * @return The message id.
 */
kst_prog_msg_id_t kst_prog_load(kst_prog_t *p, const char *name);
/**
 * @brief A save, a list or a load is on its way.
 *
 * @param p The programmer state.
 * @return true while a card operation runs.
 */
bool kst_prog_card_busy(const kst_prog_t *p);
/**
 * @brief The sequence number of the file list.
 *
 * @param p The programmer state.
 * @return The sequence number, which moves when the list changes.
 */
uint32_t kst_prog_files_seq(const kst_prog_t *p);
/**
 * @brief The names of the last list, @p i from 0; "" beyond them.
 *
 * @param p The programmer state.
 * @param i The index in the list.
 * @return The name, or an empty string beyond the list.
 */
const char *kst_prog_file_name(const kst_prog_t *p, unsigned i);
/**
 * @brief That file's servo type as the card's side gave it; 0 none.
 *
 * @param p The programmer state.
 * @param i The index in the list.
 * @return The type, or 0 beyond the list.
 */
unsigned kst_prog_file_type(const kst_prog_t *p, unsigned i);
/**
 * @brief The servo type the operator set for this session: 6 for V6, 8 for
 *        V8, 0 while none is set.
 *
 * A save states it in the file, and a load is held to it.  The session's
 * end clears it.
 *
 * @param p The programmer state.
 * @return 6, 8, or 0.
 */
unsigned kst_prog_type(const kst_prog_t *p);
/**
 * @brief The type 20k_sel of the servo read suggests, 6 or 8; 0 without a
 *        servo read.
 *
 * A guess from a drive setting (kst_reg.h): shown as one until the operator
 * sets the type, and never written to a file or used to hold a load.
 *
 * @param p The programmer state.
 * @return 6, 8, or 0.
 */
unsigned kst_prog_type_guess(const kst_prog_t *p);
/**
 * @brief Set the servo type: @p type 6 for V6 or 8 for V8.
 *
 * Nothing is written to the servo: 20k_sel stays as read.  Taken with a
 * servo read and no card operation on its way; KST_PROG_MSG_NEED_READ
 * without a servo, KST_PROG_MSG_START + KST_LINK_START_BUSY while a save,
 * a list or a load runs, KST_PROG_MSG_START + KST_LINK_START_ARG for
 * another value.  The draft stays as it is.
 *
 * @param p    The programmer state.
 * @param type 6 or 8.
 * @return KST_PROG_MSG_NONE when set, else the message.
 */
kst_prog_msg_id_t kst_prog_type_set(kst_prog_t *p, unsigned type);
/**
 * @brief The entries of the last list and, when @p total is not NULL, the servo
 *        files on the card.
 *
 * @param p     The programmer state.
 * @param total Receives the total number of servo files on the card, or NULL.
 * @return The number of files listed.
 */
unsigned kst_prog_file_count(const kst_prog_t *p, unsigned *total);
/**
 * @brief The next preset name as the last list gave it; "" when none is left.
 *
 * @param p The programmer state.
 * @return The preset name.
 */
const char *kst_prog_preset_name(const kst_prog_t *p);
/**
 * @brief The file last saved or asked for.
 *
 * @param p The programmer state.
 * @return The name.
 */
const char *kst_prog_card_name(const kst_prog_t *p);
/**
 * @brief What the file last loaded is.
 *
 * @param p The programmer state.
 * @return The kind, as kst_prog_file_kind_t.
 */
kst_prog_file_kind_t kst_prog_load_kind(const kst_prog_t *p);
/**
 * @brief The fields the file last loaded gives a value for, and through  @p
 *        fields, when not NULL, the named fields a file has: 0 where the
 *        card's side did not count them.
 *
 * @param p      The programmer state.
 * @param fields Receives the field set, or NULL.
 * @return The number of named fields.
 */
unsigned kst_prog_load_named(const kst_prog_t *p, unsigned *fields);
/**
 * @brief The fields in which the file in the draft differs from the servo and
 *        that are never written; 0 once the servo is read again or the draft is
 *        discarded.
 *
 * @param p The programmer state.
 * @return The skipped fields.
 */
kst_field_set_t kst_prog_load_skipped(const kst_prog_t *p);
/**
 * @brief The edit that waits for kst_prog_soft_confirm() is a file.
 *
 * @param p The programmer state.
 * @return true when a soft edit waits and it came from a load.
 */
bool kst_prog_soft_is_load(const kst_prog_t *p);

/**
 * @brief A request for the card's side; false while there is none.
 *
 * @param p   The programmer state.
 * @param out Receives the request.
 * @return true when a request was taken.
 */
bool kst_prog_card_take(kst_prog_t *p, kst_prog_card_req_t *out);
/**
 * @brief What the card's side made of it.
 *
 * An answer to a request that is no
 *  longer awaited changes nothing, but for one: the OK of a save given up
 *  at KST_PROG_CARD_MS names its file and, for the servo as it was saved,
 *  is the session's backup.
 *
 * @param p    The programmer state.
 * @param done The answer of the card.
 */
void kst_prog_card_done(kst_prog_t *p, const kst_prog_card_done_t *done);

/* --- the journal's file ------------------------------------------------------ */

/**
 * @brief The file text is to be written: fill @p out and return its length;  0
 *        while nothing is to be written or @p n is too small.
 *
 * @param p   The programmer state.
 * @param out Receives the text.
 * @param n   The size of out, in bytes.
 * @return The number of bytes written to out; 0 when none is wanted.
 */
size_t kst_prog_file_take(kst_prog_t *p, char *out, size_t n);
/**
 * @brief How the write of the text kst_prog_file_take() gave went.
 *
 * The answer to a write's journal releases that write when the card
 * completed the file and the write's start gate holds as it is now: the
 * port in PROGRAMMING on the write's port, no STOP, the bench not ARMED and
 * ENABLE WRITE TO SERVO on.  Otherwise the write ends as one that did not
 * start: nothing goes to the servo, its record is cleared, and the message
 * names the gate that refused it, or KST_PROG_MSG_STORE_FAILED.
 *
 * @param p  The programmer state.
 * @param ok true when the card completed the file.
 */
void kst_prog_file_done(kst_prog_t *p, bool ok);
/**
 * @brief Whether @p text is a whole file: its version, its lines and its check
 *        value.
 *
 * Changes nothing and says nothing.
 *
 * @param text The file text.
 * @param len  The length of the text, in bytes.
 * @return true when the text parses.
 */
bool kst_prog_file_whole(const char *text, size_t len);
/**
 * @brief The file as found at start.
 *
 * False, with KST_PROG_MSG_FILE_BAD, for a
 *  text that is not whole.
 *
 * @param p    The programmer state.
 * @param text The file text.
 * @param len  The length of the text, in bytes.
 * @return true when the text is a whole valid file.
 */
bool kst_prog_file_load(kst_prog_t *p, const char *text, size_t len);
/**
 * @brief The text of the state as it is, for a caller that only reads.
 *
 * @param p   The programmer state.
 * @param out Receives the text.
 * @param n   The size of out, in bytes.
 * @return The number of bytes written; 0 when out is too small.
 */
size_t kst_prog_file_text(const kst_prog_t *p, char *out, size_t n);

#ifdef __cplusplus
}
#endif
