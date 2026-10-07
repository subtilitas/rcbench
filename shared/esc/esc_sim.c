/*
 * A simulated ESC in its stick menu.  See esc_sim.h.
 *
 * The sound is a list of segments -- a beep, the gap after it, the pause
 * after a group, a wait -- each with the time it ends, walked forward to
 * the time asked for.  A throttle move is judged after the walk, against
 * the group that ended last.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_sim.h"

#include <string.h>

#include "esc_stick.h"

typedef enum {
    SEG_NONE = 0,   /* silent until something happens */
    SEG_BEEP,
    SEG_GAP,
    SEG_PAUSE,      /* after a group                  */
    SEG_WAIT,       /* the entry's silences           */
} seg_t;

void esc_sim_defaults(esc_sim_cfg_t *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->beep_ms   = 250u;
    c->gap_ms    = 250u;
    c->long_ms   = 800u;
    c->pause_ms  = 1500u;
    c->idle_ma   = 150u;
    c->beep_ma   = 600u;
    c->seed      = 1u;
    c->drop_group = -1;
}

void esc_sim_init(esc_sim_t *s, const esc_profile_t *p,
                  const esc_sim_cfg_t *c)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->p = p;
    if (c != NULL) {
        s->c = *c;
    } else {
        esc_sim_defaults(&s->c);
    }
    s->lcg = s->c.seed;
    s->pos = ESC_THR_NONE;
}

uint8_t esc_sim_stored(const esc_sim_t *s, uint8_t item)
{
    return (s != NULL) ? s->stored[item] : 0u;
}

static esc_throttle_t classify(float pct)
{
    if (pct < 0.0f) {
        return ESC_THR_NONE;            /* no signal */
    }
    if (pct < 15.0f) {
        return ESC_THR_MIN;
    }
    if (pct >= 35.0f && pct <= 65.0f) {
        return ESC_THR_MID;
    }
    if (pct > 85.0f) {
        return ESC_THR_MAX;
    }
    return ESC_THR_NONE;
}

/* Where the stick rests while the menu sounds: the profile's listen
 * position, else where this power-up entered. */
static esc_throttle_t listen_pos(const esc_sim_t *s)
{
    return (s->p->listen_throttle != ESC_THR_NONE) ? s->p->listen_throttle
                                                   : s->entry;
}

/* Whether a power-up at @p pos enters the menu: the profile's entry, or
 * the position any value is programmed from. */
static bool enters_from(const esc_profile_t *p, esc_throttle_t pos)
{
    if (pos == p->entry_throttle) {
        return true;
    }
    for (unsigned i = 0; i < p->item_count; ++i) {
        for (unsigned k = 0; k < p->items[i].value_count; ++k) {
            if (pos != ESC_THR_NONE
                && p->items[i].values[k].entry_throttle == pos) {
                return true;
            }
        }
    }
    return false;
}

/* The position the value numbered @p value of item @p item is programmed
 * from. */
static esc_throttle_t value_entry(const esc_profile_t *p, uint8_t item,
                                  uint8_t value)
{
    for (unsigned i = 0; i < p->item_count; ++i) {
        if (p->items[i].number != item) {
            continue;
        }
        for (unsigned k = 0; k < p->items[i].value_count; ++k) {
            const esc_value_t *v = &p->items[i].values[k];
            if (v->number == value && v->entry_throttle != ESC_THR_NONE) {
                return v->entry_throttle;
            }
        }
    }
    return p->entry_throttle;
}

static bool two_stage(const esc_profile_t *p)
{
    return p->value_select_throttle != ESC_THR_NONE;
}

static void add_sorted(esc_sim_t *s, uint8_t v)
{
    for (unsigned i = 0; i < s->loop_n; ++i) {
        if (s->loop[i] == v) {
            return;
        }
    }
    if (s->loop_n >= ESC_SIM_LOOP_MAX) {
        return;
    }
    unsigned at = s->loop_n;
    while (at > 0u && s->loop[at - 1u] > v) {
        s->loop[at] = s->loop[at - 1u];
        --at;
    }
    s->loop[at] = v;
    s->loop_n++;
}

static void add_extra(esc_sim_t *s)
{
    if (s->loop_n == 0u) {
        return;
    }
    const unsigned hi = s->loop[s->loop_n - 1u];
    for (unsigned k = 1; k <= s->c.extra && hi + k <= 255u; ++k) {
        add_sorted(s, (uint8_t)(hi + k));
    }
}

