/*
 * The panel's half of the KST (0x32) link page, protocol 4.12: the
 * programming port for KST and Chaservo servos.
 *
 * The session with the servo runs at the coprocessor
 * (shared/outputs/kst_port.h).  The panel names an operation, and for a
 * write the image the servo is to hold; it reads back where the port is,
 * how the operation ended and the servo's 32 registers.  A coprocessor
 * whose protocol minor is below KST_LINK_MINOR has no such page and
 * nothing is sent to it: not a write and not a read.
 *
 * One operation at a time, kst_link_start():
 *
 *   1. for a write, a restore, a release and a verify the staged registers
 *      4 to 24 are written, 6 frames;
 *   2. the command, registers 0 to 3, one frame, with SEQ one above the
 *      page's.  A command without an answer is sent again as it was; the
 *      page takes it once;
 *   3. registers 0 to 3 are read: the command started the operation or the
 *      page says why not;
 *   4. registers 0 to 3 are read every KST_LINK_POLL_MS until the operation
 *      has ended;
 *   5. registers 4 to 15 are read, the backup when the page holds one, and
 *      the image.  The page is left showing the image.
 *
 * kst_link_outcome() then hands out how it went, once.
 *
 * A write to the servo is sent only with write_enabled set in the request:
 * the state of the switch ENABLE WRITE TO SERVO.  Without it the request
 * is refused here and nothing is sent.  The coprocessor refuses a write
 * without the key on its own.
 *
 * kst_link_abort() is for STOP and for the operator's cancel: a request
 * not yet sent is dropped, and the command ABORT follows whenever the page
 * is known.
 *
 * Between operations registers 0 to 3 are read every KST_LINK_POLL_MS
 * while the port holds a channel and every KST_LINK_IDLE_MS while it does
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

/** The protocol minor that has the KST page. */
#define KST_LINK_MINOR LINK_MINOR_KST

/** Registers 0 to 3 are due this long after the last read while an
 *  operation runs or the port holds a channel. */
#define KST_LINK_POLL_MS 50u
/** And this long after it while the port is in PWM. */
#define KST_LINK_IDLE_MS 500u

/** Exchanges without an answer, in a row, after which an operation is
 *  given up. */
#define KST_LINK_RETRIES 3u

/** What came of an exchange: ACK or DATA, a NACK's reason, or none. */
#define KST_LINK_NO_ANSWER (-1)
#define KST_LINK_ACK       0

/** Where the module is. */
typedef enum {
    KST_LINK_DOWN = 0,   /**< no coprocessor answers                      */
    KST_LINK_NO_PAGE,    /**< the coprocessor has no KST page             */
    KST_LINK_SYNC,       /**< the page has not been read yet              */
    KST_LINK_READY,      /**< kst_link_start() is taken                   */
    KST_LINK_SENDING,    /**< the staged registers and the command go out */
    KST_LINK_RUNNING,    /**< the operation runs at the coprocessor       */
    KST_LINK_FETCHING,   /**< the results and the images are read         */
} kst_link_phase_t;

/** What kst_link_start() and kst_link_fetch() answer. */
typedef enum {
    KST_LINK_START_OK = 0,
    KST_LINK_START_DOWN,       /**< no coprocessor answers                */
    KST_LINK_START_NO_PAGE,    /**< the coprocessor has no KST page       */
    KST_LINK_START_BUSY,       /**< the phase is not KST_LINK_READY       */
    KST_LINK_START_ARG,        /**< no such operation or channel          */
    KST_LINK_START_NO_ENABLE,  /**< a write with write_enabled clear      */
    KST_LINK_START_NO_IMAGE,   /**< a write or a release with no image
                                    read to plan from                     */
} kst_link_start_t;

/** An operation as the screen asks for it. */
typedef struct {
    link_kst_op_t op;          /**< ENTER, READ_ALL, WRITE, RESTORE,
                                    RELEASE, VERIFY or POWER_CYCLED       */
    uint8_t  channel;          /**< 0 to 15                               */
    bool     write_enabled;    /**< ENABLE WRITE TO SERVO is on           */
    bool     confirm;          /**< RESTORE: the operator confirmed a
                                    restore whose result cannot be checked */
    kst_image_t image;         /**< WRITE: the target.  RESTORE: the
                                    backup.  VERIFY: what is expected     */
    kst_field_set_t unlock;    /**< WRITE: fields beyond the editable class */
} kst_link_request_t;

/** How an operation went. */
typedef enum {
    KST_LINK_OUT_NONE = 0,
    KST_LINK_OUT_DONE,       /**< it ran; result says how it ended        */
    KST_LINK_OUT_REFUSED,    /**< the page took the command and started
                                  nothing; refusal says why               */
    KST_LINK_OUT_NACK,       /**< the page refused a frame; nack says why */
    KST_LINK_OUT_NO_ANSWER,  /**< KST_LINK_RETRIES exchanges unanswered,
                                  or the link went                        */
    KST_LINK_OUT_RESTARTED,  /**< the page's SEQ is not the panel's: the
                                  coprocessor started again               */
    KST_LINK_OUT_DROPPED,    /**< kst_link_abort() before the command went */
} kst_link_outcome_kind_t;

typedef struct {
    kst_link_outcome_kind_t kind;
    link_kst_op_t      op;
    link_kst_refusal_t refusal;   /**< KST_LINK_OUT_REFUSED               */
    uint8_t  nack;                /**< KST_LINK_OUT_NACK: link_nack_t     */
    kst_ses_t result;             /**< KST_LINK_OUT_DONE                  */
    uint8_t  fail_reg;            /**< the register result names; 0xFF none */
    uint8_t  steps_done;          /**< writes of the plan done            */
    uint16_t frames;              /**< frames the operation sent, to 4095 */
    uint32_t diff_regs;           /**< bit n: register n differs          */
    uint32_t bad_regs;            /**< bit n: register n has no 3 equal reads */
} kst_link_outcome_t;

