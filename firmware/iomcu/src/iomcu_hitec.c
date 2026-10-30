/**
 * @file
 * @brief The HITEC page and the programming port's hardware it drives; see
 * iomcu.h for the other files.
 *
 * Implements the signal-wire programming protocols of Hitec servos.  Hitec
 * is a name or trademark of its owner; it is used here only to say which
 * servos this module works with (README.md, "Legal notice and disclaimer").
 *
 * SPDX-License-Identifier: MIT
 */
#include "iomcu.h"

/*
 * The port's hardware.  hitec_port.c decides; these do what it says.  The
 * pin and the supply are the KST page's (kst_hw_pin(), kst_hw_supply(),
 * kst_hw_supply_set()): one programming port, one PD mini.
 */

/* The port's pin from the generator to the wire driver.  The port says it
 * holds the pin, so ports_apply() releases it and leaves it unbound; the
 * open leaves the pad a released input with the family's pull.  PRECHARGE
 * claims the sampler as well (hitec_line.h). */
bool hitec_hw_take(void *ctx, uint8_t port, uint8_t pin, uint8_t family,
                   uint8_t drive, uint8_t rx)
{
    (void)ctx;
    (void)port;
    ports_apply();
    return hitec_line_open_rx(&s_hitec_line, pin, family, drive, rx)
           == HITEC_LINE_OK;
}

/* And back: the port holds no pin, so ports_apply() binds it as the PROG
 * page has it. */
void hitec_hw_give(void *ctx)
{
    (void)ctx;
    hitec_line_close(&s_hitec_line);
    ports_apply();
}

void hitec_hw_abort(void *ctx)
{
    (void)ctx;
    hitec_line_abort(&s_hitec_line);
}

void hitec_hw_run(void *ctx, hitec_port_run_t *out)
{
    (void)ctx;
    out->lead_ns = s_hitec_line.run.lead_ns;
    out->span_us = s_hitec_line.run.span_us;
    out->bit_ns = hitec_line_bit_ns(&s_hitec_line);
}

void hitec_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    hitec_port_read(&s_hitec, off, n, out);
}

uint8_t hitec_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    const uint8_t nack = hitec_port_write(&s_hitec, off, n, in, s_now_ms);

    /* A command that takes the port is the KST page's to refuse from this
     * frame on, and the SUPPLY page is held from the frame of the OPEN to
     * the frame of the CLOSE.  A step the command started is a write a
     * SUPPLY OFF behind it in the same batch of frames is judged against. */
    ports_exclusive();
    ports_tell_supply();
    /* And its pulses end in this frame, as a KST command's do. */
    outputs_hw_ports_drive(ports_pulsing());
    sense_page_ports(&s_sense, ports_pulsing());
    return nack;
}
