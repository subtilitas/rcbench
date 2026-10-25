/**
 * @file
 * @brief Search for the order of the register writes that changes an image.
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_plan.h"

#include <string.h>

/**
 * @brief The registers whose hard rules read each other.
 *
 * Every other register has rules that read that register alone.
 */
static const uint8_t k_position[] = { 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D };
static const uint8_t k_speed[] = { 0x04, 0x1B, 0x1C };

#define GROUPS 2u

/**
 * @brief What a job does next.
 *
 * One call of advance() does one of these once.
 */
enum {
    PH_IDLE = 0,   /* no begin call prepared the job */
    PH_GROUP,      /* take up the next group, or the registers after them */
    PH_SEARCH,     /* one level of the search for an order */
    PH_FIELD,      /* find the next field that can take a detour */
    PH_OPEN,       /* check one value of the detour byte */
    PH_CHOOSE,     /* take the best untried detour value */
    PH_SETTLE,     /* the settled mark of one write */
    PH_DONE,
};

/** @brief Units of the work that is no image check, by its cost against one. */
#define UNITS_CHOOSE KST_PLAN_PIECE_UNITS  /* a pass over 256 detour values */
#define UNITS_SETTLE 4u                 /**< the settled mark of one write */

static bool bit_of(const uint8_t *set, unsigned i)
{
    return (set[i / 8u] & (1u << (i % 8u))) != 0u;
}

/**
 * @brief Set one bit of a bit set.
 *
 */
static void bit_set(uint8_t *set, unsigned i)
{
    set[i / 8u] |= (uint8_t)(1u << (i % 8u));
}

/**
 * @brief Clear one bit of a bit set.
 *
 */
static void bit_clear(uint8_t *set, unsigned i)
{
    set[i / 8u] &= (uint8_t)~(1u << (i % 8u));
}

/**
 * @brief The register list of the group the job works on.
 *
 * @param j  Job whose group is read.
 * @return k_position for group 0, else k_speed.
 */
static const uint8_t *regs_of(const kst_plan_job_t *j)
{
    return j->group == 0u ? k_position : k_speed;
}

/**
 * @brief Number of registers in the group the job is working on.
 *
 */
static unsigned count_of(const kst_plan_job_t *j)
{
    return j->group == 0u ? sizeof(k_position) : sizeof(k_speed);
}

/**
 * @brief Append a write to the plan and apply it to the job's image.
 *
 */
static void emit(kst_plan_job_t *j, uint8_t reg, uint8_t value)
{
    kst_write_t *w = &j->plan->step[j->plan->n++];

    w->reg = reg;
    w->value = value;
    w->prev = j->img.r[reg];
    w->settled = 0;
    j->img.r[reg] = value;
}

/**
 * @brief Check the job's image against the hard rules it may not break.
 *
 * Costs one unit.
 *
 */
static bool valid(kst_plan_job_t *j)
{
    j->spent++;
    return (kst_limits_image(&j->img) & ~j->allowed) == 0u;
}

/**
 * @brief Whether the image with the registers of @p todo still to write is
 *        valid; j->img is that image.
 *
 * The image is a function of @p todo and, while the detour byte is in @p todo,
 * of the detour value, so the verdict is kept per subset and an image is
 * checked once.
 */
static bool sound(kst_plan_job_t *j, unsigned todo)
{
    if (!bit_of(j->known, todo)) {
        bit_set(j->known, todo);
        if (valid(j)) {
            bit_set(j->sound, todo);
        } else {
            bit_clear(j->sound, todo);
        }
    }
    return bit_of(j->sound, todo);
}

/**
 * @brief Forget what is known of every subset that holds register index @p lo:
 *        its images change with the value of that register.
 *
 * The subsets without it keep their verdicts, which hold for every detour
 * value.
 */
static void forget(kst_plan_job_t *j, unsigned lo)
{
    for (unsigned t = 0; t < 8u * sizeof(j->known); t += 8u) {
        uint8_t keep = 0;

        for (unsigned k = 0; k < 8u; ++k) {
            if (((t + k) & (1u << lo)) == 0u) {
                keep |= (uint8_t)(1u << k);
            }
        }
        j->known[t / 8u] &= keep;
        j->dead[t / 8u] &= keep;
    }
}

/**
 * @brief A field's raw value, as kst_field_get() gives it, read from the table
 *        row in place: the search reads two fields for every image it scores.
 */
