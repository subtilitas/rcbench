/**
 * @file
 * @brief The ESC_WIRE and ESC_DATA pages and the pin's way to the line and back;
 * see iomcu.h for the other files.  esc_port.c decides; these do what it says.
 *
 * SPDX-License-Identifier: MIT
 */
#include "iomcu.h"

/**
 * @brief The pin of a throttle slot or a DShot port the IOmcu holds now.
 * @param ctx     Unused.
 * @param is_port True for a programming port, false for a slot.
 * @param index   Its number.
 * @return The pin, or ESC_PORT_NO_PIN.
 */
static uint8_t esc_hw_pin(void *ctx, bool is_port, uint8_t index)
{
    (void)ctx;
    if (is_port) {
        const uint8_t pin = prog_port_esc_pin(&s_prog, index,
                                              outputs_hw_ports_bound());

        return pin == (uint8_t)PROG_PORT_NO_PIN ? (uint8_t)ESC_PORT_NO_PIN : pin;
    }
    return esc_port_slot_pin(&s_outputs, index, bound_slots());
}

/**
 * @brief The pin from its output to the line.  The port says it holds the pin, so
 *        hw_apply_only() leaves a slot unbound and ports_apply() a port; the
 *        reservation keeps every page off it.  The open parks the pad.
 * @param ctx Unused.
 * @param pin The GPIO.
 * @return True when the line opened on it.
 */
static bool esc_hw_take(void *ctx, uint8_t pin)
{
    (void)ctx;
    reserve_held();
    hw_apply_only(bound_slots());
    return ew_line_open(&s_esc_line, pin) == EW_LINE_OK;
}

/**
 * @brief And back: only this pin moves.  ARM and the throttle are 0 first, so
 *        nothing renders on it before a new ask.
 * @param ctx Unused.
 * @return True when its output holds the pin again.
 */
static bool esc_hw_give(void *ctx)
{
    (void)ctx;
    const uint16_t target = s_esc.target;
    const uint8_t index = (uint8_t)(target & 0xFFu);
    bool back;

    ew_line_close(&s_esc_line);
    safety_gate_hold_ended(s_state.control);
    (void)outputs_set(&s_outputs, CH_THROTTLE, 0u, s_now_ms);
    (void)outputs_set_role_channels(&s_outputs, OUT_ROLE_THROTTLE,
                                    (uint8_t)(PROG_PORT_CHANNEL0
                                              + LINK_PG_PORTS_MAX),
                                    0u, s_now_ms);
    reserve_held();
    if ((target & LINK_EW_TARGET_PORT) != 0u) {
        ports_apply();
        back = (outputs_hw_ports_bound() & (1u << index)) != 0u;
    } else {
        uint16_t rate[OUT_MAX_SLOTS];

        outputs_slot_rates(&s_outputs, servo_page_slots_hz(&s_servo), rate);
        back = outputs_hw_return_slot(&s_outputs, rate, index);
        sense_page_bound(&s_sense, bound_slots());
        ports_apply();
    }
    return back;
}

/**
 * @brief What the PD mini says.  An ESC session never uses PROG_SUPPLY.
 * @param ctx Unused.
 * @param out Takes it.
 */
static void esc_hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    supply_page_port_view(&s_supply, out);
}

/**
 * @brief The PD mini on or off.  No ON while its wiring is not in flash, as the
 *        SUPPLY page refuses one: a restart in the run could not switch it off.
 * @param ctx Unused.
 * @param on  True for on.
 * @param mv  The voltage, mV.
 * @param ma  The limit, mA.
 * @return True when taken.
 */
static bool esc_hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    if (on && s_supply_unsaved) {
        return false;
    }
    return supply_page_port_set(&s_supply, on, mv, ma,
                                safety_gate_supply_ok(&s_beat, &s_dev));
}

/**
 * @brief An error: the other supply goes off with the PD mini.  An OFF is always
 *        taken.
 * @param ctx Unused.
 */
static void esc_hw_others_off(void *ctx)
{
    (void)ctx;
    (void)prog_supply_port_set(&s_psup, false,
                               safety_gate_supply_ok(&s_beat, &s_dev));
}

void esc_init(void)
{
    ew_line_init(&s_esc_line);
    const esc_port_hw_t hw = {
        .ctx        = NULL,
        .ports      = IOMCU_PROG_PORTS,
        .pin        = esc_hw_pin,
        .take       = esc_hw_take,
        .give       = esc_hw_give,
        .supply     = esc_hw_supply,
        .supply_set = esc_hw_supply_set,
        .others_off = esc_hw_others_off,
        .driver     = ew_line_driver(&s_esc_line),
    };

    (void)esc_port_init(&s_esc, &hw);
}

void esc_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    esc_port_read(&s_esc, off, n, out);
}

uint8_t esc_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;

    /* An ARM served earlier in this pass counts: the OPEN behind it in the same
     * batch of frames is refused. */
    esc_port_bench_armed(&s_esc, outputs_driving(&s_outputs)
                                     || s->control[LINK_CT_ARM] != 0u);
    ports_exclusive();
    const uint8_t nack = esc_port_write(&s_esc, off, n, in, s_now_ms);

    /* From this frame on KST and HITEC refuse, the SUPPLY page is held, and the
     * port's pulses end. */
    ports_exclusive();
    ports_tell_supply();
    outputs_hw_ports_drive(ports_pulsing());
    sense_page_ports(&s_sense, ports_pulsing());
    return nack;
}

void esc_data_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    esc_port_data_read(&s_esc, off, n, out);
}

uint8_t esc_data_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return esc_port_data_write(&s_esc, off, n, in);
}
