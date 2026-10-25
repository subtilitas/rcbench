/**
 * @file
 * @brief The panel's half of the KST (0x32) link page, protocol 4.13 to 4.15:
 *        the programming ports for KST and Chaservo servos.
 *
 * The session with the servo runs at the IOmcu
 * (shared/outputs/kst_port.h).  The panel names an operation, and for a
 * write the image the servo is to hold; it reads back where the port is,
 * how the operation ended and the servo's 32 registers.  An IOmcu
 * whose protocol minor is below KST_LINK_MINOR has no programming port
 * (minor 12 has the page and takes an output channel where this module
 * names a port) and nothing is sent to it: not a write and not a read.
 *
 * One operation at a time, kst_link_start():
 *
 *   1. for a write, a restore, a release and a verify the staged registers
 *      4 to 24 are written, 6 frames; for an ENTER the supply registers 26
 *      and 27, 1 frame; for a LEAVE the wait, register 28, 1 frame;
 *   2. the command, registers 0 to 3, one frame, with SEQ one above the
 *      page's.  A command without an answer is sent again as it was; the
 *      page takes it once.  After kst_link_abort() it is not sent again:
 *      the next read of registers 0 to 3 says whether the page took it;
 *   3. registers 0 to 3 are read: the command started the operation or the
 *      page says why not;
 *   4. registers 0 to 3 are read every KST_LINK_POLL_MS until the operation
 *      has ended.  A 4.14 IOmcu plans a WRITE and a RESTORE first,
 *      for 2,161 of its passes at most: the phase is KST_LINK_PLANNING
 *      while the page says so, and KST_LINK_RUNNING after it;
 *   5. registers 4 to 15 are read, the backup when the page holds one, and
 *      the image.  The page is left showing the image.
 *
 * kst_link_outcome() then hands out how it went, once.
 *
 * A request with no order of writes ends differently by the IOmcu's
 * minor.  4.14: KST_LINK_OUT_DONE with the result LINK_KST_RESULT_NO_PATH,
 * or LINK_KST_RESULT_TOO_COMPLEX when the planner's work was spent before
 * it knew.  4.13: KST_LINK_OUT_REFUSED with LINK_KST_REF_PLAN_NO_PATH.
 * Nothing was sent to the servo in each case.
 *
 * A LEAVE takes the port from PROGRAMMING to PWM without a supply cycle
 * (LINK_MINOR_KST_LEAVE, 4.15).  The request carries the wait with the line
 * low, leave_ms.  While the IOmcu waits the port reads
 * LINK_KST_LEAVING and the phase is KST_LINK_RUNNING; the page has no
 * register for the time left.  To an IOmcu below 4.15 nothing of a
 * LEAVE is sent: kst_link_start() answers KST_LINK_START_NO_OP.
 *
 * A write to the servo is sent only with write_enabled set in the request:
 * the state of the switch ENABLE WRITE TO SERVO.  Without it the request
 * is refused here and nothing is sent.  The IOmcu refuses a write
 * without the key on its own.
 *
 * kst_link_abort() is for STOP and for the operator's cancel: a request
 * whose command the page has not taken is dropped, and the command ABORT
 * follows whenever the page is known.  A command sent without an answer is
 * never sent again after it; the page's SEQ tells whether it was taken.
 *
 * Between operations registers 0 to 3 are read every KST_LINK_POLL_MS
 * while the port is out of PWM and every KST_LINK_IDLE_MS while it is
 * not.
 *
 * Pure C, no link of its own: the caller makes each exchange and reports
 * how it went.  Control task only.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "kst_reg.h"
#include "kst_session.h"
#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The protocol minor whose KST page names programming ports. */
#define KST_LINK_MINOR LINK_MINOR_PROG

/**
 * @brief Registers 0 to 3 are due this long after the last read while an
 *        operation runs or the port is out of PWM.
 */
#define KST_LINK_POLL_MS 50u
/** @brief And this long after it while the port is in PWM. */
#define KST_LINK_IDLE_MS 500u

