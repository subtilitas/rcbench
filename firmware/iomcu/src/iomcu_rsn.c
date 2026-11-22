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
 * On the other boards (IOMCU_BOARD_RSN 0) each function does nothing: no
 * pin is touched, no fault is set and nothing is refused.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "iomcu.h"

#include "chip_gate.h"
#include "power_page.h"
#include "rsn_pins.h"

#if IOMCU_BOARD_RSN

/** @brief The POWER page. */
static power_page_t s_power;
/** @brief The chip is of the A4 stepping (chip_gate_ok()). */
static bool s_chip_ok;
/** @brief STATUS's fault bit for the chip (chip_gate_fault()). */
static uint16_t s_chip_fault;

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
    /* The strap's pull-up has had the whole start since rsn_pins_off(). */
    power_init(&s_power, !gpio_get(RSN_PIN_SW_FITTED), s_chip_ok);
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
    gpio_put(RSN_PIN_RAIL_EN, s_chip_ok && power_rail(&s_power));
    const uint16_t on = (uint16_t)(power_port_on(&s_power) & s_power.fitted);
    for (unsigned k = 0; k < RSN_PORTS; ++k) {
        gpio_put(RSN_PIN_PORT_EN(k), s_chip_ok && (on & (1u << k)) != 0u);
    }
}

void rsn_off(void)
{
    power_off(&s_power);
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
    drive();
    const uint32_t half = s_chip_ok ? RSN_LED_OK_MS : RSN_LED_REFUSED_MS;
    gpio_put(RSN_PIN_LED, ((now / half) & 1u) != 0u);
    return trip;
}

void power_page_rd(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    power_read(&s_power, off, n, out);
}

uint8_t power_page_wr(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    sense_sync();               /* the critical limits as core 1 has them */
    const power_ctx_t c = {
        .beat_alive  = safety_gate_supply_ok(&s_beat, &s_dev),
        .limits_held = (s_sense_snap.i3221_crit & 0x07u) == 0x07u,
        .throttle    = power_throttle(&s_outputs,
                                      outbind_roles(IOMCU_BOARD_ID)),
    };
    const uint8_t nack = power_write(&s_power, off, n, in, &c);
    if (nack == 0u) {
        drive();
    }
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