/* The loop a mode sounds: the items, one item's values, or every value. */
static void build_loop(esc_sim_t *s)
{
    s->loop_n = 0u;
    s->loop_i = 0u;
    s->rep = 0u;
    const esc_profile_t *p = s->p;
    if (s->mode == ESC_SIM_ITEMS) {
        for (unsigned i = 0; i < p->item_count; ++i) {
            add_sorted(s, p->items[i].number);
        }
        add_extra(s);
        return;
    }
    for (unsigned i = 0; i < p->item_count; ++i) {
        const esc_item_t *it = &p->items[i];
        if (two_stage(p) && it->number != s->item) {
            continue;
        }
        for (unsigned k = 0; k < it->value_count; ++k) {
            if (it->values[k].number != 0u) {
                add_sorted(s, it->values[k].number);
            }
        }
        if (two_stage(p)) {
            break;
        }
    }
    if (s->loop_n == 0u) {
        add_sorted(s, 1u);              /* an item the profile lacks */
    }
    if (!two_stage(p)) {
        add_extra(s);
    }
}

static uint32_t pulse_ms(const esc_sim_t *s)
{
    return s->pulse[s->pulse_i] ? s->c.long_ms : s->c.beep_ms;
}

static void start_beep(esc_sim_t *s, uint32_t at)
{
    s->seg = SEG_BEEP;
    s->seg_end = at + pulse_ms(s);
    /* The group counted when it began, so this is its number less one. */
    s->dropped = s->menu_group && s->c.drop_group >= 0
                 && s->menu_groups == (uint32_t)s->c.drop_group + 1u
                 && s->pulse_i + 1u == s->pulse_n;
}

/* A group of @p n beeps, longs first, or one long tone when @p tone. */
static void start_group(esc_sim_t *s, uint32_t at, uint8_t n, bool menu,
                        bool tone)
{
    s->pulse_n = 0u;
    s->pulse_i = 0u;
    if (tone) {
        s->pulse[s->pulse_n++] = 1u;
    } else {
        const unsigned les = (s->p->encoding == ESC_ENC_SHORT_LONG)
                                 ? s->p->long_equals_short : 0u;
        unsigned longs = (les > 0u) ? n / les : 0u;
        unsigned shorts = (les > 0u) ? n % les : n;
        while (longs > 0u && s->pulse_n < ESC_SIM_PULSE_MAX) {
            s->pulse[s->pulse_n++] = 1u;
            --longs;
        }
        while (shorts > 0u && s->pulse_n < ESC_SIM_PULSE_MAX) {
            s->pulse[s->pulse_n++] = 0u;
            --shorts;
        }
    }
    s->menu_group = menu;
    s->sounding = n;
    if (menu) {
        s->menu_groups++;
    }
    if (s->pulse_n == 0u) {
        s->seg = SEG_PAUSE;
        s->seg_end = at + s->c.pause_ms;
        return;
    }
    start_beep(s, at);
}

static void sound_next(esc_sim_t *s, uint32_t at)
{
    if (s->loop_n == 0u) {
        s->seg = SEG_NONE;
        return;
    }
    const uint8_t n = s->loop[s->loop_i];
    const unsigned reps = (s->p->repeat > 0) ? (unsigned)s->p->repeat : 1u;
    s->rep++;
    if (s->rep >= reps) {
        s->rep = 0u;
        s->loop_i = (uint8_t)((s->loop_i + 1u) % s->loop_n);
    }
    start_group(s, at, n, true, false);
}

static void begin_menu(esc_sim_t *s, uint32_t at)
{
    s->mode = two_stage(s->p) ? ESC_SIM_ITEMS : ESC_SIM_VALUES;
    build_loop(s);
    s->ended = 0u;
    sound_next(s, at);
}

/* A segment ended at @p at: what follows it. */
static void segment_over(esc_sim_t *s, uint32_t at)
{
    switch (s->seg) {
    case SEG_BEEP:
        if (s->pulse_i + 1u < s->pulse_n) {
            s->seg = SEG_GAP;
            s->seg_end = at + s->c.gap_ms;
            return;
        }
        if (s->menu_group) {
            s->ended = s->sounding;
            s->ended_ms = at;
        }
        if (s->mode == ESC_SIM_ENTRY) {
            s->tones_done = true;
            s->seg = SEG_WAIT;
            const uint32_t menu = s->on_ms + s->c.entry_ms;
            s->seg_end = ((int32_t)(menu - at) > 0) ? menu : at;
            if (s->answering) {
                /* The answer to the action, then a pause, then the menu. */
                s->answering = false;
                s->seg_end = at + s->c.pause_ms;
            }
            return;
        }
        s->seg = SEG_PAUSE;
        s->seg_end = at + s->c.pause_ms;
        return;
    case SEG_GAP:
        s->pulse_i++;
        start_beep(s, at);
        return;
    case SEG_PAUSE:
        if (s->mode == ESC_SIM_DONE || s->mode == ESC_SIM_PENDING
            || s->mode == ESC_SIM_IDLE) {
            s->seg = SEG_NONE;
            return;
        }
        sound_next(s, at);
        return;
    case SEG_WAIT:
        if (s->mode != ESC_SIM_ENTRY) {
            s->seg = SEG_NONE;
            return;
        }
        if (!s->tones_done) {
            start_group(s, at, 2u, false, false);   /* "entered" */
            return;
        }
        if (s->c.wait_hand && !s->hand_done
            && esc_profile_manual_count(s->p, ESC_MANUAL_BEFORE_MENU) > 0u) {
            /* The jumper still on, the button not pressed: the ESC waits,
             * silent, for esc_sim_hand(). */
            s->seg = SEG_NONE;
            return;
        }
        if (listen_pos(s) != s->entry) {
            s->mode = ESC_SIM_WAIT_LISTEN;
            s->seg = SEG_NONE;
            return;
        }
        begin_menu(s, at);
        return;
    default:
        return;
    }
}