/**
 * @brief Exchanges without an answer, in a row, after which an operation is
 *        given up.
 */
#define KST_LINK_RETRIES 3u

/** @brief What came of an exchange: ACK or DATA, a NACK's reason, or none. */
#define KST_LINK_NO_ANSWER (-1)
#define KST_LINK_ACK       0

/** @brief Where the module is. */
typedef enum {
    KST_LINK_DOWN = 0,   /**< no IOmcu answers                      */
    KST_LINK_NO_PAGE,    /**< the IOmcu has no KST page             */
    KST_LINK_SYNC,       /**< the page has not been read yet              */
    KST_LINK_READY,      /**< kst_link_start() is taken                   */
    KST_LINK_SENDING,    /**< the staged registers and the command go out */
    KST_LINK_RUNNING,    /**< the operation runs at the IOmcu       */
    KST_LINK_FETCHING,   /**< the results and the images are read         */
    KST_LINK_PLANNING,   /**< the operation runs at the IOmcu and has
                              sent no frame yet: its plan is being built.
                              KST_LINK_RUNNING follows                    */
} kst_link_phase_t;

/** @brief What kst_link_start() and kst_link_fetch() answer. */
typedef enum {
    KST_LINK_START_OK = 0,
    KST_LINK_START_DOWN,       /**< no IOmcu answers                */
    KST_LINK_START_NO_PAGE,    /**< the IOmcu has no KST page       */
    KST_LINK_START_BUSY,       /**< the phase is not KST_LINK_READY       */
    KST_LINK_START_ARG,        /**< no such operation or port             */
    KST_LINK_START_NO_ENABLE,  /**< a write with write_enabled clear      */
    KST_LINK_START_NO_IMAGE,   /**< a write or a release with no image
                                    read to plan from                     */
    KST_LINK_START_NO_OP,      /**< the IOmcu's minor does not have
                                    the operation: a LEAVE below 4.15     */
} kst_link_start_t;

/** @brief An operation as the screen asks for it. */
typedef struct {
    link_kst_op_t op;          /**< ENTER, READ_ALL, WRITE, RESTORE,
                                    RELEASE, VERIFY, POWER_CYCLED, REBOOT
                                    or LEAVE                              */
    uint8_t  port;             /**< the programming port, 0 to
                                    LINK_PG_PORTS_MAX - 1                 */
    uint16_t supply_mv;        /**< ENTER: what the port asks of the PD
                                    mini, mV                              */
    uint16_t supply_ma;        /**< ENTER: and its current limit, mA      */
    uint16_t leave_ms;         /**< LEAVE: the wait with the line low before
                                    the port is in PWM, 0 to
                                    LINK_KST_LEAVE_MS_MAX ms              */
    bool     write_enabled;    /**< ENABLE WRITE TO SERVO is on           */
    bool     confirm;          /**< RESTORE: the operator confirmed a
                                    restore whose result cannot be checked */
    kst_image_t image;         /**< WRITE: the target.  RESTORE: the
                                    backup.  VERIFY: what is expected     */
    kst_field_set_t unlock;    /**< WRITE: fields beyond the editable class */
    uint32_t id;               /**< the caller's number of the request: the
                                    outcome of the operation carries it   */
} kst_link_request_t;

/** @brief How an operation went. */
typedef enum {
    KST_LINK_OUT_NONE = 0,
    KST_LINK_OUT_DONE,       /**< it ran; result says how it ended        */
    KST_LINK_OUT_REFUSED,    /**< the page took the command and started
                                  nothing; refusal says why               */
    KST_LINK_OUT_NACK,       /**< the page refused a frame; nack says why */
    KST_LINK_OUT_NO_ANSWER,  /**< KST_LINK_RETRIES exchanges unanswered,
                                  or the link went                        */
    KST_LINK_OUT_RESTARTED,  /**< the page's SEQ is not the panel's, or
                                  its operation went back to NONE: the
                                  IOmcu started again               */
    KST_LINK_OUT_DROPPED,    /**< kst_link_abort() before the page took
                                  the command; nothing of it ran */
} kst_link_outcome_kind_t;

