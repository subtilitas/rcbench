/*
 * The panel's half of the SENSE_MAP page.  See sense_map_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_map_link.h"

#include <stddef.h>
#include <string.h>

/* The ID and FLAGS registers a status read takes: 26 to 30. */
#define STATUS_AT ((uint8_t)LINK_SM_ID_0)
#define STATUS_N  ((uint8_t)(LINK_SM_FLAGS - LINK_SM_ID_0 + 1))

/* The bit of each event, by its index in ev_parts. */
static const uint8_t k_ev[4] = {
    SENSE_MAP_LINK_EV_REFUSED, SENSE_MAP_LINK_EV_DARK,
    SENSE_MAP_LINK_EV_SILENT,  SENSE_MAP_LINK_EV_WRONG,
};

void sense_map_link_init(sense_map_link_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
    }
}

/* What was read and said under the map in force: news again under the
 * next. */
static void forget_reads(sense_map_link_t *s, uint32_t now_ms)
{
    s->setup_ms    = now_ms;
    s->have_status = false;
    s->asked       = false;
    s->flags       = 0u;
    s->was_flags   = 0u;
    memset(s->silent_told, 0, sizeof(s->silent_told));
    memset(s->online_seen, 0, sizeof(s->online_seen));
    s->events &= (uint8_t)~(SENSE_MAP_LINK_EV_SILENT | SENSE_MAP_LINK_EV_WRONG);
    s->ev_parts[2] = 0u;                      /* SILENT's and WRONG's parts */
    s->ev_parts[3] = 0u;
}

void sense_map_link_lost(sense_map_link_t *s)
{
    if (s == NULL) {
        return;
    }
    s->up      = false;
    s->page    = false;
    s->known   = false;
    s->forced  = false;
    s->refused = false;
    s->reread  = false;
    s->pending = SENSE_MAP_LINK_OP_NONE;
    s->told    = false;
    forget_reads(s, 0u);
    /* What the IOmcu that went said is about a page nobody reads now; a
     * dark part is the settings' and stands. */
    s->events &= (uint8_t)SENSE_MAP_LINK_EV_DARK;
}

void sense_map_link_came_up(sense_map_link_t *s, uint16_t minor,
                            uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    sense_map_link_lost(s);
    s->up   = true;
    s->page = minor >= LINK_MINOR_SENSE_MAP;
    s->setup_ms = now_ms;
}

static void raise(sense_map_link_t *s, uint8_t ev, uint8_t parts)
{
    for (unsigned i = 0u; i < 4u; ++i) {
        if (k_ev[i] == ev) {
            s->ev_parts[i] |= parts;
        }
    }
    s->events |= ev;
}

