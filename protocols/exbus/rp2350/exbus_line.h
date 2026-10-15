/*
 * An EX Bus line on one RP2350 pin: an exbus_driver_t made of one PIO
 * (programmable input/output) state machine (exbus_line.pio) and two DMA
 * (direct memory access) channels, one for the words of a transmission and
 * one for the bytes the line receives while it is released.
 *
 * Implements the EX Bus protocol of JETI receivers and devices.  JETI and EX
 * Bus are names or trademarks of their respective owner; they are used here
 * only to say which devices this module works with (README.md, "Legal
 * notice and disclaimer").
 *
 * Resources.  exbus_line_open() claims 1 state machine, 23 words of program
 * space in a PIO block that reaches the pin, and 2 DMA channels;
 * exbus_line_close() gives all of them back.  No interrupt and no PIO
 * interrupt flag is used.
 *
 * Clock.  The divider is computed from clock_get_hz(clk_sys) at the open,
 * 8 state machine cycles a bit, rounded to 1/256: the rate is at most
 * 0.2 % off.  The open is refused when the divider falls outside 1 to
 * 65535.  At 150 MHz the divider is 150 for 125 kbaud and 75 for 250 kbaud,
 * both exact.  The system clock must not change while the line is open.
 *
 * Pin.  While the line is open and no transaction runs, the pin drives
 * high, the idle level of the line.  In a transaction it drives the bytes,
 * becomes an input with its pull-up right after the last stop bit, and
 * drives high again when the window passes without a byte or the line has
 * been high for 20 bits after the last byte received (exbus_pio.h).  A
 * device that pauses 20 bits or longer inside a reply and then sends on
 * drives against the pin's high level; the reply is cut at the pause.  The close leaves the pin an
 * input with its pull-down on, the resting state of the programming port.
 * The internal pull-up is 50 to 80 kOhm; the specification's resistor of
 * 2.4 kOhm between transmit and receive belongs to a two-pin UART and has
 * no place on one pin.
 *
 * Every function is called from one context.  exbus_line_poll() ends a
 * transaction whose state machine still runs EXBUS_LINE_GRACE_US after the
 * longest it can take: the state machine is restarted with the pin driven
 * high and the capture is marked as overflowed.
 *
 * Not measured: no byte of this driver has been on a pin.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EXBUS_LINE_H
#define EXBUS_LINE_H

#include <stdbool.h>
#include <stdint.h>

#include "exbus_pio.h"
#include "exbus_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The driver ends a transaction whose state machine still runs this long
 *  after the longest it can take. */
#define EXBUS_LINE_GRACE_US 1000u

typedef enum {
    EXBUS_LINE_OK = 0,
    EXBUS_LINE_ERR_ARG,    /**< no line object, or a pin the chip does not
                                have */
    EXBUS_LINE_ERR_CLOCK,  /**< no divider from 1 to 65535 gives the rate
                                from the system clock */
    EXBUS_LINE_ERR_PIO,    /**< no state machine or no program space */
    EXBUS_LINE_ERR_DMA,    /**< fewer than 2 free DMA channels */
} exbus_line_err_t;

/** One line.  The caller keeps the object; the fields are the driver's. */
typedef struct {
    exbus_pio_clock_t clock;
    exbus_pio_run_t   run;
    void    *pio;
    uint32_t sm;
    uint32_t offset;
    uint32_t t_start_us;
    uint32_t limit_us;     /**< the longest the running transaction takes */
    int8_t   dma_tx;
    int8_t   dma_rx;
    uint8_t  pin;
    uint8_t  open;
    uint8_t  busy;
    uint32_t tx[EXBUS_PIO_TX_WORDS];
    uint32_t rx[EXBUS_PIO_RX_WORDS];
} exbus_line_t;

/** Mark @p line closed.  Call once, before the first open. */
void exbus_line_init(exbus_line_t *line);

/**
 * Claim the resources and take @p pin at @p baud: from the return on the
 * pin drives high.  The caller has stopped every other use of the pin.  On
 * an error nothing stays claimed and the pin is not touched.  An open line
 * is closed first.
 */
exbus_line_err_t exbus_line_open(exbus_line_t *line, unsigned pin,
                                 uint32_t baud);

/** Stop the state machine, give back what the open claimed and leave the
 *  pin an input with its pull-down on.  A transaction on the wire is cut.
 *  Safe on a closed line. */
void exbus_line_close(exbus_line_t *line);

bool exbus_line_is_open(const exbus_line_t *line);

/** A transaction runs: between a start() that returned true and the poll()
 *  that returns true, or an abort(). */
bool exbus_line_busy(const exbus_line_t *line);

/** The three functions of exbus_driver_t; @p ctx is the exbus_line_t. */
bool exbus_line_start(void *ctx, const uint8_t *tx, unsigned n,
                      uint32_t window_us);
bool exbus_line_poll(void *ctx, exbus_capture_t *out);
void exbus_line_abort(void *ctx);

/** The driver for a scheduler on @p line. */
exbus_driver_t exbus_line_driver(exbus_line_t *line);

#ifdef __cplusplus
}
#endif

#endif /* EXBUS_LINE_H */
