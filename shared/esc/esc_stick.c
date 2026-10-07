/*
 * Stick programming: the beep detector and the run.  See esc_stick.h.
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

/* ------------------------------------------------------- what can run */

esc_throttle_t esc_stick_listen(const esc_profile_t *p)
{
    if (p == NULL) {
        return ESC_THR_NONE;
    }
    return (p->listen_throttle != ESC_THR_NONE) ? p->listen_throttle
                                                : p->entry_throttle;
}

float esc_stick_pct(esc_throttle_t pos)
{
    switch (pos) {
    case ESC_THR_MID: return ESC_STICK_PCT_MID;
    case ESC_THR_MAX: return ESC_STICK_PCT_MAX;
    default:          return ESC_STICK_PCT_MIN;
    }
}

/* Whether any value number appears in two items. */
static bool values_repeat(const esc_profile_t *p)
{
    bool seen[256];
    memset(seen, 0, sizeof(seen));
    for (unsigned i = 0; i < p->item_count; ++i) {
        const esc_item_t *it = &p->items[i];
        for (unsigned k = 0; k < it->value_count; ++k) {
            const uint8_t v = it->values[k].number;
            if (seen[v]) {
                return true;
            }
            seen[v] = true;
        }
    }
    return false;
}

/*
 * Whether every manual step can be waited for.  A step during the menu has
 * no moment the run can know.  A hand at a powered ESC -- held while the
 * supply comes on, or after the entry -- is asked for with the stick where
 * the entry put it, so only for an entry at MIN, the motor-off position: a
 * person is not asked to reach for a powered ESC whose stick is at MID or
 * MAX.
 */
static bool hand_fits(const esc_profile_t *p)
{
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        switch (p->manual[i].when) {
        case ESC_MANUAL_BEFORE_POWER:
        case ESC_MANUAL_AFTER_PROGRAMMING:
            break;
        case ESC_MANUAL_AT_POWER_UP:
        case ESC_MANUAL_BEFORE_MENU:
            if (p->entry_throttle != ESC_THR_MIN) {
                return false;
            }
            break;
        case ESC_MANUAL_DURING_MENU:
        default:
            return false;
        }
    }
    return true;
}

static esc_stick_kind_t refuse(const char **why, const char *text)
{
    if (why != NULL) {
        *why = text;
    }
    return ESC_STICK_KIND_NONE;
}

esc_stick_kind_t esc_stick_kind(const esc_profile_t *p, const char **why)
{
    if (why != NULL) {
        *why = "";
    }
    if (p == NULL) {
        return refuse(why, "no profile");
    }
    if (p->automatable == ESC_AUTO_ASSISTED
        && (p->manual_count == 0u || p->manual == NULL)) {
        return refuse(why, "needs a person at the ESC");
    }
    if (p->automatable != ESC_AUTO_FULL
        && p->automatable != ESC_AUTO_ASSISTED) {
        return refuse(why, "no usable procedure");
    }
    if (p->entry_after_power) {
        return refuse(why, "entered after power-on");
    }
    if (!hand_fits(p)) {
        return refuse(why, "manual step");
    }
    switch (p->scheme) {
    case ESC_SCHEME_COUNT:
    case ESC_SCHEME_SHORT_LONG:
        break;
    case ESC_SCHEME_MELODY_GROUPS:
        return refuse(why, "melody menu");
    case ESC_SCHEME_YES_NO:
        return refuse(why, "yes/no menu");
    case ESC_SCHEME_STICK_POSITION:
        return refuse(why, "stick-position menu");
    default:
        return refuse(why, "menu of its own kind");
    }
    if (p->encoding != ESC_ENC_COUNT && p->encoding != ESC_ENC_SHORT_LONG) {
        return refuse(why, "tones told apart by pitch");
    }
    if (p->item_count == 0u || p->items == NULL) {
        return refuse(why, "no items");
    }
    if (p->select_throttle == ESC_THR_NONE) {
        return refuse(why, "no select move");
    }
    const esc_throttle_t rest = esc_stick_listen(p);
    /*
     * A move to the rest position at the end of the entry, at a time the
     * profile does not give, can land in another stage of the entry -- in
     * YGE's, a stick teach -- so it is not guessed.
     */
    if (rest != p->entry_throttle && p->entry_hold_ms == 0u) {
        return refuse(why, "rest move, no entry time");
    }
    if (p->value_select_throttle != ESC_THR_NONE) {
        if (p->store_throttle != ESC_THR_NONE) {
            return refuse(why, "store move, two stages");
        }
        if (p->announce != ESC_ANNOUNCE_ITEM_THEN_VALUE) {
            return refuse(why, "two moves, no value tones");
        }
        if (p->select_throttle == rest) {
            return refuse(why, "select move is the rest");
        }
        if (p->value_select_throttle == p->select_throttle) {
            return refuse(why, "value move = select move");
        }
        return ESC_STICK_KIND_TWO_STAGE;
    }
    if (p->announce == ESC_ANNOUNCE_ITEM_THEN_VALUE) {
        return refuse(why, "item and value, one move");
    }
    if (!p->one_change_per_entry) {
        return refuse(why, "many changes, one stage");
    }
    if (p->announce == ESC_ANNOUNCE_ITEM && p->item_count > 1u) {
        return refuse(why, "items counted, one stage");
    }
    if (values_repeat(p)) {
        return refuse(why, "values repeat across items");
    }
    if (p->select_throttle == rest) {
        return refuse(why, "select move is the rest");
    }
    if (p->store_throttle == p->select_throttle) {
        return refuse(why, "store move = select move");
    }
    return ESC_STICK_KIND_ONE_STAGE;
}

