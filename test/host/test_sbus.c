/*
 * S.BUS, fed the byte streams a receiver and a noisy line produce.
 *
 * The protocol has no checksum and its header byte is an ordinary channel
 * value, so the failures under test are all about framing: locking onto the
 * middle of a frame, staying locked, and reporting sixteen plausible channels
 * that are all wrong.  Nothing downstream can detect plausible-and-wrong.
 *
 * The encoder is held to the 15 frames of the step 5 research, packed by a
 * script apart from this code, and every frame it writes decodes back.
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
 * of one frame are 120 microseconds apart and frames are at least 4 ms apart,
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
     * 895..2144 us, inside it), so it only guards a caller passing a value
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
    /* 1000 us per 1639 counts: 0.6101 us per count, rounded to nearest. */
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

typedef struct {
    const char *name;
    uint16_t ch[SBUS_CHANNELS];
    uint8_t flags;
    uint8_t footer;
    const char *hex;
} vector_t;

#define ALL(v) { v, v, v, v, v, v, v, v, v, v, v, v, v, v, v, v }
#define MID992 "0F E0 03 1F F8 C0 07 3E F0 81 0F 7C E0 03 1F F8 C0 07 3E F0 81 0F 7C "

/* sbus.md, section 9.1. */
static const vector_t k_vectors[] = {
    { "all 1024", ALL(1024), 0x00, 0x00,
      "0F 00 04 20 00 01 08 40 00 02 10 80 00 04 20 00 01 08 40 00 02 10 80 00 00" },
    { "all 172", ALL(172), 0x00, 0x00,
      "0F AC 60 05 2B 58 C1 0A 56 B0 82 15 AC 60 05 2B 58 C1 0A 56 B0 82 15 00 00" },
    { "all 992", ALL(992), 0x00, 0x00, MID992 "00 00" },
    { "all 1811", ALL(1811), 0x00, 0x00,
      "0F 13 9F F8 C4 27 3E F1 89 4F 7C E2 13 9F F8 C4 27 3E F1 89 4F 7C E2 00 00" },
    { "1 to 16", { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
      0x00, 0x00,
      "0F 01 10 C0 00 08 50 00 03 1C 00 01 09 50 C0 02 18 D0 00 07 3C 00 02 00 00" },
    { "ch1 2047", { 2047 }, 0x00, 0x00,
      "0F FF 07 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" },
    { "ch2 1", { 0, 1 }, 0x00, 0x00,
      "0F 00 08 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" },
    { "ch16 2047", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2047 },
      0x00, 0x00,
      "0F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 E0 FF 00 00" },
    { "1000 1250 1500 1750 us",
      { 172, 582, 992, 1401, 992, 992, 992, 992, 992, 992, 992, 992, 992,
        992, 992, 992 }, 0x00, 0x00,
      "0F AC 30 12 F8 F2 0A 3E F0 81 0F 7C E0 03 1F F8 C0 07 3E F0 81 0F 7C 00 00" },
    { "failsafe and frame lost", ALL(992), 0x0C, 0x00, MID992 "0C 00" },
    { "ch17 and ch18", ALL(992), 0x03, 0x00, MID992 "03 00" },
    { "footer 0x04", ALL(992), 0x00, 0x04, MID992 "00 04" },
    { "footer 0x14", ALL(992), 0x00, 0x14, MID992 "00 14" },
    { "footer 0x24", ALL(992), 0x00, 0x24, MID992 "00 24" },
    { "footer 0x34", ALL(992), 0x00, 0x34, MID992 "00 34" },
};

#define N_VECTORS (sizeof(k_vectors) / sizeof(k_vectors[0]))

static void parse_hex(const char *hex, uint8_t *out)
{
    unsigned n = 0;

    while (*hex != '\0' && n < SBUS_FRAME_BYTES) {
        if (*hex == ' ') {
            ++hex;
            continue;
        }
        unsigned v = 0;
        for (unsigned i = 0; i < 2u; ++i, ++hex) {
            const char c = *hex;
            v = v * 16u + (unsigned)(c <= '9' ? c - '0' : c - 'A' + 10);
        }
        out[n++] = (uint8_t)v;
    }
}

