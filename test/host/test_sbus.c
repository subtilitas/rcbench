/*
 * S.BUS, fed the byte streams a receiver and a noisy line produce.
 *
 * The protocol has no checksum and its header byte is an ordinary channel
 * value, so the failures under test are all about framing: locking onto the
 * middle of a frame, staying locked, and reporting sixteen plausible channels
 * that are all wrong.  Nothing downstream can detect plausible-and-wrong.
 *
 * The encoder is held to frames whose bytes were worked out apart from it,
 * from the published layout (channel 1 in data byte 1 and the low three
 * bits of data byte 2, and so on), and to the decoder: a frame it packs
 * decodes to the frame it was given.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "sbus.h"

/* Build a frame from sixteen channel values. */
static void make(uint8_t *out, const uint16_t *ch, uint8_t flags,
                 uint8_t footer)
{
    memset(out, 0, SBUS_FRAME_BYTES);
    out[0] = SBUS_HEADER;
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        const unsigned bit = c * 11u;
        for (unsigned b = 0; b < 11u; ++b) {
            if (ch[c] & (1u << b)) {
                const unsigned at = bit + b;
                out[1u + at / 8u] |= (uint8_t)(1u << (at % 8u));
            }
        }
    }
    out[23] = flags;
    out[24] = footer;
}

/* Feed a whole frame at realistic byte spacing. Returns how many frames came
 * out, and leaves the last in `out`. */
static int feed(sbus_decoder_t *d, const uint8_t *bytes, size_t n,
                uint32_t *now_us, sbus_frame_t *out)
{
    int got = 0;
    for (size_t i = 0; i < n; ++i) {
        if (sbus_decode_byte(d, bytes[i], *now_us, out)) {
            ++got;
        }
        *now_us += 120;              /* 8E2 at 100 kbaud */
    }
    return got;
}

static void gap(uint32_t *now_us) { *now_us += 5000; }   /* between frames */

TEST_CASE(a_frame_decodes_to_the_channels_that_were_packed)
{
    uint16_t ch[SBUS_CHANNELS];
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        ch[c] = (uint16_t)(c * 131u + 7u);      /* all different, all in range */
    }
    uint8_t frame[SBUS_FRAME_BYTES];
    make(frame, ch, 0, 0);

    sbus_decoder_t d;
    sbus_frame_t f;
    sbus_decoder_reset(&d);
    uint32_t now = 10000;
    gap(&now);
    CHECK_EQ(feed(&d, frame, SBUS_FRAME_BYTES, &now, &f), 1);
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        if (f.channel[c] != ch[c]) {
            T_FAIL("channel %u: got %u, packed %u", c, f.channel[c], ch[c]);
        }
    }
    CHECK_EQ(d.frames, 1);
}

/*
 * Every channel at every bit position, one at a time. Sixteen channels of
 * eleven bits packed end to end across byte boundaries is where a transposed
 * shift hides, and a transposed shift produces a channel that looks entirely
 * reasonable and moves the wrong surface.
 */
TEST_CASE(every_bit_of_every_channel_lands_in_its_own_channel)
{
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        for (unsigned b = 0; b < 11u; ++b) {
            uint16_t ch[SBUS_CHANNELS];
            memset(ch, 0, sizeof(ch));
            ch[c] = (uint16_t)(1u << b);

            uint8_t frame[SBUS_FRAME_BYTES];
            make(frame, ch, 0, 0);
            sbus_decoder_t d;
            sbus_frame_t f;
            sbus_decoder_reset(&d);
            uint32_t now = 0;
            gap(&now);
            if (feed(&d, frame, SBUS_FRAME_BYTES, &now, &f) != 1) {
                T_FAIL("channel %u bit %u did not decode", c, b);
                return;
            }
            for (unsigned k = 0; k < SBUS_CHANNELS; ++k) {
                const uint16_t want = (k == c) ? (uint16_t)(1u << b) : 0u;
                if (f.channel[k] != want) {
                    T_FAIL("channel %u bit %u leaked into channel %u (%u)",
                           c, b, k, f.channel[k]);
                    return;
                }
            }
        }
    }
}

