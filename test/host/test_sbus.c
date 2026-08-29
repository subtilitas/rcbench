/*
 * S.BUS, fed the byte streams a receiver and a noisy line produce.
 *
 * The protocol has no checksum and its header byte is an ordinary channel
 * value, so the interesting failures are all about framing: locking onto the
 * middle of a frame, staying locked, and reporting sixteen plausible channels
 * that are all wrong. Plausible-and-wrong is the failure worth testing,
 * because nothing downstream can detect it.
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
 * The failure this protocol invites. 0x0F is an ordinary channel value, so a
 * decoder that frames on the header alone will start mid-frame -- and once it
 * has, the next 0x0F it sees is in the same wrong place, so it stays wrong
 * for ever while reporting sixteen plausible numbers.
 *
 * The gap is what breaks it, and this test starts the stream deliberately
 * mid-frame to prove the gap and not the header is doing the work.
 */
TEST_CASE(a_stream_joined_mid_frame_resynchronises_on_the_gap)
{
    uint16_t ch[SBUS_CHANNELS];
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        ch[c] = (uint16_t)(1000 + c);
    }
    /* Channel 0 = 0x0F so the payload really does contain a false header. */
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
     * Ten bytes of a frame and then the transmitter stops -- a receiver
     * powered off mid-frame, or a cable pulled.  Those ten sit in the buffer
     * looking like the start of something.
     */
    feed(&d, frame, 10, &now, &f);

    /* Then silence, and a whole good frame. */
    gap(&now);
    const int got = feed(&d, frame, SBUS_FRAME_BYTES, &now, &f);

    /*
     * Exactly one frame, and it is the right one.  Without the gap resetting
     * the buffer, the ten stale bytes plus the first fifteen of this frame
     * would have made a twenty-five byte "frame" of nonsense, and the real
     * frame's last ten bytes would have started the next one -- misaligned
     * for ever after.
     */
    CHECK_EQ(got, 1);
    CHECK_EQ(d.frames, 1);
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        CHECK_EQ(f.channel[c], ch[c]);
    }

    /* And the misalignment really would have happened: the same stream with
     * the gaps removed does not produce this frame. */
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
 * so the threshold has to sit between -- and a threshold below the byte
 * spacing would restart the frame on every byte, which is the same as having
 * no framing at all.
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
     * The clamp is unreachable from the decoder -- eleven bits map to
     * 895..2144 us, comfortably inside it -- so it is only there for a caller
     * passing something the decoder would never produce.  Tested directly,
     * because an untested guard is a guess.
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
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("sbus");
}
