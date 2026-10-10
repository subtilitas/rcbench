/*
 * KST servo programming protocol: the order of the writes for a change.
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
} kst_plan_result_t;

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
