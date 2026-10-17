/*
 * Stick programming: the run.  See esc_stick.h.  What can run is in
 * esc_stick_rules.c, the beep detector in esc_det.c, and the readouts of a
 * run in esc_stick_read.c.
 *
 * Three rules carry the safety of the count, because a beep missed between
 * two readings turns one number into another:
 *
 *   - A group is in order when it follows the group before it in the
 *     menu's order: one more than it, the loop's lowest after its highest,
 *     or -- where the profile repeats each group -- the same again, after a
 *     group that was itself in order and no more times than the profile
 *     repeats it.  A group is acted on only when it is in order and the
 *     group before it was too: three groups in a row agree.  A count made
 *     wrong by a missed beep is one below the truth, so it breaks the order
 *     unless the groups before it were wrong as well; a single missed beep
 *     is passed, and the menu's next loop is used.
 *   - Listening starts on a quiet line: no group is counted until GROUP GAP
 *     of quiet, so the first group heard is whole and not the tail of one.
 *   - A reading that comes later than the shortest beep or gap allows
 *     breaks the order too, since a beep may have fallen in the hole.
 *     ESC_STICK_LATE_RUN late readings in a row end the run: the supply
 *     reads too slowly for the timing the settings describe.
 *
 * A run ends safely in one step when it is aborted: throttle to its rest,
 * supply off, disarmed.  A run that ends as planned, or cycles the power
 * between two changes, switches the supply off first and moves the stick
 * only once the supply itself reports the output off with the current under
 * ESC_STICK_OFF_MA for ESC_STICK_OFF_SETTLE_MS.  Neither the module's
 * off-state current nor the ESC's run-on from its input capacitors is
 * measured; docs/StickProgramming.md states both.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick.h"
#include "esc_stick_int.h"

#include <string.h>

void esc_stick_timing_defaults(esc_stick_timing_t *t)
{
    if (t == NULL) {
        return;
    }
    t->beep_min_ms   = 200u;
    t->gap_min_ms    = 200u;
    t->long_ms       = 500u;
    t->long_max_ms   = 1500u;
    t->group_gap_ms  = 700u;
    t->entry_ms      = 5000u;
    t->store_ms      = 2000u;
    t->off_ms        = 3000u;
    t->silence_ms    = 10000u;
    t->timeout_ms    = 180000u;
    t->threshold_ma  = 100u;
    t->hysteresis_ma = 40u;
}

uint32_t esc_stick_read_max_ms(const esc_stick_timing_t *t)
{
    if (t == NULL) {
        return 0u;
    }
    return (t->beep_min_ms < t->gap_min_ms) ? t->beep_min_ms : t->gap_min_ms;
}

/* ------------------------------------------------------------ the run */

/*
 * The time from @p then to @p now, 0 when @p then is later.  A reading is
 * stamped by the task that took it and the bench's time by the one that
 * steps the run, so a reading can carry a time a little after the step's.
 */
static uint32_t since(uint32_t now, uint32_t then)
{
    const int32_t d = (int32_t)(now - then);
    return (d > 0) ? (uint32_t)d : 0u;
}

static bool powered_phase(esc_stick_phase_t ph);

void esc_stick_light_reset(esc_stick_light_t *l, const esc_stick_t *e)
{
    if (l == NULL) {
        return;
    }
    l->pulses = (e != NULL) ? e->pulses : 0u;
    l->from_ms = 0u;
    l->held = false;
}

bool esc_stick_light_green(esc_stick_light_t *l, const esc_stick_t *e,
                           uint32_t now_ms)
{
    if (l == NULL || !esc_stick_running(e)) {
        esc_stick_light_reset(l, e);
        return false;
    }
    if (!powered_phase(e->phase)) {
        /* No reading reaches the detector here: a pulse it held when the
         * supply went off is not a beep. */
        l->pulses = e->pulses;
        l->held = false;
        return false;
    }
    if (e->pulses != l->pulses) {
        l->pulses = e->pulses;
        l->from_ms = now_ms;
        l->held = true;
    }
    if (l->held && since(now_ms, l->from_ms) >= ESC_STICK_BEEP_LIGHT_MS) {
        l->held = false;
    }
    return e->det.high || l->held;
}

static void enter(esc_stick_t *e, esc_stick_phase_t ph)
{
    e->phase = ph;
    e->phase_ms = e->now_ms;
}

