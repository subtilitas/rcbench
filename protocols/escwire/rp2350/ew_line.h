/**
 * @file ew_line.h
 * @brief The signal wire of an ESC (electronic speed controller) on one RP2350
 *        pin: an ew_driver_t made of one PIO (programmable input/output) state
 *        machine (ew_line.pio) and two DMA (direct memory access) channels.
 *
 * Nothing here makes a throttle signal.  The program sends the bytes of an
 * ew_xfer_t and nothing else; ew_guard.h judges those bytes.
 *
 * Resources.  ew_line_open() claims 1 state machine, 25 words of program space
 * in a PIO block that reaches the pin, and 2 DMA channels; ew_line_close()
 * gives all of them back.  No interrupt and no PIO interrupt flag is used.  An
 * open refused leaves nothing claimed and the pin untouched.  The object is
 * 3,488B: 3072B of received words and 344B of stream.
 *
 * Clock.  Both dividers come from clock_get_hz(clk_sys) at the open, 16 state
 * machine cycles a bit.  The open is refused when a divider is below 4 or a
 * bit time more than 0.05% off.  The system clock must not change while the
 * line is open.
 *
 * Pad.  2mA drive and slow slew from the open to the close; the close puts the
 * drive and slew back as it found them.
 *
 * | State | Pad | Input buffer |
 * |---|---|---|
 * | ew_line_open(), EW_LINE_PARK, abort() | input, pull-down | off |
 * | a capture marked aborted or overflow | input, pull-down | off |
 * | EW_LINE_HIGH | driven high | on |
 * | a transaction's bytes | driven, both levels | on |
 * | a reply, the rest after a transaction, EW_LINE_UP | input, pull-up | on |
 * | EW_LINE_LOW | driven low | off |
 * | ew_line_close() | input, pull-down, SIO function | on |
 * | a restart of the chip | the chip's reset state | |
 *
 * The input buffer is off in EW_LINE_PARK since erratum RP2350-E9 holds an A2
 * chip's input pad at around 2.2V, leaking typically 120uA, against its
 * pull-down while the buffer is on; the buffer off removes the leak.  The next
 * driver takes the pad after a close, so its buffer is on there, and an A2 pad
 * can sit at 2.2V until then.  The pull-up is not affected.
 *
 * The pad's pull-up is 50 to 80kOhm.  Whether it raises an open-drain reply at
 * 52us a bit on a real board is not measured.  A line a load holds down does not
 * reach idle high on it: a first contact on such a line fails.
 *
 * The frame's start: the pad is driven high for 2 state machine cycles before
 * its first start bit, 6.5us at 52us a bit, 3.26us at 38400baud (`set pindirs,
 * 1`, `pull ifempty`, `out pins`).  It shows as a high pulse only on a line that
 * rests low; a pad at EW_LINE_UP or EW_LINE_HIGH does not dip before a frame.
 *
 * Time of a call at 150MHz, the longest the console test printed on the station
 * (commit aeea917d, 13 aborts): ew_line_start() 216us, with the stream of a 17B
 * frame; ew_line_poll() 96us; ew_line_level() 30us; ew_line_abort() 53us; when
 * in that call the pad lets go is not measured.  Not measured either:
 * ew_line_open(), ew_line_close(), a stream of 264B, a capture of 1536B.
 *
 * None of them waits for the wire.  The FIFO wait in a poll or a stop is
 * EW_LINE_RX_SETTLE_SPINS reads of a register at most.
 *
 * Every function is called from one context.  ew_line_poll() ends a transaction
 * whose state machine still runs EW_PIO_GRACE_US after ew_xfer_span_us(): the
 * machine stops and the pad parks at once, and the capture is marked aborted.
 * The session's own limit is 5ms past the same time.  A bad stop bit is an
 * answer and rests on the pull-up, for the session to judge.  The next start
 * takes the pad from any of these.
 *
 * Not measured: no byte of this driver has been to an ESC.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_LINE_H
#define EW_LINE_H

#include <stdbool.h>
#include <stdint.h>

#include "ew_pio.h"
#include "ew_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Reads of the receive FIFO's level a stop makes at most, while the
 *         channel takes the 4 words still in it. */
#define EW_LINE_RX_SETTLE_SPINS 64u

/** @brief Why an open is refused. */
typedef enum {
    EW_LINE_OK = 0,
    EW_LINE_ERR_ARG,   /**< no line object, or a pin the chip does not have */
    EW_LINE_ERR_CLOCK, /**< the system clock gives no divider               */
    EW_LINE_ERR_PIO,   /**< no state machine, or no program space           */
    EW_LINE_ERR_DMA,   /**< fewer than 2 free DMA channels                  */
} ew_line_err_t;

