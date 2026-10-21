/**
 * @file
 * @brief KST servo programming protocol: the session with one servo.
 *
 * Implements the programming protocol of KST and Chaservo servos.  KST and
 * Chaservo are names or trademarks of their respective owners; they are used
 * here only to say which servos this module works with (README.md, "Legal
 * notice and disclaimer").
 *
 * A session enters programming mode, reads the 32 registers, runs a write
 * plan with read-back and verifies the result.  It reaches the wire through
 * the 3 functions of kst_driver_t and through nothing else.
 *
 * Execution model: step-wise and non-blocking.  An operation is started by
 * kst_session_enter(), kst_session_read_all(), kst_session_verify(),
 * kst_session_edit(), kst_session_restore(), kst_session_release_pairing()
 * or kst_session_write().  The caller then calls kst_session_step() until
 * it returns something other than KST_SES_BUSY.  kst_session_step() never
 * waits: it reads the clock, polls the driver and returns.  A call every
 * 1 ms or faster keeps the pacing near its minimum; a slower caller makes
 * the gaps longer and nothing else.  One operation runs at a time.
 *
 * kst_session_edit() and kst_session_restore() plan their writes inside the
 * session, in slices: while planning is 1, one kst_session_step() call does
 * KST_PLAN_SLICE_UNITS units of planner work, sends nothing and returns
 * KST_SES_BUSY.  The planner runs once per operation and the session runs
 * the plan it built there and no other.
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

/** @brief Line low before the first frame of the entry sequence, 100 ms. */
#define KST_ENTRY_LOW_US 100000u
/** @brief End of a reply window to the next frame, 3 ms. */
#define KST_GAP_US 3000u
/**
 * @brief The same after a window without a valid reply, 20 ms.
 *
 * Inside the entry sequence the gap is KST_GAP_US: an unanswered read and an
 * unanswered sync are its normal course.
 */
#define KST_GAP_AFTER_FAULT_US 20000u
/**
 * @brief The driver has this long after the end of the reply window to hand
 *        over the capture, 5 ms.
 */
#define KST_DRIVER_SLACK_US 5000u

/**
 * @brief Passes of a read over 0x00 to 0x1F, and the passes after a
 *        disagreement.
 */
#define KST_READ_PASSES     3u
/** @brief Most passes one read takes: the 3 passes and 2 after a
 *        disagreement. */
#define KST_READ_PASSES_MAX 5u
/** @brief Equal reads a register needs. */
#define KST_READ_EQUAL 3u
/** @brief Attempts for one write before the session gives the step up. */
#define KST_WRITE_ATTEMPTS 3u

/**
 * @brief The driver: the only way to the wire.
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
    void *ctx;              /**< driver state, passed to each function */
    /**
     * @brief Clocks out @p frame and records the line up to @p window_ns; false
     *        when it cannot start.
     */
    bool (*start)(void *ctx, const kst_frame_t *frame, uint32_t window_ns);
    /**
     * @brief Reports the window's end: false while the transaction runs; true
     *        once, with @p out filled.
     */
    bool (*poll)(void *ctx, kst_capture_t *out);
    uint32_t (*now_us)(void *ctx);  /**< free-running microseconds, may wrap */
} kst_driver_t;

/** @brief Result of a session operation. */
typedef enum {
    KST_SES_OK = 0,            /**< the operation finished without error */
    KST_SES_BUSY,              /**< the operation runs; call
                                    kst_session_step() */
    KST_SES_ERR_BUSY,          /**< another operation runs */
    KST_SES_ERR_ARG,           /**< bad argument */
    KST_SES_ERR_NO_SERVO,      /**< no reply to read, sync and read */
    KST_SES_ERR_NOT_IN_MODE,   /**< kst_session_enter() has not succeeded */
    KST_SES_ERR_READ,          /**< a register without 3 equal reads in 5 */
    KST_SES_ERR_NO_BACKUP,     /**< no image read in this session */
    KST_SES_ERR_FINGERPRINT,   /**< the layout fingerprint fails: read-only */
    KST_SES_ERR_LOCKED,        /**< an earlier write left the servo in doubt */
    KST_SES_ERR_BAD_PLAN,      /**< the plan is not one kst_plan.h builds */
    KST_SES_ERR_UNCONFIRMED,   /**< an unchecked restore without confirmation */
    KST_SES_ERR_CHANGED,       /**< the servo differs from the plan's start,
                                    or is not the backup's servo */
    KST_SES_ERR_WRITE_FAILED,  /**< a write did not take; nothing half
                                    written */
    KST_SES_ERR_ROLLED_BACK,   /**< a write did not take; the field is back at
                                    its old value */
    KST_SES_ERR_TORN,          /**< the roll-back failed too */
    KST_SES_ERR_UNINTENDED,    /**< a register changed that was not written */
    KST_SES_ERR_VERIFY,        /**< the servo differs from the expected image */
    KST_SES_ERR_DRIVER,        /**< start() refused or poll() never finished */
    KST_SES_ERR_ABORTED,       /**< kst_session_abort() */
    KST_SES_ERR_NO_PLAN,       /**< the planner built no plan; plan_result
                                    tells why */
} kst_ses_t;