static uint16_t raw_of(const kst_image_t *img, kst_field_id_t id)
{
    const kst_field_t *f = kst_field(id);
    unsigned raw = ((unsigned)img->r[f->lo_reg] >> f->lo_shift)
                   & ((1u << f->lo_bits) - 1u);

    raw |= (((unsigned)img->r[f->hi_reg] >> f->hi_shift)
            & ((1u << f->hi_bits) - 1u)) << f->lo_bits;
    return (uint16_t)raw;
}

/**
 * @brief Lower is better: less travel between the end points, or less speed.
 *
 * With spd_sel off the servo runs at its own speed, taken as the highest.
 */
static int32_t authority(const kst_plan_job_t *j)
{
    if (j->group != 0u) {
        return raw_of(&j->img, KST_F_SPD_SEL) != 0u
                   ? (int32_t)raw_of(&j->img, KST_F_SPD) : 4096;
    }
    return (int32_t)raw_of(&j->img, KST_F_PULSE_UPPER)
           - (int32_t)raw_of(&j->img, KST_F_PULSE_LOWER);
}

/**
 * @brief The register of j->todo, not tried at this depth, whose write leaves a
 *        valid image of the least authority; of two alike the first.
 *
 * The group's count when there is none.  A register whose write leaves an
 * invalid image, or a set of registers known to have no order, is marked tried.
 */
static unsigned pick(kst_plan_job_t *j)
{
    const uint8_t *regs = regs_of(j);
    const unsigned n = count_of(j);
    unsigned best = n;
    int32_t best_score = 0;

    for (unsigned i = 0; i < n; ++i) {
        const uint8_t reg = regs[i];
        const uint8_t before = j->img.r[reg];
        const unsigned rest = j->todo & ~(1u << i);

        if (((j->todo & ~(unsigned)j->tried[j->depth]) & (1u << i)) == 0u) {
            continue;
        }
        j->img.r[reg] = j->plan->target.r[reg];
        if (bit_of(j->dead, rest) || !sound(j, rest)) {
            j->tried[j->depth] |= (uint8_t)(1u << i);
        } else {
            const int32_t score = authority(j);

            if (best == n || score < best_score) {
                best = i;
                best_score = score;
            }
        }
        j->img.r[reg] = before;
    }
    return best;
}

/**
 * @brief Take back the write at the depth below.
 *
 * Its bit returns to the todo set.
 */
static void take_back(kst_plan_job_t *j)
{
    j->depth--;
    j->img.r[regs_of(j)[j->order[j->depth]]] = j->held[j->depth];
    j->todo |= (uint8_t)(1u << j->order[j->depth]);
}

/**
 * @brief Start the depth-first search for an order of the group's changed
 *              registers.
 *
 */
static void search_begin(kst_plan_job_t *j)
{
    j->todo = j->todo_all;
    j->depth = 0;
    j->entered = 1;
    j->phase = PH_SEARCH;
}

/**
 * @brief The search found an order: its writes go into the plan, after the
 *        extra write when the search ran from a detour value.
 */
static void search_found(kst_plan_job_t *j)
{
    const uint8_t *regs = regs_of(j);
    const unsigned depth = j->depth;

    while (j->depth > 0u) {
        take_back(j);
    }
    if (j->in_detour != 0u) {
        j->img.r[regs[j->lo]] = j->lo_before;
        emit(j, regs[j->lo], j->extra);
    }
    for (unsigned k = 0; k < depth; ++k) {
        const uint8_t reg = regs[j->order[k]];

        emit(j, reg, j->plan->target.r[reg]);
    }
    j->group++;
    j->phase = PH_GROUP;
}

/**
 * @brief One level of a depth-first search for an order of the registers in
 *        j->todo, bit i for the group's register i, in which every image is
 *        valid.
 *
 * At most 7 levels deep; the levels are the entries of order, tried and held.
 * No image is copied and no function calls itself: a write is tried in j->img
 * and taken back.
 */
