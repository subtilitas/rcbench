/**
 * @file oy_line.h
 * @brief The telemetry wire of programming for YGE ESCs (electronic speed
 *        controllers) on one RP2350 pin: an oy_driver_t made of one PIO
 *        (programmable input/output) state machine (oy_line.pio) and two DMA
 *        (direct memory access) channels.
 *
 * The program sends the bytes oy_line_tx() is given and nothing else; the
 * session passes every frame through oy_guard_frame() first.
 *
 * Resources.  oy_line_open() claims 1 state machine, 25 words of program space
 * in a PIO block that reaches the pin, and 2 DMA channels; oy_line_close()
 * gives all of them back.  No interrupt and no PIO interrupt flag is used.  An
 * open refused leaves nothing claimed and the pin untouched.  The object is
 * 760B: 512B of received entries and 180B of stream.
 *
 * Clock.  The divider comes from clock_get_hz(clk_sys) at the open, 16 state
 * machine cycles a bit: 81 + 97/256 at 150MHz, 8680ns a bit.  The open is
 * refused when the divider is below 4 or the bit time more than 0.05% off.
 * The system clock must not change while the line is open.
 *
 * Pad.  4mA drive and slow slew from the open to the close; the close puts the
 * drive and slew back as it found them.  The line states of programming for
 * YGE ESCs:
 *
 * | State | Pad | Input buffer | State machine |
 * |---|---|---|---|
 * | oy_line_open(), oy_line_park(): before the supply reads on, a fault, STOP, link loss | input, pull-down | off | stopped |
 * | oy_line_listen(): the receiver runs | input, pull-up | on | running |
 * | a frame from oy_line_tx() | driven, both levels | on | sending |
 * | the pad released 16 cycles after the last stop bit's edge | input, pull-up | on | running |
 * | oy_line_close() | input, pull-down, SIO function | on | given back |
 * | a restart of the chip | the chip's reset state | | |
 *
 * The input buffer is off while parked since erratum RP2350-E9 holds an A2
 * chip's input pad at around 2.2V, leaking typically 120uA, against its
 * pull-down while the buffer is on; the buffer off removes the leak.  The
 * pull-up is not affected.  The pad's pull-up is 50 to 80kOhm; whether an ESC
 * drives its replies push-pull or needs a pull-up is not known (docs/OpenYGE.md
 * section 8, U1).
 *
 * oy_line_park() stops the machine before it touches the pad, so a frame being
 * sent is cut within one call and never driven over the pull-down.  The stream
 * still in the FIFO and the channel is dropped.
 *
 * A frame goes out only while the line is high between bytes: a stream handed
 * over while a byte comes in waits for that byte's stop bit.  A stream the
 * machine has not sent oy_pio_span_us() + OY_PIO_GRACE_US after its hand-over
 * (a line held low all that time) is dropped by the next oy_line_tx(), with
 * the words still in the receive FIFO, and counted in @c tx_dropped.
 *
 * Receive.  The channel writes up to OY_PIO_RX_ENTRIES entries, 256 bytes.
 * oy_line_rx() hands them over and starts the channel again at entry 0 once
 * every entry is handed over.  A buffer that filled before that loses the
 * bytes after its last entry: they are dropped, counted once per fill in
 * @c rx.full.  The session takes up to 256 bytes a step, 22ms of wire at
 * 115200 baud.
 *
 * Time of a call: not measured; H1 (docs/OpenYGE.md section 9.2) records it.
 * None of them waits for the wire.
 *
 * Every function is called from one context.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef OY_LINE_H
#define OY_LINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openyge_pio.h"
#include "openyge_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Why an open is refused. */
typedef enum {
    OY_LINE_OK = 0,
    OY_LINE_ERR_ARG,   /**< no line object, or a pin the chip does not have */
    OY_LINE_ERR_CLOCK, /**< the system clock gives no divider               */
    OY_LINE_ERR_PIO,   /**< no state machine, or no program space           */
    OY_LINE_ERR_DMA,   /**< fewer than 2 free DMA channels                  */
} oy_line_err_t;

/**
 * @brief One line.
 *
 * The caller keeps the object; the fields are the driver's.  @c rx, @c sent
 * and @c tx_dropped may be read for a report.
 */
