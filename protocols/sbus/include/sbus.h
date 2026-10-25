/**
 * @file
 * @brief Futaba S.BUS: sixteen channels in twenty-five bytes.
 *
 * Implements the S.BUS receiver protocol of Futaba.  Futaba and S.BUS are
 * names or trademarks of their respective owners; they are used here only to
 * say which receivers this module works with (NOTICE, "Trademarks").
 *
 * The wire is an inverted 8E2 UART (universal asynchronous
 * receiver-transmitter) at 100 kbaud, produced by the IOmcu's PIO
 * (programmable input/output); this file decodes the bytes.
 *
 * S.BUS has no checksum and no escaping.  A frame is a header byte, a
 * bit-packed payload and a footer, and the header value 0x0F is an ordinary
 * value inside channel data, so the header alone cannot locate a frame start:
 * a decoder that trusts it locks onto the middle of a frame and reports
 * sixteen plausible channels that are all wrong.
 *
 * The frame boundary is the gap.  Frames arrive every 7ms or 14ms and take
 * 3ms to send, so there are at least 4ms of silence between them, against
 * 120 microseconds between bytes within one.  The decoder is fed a timestamp
 * per byte and frames on that gap; the header and footer confirm.
 *
 * Two flags outrank the channels.  failsafe means the receiver has lost the
 * transmitter and is generating the values it sends; frame lost means this
 * frame did not arrive intact.  Anything driven from S.BUS input treats
 * failsafe as a stop, not as sixteen valid channels.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_SBUS_H
#define RCBENCH_SBUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Frame layout: byte count, header byte and channel count. */
#define SBUS_FRAME_BYTES 25u   /**< bytes per frame, header to footer */
#define SBUS_HEADER      0x0Fu /**< first byte of every frame */
#define SBUS_CHANNELS    16u   /**< channels per frame */

/** @brief Raw channel range: eleven bits. */
#define SBUS_RAW_MIN 0u
#define SBUS_RAW_MAX 2047u

/**
 * @brief The two ends of the range a transmitter at its endpoints actually
 *        sends, and what they mean in microseconds.
 *
 * Not the raw limits: 172 and 1811 are what the protocol settled on, and a
 * receiver can send outside them.
 */
#define SBUS_RAW_1000US 172u  /**< raw value of a 1000us pulse */
#define SBUS_RAW_2000US 1811u /**< raw value of a 2000us pulse */

/**
 * @brief Silence that means a frame boundary.
 *
 * Bytes within a frame are 120 microseconds apart at 100 kbaud 8E2; frames are
 * at least 4ms apart. Two milliseconds sits clear of both, so it cannot split
 * a frame and cannot join two.
 */
#define SBUS_GAP_US 2000u

/** @brief Flag bits in byte 23. */
#define SBUS_FLAG_CH17       0x01u /**< digital channel 17 */
#define SBUS_FLAG_CH18       0x02u /**< digital channel 18 */
#define SBUS_FLAG_FRAME_LOST 0x04u /**< frame did not arrive intact */
#define SBUS_FLAG_FAILSAFE   0x08u /**< receiver is in failsafe */

/** @brief One decoded S.BUS frame. */
typedef struct {
    uint16_t channel[SBUS_CHANNELS]; /**< 0..2047, as sent */
    bool     ch17;                   /**< the two digital channels */
    bool     ch18;                      /**< digital channel 18 */
    /** @brief This frame did not arrive intact at the receiver. */
    bool     frame_lost;
    /**
     * @brief The receiver has lost the transmitter and is generating these
     *        values.
     */
    bool     failsafe;
} sbus_frame_t;

/** @brief Decoder state for one S.BUS byte stream. */
typedef struct {
    uint8_t  buf[SBUS_FRAME_BYTES]; /**< bytes of the frame being collected */
    uint8_t  len;                   /**< bytes held in buf */
    uint32_t last_byte_us;          /**< arrival time of the last byte, in us */
    bool     have_time;             /**< true once last_byte_us is set */

    /* Diagnostics, for the analyser view. */
    uint32_t frames;
    uint32_t resyncs;       /**< bytes thrown away looking for a boundary */
    uint32_t bad_footer;    /**< right length, right header, wrong tail */
} sbus_decoder_t;

/**
 * @brief Clear a decoder, including its diagnostic counters.
 *
 * @param d  Decoder to clear.  NULL is ignored.
 */
void sbus_decoder_reset(sbus_decoder_t *d);

/**
 * @brief Feed one byte with the time it arrived; true when @p out holds a
 *        frame.
 *
 * @p now_us paces the framing.  A gap of SBUS_GAP_US or more starts a new
 * frame, whatever was half-collected before it; the gap is the only reliable
 * boundary this protocol has.
 *
 * @param d       Decoder state.  NULL returns false.
 * @param byte    Received byte.
 * @param now_us  Arrival time of the byte, in us.
 * @param out     Receives the frame; may be NULL.
 * @return true when a frame with a valid footer completes.
 */
bool sbus_decode_byte(sbus_decoder_t *d, uint8_t byte, uint32_t now_us,
                      sbus_frame_t *out);

/**
 * @brief Microseconds for a raw channel value, clamped to a sane pulse.
 *
 * @param raw  Raw channel value.
 * @return Pulse width in us, clamped to 800 to 2200.
 */
uint16_t sbus_to_us(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_SBUS_H */
