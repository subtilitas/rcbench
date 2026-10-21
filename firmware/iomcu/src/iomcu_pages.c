/*
 * The IOmcu's own pages and the output pages: IDENTITY, STATUS,
 * CONTROL, BENCH, CHANNELS, CHAN_CFG, OUTPUTS, SERVO and PROG, and the
 * glue that makes the silicon agree with the bank.  The rules are in
 * shared/outputs and shared/safety; see iomcu.h for the other files.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "iomcu.h"

#include "sense_limit.h"
#include "sense_map.h"

#include "out_store.h"
#include "outputs_pages.h"
#include "pd_uart.h"
#include "update_hw.h"

void identity_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->identity[off + i];
    }
}

void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->control[off + i];
    }
}

void status_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    /* As of the last pass rather than the last 50 Hz sample: the panel
     * writes an arm once the heartbeat bit reads clear, and a bit up to
     * 20 ms old would hold that arm back a poll. */
    status_gate(s);
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->status[off + i];
    }
}

/*
 * The numbers.  Read-only by construction: there is no write handler, so a
 * host that tries to set a measurement is refused with READ_ONLY rather than
 * quietly believed.
 */
void bench_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->bench[off + i];
    }
}

/*
 * The output pages: three pages, one bank.
 *
 * The rules (clamp, arm, slew, refuse an impossible range, refuse an unknown
 * driver) live in shared/outputs, so this end and the host end cannot hold
 * different opinions.  The IOmcu supplies the one thing only it knows,
 * whether it is safe to drive at all, by arming the bank rather than by
 * gating each write.
 *
 * A read returns the stored register array; a write validates and stores,
 * then re-derives the bank from the whole page so a partial write composes.
 */
/*
 * Keep what describes the outputs.
 *
 * The configuration is this board's, because the wires are this board's: a
 * panel that remembered a binding would reapply it to whatever is on the
 * bench now.  Nothing reaches flash here -- the request is taken later, by
 * the loop, once the bank has stopped driving.
 */
void save_outputs(const iomcu_state_t *s)
{
    /* Core 0's alone, and 288 bytes: off the stack. */
    static out_store_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.slots, s->slots, sizeof(cfg.slots));
    memcpy(cfg.chan_cfg, s->chan_cfg, sizeof(cfg.chan_cfg));
    /* And the supply's wiring, so a restart can still switch a module off. */
    memcpy(cfg.supply, &s_supply.regs[LINK_SP_ENABLE], sizeof(cfg.supply));
    /* And the sensor bus's set-up, so its pins stay held across one. */
    memcpy(cfg.sense, &s_sense.sense[LINK_SN_ENABLE], sizeof(cfg.sense));
    /* And the phase tap's. */
    memcpy(cfg.tone, s_tone.cfg, sizeof(cfg.tone));
    /* And where the programming ports are, and what each carries. */
    prog_port_kept(&s_prog, cfg.prog);
    cfg.prog_func = prog_port_kept_func(&s_prog);
    /* And the INA3221s' map, in force or derived. */
    sense_map_kept(&s_sense, cfg.sense_map, &cfg.sense_map_set);
    /* And the current limits. */
    sense_limit_kept(&s_sense, cfg.sense_limit);
    out_store_save(&cfg, s_now_ms);
}

void channels_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->channels[off + i];
    }
}