uint32_t esc_stick_profile_mv(const esc_profile_t *p)
{
    if (p == NULL) {
        return 0u;
    }
    uint32_t best = 0u;
    for (unsigned i = 0; i < p->model_count; ++i) {
        const esc_model_t *m = &p->models[i];
        if (m->cells_min == 0u) {
            continue;
        }
        const uint32_t mv = (uint32_t)m->cells_min * (m->nimh ? 1200u : 3800u);
        if (best == 0u || mv < best) {
            best = mv;
        }
    }
    return best;
}

bool esc_stick_is_action(const esc_item_t *it)
{
    return it != NULL && it->key != NULL
           && (strcmp(it->key, "reset") == 0 || strcmp(it->key, "exit") == 0);
}

const char *esc_stick_not_offered(const esc_item_t *it)
{
    if (it == NULL) {
        return "no such item";
    }
    if (esc_stick_is_action(it)) {
        return "reset and exit are actions, not settings";
    }
    if (it->value_count < 2u) {
        return "one value: nothing to choose";
    }
    return NULL;
}

esc_throttle_t esc_stick_change_entry(const esc_profile_t *p,
                                      const esc_stick_change_t *c)
{
    if (p == NULL || c == NULL || c->item >= p->item_count
        || c->value >= p->items[c->item].value_count) {
        return (p != NULL) ? p->entry_throttle : ESC_THR_MIN;
    }
    const esc_throttle_t v = p->items[c->item].values[c->value].entry_throttle;
    return (v != ESC_THR_NONE) ? v : p->entry_throttle;
}

/* Whether the profile asks for a hand at a powered ESC. */
static bool hand_powered(const esc_profile_t *p)
{
    return esc_profile_manual_count(p, ESC_MANUAL_AT_POWER_UP) > 0u
           || esc_profile_manual_count(p, ESC_MANUAL_BEFORE_MENU) > 0u;
}

/*
 * Why change @p i cannot be powered up from its own entry position, or
 * NULL.  The run powers each change up from the position its value is
 * programmed from (esc_stick_change_entry()): a Kontronik car mode from
 * the middle, the neutral the mode teaches.  The profile-wide rules of
 * esc_stick_kind() hold for that position as they hold for the profile's:
 *
 *   - A power-up takes one position.  A two-stage menu, or one that takes
 *     several changes a power-up, makes its changes in the order the ESC
 *     sounds them, not the order asked, so every change of the run shares
 *     the first one's position; a mix is refused, not reordered.  A
 *     one-stage menu takes one change a power-up and powers each up from
 *     its own.
 *   - The rest is that position where the profile names none, and the
 *     select move has to differ from it.
 *   - A power-up's entry time is its value's entry_hold_ms where the
 *     manual gives one, else the timing's entry; the changes a power-up
 *     shares share one.  A move to a named rest needs a time the profile
 *     or the value states.
 *   - A hand at a powered ESC with the stick at MAX is not asked for.  MID
 *     is, where the value names it: the manual's motor-off in the middle.
 */