typedef struct {
    oy_pio_clock_t clock;       /**< the divider                          */
    oy_pio_rx_t    rx;          /**< bytes, framing errors, fills         */
    void    *pio;               /**< the PIO block                        */
    uint32_t sm;                /**< the state machine                    */
    uint32_t offset;            /**< where the program loaded             */
    uint32_t t_tx_us;           /**< the clock at the last hand-over      */
    uint32_t span_us;           /**< oy_pio_span_us() of that stream      */
    uint32_t sent;              /**< streams handed over                  */
    uint32_t tx_dropped;        /**< streams dropped as late              */
    int8_t   dma_tx;            /**< the channel of the stream            */
    int8_t   dma_rx;            /**< the channel of the received bytes    */
    uint8_t  pin;               /**< the GPIO                             */
    uint8_t  open;              /**< 1 from open to close                 */
    uint8_t  listening;         /**< 1 from oy_line_listen() to a park    */
    uint8_t  pending;           /**< 1 from a hand-over until it is sent  */
    uint8_t  was_drive;         /**< the drive strength before the open   */
    uint8_t  was_slew;          /**< the slew rate before the open        */
    uint32_t tx[OY_PIO_TX_WORDS];       /**< the stream                  */
    uint16_t ent[OY_PIO_RX_ENTRIES];    /**< received FIFO upper halves  */
} oy_pin_line_t;

/**
 * @brief Mark @p line closed.  Call once, before the first open.
 * @param line the line
 */
void oy_line_init(oy_pin_line_t *line);

/**
 * @brief Claim the resources and take @p pin: from the return on the line is
 *        parked (input, pull-down, machine stopped).
 *
 * The caller has stopped every other use of the pin.  An open line is closed
 * first.
 *
 * @param line the line
 * @param pin  GPIO number
 * @return OY_LINE_OK, or why it was refused
 */
oy_line_err_t oy_line_open(oy_pin_line_t *line, unsigned pin);

/**
 * @brief Cut what runs, give back what the open claimed and leave the pad an
 *        input with its pull-down.  Safe on a closed line.
 * @param line the line
 */
void oy_line_close(oy_pin_line_t *line);

/**
 * @brief Whether a line is open.
 * @param line the line, may be NULL
 * @return true when it is
 */
bool oy_line_is_open(const oy_pin_line_t *line);

/**
 * @brief The pad an input with its pull-up and the receiver running, from entry
 *        0 of the buffer.  Does nothing on a closed line or one listening.
 * @param line the line
 */
void oy_line_listen(oy_pin_line_t *line);

/**
 * @brief The pad an input with its pull-down at once: the machine stopped
 *        first, the stream dropped, the buffer's bytes not yet handed over
 *        dropped.  For the time before the supply reads on, a fault, STOP and
 *        link loss.  Safe on a closed line.
 * @param line the line
 */
void oy_line_park(oy_pin_line_t *line);

/**
 * @brief The bit time the system clock gives.
 * @param line the line
 * @return the time in ns; 0 on a closed line
 */
uint32_t oy_line_bit_ns(const oy_pin_line_t *line);

/**
 * @brief Whether the last frame has gone out: the stream handed over, the
 *        machine off the frame's way out and the pad released.
 * @param line the line, may be NULL
 * @return true when it has, or when none was handed over; false on a line
 *         closed or parked
 */
bool oy_line_sent(const oy_pin_line_t *line);

/**
 * @brief tx() of oy_driver_t: hand a frame to the machine and return.
 * @param ctx   the oy_pin_line_t
 * @param frame the bytes
 * @param n     how many, 1 to OY_PIO_TX_MAX
 * @return false on a line closed or parked, a frame of no bytes or too long,
 *         or a stream before it still being sent and not late
 */
bool oy_line_tx(void *ctx, const uint8_t *frame, size_t n);

/**
 * @brief rx() of oy_driver_t: the bytes received since the last call.
 * @param ctx the oy_pin_line_t
 * @param buf takes the bytes
 * @param cap room at @p buf
 * @return the bytes written; 0 on a line closed or parked
 */
size_t oy_line_rx(void *ctx, uint8_t *buf, size_t cap);

/**
 * @brief now_us() of oy_driver_t.
 * @param ctx unused
 * @return the microsecond timer's low 32 bits
 */
uint32_t oy_line_now_us(void *ctx);

/**
 * @brief The driver for @p line.
 * @param line the line
 * @return the three functions, with @p line as their context
 */
oy_driver_t oy_line_driver(oy_pin_line_t *line);

#ifdef __cplusplus
}
#endif

#endif /* OY_LINE_H */
