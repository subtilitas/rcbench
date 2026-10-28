/**
 * @file
 * @brief The I2C queue on one of the RP2350's two I2C blocks.
 *
 * See i2c_txq_block.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "i2c_txq_block.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"

/* The queue reads the block's abort causes by its own names. */
_Static_assert(I2C_TXQ_ABRT_ADDR_NOACK
               == I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS, "addr");
_Static_assert(I2C_TXQ_ABRT_DATA_NOACK
               == I2C_IC_TX_ABRT_SOURCE_ABRT_TXDATA_NOACK_BITS, "data");
_Static_assert(I2C_TXQ_ABRT_GCALL_NOACK
               == I2C_IC_TX_ABRT_SOURCE_ABRT_GCALL_NOACK_BITS, "gcall");
_Static_assert(I2C_TXQ_ABRT_ARB_LOST
               == I2C_IC_TX_ABRT_SOURCE_ARB_LOST_BITS, "arb");
_Static_assert(I2C_TXQ_ABRT_USER
               == I2C_IC_TX_ABRT_SOURCE_ABRT_USER_ABRT_BITS, "user");
/* A transaction fits the block's FIFOs whole (IC_TX_BUFFER_DEPTH and
 * IC_RX_BUFFER_DEPTH are 16, regs/i2c.h). */
_Static_assert(I2C_TXQ_FIFO == 16u, "the RP2350 FIFOs are 16 deep");

/** @brief No address in IC_TAR yet. */
#define TAR_NONE 0xFFu

/** @brief The bus on each block, for its interrupt. */
static i2c_txq_block_t *s_bus[2];

/**
 * @brief The block of a bus.
 *
 * @param b  Bus object.
 * @return Its i2c_inst_t.
 */
static i2c_inst_t *inst(const i2c_txq_block_t *b)
{
    return (i2c_inst_t *)b->hw;
}

/**
 * @brief The block reset and set up as a 7-bit fast-mode master with every
 *        interrupt masked.
 *
 * @param b  Bus object.
 * @return The bus clock the divider gives, in Hz.
 */
static uint32_t block_setup(i2c_txq_block_t *b)
{
    const uint hz = i2c_init(inst(b), b->q.cfg.hz);

    i2c_get_hw(inst(b))->intr_mask = 0;
    b->tar = TAR_NONE;
    b->abrt = 0;
    b->stopped = false;
    b->busy = false;
    return hz;
}

/**
 * @brief i2c_txq_ops_t's get.
 *
 * @param ctx   The bus.
 * @param line  I2C_TXQ_SDA or I2C_TXQ_SCL.
 * @return True when the pad reads high.
 */
static bool op_get(void *ctx, unsigned line)
{
    const i2c_txq_block_t *b = ctx;

    return gpio_get(line == I2C_TXQ_SDA ? b->sda : b->scl);
}

/**
 * @brief i2c_txq_ops_t's start: the address, the FIFO load, the interrupt
 *        unmasked.
 *
 * @param ctx  The bus.
 * @param x    The transaction.
 */
static void op_start(void *ctx, const i2c_txq_xfer_t *x)
{
    i2c_txq_block_t *b = ctx;
    i2c_hw_t *hw = i2c_get_hw(inst(b));
    const unsigned total = (unsigned)x->wr_len + x->rd_len;
    unsigned k = 0;

    const uint32_t irq = save_and_disable_interrupts();
    if (x->addr != b->tar) {
        hw->enable = 0;
        hw->tar = x->addr;
        hw->enable = 1;
        b->tar = x->addr;
    }
    (void)hw->clr_intr;
    b->abrt = 0;
    b->stopped = false;
    b->busy = true;
    for (unsigned i = 0; i < x->wr_len; ++i, ++k) {
        hw->data_cmd = (uint32_t)x->wr[i]
                       | (k + 1u == total ? I2C_IC_DATA_CMD_STOP_BITS : 0u);
    }
    for (unsigned i = 0; i < x->rd_len; ++i, ++k) {
        hw->data_cmd = I2C_IC_DATA_CMD_CMD_BITS
                       | (i == 0u && x->wr_len != 0u
                          ? I2C_IC_DATA_CMD_RESTART_BITS : 0u)
                       | (k + 1u == total ? I2C_IC_DATA_CMD_STOP_BITS : 0u);
    }
    hw->intr_mask = I2C_IC_INTR_MASK_M_STOP_DET_BITS
                    | I2C_IC_INTR_MASK_M_TX_ABRT_BITS;
    restore_interrupts(irq);
}

/**
 * @brief i2c_txq_ops_t's abort: the block's ABORT bit.
 *
 * @param ctx  The bus.
 */
static void op_abort(void *ctx)
{
    i2c_txq_block_t *b = ctx;

    hw_set_bits(&i2c_get_hw(inst(b))->enable, I2C_IC_ENABLE_ABORT_BITS);
}

/**
 * @brief i2c_txq_ops_t's reset.
 *
 * @param ctx  The bus.
 */