static void search_level(kst_plan_job_t *j)
{
    unsigned best;

    j->spent++;
    if (j->todo == 0u) {
        search_found(j);
        return;
    }
    if (j->entered != 0u) {
        j->tried[j->depth] = 0;
    }
    best = pick(j);
    if (best != count_of(j)) {
        const uint8_t reg = regs_of(j)[best];

        j->tried[j->depth] |= (uint8_t)(1u << best);
        j->order[j->depth] = (uint8_t)best;
        j->held[j->depth] = j->img.r[reg];
        j->img.r[reg] = j->plan->target.r[reg];
        j->todo &= (uint8_t)~(1u << best);
        j->depth++;
        j->entered = 1;
        return;
    }
    bit_set(j->dead, j->todo);
    if (j->depth > 0u) {
        take_back(j);
        j->entered = 0;
        return;
    }
    if (j->in_detour != 0u) {
        j->phase = PH_CHOOSE;
    } else {
        j->field = 0;
        j->phase = PH_FIELD;
    }
}

/**
 * @brief Absolute difference of two 16-bit values.
 *
 */
static uint16_t excursion(uint16_t a, uint16_t b)
{
    return (uint16_t)(a > b ? a - b : b - a);
}

/**
 * @brief The part of a field's value that its low register holds when the
 *        register holds @p x.
 */
static uint16_t low_part(const kst_field_t *f, unsigned x)
{
    return (uint16_t)((x >> f->lo_shift) & ((1u << f->lo_bits) - 1u));
}

/**
 * @brief Index of a register in the group the job is working on.
 *
 * The group's count when the register is not in it.
 *
 */
static unsigned index_of(const kst_plan_job_t *j, uint8_t reg)
{
    const uint8_t *regs = regs_of(j);
    const unsigned n = count_of(j);
    unsigned i = 0;

    while (i < n && regs[i] != reg) {
        i++;
    }
    return i;
}

/**
 * @brief End the job with a result.
 *
 * A result other than KST_PLAN_OK leaves no writes in the plan.
 *
 */
static void finish(kst_plan_job_t *j, kst_plan_result_t result)
{
    if (result != KST_PLAN_OK) {
        j->plan->n = 0;
        j->plan->unchecked = 0;
    }
    j->result = (uint8_t)result;
    j->phase = PH_DONE;
}

/**
 * @brief Write a register to its target value when the job's image differs from
 *              it.
 *
 */
static void emit_if_changed(kst_plan_job_t *j, uint8_t reg)
{
    if (j->img.r[reg] != j->plan->target.r[reg]) {
        emit(j, reg, j->plan->target.r[reg]);
    }
}

/**
 * @brief Write Max.
 *
 * Duty and its copy to their target values when they differ.
 *
 */
static void emit_duty(kst_plan_job_t *j)
{
    emit_if_changed(j, KST_REG_DUTY);
    emit_if_changed(j, KST_REG_DUTY_COPY);
}

/**
 * @brief Start the settled-mark pass from the start image.
 *
 */
static void settle_begin(kst_plan_job_t *j)
{
    j->img = j->plan->start;
    j->settle = 0;
    j->phase = PH_SETTLE;
}

/**
 * @brief No order and no detour for a group.
 *
 * An edit ends here; a restore takes ascending address order and is marked
 * unchecked.
 */
static void group_failed(kst_plan_job_t *j)
{
    if (j->may_be_unchecked == 0u) {
        finish(j, KST_PLAN_ERR_NO_PATH);
        return;
    }
    j->plan->n = 0;
    j->plan->unchecked = 1;
    j->img = j->plan->start;
    emit_duty(j);
    for (unsigned reg = KST_REG_DUTY + 1u; reg <= KST_REG_MAX; ++reg) {
        emit_if_changed(j, (uint8_t)reg);
    }
    settle_begin(j);
}

/**
 * @brief The next field, from j->field on, that lies in two of the group's
 *        registers which both change: its low byte can take one extra write to
 *        a third value before the search runs again.
 */
static void field_next(kst_plan_job_t *j)
{
    const unsigned n = count_of(j);

    j->spent++;
    for (; j->field < (uint8_t)KST_F_COUNT; j->field++) {
        const kst_field_t *f = kst_field((kst_field_id_t)j->field);
        unsigned lo;
        unsigned hi;

        if (f->hi_bits == 0u) {
            continue;
        }
        lo = index_of(j, f->lo_reg);
        hi = index_of(j, f->hi_reg);
        if (lo < n && hi < n && (j->todo_all & (1u << lo)) != 0u
            && (j->todo_all & (1u << hi)) != 0u) {
            j->lo = (uint8_t)lo;
            j->lo_before = j->img.r[f->lo_reg];
            j->x = 0;
            memset(j->open, 0, sizeof(j->open));
            j->phase = PH_OPEN;
            return;
        }
    }
    group_failed(j);
}