TEST_CASE(the_extremes_of_the_range_survive)
{
    uint16_t ch[SBUS_CHANNELS];
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        ch[c] = (c & 1u) ? SBUS_RAW_MAX : SBUS_RAW_MIN;
    }
    uint8_t frame[SBUS_FRAME_BYTES];
    make(frame, ch, 0, 0);
    sbus_decoder_t d;
    sbus_frame_t f;
    sbus_decoder_reset(&d);
    uint32_t now = 0;
    gap(&now);
    CHECK_EQ(feed(&d, frame, SBUS_FRAME_BYTES, &now, &f), 1);
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        CHECK_EQ(f.channel[c], ch[c]);
    }
}

/*
 * The flags are the part that matters most. A receiver in failsafe sends
 * sixteen perfectly well-formed channels that mean nothing, and nothing
 * downstream can tell without this bit.
 */
TEST_CASE(the_flags_come_through_and_failsafe_is_not_just_another_channel)
{
    uint16_t ch[SBUS_CHANNELS];
    memset(ch, 0, sizeof(ch));
    uint8_t frame[SBUS_FRAME_BYTES];
    sbus_decoder_t d;
    sbus_frame_t f;
    uint32_t now = 0;

    const struct { uint8_t flags; bool c17, c18, lost, fs; } cases[] = {
        { 0x00, false, false, false, false },
        { SBUS_FLAG_CH17, true, false, false, false },
        { SBUS_FLAG_CH18, false, true, false, false },
        { SBUS_FLAG_FRAME_LOST, false, false, true, false },
        { SBUS_FLAG_FAILSAFE, false, false, false, true },
        { 0x0F, true, true, true, true },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        make(frame, ch, cases[i].flags, 0);
        sbus_decoder_reset(&d);
        gap(&now);
        if (feed(&d, frame, SBUS_FRAME_BYTES, &now, &f) != 1) {
            T_FAIL("flags 0x%02X did not decode", cases[i].flags);
            continue;
        }
        CHECK_EQ(f.ch17, cases[i].c17);
        CHECK_EQ(f.ch18, cases[i].c18);
        CHECK_EQ(f.frame_lost, cases[i].lost);
        CHECK_EQ(f.failsafe, cases[i].fs);
    }
}

/*
 * 0x0F is an ordinary channel value, so a decoder that frames on the header
 * alone starts mid-frame, and once it has, the next 0x0F it sees is in the
 * same wrong place: it stays wrong while reporting sixteen plausible numbers.
 *
 * The inter-frame gap is what resets it, and this test starts the stream
 * mid-frame to prove the gap and not the header is doing the work.
 */
TEST_CASE(a_stream_joined_mid_frame_resynchronises_on_the_gap)
{
    uint16_t ch[SBUS_CHANNELS];
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        ch[c] = (uint16_t)(1000 + c);
    }
    /* Channel 0 = 0x0F, so the payload contains a false header. */
    ch[0] = SBUS_HEADER;

    uint8_t frame[SBUS_FRAME_BYTES];
    make(frame, ch, 0, 0);
    CHECK_EQ(frame[1], SBUS_HEADER);      /* the trap is present */

    sbus_decoder_t d;
    sbus_frame_t f;
    sbus_decoder_reset(&d);
    uint32_t now = 0;
    gap(&now);

    /*
     * Ten bytes of a frame and then the transmitter stops: a receiver
     * powered off mid-frame, or a cable pulled.  Those ten sit in the buffer
     * looking like the start of a frame.
     */
    feed(&d, frame, 10, &now, &f);

    /* Then silence, and a whole good frame. */
    gap(&now);
    const int got = feed(&d, frame, SBUS_FRAME_BYTES, &now, &f);

    /*
     * Exactly one frame, and it is the right one.  Without the gap resetting
     * the buffer, the ten stale bytes plus the first fifteen of this frame
     * make a twenty-five byte "frame" of nonsense, and the real frame's last
     * ten bytes start the next one, misaligned from then on.
     */
    CHECK_EQ(got, 1);
    CHECK_EQ(d.frames, 1);
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        CHECK_EQ(f.channel[c], ch[c]);
    }

    /* The same stream with the gaps removed does not produce this frame. */
    sbus_decoder_reset(&d);
    uint32_t t = 0;
    feed(&d, frame, 10, &t, &f);
    const int without_gap = feed(&d, frame, SBUS_FRAME_BYTES, &t, &f);
    if (without_gap == 1 && f.channel[1] == ch[1]) {
        T_FAIL("the stream is not actually misaligning; the test proves "
               "nothing about the gap");
    }
}

