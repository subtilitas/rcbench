/*
 * The OpenYGE codec, fed the frames a real line produces: good ones, short
 * ones, bit-flipped ones, and good ones sitting behind noise that happens to
 * look like a sync byte.
 *
 * Frames are built here from raw bytes rather than with openyge_encode(), so
 * the decoder is held to the specification rather than to its own encoder --
 * two halves of one mistake agree with each other perfectly.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_crc.h"
#include "openyge.h"

/* Build an ESC-to-master frame by hand.  `version` chooses the header length,
 * which is the thing a legacy ESC changes. */
static size_t make_tele(uint8_t *out, uint8_t version, uint8_t type,
                        uint8_t seq, uint8_t device, const uint8_t *payload)
{
    const size_t hdr = (version >= 3) ? 6u : 4u;
    const size_t total = hdr + OPENYGE_TELEMETRY_BYTES + 2u;
    out[0] = OPENYGE_SYNC;
    out[1] = version;
    out[2] = type;
    out[3] = (uint8_t)total;
    if (hdr == 6) {
        out[4] = seq;
        out[5] = device;
    }
    memcpy(out + hdr, payload, OPENYGE_TELEMETRY_BYTES);
    const uint16_t crc = link_crc(OPENYGE_CRC_INIT, out, total - 2u);
    out[total - 2u] = (uint8_t)(crc & 0xFFu);
    out[total - 1u] = (uint8_t)(crc >> 8);
    return total;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/* A payload with every field set to something distinguishable. */
static void sample_payload(uint8_t *p)
{
    memset(p, 0, OPENYGE_TELEMETRY_BYTES);
    p[1] = 40 + 63;                  /* 63 C */
    put16(p + 2, 2512);              /* 25.12 V */
    put16(p + 4, 4310);              /* 43.10 A */
    put16(p + 6, 1234);              /* mAh */
    put16(p + 8, 4200);              /* 42,000 eRPM */
    p[10] = (uint8_t)(int8_t)72;     /* pwm % */
    p[11] = (uint8_t)(int8_t)70;     /* throttle % */
    put16(p + 12, 8100);             /* 8.100 V BEC */
    put16(p + 14, 1900);             /* 1.900 A BEC */
    p[16] = 40 + 51;                 /* BEC 51 C */
    p[17] = 0x0E;                    /* RUNNING */
    p[18] = 40 + 44;                 /* cap 44 C */
    p[19] = 40 + 39;                 /* aux 39 C */
    p[20] = 0x5A;                    /* status2, carried not read */
    put16(p + 22, 20);               /* pidx */
    put16(p + 24, 14);               /* pdata */
}

static bool feed(openyge_decoder_t *d, const uint8_t *b, size_t n,
                 openyge_frame_t *out)
{
    bool got = false;
    for (size_t i = 0; i < n; ++i) {
        if (openyge_decode_byte(d, b[i], out)) {
            got = true;
        }
    }
    return got;
}

/*
 * The seed, pinned to its published check value.  This is the one number that
 * distinguishes OpenYGE's CRC from the panel link's, and getting it wrong
 * rejects every frame -- which looks exactly like a wrong baud rate.
 */
TEST_CASE(the_crc_seed_is_xmodem_and_not_the_links_own)
{
    CHECK_EQ(link_crc(OPENYGE_CRC_INIT, "123456789", 9), OPENYGE_CRC_CHECK);
    CHECK_EQ(OPENYGE_CRC_CHECK, 0x31C3);
    /* And the link's own seed gives the other published value, so the two are
     * genuinely different variants rather than a copied constant. */
    CHECK_EQ(link_crc(LINK_CRC_INIT, "123456789", 9), LINK_CRC_CHECK);
    CHECK(OPENYGE_CRC_INIT != LINK_CRC_INIT);
}

TEST_CASE(a_v3_telemetry_frame_decodes_to_its_numbers)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], buf[64];
    sample_payload(pay);
    const size_t n = make_tele(buf, 3, OPENYGE_FT_TELE_RESP, 0x2A, 0x01, pay);
    CHECK_EQ(n, 34);   /* 6 header + 26 payload + 2 CRC */

    openyge_decoder_t d;
    openyge_frame_t f;
    openyge_decoder_reset(&d);
    CHECK(feed(&d, buf, n, &f));
    CHECK_EQ(d.frames, 1);
    CHECK_EQ(d.crc_errors, 0);
    CHECK_EQ(f.hdr.legacy, false);
    CHECK_EQ(f.hdr.seq, 0x2A);
    CHECK_EQ(f.hdr.device, 0x01);

    openyge_telemetry_t t;
    CHECK(openyge_telemetry_parse(&f, &t));
    CHECK_NEAR(t.volts, 25.12f, 0.001f);
    CHECK_NEAR(t.amps, 43.10f, 0.001f);
    CHECK_EQ(t.consumption_mah, 1234);
    CHECK_EQ(t.erpm, 42000);          /* the x10 scale */
    CHECK_EQ(t.pwm_pct, 72);
    CHECK_EQ(t.throttle_pct, 70);
    CHECK_NEAR(t.bec_volts, 8.1f, 0.001f);
    CHECK_NEAR(t.bec_amps, 1.9f, 0.001f);
    CHECK_EQ(t.temp_c, 63);
    CHECK_EQ(t.bec_temp_c, 51);
    CHECK_EQ(t.cap_temp_c, 44);
    CHECK_EQ(t.aux_temp_c, 39);
    CHECK_EQ(t.status1, 0x0E);
    CHECK_EQ(t.status2, 0x5A);
    CHECK_EQ(t.pidx, 20);
    CHECK_EQ(t.pdata, 14);
}