/* The run is over: the throttle at its rest, the supply off, disarmed. */
static void finish(esc_stick_t *e, esc_stick_phase_t ph,
                   esc_stick_reason_t why)
{
    e->out.arm = false;
    e->out.throttle_pct = ESC_STICK_PCT_MIN;
    e->out.supply_on = false;
    e->reason = why;
    enter(e, ph);
}

void esc_stick_abort(esc_stick_t *e, esc_stick_reason_t why)
{
    if (esc_stick_running(e)) {
        finish(e, ESC_STICK_ABORTED, why);
    }
}

static bool powered_phase(esc_stick_phase_t ph)
{
    return ph == ESC_STICK_ENTRY || ph == ESC_STICK_HAND_ON
           || ph == ESC_STICK_HAND_END || ph == ESC_STICK_ITEMS
           || ph == ESC_STICK_VALUES || ph == ESC_STICK_STORE;
}

static bool all_done(const esc_stick_t *e)
{
    return esc_stick_done_count(e) == e->n;
}

/* The lowest and highest number a loop of items or of one item's values
 * sounds; @p item < 0 for every value of every item. */
static void bounds(esc_stick_t *e, bool items, int item)
{
    unsigned lo = 255u, hi = 0u;
    for (unsigned i = 0; i < e->p->item_count; ++i) {
        const esc_item_t *it = &e->p->items[i];
        if (items) {
            lo = (it->number < lo) ? it->number : lo;
            hi = (it->number > hi) ? it->number : hi;
            continue;
        }
        if (item >= 0 && (unsigned)item != i) {
            continue;
        }
        for (unsigned k = 0; k < it->value_count; ++k) {
            const unsigned v = it->values[k].number;
            lo = (v < lo) ? v : lo;
            hi = (v > hi) ? v : hi;
        }
    }
    e->lo = (uint8_t)lo;
    e->hi = (uint8_t)hi;
}

static void listen(esc_stick_t *e, esc_stick_phase_t ph)
{
    enter(e, ph);
    e->prev = 0u;
    e->prev_trusted = false;
    e->run = 0u;
    e->heard_ms = e->now_ms;
    esc_det_count(&e->det);
}

/* The menu begins: the stick to its rest, and the first loop. */
/* The position the next power-up enters from: the first change still to
 * make, which in a one-stage menu is the one this power-up makes, and in
 * any other is every change's (entry_refused()). */
static esc_throttle_t next_entry(const esc_stick_t *e)
{
    for (uint8_t i = 0; i < e->n; ++i) {
        if (!e->done[i]) {
            return esc_stick_change_entry(e->p, &e->ch[i]);
        }
    }
    return e->p->entry_throttle;
}

/* Where the stick rests while the menu sounds: the profile's listen move,
 * else where this power-up entered. */
static esc_throttle_t rest_of(const esc_stick_t *e)
{
    return (e->p->listen_throttle != ESC_THR_NONE) ? e->p->listen_throttle
                                                    : e->entry;
}

static void begin_menu(esc_stick_t *e)
{
    e->out.throttle_pct = esc_stick_pct(rest_of(e));
    if (e->kind == ESC_STICK_KIND_TWO_STAGE) {
        bounds(e, true, -1);
        listen(e, ESC_STICK_ITEMS);
        return;
    }
    /* One stage: the first change still to make, one per entry. */
    for (uint8_t i = 0; i < e->n; ++i) {
        if (!e->done[i]) {
            e->active = i;
            break;
        }
    }
    bounds(e, false, -1);
    listen(e, ESC_STICK_VALUES);
}

/*
 * The first manual step from @p from that is due at this point of the run,
 * or -1.  Before a power-up: each at_power_up step, and the before_power
 * steps from the second power-up on -- the warning the run started from
 * asked for the first power-up's.  Once the ESC is powered and entered:
 * each before_menu step.
 */
static int hand_due(const esc_stick_t *e, unsigned from, bool powered)
{
    const esc_profile_t *p = e->p;
    for (unsigned i = from; p->manual != NULL && i < p->manual_count; ++i) {
        const esc_manual_when_t w = p->manual[i].when;
        const bool due = powered
            ? w == ESC_MANUAL_BEFORE_MENU
            : (w == ESC_MANUAL_AT_POWER_UP
               || (w == ESC_MANUAL_BEFORE_POWER && e->entries > 0u));
        if (due) {
            return (int)i;
        }
    }
    return -1;
}

