/**
 * @file dxl_pio.h
 * @brief The arithmetic of a PIO pin driver for a ROBOTIS Dynamixel bus,
 *        without hardware access: the clock divider for a bit rate, its
 *        rate error and its jitter, and the words the program reads.
 *
 * Implements the published Protocol 2.0 of ROBOTIS Dynamixel servos.
 * ROBOTIS and Dynamixel are names or trademarks of their owner; they are
 * used here only to say which servos this module works with (README.md,
 * "Trademarks").
 *
 * rp2350/dxl_line.pio is a half-duplex UART, 8N1, on one pin, at
 * DXL_PIO_CYCLES_PER_BIT state machine cycles a bit.  The state machine's
 * clock divider is an integer and a fraction in 1/256 steps:
 *
 *   divider = sys_hz / (8 x rate),  rounded to the nearest 1/256
 *
 * The mean rate error follows from the rounding.  A divider with a
 * fraction spaces the state machine's cycles 1 system clock unevenly, so
 * an edge lies up to 1 system clock from its ideal place: the jitter, in
 * parts per million of a bit.  A rate whose mean error exceeds
 * DXL_PIO_ERROR_MAX_PPM, or whose divider falls outside 1 to 65536, is
 * refused.
 *
 * The transmit stream of one packet, as the program pulls it, one byte per
 * FIFO word, low byte used: the byte count less 1, then the bytes.
 *
 * The module includes nothing outside the C standard headers and holds no
 * state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DXL_PIO_H
#define DXL_PIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** State machine cycles per bit. */
#define DXL_PIO_CYCLES_PER_BIT 8u
/** A rate is refused when its mean error is larger: 2 %. */
#define DXL_PIO_ERROR_MAX_PPM  20000
/** Bit times of idle high the program drives before the first start bit. */
#define DXL_PIO_LEAD_BITS      20u
/** The longest packet the stream carries: its count is 1 byte. */
#define DXL_PIO_TX_MAX         256u

typedef struct {
    uint32_t div_int;     /**< 1 to 65536 */
    uint8_t  div_frac;    /**< 1/256 steps */
    int32_t  error_ppm;   /**< (actual - asked) / asked, parts per million */
    uint32_t jitter_ppm;  /**< 1 system clock as parts of a bit; 0 for an
                               integer divider */
    uint32_t actual;      /**< the rate the divider gives, bit/s, rounded */
} dxl_pio_div_t;

/** The divider for @p baud at @p sys_hz into @p out; false, with @p out
 *  still filled where it could be, when refused. */
bool dxl_pio_divider(uint32_t sys_hz, uint32_t baud, dxl_pio_div_t *out);

/** The stream of a packet of @p n bytes into @p out (n + 1 bytes); returns
 *  the stream's length, 0 for n of 0 or above DXL_PIO_TX_MAX or for a
 *  stream longer than @p cap. */
size_t dxl_pio_tx(const uint8_t *packet, size_t n, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* DXL_PIO_H */