/* Temperatures are offset so that −40 fits in a byte; the ones below zero are
 * the half of that range nothing else exercises. */
TEST_CASE(temperatures_below_zero_decode_as_negative)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], buf[64];
    sample_payload(pay);
    pay[1] = 0;      /* -40 C */
    pay[16] = 35;    /* -5 C */
    pay[18] = 255;   /* 215 C, the top of the range */
    const size_t n = make_tele(buf, 3, OPENYGE_FT_TELE_AUTO, 0, 1, pay);

    openyge_decoder_t d; openyge_frame_t f; openyge_telemetry_t t;
    openyge_decoder_reset(&d);
    CHECK(feed(&d, buf, n, &f));
    CHECK(openyge_telemetry_parse(&f, &t));
    CHECK_EQ(t.temp_c, -40);
    CHECK_EQ(t.bec_temp_c, -5);
    CHECK_EQ(t.cap_temp_c, 215);
}

/* pwm and throttle are signed, and a regenerating ESC really does report a
 * negative duty. */
TEST_CASE(negative_duty_and_throttle_survive_the_decode)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], buf[64];
    sample_payload(pay);
    pay[10] = (uint8_t)(int8_t)-30;
    pay[11] = (uint8_t)(int8_t)-100;
    const size_t n = make_tele(buf, 3, OPENYGE_FT_TELE_AUTO, 0, 1, pay);

    openyge_decoder_t d; openyge_frame_t f; openyge_telemetry_t t;
    openyge_decoder_reset(&d);
    CHECK(feed(&d, buf, n, &f));
    CHECK(openyge_telemetry_parse(&f, &t));
    CHECK_EQ(t.pwm_pct, -30);
    CHECK_EQ(t.throttle_pct, -100);
}

/*
 * A pre-v3 ESC uses a four-byte header, so the payload starts two bytes
 * earlier.  Read it with the v3 offsets and every field is shifted -- which
 * would show up as plausible-looking nonsense rather than as a failure.
 */
TEST_CASE(a_legacy_frame_has_a_shorter_header_and_still_parses)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], buf[64];
    sample_payload(pay);
    const size_t n = make_tele(buf, 2, OPENYGE_FT_TELE_AUTO, 0, 0, pay);
    CHECK_EQ(n, 32);   /* 4 header + 26 payload + 2 CRC */

    openyge_decoder_t d; openyge_frame_t f; openyge_telemetry_t t;
    openyge_decoder_reset(&d);
    CHECK(feed(&d, buf, n, &f));
    CHECK_EQ(f.hdr.legacy, true);
    CHECK_EQ(f.hdr.seq, 0);      /* it carries neither */
    CHECK_EQ(f.hdr.device, 0);
    CHECK(openyge_telemetry_parse(&f, &t));
    CHECK_NEAR(t.volts, 25.12f, 0.001f);
    CHECK_EQ(t.erpm, 42000);
}