static void ask(esc_stick_t *e, int i, esc_stick_phase_t ph)
{
    e->hand = (uint8_t)i;
    e->hand_done = false;
    e->hand_menu = false;
    e->hand_ms = e->now_ms;
    enter(e, ph);
}

static void power_on(esc_stick_t *e)
{
    e->out.supply_on = true;
    e->have_sample = false;   /* the next one says whether it is on */
    enter(e, ESC_STICK_POWER);
}

/*
 * The supply comes on once every step due before it is done.  A step at an
 * unpowered ESC is asked for, and the supply switched on, only while the
 * supply itself reads off (off_seen): a run started with the output still
 * live, or a module that came on by itself, ends here with SUPPLY STAYS ON
 * rather than send a hand to a powered ESC.
 */
static void before_power(esc_stick_t *e, unsigned from)
{
    if (!e->off_seen) {
        finish(e, ESC_STICK_ABORTED, ESC_STICK_R_SUPPLY_ON);
        return;
    }
    /* Off, by a reading new enough to say so now. */
    if (since(e->now_ms, e->read_ms) > ESC_STICK_STALE_MS) {
        finish(e, ESC_STICK_ABORTED, ESC_STICK_R_STALE);
        return;
    }
    const int i = hand_due(e, from, false);
    if (i < 0) {
        power_on(e);
    } else {
        ask(e, i, ESC_STICK_HAND_OFF);
    }
}

/*
 * The menu begins once every step due before it is done.  A step the
 * profile marks starts_menu is the action that starts the menu -- the
 * jumper pulled, the button pressed: the ESC answers with its tones and
 * sounds the series at once -- so the run listens from the moment it asks
 * for it, with the stick where the power-up left it, and DONE is only a
 * way to say so early.  Every other step waits for DONE.  The first group the order rule finds in order with the one
 * before it is the menu running, and takes the step as done.  Where the
 * menu rests elsewhere the stick would move under the operator's hand, so
 * the run waits for DONE before listening, as for every earlier step.
 */
static void before_menu(esc_stick_t *e, unsigned from)
{
    const int i = hand_due(e, from, true);
    if (i < 0) {
        begin_menu(e);
        return;
    }
    if (e->p->manual[i].starts_menu && rest_of(e) == e->entry) {
        begin_menu(e);
        e->hand = (uint8_t)i;
        e->hand_done = false;
        e->hand_menu = true;
        e->hand_ms = e->now_ms;
        return;
    }
    ask(e, i, ESC_STICK_HAND_ON);
}

static bool hand_phase(esc_stick_phase_t ph)
{
    return ph == ESC_STICK_HAND_OFF || ph == ESC_STICK_HAND_ON
           || ph == ESC_STICK_HAND_END;
}

const esc_manual_t *esc_stick_hand(const esc_stick_t *e)
{
    if (!esc_stick_running(e) || !(hand_phase(e->phase) || e->hand_menu)
        || e->p->manual == NULL || e->hand >= e->p->manual_count) {
        return NULL;
    }
    return &e->p->manual[e->hand];
}

/*
 * The entry's time, and no less than the longest hold an at_power_up step
 * asks for: the menu, and a before_menu step, come only once the button
 * held while the supply came on may be let go.
 */
uint32_t esc_stick_change_entry_ms(const esc_profile_t *p,
                                   const esc_stick_change_t *c,
                                   const esc_stick_timing_t *t)
{
    uint32_t ms = (t != NULL) ? t->entry_ms : 0u;
    if (p == NULL) {
        return ms;
    }
    if (c != NULL && c->item < p->item_count
        && c->value < p->items[c->item].value_count
        && p->items[c->item].values[c->value].entry_hold_ms != 0u) {
        ms = p->items[c->item].values[c->value].entry_hold_ms;
    }
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        if (m->when == ESC_MANUAL_AT_POWER_UP && m->hold_ms > ms) {
            ms = m->hold_ms;
        }
    }
    return ms;
}

uint32_t esc_stick_entry_ms(const esc_stick_t *e)
{
    if (e == NULL || e->p == NULL) {
        return 0u;
    }
    /* The first change still to make is this power-up's (next_entry()). */
    for (uint8_t i = 0; i < e->n; ++i) {
        if (!e->done[i]) {
            return esc_stick_change_entry_ms(e->p, &e->ch[i], &e->t);
        }
    }
    return esc_stick_change_entry_ms(e->p, NULL, &e->t);
}

