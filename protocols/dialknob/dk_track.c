/**
 * @file
 * @brief The panel's side of the Dial's link: the turn tracker (reference,
 *        stale, glitch, repeat and skip rules) and the link's state (UP,
 *        DEGRADED, LOST) with the loss that is an error while ARMED.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "dialknob.h"

void dk_track_init(dk_track_t *t)
{
    if (t != NULL) {
        memset(t, 0, sizeof(*t));
    }
}

void dk_track_reset(dk_track_t *t)
{
    if (t != NULL) {
        t->have_ref = false;
    }
}

/**
 * @brief Make an EVENT the new reference.
 * @param t      the tracker
 * @param e      the event
 * @param now_ms its arrival
 */
static void take_ref(dk_track_t *t, const dk_evt_t *e, uint32_t now_ms)
{
    t->have_ref = true;
    t->seq = e->seq;
    t->counter = e->counter;
    t->last_ms = now_ms;
}

int32_t dk_track_feed(dk_track_t *t, const dk_evt_t *e, uint32_t now_ms,
                      dk_take_t *why)
{
    dk_take_t got;
    int32_t moved = 0;
    if (t == NULL || e == NULL) {
        if (why != NULL) {
            *why = DK_TAKE_REPEAT;
        }
        return 0;
    }
    if (!t->have_ref) {
        ++t->refs;
        take_ref(t, e, now_ms);
        got = DK_TAKE_REF;
    } else if (e->seq == t->seq) {
        ++t->repeats;
        got = DK_TAKE_REPEAT;
    } else {
        /* Both differences wrap: seq at 256, the counter at 65,536. */
        const uint8_t gap = (uint8_t)(e->seq - t->seq - 1u);
        const int16_t diff = (int16_t)(uint16_t)((uint16_t)e->counter
                                                 - (uint16_t)t->counter);
        t->skipped += gap;
        if (now_ms - t->last_ms > DK_STALE_MS) {
            ++t->stale;
            got = DK_TAKE_STALE;
        } else if (diff > DK_MAX_STEP || diff < -DK_MAX_STEP) {
            ++t->glitches;
            got = DK_TAKE_GLITCH;
        } else {
            ++t->taken;
            moved = diff;
            got = DK_TAKE_MOVE;
        }
        take_ref(t, e, now_ms);
    }
    if (why != NULL) {
        *why = got;
    }
    return moved;
}

void dk_link_init(dk_link_t *l)
{
    if (l != NULL) {
        memset(l, 0, sizeof(*l));
        l->state = DK_LINK_DOWN;
    }
}

void dk_link_off(dk_link_t *l)
{
    if (l != NULL) {
        l->state = DK_LINK_DOWN;
        l->connected = false;
        l->loss = false;
    }
}

void dk_link_connect(dk_link_t *l, uint32_t now_ms)
{
    if (l != NULL) {
        l->connected = true;
        l->last_ms = now_ms;
        l->state = DK_LINK_UP;
    }
}

void dk_link_rx(dk_link_t *l, uint32_t now_ms)
{
    if (l != NULL && l->connected) {
        l->last_ms = now_ms;
        l->state = DK_LINK_UP;
    }
}

/**
 * @brief Enter LOST; a move from UP or DEGRADED is a loss.
 * @param l the link
 */
static void lose(dk_link_t *l)
{
    if (l->state == DK_LINK_UP || l->state == DK_LINK_DEGRADED) {
        l->loss = true;
        ++l->losses;
    }
    l->state = DK_LINK_LOST;
}

void dk_link_disconnect(dk_link_t *l)
{
    if (l != NULL) {
        l->connected = false;
        lose(l);
    }
}

uint8_t dk_link_step(dk_link_t *l, uint32_t now_ms)
{
    if (l == NULL) {
        return DK_LINK_DOWN;
    }
    if (l->connected) {
        const uint32_t quiet = now_ms - l->last_ms;
        if (quiet >= DK_LOST_MS) {
            lose(l);
        } else if (quiet >= DK_DEGRADED_MS && l->state == DK_LINK_UP) {
            l->state = DK_LINK_DEGRADED;
        }
    }
    return l->state;
}

bool dk_link_loss_error(dk_link_t *l, bool armed)
{
    if (l == NULL || !l->loss) {
        return false;
    }
    l->loss = false;
    return armed;
}