static const char *entry_refused(const esc_profile_t *p,
                                 const esc_stick_change_t *ch, size_t i,
                                 const esc_stick_timing_t *t)
{
    const esc_throttle_t from = esc_stick_change_entry(p, &ch[i]);
    const uint32_t wait = esc_stick_change_entry_ms(p, &ch[i], t);
    const bool shared = p->value_select_throttle != ESC_THR_NONE
                        || !p->one_change_per_entry;
    if (shared && from != esc_stick_change_entry(p, &ch[0])) {
        return "changes need different power-up positions";
    }
    if (shared && wait != esc_stick_change_entry_ms(p, &ch[0], t)) {
        return "changes need different entry times";
    }
    if (wait <= ESC_STICK_SETTLE_MS) {
        return "ENTRY above 500 ms";
    }
    const esc_throttle_t rest = (p->listen_throttle != ESC_THR_NONE)
                                    ? p->listen_throttle : from;
    const bool timed = p->entry_hold_ms != 0u
                       || p->items[ch[i].item].values[ch[i].value]
                              .entry_hold_ms != 0u;
    if (rest != from && !timed) {
        return "rest move, no entry time";
    }
    if (p->select_throttle == rest) {
        return "select move is the rest";
    }
    if (from == ESC_THR_MAX && from != p->entry_throttle && hand_powered(p)) {
        return "manual step";
    }
    return NULL;
}

static bool no(const char **why, const char *text)
{
    if (why != NULL) {
        *why = text;
    }
    return false;
}

bool esc_stick_check(const esc_profile_t *p, const esc_stick_change_t *ch,
                     size_t n, const esc_stick_timing_t *t, const char **why)
{
    if (esc_stick_kind(p, why) == ESC_STICK_KIND_NONE) {
        return false;
    }
    if (t == NULL) {
        return no(why, "no timing");
    }
    if (ch == NULL || n == 0u) {
        return no(why, "nothing to change");
    }
    if (n > ESC_STICK_MAX_CHANGES) {
        return no(why, "too many changes");
    }
    for (size_t i = 0; i < n; ++i) {
        if (ch[i].item >= p->item_count) {
            return no(why, "no such item");
        }
        const esc_item_t *it = &p->items[ch[i].item];
        const char *not_offered = esc_stick_not_offered(it);
        if (not_offered != NULL) {
            return no(why, not_offered);
        }
        if (ch[i].value >= it->value_count) {
            return no(why, "no such value");
        }
        /* N beeps say N: a value numbered 0 is never sounded. */
        if (it->values[ch[i].value].number == 0u) {
            return no(why, "value 0 is not sounded");
        }
        const char *bad = entry_refused(p, ch, i, t);
        if (bad != NULL) {
            return no(why, bad);
        }
        for (size_t k = 0; k < i; ++k) {
            if (p->items[ch[k].item].number == it->number) {
                return no(why, "one change per item");
            }
        }
    }
    if (t->beep_min_ms == 0u || t->gap_min_ms == 0u) {
        return no(why, "BEEP MIN and GAP MIN above 0");
    }
    if (t->long_ms <= t->beep_min_ms) {
        return no(why, "LONG above BEEP MIN");
    }
    if (t->long_max_ms <= t->long_ms) {
        return no(why, "LONG MAX above LONG");
    }
    if (t->group_gap_ms <= t->gap_min_ms) {
        return no(why, "GROUP GAP above GAP MIN");
    }
    if (t->threshold_ma <= t->hysteresis_ma) {
        return no(why, "THRESHOLD above HYSTERESIS");
    }
    if (t->entry_ms <= ESC_STICK_SETTLE_MS) {
        return no(why, "ENTRY above 500 ms");
    }
    /*
     * The move is made GROUP GAP after the first quiet reading, which can
     * come a reading interval after the beep: both have to fit the window
     * the manual gives.
     */
    const uint32_t late = t->group_gap_ms + esc_stick_read_max_ms(t);
    if (p->select_within_ms != 0u && late >= p->select_within_ms) {
        return no(why, "GROUP GAP misses the select window");
    }
    if (p->value_select_within_ms != 0u && late >= p->value_select_within_ms) {
        return no(why, "GROUP GAP misses the value window");
    }
    return true;
}

