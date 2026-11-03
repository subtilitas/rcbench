/**
 * @file
 * @brief The board the IOmcu reports.  See ui_board.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_board.h"

#include <stdatomic.h>
#include <stddef.h>

#include "link_pages.h"
#include "settings_sensors.h"

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

/**
 * @brief Hold the soldered INA3221's rows: its part FITTED and its
 *        channels' WATCHES at the board's rails; let every SENSORS FITTED
 *        and WATCHES row go on a board without one.
 *
 * @param r        The board's roles, or NULL.
 * @param changed  Called for each setting whose value moved, or NULL.
 */
static void apply_ina(const outbind_roles_t *r, ui_board_changed_fn changed)
{
    const bool on = r != NULL && r->ina.on && r->ina.part < SET_INA_PARTS;
    for (int a = 0; a < SET_INA_PARTS; ++a) {
        const bool mine = on && a == (int)r->ina.part;
        const setting_id_t fit = SET_INA_FIT(a);
        if ((mine ? settings_fix(fit, 1) : settings_unfix(fit))
            && changed != NULL) {
            changed(fit);
        }
        for (int k = 0; k < (int)OUTBIND_INA_CHANNELS; ++k) {
            const setting_id_t w = SET_INA_W(3 * a + k);
            const bool moved =
                mine ? settings_fix(w, settings_sensors_watch_option(
                                           r->ina.watch[k]))
                     : settings_unfix(w);
            if (moved && changed != NULL) {
                changed(w);
            }
        }
    }
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
    apply_ina(r, changed);
    return true;
}

uint16_t ui_board_tester_rail(uint64_t pins)
{
    const outbind_roles_t *r = ui_board_roles();
    if (r == NULL || !r->ina.on || pins == 0u) {
        return (uint16_t)LINK_SM_WATCH_NONE;
    }
    /* The lowest GPIO among the pins decides. */
    unsigned g = 0u;
    while (((pins >> g) & 1u) == 0u) {
        ++g;
    }
    for (unsigned k = 0; k < OUTBIND_INA_CHANNELS && g < 32u; ++k) {
        const uint16_t w = r->ina.watch[k];
        if ((w == (uint16_t)LINK_SM_WATCH_RAIL_A
             || w == (uint16_t)LINK_SM_WATCH_RAIL_B)
            && ((r->ina.feeds[k] >> g) & 1u) != 0u) {
            return w;
        }
    }
    return (uint16_t)LINK_SM_WATCH_NONE;
}

int ui_board_prog_5v_pin(void)
{
    const outbind_roles_t *r = ui_board_roles();
    return (r != NULL) ? (int)r->prog_5v_en : -1;
}