static void op_reset(void *ctx)
{
    (void)block_setup(ctx);
}

/**
 * @brief i2c_txq_ops_t's pullups.
 *
 * @param ctx  The bus.
 * @param on   True for the pads' pull-ups, false for no pull.
 */
static void op_pullups(void *ctx, bool on)
{
    const i2c_txq_block_t *b = ctx;

    if (on) {
        gpio_pull_up(b->sda);
        gpio_pull_up(b->scl);
    } else {
        gpio_disable_pulls(b->sda);
        gpio_disable_pulls(b->scl);
    }
}

/**
 * @brief i2c_txq_ops_t's gpio: the pins between the block and plain GPIO.
 *
 * @param ctx  The bus.
 * @param on   True for GPIO, released; false for the block.
 */
static void op_gpio(void *ctx, bool on)
{
    i2c_txq_block_t *b = ctx;

    if (on) {
        i2c_deinit(inst(b));
        b->busy = false;
        /* Driven, a line is driven low; released, it is an input. */
        gpio_put(b->sda, false);
        gpio_put(b->scl, false);
        gpio_set_dir(b->sda, GPIO_IN);
        gpio_set_dir(b->scl, GPIO_IN);
        gpio_set_function(b->sda, GPIO_FUNC_SIO);
        gpio_set_function(b->scl, GPIO_FUNC_SIO);
    } else {
        (void)block_setup(b);
        gpio_set_function(b->sda, GPIO_FUNC_I2C);
        gpio_set_function(b->scl, GPIO_FUNC_I2C);
    }
}

/**
 * @brief i2c_txq_ops_t's line: open drain by hand.
 *
 * @param ctx   The bus.
 * @param line  I2C_TXQ_SDA or I2C_TXQ_SCL.
 * @param high  True to release, false to drive low.
 */
static void op_line(void *ctx, unsigned line, bool high)
{
    const i2c_txq_block_t *b = ctx;

    gpio_set_dir(line == I2C_TXQ_SDA ? b->sda : b->scl,
                 high ? GPIO_IN : GPIO_OUT);
}

/** @brief The driver the queue runs on. */
static const i2c_txq_ops_t k_ops = {
    .get = op_get,
    .start = op_start,
    .abort = op_abort,
    .reset = op_reset,
    .pullups = op_pullups,
    .gpio = op_gpio,
    .line = op_line,
};

/**
 * @brief Take what the block has raised; on the end of the transaction
 *        report it to the queue and let the queue start the next.
 *
 * The end is the STOP: STOP_DET, and when an abort was asked for, its
 * TX_ABRT too (the block sets TX_ABRT after the STOP of an abort).
 *
 * @param b  Bus object.
 */
static void service(i2c_txq_block_t *b)
{
    i2c_hw_t *hw = i2c_get_hw(inst(b));

    if (!b->busy) {
        return;
    }
    const uint32_t raw = hw->raw_intr_stat;
    if ((raw & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) != 0u) {
        b->abrt |= hw->tx_abrt_source;
        (void)hw->clr_tx_abrt;
    }
    if ((raw & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS) != 0u) {
        (void)hw->clr_stop_det;
        b->stopped = true;
    }
    if (!b->stopped
        || ((hw->enable & I2C_IC_ENABLE_ABORT_BITS) != 0u
            && (b->abrt & I2C_TXQ_ABRT_USER) == 0u)) {
        return;
    }
    const uint64_t now = time_us_64();
    uint8_t rx[I2C_TXQ_RD_MAX] = {0};
    size_t n = 0;
    while (hw->rxflr != 0u) {
        const uint8_t v = (uint8_t)hw->data_cmd;
        if (n < sizeof(rx)) {
            rx[n] = v;
        }
        ++n;
    }
    hw->intr_mask = 0;
    b->busy = false;
    const uint32_t abrt = b->abrt;
    b->abrt = 0;
    b->stopped = false;
    /* More bytes than room is a short read to the queue: FAULT.  The queue
     * starts the next one itself when a callback asked for a poll; the poll
     * here starts it otherwise.  A callback that closed the bus leaves a
     * shut queue, which the poll does not touch. */
    i2c_txq_complete(&b->q, now, abrt, rx, n <= sizeof(rx) ? n : 0u);
    i2c_txq_poll(&b->q, now);
}

/** @brief I2C0's interrupt. */
static void irq_block0(void)
{
    if (s_bus[0] != NULL) {
        service(s_bus[0]);
    }
}

/** @brief I2C1's interrupt. */
static void irq_block1(void)
{
    if (s_bus[1] != NULL) {
        service(s_bus[1]);
    }
}

/**
 * @brief The handler of a block.
 *
 * @param block  0 or 1.
 * @return Its handler.
 */
static irq_handler_t handler(unsigned block)
{
    return block == 0u ? irq_block0 : irq_block1;
}

/**
 * @brief The block's interrupt masked on this core: the queue is the
 *        caller's until unmask().
 *
 * @param b  Bus object.
 */