bool esc_stick_hand_ready(const esc_stick_t *e)
{
    return esc_stick_hand(e) != NULL && !e->hand_done
           && since(e->now_ms, e->hand_ms) >= ESC_STICK_HAND_MIN_MS;
}

uint32_t esc_stick_hand_left_ms(const esc_stick_t *e)
{
    if (esc_stick_hand(e) == NULL) {
        return 0u;
    }
    const uint32_t in = since(e->now_ms, e->hand_ms);
    return (in < ESC_STICK_HAND_WAIT_MS) ? ESC_STICK_HAND_WAIT_MS - in : 0u;
}

/*
 * DONE is taken here and acted on in the next step, after that step has
 * judged the stops, the arm, the link and the supply: a tap never powers
 * an ESC the bench has stopped or disarmed under it.
 */
bool esc_stick_confirm(esc_stick_t *e)
{
    if (!esc_stick_hand_ready(e)) {
        return false;
    }
    e->hand_done = true;
    return true;
}

/*
 * The moves after a selection, in order: the profile's store move, then
 * the moves the stored value asks for (after_select), each made once STORE
 * has passed since the one before -- the time the ESC takes to answer.  A
 * one-stage run stores one value a power-up, the active change's.
 */
esc_throttle_t esc_stick_store_move(const esc_stick_t *e, unsigned k)
{
    if (e == NULL || e->p == NULL) {
        return ESC_THR_NONE;
    }
    if (e->p->store_throttle != ESC_THR_NONE) {
        if (k == 0u) {
            return e->p->store_throttle;
        }
        --k;
    }
    if (e->kind != ESC_STICK_KIND_ONE_STAGE || e->active >= e->n) {
        return ESC_THR_NONE;
    }
    const esc_stick_change_t *c = &e->ch[e->active];
    const esc_value_t *v = &e->p->items[c->item].values[c->value];
    return (k < v->after_count && k < ESC_AFTER_MAX) ? v->after[k]
                                                      : ESC_THR_NONE;
}

/*
 * The supply off, the stick where it is; see the top of this file.  A
 * pulse under way is dropped with the group: no reading reaches the
 * detector until the next power-up starts it afresh.
 */
static void supply_off(esc_stick_t *e)
{
    e->out.supply_on = false;
    esc_det_drop_group(&e->det);
    e->off_seen = false;
    e->off_asked_ms = e->now_ms;
    e->off_seq = e->seq;
    e->off_since_known = false;
    e->cycle_moved = false;
    enter(e, all_done(e) ? ESC_STICK_OFF : ESC_STICK_CYCLE);
}

/*
 * The supply goes off once every before_power_off step from @p from is
 * done: each is asked with the ESC powered and the stick where the store
 * left it, and DONE goes on to the next or switches the supply off.
 */
static void before_off(esc_stick_t *e, unsigned from)
{
    const esc_profile_t *p = e->p;
    for (unsigned i = from; p->manual != NULL && i < p->manual_count; ++i) {
        if (p->manual[i].when == ESC_MANUAL_BEFORE_POWER_OFF) {
            ask(e, (int)i, ESC_STICK_HAND_END);
            e->end_open = true;
            return;
        }
    }
    e->end_open = false;
    supply_off(e);
}

bool esc_stick_cut_short(const esc_stick_t *e)
{
    return e != NULL && e->phase == ESC_STICK_ABORTED && e->end_open;
}

bool esc_stick_end_locks(const esc_stick_t *e)
{
    if (e == NULL || e->p == NULL || e->p->manual == NULL) {
        return false;
    }
    /* The lock is the profile's, whichever of its steps before the
     * power-off marks it: until every one is confirmed, a cut can lock. */
    for (unsigned i = 0; i < e->p->manual_count; ++i) {
        if (e->p->manual[i].when == ESC_MANUAL_BEFORE_POWER_OFF
            && e->p->manual[i].locks) {
            return true;
        }
    }
    return false;
}

bool esc_stick_lock_risk(const esc_stick_t *e)
{
    return esc_stick_cut_short(e) && esc_stick_end_locks(e);
}

/* The action that starts the menu is done: by DONE, or by the menu heard
 * in order.  SILENCE and TIMEOUT count from now. */
