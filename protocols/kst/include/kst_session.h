/*
 * KST servo programming protocol: the session with one servo.
 *
 * A session enters programming mode, reads the 32 registers, runs a write
 * plan with read-back and verifies the result.  It reaches the wire through
 * the 3 functions of kst_driver_t and through nothing else.
 *
 * Execution model: step-wise and non-blocking.  An operation is started by
 * kst_session_enter(), kst_session_read_all(), kst_session_write() or
 * kst_session_verify().  The caller then calls kst_session_step() until it
 * returns something other than KST_SES_BUSY.  kst_session_step() never
 * waits: it reads the clock, polls the driver and returns.  A call every
 * 1 ms or faster keeps the pacing near its minimum; a slower caller makes
 * the gaps longer and nothing else.  One operation runs at a time.
 *
 * Pacing, measured with the driver's clock from the end of a reply window:
 * KST_GAP_US before the next frame, KST_GAP_AFTER_FAULT_US when the window
 * held no valid reply.  A write is never repeated blind: a read always
 * follows it.
 *
 * What a session does not do: it does not stop PWM (pulse-width modulation)
 * on the pin, does not switch the servo's supply, does not check the supply
 * voltage and does not store a journal.  The caller does these.  step_index
 * is there for a journal.
 *
 * The session object holds all state.  Two sessions on two drivers run side
 * by side.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KST_SESSION_H
#define KST_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "kst_plan.h"
#include "kst_reg.h"
#include "kst_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Line low before the first frame of the entry sequence, 100 ms. */
#define KST_ENTRY_LOW_US 100000u
/** End of a reply window to the next frame, 3 ms. */
#define KST_GAP_US 3000u
/** The same after a window without a valid reply, 20 ms.  Inside the entry
 *  sequence the gap is KST_GAP_US: an unanswered read and an unanswered
 *  sync are its normal course. */
#define KST_GAP_AFTER_FAULT_US 20000u
/** The driver has this long after the end of the reply window to hand over
 *  the capture, 5 ms. */
#define KST_DRIVER_SLACK_US 5000u

/** Passes of a read over 0x00 to 0x1F, and the passes after a
 *  disagreement. */
#define KST_READ_PASSES     3u
#define KST_READ_PASSES_MAX 5u
/** Equal reads a register needs. */
#define KST_READ_EQUAL 3u
/** Attempts for one write before the session gives the step up. */
#define KST_WRITE_ATTEMPTS 3u

/**
 * The driver: the only way to the wire.
 *
 * Contract:
 *   - Outside a transaction the line is low.
 *   - start() clocks out frame->n_half half-cells of KST_HALF_CELL_NS each,
 *     taken from kst_frame_level(), drives low for KST_RELEASE_NS, switches
 *     to input and records every level change until @p window_ns after the
 *     frame's last edge.  It returns at once; false when it cannot start.
 *   - poll() returns false while the transaction runs.  When the window has
 *     ended it fills @p out and returns true, once.
 *   - now_us() is a free-running microsecond clock.  It may wrap; the
 *     session uses differences only.
 *
 * There is no delay function: the session waits by returning from
 * kst_session_step().
 */
typedef struct {
    void *ctx;
    bool (*start)(void *ctx, const kst_frame_t *frame, uint32_t window_ns);
    bool (*poll)(void *ctx, kst_capture_t *out);
    uint32_t (*now_us)(void *ctx);
} kst_driver_t;

typedef enum {
    KST_SES_OK = 0,
    KST_SES_BUSY,              /**< the operation runs; call kst_session_step() */
    KST_SES_ERR_BUSY,          /**< another operation runs */
    KST_SES_ERR_ARG,
    KST_SES_ERR_NO_SERVO,      /**< no reply to read, sync and read */
    KST_SES_ERR_NOT_IN_MODE,   /**< kst_session_enter() has not succeeded */
    KST_SES_ERR_READ,          /**< a register without 3 equal reads in 5 */
    KST_SES_ERR_NO_BACKUP,     /**< no image read in this session */
    KST_SES_ERR_FINGERPRINT,   /**< the layout fingerprint fails: read-only */
    KST_SES_ERR_LOCKED,        /**< an earlier write left the servo in doubt */
    KST_SES_ERR_BAD_PLAN,      /**< the plan is not one kst_plan.h builds */
    KST_SES_ERR_UNCONFIRMED,   /**< an unchecked restore without confirmation */
    KST_SES_ERR_CHANGED,       /**< the servo differs from the plan's start */
    KST_SES_ERR_WRITE_FAILED,  /**< a write did not take; nothing half written */
    KST_SES_ERR_ROLLED_BACK,   /**< a write did not take; the field is back at
                                    its old value */
    KST_SES_ERR_TORN,          /**< the roll-back failed too */
    KST_SES_ERR_UNINTENDED,    /**< a register changed that was not written */
    KST_SES_ERR_VERIFY,        /**< the servo differs from the expected image */
    KST_SES_ERR_DRIVER,        /**< start() refused or poll() never finished */
    KST_SES_ERR_ABORTED,       /**< kst_session_abort() */
} kst_ses_t;

/** Reply timing of the read frames since kst_session_init().  A drift is
 *  the early sign of a marginal line. */
typedef struct {
    uint32_t replies;        /**< transactions with a valid reply */
    uint32_t faults;         /**< transactions without one */
    uint32_t delay_min_ns;   /**< reads only */
    uint32_t delay_max_ns;
    uint32_t half_min_ns;
    uint32_t half_max_ns;
} kst_stats_t;