static sbus_frame_t frame_of(const vector_t *v)
{
    sbus_frame_t f;

    memset(&f, 0, sizeof(f));
    memcpy(f.channel, v->ch, sizeof(f.channel));
    f.ch17 = (v->flags & SBUS_FLAG_CH17) != 0u;
    f.ch18 = (v->flags & SBUS_FLAG_CH18) != 0u;
    f.frame_lost = (v->flags & SBUS_FLAG_FRAME_LOST) != 0u;
    f.failsafe = (v->flags & SBUS_FLAG_FAILSAFE) != 0u;
    return f;
}

TEST_CASE(the_fifteen_research_frames_encode_byte_for_byte_and_decode_back)
{
    CHECK_EQ(N_VECTORS, 15);
    for (size_t i = 0; i < N_VECTORS; ++i) {
        const vector_t *v = &k_vectors[i];
        const sbus_frame_t in = frame_of(v);
        uint8_t want[SBUS_FRAME_BYTES];
        uint8_t got[SBUS_FRAME_BYTES];
        uint8_t ref[SBUS_FRAME_BYTES];

        parse_hex(v->hex, want);
        sbus_encode(&in, v->footer, got);
        if (memcmp(got, want, SBUS_FRAME_BYTES) != 0) {
            T_FAIL("%s: the encoded frame differs from the vector", v->name);
        }
        /* The test's own packer, written apart from the encoder. */
        make(ref, v->ch, v->flags, v->footer);
        if (memcmp(ref, want, SBUS_FRAME_BYTES) != 0) {
            T_FAIL("%s: the test's packer differs from the vector", v->name);
        }

        sbus_decoder_t d;
        sbus_frame_t out;
        uint32_t now = 1000;
        sbus_decoder_reset(&d);
        gap(&now);
        CHECK_EQ(feed(&d, got, SBUS_FRAME_BYTES, &now, &out), 1);
        if (memcmp(out.channel, v->ch, sizeof(out.channel)) != 0) {
            T_FAIL("%s: the decoded channels differ", v->name);
        }
        CHECK_EQ(out.ch17, in.ch17);
        CHECK_EQ(out.ch18, in.ch18);
        CHECK_EQ(out.frame_lost, in.frame_lost);
        CHECK_EQ(out.failsafe, in.failsafe);
    }
}

TEST_CASE(each_flag_has_its_own_bit_and_the_upper_nibble_stays_clear)
{
    static const uint8_t k_bit[4] = { SBUS_FLAG_CH17, SBUS_FLAG_CH18,
                                      SBUS_FLAG_FRAME_LOST,
                                      SBUS_FLAG_FAILSAFE };
    CHECK_EQ(SBUS_FLAG_CH17, 0x01);
    CHECK_EQ(SBUS_FLAG_CH18, 0x02);
    CHECK_EQ(SBUS_FLAG_FRAME_LOST, 0x04);
    CHECK_EQ(SBUS_FLAG_FAILSAFE, 0x08);
    for (unsigned mask = 0; mask < 16u; ++mask) {
        sbus_frame_t f;
        uint8_t b[SBUS_FRAME_BYTES];
        unsigned want = 0;

        memset(&f, 0, sizeof(f));
        f.ch17 = (mask & 1u) != 0u;
        f.ch18 = (mask & 2u) != 0u;
        f.frame_lost = (mask & 4u) != 0u;
        f.failsafe = (mask & 8u) != 0u;
        sbus_encode(&f, SBUS_FOOTER, b);
        for (unsigned i = 0; i < 4u; ++i) {
            if (mask & (1u << i)) {
                want |= k_bit[i];
            }
        }
        CHECK_EQ(b[23], want);
        CHECK_EQ(b[23] & 0xF0u, 0u);
        /* No flag leaks into the channel payload. */
        for (unsigned i = 1; i < 23u; ++i) {
            CHECK_EQ(b[i], 0u);
        }
    }
}