TEST_CASE(a_bit_flip_anywhere_is_caught_by_the_crc)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], buf[64];
    sample_payload(pay);
    const size_t n = make_tele(buf, 3, OPENYGE_FT_TELE_RESP, 1, 1, pay);

    for (size_t i = 0; i < n; ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            uint8_t bad[64];
            memcpy(bad, buf, n);
            bad[i] ^= (uint8_t)(1u << bit);
            if (memcmp(bad, buf, n) == 0) {
                continue;
            }
            openyge_decoder_t d; openyge_frame_t f;
            openyge_decoder_reset(&d);
            if (feed(&d, bad, n, &f)) {
                T_FAIL("byte %zu bit %d flipped and the frame was accepted",
                       i, bit);
            }
        }
    }
}

TEST_CASE(a_truncated_frame_is_never_accepted)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], buf[64];
    sample_payload(pay);
    const size_t n = make_tele(buf, 3, OPENYGE_FT_TELE_RESP, 1, 1, pay);
    for (size_t cut = 1; cut < n; ++cut) {
        openyge_decoder_t d; openyge_frame_t f;
        openyge_decoder_reset(&d);
        if (feed(&d, buf, cut, &f)) {
            T_FAIL("%zu of %zu bytes was accepted as a frame", cut, n);
        }
    }
}

/*
 * The case that breaks a decoder which commits to the first sync it sees:
 * noise containing 0xA5 and a plausible length, immediately in front of a
 * real frame that is already whole in the buffer.
 */
TEST_CASE(a_real_frame_behind_a_false_sync_is_still_found)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], real[64], line[128];
    sample_payload(pay);
    const size_t n = make_tele(real, 3, OPENYGE_FT_TELE_RESP, 7, 1, pay);

    /* Noise: a sync, a version, a valid frame type and a plausible length. */
    const uint8_t noise[] = { 0x11, OPENYGE_SYNC, 3, OPENYGE_FT_TELE_RESP, 34,
                              0x00, 0x00, 0x99 };
    memcpy(line, noise, sizeof(noise));
    memcpy(line + sizeof(noise), real, n);

    openyge_decoder_t d; openyge_frame_t f;
    openyge_decoder_reset(&d);
    CHECK(feed(&d, line, sizeof(noise) + n, &f));
    CHECK_EQ(d.frames, 1);
    CHECK_EQ(f.hdr.seq, 7);
    CHECK(d.resyncs > 0);
}

/* A frame whose CRC holds but whose length disagrees with its type is a
 * version mismatch or a bug at the far end, not line noise -- and it is
 * counted apart from noise so the two can be told apart on the analyser. */
/*
 * The harder version of the same case: the false candidate claims a length
 * longer than anything that has arrived, so it stays *incomplete* rather than
 * failing its CRC. A decoder that stops scanning at the first candidate which
 * could still become a frame waits behind it forever, while a whole real
 * frame sits in the buffer immediately after.
 */
TEST_CASE(a_hungry_false_candidate_does_not_swallow_the_frame_behind_it)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], real[64], line[192];
    sample_payload(pay);
    const size_t n = make_tele(real, 3, OPENYGE_FT_TELE_RESP, 11, 1, pay);

    /* A sync, a valid type, and a length of 140 -- the maximum, so it cannot
     * complete until far more has arrived than ever will. */
    const uint8_t noise[] = { OPENYGE_SYNC, 3, OPENYGE_FT_TELE_RESP, 140,
                              0x00, 0x00 };
    memcpy(line, noise, sizeof(noise));
    memcpy(line + sizeof(noise), real, n);

    openyge_decoder_t d; openyge_frame_t f;
    openyge_decoder_reset(&d);
    CHECK(feed(&d, line, sizeof(noise) + n, &f));
    CHECK_EQ(f.hdr.seq, 11);
    CHECK_EQ(d.frames, 1);
}

