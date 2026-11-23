/**
 * @file
 * @brief The remote servo node's own parts (IOMCU_BOARD=rsn): the chip's
 *        stepping at boot, the POWER page, and the master rail switch and
 *        the 9 port switches it drives.
 *
 * The rules are host-tested: chip_gate.h for the stepping and what a chip
 * before A4 refuses, power_page.h for the page, its trips and the BEC
 * check.  This file reads the pins and the sensor snapshot into them and
 * drives what they decide.
 *
 *   RAIL_EN     GPIO 2, high while RAIL is 1 on an A4 chip.  The carrier
 *               ANDs it with the heartbeat's monostable and CRIT_N, so the
 *               rail is off whenever this core does not run, the heartbeat
 *               stops, or an INA3221 sees a conversion past its critical
 *               limit, whatever this file does.
 *   PORT_EN     GPIO 6 to 14, high for each port of PORT_ON on a carrier
 *               with the switches; low on one without them.
 *   SW_FITTED   GPIO 15, read once at boot with the pad's pull-up: low is
 *               a carrier with the port switches.
 *   CRIT_N      GPIO 36, read every pass with the pad's pull-up beside the
 *               carrier's 10kΩ: low is an error.
 *   LED         GPIO 39: 500ms on, 500ms off on an A4 chip; 100ms on,
 *               100ms off on a refused one.
 *
 * Programming on the common rail (prog_rail.h): a KST, Hitec or ESC wire
 * session's supply is the PD mini through its output's switch.  The
 * session's output is the one whose pin its programming port holds (the
 * ESC wire line's pin while it has one), or before that the pin the page
 * last asked for (kst_hw_pin(), esc_hw_pin()).  A channel is active while
 * an OUTPUTS slot or a programming port with a function is bound to its
 * output's pin.
 *
 * On the other boards (IOMCU_BOARD_RSN 0) each function does nothing: no
 * pin is touched, no fault is set and nothing is refused.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "iomcu.h"

#include "board_fix.h"
#include "chip_gate.h"
#include "iomcu_rsn_pio.h"
#include "power_page.h"
#include "prog_rail.h"
#include "rsn_pins.h"

#if IOMCU_BOARD_RSN

/** @brief The POWER page. */
static power_page_t s_power;
/** @brief The chip is of the A4 stepping (chip_gate_ok()). */
static bool s_chip_ok;
/** @brief STATUS's fault bit for the chip (chip_gate_fault()). */
static uint16_t s_chip_fault;
/** @brief The programming session on the rail. */
static prog_rail_t s_rail;
/** @brief The output of the pin a programming page last asked for. */
static int s_asked_k = PROG_RAIL_NO_PORT;

/** @brief LED half period on an A4 chip, ms. */
#define RSN_LED_OK_MS      500u
/** @brief LED half period on a refused chip, ms. */
#define RSN_LED_REFUSED_MS 100u

/**
 * @brief One pin an output, driven low.
 *
 * @param pin  The GPIO.
 */
static void out_low(unsigned pin)
{
    gpio_init(pin);
    gpio_put(pin, false);
    gpio_set_dir(pin, GPIO_OUT);
}

/**
 * @brief One pin an input with its pad's pull-up.
 *
 * @param pin  The GPIO.
 */
static void in_up(unsigned pin)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

void rsn_pins_off(void)
{
    out_low(RSN_PIN_RAIL_EN);
    for (unsigned k = 0; k < RSN_PORTS; ++k) {
        out_low(RSN_PIN_PORT_EN(k));
    }
    out_low(RSN_PIN_LED);
    in_up(RSN_PIN_SW_FITTED);
    in_up(RSN_PIN_CRIT_N);
}