/**
 * @brief The outcome of an operation that ended, as the link module reports it.
 */
typedef struct {
    kst_link_outcome_kind_t kind;
    link_kst_op_t      op;
    uint32_t id;                  /**< kst_link_request_t.id of the
                                       operation that ended               */
    link_kst_refusal_t refusal;   /**< KST_LINK_OUT_REFUSED               */
    uint8_t  nack;                /**< KST_LINK_OUT_NACK: link_nack_t     */
    bool     taken;               /**< the page acknowledged the command:
                                       the operation may have run         */
    uint8_t  result;              /**< KST_LINK_OUT_DONE: the low byte of
                                       LINK_KS_RESULT, a kst_ses_t value
                                       (0 to 20) or LINK_KST_RESULT_SUPPLY,
                                       _NO_PATH or _TOO_COMPLEX (32 to 34);
                                       a byte, since the enum does not
                                       hold the last three                */
    uint8_t  fail_reg;            /**< the register result names; 0xFF none */
    uint8_t  steps_done;          /**< writes of the plan done            */
    uint16_t frames;              /**< frames the operation sent, to 4095 */
    uint32_t diff_regs;           /**< bit n: register n differs          */
    uint32_t bad_regs;            /**< bit n: register n has no 3 equal reads */
} kst_link_outcome_t;

/**
 * @brief The screen's copy: a plain value the control task fills and the
 *        renderer reads.
 */
typedef struct {
    kst_link_phase_t phase;
    bool     known;            /**< the fields below were read            */
    bool     was_held;         /**< the status last read on this link had
                                    the port out of PWM: what stands while
                                    the status is not known               */
    link_kst_state_t port;     /**< where the port is                     */
    uint8_t  port_no;          /**< which port: the one last commanded    */
    uint16_t flags;            /**< LINK_KS_F_*                           */
    link_kst_op_t op;          /**< the last operation the port started   */
    uint16_t frames;           /**< frames it has sent, to 4095           */
    bool     have_image;       /**< kst_link_image() has one              */
    bool     have_backup;               /**< kst_link_backup() has one */
    bool     have_results;     /**< the fields below were read            */
    uint16_t fp_rules;         /**< bit n: fingerprint rule n is broken   */
    uint32_t fp_regs;          /**< bit n: register n deviates from it    */
    uint16_t half_min_ns;      /**< the servo's half-cell, 0 none measured */
    uint16_t half_max_ns;               /**< the servo's longest half-cell, ns; 0 none measured */
    uint16_t delay_min_us;     /**< reply delay, 0 none measured          */
    uint16_t delay_max_us;              /**< reply delay, longest, µs; 0 none measured */
} kst_link_readout_t;

/** @brief One exchange for the caller to make. */
typedef struct {
    bool     write;            /**< a write of regs; otherwise a read     */
    uint8_t  page;             /**< link_page_id_t                        */
    uint8_t off,                            /**< first register of the page             */
            n;                              /**< registers                              */
    uint16_t regs[4];          /**< a write's registers                   */
} kst_link_xfer_t;

/**
 * @brief The state of the KST link module.
 */
