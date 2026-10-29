/**
 * @file
 * @brief The board the IOmcu reports.  See ui_board.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_board.h"

#include <stdatomic.h>
#include <stddef.h>

/** @brief The board last recorded. */
static atomic_uint_least16_t s_board;

void ui_board_set(uint16_t board)
{
    atomic_store(&s_board, board);
}

uint16_t ui_board(void)
{
    return (uint16_t)atomic_load(&s_board);
}

const outbind_roles_t *ui_board_roles(void)
{
    return outbind_roles(ui_board());
}

/** @brief The board ui_board_apply_pins() applied last; 0xFFFF before any. */
static uint16_t s_applied = 0xFFFFu;

bool ui_board_apply_pins(ui_board_changed_fn changed)
{
    const uint16_t board = ui_board();
    if (board == s_applied) {
        return false;
    }
    s_applied = board;
    const outbind_roles_t *r = outbind_roles(board);
    static const setting_id_t k_ids[] = {
        SET_SENSE_SDA, SET_SENSE_SCL, SET_TONE_PIN, SET_PDMINI_TX,
        SET_PDMINI_RX,
    };
    for (size_t i = 0; i < sizeof(k_ids) / sizeof(k_ids[0]); ++i) {
        bool moved;
        if (r == NULL) {
            moved = settings_unfix(k_ids[i]);
        } else {
            const uint8_t pin = (i == 0u) ? r->sda
                              : (i == 1u) ? r->scl
                              : (i == 2u) ? r->tap
                              : (i == 3u) ? r->pd_tx : r->pd_rx;
            moved = settings_fix(k_ids[i], (int)pin);
        }
        if (moved && changed != NULL) {
            changed(k_ids[i]);
        }
    }
    return true;
}

int ui_board_prog_5v_pin(void)
{
    const outbind_roles_t *r = ui_board_roles();
    return (r != NULL) ? (int)r->prog_5v_en : -1;
}