/**
 * @brief Reply timing of the read frames since kst_session_init().
 *
 * A drift is the early sign of a marginal line.
 */
typedef struct {
    uint32_t replies;        /**< transactions with a valid reply */
    uint32_t faults;         /**< transactions without one */
    uint32_t delay_min_ns;   /**< reads only */
    uint32_t delay_max_ns;   /**< longest reply delay, in ns */
    uint32_t half_min_ns;    /**< shortest mean half-cell, in ns */
    uint32_t half_max_ns;    /**< longest mean half-cell, in ns */
} kst_stats_t;

/** @brief The session with one servo: results and state of its operation. */
typedef struct {
    /* --- for the caller to read ------------------------------------------- */
    kst_image_t image;       /**< the servo as last known; valid with
                                  has_image */
    kst_image_t backup;      /**< the first image read, after
                                  kst_session_init() or the last
                                  kst_session_power_cycled(); valid with
                                  has_backup */
    kst_fingerprint_t fingerprint;  /**< of the last image read */
    kst_stats_t stats;
    kst_reply_t last_reply;  /**< of the last transaction */
    uint32_t bad_regs;       /**< bit n: register n has no 3 equal reads */
    uint32_t wrote_regs;     /**< bit n: the session sent a write of
                                  register n, a write that was undone too,
                                  or found the register changed by one of
                                  its writes.  A restore is refused with
                                  KST_SES_ERR_CHANGED while a register
                                  outside this set differs from the
                                  backup: the servo is not the backup's */
    uint32_t diff_regs;      /**< bit n: register n differs from what the
                                  operation expected */
    uint8_t result;          /**< kst_ses_t of the last operation */
    uint8_t in_mode;         /**< the servo is in programming mode; the pin
                                  carries no PWM until the supply was off */
    uint8_t has_image;       /**< 1: image is valid */
    uint8_t has_backup;      /**< 1: backup is valid */
    uint8_t fingerprint_ok;  /**< 1: the image passed the fingerprint */
    uint8_t locked;          /**< only a restore plan is accepted */
    uint8_t step_index;      /**< writes of the running plan that are done */
    uint8_t planning;        /**< 1: the operation plans; nothing is sent */
    uint8_t plan_result;     /**< kst_plan_result_t of the last
                                  kst_session_edit(), kst_session_restore()
                                  or kst_session_release_pairing();
                                  KST_PLAN_BUSY while it plans */
    kst_rules_t plan_rules;  /**< with KST_PLAN_ERR_RULES: the hard rules
                                  the target breaks */
    uint32_t plan_cap;       /**< units a planning job may take;
                                  KST_PLAN_WORK_CAP after
                                  kst_session_init() */

    /* --- private ---------------------------------------------------------- */
    kst_driver_t drv;
    kst_plan_t plan;         /**< the running plan; between operations the
                                  planner's work space */
    kst_plan_job_t job;      /**< the planning job of the operation */
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
    uint8_t t_reg;           /**< the register of the frame */
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
    uint8_t unseen;          /* a write ended with the servo not read */
    uint8_t cycled;          /* the supply was off under the running
                                operation */
    uint8_t undone;
    uint8_t confirm;         /**< the running restore may be unchecked */
} kst_session_t;