static void hand_menu_done(esc_stick_t *e)
{
    e->hand_menu = false;
    e->hand_done = true;
    e->heard_ms = e->now_ms;
    e->phase_ms = e->now_ms;
}

static uint8_t wanted_value(const esc_stick_t *e)
{
    const esc_stick_change_t *c = &e->ch[e->active];
    return e->p->items[c->item].values[c->value].number;
}

static void selected(esc_stick_t *e)
{
    e->done[e->active] = true;
    if (all_done(e) || e->p->one_change_per_entry) {
        e->store_step = 0u;
        /* From the selection on the ESC is storing and then confirming:
         * an end now switches the supply off under it, as one while the
         * step before the power-off is asked does.  A value stored by
         * moves after the selection is not stored until they are made. */
        e->end_open = esc_profile_manual_count(
                          e->p, ESC_MANUAL_BEFORE_POWER_OFF) > 0u
                      || esc_stick_store_move(e, 0u) != ESC_THR_NONE;
        enter(e, ESC_STICK_STORE);
        return;
    }
    /* The ESC goes back to its items; the stick stays where it stored. */
    bounds(e, true, -1);
    listen(e, ESC_STICK_ITEMS);
}

static void on_group(esc_stick_t *e, uint8_t count, bool valid)
{
    e->groups++;
    e->last_count = count;
    e->last_valid = valid;
    e->last_in_order = false;
    e->last_trusted = false;
    if (!valid) {
        e->prev = 0u;
        e->prev_trusted = false;
        e->run = 0u;
        return;
    }
    const uint8_t prev = e->prev;
    bool in_order = false;
    if (prev != 0u && count == prev) {
        /* The same again: only where the profile repeats its groups, after
         * a group that was in order, and no more often than it repeats. */
        in_order = e->p->repeat > 0 && e->prev_trusted
                   && (int)e->run < e->p->repeat;
    } else if (prev != 0u) {
        in_order = (unsigned)count == (unsigned)prev + 1u
                   || (count == e->lo && prev == e->hi);
    }
    const bool act = in_order && e->prev_trusted;
    e->run = (prev != 0u && count == prev && e->run < 255u)
                 ? (uint8_t)(e->run + 1u) : 1u;
    e->prev = count;
    e->prev_trusted = in_order;
    e->last_in_order = in_order;
    if (in_order && e->hand_menu) {
        hand_menu_done(e);          /* the menu runs: the action is done */
    }
    e->last_trusted = act;
    if (!act) {
        return;
    }
    if (e->phase == ESC_STICK_ITEMS) {
        for (uint8_t i = 0; i < e->n; ++i) {
            if (!e->done[i] && e->p->items[e->ch[i].item].number == count) {
                e->active = i;
                e->out.throttle_pct = esc_stick_pct(e->p->select_throttle);
                bounds(e, false, e->ch[i].item);
                listen(e, ESC_STICK_VALUES);
                return;
            }
        }
        return;
    }
    if (count != wanted_value(e)) {
        return;
    }
    e->out.throttle_pct = esc_stick_pct(
        (e->kind == ESC_STICK_KIND_TWO_STAGE) ? e->p->value_select_throttle
                                              : e->p->select_throttle);
    selected(e);
}

bool esc_stick_start(esc_stick_t *e, const esc_profile_t *p,
                     const esc_stick_change_t *ch, size_t n,
                     const esc_stick_timing_t *t, uint32_t mv, uint32_t ma,
                     const esc_stick_bench_t *b, const char **why)
{
    if (e == NULL || b == NULL) {
        return esc_stick_no(why, "no run");
    }
    if (!esc_stick_check(p, ch, n, t, why)) {
        return false;
    }
    memset(e, 0, sizeof(*e));
    e->p = p;
    e->kind = esc_stick_kind(p, NULL);
    e->t = *t;
    memcpy(e->ch, ch, n * sizeof(*ch));
    e->n = (uint8_t)n;
    e->now_ms = b->now_ms;
    e->stops0 = b->stops;
    e->pressed0 = b->pressed;
    e->link0 = b->link_up;
    e->link_seen = b->link_up;
    e->out.supply_mv = mv;
    e->out.supply_ma = ma;
    esc_det_init(&e->det, t,
                 (p->encoding == ESC_ENC_SHORT_LONG) ? p->long_equals_short
                                                     : 0u);
    /* An arm starts from the throttle's rest, with the supply off -- asked
     * off, and to be read off before anything is powered or asked of a
     * person (before_power()). */
    e->out.arm = true;
    e->out.throttle_pct = ESC_STICK_PCT_MIN;
    e->out.supply_on = false;
    e->off_seen = false;
    e->off_asked_ms = b->now_ms;
    e->off_since_known = false;
    e->entry_wait = esc_stick_entry_ms(e);
    enter(e, ESC_STICK_ARMING);
    return true;
}

