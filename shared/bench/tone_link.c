/*
 * The panel's half of the TONE link page.  See tone_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tone_link.h"

#include <stddef.h>
#include <string.h>

/* The two frames of the set-up, each four registers from its first; the
 * second carries three set-up registers and register 7, which is 0. */
#define FRAME_N   4u
#define FRAME0_AT ((uint8_t)LINK_TN_ENABLE)
#define FRAME1_AT ((uint8_t)LINK_TN_SPLIT_PCT)
#define FRAME1_CFG (LINK_TN_CONFIG_COUNT - (unsigned)LINK_TN_SPLIT_PCT)

/* The beep numbers run 1 to 65535 and then 1 again.  A number's place in
 * the cycle is the number modulo 65535, so 65535 and "none yet" (0) are
 * the same place: the one before 1. */
#define CYCLE 65535u

/* A status register by its page number. */
#define ST(reg) ((unsigned)(reg) - (unsigned)LINK_TN_FLAGS)

/* The panel is behind the newest beep by more than this many when it takes
 * the numbering for restarted -- the page was disabled and enabled by
 * someone else, or reset -- and not for beeps it missed.  The panel
 * resynchronises at every link-up, so a count this far behind with the link
 * up is not a pace the link can fall to. */
#define RESTART_BEHIND 1024u

static unsigned pos_of(uint16_t n)
{
    return (unsigned)n % CYCLE;
}

static uint16_t num_of(unsigned pos)
{
    return (uint16_t)((pos == 0u) ? CYCLE : pos);
}

/* The number after @p n, and the one before. */
static uint16_t succ_of(uint16_t n)
{
    return num_of((pos_of(n) + 1u) % CYCLE);
}

static uint16_t pred_of(uint16_t n)
{
    return num_of((pos_of(n) + CYCLE - 1u) % CYCLE);
}

/* Beeps from @p from, the last one taken, to @p to, the newest. */
static unsigned dist(uint16_t from, uint16_t to)
{
    return (pos_of(to) + CYCLE - pos_of(from)) % CYCLE;
}

void tone_link_init(tone_link_t *t)
{
    if (t != NULL) {
        memset(t, 0, sizeof(*t));
    }
}

void tone_link_lost(tone_link_t *t)
{
    if (t == NULL) {
        return;
    }
    t->up          = false;
    t->page        = false;
    t->known       = false;
    t->refused0    = false;
    t->refused1    = false;
    t->told        = 0u;
    t->pending     = TONE_LINK_OP_NONE;
    t->have_status = false;
    t->asked_status = false;
    t->was_flags   = 0u;
    t->synced      = false;
    t->ring_empty  = false;
    t->quiet       = 0u;
    t->sel_ok      = false;
    /* What the coprocessor that went said, and not yet shown, is about a
     * page nobody reads now. */
    t->events = 0u;
}

void tone_link_came_up(tone_link_t *t, uint16_t minor, uint32_t now_ms)
{
    (void)now_ms;
    if (t == NULL) {
        return;
    }
    tone_link_lost(t);
    t->up   = true;
    t->page = minor >= TONE_LINK_MINOR;
    if (!t->page && t->want_set && t->want[LINK_TN_ENABLE] != 0u) {
        t->events |= TONE_LINK_EV_NO_PAGE;
    }
}

/* Whether @p w is a set-up SETUP can name: every value inside the page's
 * own range.  A zeroed one -- a snapshot nobody has published into -- is
 * not, and is no reason to write anything.  F_MAX_HZ above F_MIN_HZ and a
 * GAP_MS as long as F_MIN_HZ's period are for the page to judge. */
static bool setup_valid(const tone_setup_t *w)
{
    return w->f_min_hz >= LINK_TN_F_MIN_LO && w->f_min_hz <= LINK_TN_F_MIN_HI
           && w->f_max_hz > LINK_TN_F_MIN_LO && w->f_max_hz <= LINK_TN_F_MAX_HI
           && w->split_pct <= LINK_TN_SPLIT_MAX
           && w->gap_ms >= LINK_TN_GAP_MS_MIN && w->gap_ms <= LINK_TN_GAP_MS_MAX
           && w->min_periods >= LINK_TN_PERIODS_MIN
           && w->min_periods <= LINK_TN_PERIODS_MAX;
}