uint8_t channels_write(void *ctx, uint8_t off, uint8_t n,
                              const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    const uint8_t nack = outputs_channels_write(s->channels, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    /*
     * Only what this frame carried.  Applying the whole page would stamp
     * every channel's clock on a write that named one of them, and the
     * timeout that returns an uncommanded output to rest is per channel
     * exactly so that one screen's traffic cannot hold another's output up.
     */
    outputs_channels_apply_n(&s_outputs, s->channels, off, n, s_now_ms);
    return 0u;
}

/*
 * The tester's sweep pointed where the PROG page's TARGET says.  A change
 * returns the SERVO page's rate to 0 (servo_page_target()); true when that
 * moved the rate the slots run at, which then wants the slots bound again.
 */
static bool target_sync(void)
{
    const uint16_t slots_hz = servo_page_slots_hz(&s_servo);
    const uint8_t target = prog_port_target(&s_prog);
    if (target != 0u) {
        (void)servo_page_target(&s_servo, &s_outputs,
                                prog_port_channel((uint8_t)(target - 1u)),
                                1u);
    } else {
        (void)servo_page_target(&s_servo, &s_outputs, 0u, 0u);
    }
    prog_port_rate(&s_prog, servo_page_hz(&s_servo));
    return servo_page_slots_hz(&s_servo) != slots_hz;
}

/* The slots bound now, at the rates they run at now. */
static void slots_rerate(void)
{
    uint16_t rate[OUT_MAX_SLOTS];
    outputs_slot_rates(&s_outputs, servo_page_slots_hz(&s_servo), rate);
    outputs_hw_apply_only(&s_outputs, rate, bound_slots());
    sense_page_bound(&s_sense, bound_slots());
}

/*
 * The programming ports' pins bound as the PROG page holds them: the TARGET
 * port at the SERVO rate while it names one, every other port at
 * PROG_PORT_RATE_HZ (prog_port_port_hz()).  Never the pin the wire driver
 * holds: it is the driver's until the KST or HITEC port gives it back, and the page
 * keeps the port's pulse switch and target meanwhile.  The pins the wire
 * driver holds, as a mask of ports.
 */
static uint8_t ports_bind(void)
{
    uint8_t pin[LINK_PG_PORTS_MAX];
    uint16_t hz[LINK_PG_PORTS_MAX];
    const uint8_t wire = ports_wire();
    for (uint8_t i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        pin[i] = ((wire & (1u << i)) != 0u) ? (uint8_t)PROG_PORT_NO_PIN
                                            : prog_port_pin(&s_prog, i);
        /* A DShot port's rate is its bit rate, in kbit/s. */
        hz[i] = ((prog_port_dshot(&s_prog) & (1u << i)) != 0u)
                    ? prog_port_kbit(&s_prog, i)
                    : prog_port_port_hz(&s_prog, i);
    }
    outputs_hw_ports(pin, hz, prog_port_parked(&s_prog),
                     prog_port_dshot(&s_prog), (uint8_t)PROG_PORT_CHANNEL0);
    return wire;
}

/*
 * The programming ports' pins made to agree with the PROG page
 * (ports_bind()).  The tester's sweep is pointed where the page's TARGET
 * says before and after the binding, which can take the TARGET port's pin
 * away.
 */
void ports_apply(void)
{
    if (target_sync()) {
        slots_rerate();
    }
    const uint8_t wire = ports_bind();
    prog_port_bound(&s_prog, (uint8_t)(outputs_hw_ports_bound() | wire));
    if (target_sync()) {
        slots_rerate();
    }
    /* Each page told what the other holds, so neither takes a write that
     * leaves a pulsing port's endpoint longer than its frame or two rates
     * on one slice: the PROG page the rate, the SERVO page the ports that
     * pulse and the ports that share a slice. */
    servo_page_ports(&s_servo, prog_port_pwm_channels(&s_prog),
                     prog_port_sliced_channels(&s_prog));
    sense_page_ports(&s_sense, ports_pulsing());
}

/*
 * The silicon made to agree with the bank, every slot at the rate it runs
 * at: its own, or the SERVO page's for a PWM surface while the tester's
 * target is the standard servo ports.  Only the slots in @p may_bind are
 * bound; hw_apply() binds every one.  The programming ports follow, on what
 * the slots leave.
 */
void hw_apply_only(uint8_t may_bind)
{
    (void)target_sync();
    uint16_t rate[OUT_MAX_SLOTS];
    outputs_slot_rates(&s_outputs, servo_page_slots_hz(&s_servo), rate);
    outputs_hw_apply_only(&s_outputs, rate, may_bind);
    /* And the SERVO_SENSE page told which slots render frames, so a
     * capture never arms on one the silicon left unbound. */
    sense_page_bound(&s_sense, bound_slots());
    ports_apply();
}

void hw_apply(void)
{
    hw_apply_only(0xFFu);
}

/* Armed, or asked to arm by a frame served earlier in this pass. */
static bool bank_armed(const iomcu_state_t *s)
{
    return outputs_driving(&s_outputs) || s->control[LINK_CT_ARM] != 0u;
}

void chan_cfg_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->chan_cfg[off + i];
    }
}

