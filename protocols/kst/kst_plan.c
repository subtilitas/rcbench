/*
 * SPDX-License-Identifier: MIT
 */

#include "kst_plan.h"

#include <string.h>

/* The registers whose hard rules read each other.  Every other register
 * has rules that read that register alone. */
static const uint8_t k_position[] = { 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D };
static const uint8_t k_speed[] = { 0x04, 0x1B, 0x1C };

#define GROUP_MAX 7u
#define NO_COST   0xFFFFu

typedef struct {
    kst_plan_t *plan;
    kst_image_t img;       /* the image after the writes emitted so far */
    kst_rules_t allowed;   /* hard rules the end points break themselves */
} build_t;

typedef struct {
    const uint8_t *regs;
    unsigned n;
    bool speed;
    const kst_image_t *target;
    kst_rules_t allowed;
    uint8_t dead[(1u << GROUP_MAX) / 8u];  /* subsets with no valid order */
    uint8_t order[GROUP_MAX];
} search_t;

static void emit(build_t *b, uint8_t reg, uint8_t value)
{
    kst_write_t *w = &b->plan->step[b->plan->n++];

    w->reg = reg;
    w->value = value;
    w->prev = b->img.r[reg];
    w->settled = 0;
    b->img.r[reg] = value;
}

static bool valid(const search_t *s, const kst_image_t *img)
{
    return (kst_limits_image(img) & ~s->allowed) == 0u;
}

/* Lower is better: less travel between the end points, or less speed.
 * With spd_sel off the servo runs at its own speed, taken as the highest. */
static int32_t authority(const search_t *s, const kst_image_t *img)
{
    if (s->speed) {
        return kst_field_get(img, KST_F_SPD_SEL) != 0u
                   ? (int32_t)kst_field_get(img, KST_F_SPD) : 4096;
    }
    return (int32_t)kst_field_get(img, KST_F_PULSE_UPPER)
           - (int32_t)kst_field_get(img, KST_F_PULSE_LOWER);
}

/*
 * Depth-first search for an order of the registers in @p todo, bit i for
 * regs[i], in which every image passes valid().  At most 7 levels deep.
 */
static bool search(search_t *s, const kst_image_t *img, unsigned todo,
                   unsigned depth)
{
    unsigned tried = 0;

    if (todo == 0u) {
        return true;
    }
    if ((s->dead[todo / 8u] & (1u << (todo % 8u))) != 0u) {
        return false;
    }
    for (;;) {
        unsigned best = s->n;
        int32_t best_score = 0;
        kst_image_t next;

        for (unsigned i = 0; i < s->n; ++i) {
            int32_t sc;

            if (((todo & ~tried) & (1u << i)) == 0u) {
                continue;
            }
            next = *img;
            next.r[s->regs[i]] = s->target->r[s->regs[i]];
            if (!valid(s, &next)) {
                tried |= 1u << i;
                continue;
            }
            sc = authority(s, &next);
            if (best == s->n || sc < best_score) {
                best = i;
                best_score = sc;
            }
        }
        if (best == s->n) {
            break;
        }
        tried |= 1u << best;
        next = *img;
        next.r[s->regs[best]] = s->target->r[s->regs[best]];
        s->order[depth] = (uint8_t)best;
        if (search(s, &next, todo & ~(1u << best), depth + 1u)) {
            return true;
        }
    }
    s->dead[todo / 8u] |= (uint8_t)(1u << (todo % 8u));
    return false;
}

static void emit_order(build_t *b, const search_t *s, unsigned todo)
{
    unsigned count = 0;

    for (unsigned i = 0; i < s->n; ++i) {
        count += (todo >> i) & 1u;
    }
    for (unsigned k = 0; k < count; ++k) {
        const uint8_t reg = s->regs[s->order[k]];

        emit(b, reg, s->target->r[reg]);
    }
}

static uint16_t excursion(uint16_t a, uint16_t b)
{
    return (uint16_t)(a > b ? a - b : b - a);
}

