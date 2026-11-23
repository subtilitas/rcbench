/**
 * @file
 * @brief Binding writes on the remote servo node's common rail.
 *
 * See rail_bind.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "rail_bind.h"

#include <stddef.h>

#include "board_fix.h"
#include "link_pages.h"

/**
 * @brief The bit of the output a pin is.
 *
 * @param r   The board's roles.
 * @param pin The GPIO.
 * @return    1 << k for output 0 to 8; 0 for any other pin.
 */
static uint16_t port_bit(const outbind_roles_t *r, unsigned pin)
{
    const int k = board_fix_out_index(r, pin);
    return (k >= 0 && k < (int)LINK_PW_PORTS) ? (uint16_t)(1u << (unsigned)k)
                                              : 0u;
}

uint16_t rail_bind_lock(const power_page_t *pg, bool open, int k)
{
    if (pg == NULL || pg->fitted != 0u || !open) {
        return 0u;
    }
    const uint16_t own = (k >= 0 && k < (int)LINK_PW_PORTS)
                             ? (uint16_t)(1u << (unsigned)k) : 0u;
    return (uint16_t)(LINK_PW_PORT_ALL & ~own);
}

uint16_t rail_bind_slots(const uint16_t *slots, const outbind_roles_t *r)
{
    if (slots == NULL || r == NULL) {
        return 0u;
    }
    uint16_t m = 0u;
    for (unsigned s = 0; s < LINK_OUT_SLOTS_ALL; ++s) {
        const uint16_t *sl = &slots[s * LINK_OS_STRIDE];
        if (sl[LINK_OS_DRIVER] != (uint16_t)LINK_DRIVER_NONE) {
            m |= port_bit(r, sl[LINK_OS_PIN]);
        }
    }
    return m;
}

uint16_t rail_bind_prog(const prog_port_t *pp, const outbind_roles_t *r)
{
    if (pp == NULL || r == NULL) {
        return 0u;
    }
    uint16_t m = 0u;
    for (uint8_t q = 0u; q < prog_port_count(pp); ++q) {
        const uint8_t pin = prog_port_pin(pp, q);
        if (pin != PROG_PORT_NO_PIN
            && prog_port_func(pp, q) != (uint8_t)LINK_PG_FN_NONE) {
            m |= port_bit(r, pin);
        }
    }
    return m;
}

bool rail_bind_refuses(uint16_t lock, uint16_t before, uint16_t after)
{
    return (uint16_t)(after & ~before & lock) != 0u;
}

uint16_t rail_bind_hold(power_page_t *pg, const outputs_t *o,
                        const prog_port_t *pp, const outbind_roles_t *r)
{
    if (pg == NULL || o == NULL || r == NULL) {
        return 0u;
    }
    return power_hold(pg, power_throttle(o, pp, r));
}