/*
 * And the reverse: without a gap, a false header inside the payload must not
 * be taken as a frame start. Here the bytes arrive back to back, so only the
 * length and footer can save it.
 */
TEST_CASE(a_false_header_inside_a_payload_is_not_a_frame_start)
{
    uint16_t ch[SBUS_CHANNELS];
    memset(ch, 0, sizeof(ch));
    ch[0] = SBUS_HEADER;
    uint8_t frame[SBUS_FRAME_BYTES];
    make(frame, ch, 0, 0);

    sbus_decoder_t d;
    sbus_frame_t f;
    sbus_decoder_reset(&d);
    uint32_t now = 0;
    gap(&now);

    /* Three frames back to back with no gaps between them at all. */
    int got = 0;
    for (int i = 0; i < 3; ++i) {
        got += feed(&d, frame, SBUS_FRAME_BYTES, &now, &f);
    }
    CHECK_EQ(got, 3);
    CHECK_EQ(d.frames, 3);
    CHECK_EQ(d.bad_footer, 0);
}

TEST_CASE(a_frame_with_the_wrong_tail_is_refused_and_counted_apart)
{
    uint16_t ch[SBUS_CHANNELS];
    memset(ch, 0, sizeof(ch));
    uint8_t frame[SBUS_FRAME_BYTES];
    sbus_decoder_t d;
    sbus_frame_t f;
    uint32_t now = 0;

    make(frame, ch, 0, 0xFF);            /* no receiver sends this */
    sbus_decoder_reset(&d);
    gap(&now);
    CHECK_EQ(feed(&d, frame, SBUS_FRAME_BYTES, &now, &f), 0);
    CHECK_EQ(d.bad_footer, 1);
    CHECK_EQ(d.frames, 0);

    /* The variants real receivers send are accepted, because refusing them
     * would refuse hardware that works. */
    const uint8_t ok[] = { 0x00, 0x04, 0x14, 0x24, 0x34 };
    for (size_t i = 0; i < sizeof(ok); ++i) {
        make(frame, ch, 0, ok[i]);
        sbus_decoder_reset(&d);
        gap(&now);
        if (feed(&d, frame, SBUS_FRAME_BYTES, &now, &f) != 1) {
            T_FAIL("footer 0x%02X was refused", ok[i]);
        }
    }
}

/* Rubbish before a frame is discarded a byte at a time rather than swallowing
 * the frame behind it. */
TEST_CASE(noise_before_a_frame_does_not_eat_it)
{
    uint16_t ch[SBUS_CHANNELS];
    memset(ch, 0, sizeof(ch));
    ch[3] = 777;
    uint8_t frame[SBUS_FRAME_BYTES];
    make(frame, ch, 0, 0);

    sbus_decoder_t d;
    sbus_frame_t f;
    sbus_decoder_reset(&d);
    uint32_t now = 0;
    gap(&now);

    const uint8_t noise[] = { 0x55, 0xAA, 0x00, 0xFF, 0x12 };
    feed(&d, noise, sizeof(noise), &now, &f);
    CHECK_EQ(feed(&d, frame, SBUS_FRAME_BYTES, &now, &f), 1);
    CHECK_EQ(f.channel[3], 777);
    CHECK(d.resyncs >= sizeof(noise));
}

/*
 * A byte gap must not be mistaken for a frame gap.  At 100 kbaud 8E2 the bytes
 * of one frame are 120 microseconds apart and frames are at least 4ms apart,
 * so the threshold has to sit between; a threshold below the byte spacing
 * restarts the frame on every byte, which is no framing at all.
 */
