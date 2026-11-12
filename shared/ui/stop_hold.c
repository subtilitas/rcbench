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

/** @brief The hold on STOP, one instance per image. */
typedef struct {
    ui_hold_t hold;     /**< the hold on STOP */
    /** @brief A fired hold whose reset has not been seen done yet */
    bool      waiting;
    /** @brief The reset the hold asked for and nobody has taken */
    bool      asked;
} stop_hold_state_t;

/** @brief The hold's one instance. */
static stop_hold_state_t sh;

void stop_hold_reset(void)
{
    ui_hold_reset(&sh.hold);
    sh.waiting = false;
    sh.asked   = false;
}

void stop_hold_press(bool latched)
{
    /* Every press on STOP is a STOP: a reset asked before it and not yet
     * taken is for a stop that no longer is the last one. */
    sh.asked = false;
    /* A press that latches the stop is a STOP and nothing more: holding it
     * on does not undo it. */
    if (latched) {
        ui_hold_begin(&sh.hold, (int)TX_HOLD_HINT_STOP_RESET);
    } else {
        ui_hold_reset(&sh.hold);
    }
}

void stop_hold_slide(int x, int y)
{
    if (!gfx_rect_contains(ui_band_stop_rect(), x, y)) {
        (void)ui_hold_leave(&sh.hold);
    }
}

bool stop_hold_release(const touch_event_t *up)
{
    return ui_hold_end(&sh.hold, up, ui_band_stop_rect());
}

void stop_hold_cancel(void)
{
    (void)ui_hold_left(&sh.hold);
    sh.asked = false;
}

bool stop_hold_tick(float dt_s, bool latched, bool contact)
{
    ui_hold_flash_step(&sh.hold, dt_s);
    if (!contact) {
        /* The press the hold counts is no longer the router's: a screen
         * change or a band press dropped it.  One that fired waits for
         * nothing more and keeps its flash. */
        (void)ui_hold_leave(&sh.hold);
    }
    if (sh.waiting && !latched) {
        /* The reset is done: the press that asked for it flashes, if it is
         * still on the glass. */
        sh.waiting = false;
        if (sh.hold.fired) {
            ui_hold_reached(&sh.hold);
        }
    }
    if (!latched && !sh.waiting) {
        /* Nothing to reset: a hold left running asks for nothing. */
        (void)ui_hold_leave(&sh.hold);
        return false;
    }
    if (!ui_hold_tick(&sh.hold, dt_s)) {
        return false;
    }
    sh.asked   = true;
    sh.waiting = true;
    return true;
}

bool stop_hold_take_reset(void)
{
    const bool was = sh.asked;
    sh.asked = false;
    return was;
}

float stop_hold_held_s(void)
{
    return sh.hold.down ? sh.hold.held_s : 0.0f;
}

int stop_hold_flash_ms(void)
{
    return sh.hold.flash_ms;
}
