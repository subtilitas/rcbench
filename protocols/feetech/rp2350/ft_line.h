/*
 * The Feetech bus on one RP2350 pin: a feetech_driver_t made of one PIO
 * (programmable input/output) state machine (ft_line.pio) and two DMA
 * (direct memory access) channels, one for the words of a frame and one
 * for the bytes of its reply.
 *
 * Implements the serial bus protocol of Feetech SCS and STS servos.
 * Feetech is a name or trademark of its owner; it is used here only to say
 * which servos this module works with (README.md, "Trademarks").
 *
 * Resources.  ft_line_open() claims 1 state machine, 22 words of program
 * space in a PIO block that reaches the pin, and 2 DMA channels;
 * ft_line_close() gives all of them back.  No interrupt and no PIO
 * interrupt flag is used.
 *
 * Clock.  The divider is computed from clock_get_hz(clk_sys) at the open
 * and at every rate change; a rate the clock gives with more than 0.5 %
 * error is refused.  The system clock must not change while the line is
 * open.
 *
 * Pad.  Between transactions the pad is an input with its pull-up on and
 * its pull-down off: the line idles high.  In a transaction it drives high
 * 10 bit-eighths before the first start bit, drives the frame push-pull at
 * 3.3 V and is an input again at the end of the last stop bit.  The close
 * leaves the pad an input with the pull-down on.  The internal pull-up is
 * 50 to 80 kOhm; whether a servo holds the line high on its own is not
 * measured.
 *
 * A transaction ends when the bytes asked for came, or when the wait after
 * the frame has passed; the state machine is then restarted with the pad
 * an input.  Every function is called from one context.
 *
 * Not measured: no frame of this driver has been on a pin.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FT_LINE_H
#define FT_LINE_H

#include <stdbool.h>
#include <stdint.h>

#include "feetech_bus.h"
#include "feetech_pio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FT_LINE_OK = 0,
    FT_LINE_ERR_ARG,    /**< no line object, or a pin the chip does not have */
    FT_LINE_ERR_CLOCK,  /**< the system clock gives no divider for the rate */
    FT_LINE_ERR_PIO,    /**< no state machine or no program space */
    FT_LINE_ERR_DMA,    /**< fewer than 2 free DMA channels */
} ft_line_err_t;

/** One line.  The caller keeps the object; the fields are the driver's. */
typedef struct {
    feetech_pio_clock_t clock;
    void    *pio;
    uint32_t sm;
    uint32_t offset;
    uint32_t t_start_us;
    uint32_t total_us;    /**< the transaction ends by then           */
    int8_t   dma_tx;
    int8_t   dma_rx;
    uint8_t  pin;
    uint8_t  open;
    uint8_t  busy;
    uint8_t  n_rx;        /**< bytes the reply has                   */
    uint32_t tx[FEETECH_PIO_TX_WORDS];
    uint32_t rx[FEETECH_CAPTURE_MAX];
} ft_line_t;

/** Mark @p line closed.  Call once, before the first open. */
void ft_line_init(ft_line_t *line);

/**
 * Claim the resources and take @p pin at @p baud: from the return the pad
 * is an input with its pull-up on.  The caller has stopped every other use
 * of the pin.  On an error nothing stays claimed and the pin is not
 * touched.  An open line is closed first.
 */
ft_line_err_t ft_line_open(ft_line_t *line, unsigned pin, uint32_t baud);

/** Stop the state machine, give back what the open claimed and leave the
 *  pad an input with its pull-down on.  A frame on the wire is cut.  Safe
 *  on a closed line. */
void ft_line_close(ft_line_t *line);

bool ft_line_is_open(const ft_line_t *line);

/** The rate in force; 0 on a closed line. */
uint32_t ft_line_baud(const ft_line_t *line);

/** The three functions of feetech_driver_t; @p ctx is the ft_line_t. */
bool ft_line_start(void *ctx, const uint8_t *tx, unsigned n_tx, unsigned n_rx,
                   uint32_t wait_us);
bool ft_line_poll(void *ctx, feetech_capture_t *out);
bool ft_line_set_baud(void *ctx, uint32_t baud);

/** The driver for a session on @p line. */
feetech_driver_t ft_line_driver(ft_line_t *line);

#ifdef __cplusplus
}
#endif

#endif /* FT_LINE_H */