/**
 * @brief One value of the detour byte: it is a candidate when the image with it
 *        is valid and it is neither the old nor the new value.
 */
static void open_one(kst_plan_job_t *j)
{
    const uint8_t lo_reg = regs_of(j)[j->lo];
    const unsigned x = j->x;

    j->img.r[lo_reg] = (uint8_t)x;
    if (x == j->lo_before || x == j->plan->target.r[lo_reg]) {
        j->spent++;
    } else if (valid(j)) {
        bit_set(j->open, x);
    }
    j->x++;
    if (j->x == 256u) {
        j->phase = PH_CHOOSE;
    }
}

/**
 * @brief The untried detour value with the smallest excursion of the field from
 *        its old value before, and from its new value after, the high part
 *        changes; of two alike the lower value.
 *
 * The search then runs from it. The excursion is computed again for every pass,
 * from the field's layout: the open set is 32 bytes, a table of the 256
 * excursions is 512.
 */
static void choose(kst_plan_job_t *j)
{
    const kst_field_id_t id = (kst_field_id_t)j->field;
    const kst_field_t *f = kst_field(id);
    uint16_t old_v;
    uint16_t new_v;
    uint16_t old_high;
    uint16_t new_high;
    unsigned best = 256;
    uint16_t best_cost = 0;

    j->spent += UNITS_CHOOSE;
    j->img.r[f->lo_reg] = j->lo_before;
    old_v = kst_field_get(&j->img, id);
    new_v = kst_field_get(&j->plan->target, id);
    old_high = old_v & (uint16_t)~low_part(f, 0xFF);
    new_high = new_v & (uint16_t)~low_part(f, 0xFF);
    for (unsigned x = 0; x < 256u; ++x) {
        uint16_t before;
        uint16_t after;

        if (!bit_of(j->open, x)) {
            continue;
        }
        before = excursion(old_high | low_part(f, x), old_v);
        after = excursion(new_high | low_part(f, x), new_v);
        if (after < before) {
            after = before;
        }
        if (best == 256u || after < best_cost) {
            best = x;
            best_cost = after;
        }
    }
    /* What the last search learned of the subsets with the detour byte
     * holds for the value it ran from. */
    forget(j, j->lo);
    if (best == 256u) {
        j->in_detour = 0;
        j->field++;
        j->phase = PH_FIELD;
        return;
    }
    bit_clear(j->open, best);
    j->img.r[f->lo_reg] = (uint8_t)best;
    j->extra = (uint8_t)best;
    j->in_detour = 1;
    search_begin(j);
}

/**
 * @brief Check whether a register is one of the two searched groups.
 *
 */
static bool in_group(uint8_t reg)
{
    for (unsigned i = 0; i < sizeof(k_position); ++i) {
        if (k_position[i] == reg) {
            return true;
        }
    }
    for (unsigned i = 0; i < sizeof(k_speed); ++i) {
        if (k_speed[i] == reg) {
            return true;
        }
    }
    return reg == KST_REG_DUTY || reg == KST_REG_DUTY_COPY;
}

/**
 * @brief Check whether registers 0x02 and 0x01 of two images agree.
 *
 */
static bool pair_is(const kst_image_t *img, const kst_image_t *ref)
{
    return img->r[KST_REG_DUTY] == ref->r[KST_REG_DUTY]
           && img->r[KST_REG_DUTY_COPY] == ref->r[KST_REG_DUTY_COPY];
}

/**
 * @brief Check that every two-register field of an image holds its start or its
 *              target value.
 *
 */
static bool is_settled(const kst_plan_t *plan, const kst_image_t *img)
{
    for (unsigned i = 0; i < KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const uint16_t v = kst_field_get(img, id);

        if (kst_field(id)->hi_bits != 0u
            && v != kst_field_get(&plan->start, id)
            && v != kst_field_get(&plan->target, id)) {
            return false;
        }
    }
    return pair_is(img, &plan->start) || pair_is(img, &plan->target);
}

/**
 * @brief The settled mark of the next write, or the end of the job after the
 *        last.  j->img follows the plan from its start image.
 */