/**
 * @brief One line.
 *
 * The caller keeps the object; the fields are the driver's.
 */
typedef struct {
    ew_pio_clock_t clock[EW_RATES]; /**< the divider of each rate */
    ew_pio_run_t   run; /**< the running transaction */
    void    *pio; /**< the PIO block */
    uint32_t sm; /**< the state machine */
    uint32_t offset; /**< where the program loaded */
    uint32_t t_start_us; /**< the clock at the start */
    int8_t   dma_tx; /**< the channel of the stream */
    int8_t   dma_rx; /**< the channel of the replies */
    uint8_t  pin; /**< the GPIO */
    uint8_t  open; /**< 1 from open to close */
    uint8_t  busy; /**< 1 while a transaction runs */
    uint8_t  was_drive;  /**< the drive strength before the open */
    uint8_t  was_slew;   /**< the slew rate before the open */
    uint8_t  rest_down;  /**< see ew_line_rest_down() */
    uint32_t tx[EW_PIO_TX_WORDS]; /**< the stream */
    uint16_t rx[EW_PIO_RX_WORDS]; /**< the replies, a FIFO word's upper half each */
} ew_pin_line_t;

/**
 * @brief Mark @p line closed.  Call once, before the first open.
 * @param line the line
 */
void ew_line_init(ew_pin_line_t *line);

/**
 * @brief Claim the resources and take @p pin: from the return on the pad is in
 *        EW_LINE_PARK.
 *
 * The caller has stopped every other use of the pin.  An open line is closed
 * first.
 *
 * @param line the line
 * @param pin  GPIO number
 * @return EW_LINE_OK, or why it was refused
 */
ew_line_err_t ew_line_open(ew_pin_line_t *line, unsigned pin);

/**
 * @brief Cut what runs, give back what the open claimed and leave the pad an
 *        input with its pull-down.  Safe on a closed line.
 * @param line the line
 */
void ew_line_close(ew_pin_line_t *line);

/**
 * @brief Whether a line is open.
 * @param line the line, may be NULL
 * @return true when it is
 */
bool ew_line_is_open(const ew_pin_line_t *line);

/**
 * @brief Whether a transaction runs.
 * @param line the line, may be NULL
 * @return true from a start() that returned true to its capture
 */
bool ew_line_busy(const ew_pin_line_t *line);

/**
 * @brief The bit time the system clock gives.
 * @param line the line
 * @param rate an ew_rate_t
 * @return the time in ns; 0 on a closed line or an unknown rate
 */
uint32_t ew_line_bit_ns(const ew_pin_line_t *line, uint8_t rate);

/**
 * @brief For the analyser check: a transaction that receives nothing is driven
 *        high at its start and releases into a pull-down with the input buffer
 *        off, so the release shows as a falling edge.  Off after an open.
 * @param line the line
 * @param on   true for the pull-down
 */
void ew_line_rest_down(ew_pin_line_t *line, bool on);

/**
 * @brief start() of ew_driver_t.
 * @param ctx the ew_pin_line_t
 * @param x   the transaction
 * @return false for a transaction ew_xfer_valid() refuses, a closed line, or a
 *         line with a transaction running
 */
bool ew_line_start(void *ctx, const ew_xfer_t *x);

/**
 * @brief poll() of ew_driver_t.
 * @param ctx the ew_pin_line_t
 * @param out takes the capture
 * @return true once, at the transaction's end
 */
bool ew_line_poll(void *ctx, ew_capture_t *out);

/**
 * @brief abort() of ew_driver_t: the pad at EW_LINE_PARK at once, no capture.
 * @param ctx the ew_pin_line_t
 */
void ew_line_abort(void *ctx);

/**
 * @brief line() of ew_driver_t.  A transaction still running is cut, with no
 *        capture.
 * @param ctx   the ew_pin_line_t
 * @param level the pad's state
 */
void ew_line_level(void *ctx, ew_line_t level);

/**
 * @brief now_us() of ew_driver_t.
 * @param ctx unused
 * @return the microsecond timer's low 32 bits
 */
uint32_t ew_line_now_us(void *ctx);

/**
 * @brief The driver for @p line.
 * @param line the line
 * @return the five functions, with @p line as their context
 */
ew_driver_t ew_line_driver(ew_pin_line_t *line);

#ifdef __cplusplus
}
#endif

#endif /* EW_LINE_H */