/*
 * One reading while the run waits for the output to go off.  Only a
 * reading taken after the run asked it off counts -- a new count, stamped
 * after the request -- and only one in which the supply itself reports the
 * output off with the current at or under ESC_STICK_OFF_MA; such readings
 * have to run on for ESC_STICK_OFF_SETTLE_MS.  The panel's own flag says
 * only that it asked: the module switches off a link exchange and a module
 * transaction later, and until then the ESC is powered and in its menu.
 */
/* The phases in which the supply is to be off and its readings are
 * judged for it: before the first power-up, while a step at an unpowered
 * ESC is asked, and after the run asked the output off. */
static bool off_phase(esc_stick_phase_t ph)
{
    return ph == ESC_STICK_ARMING || ph == ESC_STICK_SIGNAL
           || ph == ESC_STICK_HAND_OFF || ph == ESC_STICK_CYCLE
           || ph == ESC_STICK_OFF;
}

/* Whether the stick has gone to the entry position, or a step at the ESC
 * is asked, with the supply off: the moves made only on the supply read
 * off, and so the ones a live reading has to end at once. */
static bool entry_made(const esc_stick_t *e)
{
    return (e->phase == ESC_STICK_SIGNAL && e->sig_moved)
           || (e->phase == ESC_STICK_CYCLE && e->cycle_moved)
           || e->phase == ESC_STICK_HAND_OFF;
}

static void off_reading(esc_stick_t *e, const esc_stick_sample_t *s)
{
    const bool after = s->seq != e->off_seq
                       && (int32_t)(s->at_ms - e->off_asked_ms) > 0;
    if (!after) {
        return;
    }
    const bool off = !s->reported_on && s->current_ok
                     && s->ma <= ESC_STICK_OFF_MA;
    if (!off) {
        /* On again, or the current up: whatever was seen before is not
         * the state now. */
        e->off_since_known = false;
        e->off_seen = false;
        return;
    }
    if (!e->off_since_known) {
        e->off_since_known = true;
        e->off_since_ms = s->at_ms;
    }
    if (since(s->at_ms, e->off_since_ms) >= ESC_STICK_OFF_SETTLE_MS) {
        e->off_seen = true;
    }
}