static void settle_one(kst_plan_job_t *j)
{
    kst_plan_t *plan = j->plan;

    j->spent += UNITS_SETTLE;
    if (j->settle == plan->n) {
        finish(j, KST_PLAN_OK);
        return;
    }
    j->img.r[plan->step[j->settle].reg] = plan->step[j->settle].value;
    plan->step[j->settle].settled = is_settled(plan, &j->img) ? 1u : 0u;
    j->settle++;
}

/**
 * @brief Take up the next group: the registers of it that change, and nothing
 *        known of their subsets.
 *
 * After the last group the duty pair and every other register follow, which
 * need no search.
 */
static void group_begin(kst_plan_job_t *j)
{
    const uint8_t *regs = regs_of(j);
    const unsigned n = count_of(j);

    j->spent++;
    if (j->group == GROUPS) {
        emit_duty(j);
        for (uint8_t reg = 1; reg <= KST_REG_MAX; ++reg) {
            if (!in_group(reg)) {
                emit_if_changed(j, reg);
            }
        }
        settle_begin(j);
        return;
    }
    j->todo_all = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (j->img.r[regs[i]] != j->plan->target.r[regs[i]]) {
            j->todo_all |= (uint8_t)(1u << i);
        }
    }
    memset(j->dead, 0, sizeof(j->dead));
    memset(j->known, 0, sizeof(j->known));
    j->in_detour = 0;
    search_begin(j);
}

/**
 * @brief Run one step of the job, selected by its phase.
 *
 */
static void advance(kst_plan_job_t *j)
{
    switch (j->phase) {
    case PH_GROUP:
        group_begin(j);
        break;
    case PH_SEARCH:
        search_level(j);
        break;
    case PH_FIELD:
        field_next(j);
        break;
    case PH_OPEN:
        open_one(j);
        break;
    case PH_CHOOSE:
        choose(j);
        break;
    default:
        settle_one(j);
        break;
    }
}

kst_plan_result_t kst_plan_step(kst_plan_job_t *job, uint32_t budget)
{
    uint32_t from;

    if (job == NULL || job->phase == PH_IDLE) {
        return KST_PLAN_ERR_ARG;
    }
    from = job->spent;
    while (job->phase != PH_DONE && job->spent - from < budget) {
        advance(job);
        if (job->spent > job->cap) {
            finish(job, KST_PLAN_ERR_TOO_COMPLEX);
        }
    }
    return job->phase == PH_DONE ? (kst_plan_result_t)job->result
                                 : KST_PLAN_BUSY;
}

uint32_t kst_plan_units(const kst_plan_job_t *job)
{
    return job != NULL ? job->spent : 0u;
}

/**
 * @brief @p start and @p target may lie in @p out, as a plan's own start or
 *        target do: both are read before the plan is cleared.
 *
 * The caller reads them from the plan afterwards.
 */
static void begin(kst_plan_t *out, const kst_image_t *start,
                  const kst_image_t *target, kst_plan_kind_t kind)
{
    const kst_image_t from = *start;
    const kst_image_t to = *target;

    memset(out, 0, sizeof(*out));
    out->start = from;
    out->target = to;
    out->kind = (uint8_t)kind;
}

/**
 * @brief A final result at a begin call.
 *
 * The job, when there is one, holds it.
 */
static kst_plan_result_t ended(kst_plan_job_t *job, kst_plan_result_t result)
{
    if (job != NULL) {
        memset(job, 0, sizeof(*job));
        job->result = (uint8_t)result;
        job->phase = PH_DONE;
    }
    return result;
}

/**
 * @brief Set up a planning job for a plan from its start and target images.
 *
 */
static kst_plan_result_t job_begin(kst_plan_job_t *job, kst_plan_t *out,
                                   bool may_be_unchecked, uint32_t cap)
{
    memset(job, 0, sizeof(*job));
    job->plan = out;
    job->img = out->start;
    job->allowed = kst_limits_image(&out->start)
                   | kst_limits_image(&out->target);
    job->cap = cap;
    job->may_be_unchecked = may_be_unchecked ? 1u : 0u;
    job->result = (uint8_t)KST_PLAN_BUSY;
    job->phase = PH_GROUP;
    return KST_PLAN_BUSY;
}

