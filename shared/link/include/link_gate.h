/**
 * @file
 * @brief The link writes that could drive a pin, judged as one: what the
 *        board 3 IOmcu refuses on a chip before the RP2350 A4 stepping
 *        (chip_gate.h, link_dev_refuse_drive()).
 *
 * Pure logic, host-tested in test_chip_gate.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_GATE_H
#define RCBENCH_LINK_GATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A link write that could drive a pin: 0 to let the page judge it,
 *        LINK_NACK_NOT_ARMED to refuse it before any page does.
 *
 * Refused: a CONTROL ARM other than 0; a SUPPLY OUTPUT other than 0; a
 * POWER RAIL other than 0 or any PORT_WANT bit; every write to KST, HITEC
 * and ESC_WIRE (a programming session's CONNECT among them); a PROG FUNC
 * other than NONE; an OUTPUTS, OUTPUTS_HI, BIND_OUT or BIND_OUT_HI slot
 * with a driver; a PASS SOURCE other than BENCH (OFF drives its pad low).
 * Everything else, and every other OFF, is passed on.
 *
 * @param page  A link_page_t number.
 * @param off   First register written.
 * @param n     Registers written.
 * @param in    The values, @p n of them; NULL with @p n not 0 is refused.
 * @return      0 or LINK_NACK_NOT_ARMED.
 */
uint8_t link_gate_drive(uint8_t page, uint8_t off, uint8_t n,
                        const uint16_t *in);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_GATE_H */