TEST_CASE(the_gap_threshold_sits_between_a_byte_and_a_frame)
{
    CHECK(SBUS_GAP_US > 120u * 4u);     /* clear of any plausible byte time */
    CHECK(SBUS_GAP_US < 4000u);         /* under the smallest frame gap */

    /* And a frame whose bytes arrive at the real spacing is not split by it. */
    uint16_t ch[SBUS_CHANNELS];
    memset(ch, 0, sizeof(ch));
    ch[7] = 1234;
    uint8_t frame[SBUS_FRAME_BYTES];
    make(frame, ch, 0, 0);
    sbus_decoder_t d;
    sbus_frame_t f;
    sbus_decoder_reset(&d);
    uint32_t now = 0;
    gap(&now);
    CHECK_EQ(feed(&d, frame, SBUS_FRAME_BYTES, &now, &f), 1);
    CHECK_EQ(f.channel[7], 1234);
}

TEST_CASE(the_microsecond_mapping_hits_its_two_fixed_points)
{
    CHECK_EQ(sbus_to_us(SBUS_RAW_1000US), 1000);
    CHECK_EQ(sbus_to_us(SBUS_RAW_2000US), 2000);

    /*
     * The clamp is unreachable from the decoder (eleven bits map to
     * 895..2144us, inside it), so it only guards a caller passing a value
     * the decoder never produces.  Tested directly.
     */
    CHECK_EQ(sbus_to_us(60000u), 2200);
    /* Halfway between them is halfway between the pulses. */
    CHECK_NEAR(sbus_to_us((SBUS_RAW_1000US + SBUS_RAW_2000US) / 2u), 1500, 1);

    /* Monotonic across the whole raw range, and never a pulse that would
     * damage anything even when the receiver sends outside the usual span. */
    uint16_t prev = 0;
    for (uint32_t raw = 0; raw <= SBUS_RAW_MAX; ++raw) {
        const uint16_t us = sbus_to_us((uint16_t)raw);
        if (us < 800 || us > 2200) {
            T_FAIL("raw %u mapped to %u us", (unsigned)raw, us);
            return;
        }
        if (raw > 0 && us < prev) {
            T_FAIL("raw %u went backwards: %u after %u", (unsigned)raw, us,
                   prev);
            return;
        }
        prev = us;
    }
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    sbus_decoder_reset(NULL);
    CHECK_EQ(sbus_decode_byte(NULL, SBUS_HEADER, 0, NULL), false);

    /* A decoder with nowhere to put the answer still frames correctly. */
    uint16_t ch[SBUS_CHANNELS];
    memset(ch, 0, sizeof(ch));
    uint8_t frame[SBUS_FRAME_BYTES];
    make(frame, ch, 0, 0);
    sbus_decoder_t d;
    sbus_decoder_reset(&d);
    uint32_t now = 0;
    gap(&now);
    CHECK_EQ(feed(&d, frame, SBUS_FRAME_BYTES, &now, NULL), 1);
    CHECK_EQ(d.frames, 1);
}

TEST_CASE(the_microsecond_mapping_rounds_the_same_way_on_both_sides)
{
    /* 1000us per 1639 counts: 0.6101us per count, rounded to nearest. */
    CHECK_EQ(sbus_to_us(0u), 895);       /* -104.94 -> -105 */
    CHECK_EQ(sbus_to_us(171u), 999);     /* -0.61   -> -1   */
    CHECK_EQ(sbus_to_us(172u), 1000);
    CHECK_EQ(sbus_to_us(173u), 1001);    /* +0.61   -> +1   */
    CHECK_EQ(sbus_to_us(992u), 1500);
    CHECK_EQ(sbus_to_us(1811u), 2000);
    CHECK_EQ(sbus_to_us(1812u), 2001);
    CHECK_EQ(sbus_to_us(2047u), 2144);
    /* Mirror pairs about 172 give mirror offsets. */
    for (unsigned d = 0; d <= 172u; ++d) {
        CHECK_EQ((int)sbus_to_us((uint16_t)(172u + d)) - 1000,
                 1000 - (int)sbus_to_us((uint16_t)(172u - d)));
    }
}