/*
 * A whole CHAN_CFG page, judged and put in force: 0, or the reason with the
 * page in force unchanged.  A CHAN_CFG write is the page with one window
 * replaced and @p slots NULL; a BIND commit is the page prepared
 * (out_stage_commit()), with the OUTPUTS page prepared beside it as
 * @p slots, which saves once for both pages and passes @p save false.
 */
uint8_t chan_cfg_take(iomcu_state_t *s, const uint16_t *next,
                             const uint16_t *slots, bool save)
{
    /* Not a role, nor anything of a throttle channel, while the bench is
     * armed or has been asked to arm in this pass: the bank takes the ARM
     * register only after the link is served. */
    if (outputs_chan_cfg_armed_check(s->chan_cfg, next, bank_armed(s))
        != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Not a role change that would split a PWM slice under the SERVO
     * page's rate: refused whole, rather than taken with an output left
     * unbound. */
    if (outputs_chan_cfg_rate_check(&s_outputs, next,
                                    servo_page_slots_hz(&s_servo)) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Nor an endpoint longer than the frame of the PWM output that renders
     * it, judged with the slots a commit puts in force beside it. */
    if (outputs_chan_cfg_pulse_check(&s_outputs, next, slots,
                                     servo_page_slots_hz(&s_servo)) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    memcpy(s->chan_cfg, next, sizeof(s->chan_cfg));
    outputs_chan_cfg_apply(&s_outputs, s->chan_cfg);
    /* A channel that became a surface, or stopped being one, moves to or
     * from the SERVO page's rate. */
    hw_apply();
    if (save) {
        save_outputs(s);
    }
    return 0u;
}

uint8_t chan_cfg_write(void *ctx, uint8_t off, uint8_t n,
                              const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    uint16_t next[LINK_CC_COUNT];
    memcpy(next, s->chan_cfg, sizeof(next));
    const uint8_t nack = outputs_chan_cfg_write(next, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    return chan_cfg_take(s, next, NULL, true);
}

void slots_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->slots[off + i];
    }
}

/* ------------------------------------------------- the programming ports */

void prog_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    prog_port_read(&s_prog, off, n, out);
}

/*
 * The PROG page.  A pin is judged against the slots, the board's own pins
 * and the ones the supply, the sensor bus and the phase tap hold, then
 * reserved against all of them and bound, or parked for a NONE port.  A
 * pin its function cannot hold is refused with the pins and functions as
 * they were.  No pin and no function changes while the bank is armed or
 * asked to arm, or while the KST or HITEC page holds a port.
 *
 * The pins are bound, at the rates the write leaves the ports at, before
 * the SERVO page follows a TARGET change: a refused write leaves the SERVO
 * rate, the sweep and the slots' rates as they were.
 */
uint8_t prog_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    sense_sync();
    const prog_port_t was = s_prog;
    uint16_t kept_was[PROG_PORT_KEPT_COUNT + 1u];
    uint16_t kept_now[PROG_PORT_KEPT_COUNT + 1u];
    prog_port_kept(&s_prog, kept_was);
    kept_was[PROG_PORT_KEPT_COUNT] = prog_port_kept_func(&s_prog);
    const uint8_t nack = prog_port_write(
        &s_prog, off, n, in, &s_outputs,
        supply_page_pins(&s_supply) | sense_page_held(&s_sense)
            | tone_page_pins(&s_tone),
        bank_armed(s) || ports_quiet() != 0u, s_now_ms);
    if (nack != 0u) {
        return nack;
    }
    reserve_held();
    (void)ports_bind();
    /* Every port that holds a pin has it held by its function -- bound to
     * the generator, or parked -- or the write is undone: the pins, the
     * functions, the reservation and the silicon as before. */
    for (uint8_t i = 0; i < prog_port_count(&s_prog); ++i) {
        if (prog_port_pin(&s_prog, i) != PROG_PORT_NO_PIN
            && (outputs_hw_ports_bound() & (1u << i)) == 0u
            && (prog_port_pin(&was, i) != prog_port_pin(&s_prog, i)
                || prog_port_func(&was, i) != prog_port_func(&s_prog, i))) {
            prog_port_revert(&s_prog, &was, &s_outputs);
            reserve_held();
            ports_apply();
            return LINK_NACK_BAD_VALUE;
        }
    }
    ports_apply();
    prog_port_kept(&s_prog, kept_now);
    kept_now[PROG_PORT_KEPT_COUNT] = prog_port_kept_func(&s_prog);
    if (memcmp(kept_was, kept_now, sizeof(kept_was)) != 0) {
        save_outputs(&s_state);
    }
    return 0u;
}

