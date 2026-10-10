/*
 * The sensor bus on the RP2350's I2C block.  See sense_i2c.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_i2c.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "link_pages.h"

static i2c_inst_t *s_i2c;
static uint8_t     s_sda;
static uint8_t     s_scl;
static bool        s_open;

/* The block reset and set up again: a transaction that failed leaves it
 * part way through one, and restart_on_next as that one left it. */
static void block_init(void)
{
    i2c_init(s_i2c, SENSE_I2C_HZ);
}

static bool lines_high(void)
{
    return gpio_get(s_sda) && gpio_get(s_scl);
}

/* Both lines high within @p us: the pull-ups have the bus. */
static bool lines_rise(uint32_t us)
{
    const uint64_t until = time_us_64() + us;
    while (!lines_high()) {
        if (time_us_64() >= until) {
            return false;
        }
        tight_loop_contents();
    }
    return true;
}

bool sense_i2c_open(uint8_t sda, uint8_t scl)
{
    sense_i2c_close();
    if (sda >= NUM_BANK0_GPIOS || scl >= NUM_BANK0_GPIOS
        || !link_sn_pins_pair(sda, scl)) {
        return false;
    }
    s_i2c = I2C_INSTANCE((sda / 2u) % 2u);
    s_sda = sda;
    s_scl = scl;
    /* The block set up before the pins reach it, so they never carry
     * whatever a block left in reset would do. */
    block_init();
    gpio_disable_pulls(sda);
    gpio_disable_pulls(scl);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    s_open = true;
    return true;
}

static void release(uint8_t pin)
{
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_down(pin);
}

void sense_i2c_close(void)
{
    if (!s_open) {
        return;
    }
    i2c_deinit(s_i2c);
    release(s_sda);
    release(s_scl);
    s_open = false;
}

/* A pico-sdk result as the bus sees it: @p want bytes was success.  The
 * SDK reports a NACK of the address, an arbitration lost and any other
 * abort as one PICO_ERROR_GENERIC, so the lines tell them apart: back high
 * is a NACK on a working bus, held low is a part holding it. */
static sense_err_t classify(int rc, size_t want)
{
    if (rc == (int)want) {
        return SENSE_OK;
    }
    if (rc == PICO_ERROR_TIMEOUT) {
        return SENSE_TIMEOUT;
    }
    if (rc >= 0) {
        return SENSE_NACK;        /* a data byte not acknowledged */
    }
    return lines_rise(SENSE_I2C_IDLE_US) ? SENSE_NACK : SENSE_BUS_LOW;
}

/* The end of every transaction: a failed one leaves the block reset. */
static sense_err_t done(sense_err_t e, uint32_t irq)
{
    if (e != SENSE_OK) {
        block_init();
    }
    restore_interrupts(irq);
    return e;
}

sense_err_t sense_i2c_read(void *ctx, uint8_t addr, uint8_t reg,
                           uint8_t *buf, size_t n)
{
    (void)ctx;
    if (!s_open || n == 0u) {
        return SENSE_BAD_ARG;
    }
    const uint32_t irq = save_and_disable_interrupts();
    if (!lines_rise(SENSE_I2C_IDLE_US)) {
        return done(SENSE_BUS_LOW, irq);
    }
    const absolute_time_t until = make_timeout_time_us(SENSE_I2C_TIMEOUT_US);
    /* The pointer without a STOP, then the bytes after a repeated START. */
    sense_err_t e = classify(
        i2c_write_blocking_until(s_i2c, addr, &reg, 1u, true, until), 1u);
    if (e == SENSE_OK) {
        e = classify(i2c_read_blocking_until(s_i2c, addr, buf, n, false, until),
                     n);
    }
    return done(e, irq);
}

sense_err_t sense_i2c_write(void *ctx, uint8_t addr, uint8_t reg,
                            const uint8_t *buf, size_t n)
{
    (void)ctx;
    uint8_t frame[1u + 4u];
    if (!s_open || n == 0u || n > sizeof(frame) - 1u) {
        return SENSE_BAD_ARG;
    }
    frame[0] = reg;
    for (size_t i = 0; i < n; ++i) {
        frame[1u + i] = buf[i];
    }
    const uint32_t irq = save_and_disable_interrupts();
    if (!lines_rise(SENSE_I2C_IDLE_US)) {
        return done(SENSE_BUS_LOW, irq);
    }
    const absolute_time_t until = make_timeout_time_us(SENSE_I2C_TIMEOUT_US);
    const sense_err_t e = classify(
        i2c_write_blocking_until(s_i2c, addr, frame, n + 1u, false, until),
        n + 1u);
    return done(e, irq);
}

sense_err_t sense_i2c_ask(uint8_t addr)
{
    if (!s_open) {
        return SENSE_BAD_ARG;
    }
    const uint32_t irq = save_and_disable_interrupts();
    if (!lines_rise(SENSE_I2C_IDLE_US)) {
        return done(SENSE_BUS_LOW, irq);
    }
    uint8_t b;
    const absolute_time_t until = make_timeout_time_us(SENSE_I2C_TIMEOUT_US);
    const sense_err_t e = classify(
        i2c_read_blocking_until(s_i2c, addr, &b, 1u, false, until), 1u);
    return done(e, irq);
}

/* Open drain by hand: low is the pin driven 0, high is the pin let go to
 * its pull-up. */
static void line(uint8_t pin, bool high)
{
    gpio_set_dir(pin, high ? GPIO_IN : GPIO_OUT);
}

/* SCL let go, and given SENSE_I2C_CLEAR_STRETCH_US to rise: a part may
 * hold it low a while. */
static void scl_release(void)
{
    line(s_scl, true);
    const uint64_t until = time_us_64() + SENSE_I2C_CLEAR_STRETCH_US;
    while (!gpio_get(s_scl) && time_us_64() < until) {
        tight_loop_contents();
    }
    busy_wait_us_32(SENSE_I2C_CLEAR_HALF_US);
}

void sense_i2c_recover(void)
{
    if (!s_open) {
        return;
    }
    const uint32_t irq = save_and_disable_interrupts();
    i2c_deinit(s_i2c);
    /* Both lines as GPIO driving 0 when they drive at all, let go to begin
     * with. */
    gpio_put(s_sda, false);
    gpio_put(s_scl, false);
    line(s_sda, true);
    line(s_scl, true);
    gpio_set_function(s_sda, GPIO_FUNC_SIO);
    gpio_set_function(s_scl, GPIO_FUNC_SIO);
    busy_wait_us_32(SENSE_I2C_CLEAR_HALF_US);
    /* Nine clocks: a part part way through a byte finishes it and its
     * acknowledge bit, and lets SDA go. */
    for (unsigned k = 0; k < 9u; ++k) {
        line(s_scl, false);
        busy_wait_us_32(SENSE_I2C_CLEAR_HALF_US);
        scl_release();
    }
    /* A STOP: SDA low while SCL is low, SCL up, then SDA up. */
    line(s_scl, false);
    busy_wait_us_32(SENSE_I2C_CLEAR_HALF_US);
    line(s_sda, false);
    busy_wait_us_32(SENSE_I2C_CLEAR_HALF_US);
    scl_release();
    line(s_sda, true);
    busy_wait_us_32(SENSE_I2C_CLEAR_HALF_US);
    /* The block back on its pins. */
    block_init();
    gpio_set_function(s_sda, GPIO_FUNC_I2C);
    gpio_set_function(s_scl, GPIO_FUNC_I2C);
    restore_interrupts(irq);
}