/* ------------------------------------------------------------ the encoder */

/* A frame with every field set from arguments, for the encoder tests. */
static sbus_frame_t frame_of(const uint16_t *ch, uint8_t flags)
{
    sbus_frame_t f;
    memset(&f, 0, sizeof(f));
    memcpy(f.channel, ch, sizeof(f.channel));
    f.ch17       = (flags & SBUS_FLAG_CH17) != 0u;
    f.ch18       = (flags & SBUS_FLAG_CH18) != 0u;
    f.frame_lost = (flags & SBUS_FLAG_FRAME_LOST) != 0u;
    f.failsafe   = (flags & SBUS_FLAG_FAILSAFE) != 0u;
    return f;
}

/* Byte by byte, so a failure names the byte. */
static bool same_bytes(const char *name, const uint8_t *got,
                       const uint8_t *want)
{
    for (unsigned i = 0; i < SBUS_FRAME_BYTES; ++i) {
        if (got[i] != want[i]) {
            T_FAIL("%s: byte %u is 0x%02X, want 0x%02X", name, i, got[i],
                   want[i]);
            return false;
        }
    }
    return true;
}

/*
 * Two frames whose bytes were computed apart from this module: the sixteen
 * values shifted into one 176-bit integer written out least significant
 * byte first, then read back through the published per-channel expressions
 * (channel 2 is data byte 2 shifted down three bits ORed with data byte 3
 * shifted up five, and so on) before the bytes were taken.
 */
TEST_CASE(the_encoder_writes_the_published_layout_byte_for_byte)
{
    /* Every channel at 1024: bit 10 alone, which walks through the bytes. */
    static const uint8_t centre[SBUS_FRAME_BYTES] = {
        0x0F, 0x00, 0x04, 0x20, 0x00, 0x01, 0x08, 0x40, 0x00, 0x02, 0x10,
        0x80, 0x00, 0x04, 0x20, 0x00, 0x01, 0x08, 0x40, 0x00, 0x02, 0x10,
        0x80, 0x00, 0x00,
    };
    /* Both fixed points, both raw limits, a false header (0x0F in channel
     * 9), alternating bits, CH17 and failsafe. */
    static const uint16_t mixed_ch[SBUS_CHANNELS] = {
        172, 992, 1811, 0, 2047, 1, 1024, 1023,
        0x0F, 0x555, 0x2AA, 1500, 300, 1700, 7, 2040,
    };
    static const uint8_t mixed[SBUS_FRAME_BYTES] = {
        0x0F, 0xAC, 0x00, 0xDF, 0xC4, 0x01, 0xF0, 0xFF, 0x00, 0x00, 0xF0,
        0x7F, 0x0F, 0xA8, 0xAA, 0xAA, 0xB8, 0xCB, 0x12, 0x52, 0x1F, 0x00,
        0xFF, 0x09, 0x00,
    };
    uint16_t ch[SBUS_CHANNELS];
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        ch[c] = 1024u;
    }
    uint8_t got[SBUS_FRAME_BYTES];

    sbus_frame_t f = frame_of(ch, 0);
    CHECK(sbus_encode(&f, got));
    same_bytes("all 1024", got, centre);

    f = frame_of(mixed_ch, SBUS_FLAG_CH17 | SBUS_FLAG_FAILSAFE);
    CHECK(sbus_encode(&f, got));
    same_bytes("mixed", got, mixed);

    /* The test's own packer agrees, flags and footer included. */
    uint8_t want[SBUS_FRAME_BYTES];
    make(want, mixed_ch, SBUS_FLAG_CH17 | SBUS_FLAG_FAILSAFE, SBUS_FOOTER);
    same_bytes("mixed against make()", got, want);
}

/*
 * The two ends of the field.  The output is filled with 0xA5 first, so a
 * byte the encoder does not write shows up rather than reading as zero.
 */