void servo_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    servo_page_read(&s_servo, off, n, out);
}

/*
 * Not saved: a restart drives every slot at the binding's rate again, so a
 * servo plugged in after it never meets a rate meant for another, and runs
 * no sweep.
 */
uint8_t servo_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    (void)ctx;
    const uint8_t nack = servo_page_write(&s_servo, off, n, in, &s_outputs,
                                          s_now_ms);
    if (nack != 0u) {
        return nack;
    }
    if (off == (uint8_t)LINK_SV_FRAME_HZ) {
        hw_apply();
    }
    return 0u;
}

/*
 * A whole OUTPUTS page, judged and put in force: 0, or the reason with the
 * page, the bank and the silicon as they were.  The page with one window
 * replaced for an OUTPUTS write, the page prepared for a BIND commit, as
 * chan_cfg_take().
 */
uint8_t slots_take(iomcu_state_t *s, const uint16_t *next, bool save)
{
    /* The reservation as of core 1's last tick: a bus that moved gives its
     * old pins back once core 1 has let them go. */
    sense_sync();
    /* No slot changes under an armed bank, and the binding in force,
     * written again, is taken without binding anything anew. */
    if (bank_armed(s)) {
        return outputs_slots_armed_check(s->slots, next, true);
    }
    /* Nor a slot on a programming port's pin, or a PWM slot beside one in
     * the silicon: a port is no standard servo port and lends no pin. */
    if (prog_port_slots_check(&s_prog, next) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Nor one on a pin the supply holds: the bank would leave it unbound,
     * and a restart would drive it on the module's pin. */
    if (supply_page_slots_check(&s_supply, next) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Nor on one the sensor bus holds, for the same reason: its set-up's
     * pins, and the ones core 1 has not let go of yet. */
    if (sense_page_slots_check(&s_sense, next) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Nor on the phase tap's pin while the tap runs. */
    if (tone_page_slots_check(&s_tone, next) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Nor a slot bound beside a surface at another rate. */
    if (outputs_slots_rate_check(&s_outputs, next,
                                 servo_page_slots_hz(&s_servo)) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Nor a PWM slot at a rate whose frame is shorter than its channel's
     * endpoint. */
    if (outputs_slots_pulse_check(&s_outputs, next,
                                  servo_page_slots_hz(&s_servo)) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    uint16_t prev[LINK_OS_COUNT];
    memcpy(prev, s->slots, sizeof(prev));
    const uint8_t bound_before = bound_slots();
    memcpy(s->slots, next, sizeof(s->slots));
    outputs_slots_apply(&s_outputs, s->slots);
    /* The bank has decided what the slots are; this makes the silicon agree
     * with it before the next pass renders anything. */
    hw_apply();
    /*
     * A slot the bank took that the silicon could not bind is refused
     * rather than kept unbound, whatever holds what it needs: a PIO state
     * machine, instruction memory or DMA channel of the phase tap or the
     * supply's UART, a PWM slice, a pin.  The slots this write changed and
     * the ones it unbound are judged; one an earlier write or a restart left
     * unbound does not refuse a write that did not touch it.  A restart
     * binds the outputs first, so a binding kept here that the silicon
     * refused would take the resource from the tap or the supply there.
     * outputs_bind_check() in shared/ decides; this is the glue.
     */
    uint8_t watch = (uint8_t)(outputs_slots_changed(prev, next)
                              | bound_before);
    if (s_pd_open) {
        watch = 0xFFu;   /* the UART holds two state machines: every slot */
    }
    bool refused = outputs_bind_check(&s_outputs, watch, bound_slots()) != 0u;
    if (!refused && !s_pd_open && s_supply_attach) {
        /* The supply's UART waits for its save: the slots must leave it a
         * PIO block, tried now, or the attach would fail unseen. */
        refused = !pd_uart_open(supply_page_tx(&s_supply),
                                supply_page_rx(&s_supply),
                                supply_page_uart_baud(&s_supply));
        pd_uart_close();
    }
    if (refused) {
        /* The bank as it was, and the silicon bound exactly as before: a
         * slot unbound then, retried by the trial, may not keep what a
         * slot bound then needs back. */
        memcpy(s->slots, prev, sizeof(prev));
        outputs_slots_apply(&s_outputs, s->slots);
        hw_apply_only(bound_before);
        return LINK_NACK_BAD_VALUE;
    }
    if (save) {
        save_outputs(s);
    }
    return 0u;
}

uint8_t slots_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    uint16_t next[LINK_OS_COUNT];
    memcpy(next, s->slots, sizeof(next));
    const uint8_t nack = outputs_slots_write(next, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    return slots_take(s, next, true);
}

uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    /*
     * No ARM while the IOmcu's own firmware is being written or waits
     * for its BUY: the flash windows of the update stop this core for 19 ms
     * at a time.  Refused before the page's rules run, so nothing of the
     * frame is stored; a disarm is always taken.
     */
    if (update_hw_refuses_arm(off, n, in)) {
        return LINK_NACK_NOT_ARMED;
    }
    /*
     * The page's rules are link_control_write()'s and whether the bench may
     * arm is safety_gate_control_write()'s, both host-tested: every
     * register of the frame is validated before any is stored, so a refused
     * write leaves the page as it was and lifts no latch, and a CLEAR
     * releases the failsafe and the arm latch.  The decision is the
     * IOmcu's, not the panel's.
     */
    const uint8_t nack = safety_gate_control_write(s->control, off, n, in,
                                                   &s_beat, &s_dev);
    if (nack != 0u) {
        return nack;
    }
    /*
     * The throttle rides the control page rather than the CHANNELS page, so
     * a motor command keeps control priority on the wire; underneath it is
     * the same bank, so it is set here the same way a channel is.
     */
    const uint16_t thr = (uint16_t)(((uint32_t)s->control[LINK_CT_THROTTLE]
                                     * OUT_SPAN) / LINK_THROTTLE_MAX);
    (void)outputs_set(&s_outputs, CH_THROTTLE, thr, s_now_ms);
    /*
     * And the same command to the pins bound as motors.  CH_THROTTLE is off
     * the page and nothing renders it, so on its own it drives no pin: the
     * throttle screen would command a channel no slot reads while the ESC on
     * GP0 sat at its rest for ever.  Which channels are motors is the
     * binding's answer, carried as the role on the CHAN_CFG page, so a servo
     * bound beside a motor is left alone.  A programming port whose FUNC is
     * DShot is a motor too: its channel, past the page's, has the role.
     */
    (void)outputs_set_role_channels(&s_outputs, OUT_ROLE_THROTTLE,
                                    (uint8_t)(PROG_PORT_CHANNEL0
                                              + LINK_PG_PORTS_MAX),
                                    thr, s_now_ms);
    return 0;
}
