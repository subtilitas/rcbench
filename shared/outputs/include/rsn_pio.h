/**
 * @file
 * @brief The remote servo node's PIO block placement (board 3): which of the
 *        RP2350's 3 PIO blocks a throttle output, a programming session's
 *        line and the fixed load may take.
 *
 * The rules, as tools/pio_budget.py holds them for board 3:
 *
 *   PIO0    the fixed load from boot: the PD mini UART, MIRROR and
 *           CAPTURE.  Nothing else goes there.
 *   PIO1,   the outputs on PIO (plain DShot, bidirectional DShot, PPM),
 *   PIO2    and the one programming session's line.  A line holds its
 *           block whole: no output goes into the line's block, and a line
 *           goes only into a block that holds no output.  The Hitec D
 *           precharge sampler takes a state machine of the other block.
 *   PPM     at most RSN_PIO_PPM_MAX slots: board 3 holds 3 DMA channels
 *           fixed (CAPTURE 2, the UART link's receive 1), a PPM slot takes
 *           2, a session 2 or 3, of 16.
 *
 * The drivers choose a block themselves: pico-sdk 2.3.0's
 * pio_claim_free_sm_and_add_program_for_gpio_range() and out_dshot.c's
 * claim_pair() search PIO2 down to PIO0 and take the first block with a
 * free state machine (a free adjacent pair for bidirectional DShot) and
 * room for the program.  Both skip a claimed state machine.  The placement
 * steers them without changing them: rsn_pio_begin() claims every free
 * state machine of the blocks the use may not take (the fence), the driver
 * binds, and rsn_pio_end() records where it went and lets the fence go.
 * An output then lands in PIO2 first and in PIO1 once PIO2 is full, so
 * PIO1 stays free for a session as long as PIO2 holds the outputs.
 *
 * A line goes into PIO1 when both blocks are free of outputs, else into
 * the free one.  A Hitec D line with its precharge sampler is two
 * programs in one open: its fence leaves both slot blocks open, and the
 * line's 30 words go into the block without outputs because no block that
 * holds an output (4 words at least) has 30 words free; the search takes
 * PIO2 first, so with both blocks free the line goes into PIO2 and the
 * sampler into PIO1.
 *
 * Every block runs at GPIO base 16 (the node's PIO pins are GPIO 16 to
 * 35); the firmware sets it at boot, before any program is loaded.  A
 * fenced block has every state machine claimed, so the SDK's second
 * search pass never moves its base.
 *
 * Pure logic over a small set of calls (rsn_pio_ops_t), host-tested in
 * test_rsn_pio against a model of the chip; firmware/iomcu/src/
 * iomcu_rsn_pio.c gives it the SDK's claim calls.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RSN_PIO_H
#define RCBENCH_RSN_PIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief PIO blocks of the RP2350. */
#define RSN_PIO_BLOCKS   3u
/** @brief State machines in a block. */
#define RSN_PIO_SMS      4u
/** @brief The block of the fixed load. */
#define RSN_PIO_FIXED_BLOCK 0u
/** @brief The block a line takes when both are free of outputs. */
#define RSN_PIO_LINE_BLOCK  1u
/** @brief The GPIOs a record is kept for: the RP2350B's 48. */
#define RSN_PIO_GPIOS    48u
/** @brief PPM slots board 3 takes: (16 - 3 fixed DMA channels) / 2. */
#define RSN_PIO_PPM_MAX  6u
/** @brief No block. */
#define RSN_PIO_NONE     0xFFu
/** @brief The GPIO base of every block on board 3. */
#define RSN_PIO_GPIO_BASE 16u

/** @brief What is placed. */
typedef enum {
    RSN_PIO_OUTPUT   = 0, /**< a plain or bidirectional DShot output, an
                               OUTPUTS slot or a DShot programming port */
    RSN_PIO_PPM      = 1, /**< a PPM slot: as an output, and counted      */
    RSN_PIO_LINE     = 2, /**< a programming session's line (KST, ESC
                               wire, Hitec without the sampler)           */
    RSN_PIO_LINE_PRE = 3, /**< a Hitec D line with its precharge sampler  */
    RSN_PIO_FIXED    = 4, /**< the PD mini UART, MIRROR, CAPTURE          */
    RSN_PIO_USES     = 5,
} rsn_pio_use_t;

/** @brief rsn_pio_begin()'s answer. */
typedef enum {
    RSN_PIO_OK       = 0, /**< the fence stands: bind, then rsn_pio_end() */
    RSN_PIO_NO_BLOCK = 1, /**< no block may take it: every slot block
                               holds an output (a line), or the line's
                               block leaves no other (an output)          */
    RSN_PIO_PPM_FULL = 2, /**< RSN_PIO_PPM_MAX PPM slots are bound        */
    RSN_PIO_BAD      = 3, /**< a NULL argument, an unknown use, a pin past
                               RSN_PIO_GPIOS, or a fence already standing */
} rsn_pio_err_t;