TEST_CASE(all_zero_and_all_2047_fill_the_payload_and_nothing_else)
{
    uint16_t ch[SBUS_CHANNELS];
    uint8_t got[SBUS_FRAME_BYTES];
    uint8_t want[SBUS_FRAME_BYTES];

    memset(ch, 0, sizeof(ch));
    sbus_frame_t f = frame_of(ch, 0);
    memset(got, 0xA5, sizeof(got));
    CHECK(sbus_encode(&f, got));
    memset(want, 0x00, sizeof(want));
    want[0] = SBUS_HEADER;
    same_bytes("all zero", got, want);

    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        ch[c] = SBUS_RAW_MAX;
    }
    f = frame_of(ch, 0);
    memset(got, 0xA5, sizeof(got));
    CHECK(sbus_encode(&f, got));
    /* 176 one bits fill the 22 payload bytes exactly. */
    memset(want, 0xFF, sizeof(want));
    want[0]  = SBUS_HEADER;
    want[23] = 0x00;
    want[24] = SBUS_FOOTER;
    same_bytes("all 2047", got, want);
}

/*
 * One flag at a time: its own bit in byte 23, no other byte moved, bits 4
 * to 7 clear, and the decoder reads back that flag alone.
 */
TEST_CASE(each_flag_alone_sets_its_own_bit_and_decodes_alone)
{
    uint16_t ch[SBUS_CHANNELS];
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        ch[c] = (uint16_t)(c * 97u + 300u);
    }
    uint8_t none[SBUS_FRAME_BYTES];
    sbus_frame_t f = frame_of(ch, 0);
    CHECK(sbus_encode(&f, none));
    CHECK_EQ(none[23], 0x00);

    static const uint8_t flags[] = {
        SBUS_FLAG_CH17, SBUS_FLAG_CH18, SBUS_FLAG_FRAME_LOST,
        SBUS_FLAG_FAILSAFE,
    };
    for (size_t i = 0; i < sizeof(flags); ++i) {
        f = frame_of(ch, flags[i]);
        uint8_t got[SBUS_FRAME_BYTES];
        CHECK(sbus_encode(&f, got));
        uint8_t want[SBUS_FRAME_BYTES];
        memcpy(want, none, sizeof(want));
        want[23] = flags[i];
        same_bytes("one flag", got, want);

        sbus_decoder_t d;
        sbus_frame_t back;
        sbus_decoder_reset(&d);
        uint32_t now = 0;
        gap(&now);
        if (feed(&d, got, SBUS_FRAME_BYTES, &now, &back) != 1) {
            T_FAIL("flag 0x%02X: the frame did not decode", flags[i]);
            continue;
        }
        CHECK_EQ(back.ch17, flags[i] == SBUS_FLAG_CH17);
        CHECK_EQ(back.ch18, flags[i] == SBUS_FLAG_CH18);
        CHECK_EQ(back.frame_lost, flags[i] == SBUS_FLAG_FRAME_LOST);
        CHECK_EQ(back.failsafe, flags[i] == SBUS_FLAG_FAILSAFE);
    }

    /* All four together are 0x0F and still leave the upper nibble clear. */
    f = frame_of(ch, 0x0Fu);
    uint8_t all[SBUS_FRAME_BYTES];
    CHECK(sbus_encode(&f, all));
    CHECK_EQ(all[23], 0x0F);
}