void tone_link_want(tone_link_t *t, const tone_setup_t *w, uint32_t now_ms)
{
    if (t == NULL || w == NULL || !setup_valid(w)) {
        return;
    }
    uint16_t next[LINK_TN_CONFIG_COUNT];
    next[LINK_TN_ENABLE]      = w->enable ? (uint16_t)LINK_TN_EN_TAP : 0u;
    next[LINK_TN_PIN]         = w->pin;
    next[LINK_TN_F_MIN_HZ]    = w->f_min_hz;
    next[LINK_TN_F_MAX_HZ]    = w->f_max_hz;
    next[LINK_TN_SPLIT_PCT]   = w->split_pct;
    next[LINK_TN_GAP_MS]      = w->gap_ms;
    next[LINK_TN_MIN_PERIODS] = w->min_periods;

    if (t->want_set && memcmp(next, t->want, sizeof(next)) == 0) {
        return;
    }
    const bool was_on = t->want_set && t->want[LINK_TN_ENABLE] != 0u;
    /* The tap enabled now, with a coprocessor answering that cannot read
     * it: said now, not at the next link-up. */
    if (t->up && !t->page && w->enable && !was_on) {
        t->events |= TONE_LINK_EV_NO_PAGE;
    }
    /* What was said about the request this one replaces and not yet shown
     * no longer describes anything asked. */
    uint16_t stale = (uint16_t)(TONE_LINK_EV_PIN_REFUSED
                                | TONE_LINK_EV_SETUP_REFUSED);
    if (!w->enable) {
        stale |= (uint16_t)TONE_LINK_EV_NO_PAGE;
    }
    /* A pin said to be busy is about that pin: off, or another one, and
     * the word waiting to be shown would name a pin nobody asked for. */
    if (!w->enable || !t->want_set || t->want[LINK_TN_PIN] != w->pin) {
        stale |= (uint16_t)TONE_LINK_EV_PIN_BUSY;
    }
    t->events &= (uint16_t)~stale;
    /* The first set-up is the one the panel starts with, not an edit: it
     * is due at once. */
    t->want_ms = t->want_set ? now_ms
                             : (uint32_t)(now_ms - TONE_LINK_SETTLE_MS);
    t->want_set = true;
    memcpy(t->want, next, sizeof(next));
    /* A new set-up is a new question: what was refused is asked again. */
    t->refused0 = false;
    t->refused1 = false;
    t->told     = 0u;
}

/* Whether a tone range and a gap are a set-up the page takes: the high
 * tone above the low one, and a silence at least one period of the low
 * tone long. */
static bool pair_valid(unsigned f_min, unsigned f_max, unsigned gap_ms)
{
    return f_max > f_min && gap_ms * f_min >= 1000u;
}

static bool frame0_differs(const tone_link_t *t)
{
    return memcmp(&t->want[FRAME0_AT], &t->held[FRAME0_AT],
                  FRAME_N * sizeof(uint16_t)) != 0;
}

static bool frame1_differs(const tone_link_t *t)
{
    return memcmp(&t->want[FRAME1_AT], &t->held[FRAME1_AT],
                  FRAME1_CFG * sizeof(uint16_t)) != 0;
}

/* Whether the first frame is owed again for a pin something else holds: the
 * page's FLAGS say the saved tap met a busy pin at boot, the page holds
 * what is asked, and TONE_LINK_RETRY_MS have passed since the last try. */
static bool pin_retry_due(const tone_link_t *t, uint32_t now_ms)
{
    return t->have_status && !t->refused0
           && t->want[LINK_TN_ENABLE] != 0u
           && (t->held[LINK_TN_ENABLE] & LINK_TN_EN_TAP) != 0u
           && (t->status[ST(LINK_TN_FLAGS)] & LINK_TN_PIN_REFUSED) != 0u
           && (uint32_t)(now_ms - t->retry_ms) >= TONE_LINK_RETRY_MS;
}

