/*
 * Futaba S.BUS: sixteen channels in twenty-five bytes.
 *
 * The wire is an inverted 8E2 UART at 100 kbaud, which the coprocessor's PIO
 * produces; this file is what to do with the bytes once they arrive.
 *
 * THE THING THAT MAKES S.BUS AWKWARD, and the reason this is not just a
 * memcpy: **it has no checksum and no escaping.** A frame is a header byte, a
 * bit-packed payload and a footer, and 0x0F -- the header -- is an ordinary
 * value inside channel data. So the header alone cannot tell you where a frame
 * starts, and a decoder that trusts it will happily lock onto the middle of
 * one and report sixteen plausible-looking channels that are all wrong.
 *
 * What actually delimits an S.BUS frame is the *gap*. Frames arrive every 7 ms
 * or 14 ms and take 3 ms to send, so there is at least 4 ms of silence between
 * them, against 120 microseconds between bytes within one. Feed this decoder a
 * timestamp and it uses that gap; the header and footer then confirm rather
 * than have to carry the whole job alone.
 *
 * Two flags matter more than the channels. **failsafe** means the receiver has
 * lost the transmitter and is inventing the numbers it is sending; **frame
 * lost** means this particular frame did not arrive intact. A bench that
 * drives anything from S.BUS input has to treat the first as "stop", not as
 * sixteen valid channels -- which is exactly what they look like.
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

#define SBUS_FRAME_BYTES 25u
#define SBUS_HEADER      0x0Fu
#define SBUS_CHANNELS    16u

/** Raw channel range: eleven bits. */
#define SBUS_RAW_MIN 0u
#define SBUS_RAW_MAX 2047u

/*
 * The two ends of the range a transmitter at its endpoints actually sends,
 * and what they mean in microseconds.  Not the raw limits: 172 and 1811 are
 * what the protocol settled on, and a receiver can send outside them.
 */
#define SBUS_RAW_1000US 172u
#define SBUS_RAW_2000US 1811u

/**
 * Silence that means a frame boundary.
 *
 * Bytes within a frame are 120 microseconds apart at 100 kbaud 8E2; frames are
 * at least 4 ms apart. Two milliseconds sits clear of both, so it cannot split
 * a frame and cannot join two.
 */
#define SBUS_GAP_US 2000u

/** Flag bits in byte 23. */
#define SBUS_FLAG_CH17       0x01u
#define SBUS_FLAG_CH18       0x02u
#define SBUS_FLAG_FRAME_LOST 0x04u
#define SBUS_FLAG_FAILSAFE   0x08u

typedef struct {
    uint16_t channel[SBUS_CHANNELS]; /**< 0..2047, as sent */
    bool     ch17;                   /**< the two digital channels */
    bool     ch18;
    /** This frame did not arrive intact at the receiver. */
    bool     frame_lost;
    /** The receiver has lost the transmitter and is inventing these numbers. */
    bool     failsafe;
} sbus_frame_t;

typedef struct {
    uint8_t  buf[SBUS_FRAME_BYTES];
    uint8_t  len;
    uint32_t last_byte_us;
    bool     have_time;

    /* Diagnostics, for the analyser view. */
    uint32_t frames;
    uint32_t resyncs;       /**< bytes thrown away looking for a boundary */
    uint32_t bad_footer;    /**< right length, right header, wrong tail */
} sbus_decoder_t;

void sbus_decoder_reset(sbus_decoder_t *d);

/**
 * Feed one byte with the time it arrived; true when @p out holds a frame.
 *
 * @p now_us paces the framing. A gap of SBUS_GAP_US or more starts a new
 * frame, whatever was half-collected before it -- which is the only reliable
 * boundary this protocol has.
 */
bool sbus_decode_byte(sbus_decoder_t *d, uint8_t byte, uint32_t now_us,
                      sbus_frame_t *out);

/** Microseconds for a raw channel value, clamped to a sane pulse. */
uint16_t sbus_to_us(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_SBUS_H */
