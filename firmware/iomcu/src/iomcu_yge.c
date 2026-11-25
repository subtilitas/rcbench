/**
 * @file
 * @brief The YGE_PROG and YGE_DATA pages of programming for YGE ESCs and the
 * port's hardware they drive; see iomcu.h for the other files.  yge_port.c
 * decides; these do what it says.
 *
 * YGE is a name or trademark of its owner; it is used here only to say which
 * ESCs this module works with (README.md, "Legal notice and disclaimer").
 *
 * SPDX-License-Identifier: MIT
 */
#include "iomcu.h"
#include "iomcu_rsn_pio.h"

/**
 * @brief The port's pin from its generator to the line.  The page says it holds
 *        the pin, so ports_apply() releases it and leaves it unbound; the open
 *        parks the pad.
 * @param ctx  Unused.
 * @param port Unused.
 * @param pin  The GPIO.
 * @return True when the line opened on it.
 */
static bool yge_hw_take(void *ctx, uint8_t port, uint8_t pin)
{
    (void)ctx;
    (void)port;
    ports_apply();
    /* On board 3 into a block that holds no output (iomcu_rsn_pio.h). */
    if (!place_begin(RSN_PIO_LINE, pin)) {
        return false;
    }
    const bool ok = oy_line_open(&s_yge_line, pin) == OY_LINE_OK;
    place_end(ok);
    return ok;
}

/**
 * @brief And back: ARM and THROTTLE are 0 first, so nothing renders before a
 *        new ask; the page holds no pin, so ports_apply() binds the port as the
 *        PROG page has it.
 * @param ctx Unused.
 * @return True when the port holds its pin again.
 */
static bool yge_hw_give(void *ctx)
{
    (void)ctx;
    const uint8_t port = s_yge.port;

    oy_line_close(&s_yge_line);
    place_line_gone();
    safety_gate_hold_ended(s_state.control);
    ports_apply();
    return port < LINK_PG_PORTS_MAX
           && (outputs_hw_ports_bound() & (1u << port)) != 0u;
}

/**
 * @brief The line parked (input, pull-down) or listening (pull-up, receiver).
 * @param ctx    Unused.
 * @param listen True for listening.
 */
static void yge_hw_line(void *ctx, bool listen)
{
    (void)ctx;
    if (listen) {
        oy_line_listen(&s_yge_line);
    } else {
        oy_line_park(&s_yge_line);
    }
}

/**
 * @brief What the PD mini says, on the node through the output's switch
 *        (rsn_prog_view()).  The ESC's supply is the PD mini on every board;
 *        PROG_SUPPLY feeds the ports' leads, not an ESC's battery input.
 * @param ctx Unused.
 * @param out Takes it.
 */
static void yge_hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    if (!rsn_prog_view(out)) {
        supply_page_port_view(&s_supply, out);
    }
}

/**
 * @brief The PD mini on or off, on the node with the output's switch
 *        (rsn_prog_set()).  No ON while its wiring is not in flash, as the
 *        SUPPLY page refuses one: a restart in the run could not switch it off.
 * @param ctx Unused.
 * @param on  True for on.
 * @param mv  The voltage, mV.
 * @param ma  The limit, mA.
 * @return True when taken.
 */
static bool yge_hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    const int rail = rsn_prog_set(on, mv, ma);
    if (rail >= 0) {
        return rail != 0;
    }
    if (on && s_supply_unsaved) {
        return false;
    }
    return supply_page_port_set(&s_supply, on, mv, ma,
                                safety_gate_supply_ok(&s_beat, &s_dev));
}

/**
 * @brief An error or a stop: PROG_SUPPLY off with the PD mini.  An OFF is always
 *        taken.
 * @param ctx Unused.
 */
static void yge_hw_others_off(void *ctx)
{
    (void)ctx;
    (void)prog_supply_port_set(&s_psup, false,
                               safety_gate_supply_ok(&s_beat, &s_dev));
}

void yge_init(void)
{
    oy_line_init(&s_yge_line);
    const yge_port_hw_t hw = {
        .ctx        = NULL,
        .ports      = IOMCU_PROG_PORTS,
        .pin        = kst_hw_pin,
        .take       = yge_hw_take,
        .give       = yge_hw_give,
        .line       = yge_hw_line,
        .supply     = yge_hw_supply,
        .supply_set = yge_hw_supply_set,
        .others_off = yge_hw_others_off,
        .driver     = oy_line_driver(&s_yge_line),
    };

    (void)yge_port_init(&s_yge, &hw);
}

void yge_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    yge_port_read(&s_yge, off, n, out);
}

uint8_t yge_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;

    /* An ARM served earlier in this pass counts: the OPEN behind it in the same
     * batch of frames is refused. */
    yge_port_bench_armed(&s_yge, outputs_driving(&s_outputs)
                                     || s->control[LINK_CT_ARM] != 0u);
    ports_exclusive();
    const uint8_t nack = yge_port_write(&s_yge, off, n, in, s_now_ms);

    /* No ARM or THROTTLE across either edge of a hold, in this frame. */
    ports_hold_follow();
    /* From this frame on KST, HITEC and ESC_WIRE refuse, the SUPPLY page is
     * held, and the port's pulses end. */
    ports_exclusive();
    ports_tell_supply();
    outputs_hw_ports_drive(ports_pulsing());
    sense_page_ports(&s_sense, ports_pulsing());
    return nack;
}

void yge_data_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    yge_port_data_read(&s_yge, off, n, out);
}

uint8_t yge_data_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return yge_port_data_write(&s_yge, off, n, in);
}