/** @brief Prepare a session.  False when the driver lacks a function.
 *
 * @param s       Session to prepare.
 * @param driver  Driver to use.
 * @return false when the driver lacks a function.
 */
bool kst_session_init(kst_session_t *s, const kst_driver_t *driver);

/**
 * @brief Enter programming mode.
 *
 * The caller has stopped PWM on the pin.
 *
 * Sequence: line low for KST_ENTRY_LOW_US; read 0x01; on no reply the sync
 * burst; on no acknowledge one more read of 0x01.  A servo already in the
 * mode answers the first read, or, when that read is lost, the last one: it
 * does not acknowledge a second sync.  No other bit rate is tried.
 *
 * The mode lasts until the servo's supply is removed.
 *
 * @param s  Session.
 * @return KST_SES_BUSY while the sequence runs, then the result.
 */
kst_ses_t kst_session_enter(kst_session_t *s);

/**
 * @brief Read all 32 registers: 3 passes, all 3 equal for every register.
 *
 * On any
 * disagreement 2 more passes; a register without 3 equal reads in 5 fails
 * the read.  A pass without a single reply ends the read with
 * KST_SES_ERR_NO_SERVO.
 *
 * The first image read in a session becomes the backup.  The result is
 * KST_SES_OK for a servo that fails the fingerprint too; fingerprint_ok and
 * fingerprint tell.
 *
 * @param s  Session.
 * @return KST_SES_BUSY while the read runs, then the result.
 */
kst_ses_t kst_session_read_all(kst_session_t *s);

/**
 * @brief Change the servo from image to @p target, which is copied before the
 * session's plan is cleared: it may be the target of kst_session_plan().
 * @p unlocked is the unlock set of kst_plan_edit().
 *
 * Refused at once, before any planning and in this order: a running
 * operation (KST_SES_ERR_BUSY), a NULL @p target (KST_SES_ERR_ARG, whatever
 * the state of the session), no programming mode, no backup
 * (KST_SES_ERR_NO_BACKUP), a locked session (KST_SES_ERR_LOCKED), an image
 * that fails the fingerprint (KST_SES_ERR_FINGERPRINT), and a request
 * kst_plan_edit_begin() refuses: KST_SES_ERR_NO_PLAN with plan_result
 * KST_PLAN_ERR_R00, KST_PLAN_ERR_PAIRING, or KST_PLAN_ERR_RULES with
 * plan_rules.
 *
 * Then the session plans, one slice per kst_session_step() call: at most
 * KST_PLAN_WORK_CAP units, 2,161 calls.  The planning ends the operation
 * with KST_SES_ERR_NO_PLAN, before any frame, when plan_result is
 * KST_PLAN_ERR_NO_PATH (no order of the writes with valid images in
 * between) or KST_PLAN_ERR_TOO_COMPLEX (the cap is spent without a verdict;
 * a change of fewer fields at a time takes fewer units).  An abort while
 * planning ends it with KST_SES_ERR_ABORTED; nothing was sent and the
 * session is not locked.
 *
 * With plan_result KST_PLAN_OK the plan runs as kst_session_write()
 * describes from step 1.  kst_session_plan() gives the plan.
 *
 * @param s         Session.
 * @param target    Image to reach.
 * @param unlocked  Fields whose KST_ACCESS_LOCKED is lifted.
 * @return KST_SES_BUSY while planning, then the result.
 */
kst_ses_t kst_session_edit(kst_session_t *s, const kst_image_t *target,
                           kst_field_set_t unlocked);

/**
 * @brief Take the servo from image back to the backup, planned as
 * kst_session_edit() plans.  A locked session and an image that fails the
 * fingerprint do not refuse it.  KST_SES_ERR_NO_PLAN at once with
 * plan_result KST_PLAN_ERR_R00.
 *
 * The backup goes to the servo it was read from only.  The read before the
 * first write ends the restore with KST_SES_ERR_CHANGED, nothing written,
 * when a register outside wrote_regs differs from the backup; diff_regs
 * names those registers.  After a write that ended without a full read of
 * the servo (a failed read, an abort, KST_SES_ERR_TORN) the registers that
 * differ at the next full read count as changed by that write.
 *
 * A plan the planner marks unchecked runs with @p confirm_unchecked only.
 * Without it the operation ends with KST_SES_ERR_UNCONFIRMED when the
 * planning ends, before any frame; plan_result is KST_PLAN_OK and
 * kst_session_plan() gives the plan that waits for the confirmation.  The
 * second call plans again.
 *
 * @param s                 Session.
 * @param confirm_unchecked  true to run a plan the planner marks unchecked.
 * @return KST_SES_BUSY while planning, then the result.
 */