/* The write owed now, or NONE, with its registers in @p regs.  @p retry
 * counts a first frame owed again for a busy pin. */
static tone_link_op_kind_t write_owed(const tone_link_t *t, uint16_t *regs,
                                      bool retry)
{
    /* Switching the tap off writes the first frame with the values the page
     * holds, so a set-up the page would refuse cannot keep it running; the
     * range follows in a frame of its own. */
    if (!t->refused0 && t->want[LINK_TN_ENABLE] == 0u
        && (t->held[LINK_TN_ENABLE] & LINK_TN_EN_TAP) != 0u) {
        memcpy(regs, &t->held[FRAME0_AT], FRAME_N * sizeof(uint16_t));
        regs[LINK_TN_ENABLE - FRAME0_AT] = 0u;
        return TONE_LINK_OP_FRAME0;
    }
    const bool f0 = !t->refused0 && (frame0_differs(t) || retry);
    const bool f1 = !t->refused1 && frame1_differs(t);
    bool first0 = f0;
    if (f0 && f1) {
        /* The page judges the set-up a frame would leave: the range goes
         * first unless that leaves a gap too short for its low tone and the
         * gap going first does not. */
        first0 = pair_valid(t->want[LINK_TN_F_MIN_HZ],
                            t->want[LINK_TN_F_MAX_HZ],
                            t->held[LINK_TN_GAP_MS])
                 || !pair_valid(t->held[LINK_TN_F_MIN_HZ],
                                t->held[LINK_TN_F_MAX_HZ],
                                t->want[LINK_TN_GAP_MS]);
    }
    if (first0) {
        memcpy(regs, &t->want[FRAME0_AT], FRAME_N * sizeof(uint16_t));
        return TONE_LINK_OP_FRAME0;
    }
    if (f1) {
        memcpy(regs, &t->want[FRAME1_AT], FRAME1_CFG * sizeof(uint16_t));
        regs[FRAME1_CFG] = 0u;                  /* register 7 */
        return TONE_LINK_OP_FRAME1;
    }
    return TONE_LINK_OP_NONE;
}

static void read_op(tone_link_op_t *op, tone_link_op_kind_t kind, uint8_t off,
                    uint8_t n)
{
    op->kind  = kind;
    op->write = false;
    op->page  = (uint8_t)LINK_PAGE_TONE;
    op->off   = off;
    op->n     = n;
}

/* Beeps the page has that the panel has not taken, by the last status. */
static unsigned owed(const tone_link_t *t)
{
    if (!t->synced || !t->have_status) {
        return 0u;
    }
    return dist(t->last, t->status[ST(LINK_TN_BEEP_HEAD)]);
}

