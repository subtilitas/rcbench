/*
 * S.BUS and S.BUS2 on one RP2350 pin: an sbus_driver_t made of one PIO
 * (programmable input/output) state machine (sbus_line.pio) and two DMA
 * (direct memory access) channels, one for the words of a frame and one for
 * the bytes of its listening window.
 *
 * Implements the S.BUS and S.BUS2 protocols of Futaba.  Futaba, S.BUS and
 * S.BUS2 are names or trademarks of their respective owners; they are used
 * here only to say which servos and sensors this module works with (NOTICE,
 * "Trademarks").
 *
 * Resources.  sbus_line_open() claims 1 state machine, 14 words of program
 * space in a PIO block that reaches the pin, and 2 DMA channels;
 * sbus_line_close() gives all of them back.  A second line on a second pin
 * claims its own and shares the program when its state machine is in the
 * same block.  No interrupt and no PIO interrupt flag is used.
 *
 * Clock.  The divider is computed from clock_get_hz(clk_sys) at the open: 8
 * state machine cycles per bit, 187.5 at 150 MHz.  The system clock must
 * not change while the line is open.
 *
 * Pad.  The GPIO (general-purpose input/output) output and input overrides
 * invert the pad, so the wire idles low and a start bit is high, with no
 * external inverter.  While the line is open and no window runs, the pad
 * drives low.  In a transaction it is push-pull for the frame; with a
 * window it becomes an input 2.5 us after the last stop bit ends and drives
 * low again when the window ends.  The pad's pull-down is on and its
 * pull-up off from the open on, and the close leaves the pad an input with
 * the pull-down on and both overrides off.
 *
 * Window end.  The program ends a window by its sample count; each byte
 * received makes the window 115 us longer than asked.  sbus_line_poll() cuts
 * a window that still runs at the time asked and drives the pad low.
 *
 * Every function is called from one context.
 *
 * Not measured: no frame of this driver has been on a pin.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SBUS_LINE_H
#define SBUS_LINE_H

#include <stdbool.h>
#include <stdint.h>

#include "sbus2.h"
#include "sbus_out.h"
#include "sbus_pio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A transaction without a window whose state machine still runs this long
 *  after its frame is ended by a restart. */
#define SBUS_LINE_GRACE_US 1000u

typedef enum {
    SBUS_LINE_OK = 0,
    SBUS_LINE_ERR_ARG,    /**< no line object, or a pin the chip does not have */
    SBUS_LINE_ERR_CLOCK,  /**< the system clock gives no divider for 100 kbaud */
    SBUS_LINE_ERR_PIO,    /**< no state machine or no program space */
    SBUS_LINE_ERR_DMA,    /**< fewer than 2 free DMA channels */
} sbus_line_err_t;

/** One line.  The caller keeps the object; the fields are the driver's. */
typedef struct {
    sbus_pio_clock_t clock;
    sbus_pio_run_t   run;
    void    *pio;
    uint32_t sm;
    uint32_t offset;
    uint32_t t_start_us;
    int8_t   dma_tx;
    int8_t   dma_rx;
    uint8_t  pin;
    uint8_t  open;
    uint8_t  busy;
    uint32_t tx[SBUS_PIO_TX_WORDS];
    uint32_t rx[SBUS_PIO_RX_WORDS];
} sbus_line_t;

/** Mark @p line closed.  Call once, before the first open. */
void sbus_line_init(sbus_line_t *line);

/**
 * Claim the resources and take @p pin: from the return on the pad drives
 * low.  The caller has stopped every other use of the pin.  On an error
 * nothing stays claimed and the pin is not touched.  An open line is closed
 * first.
 */
sbus_line_err_t sbus_line_open(sbus_line_t *line, unsigned pin);

/** Stop the state machine, give back what the open claimed and leave the
 *  pad an input with its pull-down on.  A frame on the wire is cut.  Safe
 *  on a closed line. */
void sbus_line_close(sbus_line_t *line);

bool sbus_line_is_open(const sbus_line_t *line);

/** A transaction runs: between a start() that returned true and the poll()
 *  that returns true. */
bool sbus_line_busy(const sbus_line_t *line);

/** The three functions of sbus_driver_t; @p ctx is the sbus_line_t. */
bool sbus_line_start(void *ctx, const uint8_t *frame, uint32_t window_us);
bool sbus_line_poll(void *ctx, sbus2_capture_t *out);
uint32_t sbus_line_now_us(void *ctx);

/** The driver for an sbus_out_t on @p line. */
sbus_driver_t sbus_line_driver(sbus_line_t *line);

#ifdef __cplusplus
}
#endif

#endif /* SBUS_LINE_H */