kst_ses_t kst_session_restore(kst_session_t *s, bool confirm_unchecked);

/**
 * @brief Write 0x00 to register 0x1D, as kst_plan_release_pairing() plans it.
 * Refused like an edit.  The plan is 1 write or none and is built in the
 * call: planning stays 0.
 *
 * @param s  Session.
 * @return The result.
 */
kst_ses_t kst_session_release_pairing(kst_session_t *s);

/**
 * @brief The plan of the last kst_session_edit(), kst_session_restore() or
 * kst_session_release_pairing(), for a journal or a display.  It is the
 * planner's plan while planning is 0 and plan_result is KST_PLAN_OK, until
 * the next operation of these 3 or kst_session_write() starts.  NULL for a
 * NULL session.
 *
 * @param s  Session.
 * @return The plan; NULL for a NULL session.
 */
const kst_plan_t *kst_session_plan(const kst_session_t *s);

/**
 * @brief Run a plan the caller built.
 *
 * The plan is copied.  @p plan must not be
 * the session's own copy or overlap it: that is answered with
 * KST_SES_ERR_ARG.
 *
 * This call runs the planner a second time and to its end inside the call,
 * without a cap: its duration grows with the plan.  A caller that must not
 * block uses kst_session_edit(), kst_session_restore() and
 * kst_session_release_pairing().
 *
 * Refused before any frame: no programming mode, no backup, a plan that is
 * not the one kst_plan.h builds for its kind, start, target and unlock set
 * (the planner is run again, into the session's own copy, and its writes,
 * their order, the settled marks and the unchecked mark are compared), an
 * edit or release plan that starts from an image that fails the
 * fingerprint, a locked session, a restore whose target is not the backup,
 * an unchecked restore without @p confirm_unchecked.
 *
 * Then:
 *   1. Read all.  The servo must equal the plan's start image; for a
 *      restore see kst_session_restore().  KST_SES_ERR_CHANGED otherwise:
 *      image stays the one held before, and kst_session_read_all() comes
 *      before the next plan.
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
 *
 * @param s                  Session.
 * @param plan               Plan to run; copied.
 * @param confirm_unchecked  true to run an unchecked restore.
 * @return The result of the run.
 */
kst_ses_t kst_session_write(kst_session_t *s, const kst_plan_t *plan,
                            bool confirm_unchecked);

/**
 * @brief Read all and compare with @p expected, which is copied.
 *
 * diff_regs names
 *  the registers that differ.
 *
 * @param s         Session.
 * @param expected  Image to compare with; copied.
 * @return KST_SES_BUSY while the read runs, then the result.
 */
kst_ses_t kst_session_verify(kst_session_t *s, const kst_image_t *expected);

/**
 * @brief Advance the running operation.
 *
 * KST_SES_BUSY while it runs, then its
 *  result; the result again on every later call.  A call that plans does
 *  one slice of KST_PLAN_SLICE_UNITS units and returns.
 *
 * @param s  Session.
 * @return KST_SES_BUSY, or the result of the operation.
 */
kst_ses_t kst_session_step(kst_session_t *s);

/**
 * @brief End the running operation with KST_SES_ERR_ABORTED.
 *
 * A transaction on
 *  the wire is finished first: a frame is never cut.  An operation that
 *  plans ends at the next kst_session_step() call.
 *
 * @param s  Session.
 */
void kst_session_abort(kst_session_t *s);

/**
 * @brief The servo's supply was off: it has left programming mode, and the
 *  servo on the line may be another one.  A running operation is aborted.
 *  The backup, wrote_regs and the lock are dropped, after the end of the
 *  running operation where one runs: an edit, a write and a restore are
 *  refused with KST_SES_ERR_NO_BACKUP until a kst_session_read_all() takes
 *  the servo as it then is for the new backup.
 *
 * @param s  Session.
 */
void kst_session_power_cycled(kst_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* KST_SESSION_H */
