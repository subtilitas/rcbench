/*
 * A bus of ROBOTIS Dynamixel servos on one RP2350 pin: a dxl_driver_t made
 * of one PIO (programmable input/output) state machine (dxl_line.pio) and
 * two DMA (direct memory access) channels, one for the bytes of a packet and
 * one for the bytes of the reply.
 *
 * Implements the published Protocol 2.0 of ROBOTIS Dynamixel servos.
 * ROBOTIS and Dynamixel are names or trademarks of their owner; they are
 * used here only to say which servos this module works with (README.md,
 * "Trademarks").
 *
 * Resources.  dxl_line_open() claims 1 state machine, 22 words of program
 * space in a PIO block that reaches the pin, and 2 DMA channels;
 * dxl_line_close() gives all of them back.  No interrupt and no PIO
 * interrupt flag is used.
 *
 * Rate.  8 state machine cycles a bit; the divider comes from
 * clock_get_hz(clk_sys) through dxl_pio_divider(), which refuses a rate
 * more than 2 % off.  At 150 MHz every rate of the Baud Rate register is
 * taken; 4,500,000 bit/s has a mean error of -0.03 % and an edge jitter of
 * 1 system clock, 3.0 % of a bit.  The system clock must not change while
 * the line is open.
 *
 * Pad.  From the open on, and between transactions, the pad is an input
 * with its pull-up on: the line idles high through the pull-up and the
 * servo.  A transaction drives the pad high, sends the packet push-pull,
 * and releases it to the input 9 cycles (1.1 bit times) into the last stop
 * bit.  The close leaves the pad an input with its pull-up on.  The
 * internal pull-up is 50 to 80 kOhm; a pull-down on the board pulls the
 * idle line low, which a servo reads as a break.
 *
 * Reply.  The bytes go by DMA into the line object, up to
 * DXL_LINE_RX_BYTES per transaction; poll() hands them over.  A byte with a
 * low stop bit is dropped by the program.
 *
 * Every function is called from one context.
 *
 * Not measured: no byte of this driver has been on a pin.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DXL_LINE_H
#define DXL_LINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dxl_bus.h"
#include "dxl_pio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Reply bytes one transaction keeps: two of the longest packets. */
#define DXL_LINE_RX_BYTES (2u * DXL_WIRE_MAX)

typedef enum {
    DXL_LINE_OK = 0,
    DXL_LINE_ERR_ARG,    /**< no line object, or a pin the chip does not have */
    DXL_LINE_ERR_RATE,   /**< the system clock gives no rate within 2 % */
    DXL_LINE_ERR_PIO,    /**< no state machine or no program space */
    DXL_LINE_ERR_DMA,    /**< fewer than 2 free DMA channels */
} dxl_line_err_t;

/** One line.  The caller keeps the object; the fields are the driver's. */
typedef struct {
    dxl_pio_div_t div;
    void    *pio;
    uint32_t sm;
    uint32_t offset;
    uint32_t baud;
    int8_t   dma_tx;
    int8_t   dma_rx;
    uint8_t  pin;
    uint8_t  open;
    uint8_t  busy;
    uint16_t n_read;     /**< reply bytes handed over */
    uint8_t  tx[DXL_PIO_TX_MAX + 1u];
    uint8_t  rx[DXL_LINE_RX_BYTES];
} dxl_line_t;

/** Mark @p line closed.  Call once, before the first open. */
void dxl_line_init(dxl_line_t *line);

/**
 * Claim the resources and take @p pin at @p baud: from the return on the
 * pad is an input with its pull-up.  The caller has stopped every other use
 * of the pin.  On an error nothing stays claimed and the pin is not
 * touched.  An open line is closed first.
 */
dxl_line_err_t dxl_line_open(dxl_line_t *line, unsigned pin, uint32_t baud);

/** Stop the state machine, give back what the open claimed and leave the
 *  pad an input with its pull-up on.  A transaction on the wire is cut.
 *  Safe on a closed line. */
void dxl_line_close(dxl_line_t *line);

bool dxl_line_is_open(const dxl_line_t *line);

/** The five functions of dxl_driver_t; @p ctx is the dxl_line_t. */
bool dxl_line_set_baud(void *ctx, uint32_t baud);
bool dxl_line_start(void *ctx, const uint8_t *tx, size_t n);
size_t dxl_line_poll(void *ctx, uint8_t *out, size_t cap);
void dxl_line_stop(void *ctx);
uint32_t dxl_line_now_us(void *ctx);

/** The driver for a bus on @p line. */
dxl_driver_t dxl_line_driver(dxl_line_t *line);

#ifdef __cplusplus
}
#endif

#endif /* DXL_LINE_H */
