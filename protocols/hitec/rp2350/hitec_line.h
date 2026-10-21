/**
 * @file
 * @brief The Hitec wire on one RP2350 pin: a hitec_driver_t made of one PIO
 *        (programmable input/output) state machine (hitec_line.pio) and two DMA
 *        (direct memory access) channels, one for the words of a transaction
 *        and one for the received bytes.
 *
 * A D series line opened to receive by PRECHARGE (hitec_pre.h) adds a second
 * state machine, the sampler, and a third DMA channel for its samples.
 *
 * Implements the signal-wire programming protocols of Hitec servos.  Hitec
 * is a name or trademark of its owner; it is used here only to say which
 * servos this module works with (README.md, "Legal notice and disclaimer").
 *
 * Resources.  hitec_line_open() claims 1 state machine, 29 words of program
 * space in a PIO block that reaches the pin, and 2 DMA channels;
 * hitec_line_close() gives all of them back.  Every open line loads its own
 * copy of the program, so a block of 32 words holds 1 line and the RP2350's
 * 3 blocks hold up to 3.  No interrupt and no PIO interrupt flag is used.
 * PRECHARGE claims 1 more state machine with the sampler's program, 7
 * words at 150 MHz (hitec_pre_program()), in a block with room for it, and
 * 1 more DMA channel, at the open; the close gives them back.
 *
 * Clock.  The divider is computed from clock_get_hz(clk_sys) at the open,
 * 16 state machine cycles a bit.  The open is refused when the divider would
 * be below 4 or the bit time more than 0.05 % off.  The system clock must
 * not change while the line is open.
 *
 * Pad.  Open and outside a transaction the pad is an input: classic with
 * its pull-down on, the D series with its pull-up on and the input
 * inverted.  The internal pulls are 50 to 80 kOhm; the D series needs a
 * pull-up on the board.  In a transaction the pad drives as the stream
 * says and is released at the end of the transmit, at the end of the
 * answer, at the end of the window, at an abort and at a restart.  The
 * close leaves the pad an input with the family's pull and the input
 * override off.
 *
 * PRECHARGE.  A SEND with a receive window goes out with no receiver
 * (hitec_pre_tx_xfer()).  The first poll() that finds the request ended
 * hands the pad to the sampler: the pin's control and pad registers are
 * kept, the pin goes to the sampler's PIO block with no input override,
 * 2 mA drive and slow slew, and a DMA channel writes the samples to the
 * line object.  Each poll() decodes the words written since the last one
 * (hitec_pre_step()).  At the decoder's end, at the last word, at an abort
 * and at the close the sampler stops with the pad released, and the
 * pin's two registers are written back as they were.  A LISTEN samples
 * from its start; one that answers is refused, and so is a window above
 * HITEC_PRE_WINDOW_MAX_US.  A transaction with no window and a LEVEL run as
 * in PULL-UP.  The bytes' times carry the sampler's start, read off the
 * microsecond timer: within 1 us.
 *
 * Every function is called from one context.  hitec_line_poll() ends a
 * transaction whose state machine still runs HITEC_LINE_GRACE_US after the
 * time the transaction can take: the state machine is restarted with the
 * pad released and the capture is marked as overflowed.
 *
 * Not measured: no byte of this driver has been on a pin.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HITEC_LINE_H
#define HITEC_LINE_H

#include <stdbool.h>
#include <stdint.h>

#include "hitec_pio.h"
#include "hitec_pre.h"
#include "hitec_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The driver ends a transaction whose state machine still runs this long
 *        after the time the transaction can take.
 */
#define HITEC_LINE_GRACE_US 1000u

typedef enum {
    HITEC_LINE_OK = 0,
    /**
     * @brief No line object, a pin the chip does not have, an unknown family or
     *        drive, or push-pull for classic.
     */
    HITEC_LINE_ERR_ARG,
    /**
     * @brief The system clock gives no divider (hitec_pio.h), or PRECHARGE no
     *        cycle counts (hitec_pre_timing()).
     */
    HITEC_LINE_ERR_CLOCK,
    /**
     * @brief No state machine or no program space; for PRECHARGE, none for the
     *        sampler.
     */
    HITEC_LINE_ERR_PIO,
    /** @brief Fewer than 2 free DMA channels, 3 for PRECHARGE. */
    HITEC_LINE_ERR_DMA,
} hitec_line_err_t;

/**
 * @brief One line.
 *
 * The caller keeps the object; the fields are the driver's.
 */