TEST_CASE(channel_limits_go_out_as_given_and_out_of_range_values_are_clamped)
{
    sbus_frame_t f;
    sbus_frame_t out;
    uint8_t b[SBUS_FRAME_BYTES];
    sbus_decoder_t d;
    uint32_t now = 0;

    for (unsigned ch = 0; ch < SBUS_CHANNELS; ++ch) {
        memset(&f, 0, sizeof(f));
        f.channel[ch] = 2047u;
        sbus_encode(&f, 0, b);
        sbus_decoder_reset(&d);
        gap(&now);
        CHECK_EQ(feed(&d, b, SBUS_FRAME_BYTES, &now, &out), 1);
        for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
            CHECK_EQ(out.channel[c], c == ch ? 2047u : 0u);
        }
        /* Above eleven bits: clamped to 2047, never wrapped and never
         * spilled into the next channel. */
        f.channel[ch] = (uint16_t)(ch % 2u == 0u ? 2048u : 0xFFFFu);
        sbus_encode(&f, 0, b);
        sbus_decoder_reset(&d);
        gap(&now);
        CHECK_EQ(feed(&d, b, SBUS_FRAME_BYTES, &now, &out), 1);
        for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
            CHECK_EQ(out.channel[c], c == ch ? 2047u : 0u);
        }
    }
    /* All at 0 is a header and 24 zero bytes. */
    memset(&f, 0, sizeof(f));
    sbus_encode(&f, 0, b);
    CHECK_EQ(b[0], SBUS_HEADER);
    for (unsigned i = 1; i < SBUS_FRAME_BYTES; ++i) {
        CHECK_EQ(b[i], 0u);
    }
    /* NULL arguments write nothing. */
    memset(b, 0xA5, sizeof(b));
    sbus_encode(NULL, 0, b);
    CHECK_EQ(b[0], 0xA5);
    sbus_encode(&f, 0, NULL);
}

TEST_CASE(each_footer_is_byte_24_and_the_decoder_takes_every_one_it_should)
{
    static const uint8_t k_footers[5] = { 0x00, 0x04, 0x14, 0x24, 0x34 };
    sbus_frame_t f;

    memset(&f, 0, sizeof(f));
    for (unsigned i = 0; i < 5u; ++i) {
        uint8_t b[SBUS_FRAME_BYTES];
        sbus_decoder_t d;
        uint32_t now = 0;

        sbus_encode(&f, k_footers[i], b);
        CHECK_EQ(b[24], k_footers[i]);
        sbus_decoder_reset(&d);
        gap(&now);
        CHECK_EQ(feed(&d, b, SBUS_FRAME_BYTES, &now, NULL), 1);
        CHECK_EQ(d.bad_footer, 0u);
    }
}

TEST_CASE(microseconds_to_raw_meets_the_decoder_on_both_fixed_points)
{
    CHECK_EQ(sbus_from_us(1000u), 172u);
    CHECK_EQ(sbus_from_us(1250u), 582u);    /* 581.75 */
    CHECK_EQ(sbus_from_us(1500u), 992u);    /* 991.5, half away from 0 */
    CHECK_EQ(sbus_from_us(1750u), 1401u);   /* 1401.25 */
    CHECK_EQ(sbus_from_us(2000u), 1811u);
    CHECK_EQ(sbus_from_us(999u), 170u);     /* 170.361 */
    /* Clamped at both ends of eleven bits. */
    CHECK_EQ(sbus_from_us(895u), 0u);       /* -0.105 */
    CHECK_EQ(sbus_from_us(0u), 0u);
    CHECK_EQ(sbus_from_us(2144u), 2047u);   /* 2047.016 */
    CHECK_EQ(sbus_from_us(2145u), 2047u);
    CHECK_EQ(sbus_from_us(0xFFFFu), 2047u);
    /* The round trip holds wherever the raw value is in range. */
    for (unsigned us = 896; us <= 2143u; ++us) {
        const uint16_t back = sbus_to_us(sbus_from_us((uint16_t)us));

        if (back != us) {
            T_FAIL("%u us comes back as %u us", us, back);
        }
    }
    /* Monotonic over the whole input range. */
    for (unsigned us = 1; us <= 0xFFFFu; ++us) {
        if (sbus_from_us((uint16_t)us) < sbus_from_us((uint16_t)(us - 1u))) {
            T_FAIL("not monotonic at %u us", us);
        }
    }
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
    RUN(the_fifteen_research_frames_encode_byte_for_byte_and_decode_back);
    RUN(each_flag_has_its_own_bit_and_the_upper_nibble_stays_clear);
    RUN(channel_limits_go_out_as_given_and_out_of_range_values_are_clamped);
    RUN(each_footer_is_byte_24_and_the_decoder_takes_every_one_it_should);
    RUN(microseconds_to_raw_meets_the_decoder_on_both_fixed_points);
    return test_summary("sbus");
}