typedef struct {
    /* --- for the caller to read ------------------------------------------- */
    kst_image_t image;       /**< the servo as last known; valid with has_image */
    kst_image_t backup;      /**< the first image read; valid with has_backup */
    kst_fingerprint_t fingerprint;  /**< of the last image read */
    kst_stats_t stats;
    kst_reply_t last_reply;  /**< of the last transaction */
    uint32_t bad_regs;       /**< bit n: register n has no 3 equal reads */
    uint32_t diff_regs;      /**< bit n: register n differs from what the
                                  operation expected */
    uint8_t result;          /**< kst_ses_t of the last operation */
    uint8_t in_mode;         /**< the servo is in programming mode; the pin
                                  carries no PWM until the supply was off */
    uint8_t has_image;
    uint8_t has_backup;
    uint8_t fingerprint_ok;
    uint8_t locked;          /**< only a restore plan is accepted */
    uint8_t step_index;      /**< writes of the running plan that are done */

    /* --- private ----------------------------------------------------------- */
    kst_driver_t drv;
    kst_plan_t plan;         /**< the running plan, copied */
    kst_image_t expect;      /**< verify: expected; write: image so far */
    kst_image_t read;        /**< result of the read passes */
    kst_frame_t frame;
    uint8_t vals[KST_READ_PASSES_MAX][KST_REG_COUNT];
    uint32_t vals_ok[KST_READ_PASSES_MAX];
    uint32_t t_mark;         /**< clock at the end of the last window */
    uint32_t t_gap;          /**< wait from t_mark before the next frame */
    uint32_t t_start;
    uint32_t t_limit;
    int16_t undo;            /**< step being rolled back */
    int16_t undo_stop;
    uint8_t op;
    uint8_t phase;
    uint8_t t_state;
    uint8_t t_expect;
    uint8_t t_frame;
    uint8_t fault;
    uint8_t abort;
    uint8_t reading;
    uint8_t read_result;
    uint8_t pass;
    uint8_t passes;
    uint8_t reg;
    uint8_t attempt;
    uint8_t first_ok;
    uint8_t dirty;
    uint8_t undone;
} kst_session_t;

/** Prepare a session.  False when the driver lacks a function. */
bool kst_session_init(kst_session_t *s, const kst_driver_t *driver);

/**
 * Enter programming mode.  The caller has stopped PWM on the pin.
 *
 * Sequence: line low for KST_ENTRY_LOW_US; read 0x01; on no reply the sync
 * burst; on no acknowledge one more read of 0x01.  A servo already in the
 * mode answers the first read, or, when that read is lost, the last one: it
 * does not acknowledge a second sync.  No other bit rate is tried.
 *
 * The mode lasts until the servo's supply is removed.
 */
kst_ses_t kst_session_enter(kst_session_t *s);

/**
 * Read all 32 registers: 3 passes, all 3 equal for every register.  On any
 * disagreement 2 more passes; a register without 3 equal reads in 5 fails
 * the read.  A pass without a single reply ends the read with
 * KST_SES_ERR_NO_SERVO.
 *
 * The first image read in a session becomes the backup.  The result is
 * KST_SES_OK for a servo that fails the fingerprint too; fingerprint_ok and
 * fingerprint tell.
 */
kst_ses_t kst_session_read_all(kst_session_t *s);

/**
 * Run a plan.  The plan is copied.
 *
 * Refused before any frame: no programming mode, no backup, a plan that is
 * not consistent, an edit or release plan that writes 0x1D against its
 * kind, breaks a hard rule or starts from an image that fails the
 * fingerprint, a locked session, a restore whose target is not the backup,
 * an unchecked restore without @p confirm_unchecked.
 *
 * Then:
 *   1. Read all.  The servo must equal the plan's start image.
 *   2. Per write: the write frame, then 2 reads of that register.  Both
 *      equal to the value: next write.  Otherwise read all; a difference in
 *      any other register ends the plan with KST_SES_ERR_UNINTENDED; a
 *      register that does hold the value counts as written; else the write
 *      is repeated, KST_WRITE_ATTEMPTS in total.
 *   3. A write that fails for good: the writes back to the last settled
 *      step are undone in reverse order, each with 2 reads, and the failed
 *      register is written back when it holds neither value.  Read all
 *      confirms.  Result KST_SES_ERR_ROLLED_BACK, or
 *      KST_SES_ERR_WRITE_FAILED when nothing had to be undone.  The servo
 *      holds a valid image; image tells which.  An undo that fails 3 times:
 *      KST_SES_ERR_TORN.
 *   4. After the last write: read all against the plan's target.  All 32
 *      registers are compared, because a bit error in a frame's address
 *      field writes another register.
 *
 * A plan that ends with KST_SES_ERR_TORN, KST_SES_ERR_UNINTENDED,
 * KST_SES_ERR_VERIFY, or with a read, driver or abort result after its first
 * write frame, locks the session.  A restore plan that succeeds, or a verify
 * against the backup that succeeds, unlocks it.
 */
kst_ses_t kst_session_write(kst_session_t *s, const kst_plan_t *plan,
                            bool confirm_unchecked);

/** Read all and compare with @p expected, which is copied.  diff_regs names
 *  the registers that differ. */
kst_ses_t kst_session_verify(kst_session_t *s, const kst_image_t *expected);

/** Advance the running operation.  KST_SES_BUSY while it runs, then its
 *  result; the result again on every later call. */
kst_ses_t kst_session_step(kst_session_t *s);

/** End the running operation with KST_SES_ERR_ABORTED.  A transaction on
 *  the wire is finished first: a frame is never cut. */
void kst_session_abort(kst_session_t *s);

/** The servo's supply was off: it has left programming mode.  A running
 *  operation is aborted. */
void kst_session_power_cycled(kst_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* KST_SESSION_H */