/* Whether a move now answers the group that ended last: inside the window
 * the configuration or the profile gives, or with neither, before the next
 * group begins. */
static bool in_window(const esc_sim_t *s, uint32_t now, uint32_t profile_ms)
{
    if (s->ended == 0u || s->c.deaf) {
        return false;
    }
    const uint32_t w = (s->c.window_ms != 0u) ? s->c.window_ms : profile_ms;
    if (w == 0u) {
        return s->seg == SEG_PAUSE && s->menu_group;
    }
    return (uint32_t)(now - s->ended_ms) <= w;
}

/* The moves that store @p value of @p item once it is selected: the
 * profile's store move, then the value's own after_select.  How many. */
static unsigned store_moves(const esc_profile_t *p, uint8_t item,
                            uint8_t value, esc_throttle_t *out)
{
    unsigned n = 0u;
    if (p->store_throttle != ESC_THR_NONE) {
        out[n++] = p->store_throttle;
    }
    for (unsigned i = 0; i < p->item_count; ++i) {
        if (p->items[i].number != item) {
            continue;
        }
        for (unsigned k = 0; k < p->items[i].value_count; ++k) {
            const esc_value_t *v = &p->items[i].values[k];
            for (unsigned m = 0; v->number == value && m < v->after_count
                                 && m < ESC_AFTER_MAX; ++m) {
                out[n++] = v->after[m];
            }
        }
    }
    return n;
}

static void store(esc_sim_t *s, uint32_t now, uint8_t item, uint8_t value)
{
    esc_throttle_t moves[ESC_AFTER_MAX + 1u];
    if (s->mode != ESC_SIM_PENDING
        && store_moves(s->p, item, value, moves) > 0u) {
        /* Answered, and stored only by the moves that follow, in order;
         * the power going first loses it. */
        s->pend_item = item;
        s->pend_value = value;
        s->pend_step = 0u;
        s->ended = 0u;
        s->mode = ESC_SIM_PENDING;
        start_group(s, now, 2u, false, false);
        return;
    }
    s->stored[item] = value;
    s->stored_from[item] = s->entry;
    s->stores++;
    /* A value stored from another position than its own teaches that
     * position: a car mode entered at the brake stores the brake as its
     * neutral. */
    if (value_entry(s->p, item, value) != s->entry) {
        s->misplaced++;
    }
    s->ended = 0u;
    if (s->p->one_change_per_entry) {
        s->mode = ESC_SIM_DONE;
    } else {
        s->mode = ESC_SIM_ITEMS;
        build_loop(s);
    }
    start_group(s, now, 1u, false, true);   /* the stored tone */
}