typedef struct {
    bool     up;
    bool     page;
    uint16_t minor;            /**< the IOmcu's protocol minor      */
    bool     known;            /**< status was read since the link came up */
    uint8_t  phase;            /**< kst_link_phase_t, never
                                    KST_LINK_PLANNING: that is
                                    KST_LINK_RUNNING here                 */
    uint8_t  pending;          /**< the exchange in flight                */
    uint8_t  tries;            /**< exchanges unanswered in a row         */
    bool     was_held;         /**< the status last read had the port out
                                    of PWM                                */

    uint16_t status[LINK_KS_W_CMD_FRAME];
    bool     polled;           /**< status_ms is meaningful               */
    bool     poll_now;         /**< a status read is owed at once         */
    uint32_t status_ms;        /**< when status was last asked            */
    uint16_t seq;              /**< the page's SEQ                        */

    /* The operation. */
    bool     active;           /**< an operation is on its way            */
    bool     verdict;          /**< its command was taken; the status that
                                    says what became of it is owed        */
    bool     taken;            /**< its command was acknowledged          */
    uint8_t  op;               /**< link_kst_op_t                         */
    uint32_t id;               /**< kst_link_request_t.id                 */
    uint8_t  stage_at;         /**< staged windows written, to STAGE_WINDOWS */
    uint16_t staged[LINK_KS_W_STAGED];
    bool     supply_due;       /**< an ENTER's supply registers are owed  */
    uint16_t supply[2];        /**< SUPPLY_MV, SUPPLY_MA                  */
    bool     leave_due;        /**< a LEAVE's wait register is owed       */
    uint16_t leave_ms;         /**< LEAVE_MS                              */
    uint16_t cmd_proto[LINK_KS_W_CMD_FRAME];  /**< the command less its SEQ */

    /* The command on its way, the operation's or an abort. */
    bool     cmd_live;
    bool     cmd_abort;
    bool     cmd_doubt;        /**< the operation's command went without an
                                    answer and an abort came: it is not
                                    sent again, and the next status read
                                    says whether the page took it        */
    uint16_t cmd[LINK_KS_W_CMD_FRAME];
    bool     abort_wanted;

    /* What was fetched. */
    uint8_t  fetch;            /**< the step of the fetch                 */
    uint16_t end_status[LINK_KS_W_CMD_FRAME]; /**< status as the operation
                                                   ended                  */
    bool     have_results;
    uint16_t results[LINK_KS_IMAGE - LINK_KS_FAIL];
    bool     have_image;
    uint16_t image_regs[LINK_KS_IMAGE_REGS];
    bool     have_backup;
    uint16_t backup_regs[LINK_KS_IMAGE_REGS];

    bool     outcome_new;
    kst_link_outcome_t outcome;
} kst_link_t;

/**
 * @brief Sets up the link state for the KST link page.
 *
 * @param k The link state to set up.
 */
void kst_link_init(kst_link_t *k);

/**
 * @brief The link went: an operation on its way ends as KST_LINK_OUT_NO_ANSWER
 *        and nothing is read or written until it comes up again.
 *
 * The images
 *  stay.
 *
 * @param k The link state.
 */
void kst_link_lost(kst_link_t *k);

/**
 * @brief An IOmcu answered with protocol minor @p minor.
 *
 * Below
 *  KST_LINK_MINOR it has no page and nothing is sent to it.
 *
 * @param k     The link state.
 * @param minor The IOmcu's protocol minor version.
 */
void kst_link_came_up(kst_link_t *k, uint16_t minor);

/**
 * @brief Whether the IOmcu has the page: false while none answers.
 *
 * @param k The link state.
 * @return true while the IOmcu answers and has the page.
 */
bool kst_link_has_page(const kst_link_t *k);

/**
 * @brief Start an operation.
 *
 * Taken in KST_LINK_READY only; any other answer
 * than KST_LINK_START_OK means nothing is sent.
 *
 * A LEAVE with leave_ms above LINK_KST_LEAVE_MS_MAX is
 * KST_LINK_START_ARG: the value is refused, not clamped.
 *
 * A WRITE and a RELEASE are planned at the IOmcu from the image it
 * read.  The request names the image the panel last fetched by its CRC
 * (cyclic redundancy check), and the IOmcu refuses with
 * LINK_KST_REF_START when it holds another.
 *
 * @param k The link state.
 * @param r The request to start.
 * @return KST_LINK_START_OK when the operation is started, else the reason it
 *         is not.
 */
kst_link_start_t kst_link_start(kst_link_t *k, const kst_link_request_t *r);

/**
 * @brief Read the results and the images again without an operation: for a
 *        panel that started while the port is out of PWM.
 *
 * No outcome.
 *
 * @param k The link state.
 * @return The start result, as kst_link_start() gives it.
 */
kst_link_start_t kst_link_fetch(kst_link_t *k);