bool tone_link_next(tone_link_t *t, uint32_t now_ms, tone_link_op_t *op)
{
    if (t == NULL || op == NULL) {
        return false;
    }
    memset(op, 0, sizeof(*op));
    t->pending = TONE_LINK_OP_NONE;
    if (!t->page) {
        return false;
    }
    if (!t->known) {
        read_op(op, TONE_LINK_OP_READ_SETUP, 0u,
                (uint8_t)LINK_TN_CONFIG_COUNT);
        t->pending = op->kind;
        return true;
    }
    if (t->refused0
        && (uint32_t)(now_ms - t->refused0_ms) >= TONE_LINK_RETRY_MS) {
        t->refused0 = false;
    }
    if (t->want_set
        && (uint32_t)(now_ms - t->want_ms) >= TONE_LINK_SETTLE_MS) {
        uint16_t regs[FRAME_N];
        const bool retry = pin_retry_due(t, now_ms);
        const tone_link_op_kind_t w = write_owed(t, regs, retry);
        if (w != TONE_LINK_OP_NONE) {
            if (retry && w == TONE_LINK_OP_FRAME0) {
                /* Offered again quietly: the operator was told. */
                t->retry_ms = now_ms;
                t->told |= 1u;
            }
            op->kind  = w;
            op->write = true;
            op->page  = (uint8_t)LINK_PAGE_TONE;
            op->off   = (w == TONE_LINK_OP_FRAME0) ? FRAME0_AT : FRAME1_AT;
            op->n     = (uint8_t)FRAME_N;
            memcpy(op->regs, regs, sizeof(regs));
            memcpy(t->out, regs, sizeof(regs));
            t->pending = w;
            return true;
        }
    }
    if ((t->held[LINK_TN_ENABLE] & LINK_TN_EN_TAP) == 0u) {
        return false;
    }
    if (!t->asked_status
        || (uint32_t)(now_ms - t->status_asked_ms) >= TONE_LINK_READ_MS) {
        read_op(op, TONE_LINK_OP_STATUS, (uint8_t)LINK_TN_FLAGS,
                (uint8_t)TONE_LINK_STATUS_COUNT);
        t->pending = op->kind;
        t->asked_status = true;
        t->status_asked_ms = now_ms;
        return true;
    }
    if (owed(t) > 0u) {
        const uint16_t wanted = succ_of(t->last);
        if (!t->sel_ok || t->sel_num != wanted) {
            op->kind  = TONE_LINK_OP_SELECT;
            op->write = true;
            op->page  = (uint8_t)LINK_PAGE_TONE;
            op->off   = (uint8_t)LINK_TN_EVT_SEL;
            op->n     = 1u;
            op->regs[0] = wanted;
            t->out[0] = wanted;
        } else {
            read_op(op, TONE_LINK_OP_BEEP, (uint8_t)LINK_TN_EVT_SEQ,
                    (uint8_t)TONE_LINK_BEEP_COUNT);
        }
        t->pending = op->kind;
        return true;
    }
    return false;
}

static void written(tone_link_t *t, tone_link_op_kind_t w, int result,
                    uint32_t now_ms)
{
    if (result == TONE_LINK_ACK) {
        if (w == TONE_LINK_OP_FRAME0) {
            /* The page starts the capture again, with its ring empty, when
             * the tap is enabled, moved to another pin or tried again for
             * a pin that was busy.  A change of the range only restarts
             * the detector: the ring and the numbering stay. */
            const bool capture =
                t->out[LINK_TN_ENABLE - FRAME0_AT] != t->held[LINK_TN_ENABLE]
                || t->out[LINK_TN_PIN - FRAME0_AT] != t->held[LINK_TN_PIN]
                || (t->have_status
                    && (t->status[ST(LINK_TN_FLAGS)] & LINK_TN_PIN_REFUSED)
                           != 0u);
            memcpy(&t->held[FRAME0_AT], t->out, FRAME_N * sizeof(uint16_t));
            /* The range moved: a gap refused against the old one is asked
             * again. */
            t->refused1 = false;
            if (capture) {
                /* The numbering goes on from the beep before the ring
                 * emptied.  A panel that knows its place keeps it: beeps
                 * it had not read are gone and count as missed, and every
                 * beep after the restart is read, also one that came
                 * before the next status.  One that does not know its
                 * place finds it at the next status. */
                t->ring_empty = !t->synced;
                t->sel_ok     = false;
                /* The flags start again with the capture; a sticky flag
                 * already told stays told while the capture goes on. */
                t->was_flags  = 0u;
            }
        } else {
            memcpy(&t->held[FRAME1_AT], t->out,
                   FRAME1_CFG * sizeof(uint16_t));
            t->refused0 = false;
        }
        return;
    }
    if (w == TONE_LINK_OP_FRAME0) {
        t->refused0    = true;
        t->refused0_ms = now_ms;
        if ((t->told & 1u) == 0u) {
            t->events |= TONE_LINK_EV_PIN_REFUSED;
            t->told |= 1u;
        }
    } else {
        t->refused1 = true;
        if ((t->told & 2u) == 0u) {
            t->events |= TONE_LINK_EV_SETUP_REFUSED;
            t->told |= 2u;
        }
    }
}