static void moved(esc_sim_t *s, uint32_t now)
{
    const esc_profile_t *p = s->p;
    switch (s->mode) {
    case ESC_SIM_ENTRY:
        if (s->pos != s->entry) {
            s->mode = ESC_SIM_IDLE;     /* left the entry: runs normally */
            s->seg = SEG_NONE;
        }
        return;
    case ESC_SIM_WAIT_LISTEN:
        if (s->pos == listen_pos(s)) {
            s->mode = two_stage(p) ? ESC_SIM_ITEMS : ESC_SIM_VALUES;
            build_loop(s);
            s->seg = SEG_PAUSE;
            s->seg_end = now + s->c.pause_ms;
        }
        return;
    case ESC_SIM_PENDING: {
        esc_throttle_t moves[ESC_AFTER_MAX + 1u];
        const unsigned n = store_moves(p, s->pend_item, s->pend_value, moves);
        if (s->pend_step < n && s->pos == moves[s->pend_step]) {
            s->pend_step++;
            if (s->pend_step == n) {
                store(s, now, s->pend_item, s->pend_value);
            } else {
                start_group(s, now, 2u, false, false);  /* answered */
            }
        }
        return;
    }
    case ESC_SIM_ITEMS:
        if (s->pos == p->select_throttle
            && in_window(s, now, p->select_within_ms)) {
            /* An action acts on the select move and sounds no values. */
            const esc_item_t *it = NULL;
            for (unsigned i = 0; i < p->item_count && it == NULL; ++i) {
                if (p->items[i].number == s->ended) {
                    it = &p->items[i];
                }
            }
            if (it != NULL && esc_stick_is_action(it)
                && strcmp(it->key, "exit") == 0) {
                s->mode = ESC_SIM_IDLE;     /* out of the menu */
                s->seg = SEG_NONE;
                return;
            }
            if (esc_stick_is_action(it)) {
                memset(s->stored, 0, sizeof(s->stored));
                s->resets++;
                s->ended = 0u;
                if (p->one_change_per_entry) {
                    s->mode = ESC_SIM_DONE;
                } else {
                    build_loop(s);
                }
                start_group(s, now, 1u, false, true);
                return;
            }
            s->item = s->ended;
            s->ended = 0u;
            s->mode = ESC_SIM_VALUES;
            build_loop(s);
            s->seg = SEG_PAUSE;
            s->seg_end = now + s->c.pause_ms;
        }
        return;
    case ESC_SIM_VALUES:
        if (two_stage(p)) {
            if (s->pos == p->value_select_throttle
                && in_window(s, now, p->value_select_within_ms)) {
                store(s, now, s->item, s->ended);
            }
            return;
        }
        if (s->pos == p->select_throttle
            && in_window(s, now, p->select_within_ms)) {
            uint8_t item = 0u;
            for (unsigned i = 0; i < p->item_count && item == 0u; ++i) {
                for (unsigned k = 0; k < p->items[i].value_count; ++k) {
                    if (p->items[i].values[k].number == s->ended) {
                        item = p->items[i].number;
                        break;
                    }
                }
            }
            store(s, now, item, s->ended);
        }
        return;
    default:
        return;
    }
}

int32_t esc_sim_step(esc_sim_t *s, uint32_t now_ms, bool powered,
                     float throttle_pct)
{
    if (s == NULL || s->p == NULL) {
        return 0;
    }
    const esc_throttle_t pos = classify(throttle_pct);
    if (!powered) {
        s->powered = false;
        s->mode = ESC_SIM_OFF;
        s->seg = SEG_NONE;
        s->pos = pos;
        return 0;
    }
    if (!s->powered) {
        /* Power on: the stick's position now decides what the ESC does. */
        s->powered = true;
        s->on_ms = now_ms;
        s->pos = pos;
        s->ended = 0u;
        s->tones_done = false;
        s->hand_done = false;
        s->menu_groups = 0u;
        if (s->c.entry_ms == 0u) {
            s->c.entry_ms = (s->p->entry_hold_ms != 0u) ? s->p->entry_hold_ms
                                                       : 3000u;
        }
        s->entry = pos;
        if (enters_from(s->p, pos)) {
            s->mode = ESC_SIM_ENTRY;
            s->seg = SEG_WAIT;
            const uint32_t quarter = s->c.entry_ms / 4u;
            s->seg_end = now_ms + ((quarter < 1000u) ? quarter : 1000u);
        } else {
            s->mode = ESC_SIM_IDLE;
            s->seg = SEG_NONE;
        }
    }
    /* Every segment that ended by now, in order. */
    for (unsigned guard = 0; s->seg != SEG_NONE && guard < 1000u; ++guard) {
        if ((int32_t)(now_ms - s->seg_end) < 0) {
            break;
        }
        segment_over(s, s->seg_end);
    }
    if (pos != s->pos) {
        s->pos = pos;
        if (pos != ESC_THR_NONE) {
            moved(s, now_ms);
        }
    }
    int32_t ma = (int32_t)s->c.idle_ma;
    if (s->seg == SEG_BEEP && !s->dropped && !s->c.mute) {
        ma += (int32_t)s->c.beep_ma;
    }
    if (s->c.noise_ma > 0u) {
        s->lcg = s->lcg * 1664525u + 1013904223u;
        const uint32_t span = 2u * s->c.noise_ma + 1u;
        ma += (int32_t)((s->lcg >> 16) % span) - (int32_t)s->c.noise_ma;
    }
    return (ma > 0) ? ma : 0;
}

void esc_sim_hand(esc_sim_t *s, uint32_t now_ms)
{
    if (s == NULL || !s->powered || s->hand_done) {
        return;
    }
    s->hand_done = true;
    if (s->mode == ESC_SIM_ENTRY && s->tones_done && s->seg == SEG_NONE) {
        /* Waiting: the three-tone answer at once, then the menu. */
        s->answering = true;
        start_group(s, now_ms, 3u, false, false);
    }
}