void rsn_boot(iomcu_state_t *s)
{
    const uint8_t rev = rp2350_chip_version();
    const uint8_t rom = rp2350_rom_version();
    s_chip_ok    = chip_gate_ok(rev, rom);
    s_chip_fault = chip_gate_fault(rev, rom);
    s->identity[LINK_ID_CHIP_REV] = rev;
    s->identity[LINK_ID_ROM_VER]  = rom;
    /* Every PIO block at GPIO base 16, before any program is loaded. */
    place_boot();
    /* The strap's pull-up has had the whole start since rsn_pins_off(). */
    power_init(&s_power, !gpio_get(RSN_PIN_SW_FITTED), s_chip_ok);
    prog_rail_init(&s_rail);
    printf("rcbench-iomcu: RP2350 revision %u, boot ROM %u: %s; port "
           "switches %s\n", (unsigned)rev, (unsigned)rom,
           s_chip_ok ? "A4 or later" : "BEFORE A4, NOTHING IS DRIVEN",
           (s_power.fitted != 0u) ? "fitted" : "not fitted");
}

bool rsn_chip_refused(void)
{
    return !s_chip_ok;
}

void rsn_link(link_dev_t *d)
{
    link_dev_refuse_drive(d, !s_chip_ok);
}

uint16_t rsn_faults(void)
{
    return (uint16_t)(s_chip_fault | power_faults(&s_power));
}

/**
 * @brief The switches as the page decides: RAIL_EN only on an A4 chip,
 *        each PORT_EN only on a carrier with the switches.
 */
static void drive(void)
{
    gpio_put(RSN_PIN_RAIL_EN, s_chip_ok && power_rail_driven(&s_power));
    const uint16_t on = (uint16_t)(power_port_on(&s_power) & s_power.fitted);
    for (unsigned k = 0; k < RSN_PORTS; ++k) {
        gpio_put(RSN_PIN_PORT_EN(k), s_chip_ok && (on & (1u << k)) != 0u);
    }
}

void rsn_off(void)
{
    power_off(&s_power);
    prog_rail_off(&s_rail);
    drive();
}

/**
 * @brief The rail's voltage as the PD mini reads it, while its output is
 *        on and it answers.
 *
 * @return mV, or 0 when not known.
 */
static uint16_t rail_mv(void)
{
    return (s_supply.regs[LINK_SP_OUTPUT] != 0u)
               ? s_supply.regs[LINK_SP_V_MV] : 0u;
}

/**
 * @brief The ports whose output is a throttle: an OUTPUTS slot's or a
 *        programming port's (power_throttle()).
 *
 * @return Bit k for port k.
 */
static uint16_t throttles(void)
{
    return power_throttle(&s_outputs, &s_prog, outbind_roles(IOMCU_BOARD_ID));
}

uint16_t rsn_pass(uint32_t now)
{
    power_in_t in;
    memset(&in, 0, sizeof(in));
    in.crit_n_low = !gpio_get(RSN_PIN_CRIT_N);
    for (unsigned a = 0; a < POWER_PARTS; ++a) {
        in.me[a]   = s_sense_snap.i3221_flags[a];
        in.me_n[a] = s_sense_snap.i3221_flags_n[a];
    }
    in.me_ok = (uint8_t)(s_sense_snap.i3221_have_flags & 0x07u);
    for (unsigned k = 0; k < LINK_PW_PORTS; ++k) {
        in.over_n[k] = s_sense_snap.over_n[k];
    }
    uint16_t trip = power_step(&s_power, &in);
    if (s_sense_snap.have_win) {
        int32_t  mv[LINK_PW_PORTS];
        uint16_t have = 0u;
        for (unsigned k = 0; k < LINK_PW_PORTS; ++k) {
            const sense_window_t *w = &s_sense_snap.win[k];
            mv[k] = w->v_mean_uv / 1000;
            if (w->n_v > 0u) {
                have |= (uint16_t)(1u << k);
            }
        }
        /* The series: a set-up of the schedule numbers windows anew. */
        trip |= power_window(&s_power, s_sense_snap.setup,
                             s_sense_snap.win[0].number, mv, have, rail_mv());
    }
    if (trip != 0u) {
        prog_rail_off(&s_rail);     /* no session switches a trip back on */
    }
    /* A port on whose output became a throttle afterwards goes off. */
    (void)power_hold(&s_power, throttles());
    drive();
    const uint32_t half = s_chip_ok ? RSN_LED_OK_MS : RSN_LED_REFUSED_MS;
    gpio_put(RSN_PIN_LED, ((now / half) & 1u) != 0u);
    return trip;
}

