/*
 * KST servo programming protocol: the order of the writes for a change.
 *
 * Implements the programming protocol of KST and Chaservo servos.  KST and
 * Chaservo are names or trademarks of their respective owners; they are used
 * here only to say which servos this module works with (README.md, "Legal
 * notice and disclaimer").
 *
 * A plan is the list of single-register writes that takes a servo from one
 * image to another.  A write frame carries no checksum and a field can lie
 * in two registers, so the order matters: between two writes the servo holds
 * an image that is neither the old nor the new setting.
 *
 * Rules of a plan:
 *   - Only registers whose value changes are written.  A whole register is
 *     written, with the bits of every field that does not change kept as the
 *     start image has them.
 *   - Register 0x00 is never written.  An address above 0x1F does not exist
 *     in an image.
 *   - Register 0x1D is written by a restore plan and by a release-pairing
 *     plan and by no other.
 *   - Max. Duty writes 0x02, then its copy 0x01.
 *   - Every image between two writes breaks no hard rule of kst_limits.h
 *     that the start image and the target image do not break themselves.
 *     Between the writes of 0x02 and 0x01 the copy differs; that state is
 *     exempt.
 *   - Where more than one order qualifies, the write that leaves less travel
 *     (Pulse Upper less Pulse Lower) or less speed comes first.
 *   - Where no order of the changed registers qualifies, one extra write
 *     sets the low byte of a two-register field to a third value first.  The
 *     value with the smallest excursion of the field is taken.
 *   - Where that fails too, an edit is refused.  A restore falls back to
 *     ascending address order and marks the plan unchecked; the caller asks
 *     for one confirmation before running it.
 *
 * Order of the register groups in a plan: positions (0x07 to 0x0D), speed
 * (0x04, 0x1B, 0x1C), duty (0x02, 0x01), then every other register by
 * ascending address.  A rule that ties the groups to each other is not
 * known.
 *
 * Execution model: a plan is built by a job, in slices.
 * kst_plan_edit_begin() or kst_plan_restore_begin() checks the request and
 * prepares a kst_plan_job_t; kst_plan_step() then does a bounded amount of
 * work per call and returns KST_PLAN_BUSY until the plan is there.
 * kst_plan_edit() and kst_plan_restore() run a job to its end in one call.
 *
 * Work is counted in units.  One unit is one check of an image against the
 * hard rules, or bookkeeping of about that cost.  The count of a request
 * depends on its two images alone, not on how the work is cut into calls.
 * A job has a cap: a request that needs more units than the cap ends with
 * KST_PLAN_ERR_TOO_COMPLEX.  That is not KST_PLAN_ERR_NO_PATH: the planner
 * reached no verdict.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KST_PLAN_H
#define KST_PLAN_H

#include <stdbool.h>
#include <stdint.h>

#include "kst_limits.h"
#include "kst_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 31 writable registers and 1 extra write in each of the 2 searched
 *  groups. */
#define KST_PLAN_MAX_STEPS 33u

typedef enum {
    KST_PLAN_EDIT = 0,         /**< a change of editable fields */
    KST_PLAN_RESTORE,          /**< back to the backup, locks lifted */
    KST_PLAN_RELEASE_PAIRING,  /**< register 0x1D to 0x00 */
} kst_plan_kind_t;

typedef enum {
    KST_PLAN_OK = 0,
    KST_PLAN_ERR_ARG,      /**< a NULL argument */
    KST_PLAN_ERR_RULES,    /**< the target breaks a hard rule */
    KST_PLAN_ERR_R00,      /**< the target differs in register 0x00 */
    KST_PLAN_ERR_PAIRING,  /**< an edit differs in register 0x1D */
    KST_PLAN_ERR_NO_PATH,  /**< no order with valid images in between */
    KST_PLAN_BUSY,         /**< the job runs; call kst_plan_step() */
    KST_PLAN_ERR_TOO_COMPLEX,  /**< the job's cap is spent; no verdict */
} kst_plan_result_t;

