/**
 * @file
 * @brief The KST wire on one RP2350 pin: a kst_driver_t made of one PIO
 * (programmable input/output) state machine (kst_line.pio) and two DMA
 * (direct memory access) channels, one for the words of a frame and one
 * for the stamps of its reply window.
 *
 * Implements the programming protocol of KST and Chaservo servos.  KST and
 * Chaservo are names or trademarks of their respective owners; they are used
 * here only to say which servos this module works with (README.md, "Legal
 * notice and disclaimer").
 *
 * Resources.  kst_line_open() claims 1 state machine, 24 words of program
 * space in a PIO block that reaches the pin, and 2 DMA channels;
 * kst_line_close() gives all of them back.  A second line on a second pin
 * claims its own and shares the program when its state machine is in the
 * same block.  No interrupt and no PIO interrupt flag is used.
 *
 * Clock.  The counts are computed from clock_get_hz(clk_sys) at the open.
 * The open is refused when the half-cell that clock gives is more than
 * 0.2 % from 25.40 us.  At 150 MHz the half-cell is 3810 cycles and a
 * stamp resolves 26.7 ns.  The system clock must not change while the line
 * is open.
 *
 * Pad.  While the line is open and no transaction runs, the pad drives low.
 * In a transaction it is push-pull for the frame, driven low for 50 us
 * after the frame's last edge, then an input.  It drives low again 165 us
 * after the last rising edge of a reply, or at the end of the window when
 * no edge came.  The pad's pull-down is on and its pull-up off from the
 * open on, and the close leaves the pad an input with the pull-down on.  A
 * pull-down on the board is expected: the internal one is 50 to 80 kOhm.
 * A level change before the reply starts the 165 us early, so the pad may
 * drive low into a reply; the capture then holds the edges up to that point
 * and the decoder refuses it.
 *
 * Every function is called from one context.  kst_line_poll() ends a
 * transaction whose state machine still runs 1 ms after the window: the
 * state machine is restarted with the pad driven low and the capture is
 * marked as overflowed.
 *
 * Not measured: no frame of this driver has been on a pin.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef KST_LINE_H
#define KST_LINE_H

#include <stdbool.h>
#include <stdint.h>

#include "kst_pio.h"
#include "kst_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The driver ends a transaction whose state machine still runs this
 *        long after the end of the window.
 */
#define KST_LINE_GRACE_US 1000u

/** @brief Error codes of the KST line driver. */
typedef enum {
    KST_LINE_OK = 0,     /**< no error */
    KST_LINE_ERR_ARG,    /**< no line object, or a pin the chip does not have */
    KST_LINE_ERR_CLOCK,  /**< the system clock gives no 25.40 us half-cell */
    KST_LINE_ERR_PIO,    /**< no state machine or no program space */
    KST_LINE_ERR_DMA,    /**< fewer than 2 free DMA channels */
} kst_line_err_t;

/**
 * @brief One line.  The caller keeps the object; the fields are the driver's.
 */
typedef struct {
    kst_pio_clock_t clock;
    kst_pio_run_t   run;
    void    *pio;
    uint32_t sm;
    uint32_t offset;
    uint32_t t_start_us;
    int8_t   dma_tx;
    int8_t   dma_rx;
    uint8_t  pin;
    uint8_t  open;
    uint8_t  busy;
    uint32_t tx[KST_PIO_TX_WORDS];
    uint32_t rx[KST_PIO_RX_WORDS];
} kst_line_t;

/** @brief Mark @p line closed.  Call once, before the first open.
 *
 * @param line  Line object to mark closed.
 */
void kst_line_init(kst_line_t *line);

/**
 * @brief Claim the resources and take @p pin: from the return on the pad drives
 * low.  The caller has stopped every other use of the pin.  On an error
 * nothing stays claimed and the pin is not touched.  An open line is closed
 * first.
 *
 * @param line  Line object.
 * @param pin   GPIO number.
 * @return KST_LINE_OK, or the error that refused the open.
 */
kst_line_err_t kst_line_open(kst_line_t *line, unsigned pin);

/**
 * @brief Stop the state machine, give back what the open claimed and leave the
 *  pad an input with its pull-down on.  A transaction on the wire is cut.
 *  Safe on a closed line.
 *
 * @param line  Line object.
 */
void kst_line_close(kst_line_t *line);

/**
 * @brief Check whether a line is open.
 *
 * @param line  Line object.
 * @return true when the line is open.
 */
bool kst_line_is_open(const kst_line_t *line);

/**
 * @brief A transaction runs: between a start() that returned true and the
 *          poll() that returns true.
 *
 * @param line  Line object.
 * @return true between a start that returned true and the poll that returns
 *              true.
 */
bool kst_line_busy(const kst_line_t *line);

/**
 * @brief The transmit half-cell the system clock gives, in ns; 0 on a closed
 *  line.
 *
 * @param line  Line object.
 * @return The transmit half-cell in ns; 0 on a closed line.
 */
uint32_t kst_line_half_cell_ns(const kst_line_t *line);

/** @brief The three functions of kst_driver_t; @p ctx is the kst_line_t.
 *
 * @param ctx       The kst_line_t.
 * @param frame     Frame to send.
 * @param window_ns  Capture end, in ns after the last edge.
 * @return true when the transaction started.
 */
bool kst_line_start(void *ctx, const kst_frame_t *frame, uint32_t window_ns);
/**
 * @brief Poll a transaction; the second of the three driver functions.
 *
 * @param ctx  The kst_line_t.
 * @param out  Receives the capture when the transaction has ended.
 * @return true once, when the transaction has ended.
 */
bool kst_line_poll(void *ctx, kst_capture_t *out);
/**
 * @brief Read the microsecond clock; the third of the three driver functions.
 *
 * @param ctx  The kst_line_t.
 * @return The microsecond clock.
 */
uint32_t kst_line_now_us(void *ctx);

/** @brief The driver for a session on @p line.
 *
 * @param line  Line object.
 * @return The driver for a session on the line.
 */
kst_driver_t kst_line_driver(kst_line_t *line);

#ifdef __cplusplus
}
#endif

#endif /* KST_LINE_H */