TEST_CASE(a_verifying_frame_with_an_impossible_shape_is_rejected_separately)
{
    /* A telemetry response carrying a 4-byte control payload. */
    uint8_t buf[32];
    const size_t total = 6 + 4 + 2;
    buf[0] = OPENYGE_SYNC; buf[1] = 3; buf[2] = OPENYGE_FT_TELE_RESP;
    buf[3] = (uint8_t)total; buf[4] = 1; buf[5] = 1;
    memset(buf + 6, 0, 4);
    const uint16_t crc = link_crc(OPENYGE_CRC_INIT, buf, total - 2);
    buf[total - 2] = (uint8_t)(crc & 0xFFu);
    buf[total - 1] = (uint8_t)(crc >> 8);

    openyge_decoder_t d; openyge_frame_t f;
    openyge_decoder_reset(&d);
    CHECK_EQ(feed(&d, buf, total, &f), false);
    CHECK_EQ(d.rejected, 1);
    CHECK_EQ(d.crc_errors, 0);   /* the CRC was fine; the shape was not */
}

TEST_CASE(an_unknown_frame_type_is_refused)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], buf[64];
    sample_payload(pay);
    size_t n = make_tele(buf, 3, 0x07, 1, 1, pay);   /* 0x07 is not a type */
    openyge_decoder_t d; openyge_frame_t f;
    openyge_decoder_reset(&d);
    CHECK_EQ(feed(&d, buf, n, &f), false);

    /*
     * And one whose payload length is the size an unknown type would be
     * assumed to carry, so that the *type* check is what refuses it rather
     * than the shape check happening to. Without this, deleting the type
     * check entirely leaves the suite green.
     */
    uint8_t ctl[32];
    const size_t total = 6 + OPENYGE_CONTROL_BYTES + 2;
    ctl[0] = OPENYGE_SYNC; ctl[1] = 3; ctl[2] = 0x77;
    ctl[3] = (uint8_t)total; ctl[4] = 1; ctl[5] = 1;
    memset(ctl + 6, 0, OPENYGE_CONTROL_BYTES);
    const uint16_t crc = link_crc(OPENYGE_CRC_INIT, ctl, total - 2);
    ctl[total - 2] = (uint8_t)(crc & 0xFFu);
    ctl[total - 1] = (uint8_t)(crc >> 8);

    openyge_decoder_reset(&d);
    CHECK_EQ(feed(&d, ctl, total, &f), false);
    CHECK_EQ(d.frames, 0);
}

TEST_CASE(a_request_carries_a_control_payload_and_not_telemetry)
{
    uint8_t buf[32];
    const size_t n = openyge_build_param_write(buf, sizeof(buf), 1, 9, 20, 14);
    CHECK_EQ(n, 12);   /* 6 + 4 + 2 */
    CHECK_EQ(buf[5] & OPENYGE_DEV_MASTER, OPENYGE_DEV_MASTER);

    openyge_decoder_t d; openyge_frame_t f;
    openyge_decoder_reset(&d);
    CHECK(feed(&d, buf, n, &f));
    CHECK_EQ(f.hdr.type, OPENYGE_FT_WRITE_PARAM_REQ);
    CHECK_EQ(f.hdr.seq, 9);
    CHECK_EQ(openyge_frame_has_telemetry(&f), false);

    uint16_t idx = 0, val = 0;
    CHECK(openyge_control_parse(&f, &idx, &val));
    CHECK_EQ(idx, 20);
    CHECK_EQ(val, 14);

    openyge_telemetry_t t;
    CHECK_EQ(openyge_telemetry_parse(&f, &t), false);
}

TEST_CASE(a_telemetry_request_sends_both_words_zero)
{
    uint8_t buf[32];
    const size_t n = openyge_build_telemetry_request(buf, sizeof(buf), 1, 3);
    openyge_decoder_t d; openyge_frame_t f;
    openyge_decoder_reset(&d);
    CHECK(feed(&d, buf, n, &f));
    uint16_t idx = 0xFFFF, val = 0xFFFF;
    CHECK(openyge_control_parse(&f, &idx, &val));
    CHECK_EQ(idx, 0);
    CHECK_EQ(val, 0);
}

/*
 * The reference sets its length field and only then checks whether the frame
 * fits, so a request that does not fit leaves a length claiming more than was
 * written and the next transmission sends whatever follows it.  Here the
 * bounds check happens before anything is written at all.
 */