void sense_map_link_want(sense_map_link_t *s, const sense_map_want_t *w,
                         uint32_t now_ms)
{
    if (s == NULL || w == NULL) {
        return;
    }
    uint16_t next[LINK_SM_KEPT];
    memcpy(next, w->regs, sizeof(next));
    next[LINK_SM_ENABLE] &= (uint16_t)LINK_SM_FIT_ALL;
    const uint8_t fitted = (uint8_t)next[LINK_SM_ENABLE];
    if (w->nobus) {
        /* No bus: SENSE clears its pins, so no part may stay enabled on
         * the ones the page held. */
        next[LINK_SM_ENABLE] = 0u;
        next[LINK_SM_FAST]   = (uint16_t)LINK_SM_FAST_NONE;
    }
    /* A part on the INA228's address while it is on: left out, as SENSE
     * leaves both off, with FAST if it was there. */
    uint8_t clash = 0u;
    if (w->i228_addr >= LINK_SN_I3221_ADDR_MIN
        && w->i228_addr <= LINK_SN_I3221_ADDR_MAX) {
        const uint16_t bit =
            (uint16_t)(1u << (w->i228_addr - LINK_SN_I3221_ADDR_MIN));
        if ((next[LINK_SM_ENABLE] & bit) != 0u) {
            clash = (uint8_t)bit;
            next[LINK_SM_ENABLE] &= (uint16_t)~bit;
            if (next[LINK_SM_FAST] != LINK_SM_FAST_NONE
                && (1u << (next[LINK_SM_FAST] / 3u)) == bit) {
                next[LINK_SM_FAST] = (uint16_t)LINK_SM_FAST_NONE;
            }
        }
    }
    const uint8_t dark = (uint8_t)(w->dark & next[LINK_SM_ENABLE]);
    const bool moved = !s->want_set || memcmp(next, s->want, sizeof(next)) != 0
                       || dark != s->dark || clash != s->clash
                       || fitted != s->fitted || w->nobus != s->nobus;
    if (!moved) {
        return;
    }
    /* The first map is the one the panel starts with, not an edit: due at
     * once. */
    s->want_ms = s->want_set ? now_ms
                             : (uint32_t)(now_ms - SENSE_MAP_LINK_SETTLE_MS);
    const uint8_t was_dark = s->dark;
    s->want_set = true;
    memcpy(s->want, next, sizeof(next));
    s->clash   = clash;
    s->dark    = dark;
    s->fitted  = fitted;
    s->nobus   = w->nobus;
    /* A new map is a new question: what was refused is asked again. */
    s->refused = false;
    s->events &= (uint8_t)~(SENSE_MAP_LINK_EV_REFUSED | SENSE_MAP_LINK_EV_DARK);
    s->ev_parts[0] = 0u;
    s->ev_parts[1] = 0u;
    if (dark != 0u && dark != was_dark) {
        raise(s, SENSE_MAP_LINK_EV_DARK, dark);
    }
}

static bool fitted_any(const sense_map_link_t *s)
{
    return ((s->want_set ? s->want[LINK_SM_ENABLE] : 0u)
            | s->held[LINK_SM_ENABLE]) != 0u;
}

bool sense_map_link_next(sense_map_link_t *s, uint32_t now_ms, bool idle,
                         sense_map_link_op_t *op)
{
    if (s == NULL || op == NULL) {
        return false;
    }
    memset(op, 0, sizeof(*op));
    s->pending = SENSE_MAP_LINK_OP_NONE;
    if (!s->up || !s->page) {
        return false;
    }
    op->page = (uint8_t)LINK_PAGE_SENSE_MAP;
    if (!s->known) {
        op->kind = SENSE_MAP_LINK_OP_READ;
        op->off  = 0u;
        op->n    = (uint8_t)LINK_SM_COUNT;
        s->pending = op->kind;
        return true;
    }
    const bool owed = s->want_set && s->dark == 0u && !s->refused
                      && memcmp(s->want, s->held, sizeof(s->want)) != 0;
    if (owed && idle
        && (uint32_t)(now_ms - s->want_ms) >= SENSE_MAP_LINK_SETTLE_MS) {
        op->kind  = SENSE_MAP_LINK_OP_WRITE;
        op->write = true;
        op->off   = 0u;
        op->n     = (uint8_t)LINK_SM_KEPT;
        op->regs  = s->want;
        s->pending = op->kind;
        return true;
    }
    if (fitted_any(s)
        && (!s->asked
            || (uint32_t)(now_ms - s->asked_ms) >= SENSE_MAP_LINK_READ_MS)) {
        op->kind = SENSE_MAP_LINK_OP_STATUS;
        op->off  = STATUS_AT;
        op->n    = STATUS_N;
        s->asked    = true;
        s->asked_ms = now_ms;
        s->pending  = op->kind;
        return true;
    }
    return false;
}

/* The part SENSE reports on, by the map in force: FAST's, else the lowest
 * fitted; 4 for none. */
static unsigned held_mirror(const sense_map_link_t *s)
{
    const uint16_t fast = s->held[LINK_SM_FAST];
    if (fast < LINK_SM_CHANNELS) {
        return fast / 3u;
    }
    for (unsigned a = 0u; a < LINK_SM_PARTS; ++a) {
        if ((s->held[LINK_SM_ENABLE] & (1u << a)) != 0u) {
            return a;
        }
    }
    return LINK_SM_PARTS;
}