typedef struct {
    hitec_pio_clock_t clock;
    hitec_pio_run_t   run;
    void    *pio;
    uint32_t sm;
    uint32_t offset;
    uint32_t t_start_us;
    int8_t   dma_tx;
    int8_t   dma_rx;
    uint8_t  pin;
    uint8_t  family;
    uint8_t  drive;
    uint8_t  open;
    uint8_t  busy;
    uint8_t  aborted;
    uint8_t  kept;      /**< bytes received before an abort */
    uint8_t  cut;       /**< the abort came while the answer was due */
    uint32_t tx[HITEC_PIO_TX_WORDS];
    uint32_t rx[HITEC_RX_MAX];
    /* PRECHARGE: the sampler and its run. */
    uint8_t  rx_mode;   /**< hitec_rx_mode_t of the open line */
    uint8_t  pre_on;    /**< the running transaction is the sampler's */
    uint8_t  pre_held;  /**< the sampler has the pad */
    uint8_t  pre_stall; /**< the sampler stalled before its last word */
    int8_t   dma_pre;
    void    *pre_pio;
    uint32_t pre_sm;
    uint32_t pre_offset;
    uint32_t pre_len;
    uint32_t pre_t_us;      /**< the sampler's start */
    uint32_t pre_tx_us;     /**< the longest the request can take */
    uint32_t pre_span_us;   /**< the longest the sampling can take */
    uint32_t pre_ctrl;      /**< the pin's control register before */
    uint32_t pre_pad;       /**< and its pad register */
    uint16_t pre_prog[HITEC_PRE_PROGRAM_MAX];
    hitec_pre_timing_t pre_timing;
    hitec_pre_run_t    pre;
    uint32_t samples[HITEC_PRE_WORDS];
} hitec_line_t;

/**
 * @brief Mark @p line closed.
 *
 * Call once, before the first open.
 *
 * @param line  Line object to mark closed.
 */
void hitec_line_init(hitec_line_t *line);

/**
 * @brief Claim the resources and take @p pin for @p family (hitec_family_t)
 *        with @p drive (hitec_drive_t): from the return on the pad is a
 *        released input with the family's pull.
 *
 * The caller has stopped every other use of the pin.  On an error nothing stays
 * claimed and the pin is not touched.  An open line is closed first.
 *
 * @param line    Line object.
 * @param pin     GPIO number.
 * @param family  hitec_family_t value.
 * @param drive   hitec_drive_t value.
 * @return HITEC_LINE_OK, or the error that refused the open.
 */
hitec_line_err_t hitec_line_open(hitec_line_t *line, unsigned pin,
                                 uint8_t family, uint8_t drive);

/**
 * @brief hitec_line_open() with the receive mode @p rx (hitec_rx_mode_t).
 *
 * HITEC_RX_PULL_UP is hitec_line_open().  HITEC_RX_PRECHARGE takes the D series
 * only (HITEC_LINE_ERR_ARG for classic) and claims the sampler as well; on an
 * error nothing stays claimed and the pin is not touched.
 *
 * @param line    Line object.
 * @param pin     GPIO number.
 * @param family  hitec_family_t value.
 * @param drive   hitec_drive_t value.
 * @param rx      hitec_rx_mode_t value.
 * @return HITEC_LINE_OK, or the error that refused the open.
 */
hitec_line_err_t hitec_line_open_rx(hitec_line_t *line, unsigned pin,
                                    uint8_t family, uint8_t drive,
                                    uint8_t rx);

/**
 * @brief The receive mode of the open line; HITEC_RX_PULL_UP while closed.
 *
 * @param line  Line object.
 * @return The receive mode of the open line; HITEC_RX_PULL_UP while closed.
 */
uint8_t hitec_line_rx_mode(const hitec_line_t *line);

/**
 * @brief Stop the state machine, give back what the open claimed and leave the
 *        pad an input with the family's pull and no input override.
 *
 * A transaction on the wire is cut.  Safe on a closed line.
 *
 * @param line  Line object.
 */
void hitec_line_close(hitec_line_t *line);

/**
 * @brief Check whether a line is open.
 *
 * @param line  Line object.
 * @return true when the line is open.
 */
bool hitec_line_is_open(const hitec_line_t *line);

/**
 * @brief A transaction runs: between a start() that returned true and the
 *        poll() that returns true.
 *
 * @param line  Line object.
 * @return true between a start that returned true and the poll that returns
 *              true.
 */
bool hitec_line_busy(const hitec_line_t *line);

/**
 * @brief The bit time the system clock gives, in ns; 0 on a closed line.
 *
 * @param line  Line object.
 * @return The bit time in ns; 0 on a closed line.
 */
uint32_t hitec_line_bit_ns(const hitec_line_t *line);

/**
 * @brief Cut the running transaction: the pad is released at once, and the next
 *        poll() returns the bytes so far with the capture marked aborted.
 *
 * Does nothing when no transaction runs.
 *
 * @param line  Line object.
 */
void hitec_line_abort(hitec_line_t *line);

/**
 * @brief The three functions of hitec_driver_t; @p ctx is the hitec_line_t.
 *
 * @param ctx   The hitec_line_t.
 * @param xfer  Transaction to run.
 * @return true when the transaction started.
 */
bool hitec_line_start(void *ctx, const hitec_xfer_t *xfer);
/**
 * @brief Poll a transaction; the second of the three driver functions.
 *
 * @param ctx  The hitec_line_t.
 * @param out  Receives the capture when the transaction has ended.
 * @return true once, when the transaction has ended.
 */
bool hitec_line_poll(void *ctx, hitec_capture_t *out);
/**
 * @brief Read the microsecond clock; the third of the three driver functions.
 *
 * @param ctx  The hitec_line_t.
 * @return The microsecond clock.
 */
uint32_t hitec_line_now_us(void *ctx);

/**
 * @brief The driver for @p line.
 *
 * @param line  Line object.
 * @return The hitec_driver_t for the line.
 */
hitec_driver_t hitec_line_driver(hitec_line_t *line);

#ifdef __cplusplus
}
#endif

#endif /* HITEC_LINE_H */