/** No cap: a job runs until the planner has its verdict. */
#define KST_PLAN_NO_CAP 0xFFFFFFFFu
/** The most units one piece of work takes.  kst_plan_step() ends a call
 *  after the piece that brings its count to the budget, so a call takes
 *  less than its budget plus this. */
#define KST_PLAN_PIECE_UNITS 10u
/**
 * Units per kst_plan_step() call of a caller that must not block: a call
 * takes 100 to 109 units.
 *
 * Cost on a Cortex-M33 at 150 MHz, computed in an emulator from the
 * instructions a call executes, with ARM GNU 14.2 at -O3; not measured on
 * hardware.  The upper estimate counts 1 cycle per instruction, 1 more per
 * load and per store and 2 per basic block.  The costliest call found:
 * 122,397 cycles, 816 us, over the 71 costliest requests found.  The
 * costliest single unit found is 1,228 cycles, which puts a call of 109
 * units at 133,852 cycles, 892 us, or less.  A begin call: 9,670
 * cycles, 64 us.
 */
#define KST_PLAN_SLICE_UNITS 100u
/**
 * The cap of such a caller: at most 2,161 calls of KST_PLAN_SLICE_UNITS.
 *
 * The costliest request found takes 120,136 units: a restore that ends
 * unchecked.  The cap is 1.8 times that.  The costliest edit found takes 118,776 units and
 * ends with KST_PLAN_ERR_NO_PATH.  Found by 336 searches that start from
 * the 12 slowest of 10,800,000 random requests, change 1 to 3 register
 * bytes at a time and keep the dearer request; 78 of them end at 120,136
 * units and none above.  A bound over every request is not proven: the cap is
 * what holds one.
 */
#define KST_PLAN_WORK_CAP 216000u

/** One write. */
typedef struct {
    uint8_t reg;
    uint8_t value;
    uint8_t prev;     /**< the register's value before this write */
    uint8_t settled;  /**< 1: after this write no two-register field and no
                           duty pair is half written */
} kst_write_t;

typedef struct {
    kst_image_t start;       /**< the image the servo must hold beforehand */
    kst_image_t target;      /**< the image after the last write */
    kst_field_set_t unlocked;
    uint8_t kind;            /**< kst_plan_kind_t */
    uint8_t n;               /**< number of writes */
    uint8_t unchecked;       /**< 1: a restore whose order is not checked */
    kst_write_t step[KST_PLAN_MAX_STEPS];
} kst_plan_t;

/**
 * The state of one planning job: 176 bytes.  The caller owns it and reads
 * no member.  It holds a pointer to the plan it builds: the plan is not
 * read until the job has ended, and it stays where it is, or the caller
 * that moves job and plan sets the member plan to the new place before the
 * next kst_plan_step().  Two jobs on two plans run side by side.
 */
typedef struct {
    kst_rules_t allowed;     /**< hard rules the end points break themselves */
    kst_plan_t *plan;
    uint32_t spent;          /**< units since the job began */
    uint32_t cap;
    kst_image_t img;         /**< the image after the writes emitted so far */
    uint8_t open[32];        /**< bit x: the extra write of x is untried */
    uint8_t dead[16];        /**< bit t: the subset t has no valid order */
    uint8_t known[16];       /**< bit t: sound holds the verdict on t */
    uint8_t sound[16];       /**< bit t: the image of subset t is valid */
    uint8_t order[7];        /**< the index written at each depth */
    uint8_t tried[7];        /**< bit i: done with at this depth */
    uint8_t held[7];         /**< the value the write at each depth replaced */
    uint8_t phase;
    uint8_t result;          /**< kst_plan_result_t */
    uint8_t may_be_unchecked;
    uint8_t group;
    uint8_t todo_all;        /**< the group's registers that change */
    uint8_t todo;            /**< those of them not written at this depth */
    uint8_t depth;
    uint8_t entered;
    uint8_t in_detour;
    uint8_t field;           /**< the field whose low byte takes the detour */
    uint8_t lo;              /**< index of that byte in the group */
    uint8_t lo_before;
    uint8_t extra;           /**< the detour value under search */
    uint8_t settle;          /**< writes whose settled mark is set */
    uint16_t x;              /**< detour values checked so far */
} kst_plan_job_t;