uint16_t sense_map_link_mirror_dmohm(const sense_map_link_t *s, unsigned ch)
{
    if (!sense_map_link_page(s) || !s->known || ch < 1u || ch > 3u) {
        return 0u;
    }
    const unsigned a = held_mirror(s);
    return (a < LINK_SM_PARTS) ? s->held[LINK_SM_SHUNT(3u * a + ch - 1u)]
                               : 0u;
}

bool sense_map_link_waits_for_pins(const sense_map_link_t *s,
                                   const sense_link_t *sl)
{
    /* SENSE not read yet: its pins are not known to be the ones asked. */
    return s != NULL && sl != NULL && s->want_set
           && s->want[LINK_SM_ENABLE] != 0u
           && (!sl->known
               || (sense_link_unheld(sl) & SENSE_LINK_ROW_PINS) != 0u);
}

/* What a read of IDs and FLAGS says that the operator is told. */
static void judge(sense_map_link_t *s, uint32_t now_ms)
{
    const bool waited = (uint32_t)(now_ms - s->setup_ms) >= SENSE_LINK_GRACE_MS;
    const unsigned mirror = held_mirror(s);
    for (unsigned a = 0u; a < LINK_SM_PARTS; ++a) {
        if ((s->held[LINK_SM_ENABLE] & (1u << a)) == 0u || a == mirror) {
            s->silent_told[a] = false;
            s->online_seen[a] = false;
            continue;
        }
        const uint8_t bit = (uint8_t)(1u << a);
        if ((s->flags & LINK_SM_ONLINE(a)) != 0u) {
            s->silent_told[a] = false;
            s->online_seen[a] = true;
            s->ev_parts[2] &= (uint8_t)~bit;
            s->ev_parts[3] &= (uint8_t)~bit;
            continue;
        }
        if ((s->flags & LINK_SM_ID_WRONG(a)) != 0u) {
            if ((s->was_flags & LINK_SM_ID_WRONG(a)) == 0u) {
                s->wrong_id[a] = s->ids[a];    /* as read, kept if put back */
                raise(s, SENSE_MAP_LINK_EV_WRONG, bit);
            }
            continue;
        }
        if (!s->silent_told[a] && (s->online_seen[a] || waited)) {
            raise(s, SENSE_MAP_LINK_EV_SILENT, bit);
            s->silent_told[a] = true;
            s->online_seen[a] = false;
        }
    }
    s->was_flags = s->flags;
}

static void take_status(sense_map_link_t *s, const uint16_t *regs)
{
    for (unsigned a = 0u; a < LINK_SM_PARTS; ++a) {
        s->ids[a] = regs[a];
    }
    s->flags = regs[LINK_SM_FLAGS - LINK_SM_ID_0];
    s->forced = (s->flags & LINK_SM_IN_FORCE) != 0u;
    s->have_status = true;
}

void sense_map_link_done(sense_map_link_t *s, int result,
                         const uint16_t *regs, uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    const sense_map_link_op_kind_t op = s->pending;
    s->pending = SENSE_MAP_LINK_OP_NONE;
    if (result == SENSE_MAP_LINK_NO_ANSWER || op == SENSE_MAP_LINK_OP_NONE) {
        return;                         /* owed still: asked again */
    }
    if (op == SENSE_MAP_LINK_OP_WRITE) {
        if (result == SENSE_MAP_LINK_ACK) {
            memcpy(s->held, s->want, sizeof(s->held));
            s->reread = true;
            s->forced = true;
            forget_reads(s, now_ms);
        } else {
            s->refused = true;
            raise(s, SENSE_MAP_LINK_EV_REFUSED, 0u);
        }
        return;
    }
    if (result != SENSE_MAP_LINK_ACK || regs == NULL) {
        /* A read refused: an IOmcu that names 4.25 and has not the page.
         * Nothing more goes to it until the link comes up again. */
        s->page = false;
        return;
    }
    if (op == SENSE_MAP_LINK_OP_READ) {
        memcpy(s->held, regs, sizeof(s->held));
        s->known = true;
        forget_reads(s, now_ms);
        take_status(s, &regs[LINK_SM_ID_0]);
    } else {
        take_status(s, regs);
    }
    judge(s, now_ms);
}