/* What a status read says that the operator is told: a pin that is not
 * free and an overrun, each on its edge. */
static void judge_status(tone_link_t *t)
{
    const uint16_t f   = t->status[ST(LINK_TN_FLAGS)];
    const uint16_t was = t->was_flags;
    if ((f & LINK_TN_PIN_REFUSED) != 0u && (was & LINK_TN_PIN_REFUSED) == 0u) {
        t->events |= TONE_LINK_EV_PIN_BUSY;
        t->retry_ms = t->status_ms;
    }
    if ((f & LINK_TN_OVERRUN) != 0u && (was & LINK_TN_OVERRUN) == 0u) {
        t->events |= TONE_LINK_EV_OVERRUN;
    }
    t->was_flags = f;
}

/* Where the panel stands against BEEP_HEAD after a status read. */
static void follow_head(tone_link_t *t)
{
    const uint16_t head = t->status[ST(LINK_TN_BEEP_HEAD)];
    if (head == 0u) {
        /* No beep yet, or the numbering restarted: the next is 1. */
        t->last = 0u;
        t->synced = true;
        t->ring_empty = false;
        return;
    }
    if (!t->synced && t->ring_empty) {
        /* The capture began again with its ring empty and the numbering
         * going on, and the panel had no place before it: every beep the
         * ring holds is a new one.  The panel asks the ring's span back
         * from the head; those not in it were before the restart and are
         * not counted as missed. */
        /* The span crosses the wrap from 65535 to 1 like any other: a
         * number below the ring's size says nothing about how many beeps
         * came since the restart. */
        t->last  = num_of((pos_of(head) + CYCLE - LINK_TN_RING) % CYCLE);
        t->quiet = (uint8_t)LINK_TN_RING;
        t->synced = true;
        t->ring_empty = false;
        return;
    }
    if (!t->synced) {
        /* The newest beep is the first one read: what came before the
         * panel looked is not a count of anything. */
        t->last = pred_of(head);
        t->synced = true;
        return;
    }
    const unsigned d = dist(t->last, head);
    if (d > RESTART_BEHIND) {
        /* Further behind than the ring and the link can account for: the
         * page's numbering began again. */
        t->last = pred_of(head);
    } else if (d > LINK_TN_RING) {
        /* The ring has moved past beeps the panel had not read. */
        t->missed += d - LINK_TN_RING;
        t->last = num_of((pos_of(head) + CYCLE - LINK_TN_RING) % CYCLE);
    }
}

static void take_beep(tone_link_t *t, const uint16_t *r)
{
    const uint16_t sel = t->sel_num;
    t->last = sel;
    const bool quiet = t->quiet != 0u;
    if (quiet) {
        --t->quiet;
    }
    if (r[0] != sel) {
        /* Not in the ring: it left before the panel asked, or, while
         * quiet, it came before the capture began again. */
        if (!quiet) {
            ++t->missed;
        }
        return;
    }
    tone_beep_t *b = &t->hist[t->hist_at];
    b->seq         = sel;
    b->start_ms    = (uint32_t)r[LINK_TN_EVT_START_LO - LINK_TN_EVT_SEQ]
                     | ((uint32_t)r[LINK_TN_EVT_START_HI - LINK_TN_EVT_SEQ]
                        << 16);
    b->len_dms     = r[LINK_TN_EVT_LEN_DMS - LINK_TN_EVT_SEQ];
    b->freq_dhz    = r[LINK_TN_EVT_FREQ_DHZ - LINK_TN_EVT_SEQ];
    b->bursts      = r[LINK_TN_EVT_BURSTS - LINK_TN_EVT_SEQ];
    b->carrier_hhz = r[LINK_TN_EVT_CARRIER_HHZ - LINK_TN_EVT_SEQ];
    b->flags       = (uint8_t)r[LINK_TN_EVT_FLAGS - LINK_TN_EVT_SEQ];
    t->hist_at = (uint8_t)((t->hist_at + 1u) % TONE_LINK_HISTORY);
    if (t->hist_n < TONE_LINK_HISTORY) {
        ++t->hist_n;
    }
    ++t->beeps_read;
}

