/*
 * The panel-to-coprocessor frame: envelope, page/register payload, CRC.
 *
 * The model is ArduPilot's IOMCU, which has flown this exact problem for a
 * decade -- a small processor owning RC input and PWM output while the main
 * one does everything else.  Every transaction is a page, an offset and a
 * count over sixteen-bit registers, so a new feature adds a *page* rather
 * than a message type, and a decoder written years from now needs to
 * understand five bytes rather than a grammar.
 *
 * Two properties this file exists to guarantee:
 *
 *   The coprocessor never speaks unsolicited.  There is no "event" op and
 *   there is not going to be one.  Nothing arbitrates outbound priority
 *   because nothing competes for it, which is how a stop command is kept from
 *   queueing behind a telemetry burst -- by construction rather than by
 *   scheduling.
 *
 *   A corrupt frame is never accepted.  The decoder resynchronises inside its
 *   own buffer rather than waiting for a gap, because an auto-direction RS485
 *   transceiver holds its driver enabled for a fixed time after the last edge
 *   and a mid-frame turnaround looks exactly like truncation.
 *
 * ---------------------------------------------------------------- the wire
 *
 *   0      1      2     3      4       5       6 .. 6+2n      last two
 *   +------+------+-----+------+-------+-------+-------------+---------+
 *   | SYNC | LEN  | OP  | PAGE | OFFS  | COUNT | payload ... |  CRC16  |
 *   +------+------+-----+------+-------+-------+-------------+---------+
 *
 * LEN counts every byte after itself, CRC included, so a frame on the wire is
 * LEN + 2 bytes long and a receiver knows how much to read from byte one.
 * Registers are little-endian, and so is the CRC -- one convention, not two.
 *
 * The CRC covers SYNC and LEN as well as the payload.  A corrupted LEN is
 * caught by the arithmetic anyway (the CRC would be read from the wrong
 * offset), but covering it costs nothing and means the check is over the
 * whole frame rather than over most of it.
 *
 * ------------------------------------------------------------- frame sizes
 *
 * A page is 32 registers, so a whole-page transfer is 6 + 64 + 2 = 72 bytes:
 * 480 us at 1.5 Mbaud.  The research that specified this link put the cap at
 * 64 bytes and quoted 340 us, but that figure was compared against the touch
 * controller's own 8-16 ms scan interval -- and 480 us is just as negligible
 * against it.  So the cap follows the page size here rather than the page
 * size being cut to fit an approximate cap.  Latency was never the wire's
 * problem; that is why the stop has its own line.
 */
#ifndef RCBENCH_LINK_FRAME_H
#define RCBENCH_LINK_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not 0x00 and not 0xFF: an idle line and a stuck driver both present as one
 * of those, and a sync byte that a fault can manufacture is not a sync byte. */
#define LINK_SYNC 0xA5u

/** Registers in a page, and therefore in the largest transfer. */
#define LINK_MAX_REGS 32

/** Bytes before the payload: SYNC, LEN, OP, PAGE, OFFSET, COUNT. */
#define LINK_HEADER_BYTES 6

/** The largest frame on the wire. */
#define LINK_MAX_FRAME (LINK_HEADER_BYTES + 2 * LINK_MAX_REGS + 2)

typedef enum {
    LINK_OP_READ  = 0x01, /**< host asks for COUNT registers from PAGE:OFFS  */
    LINK_OP_WRITE = 0x02, /**< host sets them                                */
    LINK_OP_DATA  = 0x03, /**< coprocessor answers a read                    */
    LINK_OP_ACK   = 0x04, /**< coprocessor accepted a write                  */
    LINK_OP_NACK  = 0x05, /**< coprocessor refused it; regs[0] says why      */
} link_op_t;

/** Reasons a NACK carries.  The coprocessor reports; it does not ask. */
typedef enum {
    LINK_NACK_BAD_PAGE   = 1,
    LINK_NACK_BAD_RANGE  = 2, /**< offset + count runs off the end of a page */
    LINK_NACK_READ_ONLY  = 3,
    LINK_NACK_BAD_VALUE  = 4,
    LINK_NACK_NOT_ARMED  = 5,
} link_nack_t;

typedef struct {
    uint8_t  op;     /**< a link_op_t                                       */
    uint8_t  page;
    uint8_t  offset; /**< first register within the page                    */
    uint8_t  count;  /**< registers meant, 0..LINK_MAX_REGS                 */
    uint16_t regs[LINK_MAX_REGS];
} link_msg_t;

/**
 * Serialise @p msg into @p out.
 *
 * Returns the number of bytes written, or 0 if the message is malformed or
 * would not fit -- there is no partial write and no truncation, because a
 * short frame on this wire is indistinguishable from a mid-frame turnaround.
 *
 * A READ carries no payload however large its count is: the count is the
 * question, not the answer.
 */
size_t link_encode(uint8_t *out, size_t cap, const link_msg_t *msg);

/**
 * The receiver.  Feed it one byte at a time -- which is what a UART interrupt
 * has -- and it returns true exactly when a whole frame has verified.
 *
 * It never reports a frame it has not checked, and on a failure it does not
 * discard what it holds: it steps past the byte that looked like a sync and
 * re-examines the rest, so a real frame that began inside a burst of noise is
 * still found.  Waiting for an idle gap instead would mean losing the frame
 * that follows the corrupt one, and on a polled link that is the reply.
 */
typedef struct {
    uint8_t buf[LINK_MAX_FRAME];
    uint8_t len;
    /* Diagnostics.  A link that is merely noisy and one that is misconfigured
     * look identical from a single failure; over a minute they do not. */
    uint32_t frames;
    uint32_t crc_errors;
    uint32_t resyncs;
} link_decoder_t;

void link_decoder_reset(link_decoder_t *d);
bool link_decode_byte(link_decoder_t *d, uint8_t byte, link_msg_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_FRAME_H */