/**
 * Check an edit from @p start to @p target and prepare @p job to plan it
 * into @p out.  KST_PLAN_BUSY: the job is ready for kst_plan_step().  Every
 * other result is final and is what kst_plan_edit() returns for the same
 * request before it searches: KST_PLAN_ERR_ARG, _R00, _PAIRING, _RULES.
 * The call checks at most 4 images; its cost does not grow with the
 * request.
 *
 * @p start and @p target are copied before @p out is cleared: either may
 * lie in @p out.  The same holds for kst_plan_restore_begin(),
 * kst_plan_edit(), kst_plan_restore() and kst_plan_release_pairing().
 *
 * @p cap is the most units the job may take, or KST_PLAN_NO_CAP.
 */
kst_plan_result_t kst_plan_edit_begin(kst_plan_job_t *job,
                                      const kst_image_t *start,
                                      const kst_image_t *target,
                                      kst_field_set_t unlocked,
                                      kst_plan_t *out,
                                      kst_rules_t *violated,
                                      uint32_t cap);

/** The same for kst_plan_restore(): KST_PLAN_BUSY, or KST_PLAN_ERR_ARG or
 *  KST_PLAN_ERR_R00 at once. */
kst_plan_result_t kst_plan_restore_begin(kst_plan_job_t *job,
                                         const kst_image_t *current,
                                         const kst_image_t *backup,
                                         kst_plan_t *out,
                                         uint32_t cap);

/**
 * Advance a job by @p budget units.  The call ends after the piece of work
 * that brings its count to @p budget or above; a piece is at most
 * KST_PLAN_PIECE_UNITS units.  @p budget 0 does nothing.
 *
 * KST_PLAN_BUSY while work is left.  Then the job's result, and the same
 * result on every later call: KST_PLAN_OK, KST_PLAN_ERR_NO_PATH, or
 * KST_PLAN_ERR_TOO_COMPLEX when the job needs more units than its cap.  A
 * job that needs as many units as its cap ends with its verdict.  The plan
 * holds 0 writes after every result but KST_PLAN_OK.
 *
 * A job whose begin call returned a final result gives that result.
 * KST_PLAN_ERR_ARG for NULL and for a job of zeroed memory.
 */
kst_plan_result_t kst_plan_step(kst_plan_job_t *job, uint32_t budget);

/** Units the job has taken so far. */
uint32_t kst_plan_units(const kst_plan_job_t *job);

/**
 * Plan an edit from @p start to @p target.
 *
 * The hard rules of kst_limits_edit(@p start, @p target, @p unlocked) are
 * checked first; @p violated, when not NULL, receives the broken ones.  Soft
 * rules do not refuse a plan: the caller shows them and asks.
 *
 * @p out holds a plan with 0 writes when the result is not KST_PLAN_OK.
 */
kst_plan_result_t kst_plan_edit(const kst_image_t *start,
                                const kst_image_t *target,
                                kst_field_set_t unlocked,
                                kst_plan_t *out,
                                kst_rules_t *violated);

/**
 * Plan the way from @p current back to @p backup.  Access classes and limits
 * do not apply: every value is one this servo held.  Register 0x1D is
 * written back like any other.  A difference in register 0x00 is refused.
 */
kst_plan_result_t kst_plan_restore(const kst_image_t *current,
                                   const kst_image_t *backup,
                                   kst_plan_t *out);

/**
 * Plan the write of 0x00 to register 0x1D.  0x00 is the only value the
 * vendor's card does not refuse in any tested case.  The effect inside the
 * servo is not known.  A card identifier is never written.
 */
kst_plan_result_t kst_plan_release_pairing(const kst_image_t *current,
                                           kst_plan_t *out);

/** The image after the first @p steps writes of a plan.  False for more
 *  steps than the plan has. */
bool kst_plan_image_after(const kst_plan_t *plan, unsigned steps,
                          kst_image_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KST_PLAN_H */