void tone_link_done(tone_link_t *t, int result, const uint16_t *regs,
                    uint32_t now_ms)
{
    if (t == NULL) {
        return;
    }
    const tone_link_op_kind_t op = t->pending;
    t->pending = TONE_LINK_OP_NONE;
    if (result == TONE_LINK_NO_ANSWER || op == TONE_LINK_OP_NONE) {
        return;                         /* owed still: asked again */
    }
    if (op == TONE_LINK_OP_FRAME0 || op == TONE_LINK_OP_FRAME1) {
        written(t, op, result, now_ms);
        return;
    }
    const bool is_read = op != TONE_LINK_OP_SELECT;
    if (result != TONE_LINK_ACK || (is_read && regs == NULL)) {
        /* A request refused: a coprocessor that names 4.8 and has not the
         * page.  Nothing more is sent to it until the link comes up
         * again. */
        t->page = false;
        if (t->want_set && t->want[LINK_TN_ENABLE] != 0u) {
            t->events |= TONE_LINK_EV_NO_PAGE;
        }
        return;
    }
    switch (op) {
    case TONE_LINK_OP_READ_SETUP:
        memcpy(t->held, regs, sizeof(t->held));
        t->known = true;
        t->was_flags = 0u;
        break;
    case TONE_LINK_OP_STATUS:
        memcpy(t->status, regs, sizeof(t->status));
        t->have_status = true;
        t->status_ms   = now_ms;
        judge_status(t);
        follow_head(t);
        break;
    case TONE_LINK_OP_SELECT:
        t->sel_ok  = true;
        t->sel_num = t->out[0];
        break;
    case TONE_LINK_OP_BEEP:
        take_beep(t, regs);
        break;
    default:
        break;
    }
}

uint16_t tone_link_event(tone_link_t *t, uint32_t now_ms)
{
    /* Most pressing first: what stops the tap being read at all, then what
     * makes its beeps absent or cut. */
    static const uint16_t k_order[] = {
        TONE_LINK_EV_NO_PAGE,       TONE_LINK_EV_PIN_REFUSED,
        TONE_LINK_EV_SETUP_REFUSED, TONE_LINK_EV_PIN_BUSY,
        TONE_LINK_EV_OVERRUN,
    };
    if (t == NULL || t->events == 0u
        || (t->event_given
            && (uint32_t)(now_ms - t->event_ms) < TONE_LINK_EVENT_GAP_MS)) {
        return 0u;
    }
    for (size_t i = 0u; i < sizeof(k_order) / sizeof(k_order[0]); ++i) {
        if ((t->events & k_order[i]) != 0u) {
            t->event_given = true;
            t->event_ms    = now_ms;
            t->events &= (uint16_t)~k_order[i];
            return k_order[i];
        }
    }
    t->events = 0u;                 /* no bit this build knows */
    return 0u;
}

void tone_link_event_back(tone_link_t *t, uint16_t ev)
{
    if (t == NULL || ev == 0u || !t->up) {
        return;
    }
    t->events |= ev;
}

bool tone_link_settled(const tone_link_t *t)
{
    if (t == NULL) {
        return false;
    }
    if (!t->page || !t->want_set) {
        return true;
    }
    uint16_t regs[FRAME_N];
    return t->known && write_owed(t, regs, false) == TONE_LINK_OP_NONE;
}

uint8_t tone_link_unheld(const tone_link_t *t)
{
    if (t == NULL || !t->up || !t->want_set) {
        return 0u;
    }
    if (!t->page) {
        return (t->want[LINK_TN_ENABLE] != 0u) ? (uint8_t)TONE_LINK_ROWS_TAP
                                               : 0u;
    }
    if (!t->known) {
        return 0u;
    }
    return (uint8_t)((frame0_differs(t) ? TONE_LINK_ROWS_TAP : 0u)
                     | (frame1_differs(t) ? TONE_LINK_ROWS_BEEP : 0u));
}

