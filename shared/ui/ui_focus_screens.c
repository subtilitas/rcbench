/**
 * @file
 * @brief The screens the M5Stack Dial navigates (ui_focus.h): which screen
 *        names its items where, and the overview's tiles.
 *
 * | Screen | Items |
 * | --- | --- |
 * | the overview | its tiles, row by row |
 * | SETUP | the hub's tiles and SAVE; a list's rows and strip; a split pane's entries first (settings_screen_focus()) |
 * | SERVO's hub | its eight tiles and SETTINGS, or the SETTINGS list (servo_hub_focus()) |
 * | OUTPUTS, board view | the protocol list's face, CLEAR ALL and the pins, or the open list (picker_screen_focus()) |
 *
 * Every other screen offers none: the Dial does there what it did before
 * the focus layer, on a bench screen the throttle or the horn.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_focus.h"

#include "outputs_screen.h"
#include "overview_screen.h"
#include "picker_screen.h"
#include "servo_screen.h"
#include "settings_screen.h"

/** @brief Slots the overview is asked for: more than its grid holds. */
#define OVERVIEW_SLOTS 32

/**
 * @brief The overview's tiles, row by row from the top left.
 * @param out Receives them.
 * @return true.
 */
static bool overview_focus(ui_focus_list_t *out)
{
    out->list = 0x4F56u;
    for (int slot = 0; slot < OVERVIEW_SLOTS; ++slot) {
        if (overview_screen_tile(slot) == SCREEN_COUNT) {
            continue;
        }
        (void)ui_focus_add(out, overview_screen_tile_rect(slot),
                           overview_screen_tile_name(slot));
    }
    return true;
}

bool ui_focus_collect(ui_screen_id_t route, ui_focus_list_t *out)
{
    if (out == NULL) {
        return false;
    }
    switch (route) {
    case SCREEN_OVERVIEW:
        return overview_focus(out);
    case SCREEN_SETUP:
        return settings_screen_focus(out);
    case SCREEN_SERVO:
        return servo_hub_focus(out);
    case SCREEN_OUTPUTS:
        return outputs_view_is_board() && picker_screen_focus(out);
    default:
        return false;
    }
}