/*
 * One extra write of the field's low byte to a third value, then a search
 * from there.  Candidates are tried by ascending excursion of the field
 * from its old value before, and from its new value after, the high part
 * changes.
 */
static bool detour(build_t *b, search_t *s, unsigned todo, kst_field_id_t id)
{
    const kst_field_t *f = kst_field(id);
    const uint16_t old_v = kst_field_get(&b->img, id);
    const uint16_t new_v = kst_field_get(s->target, id);
    uint16_t cost[256];

    for (unsigned x = 0; x < 256u; ++x) {
        kst_image_t a = b->img;
        kst_image_t c;
        uint16_t before;
        uint16_t after;

        cost[x] = NO_COST;
        a.r[f->lo_reg] = (uint8_t)x;
        if (x == b->img.r[f->lo_reg] || x == s->target->r[f->lo_reg]
            || !valid(s, &a)) {
            continue;
        }
        c = a;
        c.r[f->hi_reg] = s->target->r[f->hi_reg];
        before = excursion(kst_field_get(&a, id), old_v);
        after = excursion(kst_field_get(&c, id), new_v);
        cost[x] = before > after ? before : after;
    }
    for (;;) {
        unsigned best = 256;
        kst_image_t from = b->img;

        for (unsigned x = 0; x < 256u; ++x) {
            if (cost[x] != NO_COST && (best == 256u || cost[x] < cost[best])) {
                best = x;
            }
        }
        if (best == 256u) {
            return false;
        }
        cost[best] = NO_COST;
        from.r[f->lo_reg] = (uint8_t)best;
        memset(s->dead, 0, sizeof(s->dead));
        if (search(s, &from, todo, 0)) {
            emit(b, f->lo_reg, (uint8_t)best);
            emit_order(b, s, todo);
            return true;
        }
    }
}

static unsigned index_of(const search_t *s, uint8_t reg)
{
    unsigned i = 0;

    while (i < s->n && s->regs[i] != reg) {
        i++;
    }
    return i;
}

static bool plan_group(build_t *b, const uint8_t *regs, unsigned n, bool speed)
{
    search_t s;
    unsigned todo = 0;

    memset(&s, 0, sizeof(s));
    s.regs = regs;
    s.n = n;
    s.speed = speed;
    s.target = &b->plan->target;
    s.allowed = b->allowed;
    for (unsigned i = 0; i < n; ++i) {
        if (b->img.r[regs[i]] != s.target->r[regs[i]]) {
            todo |= 1u << i;
        }
    }
    if (search(&s, &b->img, todo, 0)) {
        emit_order(b, &s, todo);
        return true;
    }
    for (unsigned i = 0; i < KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const kst_field_t *f = kst_field(id);
        unsigned lo;
        unsigned hi;

        if (f->hi_bits == 0u) {
            continue;
        }
        lo = index_of(&s, f->lo_reg);
        hi = index_of(&s, f->hi_reg);
        if (lo < n && hi < n && (todo & (1u << lo)) != 0u
            && (todo & (1u << hi)) != 0u && detour(b, &s, todo, id)) {
            return true;
        }
    }
    return false;
}

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

static void emit_if_changed(build_t *b, uint8_t reg)
{
    if (b->img.r[reg] != b->plan->target.r[reg]) {
        emit(b, reg, b->plan->target.r[reg]);
    }
}

static void emit_duty(build_t *b)
{
    emit_if_changed(b, KST_REG_DUTY);
    emit_if_changed(b, KST_REG_DUTY_COPY);
}

static bool pair_is(const kst_image_t *img, const kst_image_t *ref)
{
    return img->r[KST_REG_DUTY] == ref->r[KST_REG_DUTY]
           && img->r[KST_REG_DUTY_COPY] == ref->r[KST_REG_DUTY_COPY];
}

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

static void mark_settled(kst_plan_t *plan)
{
    kst_image_t img = plan->start;

    for (unsigned i = 0; i < plan->n; ++i) {
        img.r[plan->step[i].reg] = plan->step[i].value;
        plan->step[i].settled = is_settled(plan, &img) ? 1u : 0u;
    }
}