void esc_stick_sample(esc_stick_t *e, const esc_stick_sample_t *s)
{
    if (!esc_stick_running(e) || s == NULL) {
        return;
    }
    const bool fresh = !e->have_sample || s->seq != e->seq;
    e->have_sample = true;
    e->output = s->output;
    e->reported_on = s->reported_on;
    e->online = s->online;
    e->current_ok = s->current_ok;
    if (off_phase(e->phase) && !e->out.supply_on && fresh) {
        off_reading(e, s);
        /* The stick is at the entry position, or a person is at the ESC,
         * on the word that it is unpowered: a reading that says otherwise
         * ends the run at once -- throttle to MIN, supply off. */
        if (entry_made(e) && !e->off_seen) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_SUPPLY_ON);
            return;
        }
    }
    /* A supply gone is said at once, before its missing readings can be
     * taken for a slow one. */
    if (powered_phase(e->phase) && !s->online) {
        finish(e, ESC_STICK_ABORTED, ESC_STICK_R_SUPPLY_LOST);
        return;
    }
    if (powered_phase(e->phase) && (!s->output || !s->reported_on)) {
        finish(e, ESC_STICK_ABORTED, ESC_STICK_R_SUPPLY_OFF);
        return;
    }
    if (!fresh) {
        return;
    }
    const uint16_t step = (uint16_t)(s->seq - e->seq);
    e->seq = s->seq;
    const uint32_t iv = since(s->at_ms, e->read_ms);
    const bool had_reading = e->have_reading;
    e->read_ms = s->at_ms;
    e->ma = s->ma;
    if (!powered_phase(e->phase)) {
        e->iv_ms = 0u;
        e->late_run = 0u;
        return;
    }
    e->have_reading = true;
    if (!had_reading) {
        return;     /* the first of this power-up: no interval yet */
    }
    e->iv_ms = iv;
    /* A reading without a current is a reading missed, and so is one the
     * supply took and the panel never saw. */
    const bool late = iv > esc_stick_read_max_ms(&e->t) || !s->current_ok
                      || step > 1u;
    e->late_run = late ? e->late_run + 1u : 0u;
    if (e->late_run >= ESC_STICK_LATE_RUN) {
        finish(e, ESC_STICK_ABORTED, ESC_STICK_R_RATE);
        return;
    }
    if (!s->current_ok) {
        e->prev = 0u;
        return;
    }
    if (e->phase == ESC_STICK_ENTRY
        && since(s->at_ms, e->on_ms) < ESC_STICK_SETTLE_MS) {
        return;     /* the input capacitors, not the ESC */
    }
    const bool was_high = e->det.high;
    const esc_det_event_t ev = esc_det_reading(&e->det, s->at_ms, s->ma,
                                               late);
    if (!was_high && e->det.high) {
        e->pulses++;
    }
    const bool menu = e->phase == ESC_STICK_ITEMS
                      || e->phase == ESC_STICK_VALUES;
    if (late && menu) {
        e->prev = 0u;   /* a beep may have fallen in the hole */
    }
    switch (ev) {
    case ESC_DET_PULSE:
        e->heard_ms = s->at_ms;
        break;
    case ESC_DET_STUCK:
        finish(e, ESC_STICK_ABORTED, ESC_STICK_R_HIGH);
        break;
    case ESC_DET_GROUP:
        if (menu) {
            on_group(e, e->det.count, e->det.valid);
        }
        break;
    default:
        break;
    }
}