kst_plan_result_t kst_plan_edit_begin(kst_plan_job_t *job,
                                      const kst_image_t *start,
                                      const kst_image_t *target,
                                      kst_field_set_t unlocked,
                                      kst_plan_t *out,
                                      kst_rules_t *violated,
                                      uint32_t cap)
{
    kst_rules_t broken;

    if (violated != NULL) {
        *violated = 0;
    }
    if (out == NULL) {
        return ended(job, KST_PLAN_ERR_ARG);
    }
    if (start == NULL || target == NULL || job == NULL) {
        memset(out, 0, sizeof(*out));
        return ended(job, KST_PLAN_ERR_ARG);
    }
    begin(out, start, target, KST_PLAN_EDIT);
    out->unlocked = unlocked;
    if (out->start.r[0] != out->target.r[0]) {
        return ended(job, KST_PLAN_ERR_R00);
    }
    if (out->start.r[KST_REG_NODE_ADDR] != out->target.r[KST_REG_NODE_ADDR]) {
        return ended(job, KST_PLAN_ERR_PAIRING);
    }
    broken = kst_limits_edit(&out->start, &out->target, unlocked, NULL)
             & KST_RULES_HARD;
    if (violated != NULL) {
        *violated = broken;
    }
    if (broken != 0u) {
        return ended(job, KST_PLAN_ERR_RULES);
    }
    return job_begin(job, out, false, cap);
}

kst_plan_result_t kst_plan_restore_begin(kst_plan_job_t *job,
                                         const kst_image_t *current,
                                         const kst_image_t *backup,
                                         kst_plan_t *out,
                                         uint32_t cap)
{
    if (out == NULL) {
        return ended(job, KST_PLAN_ERR_ARG);
    }
    if (current == NULL || backup == NULL || job == NULL) {
        memset(out, 0, sizeof(*out));
        return ended(job, KST_PLAN_ERR_ARG);
    }
    begin(out, current, backup, KST_PLAN_RESTORE);
    out->unlocked = ~(kst_field_set_t)0;
    if (out->start.r[0] != out->target.r[0]) {
        return ended(job, KST_PLAN_ERR_R00);
    }
    return job_begin(job, out, true, cap);
}

/**
 * @brief Run a job to its end in one call.
 *
 */
static kst_plan_result_t run(kst_plan_job_t *job, kst_plan_result_t r)
{
    while (r == KST_PLAN_BUSY) {
        r = kst_plan_step(job, KST_PLAN_NO_CAP);
    }
    return r;
}

kst_plan_result_t kst_plan_edit(const kst_image_t *start,
                                const kst_image_t *target,
                                kst_field_set_t unlocked,
                                kst_plan_t *out,
                                kst_rules_t *violated)
{
    kst_plan_job_t job;

    return run(&job, kst_plan_edit_begin(&job, start, target, unlocked, out,
                                         violated, KST_PLAN_NO_CAP));
}

kst_plan_result_t kst_plan_restore(const kst_image_t *current,
                                   const kst_image_t *backup,
                                   kst_plan_t *out)
{
    kst_plan_job_t job;

    return run(&job, kst_plan_restore_begin(&job, current, backup, out,
                                            KST_PLAN_NO_CAP));
}

kst_plan_result_t kst_plan_release_pairing(const kst_image_t *current,
                                           kst_plan_t *out)
{
    if (out == NULL) {
        return KST_PLAN_ERR_ARG;
    }
    if (current == NULL) {
        memset(out, 0, sizeof(*out));
        return KST_PLAN_ERR_ARG;
    }
    begin(out, current, current, KST_PLAN_RELEASE_PAIRING);
    out->target.r[KST_REG_NODE_ADDR] = 0x00;
    if (out->start.r[KST_REG_NODE_ADDR] != 0x00u) {
        out->step[0].reg = KST_REG_NODE_ADDR;
        out->step[0].value = 0x00;
        out->step[0].prev = out->start.r[KST_REG_NODE_ADDR];
        out->step[0].settled = is_settled(out, &out->target) ? 1u : 0u;
        out->n = 1;
    }
    return KST_PLAN_OK;
}

bool kst_plan_image_after(const kst_plan_t *plan, unsigned steps,
                          kst_image_t *out)
{
    if (plan == NULL || out == NULL || steps > plan->n
        || plan->n > KST_PLAN_MAX_STEPS) {
        return false;
    }
    *out = plan->start;
    for (unsigned i = 0; i < steps; ++i) {
        out->r[plan->step[i].reg & KST_REG_MAX] = plan->step[i].value;
    }
    return true;
}