bool sense_map_link_page(const sense_map_link_t *s)
{
    return s != NULL && s->up && s->page;
}

bool sense_map_link_held(const sense_map_link_t *s)
{
    return sense_map_link_page(s) && s->known && s->want_set && !s->refused
           && memcmp(s->want, s->held, sizeof(s->want)) == 0;
}

bool sense_map_link_in_force(const sense_map_link_t *s)
{
    return sense_map_link_page(s) && s->known && s->forced;
}

bool sense_map_link_take_reread(sense_map_link_t *s)
{
    if (s == NULL || !s->reread) {
        return false;
    }
    s->reread = false;
    return true;
}

uint8_t sense_map_link_event(sense_map_link_t *s, uint32_t now_ms,
                             uint8_t *part, uint16_t *id)
{
    if (s == NULL || (s->told && (uint32_t)(now_ms - s->told_ms)
                                     < SENSE_MAP_LINK_EVENT_GAP_MS)) {
        return 0u;
    }
    for (unsigned i = 0u; i < 4u; ++i) {
        if ((s->events & k_ev[i]) == 0u) {
            continue;
        }
        uint8_t a = 0u;
        while (a < LINK_SM_PARTS && (s->ev_parts[i] & (1u << a)) == 0u) {
            ++a;
        }
        /* A part's event goes with the part: one no longer fitted on a
         * live link is dropped unseen. */
        while ((k_ev[i] == SENSE_MAP_LINK_EV_SILENT
                || k_ev[i] == SENSE_MAP_LINK_EV_WRONG)
               && a < LINK_SM_PARTS
               && !sense_map_link_event_stands(s, k_ev[i], a)) {
            s->ev_parts[i] &= (uint8_t)~(1u << a);
            while (a < LINK_SM_PARTS
                   && (s->ev_parts[i] & (1u << a)) == 0u) {
                ++a;
            }
        }
        if (k_ev[i] == SENSE_MAP_LINK_EV_SILENT
            || k_ev[i] == SENSE_MAP_LINK_EV_WRONG) {
            if (a >= LINK_SM_PARTS) {
                /* Over before it was handed out. */
                s->events &= (uint8_t)~k_ev[i];
                continue;
            }
            s->ev_parts[i] &= (uint8_t)~(1u << a);
        } else {
            s->ev_parts[i] = 0u;
        }
        if (s->ev_parts[i] == 0u) {
            s->events &= (uint8_t)~k_ev[i];
        }
        if (part != NULL) {
            *part = (a < LINK_SM_PARTS) ? a : 0u;
        }
        if (id != NULL) {
            *id = (a < LINK_SM_PARTS) ? s->wrong_id[a] : 0u;
        }
        s->told    = true;
        s->told_ms = now_ms;
        return k_ev[i];
    }
    return 0u;
}

bool sense_map_link_event_stands(const sense_map_link_t *s, uint8_t ev,
                                 uint8_t part)
{
    if (s == NULL || part >= LINK_SM_PARTS) {
        return false;
    }
    const uint8_t bit = (uint8_t)(1u << part);
    switch (ev) {
    case SENSE_MAP_LINK_EV_REFUSED:
        return s->up && s->refused;
    case SENSE_MAP_LINK_EV_DARK:
        /* A dark map is the settings' and holds whatever answers. */
        return (s->dark & bit) != 0u;
    case SENSE_MAP_LINK_EV_SILENT:
    case SENSE_MAP_LINK_EV_WRONG:
        /* About the IOmcu that answered; a link gone since took it. */
        return s->up && (s->want[LINK_SM_ENABLE] & bit) != 0u;
    default:
        return false;
    }
}