void esc_stick_step(esc_stick_t *e, const esc_stick_bench_t *b)
{
    if (!esc_stick_running(e) || b == NULL) {
        return;
    }
    e->now_ms = b->now_ms;
    const uint32_t in_phase = since(e->now_ms, e->phase_ms);

    if (b->stops != e->stops0) {
        finish(e, ESC_STICK_ABORTED,
               esc_stick_stop_reason(b->stops - e->stops0,
                                     b->pressed - e->pressed0));
        return;
    }
    /* A link that was up and went is lost, whenever it came up. */
    if (b->link_up) {
        e->link_seen = true;
    } else if (e->link_seen) {
        finish(e, ESC_STICK_ABORTED, ESC_STICK_R_LINK);
        return;
    }
    if (e->phase == ESC_STICK_ARMING) {
        if (b->armed) {
            /* Armed, the stick still at MIN: it goes to the entry position
             * only once the supply reads off (SIGNAL). */
            e->armed_seen = true;
            e->entry = next_entry(e);
            e->sig_moved = false;
            enter(e, ESC_STICK_SIGNAL);
        } else if (in_phase >= ESC_STICK_ARM_WAIT_MS) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_NOT_ARMED);
        }
        return;
    }
    if (!b->armed) {
        finish(e, ESC_STICK_ABORTED, ESC_STICK_R_DISARMED);
        return;
    }
    if (powered_phase(e->phase)) {
        if (!e->online) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_SUPPLY_LOST);
            return;
        }
        if (!e->output || !e->reported_on) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_SUPPLY_OFF);
            return;
        }
        if (since(e->now_ms, e->read_ms) > ESC_STICK_STALE_MS) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_STALE);
            return;
        }
    }

    /* The stick at the entry position, or a person at the ESC, with the
     * supply off: no reading for ESC_STICK_STALE_MS says it still is. */
    if (entry_made(e) && since(e->now_ms, e->read_ms) > ESC_STICK_STALE_MS) {
        finish(e, ESC_STICK_ABORTED, ESC_STICK_R_STALE);
        return;
    }

    switch (e->phase) {
    case ESC_STICK_SIGNAL:
        /* The stick stays at MIN until the supply reads off in readings
         * taken since the run began; not off within ESC_STICK_POWER_WAIT_MS:
         * SUPPLY STAYS ON, and nothing is moved or asked of a person.  Off:
         * the stick to the entry position, and the signal's time again
         * from there, as a power cycle does. */
        if (!e->sig_moved) {
            if (e->off_seen) {
                e->sig_moved = true;
                e->out.throttle_pct = esc_stick_pct(e->entry);
                enter(e, ESC_STICK_SIGNAL);
            } else if (in_phase >= ESC_STICK_POWER_WAIT_MS) {
                finish(e, ESC_STICK_ABORTED, ESC_STICK_R_SUPPLY_ON);
            }
        } else if (in_phase >= ESC_STICK_SIGNAL_MS) {
            before_power(e, 0u);
        }
        break;
    case ESC_STICK_HAND_OFF:
    case ESC_STICK_HAND_ON:
    case ESC_STICK_HAND_END:
        if (e->hand_done) {
            if (e->phase == ESC_STICK_HAND_OFF) {
                before_power(e, (unsigned)e->hand + 1u);
            } else if (e->phase == ESC_STICK_HAND_ON) {
                before_menu(e, (unsigned)e->hand + 1u);
            } else {
                before_off(e, (unsigned)e->hand + 1u);
            }
        } else if (in_phase >= ESC_STICK_HAND_WAIT_MS) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_HAND);
        }
        break;
    case ESC_STICK_POWER:
        if (e->have_sample && e->output && e->reported_on && e->online
            && e->current_ok) {
            e->on_ms = e->now_ms;
            e->read_ms = e->now_ms;   /* the stale clock starts here */
            e->have_reading = false;
            e->late_run = 0u;
            e->entries++;
            /* This power-up's wait, kept for it and for the result after
             * the change it makes is done. */
            e->entry_wait = esc_stick_entry_ms(e);
            esc_det_init(&e->det, &e->t, e->det.long_equals_short);
            esc_det_floor_only(&e->det);
            enter(e, ESC_STICK_ENTRY);
        } else if (in_phase >= ESC_STICK_POWER_WAIT_MS) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_NO_POWER);
        }
        break;
    case ESC_STICK_ENTRY:
        if (since(e->now_ms, e->on_ms) >= e->entry_wait) {
            before_menu(e, 0u);
        }
        break;
    case ESC_STICK_ITEMS:
    case ESC_STICK_VALUES:
        if (e->hand_menu) {
            /* Listening while the action is asked for: the ESC is silent
             * until it is done, so SILENCE and TIMEOUT run from then. */
            if (e->hand_done) {
                hand_menu_done(e);
            } else if (since(e->now_ms, e->hand_ms)
                       >= ESC_STICK_HAND_WAIT_MS) {
                finish(e, ESC_STICK_ABORTED, ESC_STICK_R_HAND);
            }
            break;
        }
        if (since(e->now_ms, e->heard_ms) >= e->t.silence_ms) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_NO_BEEPS);
        } else if (in_phase >= e->t.timeout_ms) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_TIMEOUT);
        }
        break;
    case ESC_STICK_STORE:
        if (in_phase < e->t.store_ms) {
            break;
        }
        if (esc_stick_store_move(e, e->store_step) != ESC_THR_NONE) {
            /* The ESC has answered the selection, or the move before: the
             * next move, held as long again. */
            e->out.throttle_pct =
                esc_stick_pct(esc_stick_store_move(e, e->store_step));
            e->store_step++;
            enter(e, ESC_STICK_STORE);
            break;
        }
        /* Off first, once the ESC has confirmed where the profile asks to
         * watch it do so. */
        before_off(e, 0u);
        break;
    case ESC_STICK_OFF:
        if (e->off_seen) {
            finish(e, ESC_STICK_DONE, ESC_STICK_R_NONE);
        } else if (in_phase >= ESC_STICK_POWER_WAIT_MS) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_SUPPLY_ON);
        }
        break;
    case ESC_STICK_CYCLE:
        if (!e->off_seen) {
            if (in_phase >= ESC_STICK_POWER_WAIT_MS) {
                finish(e, ESC_STICK_ABORTED, ESC_STICK_R_SUPPLY_ON);
            }
            break;
        }
        /* Off: the stick to the entry position, and in again after OFF
         * TIME -- no less than the signal time -- with it there. */
        if (!e->cycle_moved) {
            e->cycle_moved = true;
            e->entry = next_entry(e);
            e->out.throttle_pct = esc_stick_pct(e->entry);
            enter(e, ESC_STICK_CYCLE);
            break;
        }
        if (in_phase >= e->t.off_ms && in_phase >= ESC_STICK_SIGNAL_MS) {
            before_power(e, 0u);
        }
        break;
    default:
        break;
    }
}
