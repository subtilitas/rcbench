/**
 * @file
 * @brief The Dial's screen button: the panel's gestures, the switch-off's
 *        warning and choice, and the forget.  See include/dial_button.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial_button.h"

#include <string.h>

#include "dialknob.h"

void dial_button_init(dial_button_t *b)
{
    memset(b, 0, sizeof *b);
    b->stage = DIAL_OFF_NONE;
}

/**
 * @brief The button went up.
 * @param b   the state
 * @param now the time
 * @return DIAL_BTN_* bits
 */
static unsigned released(dial_button_t *b, uint32_t now)
{
    const bool press = now - b->down_ms < DK_HOLD_MS;
    b->down = false;
    if (b->own) {
        /* A press in the choice takes the focused answer; a hold has
         * taken KEEP ON already, at 1,000ms. */
        b->own = false;
        if (b->stage == DIAL_OFF_ASK && press) {
            b->stage = DIAL_OFF_NONE;
            return b->focus_off ? DIAL_BTN_OFF : DIAL_BTN_KEEP;
        }
        return 0u;
    }
    if (b->stage == DIAL_OFF_WARN) {
        b->stage = DIAL_OFF_ASK;
        b->focus_off = false;
        b->idle_ms = now;
        return 0u;
    }
    return b->hold_sent ? 0u : DIAL_BTN_PRESS;
}

/**
 * @brief The button is down: HOLD, the warning, the forget; in the
 *        choice, the hold that takes KEEP ON.
 * @param b   the state
 * @param now the time
 * @return DIAL_BTN_* bits
 */
static unsigned held(dial_button_t *b, uint32_t now)
{
    const uint32_t t = now - b->down_ms;
    unsigned act = 0u;
    if (b->own) {
        if (b->stage == DIAL_OFF_ASK && t >= DK_HOLD_MS) {
            b->stage = DIAL_OFF_NONE;
            act = DIAL_BTN_KEEP;
        }
        return act;
    }
    if (!b->hold_sent && t >= DK_HOLD_MS) {
        b->hold_sent = true;
        act |= DIAL_BTN_HOLD;
    }
    if (!b->forgot && t >= DK_FORGET_MS) {
        b->forgot = true;
        b->stage = DIAL_OFF_NONE;
        act |= DIAL_BTN_FORGET;
    } else if (!b->forgot && b->stage == DIAL_OFF_NONE && t >= DIAL_OFF_MS) {
        b->stage = DIAL_OFF_WARN;
        b->warn_ms = now;
        act |= DIAL_BTN_WARN;
    }
    return act;
}

unsigned dial_button_step(dial_button_t *b, bool down, int32_t turns,
                          uint32_t now)
{
    unsigned act = 0u;
    if (b->stage == DIAL_OFF_ASK && turns != 0) {
        b->focus_off = turns > 0;
        b->idle_ms = now;
    }
    if (down && !b->down) {
        b->down = true;
        b->down_ms = now;
        b->hold_sent = false;
        b->forgot = false;
        b->own = b->stage == DIAL_OFF_ASK;
        b->idle_ms = now;
    } else if (!down && b->down) {
        act |= released(b, now);
    }
    if (b->down) {
        act |= held(b, now);
    } else if (b->stage == DIAL_OFF_ASK
               && now - b->idle_ms >= DIAL_OFF_ASK_MS) {
        b->stage = DIAL_OFF_NONE;
        act |= DIAL_BTN_KEEP;
    }
    return act;
}

bool dial_button_local(const dial_button_t *b)
{
    return b->stage != DIAL_OFF_NONE;
}

void dial_button_view(const dial_button_t *b, uint32_t now,
                      dial_face_view_t *v)
{
    v->off = b->stage;
    v->off_lit = b->stage == DIAL_OFF_WARN
                 && ((now - b->warn_ms) / DIAL_OFF_BLINK_MS) % 2u == 0u;
    v->off_focus = b->stage == DIAL_OFF_ASK && b->focus_off;
    v->off_secs = 0u;
    if (b->stage == DIAL_OFF_ASK) {
        const uint32_t gone = now - b->idle_ms;
        const uint32_t left = gone >= DIAL_OFF_ASK_MS ? 0u
                                                      : DIAL_OFF_ASK_MS - gone;
        v->off_secs = (uint8_t)((left + 999u) / 1000u);
    }
}