static void mask(i2c_txq_block_t *b)
{
    if (b->masked++ == 0u) {
        irq_set_enabled(b->irq, false);
    }
}

/**
 * @brief The block's interrupt unmasked again by the outermost unmask(),
 *        while the bus is open.  A call from a done callback inside
 *        another driver call nests, and a bus closed inside it stays
 *        masked.
 *
 * @param b  Bus object.
 */
static void unmask(i2c_txq_block_t *b)
{
    if (--b->masked == 0u && b->open) {
        irq_set_enabled(b->irq, true);
    }
}

void i2c_txq_block_init(i2c_txq_block_t *b)
{
    if (b != NULL) {
        memset(b, 0, sizeof(*b));
    }
}

i2c_txq_block_err_t i2c_txq_block_open(i2c_txq_block_t *b, unsigned sda,
                                       unsigned scl, const i2c_txq_cfg_t *cfg)
{
    if (b == NULL) {
        return I2C_TXQ_BLOCK_ERR_ARG;
    }
    i2c_txq_block_close(b);
    if (sda >= NUM_BANK0_GPIOS || scl >= NUM_BANK0_GPIOS
        || (sda % 2u) != 0u || scl != sda + 1u) {
        return I2C_TXQ_BLOCK_ERR_PINS;
    }
    const unsigned block = (sda / 2u) % 2u;
    const unsigned irq = block == 0u ? I2C0_IRQ : I2C1_IRQ;
    if (s_bus[block] != NULL || irq_has_handler(irq)) {
        return I2C_TXQ_BLOCK_ERR_BUSY;
    }
    i2c_txq_block_t fresh;
    memset(&fresh, 0, sizeof(fresh));
    if (!i2c_txq_init(&fresh.q, &k_ops, b, cfg)) {
        return I2C_TXQ_BLOCK_ERR_ARG;
    }
    *b = fresh;
    b->masked = 1;            /* unmask() below enables the interrupt */
    b->hw = I2C_INSTANCE(block);
    b->irq = irq;
    b->sda = (uint8_t)sda;
    b->scl = (uint8_t)scl;
    b->block = (uint8_t)block;
    /* The block set up before the pins reach it. */
    b->q.cfg.hz = block_setup(b);
    gpio_disable_pulls(sda);
    gpio_disable_pulls(scl);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    s_bus[block] = b;
    b->open = true;
    irq_set_exclusive_handler(irq, handler(block));
    unmask(b);
    return I2C_TXQ_BLOCK_OK;
}

/**
 * @brief A pin back to a plain input with its pull-down, as at reset.
 *
 * @param pin  GPIO number.
 */
static void release(unsigned pin)
{
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_down(pin);
}

void i2c_txq_block_close(i2c_txq_block_t *b)
{
    if (b == NULL || !b->open) {
        return;
    }
    mask(b);
    /* Closed before the callbacks of the flush run: a submit from one is
     * refused, by the driver and by the shut queue. */
    b->open = false;
    i2c_txq_shut(&b->q, time_us_64());
    irq_remove_handler(b->irq, handler(b->block));
    i2c_deinit(inst(b));
    release(b->sda);
    release(b->scl);
    s_bus[b->block] = NULL;
    b->busy = false;
    unmask(b);
}

i2c_txq_err_t i2c_txq_block_submit(i2c_txq_block_t *b, i2c_txq_xfer_t *x)
{
    if (b == NULL) {
        return I2C_TXQ_E_ARG;
    }
    if (!b->open) {
        return I2C_TXQ_E_CLOSED;
    }
    mask(b);
    const i2c_txq_err_t e = i2c_txq_submit(&b->q, x);
    if (e == I2C_TXQ_E_OK) {
        i2c_txq_poll(&b->q, time_us_64());
    }
    unmask(b);
    return e;
}

bool i2c_txq_block_abort(i2c_txq_block_t *b, i2c_txq_xfer_t *x)
{
    if (b == NULL || !b->open) {
        return false;
    }
    mask(b);
    const bool ok = i2c_txq_abort(&b->q, x, time_us_64());
    unmask(b);
    return ok;
}

bool i2c_txq_block_recover(i2c_txq_block_t *b)
{
    if (b == NULL || !b->open) {
        return false;
    }
    mask(b);
    const bool ok = i2c_txq_recover(&b->q, time_us_64());
    unmask(b);
    return ok;
}

void i2c_txq_block_hold(i2c_txq_block_t *b, bool on)
{
    if (b == NULL || !b->open) {
        return;
    }
    mask(b);
    i2c_txq_hold(&b->q, on);
    if (!on) {
        i2c_txq_poll(&b->q, time_us_64());
    }
    unmask(b);
}

void i2c_txq_block_poll(i2c_txq_block_t *b)
{
    if (b == NULL || !b->open) {
        return;
    }
    mask(b);
    service(b);
    i2c_txq_poll(&b->q, time_us_64());
    unmask(b);
}

const i2c_txq_t *i2c_txq_block_queue(const i2c_txq_block_t *b)
{
    return b != NULL ? &b->q : NULL;
}
