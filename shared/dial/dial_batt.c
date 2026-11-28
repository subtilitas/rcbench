/**
 * @file
 * @brief The Dial's battery share and the K518's supply reading
 *        (dial_batt.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial_batt.h"

#include <string.h>

uint8_t dial_batt_pct(uint16_t mv)
{
    if (mv <= DIAL_BATT_EMPTY_MV) {
        return 0u;
    }
    if (mv >= DIAL_BATT_FULL_MV) {
        return 100u;
    }
    return (uint8_t)((mv - DIAL_BATT_EMPTY_MV) * 100u
                     / (DIAL_BATT_FULL_MV - DIAL_BATT_EMPTY_MV));
}

uint16_t dk518_rail_mv(const uint16_t pin_mv[DK518_SAMPLES])
{
    uint32_t sum = 0u;
    uint16_t lo = UINT16_MAX;
    uint16_t hi = 0u;
    for (unsigned i = 0u; i < DK518_SAMPLES; ++i) {
        sum += pin_mv[i];
        lo = pin_mv[i] < lo ? pin_mv[i] : lo;
        hi = pin_mv[i] > hi ? pin_mv[i] : hi;
    }
    sum -= (uint32_t)lo + hi;
    const uint32_t mv = 2u * sum / (DK518_SAMPLES - 2u);
    return (uint16_t)(mv > UINT16_MAX ? UINT16_MAX : mv);
}

void dk518_supply_init(dk518_supply_t *s)
{
    memset(s, 0, sizeof *s);
}

/**
 * @brief Step one flag by a cell reading.
 * @param f     the flag
 * @param mv    the reading
 * @param below sets toward on under this
 * @param ok    sets toward off from this
 */
static void flag_step(dk518_flag_t *f, uint16_t mv, uint16_t below,
                      uint16_t ok)
{
    const bool toward = f->on ? mv >= ok : mv < below;
    const bool away = f->on ? mv < below : mv >= ok;
    if (toward) {
        if (++f->run >= DK518_BATT_RUN) {
            f->on = !f->on;
            f->run = 0u;
        }
    } else if (away) {
        f->run = 0u;
    }
}

void dk518_supply_step(dk518_supply_t *s, uint16_t rail_mv, bool host)
{
    s->usb = host || rail_mv > DK518_USB_MV;
    s->known = !s->usb && rail_mv >= DK518_NO_CELL_MV;
    s->mv = s->known ? rail_mv : 0u;
    if (!s->known) {
        memset(&s->low, 0, sizeof s->low);
        memset(&s->empty, 0, sizeof s->empty);
        return;
    }
    flag_step(&s->low, rail_mv, DK518_LOW_MV, DK518_LOW_OK_MV);
    flag_step(&s->empty, rail_mv, DK518_EMPTY_MV, DK518_EMPTY_OK_MV);
}
