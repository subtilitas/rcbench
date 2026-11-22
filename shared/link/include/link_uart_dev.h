/**
 * @file
 * @brief The IOmcu end of the link on a UART (universal asynchronous
 * receiver-transmitter): the receive ring a DMA (direct memory access)
 * channel fills, the transmit queue the main loop empties into the UART's
 * FIFO (first in, first out buffer), and the answer to one request frame.
 *
 * The UART runs at LINK_UART_BAUD, 8N1: one byte every LINK_UART_BYTE_US.
 * Frames are link_uart.h's: COBS (consistent overhead byte stuffing) with a
 * CRC-16 (cyclic redundancy check), 9 to 17 bytes each.
 *
 * Receive.  One DMA channel copies every received byte into a ring of
 * LINK_UART_RING_BYTES in SRAM, whatever the processor does: a flash window
 * that stops core 0 for up to 42ms (docs/Safety.md) holds 4,200 byte times,
 * and the UART's own FIFO holds 32 bytes, 320us.  The channel re-triggers
 * itself every LINK_UART_DMA_RELOAD bytes, a multiple of the ring, and
 * counts down from it; link_uart_ring_dma() turns that count into the bytes
 * written since the start, so the reader knows how far the DMA has run, not
 * only where it writes.  A reader that falls more than
 * LINK_UART_RING_BYTES - LINK_UART_RING_SLACK bytes behind has lost bytes:
 * it counts one lap, drops what the ring holds and the frame under way, and
 * starts again at the next 0x00.  A lap is counted as the CAN (Controller
 * Area Network) controller's receive overrun is, not as a refused frame.
 *
 * With one request in flight, the node sends at most one request frame and
 * its one repeat 50ms later (LINK_HOST_RETRY_MS) before it waits for an
 * answer: 34 bytes in any 42ms window.  Only noise or a boot message on the
 * line fills the ring faster.
 *
 * Transmit.  A reply is queued whole or not at all, as on CAN
 * (can_txq.h): LINK_UART_TXQ_BYTES hold three replies of 8 frames.  The
 * main loop moves bytes into the UART's 32-byte FIFO while it has room; no
 * call waits.  A reply refused counts in `dropped`, and the node's repeat
 * after 50ms is answered from the copy link_dev.c keeps.
 *
 * Plain C, no includes outside `shared/link`; also compiles as C++.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_UART_DEV_H
#define RCBENCH_LINK_UART_DEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_can.h"
#include "link_dev.h"
#include "link_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The line rate, baud: 8N1, 10 bit times a byte. */
#define LINK_UART_BAUD 1000000u

/** @brief One byte on the line at LINK_UART_BAUD, us. */
#define LINK_UART_BYTE_US 10u

/** @brief The receive ring, bytes: a power of two, aligned to its size. */
#define LINK_UART_RING_BYTES 1024u

/**
 * @brief Bytes short of a full ring that already count as a lap.
 *
 * The DMA writes on while the reader takes bytes; 64 bytes are 640us of
 * line time, far more than a reader takes for the 960 bytes before them.
 */
#define LINK_UART_RING_SLACK 64u

/**
 * @brief Transfers per trigger of the receive channel: the largest multiple
 * of the ring that the RP2350's 28-bit transfer count holds.
 */
#define LINK_UART_DMA_RELOAD 0x0FFFFC00u

/** @brief The transmit queue, bytes: a power of two. */
#define LINK_UART_TXQ_BYTES 512u

/** @brief The UART's own transmit FIFO, bytes. */
#define LINK_UART_FIFO_BYTES 32u

/**
 * @brief The reader of the receive ring and its decoder.  Set up with
 * link_uart_ring_init(); no field is for the caller to write.
 */
typedef struct {
    const volatile uint8_t *ring; /**< LINK_UART_RING_BYTES, written by DMA */
    uint32_t reload;  /**< transfers per trigger, a multiple of the ring    */
    uint32_t done;    /**< transfers of the current trigger at the last look */
    uint32_t head;    /**< bytes the DMA wrote since the start, mod 2^32    */
    uint32_t tail;    /**< bytes taken, mod 2^32                            */
    uint32_t laps;    /**< times the DMA overwrote bytes not yet taken      */
    link_uart_rx_t rx; /**< the frame decoder and its counts                */
} link_uart_ring_t;

/**
 * @brief The transmit queue: whole replies as wire bytes.  Set up with
 * link_uart_txq_init(); no field is for the caller to write.
 */
typedef struct {
    uint8_t  buf[LINK_UART_TXQ_BYTES]; /**< the bytes, modulo the size      */
    uint32_t head;    /**< bytes put since the start, mod 2^32              */
    uint32_t tail;    /**< bytes taken, mod 2^32                            */
    uint32_t dropped; /**< replies refused whole, mod 2^32                  */
    uint16_t peak;    /**< most bytes held since link_uart_txq_take_peak()  */
} link_uart_txq_t;

