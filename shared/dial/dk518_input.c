/**
 * @file
 * @brief The Guition JC3636K518C's encoder decode and still touch
 *        (dk518_input.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "dk518_input.h"

#include <string.h>

#include "dialknob.h"

void dk518_enc_init(dk518_enc_t *e)
{
    memset(e, 0, sizeof *e);
}

/**
 * @brief One line's step: a detent when its total moved after a quiet
 *        step.
 * @param last  the line's last total, updated
 * @param run   whether the last step saw an edge, updated
 * @param total its total now
 * @return 1 for a detent, else 0
 */
static int32_t line_step(int32_t *last, bool *run, int32_t total)
{
    const bool edge = total != *last;
    const int32_t d = edge && !*run ? 1 : 0;
    *last = total;
    *run = edge;
    return d;
}

int32_t dk518_enc_step(dk518_enc_t *e, int32_t cw, int32_t ccw)
{
    if (!e->primed) {
        e->primed = true;
        e->cw = cw;
        e->ccw = ccw;
        return 0;
    }
    const int32_t up = line_step(&e->cw, &e->cw_run, cw);
    const int32_t down = line_step(&e->ccw, &e->ccw_run, ccw);
    return up - down;
}

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

bool dk518_key_step(dk518_key_t *k, bool offer,
                    const dk518_still_out_t *o, uint32_t now)
{
    bool press = false;
    if (offer != k->offer) {
        /* Opened or closed: the window ends, the touch under way is
         * nobody's. */
        k->offer = offer;
        k->touch_ok = false;
        k->hold = false;
        k->on = false;
    }
    if (o->began) {
        k->touch_ok = offer;
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
