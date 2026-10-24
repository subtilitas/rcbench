/*
 * The pico-sdk calls firmware/iomcu/src/sense_i2c.c makes, for
 * test_sense_i2c: a modelled pair of pads and an I2C block that sends
 * nothing.  Each pin is low while a part holds it, high on an external
 * pull-up, and high on its own pad pull-up once that has been on for the
 * pin's rise time; low otherwise, as a pin nothing is wired to reads.  The
 * clock moves 1us at every tight_loop_contents() and busy wait.  Nothing
 * else of the SDK (software development kit) is stood in for here.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NUM_BANK0_GPIOS     48u
#define PICO_ERROR_GENERIC  (-1)
#define PICO_ERROR_TIMEOUT  (-2)

enum gpio_function { GPIO_FUNC_I2C = 3, GPIO_FUNC_SIO = 5 };
#define GPIO_IN  false
#define GPIO_OUT true

typedef uint64_t absolute_time_t;
typedef struct { int index; } i2c_inst_t;

/* The model, set and read by the suite. */
typedef struct {
    bool     ext_pullup;   /* a pull-up on the board or a module */
    bool     held_low;     /* a part drives it low */
    uint32_t pad_rise_us;  /* how long the pad's pull-up takes to raise it;
                              UINT32_MAX for never */
    bool     pad_up;       /* the pad's pull-up is on */
    bool     pad_down;     /* the pad's pull-down is on */
    uint64_t pad_up_at;    /* when it went on */
    bool     out;          /* driven: GPIO_OUT with 0 put */
    int      func;
} fake_pin_t;

extern fake_pin_t  fake_pins[NUM_BANK0_GPIOS];
extern uint64_t    fake_us;
extern unsigned    fake_wire;          /* transactions put on the wire */
extern int         fake_rc;            /* what a transaction returns; 0 for
                                          the bytes asked */
extern i2c_inst_t  fake_i2c[2];

#define I2C_INSTANCE(n) (&fake_i2c[(n)])

static inline uint64_t time_us_64(void) { return fake_us; }
static inline void tight_loop_contents(void) { ++fake_us; }
static inline void busy_wait_us_32(uint32_t us) { fake_us += us; }
static inline absolute_time_t make_timeout_time_us(uint64_t us)
{
    return fake_us + us;
}

static inline bool gpio_get(unsigned pin)
{
    const fake_pin_t *p = &fake_pins[pin];
    if (p->held_low || p->out) {
        return false;
    }
    if (p->ext_pullup) {
        return true;
    }
    return p->pad_up && p->pad_rise_us != UINT32_MAX
           && fake_us - p->pad_up_at >= p->pad_rise_us;
}

static inline void gpio_pull_up(unsigned pin)
{
    if (!fake_pins[pin].pad_up) {
        fake_pins[pin].pad_up_at = fake_us;
    }
    fake_pins[pin].pad_up   = true;
    fake_pins[pin].pad_down = false;
}

static inline void gpio_pull_down(unsigned pin)
{
    fake_pins[pin].pad_up   = false;
    fake_pins[pin].pad_down = true;
}

static inline void gpio_disable_pulls(unsigned pin)
{
    fake_pins[pin].pad_up   = false;
    fake_pins[pin].pad_down = false;
}

static inline void gpio_set_function(unsigned pin, enum gpio_function f)
{
    fake_pins[pin].func = (int)f;
}

/* Open drain by hand: out with 0 put drives the pin low. */
static inline void gpio_set_dir(unsigned pin, bool out)
{
    fake_pins[pin].out = out;
}

static inline void gpio_put(unsigned pin, bool value) { (void)pin; (void)value; }

static inline unsigned i2c_init(i2c_inst_t *i2c, unsigned hz)
{
    (void)i2c;
    return hz;
}

static inline void i2c_deinit(i2c_inst_t *i2c) { (void)i2c; }

static inline int fake_transfer(size_t len)
{
    ++fake_wire;
    return (fake_rc == 0) ? (int)len : fake_rc;
}

static inline int i2c_write_blocking_until(i2c_inst_t *i2c, uint8_t addr,
                                           const uint8_t *src, size_t len,
                                           bool nostop, absolute_time_t until)
{
    (void)i2c; (void)addr; (void)src; (void)nostop; (void)until;
    return fake_transfer(len);
}

static inline int i2c_read_blocking_until(i2c_inst_t *i2c, uint8_t addr,
                                          uint8_t *dst, size_t len,
                                          bool nostop, absolute_time_t until)
{
    (void)i2c; (void)addr; (void)nostop; (void)until;
    for (size_t i = 0; i < len; ++i) {
        dst[i] = 0u;
    }
    return fake_transfer(len);
}

static inline uint32_t save_and_disable_interrupts(void) { return 0u; }
static inline void restore_interrupts(uint32_t s) { (void)s; }