/**
 * @brief Start a reader at the ring's first byte with nothing written.
 *
 * The DMA channel starts at the ring's first byte with @p reload transfers
 * at the same time.  The decoder starts as after a 0x00: on a line that is
 * idle at the start the node's first request is taken, and a start in the
 * middle of a frame costs that frame, refused at its 0x00.
 *
 * @param r The reader.
 * @param ring The ring, LINK_UART_RING_BYTES; kept, not copied.
 * @param reload Transfers per trigger: a non-zero multiple of
 *        LINK_UART_RING_BYTES (LINK_UART_DMA_RELOAD on the RP2350).
 * @return true when set up; false for a NULL argument or a @p reload that is
 *         0 or no multiple of the ring, which leaves @p r untouched.
 */
bool link_uart_ring_init(link_uart_ring_t *r, const volatile uint8_t *ring,
                         uint32_t reload);

/**
 * @brief Read how far the DMA has run: its transfer count as read now.
 *
 * A count above the one read before is a new trigger: the bytes up to the
 * reload and from it count both.  The reader must look at least once a
 * trigger, LINK_UART_DMA_RELOAD bytes or 2,684s at LINK_UART_BAUD; the main
 * loop looks every pass.
 *
 * @param r The reader.
 * @param remaining The channel's transfer count, the mode bits masked off:
 *        0 to the reload.  A value above the reload is taken as the reload.
 */
void link_uart_ring_dma(link_uart_ring_t *r, uint32_t remaining);

/**
 * @brief Take bytes from the ring up to the head link_uart_ring_dma() read
 * last, and hand out the next good frame.
 *
 * A reader more than LINK_UART_RING_BYTES - LINK_UART_RING_SLACK bytes
 * behind first counts one lap in `laps`, moves to the head and drops the
 * frame under way (link_uart_rx_drop()).  Frames the decoder refuses count
 * in @p refused (link_uart_feed()).
 *
 * @param r The reader.
 * @param out Receives the frame when true is returned.
 * @param refused The refused-frame counter, STATUS registers 8 and 9; NULL
 *        counts nothing there.
 * @return true with a frame in @p out; false once every byte up to the head
 *         is taken, and for a NULL @p r or @p out.
 */
bool link_uart_ring_next(link_uart_ring_t *r, link_can_frame_t *out,
                         uint32_t *refused);

/**
 * @brief Empty a queue and clear its counts.
 *
 * @param q The queue; NULL does nothing.
 */
void link_uart_txq_init(link_uart_txq_t *q);

/**
 * @brief Queue @p n frames as wire bytes, all of them or none.
 *
 * @param q The queue.
 * @param frames The frames, as link_can_encode() made them.
 * @param n Frames, 1 to LINK_CAN_MAX_FRAMES.
 * @return true when every frame is queued; false, nothing queued and
 *         `dropped` raised by 1, when they do not fit or a frame does not
 *         encode; false without a count for a NULL argument or @p n 0 or
 *         over LINK_CAN_MAX_FRAMES.
 */
bool link_uart_txq_put(link_uart_txq_t *q, const link_can_frame_t *frames,
                       size_t n);

/**
 * @brief Take the next byte to send.
 *
 * @param q The queue.
 * @param byte Receives it when true is returned.
 * @return true with a byte; false when the queue is empty or an argument is
 *         NULL.
 */
bool link_uart_txq_get(link_uart_txq_t *q, uint8_t *byte);

/**
 * @brief Bytes waiting in the queue.
 *
 * @param q The queue.
 * @return 0 to LINK_UART_TXQ_BYTES; 0 for NULL.
 */
size_t link_uart_txq_held(const link_uart_txq_t *q);

/**
 * @brief The most bytes held since the last call, and start counting again
 * from what is held now.
 *
 * @param q The queue.
 * @return The peak, 0 to LINK_UART_TXQ_BYTES; 0 for NULL.
 */
uint16_t link_uart_txq_take_peak(link_uart_txq_t *q);

/**
 * @brief Answer one received frame: decode it, dispatch it, and render the
 * reply as frames.
 *
 * The same steps the CAN receive loop runs: link_can_decode_counted()
 * (a refusal counts in @p refused), one in @p served for a request taken,
 * link_dev_dispatch() and link_can_encode().
 *
 * @param dev The link's device end.
 * @param in The frame received.
 * @param now_ms The pass's clock, ms.
 * @param refused The refused-frame counter; NULL counts nothing there.
 * @param served Requests taken, mod 2^32; NULL counts nothing.
 * @param out Receives up to LINK_CAN_MAX_FRAMES frames of the reply.
 * @return The reply's frames, 1 to LINK_CAN_MAX_FRAMES; 0 for a refused
 *         frame, a request that gets no reply or a NULL @p dev, @p in or
 *         @p out.
 */
size_t link_uart_dev_answer(link_dev_t *dev, const link_can_frame_t *in,
                            uint32_t now_ms, uint32_t *refused,
                            uint32_t *served, link_can_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_UART_DEV_H */