/* ---------------------------------------------------------- the beeps */

void esc_det_init(esc_det_t *d, const esc_stick_timing_t *t,
                  uint8_t long_equals_short)
{
    if (d == NULL || t == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->t = *t;
    d->long_equals_short = long_equals_short;
}

void esc_det_drop_group(esc_det_t *d)
{
    d->high = false;
    d->group_open = false;
    d->group_bad = false;
    d->shorts = 0u;
    d->longs = 0u;
    d->n_high = 0u;
    d->n_low = 0u;
    d->stuck_told = false;
}

void esc_det_floor_only(esc_det_t *d)
{
    d->counting = false;
    esc_det_drop_group(d);
}

void esc_det_count(esc_det_t *d)
{
    /* Quiet since the last pulse ended, when one did and nothing has risen
     * since: then the gap already heard counts toward the quiet. */
    const bool quiet = d->counting && !d->high && d->fall_ms != 0u;
    const uint32_t from = d->fall_ms;
    d->counting = true;
    esc_det_drop_group(d);
    d->wait_quiet = true;
    d->quiet_known = quiet;
    d->quiet_from = from;
    d->busy = false;
}

int32_t esc_det_floor_ma(const esc_det_t *d)
{
    return d->floor_x16 / 16;
}

/* How many readings a span of @p ms must hold, at the longest interval seen
 * that was not late.  0 while no interval is known. */
static uint32_t readings_in(const esc_det_t *d, uint32_t ms)
{
    return (d->iv_max_ms == 0u) ? 0u : ms / d->iv_max_ms;
}

static void close_pulse(esc_det_t *d, uint32_t t_ms)
{
    d->high = false;
    d->fall_ms = t_ms;
    d->n_low = 1u;
    /*
     * The beep began after the reading before the rise and ended before this
     * one: between the two spans lies the true length.  The shorter span is
     * never longer than the beep, so a pulse is judged too long on it alone.
     */
    const uint32_t lo = d->last_high_ms - d->rise_ms;
    const uint32_t hi = t_ms - d->before_rise_ms;
    const uint32_t est = (lo + hi) / 2u;
    if (d->n_high < readings_in(d, d->t.beep_min_ms)
        || lo > d->t.long_max_ms) {
        d->group_bad = true;               /* shorter or longer than a beep */
    } else if (est >= d->t.long_ms) {
        if (d->long_equals_short == 0u || d->shorts > 0u) {
            d->group_bad = true;           /* no long here, or after a short */
        } else if (d->longs < 255u) {
            d->longs++;
        }
    } else if (d->shorts < 255u) {
        d->shorts++;
    }
}

esc_det_event_t esc_det_reading(esc_det_t *d, uint32_t t_ms, int32_t ma,
                                bool late)
{
    if (d == NULL) {
        return ESC_DET_NONE;
    }
    if (d->have_prev && !late) {
        const uint32_t iv = t_ms - d->prev_ms;
        if (iv > d->iv_max_ms) {
            d->iv_max_ms = iv;
        }
    }
    const uint32_t before = d->have_prev ? d->prev_ms : t_ms;
    d->have_prev = true;
    d->prev_ms = t_ms;

    const int32_t x16 = ma * 16;
    if (!d->seeded) {
        d->seeded = true;
        d->floor_x16 = x16;
    }
    if (!d->counting) {
        /* The floor while the ESC starts: the lowest current seen, which a
         * beep cannot raise. */
        if (x16 < d->floor_x16) {
            d->floor_x16 = x16;
        }
        return ESC_DET_NONE;
    }

    if (late && (d->group_open || d->high)) {
        d->group_bad = true;
    }
    const int32_t on  = d->floor_x16 + (int32_t)d->t.threshold_ma * 16;
    const int32_t off = on - (int32_t)d->t.hysteresis_ma * 16;

    if (d->wait_quiet) {
        /* Anything over the release, or a late reading that may hide a
         * beep, is not quiet. */
        if (x16 > off || late) {
            d->quiet_known = false;
            if (x16 > off && !d->busy) {
                d->busy = true;
                d->busy_from = t_ms;
            } else if (x16 > off && !d->stuck_told
                       && t_ms - d->busy_from > 2u * d->t.long_max_ms) {
                d->stuck_told = true;
                return ESC_DET_STUCK;
            }
            return ESC_DET_NONE;
        }
        d->busy = false;
        d->floor_x16 += (x16 - d->floor_x16) / 16;
        if (!d->quiet_known) {
            d->quiet_known = true;
            d->quiet_from = t_ms;
        }
        if (t_ms - d->quiet_from >= d->t.group_gap_ms) {
            d->wait_quiet = false;
        }
        return ESC_DET_NONE;
    }

    if (d->high) {
        if (x16 < off) {
            close_pulse(d, t_ms);
            return ESC_DET_PULSE;
        }
        d->n_high++;
        d->last_high_ms = t_ms;
        if (!d->stuck_told && t_ms - d->rise_ms > 2u * d->t.long_max_ms) {
            d->stuck_told = true;
            return ESC_DET_STUCK;
        }
        return ESC_DET_NONE;
    }
    if (x16 > on) {
        if (d->group_open) {
            if (d->n_low < readings_in(d, d->t.gap_min_ms)) {
                d->group_bad = true;       /* a gap shorter than any gap */
            }
        } else {
            d->group_open = true;
            d->group_bad = late;
            d->shorts = 0u;
            d->longs = 0u;
        }
        d->high = true;
        d->rise_ms = t_ms;
        d->last_high_ms = t_ms;
        d->before_rise_ms = before;
        d->n_high = 1u;
        d->stuck_told = false;
        return ESC_DET_NONE;
    }
    /* Quiet: the floor follows the idle current, slowly. */
    d->floor_x16 += (x16 - d->floor_x16) / 16;
    d->n_low++;
    if (d->group_open && t_ms - d->fall_ms >= d->t.group_gap_ms) {
        d->group_open = false;
        const uint32_t n = (uint32_t)d->longs * d->long_equals_short
                           + d->shorts;
        d->count = (uint8_t)((n > 255u) ? 255u : n);
        d->valid = !d->group_bad && n > 0u && n <= 255u;
        d->shorts = 0u;
        d->longs = 0u;
        d->group_bad = false;
        return ESC_DET_GROUP;
    }
    return ESC_DET_NONE;
}

/* ------------------------------------------------------------ the run */

const char *esc_stick_reason_text(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_NONE:        return "";
    case ESC_STICK_R_STOP:        return "STOP";
    case ESC_STICK_R_BENCH_STOP:  return "BENCH STOPPED";
    case ESC_STICK_R_DISARMED:    return "DISARMED";
    case ESC_STICK_R_LINK:        return "LINK LOST";
    case ESC_STICK_R_SUPPLY_OFF:  return "SUPPLY OFF";
    case ESC_STICK_R_SUPPLY_LOST: return "SUPPLY NOT ANSWERING";
    case ESC_STICK_R_STALE:       return "NO READINGS";
    case ESC_STICK_R_RATE:        return "READ RATE";
    case ESC_STICK_R_NOT_ARMED:   return "NOT ARMED";
    case ESC_STICK_R_NO_POWER:    return "NO POWER";
    case ESC_STICK_R_SUPPLY_ON:   return "SUPPLY STAYS ON";
    case ESC_STICK_R_TOUCH:       return "TOUCH LOST";
    case ESC_STICK_R_NO_BEEPS:    return "NO BEEPS";
    case ESC_STICK_R_HIGH:        return "CURRENT STAYS HIGH";
    case ESC_STICK_R_TIMEOUT:     return "TIMEOUT";
    case ESC_STICK_R_HAND:        return "NOT CONFIRMED";
    case ESC_STICK_R_USER:        return "ABORTED";
    case ESC_STICK_R_LEFT:        return "SCREEN LEFT";
    }
    return "?";
}