unsigned tone_link_beeps(const tone_link_t *t, tone_beep_t *out, unsigned max)
{
    if (t == NULL || out == NULL) {
        return 0u;
    }
    unsigned n = 0u;
    while (n < max && n < t->hist_n) {
        const unsigned at = ((unsigned)t->hist_at + TONE_LINK_HISTORY - 1u - n)
                            % TONE_LINK_HISTORY;
        out[n] = t->hist[at];
        ++n;
    }
    return n;
}

uint32_t tone_link_read_count(const tone_link_t *t)
{
    return (t != NULL) ? t->beeps_read : 0u;
}

uint32_t tone_link_missed(const tone_link_t *t)
{
    return (t != NULL) ? t->missed : 0u;
}

uint16_t tone_link_flags(const tone_link_t *t)
{
    return (t != NULL && t->have_status) ? t->status[ST(LINK_TN_FLAGS)] : 0u;
}

/* What the tap is doing, as the screen words it; @p fresh says the last
 * status read is recent enough to speak for the page. */
static tone_state_t state_of(const tone_link_t *t, bool fresh)
{
    if (!t->want_set || t->want[LINK_TN_ENABLE] == 0u) {
        /* Off is asked and the page does not hold it yet: not off. */
        return (t->want_set && t->up && t->page && t->known
                && (t->held[LINK_TN_ENABLE] & LINK_TN_EN_TAP) != 0u)
                   ? TONE_STATE_WAITING
                   : TONE_STATE_OFF;
    }
    if (!t->up) {
        return TONE_STATE_WAITING;
    }
    if (!t->page) {
        return TONE_STATE_NO_PAGE;
    }
    if (t->known && (t->held[LINK_TN_ENABLE] & LINK_TN_EN_TAP) == 0u) {
        /* The page holds the tap off, and nothing is read from it. */
        return TONE_STATE_STOPPED;
    }
    if (!fresh) {
        return TONE_STATE_WAITING;
    }
    const uint16_t f = t->status[ST(LINK_TN_FLAGS)];
    if ((f & LINK_TN_PIN_REFUSED) != 0u) {
        return TONE_STATE_REFUSED;
    }
    return ((f & LINK_TN_RUNNING) != 0u) ? TONE_STATE_RUNNING
                                         : TONE_STATE_STOPPED;
}

void tone_link_readout(const tone_link_t *t, uint32_t now_ms,
                       tone_readout_t *r)
{
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
    if (t == NULL) {
        return;
    }
    r->missed = t->missed;
    r->n = (uint8_t)tone_link_beeps(t, r->beeps, TONE_LINK_SHOWN);
    const bool fresh = t->have_status
                       && (uint32_t)(now_ms - t->status_ms)
                              < TONE_LINK_STALE_MS;
    if (t->have_status) {
        const uint16_t f = t->status[ST(LINK_TN_FLAGS)];
        r->overrun      = (f & LINK_TN_OVERRUN) != 0u;
        r->tone         = (f & LINK_TN_TONE) != 0u;
        r->beep         = (f & LINK_TN_BEEP) != 0u;
        r->win_freq_dhz = t->status[ST(LINK_TN_WIN_FREQ_DHZ)];
        r->win_periods  = t->status[ST(LINK_TN_WIN_PERIODS)];
        r->lost         = t->status[ST(LINK_TN_LOST)];
        r->glitches     = t->status[ST(LINK_TN_GLITCHES)];
    }
    r->state = state_of(t, fresh);
}

unsigned tone_link_pin(const tone_link_t *t)
{
    return (t != NULL) ? t->want[LINK_TN_PIN] : 0u;
}

unsigned tone_link_f_min(const tone_link_t *t)
{
    return (t != NULL) ? t->want[LINK_TN_F_MIN_HZ] : 0u;
}

unsigned tone_link_f_max(const tone_link_t *t)
{
    return (t != NULL) ? t->want[LINK_TN_F_MAX_HZ] : 0u;
}