static bool build_checked(build_t *b)
{
    if (!plan_group(b, k_position, sizeof(k_position), false)
        || !plan_group(b, k_speed, sizeof(k_speed), true)) {
        return false;
    }
    emit_duty(b);
    for (uint8_t reg = 1; reg <= KST_REG_MAX; ++reg) {
        if (!in_group(reg)) {
            emit_if_changed(b, reg);
        }
    }
    return true;
}

static void build_unchecked(build_t *b)
{
    b->plan->n = 0;
    b->plan->unchecked = 1;
    b->img = b->plan->start;
    emit_duty(b);
    for (uint8_t reg = KST_REG_DUTY + 1u; reg <= KST_REG_MAX; ++reg) {
        emit_if_changed(b, reg);
    }
}

static void begin(kst_plan_t *out, const kst_image_t *start,
                  const kst_image_t *target, kst_plan_kind_t kind)
{
    memset(out, 0, sizeof(*out));
    out->start = *start;
    out->target = *target;
    out->kind = (uint8_t)kind;
}

static kst_plan_result_t build(kst_plan_t *out, bool may_be_unchecked)
{
    build_t b;

    b.plan = out;
    b.img = out->start;
    b.allowed = kst_limits_image(&out->start) | kst_limits_image(&out->target);
    if (!build_checked(&b)) {
        if (!may_be_unchecked) {
            out->n = 0;
            return KST_PLAN_ERR_NO_PATH;
        }
        build_unchecked(&b);
    }
    mark_settled(out);
    return KST_PLAN_OK;
}

kst_plan_result_t kst_plan_edit(const kst_image_t *start,
                                const kst_image_t *target,
                                kst_field_set_t unlocked,
                                kst_plan_t *out,
                                kst_rules_t *violated)
{
    kst_rules_t broken;

    if (violated != NULL) {
        *violated = 0;
    }
    if (out == NULL) {
        return KST_PLAN_ERR_ARG;
    }
    if (start == NULL || target == NULL) {
        memset(out, 0, sizeof(*out));
        return KST_PLAN_ERR_ARG;
    }
    begin(out, start, target, KST_PLAN_EDIT);
    out->unlocked = unlocked;
    if (start->r[0] != target->r[0]) {
        return KST_PLAN_ERR_R00;
    }
    if (start->r[KST_REG_NODE_ADDR] != target->r[KST_REG_NODE_ADDR]) {
        return KST_PLAN_ERR_PAIRING;
    }
    broken = kst_limits_edit(start, target, unlocked, NULL) & KST_RULES_HARD;
    if (violated != NULL) {
        *violated = broken;
    }
    if (broken != 0u) {
        return KST_PLAN_ERR_RULES;
    }
    return build(out, false);
}

kst_plan_result_t kst_plan_restore(const kst_image_t *current,
                                   const kst_image_t *backup,
                                   kst_plan_t *out)
{
    if (out == NULL) {
        return KST_PLAN_ERR_ARG;
    }
    if (current == NULL || backup == NULL) {
        memset(out, 0, sizeof(*out));
        return KST_PLAN_ERR_ARG;
    }
    begin(out, current, backup, KST_PLAN_RESTORE);
    out->unlocked = ~(kst_field_set_t)0;
    if (current->r[0] != backup->r[0]) {
        return KST_PLAN_ERR_R00;
    }
    return build(out, true);
}

kst_plan_result_t kst_plan_release_pairing(const kst_image_t *current,
                                           kst_plan_t *out)
{
    build_t b;

    if (out == NULL) {
        return KST_PLAN_ERR_ARG;
    }
    if (current == NULL) {
        memset(out, 0, sizeof(*out));
        return KST_PLAN_ERR_ARG;
    }
    begin(out, current, current, KST_PLAN_RELEASE_PAIRING);
    out->target.r[KST_REG_NODE_ADDR] = 0x00;
    b.plan = out;
    b.img = out->start;
    b.allowed = 0;
    emit_if_changed(&b, KST_REG_NODE_ADDR);
    mark_settled(out);
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