/**
 * @brief The output a pin is, 0 to 8.
 *
 * @param pin The GPIO.
 * @return    The output, or PROG_RAIL_NO_PORT.
 */
static int out_of(unsigned pin)
{
    const int k = board_fix_out_index(outbind_roles(IOMCU_BOARD_ID), pin);
    return (k >= 0 && k < (int)LINK_PW_PORTS) ? k : PROG_RAIL_NO_PORT;
}

/**
 * @brief The ports whose channel is active: an OUTPUTS slot, or a
 *        programming port with a function, bound to the port's pin.
 *
 * @return Bit k for port k.
 */
static uint16_t active_ports(void)
{
    uint16_t m = 0u;
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        const int k = (s_outputs.slot[i].driver != OUT_DRIVER_NONE)
                          ? out_of(s_outputs.slot[i].pin) : PROG_RAIL_NO_PORT;
        m |= (k >= 0) ? (uint16_t)(1u << (unsigned)k) : 0u;
    }
    for (uint8_t q = 0u; q < prog_port_count(&s_prog); ++q) {
        const uint8_t pin = prog_port_pin(&s_prog, q);
        const int k = (pin != PROG_PORT_NO_PIN
                       && prog_port_func(&s_prog, q) != LINK_PG_FN_NONE)
                          ? out_of(pin) : PROG_RAIL_NO_PORT;
        m |= (k >= 0) ? (uint16_t)(1u << (unsigned)k) : 0u;
    }
    return m;
}

/**
 * @brief What the rail's rules are judged against now.
 *
 * @param c Takes it.
 */
static void rail_ctx(prog_rail_ctx_t *c)
{
    sense_sync();               /* the critical limits as core 1 has them */
    c->power.beat_alive  = safety_gate_supply_ok(&s_beat, &s_dev);
    c->power.limits_held = (s_sense_snap.i3221_crit & 0x07u) == 0x07u;
    c->power.throttle    = throttles();
    c->active            = active_ports();
}

/**
 * @brief The session's output: its programming port's pin while KST or
 *        HITEC holds one, the ESC wire line's pin while it has one, else
 *        the pin last asked for.
 *
 * @return The output, or PROG_RAIL_NO_PORT.
 */
static int session_port(void)
{
    const uint8_t q = (uint8_t)((kst_port_quiet_mask(&s_kst)
                                 | hitec_port_quiet_mask(&s_hitec))
                                & ((1u << IOMCU_PROG_PORTS) - 1u));
    for (uint8_t i = 0u; i < IOMCU_PROG_PORTS; ++i) {
        if ((q & (1u << i)) != 0u) {
            const uint8_t pin = prog_port_pin(&s_prog, i);
            return (pin != PROG_PORT_NO_PIN) ? out_of(pin)
                                             : PROG_RAIL_NO_PORT;
        }
    }
    const uint64_t esc = esc_port_pins(&s_esc);
    for (unsigned pin = 0u; pin < 64u; ++pin) {
        if ((esc & ((uint64_t)1u << pin)) != 0u) {
            return out_of(pin);
        }
    }
    return s_asked_k;
}

void rsn_prog_pin(uint8_t pin)
{
    s_asked_k = out_of(pin);
}

bool rsn_prog_view(supply_port_view_t *out)
{
    supply_port_view_t pd;
    supply_page_port_view(&s_supply, &pd);
    prog_rail_port(&s_rail, session_port());
    prog_rail_view(&s_rail, &s_power, &pd, out);
    return true;
}

int rsn_prog_set(bool on, uint16_t mv, uint16_t ma)
{
    if (on && s_supply_unsaved) {
        return 0;               /* as the SUPPLY page refuses one */
    }
    prog_rail_ctx_t c;
    rail_ctx(&c);
    prog_rail_port(&s_rail, session_port());
    const bool taken = prog_rail_set(&s_rail, &s_power, &s_supply, on, mv, ma,
                                     &c, s_now_ms);
    drive();
    return taken ? 1 : 0;
}