/**
 * @brief STOP, or the operator's cancel.
 *
 * A request whose command has not gone
 *  is dropped (KST_LINK_OUT_DROPPED).  A command that went without an
 *  answer is not sent again: a read of registers 0 to 3 follows.  A page
 *  at the SEQ before the command did not take it, which drops the request
 *  too; any other SEQ is an IOmcu that started again
 *  (KST_LINK_OUT_RESTARTED).  A page took it when
 *  it shows the command's SEQ and either the command's operation in
 *  LINK_KS_OP or a refusal.  The command's SEQ with operation NONE and no
 *  refusal is an IOmcu that started again, at SEQ 0 after 0xFFFF:
 *  KST_LINK_OUT_RESTARTED, never KST_LINK_OUT_DONE.  So is operation NONE
 *  at any SEQ after a status read that named an operation: the page keeps
 *  the last operation started until the IOmcu starts again.  A page
 *  that has taken no command reads SEQ 0 and NONE before and after a
 *  start, and its first command counts as dropped.  A page that took it
 *  runs the operation, and the ABORT ends it with KST_SES_ERR_ABORTED.
 *  The read unanswered KST_LINK_RETRIES times ends the request with
 *  KST_LINK_OUT_NO_ANSWER.  The command ABORT is sent as soon as the page
 *  is known; an operation that runs ends with KST_SES_ERR_ABORTED.  An
 *  ABORT the page refuses is followed by a read of registers 0 to 3 with
 *  the same checks: a page off the panel's SEQ, or with its operation back
 *  at NONE, ends the operation as KST_LINK_OUT_RESTARTED, never
 *  KST_LINK_OUT_DONE.  An ABORT refused with LINK_NACK_BAD_VALUE goes
 *  again after that read, with the page's SEQ.
 *
 * @param k The link state.
 */
void kst_link_abort(kst_link_t *k);

/**
 * @brief The next exchange owed at @p now_ms, into @p x; false for none.
 *
 * The
 *  caller makes the exchange and reports with kst_link_done().
 *
 * @param k      The link state.
 * @param now_ms The time, in ms.
 * @param x      Receives the exchange owed.
 * @return true when an exchange is owed.
 */
bool kst_link_next(kst_link_t *k, uint32_t now_ms, kst_link_xfer_t *x);

/**
 * @brief How the exchange kst_link_next() gave went: KST_LINK_ACK with the
 *        registers read (@p regs, x.n of them; NULL for a write), a NACK
 *        reason,  or KST_LINK_NO_ANSWER.
 *
 * @param k      The link state.
 * @param result The result: an acknowledgement, a NACK's reason or
 *               KST_LINK_NO_ANSWER.
 * @param regs   The registers read, or NULL for a write.
 */
void kst_link_done(kst_link_t *k, int result, const uint16_t *regs);

/**
 * @brief Where the module is.
 *
 * KST_LINK_PLANNING while the operation runs and
 *  the page last read says it plans.
 *
 * @param k The link state.
 * @return The phase of the module.
 */
kst_link_phase_t kst_link_phase(const kst_link_t *k);

/**
 * @brief How the last operation went, once: false while there is nothing new.
 *
 * @param k   The link state.
 * @param out Receives the outcome.
 * @return true when a new outcome was written to out.
 */
bool kst_link_outcome(kst_link_t *k, kst_link_outcome_t *out);

/**
 * @brief The screen's copy of the page.
 *
 * @param k The link state.
 * @param r Receives the copy of the page.
 */
void kst_link_readout(const kst_link_t *k, kst_link_readout_t *r);

/**
 * @brief The servo's registers as last fetched; false while there are none.
 *
 * @param k   The link state.
 * @param out Receives the servo's image.
 * @return true when out holds the image.
 */
bool kst_link_image(const kst_link_t *k, kst_image_t *out);

/**
 * @brief The backup the IOmcu's session holds, as last fetched.
 *
 * @param k   The link state.
 * @param out Receives the backup image.
 * @return true when out holds the backup.
 */
bool kst_link_backup(const kst_link_t *k, kst_image_t *out);

#ifdef __cplusplus
}
#endif