TEST_CASE(a_frame_that_does_not_fit_writes_nothing_at_all)
{
    uint8_t buf[8];
    memset(buf, 0xEE, sizeof(buf));
    CHECK_EQ(openyge_build_telemetry_request(buf, sizeof(buf), 1, 1), 0);
    for (size_t i = 0; i < sizeof(buf); ++i) {
        if (buf[i] != 0xEE) {
            T_FAIL("byte %zu was written despite the frame not fitting", i);
        }
    }
}

TEST_CASE(encode_refuses_a_payload_that_does_not_match_the_type)
{
    uint8_t buf[64], pay[OPENYGE_TELEMETRY_BYTES] = { 0 };
    /* A request with a telemetry-sized payload, and the reverse. */
    CHECK_EQ(openyge_encode(buf, sizeof(buf), OPENYGE_FT_TELE_REQ, 1, 1,
                            pay, OPENYGE_TELEMETRY_BYTES), 0);
    CHECK_EQ(openyge_encode(buf, sizeof(buf), OPENYGE_FT_TELE_RESP, 1, 1,
                            pay, OPENYGE_CONTROL_BYTES), 0);
    CHECK_EQ(openyge_encode(buf, sizeof(buf), 0x07, 1, 1, pay, 4), 0);
}

/* Two frames back to back, and a byte of rubbish between them. */
TEST_CASE(back_to_back_frames_are_both_delivered)
{
    uint8_t pay[OPENYGE_TELEMETRY_BYTES], a[64], b[64], line[192];
    sample_payload(pay);
    const size_t na = make_tele(a, 3, OPENYGE_FT_TELE_RESP, 1, 1, pay);
    pay[17] = 0x00;
    const size_t nb = make_tele(b, 3, OPENYGE_FT_TELE_RESP, 2, 1, pay);

    size_t n = 0;
    memcpy(line, a, na); n += na;
    line[n++] = 0x00;
    memcpy(line + n, b, nb); n += nb;

    openyge_decoder_t d; openyge_frame_t f;
    openyge_decoder_reset(&d);
    int seqs[4] = { -1, -1, -1, -1 }, got = 0;
    for (size_t i = 0; i < n; ++i) {
        if (openyge_decode_byte(&d, line[i], &f) && got < 4) {
            seqs[got++] = f.hdr.seq;
        }
    }
    CHECK_EQ(got, 2);
    CHECK_EQ(seqs[0], 1);
    CHECK_EQ(seqs[1], 2);
    CHECK_EQ(d.frames, 2);
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    openyge_frame_t f;
    memset(&f, 0, sizeof(f));
    openyge_decoder_reset(NULL);
    CHECK_EQ(openyge_decode_byte(NULL, 0xA5, &f), false);
    CHECK_EQ(openyge_frame_has_telemetry(NULL), false);
    CHECK_EQ(openyge_telemetry_parse(NULL, NULL), false);
    CHECK_EQ(openyge_control_parse(NULL, NULL, NULL), false);
    CHECK_EQ(openyge_encode(NULL, 64, OPENYGE_FT_TELE_REQ, 1, 1, "abcd", 4), 0);
}

int main(void)
{
    RUN(the_crc_seed_is_xmodem_and_not_the_links_own);
    RUN(a_v3_telemetry_frame_decodes_to_its_numbers);
    RUN(temperatures_below_zero_decode_as_negative);
    RUN(negative_duty_and_throttle_survive_the_decode);
    RUN(a_legacy_frame_has_a_shorter_header_and_still_parses);
    RUN(a_bit_flip_anywhere_is_caught_by_the_crc);
    RUN(a_truncated_frame_is_never_accepted);
    RUN(a_real_frame_behind_a_false_sync_is_still_found);
    RUN(a_hungry_false_candidate_does_not_swallow_the_frame_behind_it);
    RUN(a_verifying_frame_with_an_impossible_shape_is_rejected_separately);
    RUN(an_unknown_frame_type_is_refused);
    RUN(a_request_carries_a_control_payload_and_not_telemetry);
    RUN(a_telemetry_request_sends_both_words_zero);
    RUN(a_frame_that_does_not_fit_writes_nothing_at_all);
    RUN(encode_refuses_a_payload_that_does_not_match_the_type);
    RUN(back_to_back_frames_are_both_delivered);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("openyge_frame");
}