void sense_map_link_event_back(sense_map_link_t *s, uint8_t ev,
                               uint8_t part)
{
    if (!sense_map_link_event_stands(s, ev, part)) {
        return;
    }
    uint8_t parts = 0u;
    if (ev == SENSE_MAP_LINK_EV_DARK) {
        parts = s->dark;
    } else if (ev != SENSE_MAP_LINK_EV_REFUSED) {
        parts = (uint8_t)(1u << part);
    }
    raise(s, ev, parts);
}

/* An IOmcu older than 4.25: the mirror part as SENSE reads it. */
static sens_part_t old_part(const sense_link_t *sl, uint32_t now_ms)
{
    if (sl == NULL || !sl->up || !sl->page || !sl->known) {
        return SENS_PART_UNKNOWN;
    }
    /* A frame not written yet first: the flags are the old frame's. */
    const uint16_t rows = (uint16_t)(SENSE_LINK_ROW_I3221
                                     | SENSE_LINK_ROW_I3221_ADDR
                                     | SENSE_LINK_ROW_I3221_SHUNT
                                     | SENSE_LINK_ROW_I3221_CH);
    if ((sense_link_unheld(sl) & rows) != 0u) {
        return sl->refused_i3221 ? SENS_PART_REFUSED : SENS_PART_PENDING;
    }
    const uint16_t f = sense_link_flags(sl);
    if ((f & LINK_SN_I3221_ONLINE) != 0u) {
        return SENS_PART_ONLINE;
    }
    if ((f & LINK_SN_I3221_ID_WRONG) != 0u) {
        return SENS_PART_WRONG;
    }
    return ((uint32_t)(now_ms - sl->setup_ms) >= SENSE_LINK_GRACE_MS)
               ? SENS_PART_SILENT : SENS_PART_SEARCHING;
}

sens_part_t sense_map_link_part(const sense_map_link_t *s,
                                const sense_link_t *sl, unsigned a,
                                unsigned mirror, uint32_t now_ms)
{
    if (s == NULL || a >= LINK_SM_PARTS || !s->want_set) {
        return SENS_PART_UNKNOWN;
    }
    const uint8_t bit = (uint8_t)(1u << a);
    if (s->nobus && (s->fitted & bit) != 0u) {
        return SENS_PART_NO_BUS;
    }
    if ((s->clash & bit) != 0u) {
        return SENS_PART_SAME_ADDR;
    }
    if ((s->want[LINK_SM_ENABLE] & bit) == 0u || !s->up) {
        return SENS_PART_UNKNOWN;
    }
    if ((s->dark & bit) != 0u) {
        return SENS_PART_DARK;
    }
    if (!s->page) {
        return (a == mirror) ? old_part(sl, now_ms) : SENS_PART_OLD;
    }
    if (s->refused) {
        return SENS_PART_REFUSED;
    }
    if (!s->known) {
        return SENS_PART_UNKNOWN;
    }
    if (memcmp(s->want, s->held, sizeof(s->want)) != 0) {
        return SENS_PART_PENDING;
    }
    if (!s->have_status) {
        return SENS_PART_SEARCHING;
    }
    if ((s->flags & LINK_SM_ONLINE(a)) != 0u) {
        return SENS_PART_ONLINE;
    }
    if ((s->flags & LINK_SM_ID_WRONG(a)) != 0u) {
        return SENS_PART_WRONG;
    }
    return (s->online_seen[a] || s->silent_told[a]
            || (uint32_t)(now_ms - s->setup_ms) >= SENSE_LINK_GRACE_MS)
               ? SENS_PART_SILENT : SENS_PART_SEARCHING;
}

uint16_t sense_map_link_status(const sense_map_link_t *s,
                               const sense_link_t *sl, unsigned mirror,
                               uint32_t now_ms)
{
    uint16_t word = 0u;
    for (unsigned a = 0u; a < LINK_SM_PARTS; ++a) {
        word |= (uint16_t)((unsigned)sense_map_link_part(s, sl, a, mirror,
                                                         now_ms)
                           << (4u * a));
    }
    return word;
}