esc_stick_reason_t esc_stick_stop_reason(uint32_t stops, uint32_t pressed)
{
    return (stops > pressed) ? ESC_STICK_R_BENCH_STOP : ESC_STICK_R_STOP;
}

/*
 * Every reason decided here, without a default, so a reason added to the
 * list is decided too: -Wswitch names it.
 */
bool esc_stick_reason_is_fault(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_NONE:          /* DONE, or not ended            */
    case ESC_STICK_R_STOP:          /* STOP pressed                  */
    case ESC_STICK_R_USER:          /* ABORT                         */
    case ESC_STICK_R_LEFT:          /* the screen left               */
        return false;
    case ESC_STICK_R_BENCH_STOP:    /* touch, or the far end         */
    case ESC_STICK_R_DISARMED:
    case ESC_STICK_R_LINK:
    case ESC_STICK_R_SUPPLY_OFF:
    case ESC_STICK_R_SUPPLY_LOST:
    case ESC_STICK_R_STALE:
    case ESC_STICK_R_RATE:
    case ESC_STICK_R_NOT_ARMED:
    case ESC_STICK_R_NO_POWER:
    case ESC_STICK_R_SUPPLY_ON:
    case ESC_STICK_R_NO_BEEPS:
    case ESC_STICK_R_HIGH:
    case ESC_STICK_R_TIMEOUT:
    case ESC_STICK_R_HAND:          /* no DONE: not chosen either    */
    case ESC_STICK_R_TOUCH:         /* events lost, not chosen       */
        return true;
    }
    return true;        /* a value outside the list is not as expected */
}

