/**
 * @file stop_hold.c
 * @brief The hold that resets a latched STOP (stop_hold.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "stop_hold.h"

#include <stddef.h>

#include "gfx.h"
#include "ui_band.h"
#include "ui_text.h"
#include "ui_widgets.h"

/** @brief The hold on STOP. */
static ui_hold_t s_hold;
/** @brief A fired hold whose reset has not been seen done yet. */
static bool      s_waiting;
/** @brief The reset the hold asked for and nobody has taken. */
static bool      s_asked;

void stop_hold_reset(void)
{
    ui_hold_reset(&s_hold);
    s_waiting = false;
    s_asked   = false;
}

void stop_hold_press(bool latched)
{
    /* A press that latches the stop is a STOP and nothing more: holding it
     * on does not undo it. */
    if (latched) {
        ui_hold_begin(&s_hold, (int)TX_HOLD_HINT_STOP_RESET);
    } else {
        ui_hold_reset(&s_hold);
    }
}

void stop_hold_slide(int x, int y)
{
    if (!gfx_rect_contains(ui_band_stop_rect(), x, y)) {
        (void)ui_hold_leave(&s_hold);
    }
}

bool stop_hold_release(const touch_event_t *up)
{
    return ui_hold_end(&s_hold, up, ui_band_stop_rect());
}

void stop_hold_cancel(void)
{
    (void)ui_hold_left(&s_hold);
}

bool stop_hold_tick(float dt_s, bool latched)
{
    ui_hold_flash_step(&s_hold, dt_s);
    if (s_waiting && !latched) {
        /* The reset is done: the press that asked for it flashes, if it is
         * still on the glass. */
        s_waiting = false;
        if (s_hold.fired) {
            ui_hold_reached(&s_hold);
        }
    }
    if (!latched && !s_waiting) {
        /* Nothing to reset: a hold left running asks for nothing. */
        (void)ui_hold_leave(&s_hold);
        return false;
    }
    if (!ui_hold_tick(&s_hold, dt_s)) {
        return false;
    }
    s_asked   = true;
    s_waiting = true;
    return true;
}

bool stop_hold_take_reset(void)
{
    const bool was = s_asked;
    s_asked = false;
    return was;
}

float stop_hold_held_s(void)
{
    return s_hold.down ? s_hold.held_s : 0.0f;
}

int stop_hold_flash_ms(void)
{
    return s_hold.flash_ms;
}