void rsn_prog_tell(void)
{
    const bool held = ports_hold_supply() || esc_port_supply_hold(&s_esc);
    const bool writing = ports_writing() || esc_port_writing(&s_esc);
    prog_rail_port(&s_rail, session_port());
    /* A pending ON is judged against the rules as they stand now. */
    prog_rail_ctx_t c;
    memset(&c, 0, sizeof(c));
    if (s_rail.pending) {
        rail_ctx(&c);
    }
    prog_rail_tell(&s_rail, &s_power, &s_supply, held, writing,
                   s_rail.pending ? &c : NULL, s_now_ms);
    drive();
}

uint8_t rsn_prog_refused(void)
{
    /* CONNECT's rules read the BEC check and the active ports only. */
    prog_rail_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.active = active_ports();
    int k_of[IOMCU_PROG_PORTS];
    for (uint8_t q = 0u; q < IOMCU_PROG_PORTS; ++q) {
        const uint8_t pin = prog_port_pin(&s_prog, q);
        k_of[q] = (pin != PROG_PORT_NO_PIN) ? out_of(pin) : PROG_RAIL_NO_PORT;
    }
    /* One programming session at a time on the node (PLAN 2.1, 2.6). */
    const uint8_t held = (uint8_t)(kst_port_quiet_mask(&s_kst)
                                   | hitec_port_quiet_mask(&s_hitec)
                                   | (esc_port_supply_hold(&s_esc) ? 0xFFu
                                                                   : 0u));
    return prog_rail_connect_mask(&s_power, k_of, IOMCU_PROG_PORTS, held, &c);
}

bool rsn_esc_refused(void)
{
    return s_power.bec != 0u;
}

bool rsn_esc_open_ok(uint8_t pin)
{
    prog_rail_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.active = active_ports();
    const uint8_t held = (uint8_t)(kst_port_quiet_mask(&s_kst)
                                   | hitec_port_quiet_mask(&s_hitec));
    return prog_rail_open_ok(&s_power, out_of(pin), held, &c);
}

void power_page_rd(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    power_read(&s_power, off, n, out);
    /* The session's output reads wanted, so the panel's write keeps it. */
    if (s_power.fitted != 0u && off <= LINK_PW_PORT_WANT
        && (unsigned)off + n > LINK_PW_PORT_WANT) {
        out[LINK_PW_PORT_WANT - off] |= prog_rail_claim(&s_rail);
    }
}

uint8_t power_page_wr(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    prog_rail_ctx_t c;
    rail_ctx(&c);
    const uint8_t nack = prog_rail_write(&s_rail, &s_power, off, n, in, &c,
                                         s_now_ms);
    drive();
    return nack;
}

#else /* the other boards: no node parts */

void rsn_pins_off(void) {}

void rsn_boot(iomcu_state_t *s) { (void)s; }

bool rsn_chip_refused(void) { return false; }

void rsn_link(link_dev_t *d) { (void)d; }

uint16_t rsn_faults(void) { return 0u; }

void rsn_off(void) {}

uint16_t rsn_pass(uint32_t now)
{
    (void)now;
    return 0u;
}

void rsn_prog_pin(uint8_t pin) { (void)pin; }

bool rsn_prog_view(supply_port_view_t *out)
{
    (void)out;
    return false;
}

int rsn_prog_set(bool on, uint16_t mv, uint16_t ma)
{
    (void)on;
    (void)mv;
    (void)ma;
    return -1;
}

void rsn_prog_tell(void) {}

uint8_t rsn_prog_refused(void) { return 0u; }

bool rsn_esc_refused(void) { return false; }

bool rsn_esc_open_ok(uint8_t pin)
{
    (void)pin;
    return true;
}

void power_page_rd(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    (void)off;
    for (unsigned i = 0; i < n; ++i) {
        out[i] = 0u;
    }
}

uint8_t power_page_wr(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    (void)off;
    (void)n;
    (void)in;
    return (uint8_t)LINK_NACK_BAD_PAGE;
}

#endif /* IOMCU_BOARD_RSN */