/* xorshift32: a fixed sequence, the same on every host. */
static uint32_t next_rand(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/*
 * Random frames through the encoder and back through the decoder, fed as
 * one stream at the real byte spacing with the inter-frame gap.  Every
 * channel and every flag comes back as given.
 */
TEST_CASE(random_frames_encode_and_decode_back_unchanged)
{
    uint32_t seed = 0x5B05C0DEu;
    sbus_decoder_t d;
    sbus_decoder_reset(&d);
    uint32_t now = 0;

    for (unsigned n = 0; n < 2000u; ++n) {
        uint16_t ch[SBUS_CHANNELS];
        for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
            ch[c] = (uint16_t)(next_rand(&seed) & SBUS_RAW_MAX);
        }
        const sbus_frame_t in = frame_of(ch, (uint8_t)(next_rand(&seed)
                                                       & 0x0Fu));
        uint8_t bytes[SBUS_FRAME_BYTES];
        if (!sbus_encode(&in, bytes)) {
            T_FAIL("frame %u was refused", n);
            return;
        }
        sbus_frame_t out;
        gap(&now);
        if (feed(&d, bytes, SBUS_FRAME_BYTES, &now, &out) != 1) {
            T_FAIL("frame %u did not decode", n);
            return;
        }
        if (memcmp(out.channel, in.channel, sizeof(in.channel)) != 0
            || out.ch17 != in.ch17 || out.ch18 != in.ch18
            || out.frame_lost != in.frame_lost
            || out.failsafe != in.failsafe) {
            T_FAIL("frame %u came back different", n);
            return;
        }
    }
    CHECK_EQ(d.frames, 2000);
    CHECK_EQ(d.bad_footer, 0);
    CHECK_EQ(d.resyncs, 0);
}

/*
 * A channel above eleven bits refuses the frame, in every channel position,
 * and leaves the output as it was.  2047 is the last value accepted.
 */
TEST_CASE(a_channel_above_2047_refuses_the_frame_and_writes_nothing)
{
    static const uint16_t bad[] = { SBUS_RAW_MAX + 1u, 4095u, 0xFFFFu };
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            uint16_t ch[SBUS_CHANNELS];
            for (unsigned k = 0; k < SBUS_CHANNELS; ++k) {
                ch[k] = 1000u;
            }
            ch[c] = bad[i];
            const sbus_frame_t f = frame_of(ch, 0x0Fu);
            uint8_t got[SBUS_FRAME_BYTES];
            memset(got, 0xA5, sizeof(got));
            if (sbus_encode(&f, got)) {
                T_FAIL("channel %u = %u was accepted", c, bad[i]);
                return;
            }
            for (unsigned b = 0; b < SBUS_FRAME_BYTES; ++b) {
                if (got[b] != 0xA5u) {
                    T_FAIL("channel %u = %u: byte %u written on refusal", c,
                           bad[i], b);
                    return;
                }
            }
        }
        uint16_t ch[SBUS_CHANNELS];
        memset(ch, 0, sizeof(ch));
        ch[c] = SBUS_RAW_MAX;
        const sbus_frame_t f = frame_of(ch, 0);
        uint8_t got[SBUS_FRAME_BYTES];
        CHECK(sbus_encode(&f, got));
    }

    /* NULL on either side is refused rather than dereferenced. */
    uint16_t ch[SBUS_CHANNELS];
    memset(ch, 0, sizeof(ch));
    const sbus_frame_t f = frame_of(ch, 0);
    uint8_t got[SBUS_FRAME_BYTES];
    CHECK_EQ(sbus_encode(NULL, got), false);
    CHECK_EQ(sbus_encode(&f, NULL), false);
}

TEST_CASE(microseconds_to_raw_hits_the_fixed_points_and_rounds_like_sbus_to_us)
{
    CHECK_EQ(sbus_from_us(1000u), SBUS_RAW_1000US);
    CHECK_EQ(sbus_from_us(2000u), SBUS_RAW_2000US);
    /* 1.639 counts per us, rounded to nearest. */
    CHECK_EQ(sbus_from_us(1001u), 174u);    /* 173.639 */
    CHECK_EQ(sbus_from_us(999u), 170u);     /* 170.361 */
    CHECK_EQ(sbus_from_us(1250u), 582u);    /* 581.75 */
    CHECK_EQ(sbus_from_us(1500u), 992u);    /* 991.5, a half, away from 1000us */
    CHECK_EQ(sbus_from_us(1750u), 1401u);   /* 1401.25 */
    /* The ends of the raw field and the clamp past them. */
    CHECK_EQ(sbus_from_us(895u), 0u);       /* -0.095 */
    CHECK_EQ(sbus_from_us(894u), 0u);       /* -1.734, clamped */
    CHECK_EQ(sbus_from_us(0u), 0u);
    CHECK_EQ(sbus_from_us(2144u), 2047u);   /* 2047.016 */
    CHECK_EQ(sbus_from_us(2145u), 2047u);   /* 2048.655, clamped */
    CHECK_EQ(sbus_from_us(0xFFFFu), 2047u);
    /* Mirror pulses about 1000us give mirror counts about 172, as far as
     * the lower clamp allows.  The only half below 1000us is at 500us,
     * where the clamp gives 0 whichever way the half goes. */
    for (unsigned d = 0; d <= 105u; ++d) {
        CHECK_EQ((int)sbus_from_us((uint16_t)(1000u + d)) - 172,
                 172 - (int)sbus_from_us((uint16_t)(1000u - d)));
    }
}

