/**
 * @file
 * @brief The Guition JC3636K518C's still touch and update key
 *        (dk518_input.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "dk518_input.h"

#include <string.h>

#include "dialknob.h"

void dk518_still_init(dk518_still_t *s)
{
    memset(s, 0, sizeof *s);
    dial_button_init(&s->button);
}

/**
 * @brief Whether a point is over DK518_STILL_PX from the start.
 * @param s the state
 * @param x the column
 * @param y the row
 * @return true when it moved that far on either axis
 */
static bool far(const dk518_still_t *s, uint16_t x, uint16_t y)
{
    const int dx = (int)x - (int)s->x0;
    const int dy = (int)y - (int)s->y0;
    return dx > DK518_STILL_PX || dx < -DK518_STILL_PX
           || dy > DK518_STILL_PX || dy < -DK518_STILL_PX;
}

unsigned dk518_still_step(dk518_still_t *s, bool touch, uint16_t x,
                          uint16_t y, int32_t turns, bool yield,
                          uint32_t now, dk518_still_out_t *out)
{
    dk518_still_out_t o;
    memset(&o, 0, sizeof o);
    if (touch && !s->down) {
        s->down = true;
        s->moved = false;
        s->held = false;
        o.began = true;
        s->x0 = x;
        s->y0 = y;
        s->down_ms = now;
    } else if (!touch && s->down) {
        const uint32_t t = now - s->down_ms;
        s->down = false;
        o.lifted = true;
        if (!s->moved) {
            o.still_lift = t < DK_HOLD_MS;
            if (t <= DK518_TAP_MS) {
                o.tap = true;
                o.tap_x = dk518_touch_scale(s->x0);
                o.tap_y = dk518_touch_scale(s->y0);
            }
        }
    }
    unsigned act = 0u;
    if (touch && !s->moved && far(s, x, y)) {
        s->moved = true;
        if (!yield && now - s->down_ms < DK_HOLD_MS
            && !dial_button_local(&s->button)) {
            /* The press under way goes nowhere until the lift. */
            dial_button_yield(&s->button, true, now);
        }
    }
    if (touch && !s->moved && !s->held && now - s->down_ms >= DK_HOLD_MS) {
        s->held = true;
        o.held = true;
    }
    if (yield) {
        dial_button_yield(&s->button, touch, now);
    } else {
        act = dial_button_step(&s->button, touch, turns, now);
    }
    if (out != NULL) {
        *out = o;
    }
    return act;
}

void dk518_glass_init(dk518_glass_t *g)
{
    memset(g, 0, sizeof *g);
}

bool dk518_glass_read(dk518_glass_t *g, dk518_read_t r, uint16_t x,
                      uint16_t y)
{
    if (r != DK518_READ_FAIL) {
        g->fails = 0u;
        if (r == DK518_READ_UP) {
            g->lost = false;
        }
        /* After a loss, the finger read again is the same touch: it
         * counts once the glass read empty. */
        g->down = r == DK518_READ_DOWN && !g->lost;
        if (g->down) {
            g->x = x;
            g->y = y;
        }
        return false;
    }
    /* A failure says nothing about the finger: the last state stands
     * until DK518_READ_FAILS of them in a row. */
    if (g->fails < DK518_READ_FAILS) {
        ++g->fails;
    }
    if (g->fails == DK518_READ_FAILS && g->down) {
        g->down = false;
        g->lost = true;
        return true;
    }
    return false;
}

void dk518_still_lost(dk518_still_t *s, uint32_t now)
{
    if (!s->down) {
        return;
    }
    /* Taken as a move: the lift gives no PRESS, TAP or still lift, and
     * the button owns the press, so no stage answers it. */
    s->moved = true;
    dial_button_yield(&s->button, true, now);
}

void dk518_key_init(dk518_key_t *k)
{
    memset(k, 0, sizeof *k);
}

/**
 * @brief Whether a new window may open: none was opened in this offer,
 *        or the last one ended DK518_KEY_GAP_MS ago.
 * @param k   the key
 * @param now the time
 * @return true when it may
 */
static bool key_free(const dk518_key_t *k, uint32_t now)
{
    return !k->on
           || (!k->hold
               && (int32_t)(now - k->until) >= (int32_t)DK518_KEY_GAP_MS);
}

bool dk518_key_step(dk518_key_t *k, bool offer, uint32_t id, bool shown,
                    bool empty, const dk518_still_out_t *o, uint32_t now)
{
    bool press = false;
    if (offer != k->offer || id != k->id) {
        /* Opened, closed or replaced: the window ends, the touch under
         * way is nobody's, and the glass must read empty again. */
        k->offer = offer;
        k->id = id;
        k->touch_ok = false;
        k->hold = false;
        k->on = false;
        k->clear = false;
    }
    if (offer && !shown) {
        /* The question is not on the display, or another face covers it
         * (EMPTY): nothing on the glass answers it, and once it shows
         * again the glass must read empty first. */
        k->clear = false;
        k->touch_ok = false;
    } else if (empty && offer) {
        /* The glass empty while this offer's question was on display. */
        k->clear = true;
    }
    if (o->began) {
        /* The finger came down after a read that found the glass empty
         * with this offer standing. */
        k->touch_ok = offer && k->clear;
    }
    if (o->held && k->touch_ok) {
        const uint32_t until = now + DK518_KEY_HOLD_MS;
        if (!k->on || (int32_t)(until - k->until) > 0) {
            k->until = until;
        }
        k->on = true;
        k->hold = true;
    }
    if (o->still_lift && k->touch_ok && key_free(k, now)) {
        k->on = true;
        k->until = now + DK518_KEY_PRESS_MS;
        press = true;
    }
    if (o->lifted) {
        k->touch_ok = false;
        k->hold = false;
    }
    return press;
}

uint32_t dk518_key_offer(const dk518_key_t *k)
{
    return k->id;
}

bool dk518_key_down(const dk518_key_t *k, uint32_t now)
{
    return k->offer && k->on
           && (k->hold || (int32_t)(k->until - now) > 0);
}

uint8_t dk518_touch_scale(uint16_t v)
{
    const unsigned c = v >= DK518_TOUCH_SIZE ? DK518_TOUCH_SIZE - 1u : v;
    return (uint8_t)(c * 2u / 3u);
}
