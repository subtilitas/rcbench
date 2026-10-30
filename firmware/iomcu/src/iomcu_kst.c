/**
 * @file
 * @brief The KST page and the programming port's hardware it drives; see
 * iomcu.h for the other files.
 *
 * SPDX-License-Identifier: MIT
 */
#include "iomcu.h"

/* ----------------------------------------------------------------- KST */

/*
 * The port's hardware.  kst_port.c decides; these do what it says.
 *
 * The servo on a programming port is supplied by the PD mini: the SUPPLY
 * page's module is the one switched.
 */

/* The pin of a port the IOmcu holds; a port without a pin, one the
 * silicon refused, or a DShot port, whose lead goes to an ESC, has none
 * to hand over (prog_port_wire_pin()).  KST and HITEC both ask here. */
uint8_t kst_hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return prog_port_wire_pin(&s_prog, port, outputs_hw_ports_bound());
}

/* The port's pin from the generator to the wire driver.  The port says it
 * holds the pin, so ports_apply() releases it and leaves it unbound; the
 * open drives the pad low. */
bool kst_hw_take(void *ctx, uint8_t port, uint8_t pin)
{
    (void)ctx;
    (void)port;
    ports_apply();
    return kst_line_open(&s_kst_line, pin) == KST_LINE_OK;
}

/* And back: the port holds no pin, so ports_apply() binds it as the PROG
 * page has it. */
void kst_hw_give(void *ctx)
{
    (void)ctx;
    kst_line_close(&s_kst_line);
    ports_apply();
}

void kst_hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    /* On a board that switches the ports' own supply, that one. */
    if (prog_supply_fitted(&s_psup)) {
        prog_supply_port_view(&s_psup, out);
        return;
    }
    supply_page_port_view(&s_supply, out);
}

/* No ON while the supply's wiring is not in flash, as the SUPPLY page
 * refuses one: a restart in the run could not switch the module off. */
bool kst_hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    if (prog_supply_fitted(&s_psup)) {
        return prog_supply_port_set(&s_psup, on,
                                    safety_gate_supply_ok(&s_beat, &s_dev));
    }
    if (on && s_supply_unsaved) {
        return false;
    }
    return supply_page_port_set(&s_supply, on, mv, ma,
                                safety_gate_supply_ok(&s_beat, &s_dev));
}

void kst_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    kst_port_read(&s_kst, off, n, out);
}

uint8_t kst_write(void *ctx, uint8_t off, uint8_t n,
                         const uint16_t *in)
{
    (void)ctx;
    const uint8_t nack = kst_port_write(&s_kst, off, n, in, s_now_ms);

    /* A command that takes the port out of PWM holds the SUPPLY page from
     * this frame on: a SUPPLY write queued behind it is judged held.  And
     * the HITEC page refuses the port from this frame on.  Whether it
     * writes is told with it: a STOP ends a write in this frame. */
    ports_exclusive();
    ports_tell_supply();
    /* And its pulses end in this frame: a capture armed on the port behind
     * the command is judged against the port as it is. */
    outputs_hw_ports_drive(ports_pulsing());
    sense_page_ports(&s_sense, ports_pulsing());
    return nack;
}