const char *esc_stick_phase_text(esc_stick_phase_t ph)
{
    switch (ph) {
    case ESC_STICK_IDLE:    return "READY";
    case ESC_STICK_ARMING:  return "ARMING";
    case ESC_STICK_SIGNAL:  return "SIGNAL";
    case ESC_STICK_POWER:   return "POWER ON";
    case ESC_STICK_ENTRY:   return "ENTRY";
    case ESC_STICK_ITEMS:   return "ITEMS";
    case ESC_STICK_VALUES:  return "VALUES";
    case ESC_STICK_STORE:   return "STORING";
    case ESC_STICK_CYCLE:   return "POWER CYCLE";
    case ESC_STICK_HAND_OFF: return "MANUAL STEP";
    case ESC_STICK_HAND_ON: return "MANUAL STEP, POWERED";
    case ESC_STICK_OFF:     return "POWER OFF";
    case ESC_STICK_DONE:    return "DONE";
    case ESC_STICK_ABORTED: return "ABORTED";
    }
    return "?";
}

bool esc_stick_running(const esc_stick_t *e)
{
    return e != NULL && e->phase != ESC_STICK_IDLE
           && e->phase != ESC_STICK_DONE && e->phase != ESC_STICK_ABORTED;
}

const esc_stick_out_t *esc_stick_out(const esc_stick_t *e)
{
    return (e != NULL) ? &e->out : NULL;
}

unsigned esc_stick_done_count(const esc_stick_t *e)
{
    unsigned n = 0u;
    for (unsigned i = 0; e != NULL && i < e->n; ++i) {
        n += e->done[i] ? 1u : 0u;
    }
    return n;
}

unsigned esc_stick_beeps(const esc_stick_t *e)
{
    if (e == NULL || !e->det.group_open) {
        return 0u;
    }
    return (unsigned)e->det.longs * e->det.long_equals_short + e->det.shorts
           + (e->det.high ? 1u : 0u);
}

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
           || ph == ESC_STICK_ITEMS
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
    enter(e, ph);
}

static void power_on(esc_stick_t *e)
{
    e->out.supply_on = true;
    e->have_sample = false;   /* the next one says whether it is on */
    enter(e, ESC_STICK_POWER);
}

/* The supply comes on once every step due before it is done. */
static void before_power(esc_stick_t *e, unsigned from)
{
    const int i = hand_due(e, from, false);
    if (i < 0) {
        power_on(e);
    } else {
        ask(e, i, ESC_STICK_HAND_OFF);
    }
}

/* The menu begins once every step due before it is done. */
static void before_menu(esc_stick_t *e, unsigned from)
{
    const int i = hand_due(e, from, true);
    if (i < 0) {
        begin_menu(e);
    } else {
        ask(e, i, ESC_STICK_HAND_ON);
    }
}

static bool hand_phase(esc_stick_phase_t ph)
{
    return ph == ESC_STICK_HAND_OFF || ph == ESC_STICK_HAND_ON;
}

