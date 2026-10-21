/**
 * @file
 * @brief Stick programming: what can run.
 *
 * Which profiles the engine runs, the voltage and current set points a model is
 * given, and the check a run's changes and timing pass before it starts.  See
 * esc_stick.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick.h"
#include "esc_stick_int.h"

#include <string.h>

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

/**
 * @brief Whether any value number appears in two items.
 *
 * @param p  The profile.
 * @return true when a value number repeats across items.
 */
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

/**
 * @brief Whether every manual step can be waited for.
 *
 * A step during the menu has no moment the run can know.  A hand at a powered
 * ESC -- held while the supply comes on, or after the entry -- is asked for
 * with the stick where the entry put it, so only for an entry at MIN, the
 * motor-off position: a person is not asked to reach for a powered ESC whose
 * stick is at MID or MAX.  A step before the supply goes off asks for no hand
 * at all -- the operator watches the ESC -- so any stick position holds it.
 *
 * @param p  The profile.
 * @return true when every manual step can be waited for.
 */
static bool hand_fits(const esc_profile_t *p)
{
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        switch (p->manual[i].when) {
        case ESC_MANUAL_BEFORE_POWER:
        case ESC_MANUAL_BEFORE_POWER_OFF:
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

/**
 * @brief Whether a value states its own entry time: the profile-wide rest rule
 *        leaves such a profile to the per-change one (entry_refused()), which
 *        sees the value's time.
 *
 * @param p  The profile.
 * @return true when a value has an entry_hold_ms.
 */
static bool value_holds(const esc_profile_t *p)
{
    for (unsigned i = 0; i < p->item_count; ++i) {
        for (unsigned k = 0; k < p->items[i].value_count; ++k) {
            if (p->items[i].values[k].entry_hold_ms != 0u) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief The step that starts the menu, or NULL.
 *
 * @param p  The profile.
 * @return The step marked starts_menu, or NULL when none is.
 */
static const esc_manual_t *menu_start(const esc_profile_t *p)
{
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        if (p->manual[i].starts_menu) {
            return &p->manual[i];
        }
    }
    return NULL;
}

/**
 * @brief Set the refusal reason and return no menu kind.
 *
 * @param why   Receives @p text when not NULL.
 * @param text  The static reason string.
 * @return ESC_STICK_KIND_NONE.
 */
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
    /* The run listens from the moment it asks for the action that starts
     * the menu, with the stick where the power-up left it: a menu that
     * rests elsewhere would need a move under the operator's hand at a
     * powered ESC, so such a profile is not run.  The parsers refuse the
     * file; this holds a profile built otherwise. */
    if (menu_start(p) != NULL && p->listen_throttle != ESC_THR_NONE
        && p->listen_throttle != p->entry_throttle) {
        return refuse(why, "menu start, rest elsewhere");
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
    if (rest != p->entry_throttle && p->entry_hold_ms == 0u
        && esc_stick_change_entry_ms(p, NULL, NULL) == 0u
        && !value_holds(p)) {
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

uint32_t esc_stick_model_mv(const esc_profile_t *p, int model)
{
    if (p == NULL || model < 0 || (unsigned)model >= p->model_count
        || p->models[model].cells_min == 0u) {
        return esc_stick_profile_mv(p);
    }
    const esc_model_t *m = &p->models[model];
    return (uint32_t)m->cells_min * (m->nimh ? 1200u : 3800u);
}

uint32_t esc_stick_model_v_max(const esc_profile_t *p, int model)
{
    if (p == NULL || p->model_count == 0u || p->models == NULL) {
        return 0u;
    }
    if (model >= 0) {
        return ((unsigned)model < p->model_count) ? p->models[model].v_max_mv
                                                  : 0u;
    }
    uint32_t low = 0u;
    for (unsigned i = 0; i < p->model_count; ++i) {
        const uint32_t v = p->models[i].v_max_mv;
        if (v == 0u) {
            return 0u;          /* one model the data cannot vouch for */
        }
        low = (low == 0u || v < low) ? v : low;
    }
    return low;
}

uint32_t esc_stick_model_ma_max(const esc_profile_t *p, int model)
{
    if (p == NULL || p->model_count == 0u || p->models == NULL) {
        return 0u;
    }
    if (model >= 0) {
        return ((unsigned)model < p->model_count)
                   ? (uint32_t)p->models[model].current_a * 1000u : 0u;
    }
    uint32_t low = 0u;
    for (unsigned i = 0; i < p->model_count; ++i) {
        const uint32_t a = (uint32_t)p->models[i].current_a * 1000u;
        if (a != 0u && (low == 0u || a < low)) {
            low = a;
        }
    }
    return low;
}

uint32_t esc_stick_model_v_min(const esc_profile_t *p, int model)
{
    if (p == NULL || p->model_count == 0u || p->models == NULL) {
        return 0u;
    }
    if (model >= 0) {
        return ((unsigned)model < p->model_count) ? p->models[model].v_min_mv
                                                  : 0u;
    }
    uint32_t high = 0u;
    for (unsigned i = 0; i < p->model_count; ++i) {
        const uint32_t v = p->models[i].v_min_mv;
        high = (v > high) ? v : high;
    }
    return high;
}

esc_stick_rating_t esc_stick_rating(const esc_profile_t *p, int model,
                                    uint32_t mv, uint32_t ma, bool by_hand)
{
    (void)by_hand;
    const uint32_t v_max = esc_stick_model_v_max(p, model);
    if (v_max != 0u && mv > v_max) {
        return ESC_STICK_RATING_V_OVER;
    }
    const uint32_t v_min = esc_stick_model_v_min(p, model);
    if (v_min != 0u && mv < v_min) {
        return ESC_STICK_RATING_V_UNDER;
    }
    const uint32_t ma_max = esc_stick_model_ma_max(p, model);
    if (ma_max != 0u && ma > ma_max) {
        return ESC_STICK_RATING_I_OVER;
    }
    return ESC_STICK_RATING_OK;
}

bool esc_stick_rating_unknown(const esc_profile_t *p, int model, uint32_t mv,
                              bool by_hand)
{
    if (esc_stick_model_v_max(p, model) != 0u) {
        return false;
    }
    /* The cell count the data states for this model, or the family's for
     * -1: not one borrowed from another model. */
    const bool own = !by_hand && mv != 0u && p != NULL
                     && (model < 0
                         || ((unsigned)model < p->model_count
                             && p->models[model].cells_min != 0u));
    return !own;
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

/**
 * @brief Whether the profile asks for a hand at a powered ESC.
 *
 * @param p  The profile.
 * @return true when an at_power_up or before_menu step is due.
 */
static bool hand_powered(const esc_profile_t *p)
{
    return esc_profile_manual_count(p, ESC_MANUAL_AT_POWER_UP) > 0u
           || esc_profile_manual_count(p, ESC_MANUAL_BEFORE_MENU) > 0u;
}

/**
 * @brief Why change @p i cannot be powered up from its own entry position, or
 *        NULL.
 *
 * The run powers each change up from the position its value is programmed from
 * (esc_stick_change_entry()): a Kontronik car mode from the middle, the neutral
 * the mode teaches.  The profile-wide rules of esc_stick_kind() hold for that
 * position as they hold for the profile's:
 *
 *   - A power-up takes one position.  A two-stage menu, or one that takes
 *     several changes a power-up, makes its changes in the order the ESC
 *     sounds them, not the order asked, so every change of the run shares
 *     the first one's position; a mix is refused, not reordered.  A
 *     one-stage menu takes one change a power-up and powers each up from
 *     its own.
 *   - The rest is that position where the profile names none, and the
 *     select move has to differ from it.
 *   - A power-up's entry time is what the run waits
 *     (esc_stick_change_entry_ms()): its value's entry_hold_ms where the
 *     manual gives one, else the timing's entry, and no less than the
 *     longest at_power_up hold; the changes a power-up shares share one.
 *     A move to a named rest needs a time the profile, the value or an
 *     at_power_up step's hold states.
 *   - A hand at a powered ESC with the stick at MAX is not asked for.  MID
 *     is, where the value names it: the manual's motor-off in the middle.
 *
 * @param p  The profile.
 * @param ch The changes of the run.
 * @param i  Index of the change checked.
 * @param t  The timing.
 * @return A static reason string, or NULL when the change can be powered up.
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
    /* After the value move a two-stage menu goes back to its items, and
     * which change comes last is the ESC's order: no moves after it. */
    const esc_value_t *val = &p->items[ch[i].item].values[ch[i].value];
    if (p->value_select_throttle != ESC_THR_NONE && val->after_count > 0u) {
        return "moves after the value, two stages";
    }
    /* Each of the value's moves a move, from where the store or the select
     * move left the stick: one that stays put would count as made.  The
     * parsers refuse such a file; this holds a profile built otherwise. */
    esc_throttle_t at = (p->store_throttle != ESC_THR_NONE)
                            ? p->store_throttle : p->select_throttle;
    for (unsigned k = 0; k < val->after_count && k < ESC_AFTER_MAX; ++k) {
        if (val->after[k] == at) {
            return "a move after the value makes no move";
        }
        at = val->after[k];
    }
    if (wait <= ESC_STICK_SETTLE_MS) {
        return "ENTRY above 500 ms";
    }
    const esc_throttle_t rest = (p->listen_throttle != ESC_THR_NONE)
                                    ? p->listen_throttle : from;
    /* A time the profile states: its entry's, or the one the run waits
     * for this change without the timing's default -- the value's own,
     * and an at_power_up step's hold. */
    const bool timed = p->entry_hold_ms != 0u
                       || esc_stick_change_entry_ms(p, &ch[i], NULL) != 0u;
    if (rest != from && !timed) {
        return "rest move, no entry time";
    }
    if (p->select_throttle == rest) {
        return "select move is the rest";
    }
    if (from == ESC_THR_MAX && from != p->entry_throttle && hand_powered(p)) {
        return "manual step";
    }
    if (menu_start(p) != NULL && rest != from) {
        return "menu start, rest elsewhere";
    }
    return NULL;
}

bool esc_stick_no(const char **why, const char *text)
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
        return esc_stick_no(why, "no timing");
    }
    if (ch == NULL || n == 0u) {
        return esc_stick_no(why, "nothing to change");
    }
    if (n > ESC_STICK_MAX_CHANGES) {
        return esc_stick_no(why, "too many changes");
    }
    for (size_t i = 0; i < n; ++i) {
        if (ch[i].item >= p->item_count) {
            return esc_stick_no(why, "no such item");
        }
        const esc_item_t *it = &p->items[ch[i].item];
        const char *not_offered = esc_stick_not_offered(it);
        if (not_offered != NULL) {
            return esc_stick_no(why, not_offered);
        }
        if (ch[i].value >= it->value_count) {
            return esc_stick_no(why, "no such value");
        }
        /* N beeps say N: a value numbered 0 is never sounded. */
        if (it->values[ch[i].value].number == 0u) {
            return esc_stick_no(why, "value 0 is not sounded");
        }
        const char *bad = entry_refused(p, ch, i, t);
        if (bad != NULL) {
            return esc_stick_no(why, bad);
        }
        for (size_t k = 0; k < i; ++k) {
            if (p->items[ch[k].item].number == it->number) {
                return esc_stick_no(why, "one change per item");
            }
        }
    }
    if (t->beep_min_ms == 0u || t->gap_min_ms == 0u) {
        return esc_stick_no(why, "BEEP MIN and GAP MIN above 0");
    }
    if (t->long_ms <= t->beep_min_ms) {
        return esc_stick_no(why, "LONG above BEEP MIN");
    }
    if (t->long_max_ms <= t->long_ms) {
        return esc_stick_no(why, "LONG MAX above LONG");
    }
    if (t->group_gap_ms <= t->gap_min_ms) {
        return esc_stick_no(why, "GROUP GAP above GAP MIN");
    }
    if (t->threshold_ma <= t->hysteresis_ma) {
        return esc_stick_no(why, "THRESHOLD above HYSTERESIS");
    }
    if (t->entry_ms <= ESC_STICK_SETTLE_MS) {
        return esc_stick_no(why, "ENTRY above 500 ms");
    }
    /*
     * The move is made GROUP GAP after the first quiet reading, which can
     * come a reading interval after the beep: both have to fit the window
     * the manual gives.
     */
    const uint32_t late = t->group_gap_ms + esc_stick_read_max_ms(t);
    if (p->select_within_ms != 0u && late >= p->select_within_ms) {
        return esc_stick_no(why, "GROUP GAP misses the select window");
    }
    if (p->value_select_within_ms != 0u && late >= p->value_select_within_ms) {
        return esc_stick_no(why, "GROUP GAP misses the value window");
    }
    return true;
}