/*
 * Every pulse width a uint16_t holds.  From 895us to 2144us the decoder's
 * mapping gives back the pulse the encoder's mapping was given; outside
 * that the raw value is clamped to 0 or 2047, the ends of the field.
 * The other way, raw to us to raw, lands within one count.
 */
TEST_CASE(microseconds_round_trip_over_the_whole_raw_range)
{
    for (uint32_t us = 0; us <= 0xFFFFu; ++us) {
        const uint16_t raw = sbus_from_us((uint16_t)us);
        const uint16_t back = sbus_to_us(raw);
        uint16_t want = (uint16_t)us;
        if (us < 895u) {
            want = 895u;
        } else if (us > 2144u) {
            want = 2144u;
        }
        if (raw > SBUS_RAW_MAX || back != want) {
            T_FAIL("%u us -> raw %u -> %u us, want %u", (unsigned)us,
                   raw, back, want);
            return;
        }
        if (us > 0u && raw < sbus_from_us((uint16_t)(us - 1u))) {
            T_FAIL("%u us went backwards", (unsigned)us);
            return;
        }
    }
    for (uint32_t raw = 0; raw <= SBUS_RAW_MAX; ++raw) {
        const int again = (int)sbus_from_us(sbus_to_us((uint16_t)raw));
        if (again - (int)raw > 1 || (int)raw - again > 1) {
            T_FAIL("raw %u -> %u us -> raw %d", (unsigned)raw,
                   sbus_to_us((uint16_t)raw), again);
            return;
        }
    }
    /* Both fixed points survive that direction exactly. */
    CHECK_EQ(sbus_from_us(sbus_to_us(SBUS_RAW_1000US)), SBUS_RAW_1000US);
    CHECK_EQ(sbus_from_us(sbus_to_us(SBUS_RAW_2000US)), SBUS_RAW_2000US);
}

int main(void)
{
    RUN(a_frame_decodes_to_the_channels_that_were_packed);
    RUN(every_bit_of_every_channel_lands_in_its_own_channel);
    RUN(the_extremes_of_the_range_survive);
    RUN(the_flags_come_through_and_failsafe_is_not_just_another_channel);
    RUN(a_stream_joined_mid_frame_resynchronises_on_the_gap);
    RUN(a_false_header_inside_a_payload_is_not_a_frame_start);
    RUN(a_frame_with_the_wrong_tail_is_refused_and_counted_apart);
    RUN(noise_before_a_frame_does_not_eat_it);
    RUN(the_gap_threshold_sits_between_a_byte_and_a_frame);
    RUN(the_microsecond_mapping_hits_its_two_fixed_points);
    RUN(the_microsecond_mapping_rounds_the_same_way_on_both_sides);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    RUN(the_encoder_writes_the_published_layout_byte_for_byte);
    RUN(all_zero_and_all_2047_fill_the_payload_and_nothing_else);
    RUN(each_flag_alone_sets_its_own_bit_and_decodes_alone);
    RUN(random_frames_encode_and_decode_back_unchanged);
    RUN(a_channel_above_2047_refuses_the_frame_and_writes_nothing);
    RUN(microseconds_to_raw_hits_the_fixed_points_and_rounds_like_sbus_to_us);
    RUN(microseconds_round_trip_over_the_whole_raw_range);
    return test_summary("sbus");
}