/** @brief The chip's claim calls, as pio_sm_is_claimed(), pio_sm_claim()
 *         and pio_sm_unclaim() on pio_get_instance(block). */
typedef struct {
    bool (*claimed)(void *ctx, unsigned block, unsigned sm); /**< claimed? */
    void (*claim)(void *ctx, unsigned block, unsigned sm);   /**< claim    */
    void (*unclaim)(void *ctx, unsigned block, unsigned sm); /**< let go   */
    void *ctx;                                    /**< passed to each call */
} rsn_pio_ops_t;

/** @brief The placement's records. */
typedef struct {
    uint8_t  block[RSN_PIO_GPIOS]; /**< the block of the output bound on
                                        each GPIO, RSN_PIO_NONE           */
    uint64_t ppm;                  /**< bit per GPIO: the output is PPM    */
    uint8_t  line;                 /**< the line's block, RSN_PIO_NONE     */
    /* The bind under way, between rsn_pio_begin() and rsn_pio_end(). */
    bool     open;                 /**< a fence stands                     */
    uint8_t  use;                  /**< its rsn_pio_use_t                  */
    uint8_t  pin;                  /**< its GPIO                           */
    uint8_t  allowed;              /**< bit per block it may take          */
    uint8_t  line_at;              /**< the block a line goes into         */
    uint8_t  before[RSN_PIO_BLOCKS]; /**< claimed state machines before    */
    uint8_t  fence[RSN_PIO_BLOCKS];  /**< state machines the fence claimed */
} rsn_pio_t;

/**
 * @brief Nothing placed, no line, no fence.
 *
 * @param p The records.
 */
void rsn_pio_init(rsn_pio_t *p);

/**
 * @brief The blocks a use may take now, and for a line the block it goes
 *        into.
 *
 * RSN_PIO_FIXED: PIO0.  RSN_PIO_OUTPUT and RSN_PIO_PPM: PIO1 and PIO2 less
 * the line's block; none for a PPM slot past RSN_PIO_PPM_MAX.
 * RSN_PIO_LINE: PIO1 when it holds no output, else PIO2 when it holds
 * none, else none.  RSN_PIO_LINE_PRE: PIO1 and PIO2 when one of them holds
 * no output (the line goes into PIO2 when it holds none, else into PIO1),
 * else none.  A line while a line is placed: none.
 *
 * @param p The records.
 * @param use The use, rsn_pio_use_t.
 * @param line_at Receives the block a line goes into, RSN_PIO_NONE for
 *                the other uses and when refused; may be NULL.
 * @return Bit b for block b; 0 when refused.
 */
uint8_t rsn_pio_blocks(const rsn_pio_t *p, rsn_pio_use_t use,
                       uint8_t *line_at);

/**
 * @brief Before a driver binds: the fence, every free state machine of the
 *        blocks the use may not take claimed.
 *
 * On anything but RSN_PIO_OK nothing is claimed and no fence stands.
 *
 * @param p The records.
 * @param ops The chip's claim calls.
 * @param use The use, rsn_pio_use_t.
 * @param pin The GPIO of an output, recorded by rsn_pio_end(); any value
 *            below RSN_PIO_GPIOS for the other uses.
 * @return RSN_PIO_OK, or why not.
 */
rsn_pio_err_t rsn_pio_begin(rsn_pio_t *p, const rsn_pio_ops_t *ops,
                            rsn_pio_use_t use, uint8_t pin);

/**
 * @brief After the driver's bind: the fence let go, and on @p ok the output
 *        or the line recorded.
 *
 * An output is recorded in the block whose claimed state machines grew;
 * a bind that claimed nothing (the driver kept a binding it already had)
 * keeps the record the pin has.  A line is recorded in the block
 * rsn_pio_blocks() named.  Nothing without a fence standing.
 *
 * @param p The records.
 * @param ops The chip's claim calls, as given to rsn_pio_begin().
 * @param ok Whether the driver bound.
 */
void rsn_pio_end(rsn_pio_t *p, const rsn_pio_ops_t *ops, bool ok);

/**
 * @brief An output released: its record cleared.
 *
 * @param p The records.
 * @param pin Its GPIO; any pin, bound or not.
 */
void rsn_pio_gone(rsn_pio_t *p, uint8_t pin);

/**
 * @brief The session's line closed: its block free for outputs again.
 *
 * @param p The records.
 */
void rsn_pio_line_gone(rsn_pio_t *p);

/**
 * @brief The blocks that hold an output.
 *
 * @param p The records.
 * @return Bit b for block b.
 */
uint8_t rsn_pio_output_blocks(const rsn_pio_t *p);

/**
 * @brief The PPM slots bound.
 *
 * @param p The records.
 * @return 0 to RSN_PIO_GPIOS.
 */
unsigned rsn_pio_ppm_count(const rsn_pio_t *p);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_RSN_PIO_H */