/** The screen's copy: a plain value the control task fills and the
 *  renderer reads. */
typedef struct {
    kst_link_phase_t phase;
    bool     known;            /**< the fields below were read            */
    link_kst_state_t port;     /**< where the port's channel is           */
    uint8_t  channel;
    uint16_t flags;            /**< LINK_KS_F_*                           */
    link_kst_op_t op;          /**< the last operation the port started   */
    uint16_t frames;           /**< frames it has sent, to 4095           */
    bool     have_image;       /**< kst_link_image() has one              */
    bool     have_backup;
    bool     have_results;     /**< the fields below were read            */
    uint16_t fp_rules;         /**< bit n: fingerprint rule n is broken   */
    uint32_t fp_regs;          /**< bit n: register n deviates from it    */
    uint16_t half_min_ns;      /**< the servo's half-cell, 0 none measured */
    uint16_t half_max_ns;
    uint16_t delay_min_us;     /**< reply delay, 0 none measured          */
    uint16_t delay_max_us;
} kst_link_readout_t;

/** One exchange for the caller to make. */
typedef struct {
    bool     write;            /**< a write of regs; otherwise a read     */
    uint8_t  page;             /**< link_page_id_t                        */
    uint8_t  off, n;
    uint16_t regs[4];          /**< a write's registers                   */
} kst_link_xfer_t;

typedef struct {
    bool     up;
    bool     page;
    bool     known;            /**< status was read since the link came up */
    uint8_t  phase;            /**< kst_link_phase_t                      */
    uint8_t  pending;          /**< the exchange in flight                */
    uint8_t  tries;            /**< exchanges unanswered in a row         */

    uint16_t status[LINK_KS_W_CMD_FRAME];
    bool     polled;           /**< status_ms is meaningful               */
    bool     poll_now;         /**< a status read is owed at once         */
    uint32_t status_ms;        /**< when status was last asked            */
    uint16_t seq;              /**< the page's SEQ                        */

    /* The operation. */
    bool     active;           /**< an operation is on its way            */
    bool     verdict;          /**< its command was taken; the status that
                                    says what became of it is owed        */
    uint8_t  op;               /**< link_kst_op_t                         */
    uint8_t  stage_at;         /**< staged windows written, to STAGE_WINDOWS */
    uint16_t staged[LINK_KS_W_STAGED];
    uint16_t cmd_proto[LINK_KS_W_CMD_FRAME];  /**< the command less its SEQ */

    /* The command on its way, the operation's or an abort. */
    bool     cmd_live;
    bool     cmd_abort;
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

void kst_link_init(kst_link_t *k);

/** The link went: an operation on its way ends as KST_LINK_OUT_NO_ANSWER
 *  and nothing is read or written until it comes up again.  The images
 *  stay. */
void kst_link_lost(kst_link_t *k);

/** A coprocessor answered with protocol minor @p minor.  Below
 *  KST_LINK_MINOR it has no page and nothing is sent to it. */
void kst_link_came_up(kst_link_t *k, uint16_t minor);

/** Whether the coprocessor has the page: false while none answers. */
bool kst_link_has_page(const kst_link_t *k);

/**
 * Start an operation.  Taken in KST_LINK_READY only; any other answer
 * than KST_LINK_START_OK means nothing is sent.
 *
 * A WRITE and a RELEASE are planned at the coprocessor from the image it
 * read.  The request names the image the panel last fetched by its CRC
 * (cyclic redundancy check), and the coprocessor refuses with
 * LINK_KST_REF_START when it holds another.
 */
kst_link_start_t kst_link_start(kst_link_t *k, const kst_link_request_t *r);

/** Read the results and the images again without an operation: for a
 *  panel that started while the port holds a channel.  No outcome. */
kst_link_start_t kst_link_fetch(kst_link_t *k);

/** STOP, or the operator's cancel.  A request whose command has not gone
 *  is dropped (KST_LINK_OUT_DROPPED).  The command ABORT is sent as soon as
 *  the page is known; an operation that runs ends with
 *  KST_SES_ERR_ABORTED. */
void kst_link_abort(kst_link_t *k);

/** The next exchange owed at @p now_ms, into @p x; false for none.  The
 *  caller makes the exchange and reports with kst_link_done(). */
bool kst_link_next(kst_link_t *k, uint32_t now_ms, kst_link_xfer_t *x);

/** How the exchange kst_link_next() gave went: KST_LINK_ACK with the
 *  registers read (@p regs, x.n of them; NULL for a write), a NACK reason,
 *  or KST_LINK_NO_ANSWER. */
void kst_link_done(kst_link_t *k, int result, const uint16_t *regs);

/** Where the module is. */
kst_link_phase_t kst_link_phase(const kst_link_t *k);

/** How the last operation went, once: false while there is nothing new. */
bool kst_link_outcome(kst_link_t *k, kst_link_outcome_t *out);

/** The screen's copy of the page. */
void kst_link_readout(const kst_link_t *k, kst_link_readout_t *r);

/** The servo's registers as last fetched; false while there are none. */
bool kst_link_image(const kst_link_t *k, kst_image_t *out);

/** The backup the coprocessor's session holds, as last fetched. */
bool kst_link_backup(const kst_link_t *k, kst_image_t *out);

#ifdef __cplusplus
}
#endif