const esc_manual_t *esc_stick_hand(const esc_stick_t *e)
{
    if (!esc_stick_running(e) || !hand_phase(e->phase)
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
    const uint32_t entry = (t != NULL) ? t->entry_ms : 0u;
    if (p == NULL || c == NULL || c->item >= p->item_count
        || c->value >= p->items[c->item].value_count) {
        return entry;
    }
    const uint32_t v = p->items[c->item].values[c->value].entry_hold_ms;
    return (v != 0u) ? v : entry;
}

uint32_t esc_stick_entry_ms(const esc_stick_t *e)
{
    if (e == NULL || e->p == NULL) {
        return 0u;
    }
    /* The first change still to make is this power-up's (next_entry()). */
    uint32_t ms = e->t.entry_ms;
    for (uint8_t i = 0; i < e->n; ++i) {
        if (!e->done[i]) {
            ms = esc_stick_change_entry_ms(e->p, &e->ch[i], &e->t);
            break;
        }
    }
    for (unsigned i = 0; e->p->manual != NULL && i < e->p->manual_count;
         ++i) {
        const esc_manual_t *m = &e->p->manual[i];
        if (m->when == ESC_MANUAL_AT_POWER_UP && m->hold_ms > ms) {
            ms = m->hold_ms;
        }
    }
    return ms;
}

bool esc_stick_hand_ready(const esc_stick_t *e)
{
    return esc_stick_hand(e) != NULL && !e->hand_done
           && since(e->now_ms, e->phase_ms) >= ESC_STICK_HAND_MIN_MS;
}

uint32_t esc_stick_hand_left_ms(const esc_stick_t *e)
{
    if (esc_stick_hand(e) == NULL) {
        return 0u;
    }
    const uint32_t in = since(e->now_ms, e->phase_ms);
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

static uint8_t wanted_value(const esc_stick_t *e)
{
    const esc_stick_change_t *c = &e->ch[e->active];
    return e->p->items[c->item].values[c->value].number;
}

static void selected(esc_stick_t *e)
{
    e->done[e->active] = true;
    if (all_done(e) || e->p->one_change_per_entry) {
        e->store_moved = false;
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
        return no(why, "no run");
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
    /* An arm starts from the throttle's rest, with the supply off. */
    e->out.arm = true;
    e->out.throttle_pct = ESC_STICK_PCT_MIN;
    e->out.supply_on = false;
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
        e->off_since_known = false;
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
    if ((e->phase == ESC_STICK_CYCLE || e->phase == ESC_STICK_OFF)
        && !e->out.supply_on && fresh) {
        off_reading(e, s);
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
            e->armed_seen = true;
            e->entry = next_entry(e);
            e->out.throttle_pct = esc_stick_pct(e->entry);
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

    switch (e->phase) {
    case ESC_STICK_SIGNAL:
        if (in_phase >= ESC_STICK_SIGNAL_MS) {
            before_power(e, 0u);
        }
        break;
    case ESC_STICK_HAND_OFF:
    case ESC_STICK_HAND_ON:
        if (e->hand_done) {
            if (e->phase == ESC_STICK_HAND_OFF) {
                before_power(e, (unsigned)e->hand + 1u);
            } else {
                before_menu(e, (unsigned)e->hand + 1u);
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
            esc_det_init(&e->det, &e->t, e->det.long_equals_short);
            esc_det_floor_only(&e->det);
            enter(e, ESC_STICK_ENTRY);
        } else if (in_phase >= ESC_STICK_POWER_WAIT_MS) {
            finish(e, ESC_STICK_ABORTED, ESC_STICK_R_NO_POWER);
        }
        break;
    case ESC_STICK_ENTRY:
        if (since(e->now_ms, e->on_ms) >= esc_stick_entry_ms(e)) {
            before_menu(e, 0u);
        }
        break;
    case ESC_STICK_ITEMS:
    case ESC_STICK_VALUES:
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
        if (e->p->store_throttle != ESC_THR_NONE && !e->store_moved) {
            /* The ESC has answered the selection: the move that stores
             * it, held as long again. */
            e->store_moved = true;
            e->out.throttle_pct = esc_stick_pct(e->p->store_throttle);
            enter(e, ESC_STICK_STORE);
            break;
        }
        /* Off first, the stick where it is; see the top of this file.  A
         * pulse under way is dropped with the group: no reading reaches the
         * detector until the next power-up starts it afresh. */
        e->out.supply_on = false;
        esc_det_drop_group(&e->det);
        e->off_seen = false;
        e->off_asked_ms = e->now_ms;
        e->off_seq = e->seq;
        e->off_since_known = false;
        e->cycle_moved = false;
        enter(e, all_done(e) ? ESC_STICK_OFF : ESC_STICK_CYCLE);
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
