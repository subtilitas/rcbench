/*
 * The Hitec wire on one RP2350 pin: a hitec_driver_t made of one PIO
 * (programmable input/output) state machine (hitec_line.pio) and two DMA
 * (direct memory access) channels, one for the words of a transaction and
 * one for the received bytes.
 *
 * Implements the signal-wire programming protocols of Hitec servos.  Hitec
 * is a name or trademark of its owner; it is used here only to say which
 * servos this module works with (README.md, "Legal notice and disclaimer").
 *
 * Resources.  hitec_line_open() claims 1 state machine, 27 words of program
 * space in a PIO block that reaches the pin, and 2 DMA channels;
 * hitec_line_close() gives all of them back.  A second line shares the
 * program when its state machine is in the same block.  No interrupt and no
 * PIO interrupt flag is used.
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
#include "hitec_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The driver ends a transaction whose state machine still runs this long
 *  after the time the transaction can take. */
#define HITEC_LINE_GRACE_US 1000u

typedef enum {
    HITEC_LINE_OK = 0,
    /** No line object, a pin the chip does not have, an unknown family or
     *  drive, or push-pull for classic. */
    HITEC_LINE_ERR_ARG,
    HITEC_LINE_ERR_CLOCK,  /**< the system clock gives no divider (hitec_pio.h) */
    HITEC_LINE_ERR_PIO,    /**< no state machine or no program space */
    HITEC_LINE_ERR_DMA,    /**< fewer than 2 free DMA channels */
} hitec_line_err_t;

/** One line.  The caller keeps the object; the fields are the driver's. */
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
    uint32_t tx[HITEC_PIO_TX_WORDS];
    uint32_t rx[HITEC_RX_MAX];
} hitec_line_t;

/** Mark @p line closed.  Call once, before the first open. */
void hitec_line_init(hitec_line_t *line);

/**
 * Claim the resources and take @p pin for @p family (hitec_family_t) with
 * @p drive (hitec_drive_t): from the return on the pad is a released input
 * with the family's pull.  The caller has stopped every other use of the
 * pin.  On an error nothing stays claimed and the pin is not touched.  An
 * open line is closed first.
 */
hitec_line_err_t hitec_line_open(hitec_line_t *line, unsigned pin,
                                 uint8_t family, uint8_t drive);

/** Stop the state machine, give back what the open claimed and leave the
 *  pad an input with the family's pull and no input override.  A
 *  transaction on the wire is cut.  Safe on a closed line. */
void hitec_line_close(hitec_line_t *line);

bool hitec_line_is_open(const hitec_line_t *line);

/** A transaction runs: between a start() that returned true and the poll()
 *  that returns true. */
bool hitec_line_busy(const hitec_line_t *line);

/** The bit time the system clock gives, in ns; 0 on a closed line. */
uint32_t hitec_line_bit_ns(const hitec_line_t *line);

/** Cut the running transaction: the pad is released at once, and the next
 *  poll() returns the bytes so far with the capture marked aborted.  Does
 *  nothing when no transaction runs. */
void hitec_line_abort(hitec_line_t *line);

/** The three functions of hitec_driver_t; @p ctx is the hitec_line_t. */
bool hitec_line_start(void *ctx, const hitec_xfer_t *xfer);
bool hitec_line_poll(void *ctx, hitec_capture_t *out);
uint32_t hitec_line_now_us(void *ctx);

/** The driver for @p line. */
hitec_driver_t hitec_line_driver(hitec_line_t *line);

#ifdef __cplusplus
}
#endif

#endif /* HITEC_LINE_H */
