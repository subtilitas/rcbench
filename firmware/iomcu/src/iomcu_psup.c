/**
 * @file
 * @brief The PROG_SUPPLY page and the pin behind it: the programming ports'
 *        own supply on a board that switches one (the rbmod carrier's GPIO
 *        21, the enable of its 5V buck M7); see iomcu.h for the other files.
 *
 * SPDX-License-Identifier: MIT
 */
#include "iomcu.h"
#include "iomcu_pins.h"

#include "hardware/gpio.h"

prog_supply_t s_psup;

void psup_init(void)
{
    const outbind_roles_t *r = outbind_roles(IOMCU_BOARD_ID);
    prog_supply_init(&s_psup, (r != NULL) ? r->prog_5v_en : LINK_PS_NO_PIN,
                     PROG_SUPPLY_CARRIER_MV);
}

void psup_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    prog_supply_read(&s_psup, off, n, out);
}

uint8_t psup_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return prog_supply_write(&s_psup, off, n, in,
                             safety_gate_supply_ok(&s_beat, &s_dev));
}

void psup_pass(uint32_t now, bool frame_out, bool writing, bool supply_ok)
{
    prog_supply_port_frame(&s_psup, frame_out, now);
    prog_supply_port_writes(&s_psup, writing);
    const bool on = prog_supply_step(&s_psup, supply_ok);
    if (prog_supply_fitted(&s_psup)) {
        gpio_put(s_psup.pin, on);
    }
}
