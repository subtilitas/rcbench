/*
 * The Hitec line's PIO (programmable input/output) program, run on the host
 * (protocols/hitec/rp2350/hitec_line.pio), with the arithmetic that feeds
 * it and reads it (protocols/hitec/hitec_pio.c).
 *
 * The program is assembled from its source by a small assembler for
 * the instructions it uses, and run by a model of the state machine that
 * steps one state machine cycle at a time: JMP always, on X--, Y--, the pin
 * and !OSRE; OUT to the pins, the pin directions, X, Y and NULL with
 * autopull at 32 bits, shifting right; MOV OSR, NULL; IN from the pins and
 * X, shifting right; PUSH noblock; SET of the pin directions and Y; delays;
 * the wrap.  A DMA (direct
 * memory access) channel fills the 4-word transmit FIFO and another drains
 * the 4-word receive FIFO into a buffer of a given length.  A cycle lasts
 * the divider's system clock cycles, fraction included, and the pin is read
 * 2 system clock cycles after it moves, as the input synchroniser delays
 * it.  The D series' input override inverts what the program reads.  The
 * assembled words are held to fixtures/hitec_line_words.txt, which the
 * firmware build holds pioasm's output to: the model runs what the chip
 * runs.
 *
 * The wire is the pad's latch while the pad drives, and the level the test
 * gives while it does not: the servo's high level or a pull-up, and what
 * the servo sends.
 *
 * Under test:
 *  - the transmit: every bit is 16 cycles on a grid; classic at 572.9us
 *    start to start, released at the start of the last stop bit, never
 *    driven high; the D series driven low 88us first, back to back, its
 *    last stop bit sent, released one bit later; push-pull driving both
 *    levels; the bytes on the wire are the frame;
 *  - the receive: the bytes of a reply, each start bit stamped within one
 *    cycle at every phase of the sampling, a bad stop bit, a break, the
 *    window's end with a byte just inside and just outside it, more bytes
 *    than the channel takes;
 *  - the answer: sent 10 to 10.125 bit times after the first byte's start
 *    bit, 10 bit times long, only after the first byte, the receiver back
 *    from the end of its stop bit, and the time of the bytes after it;
 *  - a level held for its time, an abort, the clock's limits, the stream's
 *    layout and refusals, the time a transaction can take.
 *
 * Not tested here, because the host has no PIO: that the chip's input
 * synchroniser, autopull, JMP PIN, JMP !OSRE, the fractional divider, the
 * restart of a state machine and the DMA handshakes behave as this model
 * says; the pad, the pulls and the servo's levels.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "hitec_pio.h"
#include "hitec_wire.h"

#include "hitec_pio_model.h"

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_words_are_the_ones_the_firmware_build_checks)
{
    FILE *f = fopen(HITEC_LINE_WORDS, "r");
    char line[32];
    unsigned n = 0;

    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        CHECK(n < prog_len);
        if (n < prog_len) {
            CHECK_EQ((unsigned)strtoul(line, NULL, 16), prog[n]);
        }
        ++n;
    }
    fclose(f);
    CHECK_EQ(n, prog_len);
    CHECK_EQ(prog_len, HITEC_PIO_PROGRAM_WORDS);
    CHECK_EQ(wrap_target, 0u);
    CHECK_EQ(wrap_at, HITEC_PIO_PROGRAM_WORDS - 1u);
    /* The instructions the model and the driver name by address. */
    CHECK_EQ(prog[HITEC_PIO_IDLE_PC], 0x6001u);       /* out pins, 1 */
    CHECK_EQ(prog[HITEC_PIO_BIT_PC], 0x6081u);        /* out pindirs, 1 */
    CHECK_EQ(prog[HITEC_PIO_RELEASE_PC], 0xE080u);    /* set pindirs, 0 */
    CHECK_EQ(prog[HITEC_PIO_ASK_PC], 0x00F8u);        /* jmp !osre, answer */
    CHECK_EQ(prog[HITEC_PIO_ANSWER_PC], 0xE249u);     /* set y, 9 [2] */
    CHECK_EQ(prog[HITEC_PIO_ANSWER_PC + 1u], 0x606Cu);  /* out null, 12 */
    CHECK_EQ(prog[HITEC_PIO_ANSWER_PC + 2u], 0x0008u);  /* jmp bit */
    CHECK_EQ(prog[HITEC_PIO_DRAIN_PC], 0xA0E3u);      /* mov osr, null */
    CHECK_EQ(prog[HITEC_PIO_DRAIN_PC + 1u], 0x6060u); /* out null, 32 */
}

TEST_CASE(a_classic_command_is_16_cycles_a_bit_and_never_drives_high)
{
    hitec_xfer_t x;
    uint8_t got[8] = { 0 };
    uint64_t at[8] = { 0 };
    uint8_t want[4];

    hitec_c_xfer(HITEC_C_READ_EEPROM, 0x05, 0x00, &x);
    hitec_c_frame(HITEC_C_READ_EEPROM, 0x05, 0x00, want);
    memset(&m, 0, sizeof(m));
    ext_clear(true);                         /* the servo holds it high */
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 2, 0));
    CHECK(m.finished);
    /* 150MHz: 488.28125 system cycles a state machine cycle, exactly. */
    CHECK_EQ(clk.div256, 125000u);
    CHECK_EQ(clk.err_ppm, 0u);
    CHECK_EQ(hitec_pio_bit_ns(&clk), 52083u);
    CHECK(!pad_ever_high(0));
    CHECK_EQ(wire_bytes(0, m.release + 16u, false, got, at, 8), 4u);
    CHECK(memcmp(got, want, 4) == 0);
    /* 11 bit times start to start: 572.9us. */
    for (unsigned i = 1; i < 4u; ++i) {
        CHECK_EQ(at[i] - at[i - 1u], 176u);
    }
    CHECK_NEAR(cyc_ns(at[1] - at[0]), 572917.0, 1.0);
    check_grid(at[0], at[0], m.release);
    /* Released at the start of the last stop bit, to the cycle, and an
     * input from there on. */
    CHECK_EQ(m.release - at[3], 9u * 16u);
    CHECK_EQ(pad_at(&m, m.release), -1);
    CHECK_EQ(next_dir(m.release, true), 0u);
    /* No reply: the window's waiting, 1.5ms, and the machine waits again. */
    CHECK_EQ(run.window_ticks, 231u);
    CHECK_NEAR(cyc_ns(2u * run.window_ticks), 1.5e6, cyc_ns(2));
    CHECK_EQ(m.end - m.release, 2u * run.window_ticks + 5u + HITEC_PIO_DRAIN_CYCLES);
    CHECK_EQ(m.n_rx, 0u);
    /* The time the driver allows covers the run. */
    CHECK(cyc_ns(m.end) < (double)run.span_us * 1000.0);
}

TEST_CASE(a_classic_reply_comes_back_with_its_times_and_decodes)
{
    static const struct {
        uint8_t cmd, p1, p2, r0, r1;
        double delay_us, spacing_us;
    } rows[] = {
        /* From the bench: a block read of the HS-5645MG, a write and the
         * first read after it, the first command, a position read. */
        { 0x61, 0x02, 0x00, 0x01, 0x2B, 513.0, 516.0 },
        { 0x62, 0x02, 0x01, 0x2B, 0x2B, 514.0, 528.0 },
        { 0x61, 0x00, 0x00, 0x50, 0x2B, 608.0, 520.0 },
        { 0x67, 0x00, 0x00, 0x16, 0x2B, 508.0, 522.0 },
        { 0x65, 0x00, 0x00, 0x02, 0xD6, 517.0, 516.0 },
    };

    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        hitec_xfer_t x;
        hitec_capture_t cap;
        hitec_reply_t r;
        hitec_c_expect_t e;
        const double arm = 9.0 * bit_ns(HITEC_CLASSIC);
        const double t0 = rows[i].delay_us * 1000.0 - arm;

        hitec_c_xfer(rows[i].cmd, rows[i].p1, rows[i].p2, &x);
        memset(&m, 0, sizeof(m));
        ext_clear(true);
        ext_byte(HITEC_CLASSIC, t0, bit_ns(HITEC_CLASSIC), rows[i].r0, true);
        ext_byte(HITEC_CLASSIC, t0 + rows[i].spacing_us * 1000.0,
                 bit_ns(HITEC_CLASSIC), rows[i].r1, true);
        CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 2, 0));
        CHECK_EQ(m.n_rx, 2u);
        CHECK_EQ(m.dropped, 0u);
        captured(&cap, true);
        CHECK_EQ(cap.n, 2);
        CHECK_EQ(cap.overflow, 0);
        check_at(cap.byte[0].at_ns, t0);
        check_at(cap.byte[1].at_ns, t0 + rows[i].spacing_us * 1000.0);
        CHECK(hitec_c_expect_of(rows[i].cmd, &e));
        CHECK_EQ(hitec_c_decode(&cap, e, &r), HITEC_RX_OK);
        CHECK_NEAR((double)r.delay_ns, rows[i].delay_us * 1000.0,
                   cyc_ns(1) + 20.0);
        if (e == HITEC_C_EXPECT_VALUE) {
            CHECK_EQ(r.value, rows[i].r0);
        } else if (e == HITEC_C_EXPECT_WORD) {
            CHECK_EQ(r.value, (uint16_t)(rows[i].r0 << 8 | rows[i].r1));
        }
        /* The pad stays an input through the reply. */
        CHECK_EQ(next_dir(m.release, true), 0u);
    }
}

TEST_CASE(a_start_bit_at_any_phase_of_the_sampling_is_stamped_within_a_cycle)
{
    hitec_xfer_t x;

    hitec_c_xfer(HITEC_C_READ_EEPROM, 0x00, 0x00, &x);
    /* A cycle is 3.255us at 150MHz; 0 to 7us in 0.4us walks both
     * cycles of the 2-cycle sampling and the 1-cycle step into it. */
    for (double shift = 0.0; shift < 7000.0; shift += 400.0) {
        hitec_capture_t cap;
        const double t0 = 45000.0 + shift;

        memset(&m, 0, sizeof(m));
        ext_clear(true);
        ext_byte(HITEC_CLASSIC, t0, bit_ns(HITEC_CLASSIC), 0xA5, true);
        ext_byte(HITEC_CLASSIC, t0 + 520833.0 + shift / 3.0,
                 bit_ns(HITEC_CLASSIC), 0x2B, true);
        CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 2, 0));
        captured(&cap, true);
        CHECK_EQ(cap.n, 2);
        CHECK_EQ(cap.byte[0].value, 0xA5);
        CHECK_EQ(cap.byte[1].value, 0x2B);
        check_at(cap.byte[0].at_ns, t0);
        check_at(cap.byte[1].at_ns, t0 + 520833.0 + shift / 3.0);
    }
}

TEST_CASE(a_d_read_drives_low_88_us_then_sends_back_to_back_and_releases)
{
    hitec_xfer_t x;
    uint8_t got[8] = { 0 };
    uint64_t at[8] = { 0 };
    uint8_t want[HITEC_D_READ_LEN];
    uint64_t first_drive;
    uint64_t last_drive_end;

    hitec_d_read_xfer(0x00, 0x0C, &x);
    hitec_d_read_frame(0x00, 0x0C, want);
    memset(&m, 0, sizeof(m));
    ext_clear(true);                         /* the pull-up */
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 8, 0));
    CHECK(m.finished);
    CHECK(!pad_ever_high(0));
    /* Up to the released settle bit, which reads as a start bit. */
    CHECK_EQ(wire_bytes(0, m.release - 16u, true, got, at, 8), 5u);
    CHECK(memcmp(got, want, 5) == 0);
    for (unsigned i = 1; i < 5u; ++i) {
        CHECK_EQ(at[i] - at[i - 1u], 160u);   /* back to back */
    }
    check_grid(at[0], at[0], m.release);
    /* Driven low for 88us or more before the first start bit.  The 13
     * bit times that pad the front of the stream make it 117us here: the
     * lead's fixed 8 cycles and 208. */
    first_drive = next_dir(0, true);
    CHECK(first_drive > 0u);
    CHECK_EQ(at[0] - first_drive, 8u + 13u * 16u);
    CHECK(cyc_ns(at[0] - first_drive) >= 88000.0);
    CHECK_NEAR(cyc_ns(at[0] - first_drive), 117180.0, 10.0);
    /* The last stop bit is driven, then one released bit before the
     * receiver starts. */
    last_drive_end = at[4] + 160u;
    CHECK_EQ(pad_at(&m, last_drive_end - 1u), 0);
    CHECK_EQ(pad_at(&m, last_drive_end), -1);
    CHECK_EQ(m.release, last_drive_end + 16u);
    CHECK_EQ(next_dir(last_drive_end, true), 0u);
    /* Nothing came: the window's 25ms of waiting. */
    CHECK_EQ(m.end - m.release, 2u * run.window_ticks + 5u + HITEC_PIO_DRAIN_CYCLES);
    CHECK_NEAR(cyc_ns(2u * run.window_ticks), 25e6, cyc_ns(2));
}

TEST_CASE(a_d_answer_is_taken_with_the_byte_after_it_and_decodes)
{
    static const uint8_t a[] = { 0x69, 0x00, 0x0C, 0x02, 0xC3, 0x3B, 0x0C };
    const double settle = bit_ns(HITEC_D);
    hitec_xfer_t x;
    hitec_capture_t cap;
    hitec_reply_t r;

    hitec_d_read_xfer(0x00, 0x0C, &x);
    memset(&m, 0, sizeof(m));
    /* 6.46ms from the end of the request, as on the bench. */
    d_answer(6461000.0 - settle, a, sizeof(a));
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 8, 0));
    CHECK_EQ(m.n_rx, 8u);
    captured(&cap, true);
    CHECK_EQ(cap.n, 8);
    CHECK_EQ(cap.full, 1);
    for (unsigned i = 0; i < sizeof(a); ++i) {
        CHECK_EQ(cap.byte[i].value, a[i]);
        CHECK_EQ(cap.byte[i].framing, 0);
        check_at(cap.byte[i].at_ns,
                 6461000.0 - settle + 10.0 * bit_ns(HITEC_D) * i);
    }
    CHECK_EQ(cap.byte[7].value, 0x00);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_OK);
    CHECK_EQ(r.value, 15299u);
    CHECK_NEAR((double)r.delay_ns, 6461000.0, cyc_ns(1) + 20.0);
    CHECK_EQ(next_dir(m.release, true), 0u);
}

TEST_CASE(the_release_after_the_answer_reads_as_a_byte_with_a_bad_stop_bit)
{
    static const uint8_t a[] = { 0x69, 0x00, 0x1E, 0x02, 0xB8, 0x0B, 0xE3 };
    hitec_xfer_t x;
    hitec_capture_t cap;
    hitec_reply_t r;

    hitec_d_read_xfer(0x00, 0x1E, &x);
    memset(&m, 0, sizeof(m));
    d_answer(6000000.0, a, sizeof(a));
    /* A channel with room for 10: the break after the byte 0x00 is the
     * ninth, and the receiver then waits for the idle level until the
     * window ends. */
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 10, 0));
    CHECK_EQ(m.n_rx, 9u);
    hitec_pio_capture(&clk, &run, m.rx, m.n_rx, false, false, false, false,
                      &cap);
    CHECK_EQ(cap.n, 9);
    CHECK_EQ(cap.byte[8].value, 0x00);
    CHECK_EQ(cap.byte[8].framing, 1);
    CHECK_EQ(hitec_d_decode(&cap, 0x1E, &r), HITEC_RX_OK);
    CHECK_EQ(r.value, 3000u);
}

TEST_CASE(a_push_pull_d_line_drives_both_levels_and_releases_after_the_stop)
{
    hitec_xfer_t x;
    uint8_t got[8] = { 0 };
    uint64_t at[8] = { 0 };
    uint8_t want[HITEC_D_WRITE_LEN];
    uint64_t first;

    hitec_d_write_xfer(0x00, 0x70, 0xFFFF, &x);
    hitec_d_write_frame(0x00, 0x70, 0xFFFF, want);
    memset(&m, 0, sizeof(m));
    ext_clear(false);                       /* no pull-up: the servo's pull-down */
    CHECK(transact(150000000u, HITEC_DRIVE_PUSH_PULL, &x, 0, 0));
    CHECK(m.finished);
    CHECK_EQ(wire_bytes(0, m.release, true, got, at, 8), 7u);
    CHECK(memcmp(got, want, 7) == 0);
    CHECK(pad_ever_high(0));
    /* The pad drives from the lead to the end of the last stop bit without
     * a gap, and never after it.  The level moves 1 cycle after the
     * direction would, so the wire's bits sit 1 cycle late and the
     * release cuts the last stop bit to 15 cycles. */
    first = next_dir(0, true);
    for (uint64_t c = first; c < at[6] + 159u; ++c) {
        CHECK(pad_at(&m, c) >= 0);
    }
    CHECK_EQ(pad_at(&m, at[6] + 159u), -1);
    CHECK_EQ(next_dir(at[6] + 159u, true), 0u);
    check_grid(at[0], at[0], at[6] + 158u);
    /* A write receives nothing and ends at once. */
    CHECK(m.end - m.release < 8u);
    CHECK_EQ(m.n_rx, 0u);
    /* Classic has no push-pull. */
    hitec_c_xfer(HITEC_C_MOVE, 0x17, 0x70, &x);
    CHECK(hitec_pio_clock(150000000u, HITEC_CLASSIC, &clk));
    CHECK_EQ(hitec_pio_tx(&clk, HITEC_DRIVE_PUSH_PULL, &x, m.tx, &run), 0u);
}

TEST_CASE(a_listen_answers_the_first_byte_10_bit_times_after_its_start)
{
    const double bit = bit_ns(HITEC_CLASSIC);
    hitec_xfer_t x;

    CHECK(hitec_listen_xfer(HITEC_CLASSIC, 1000000u, 2, true, HITEC_C_ACK,
                            &x));
    /* Over one cycle of phase, in 8 steps. */
    for (unsigned k = 0; k < 8u; ++k) {
        hitec_capture_t cap;
        hitec_reply_t r;
        uint8_t got[4] = { 0 };
        uint64_t at[4] = { 0 };
        uint64_t edge;
        const double t0 = 1000000.0 + 410.0 * k;
        const double t1 = t0 + 260e6;
        double lat;

        memset(&m, 0, sizeof(m));
        ext_clear(true);
        ext_byte(HITEC_CLASSIC, t0, bit, HITEC_C_ACK, true);
        ext_byte(HITEC_CLASSIC, t1, bit, HITEC_C_ACK, true);
        CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 2, 0));
        CHECK_EQ(m.answers, 1u);
        CHECK_EQ(m.n_rx, 2u);
        /* The answer's start bit: the first time the pad drives. */
        edge = next_dir(m.release, true);
        CHECK(edge > 0u);
        lat = cyc_ns(edge) - cyc_ns(m.release) - t0;
        CHECK(lat >= 10.0 * bit - 2.0);
        CHECK(lat <= 10.125 * bit + 2e9 / 150e6 + 2.0);
        CHECK(lat >= 520000.0 && lat <= 550000.0);
        CHECK_EQ(wire_bytes(edge - 1u, edge + 200u, false, got, at, 4), 1u);
        CHECK_EQ(got[0], HITEC_C_ACK);
        CHECK(!pad_ever_high(0));
        /* Released after the answer's 10 bit times, for good. */
        CHECK_EQ(next_dir(edge + 10u * 16u, true), 0u);
        captured(&cap, true);
        CHECK_EQ(cap.answered, 1);
        CHECK_EQ(cap.n, 2);
        check_at(cap.byte[0].at_ns, t0);
        check_at(cap.byte[1].at_ns, t1);
        CHECK_EQ(hitec_listen_decode(&cap, HITEC_C_ACK, &r),
                 HITEC_RX_ERR_LENGTH);
        cap.n = 1;                    /* the first byte, as a 1-byte listen */
        CHECK_EQ(hitec_listen_decode(&cap, HITEC_C_ACK, &r), HITEC_RX_OK);
        CHECK_NEAR((double)r.delay_ns, t0, cyc_ns(1) + 20.0);
    }
}

/*
 * The servo's next byte half a bit after the answer's stop bit, and one back
 * to back behind it: both taken at their times, and no second answer.  The
 * answer is 10 bit times, and the receiver waits again from the end of its
 * stop bit.  Classic open drain on a line the servo holds high; the D series
 * push-pull on a line that idles low.
 */
TEST_CASE(a_byte_right_after_the_answer_is_taken)
{
    static const struct {
        uint8_t family;
        uint8_t drive;
        bool idle;
        uint8_t answer;
    } c[] = {
        { HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN, true, HITEC_C_ACK },
        { HITEC_D, HITEC_DRIVE_PUSH_PULL, false, 0x96 },
    };
    static const uint8_t v[3] = { 0x2B, 0x5A, 0xC3 };

    for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); ++i) {
        const double bit = bit_ns(c[i].family);
        const bool inv = c[i].family == HITEC_D;
        double t[3];
        hitec_xfer_t x;
        hitec_capture_t cap;
        uint8_t got[4] = { 0 };
        uint64_t at[4] = { 0 };
        uint64_t edge;

        t[0] = 1000000.0;
        t[1] = t[0] + 20.5 * bit;
        t[2] = t[1] + 10.0 * bit;
        CHECK(hitec_listen_xfer(c[i].family, 20000u, 3, true, c[i].answer,
                                &x));
        memset(&m, 0, sizeof(m));
        ext_clear(c[i].idle);
        for (unsigned k = 0; k < 3u; ++k) {
            ext_byte(c[i].family, t[k], bit, v[k], true);
        }
        CHECK(transact(150000000u, c[i].drive, &x, 3, 0));
        CHECK_EQ(m.answers, 1u);
        CHECK_EQ(m.n_rx, 3u);
        captured(&cap, true);
        CHECK_EQ(cap.n, 3);
        CHECK_EQ(cap.full, 1);
        CHECK_EQ(cap.answered, 1);
        CHECK_EQ(cap.overflow, 0);
        for (unsigned k = 0; k < 3u; ++k) {
            CHECK_EQ(cap.byte[k].value, v[k]);
            CHECK_EQ(cap.byte[k].framing, 0);
            check_at(cap.byte[k].at_ns, t[k]);
        }
        /* The answer on the wire, and the pad released 10 bit times after
         * its start bit, for good. */
        edge = next_dir(m.release, true);
        CHECK(edge > 0u);
        CHECK_EQ(wire_bytes(edge - 1u, edge + 16u, inv, got, at, 4), 1u);
        CHECK_EQ(got[0], c[i].answer);
        CHECK_EQ(next_dir(edge + HITEC_PIO_ANSWER_BITS * 16u, true), 0u);
        CHECK(cyc_ns(edge + HITEC_PIO_ANSWER_BITS * 16u) - cyc_ns(m.release)
              < t[1]);
    }
}

TEST_CASE(a_listen_without_an_answer_never_drives)
{
    hitec_xfer_t x;
    hitec_capture_t cap;

    CHECK(hitec_listen_xfer(HITEC_CLASSIC, 500000u, 3, false, 0, &x));
    memset(&m, 0, sizeof(m));
    ext_clear(true);
    ext_byte(HITEC_CLASSIC, 12000.0, bit_ns(HITEC_CLASSIC), 0x2B, true);
    ext_byte(HITEC_CLASSIC, 3000000.0, bit_ns(HITEC_CLASSIC), 0x2B, true);
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 3, 0));
    CHECK(m.finished);
    CHECK_EQ(m.answers, 0u);
    CHECK_EQ(next_dir(0, true), 0u);
    captured(&cap, false);
    CHECK_EQ(cap.n, 2);
    CHECK_EQ(cap.answered, 0);
    check_at(cap.byte[0].at_ns, 12000.0);
    check_at(cap.byte[1].at_ns, 3000000.0);
    /* The window counts the waiting only: 500ms and the 2 bytes, then
     * the drain. */
    CHECK_NEAR(cyc_ns(m.end - m.release),
               500e6 + 2 * cyc_ns(157) + cyc_ns(HITEC_PIO_DRAIN_CYCLES),
               3 * cyc_ns(2));
}

TEST_CASE(a_level_holds_the_line_for_its_time_then_releases)
{
    hitec_xfer_t x;
    uint64_t on;
    uint64_t off;

    /* The classic probe: 6.7ms low. */
    CHECK(hitec_level_xfer(HITEC_CLASSIC, 0, 6700u, &x));
    memset(&m, 0, sizeof(m));
    ext_clear(true);
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 0, 0));
    CHECK(m.finished);
    on = next_dir(0, true);
    off = next_dir(on, false);
    CHECK(on > 0u && off > on);
    CHECK_EQ(pad_at(&m, on), 0);
    CHECK_NEAR(cyc_ns(off - on), 6.7e6, cyc_ns(2));
    CHECK_EQ(next_dir(off, true), 0u);
    CHECK_EQ(m.n_rx, 0u);
    CHECK(cyc_ns(m.end) < (double)run.span_us * 1000.0);

    /* Push-pull holds a high level; open drain does not make one. */
    CHECK(hitec_level_xfer(HITEC_D, 1, 1000u, &x));
    memset(&m, 0, sizeof(m));
    ext_clear(false);
    CHECK(transact(150000000u, HITEC_DRIVE_PUSH_PULL, &x, 0, 0));
    on = next_dir(0, true);
    off = next_dir(on, false);
    CHECK_EQ(pad_at(&m, on), 1);
    CHECK_NEAR(cyc_ns(off - on), 1e6, cyc_ns(2));
    CHECK(!transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 0, 0));
    /* The shortest level is the lead's fixed part. */
    CHECK(hitec_level_xfer(HITEC_D, 0, 1u, &x));
    memset(&m, 0, sizeof(m));
    ext_clear(true);
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 0, 0));
    on = next_dir(0, true);
    CHECK_EQ(next_dir(on, false) - on, HITEC_PIO_LEAD_CYCLES);
}

TEST_CASE(a_byte_with_a_low_stop_bit_is_marked_and_the_next_one_is_taken)
{
    hitec_xfer_t x;
    hitec_capture_t cap;
    hitec_reply_t r;
    const double bit = bit_ns(HITEC_CLASSIC);

    hitec_c_xfer(HITEC_C_READ_EEPROM, 0x01, 0x00, &x);
    memset(&m, 0, sizeof(m));
    ext_clear(true);
    ext_byte(HITEC_CLASSIC, 45000.0, bit, 0x6E, false);
    ext_byte(HITEC_CLASSIC, 45000.0 + 12.0 * bit, bit, 0x2B, true);
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 2, 0));
    captured(&cap, true);
    CHECK_EQ(cap.n, 2);
    CHECK_EQ(cap.byte[0].value, 0x6E);
    CHECK_EQ(cap.byte[0].framing, 1);
    CHECK_EQ(cap.byte[1].value, 0x2B);
    CHECK_EQ(cap.byte[1].framing, 0);
    check_at(cap.byte[1].at_ns, 45000.0 + 12.0 * bit);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_FRAMING);
}

TEST_CASE(the_window_takes_a_byte_just_inside_it_and_not_one_just_after)
{
    hitec_xfer_t x;
    hitec_capture_t cap;
    double end_ns;

    CHECK(hitec_listen_xfer(HITEC_D, 1000u, 1, false, 0, &x));
    /* Where the waiting ends with the line idle. */
    memset(&m, 0, sizeof(m));
    ext_clear(false);                       /* idle: low on the wire */
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 1, 0));
    CHECK(m.finished);
    /* The receiver stops sampling where the drain starts. */
    end_ns = cyc_ns(m.end - m.release - HITEC_PIO_DRAIN_CYCLES);
    CHECK_NEAR(end_ns, 1e6, 3 * cyc_ns(2));

    memset(&m, 0, sizeof(m));
    ext_clear(false);
    ext_byte(HITEC_D, end_ns - 5.0 * cyc_ns(1), bit_ns(HITEC_D), 0x5A, true);
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 1, 0));
    captured(&cap, true);
    CHECK_EQ(cap.n, 1);
    CHECK_EQ(cap.byte[0].value, 0x5A);

    memset(&m, 0, sizeof(m));
    ext_clear(false);
    ext_byte(HITEC_D, end_ns + 2.0 * cyc_ns(1), bit_ns(HITEC_D), 0x5A, true);
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 1, 0));
    CHECK_EQ(m.n_rx, 0u);
}

TEST_CASE(an_abort_releases_the_pad_and_the_next_transaction_runs)
{
    hitec_xfer_t x;
    uint8_t got[8] = { 0 };
    uint64_t at[8] = { 0 };
    uint8_t want[HITEC_D_WRITE_LEN];

    /* Cut a push-pull write in the middle of its third byte. */
    hitec_d_write_xfer(0x00, 0x32, 0x0003, &x);
    memset(&m, 0, sizeof(m));
    ext_clear(false);
    CHECK(transact(150000000u, HITEC_DRIVE_PUSH_PULL, &x, 0, 0));
    CHECK(wire_bytes(0, m.release, true, got, at, 8) == 7u);
    memset(&m, 0, sizeof(m));
    ext_clear(false);
    CHECK(transact(150000000u, HITEC_DRIVE_PUSH_PULL, &x, 0,
                   at[2] + 40u));
    CHECK_EQ(pad_at(&m, at[2] + 40u), -1);
    CHECK_EQ(next_dir(at[2] + 40u, true), 0u);
    CHECK(m.end > at[2] + 40u && m.end < at[2] + 44u);

    /* The restart leaves the machine as a fresh one: the same stream runs
     * whole. */
    hitec_d_write_frame(0x00, 0x32, 0x0003, want);
    memset(&m, 0, sizeof(m));
    ext_clear(false);
    CHECK(transact(150000000u, HITEC_DRIVE_PUSH_PULL, &x, 0, 0));
    CHECK_EQ(wire_bytes(0, m.release, true, got, at, 8), 7u);
    CHECK(memcmp(got, want, 7) == 0);

    /* A cut receive: the bytes so far, marked. */
    {
        hitec_capture_t cap;
        hitec_reply_t r;

        hitec_pio_capture(&clk, &run, m.rx, 0, false, false, true, false,
                          &cap);
        CHECK_EQ(cap.aborted, 1);
        CHECK_EQ(hitec_d_decode(&cap, 0x32, &r), HITEC_RX_ERR_ABORTED);
    }
}

TEST_CASE(bytes_past_the_channel_are_dropped_and_the_pad_ends_released)
{
    hitec_xfer_t x;
    hitec_capture_t cap;

    CHECK(hitec_listen_xfer(HITEC_CLASSIC, 100000u, 2, false, 0, &x));
    memset(&m, 0, sizeof(m));
    ext_clear(true);
    for (unsigned i = 0; i < 8u; ++i) {
        ext_byte(HITEC_CLASSIC, 10000.0 + 520833.0 * i, bit_ns(HITEC_CLASSIC),
                 (uint8_t)(0x30 + i), true);
    }
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 2, 0));
    CHECK(m.finished);
    CHECK_EQ(m.n_rx, 2u);
    CHECK_EQ(m.dropped, 2u);
    CHECK_EQ(next_dir(0, true), 0u);
    captured(&cap, true);
    CHECK_EQ(cap.n, 2);
    CHECK_EQ(cap.full, 1);
    CHECK_EQ(cap.byte[1].value, 0x31);
}

TEST_CASE(a_stamp_no_count_gives_is_an_overflow)
{
    hitec_xfer_t x;
    hitec_capture_t cap;
    uint32_t s[HITEC_RX_MAX + 1u];

    CHECK(hitec_pio_clock(150000000u, HITEC_CLASSIC, &clk));
    CHECK(hitec_listen_xfer(HITEC_CLASSIC, 1000u, 4, false, 0, &x));
    CHECK(hitec_pio_tx(&clk, HITEC_DRIVE_OPEN_DRAIN, &x, m.tx, &run) != 0u);
    /* Above the window. */
    s[0] = (run.window_ticks + 1u) << 9 | 0x12u;
    hitec_pio_capture(&clk, &run, s, 1, false, false, false, false, &cap);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.n, 0);
    /* Above the one before. */
    s[0] = (run.window_ticks - 10u) << 9 | 0x100u | 0x12u;
    s[1] = (run.window_ticks - 9u) << 9 | 0x100u | 0x13u;
    hitec_pio_capture(&clk, &run, s, 2, false, false, false, false, &cap);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.n, 1);
    CHECK_EQ(cap.byte[0].value, 0x12);
    CHECK_EQ(cap.byte[0].framing, 0);
    /* The limits are times: the window's first and last tick, one counter
     * twice. */
    s[0] = run.window_ticks << 9 | 0x100u;
    s[1] = 0x100u;                           /* counter 0 */
    s[2] = 0u;
    hitec_pio_capture(&clk, &run, s, 3, false, false, false, false, &cap);
    CHECK_EQ(cap.overflow, 0);
    CHECK_EQ(cap.n, 3);
    CHECK_NEAR(cap.byte[0].at_ns, cyc_ns(1) - 2e9 / 150e6, 2.0);
    CHECK_EQ(cap.byte[2].framing, 1);
    CHECK(cap.byte[1].at_ns > cap.byte[0].at_ns);
    CHECK_NEAR((double)(cap.byte[2].at_ns - cap.byte[1].at_ns),
               cyc_ns(HITEC_PIO_BYTE_CYCLES), 2.0);
    /* More stamps than a capture holds; the driver's mark is kept. */
    for (unsigned i = 0; i <= HITEC_RX_MAX; ++i) {
        s[i] = (run.window_ticks - i) << 9 | 0x100u;
    }
    hitec_pio_capture(&clk, &run, s, HITEC_RX_MAX + 1u, false, false, false,
                      false, &cap);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.n, HITEC_RX_MAX);
    hitec_pio_capture(&clk, &run, s, 2, false, true, false, false, &cap);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.n, 2);
    hitec_pio_capture(&clk, &run, s, 0, false, false, false, false, &cap);
    CHECK_EQ(cap.n, 0);
    CHECK_EQ(cap.overflow, 0);
    CHECK_EQ(cap.window_ns, 1000000u);
    CHECK_EQ(cap.family, HITEC_CLASSIC);
}

TEST_CASE(other_system_clocks_give_the_bit_within_0_05_percent)
{
    check_clock_runs(125000000u);
    check_clock_runs(133000000u);
    check_clock_runs(200000000u);
    check_clock_runs(48000000u);
    check_clock_runs(12000000u);
    CHECK_EQ(clk.div256 >> 8, 6u);          /* D series at 12MHz: 6.51 */
    check_clock_runs(2000000u);             /* classic only */
    CHECK(!hitec_pio_clock(2000000u, HITEC_D, &clk));
}

TEST_CASE(a_clock_below_4_cycles_a_state_machine_cycle_is_refused)
{
    hitec_pio_clock_t c;
    uint32_t low;

    memset(&c, 0x5A, sizeof(c));
    CHECK(!hitec_pio_clock(0u, HITEC_CLASSIC, &c));
    CHECK(!hitec_pio_clock(150000000u, HITEC_CLASSIC, NULL));
    CHECK(!hitec_pio_clock(150000000u, 2u, &c));
    /* A refused clock leaves the counts as they were. */
    CHECK_EQ(c.div256, 0x5A5A5A5Au);
    /* 64 bit rates, less half a step of the divider. */
    low = lowest_clock(HITEC_CLASSIC);
    CHECK_EQ(low, 1228200u);
    CHECK(hitec_pio_clock(low, HITEC_CLASSIC, &c));
    CHECK_EQ(c.div256, 4u * 256u);
    CHECK(c.err_ppm <= 489u);
    CHECK(!hitec_pio_clock(low - 1u, HITEC_CLASSIC, &c));
    low = lowest_clock(HITEC_D);
    CHECK_EQ(low, 7369200u);
    CHECK(hitec_pio_clock(low, HITEC_D, &c));
    CHECK_EQ(c.div256, 4u * 256u);
    CHECK(!hitec_pio_clock(low - 1u, HITEC_D, &c));
    /* Every clock above gives the bit within 489ppm. */
    for (uint32_t hz = low; hz < 400000000u; hz = hz + hz / 97u + 13u) {
        CHECK(hitec_pio_clock(hz, HITEC_D, &c));
        CHECK(c.err_ppm <= 489u);
        CHECK(hitec_pio_clock(hz, HITEC_CLASSIC, &c));
        CHECK(c.err_ppm <= 489u);
    }
    /* The largest clock: the divider's integer part stays below 65536. */
    CHECK(hitec_pio_clock(0xFFFFFFFFu, HITEC_CLASSIC, &c));
    CHECK_EQ(c.div256 >> 8, 13981u);
    CHECK(hitec_pio_clock(0xFFFFFFFFu, HITEC_D, &c));
}

TEST_CASE(the_stream_pads_its_front_to_a_word_and_refuses_what_it_cannot_run)
{
    hitec_xfer_t x;
    uint32_t w[HITEC_PIO_TX_WORDS + 1u];
    unsigned n;

    CHECK(hitec_pio_clock(150000000u, HITEC_CLASSIC, &clk));
    for (unsigned fam = HITEC_CLASSIC; fam <= HITEC_D; ++fam) {
        CHECK(hitec_pio_clock(150000000u, (uint8_t)fam, &clk));
        for (unsigned nb = 1; nb <= HITEC_TX_MAX; ++nb) {
            for (unsigned gap = 0; gap <= HITEC_GAP_MAX; ++gap) {
                const unsigned raw = nb * 10u + (nb - 1u) * gap
                                     + (fam == HITEC_D ? 1u : 0u)
                                     - (fam == HITEC_CLASSIC ? 1u : 0u);
                const unsigned bits = (raw + 15u) / 16u * 16u;

                memset(&x, 0, sizeof(x));
                x.kind = HITEC_XFER_SEND;
                x.family = (uint8_t)fam;
                x.n_tx = (uint8_t)nb;
                x.gap_bits = (uint8_t)gap;
                memset(x.tx, 0x55, sizeof(x.tx));
                w[HITEC_PIO_TX_WORDS] = 0xDEADBEEFu;
                n = hitec_pio_tx(&clk, HITEC_DRIVE_OPEN_DRAIN, &x, w, &run);
                CHECK_EQ(n, 3u + bits / 16u);
                CHECK_EQ(w[HITEC_PIO_TX_WORDS], 0xDEADBEEFu);
                CHECK_EQ(w[2], bits);
                CHECK_EQ(w[1], 0u);
                CHECK_EQ(run.window_ticks, 0u);
            }
        }
    }
    /* The lead: the count makes up what the padding does not. */
    CHECK(hitec_pio_clock(150000000u, HITEC_D, &clk));
    hitec_d_read_xfer(0x00, 0x0C, &x);
    n = hitec_pio_tx(&clk, HITEC_DRIVE_OPEN_DRAIN, &x, w, &run);
    CHECK_EQ(n, 7u);
    /* 51 bit times, 13 in front: 216 cycles, more than the 162 of 88us,
     * so no count. */
    CHECK_EQ(w[2], 64u);
    CHECK_EQ(w[0] & 3u, 2u);                 /* low and driven */
    CHECK_EQ(w[0] >> 2, 0u);
    CHECK_EQ(w[1], run.window_ticks);
    CHECK_EQ(run.window_ticks, 23041u);      /* 25ms in 1/8 bits */
    /* A write: 71 bit times, 9 in front, 152 cycles; the count makes up
     * the 11 to 163, 88us rounded up, in steps of 2: 164 cycles,
     * 88.97us. */
    hitec_d_write_xfer(0x00, 0x46, 1, &x);
    CHECK_EQ(hitec_pio_tx(&clk, HITEC_DRIVE_OPEN_DRAIN, &x, w, &run), 8u);
    CHECK_EQ(w[2], 80u);
    CHECK_EQ(w[0] >> 2, 6u);
    CHECK_NEAR((double)run.lead_ns, cyc_ns(2u * 6u + 8u + 80u * 16u), 1.0);
    /* lead_ns, the lead's first edge to the release: the lead and every
     * bit time.  705,283ns, as docs/BenchConsole.md gives it. */
    CHECK_EQ(run.lead_ns, 705283u);
    hitec_d_read_xfer(0x00, 0x0C, &x);
    CHECK_EQ(hitec_pio_tx(&clk, HITEC_DRIVE_OPEN_DRAIN, &x, w, &run), 7u);
    /* A read: 117.2us of lead, 50 bit times of bytes and 1 released, the
     * 559,886ns the station's console reported and the docs give. */
    CHECK_NEAR((double)run.lead_ns, cyc_ns(8u + 64u * 16u), 1.0);
    CHECK_EQ(run.lead_ns, 559886u);
    CHECK_EQ(run.max_rx, 8u);
    /* Listen: no bit times, the answer word behind the counts. */
    CHECK(hitec_listen_xfer(HITEC_D, 1000u, 1, true, 0x96, &x));
    CHECK_EQ(hitec_pio_tx(&clk, HITEC_DRIVE_OPEN_DRAIN, &x, w, &run), 4u);
    CHECK_EQ(w[2], 0u);
    CHECK_EQ(w[0], 0u);
    CHECK_EQ(run.answer, 1u);
    /* Refused: no clock, no words, no run, an invalid transaction, another
     * family, an unknown drive. */
    CHECK_EQ(hitec_pio_tx(NULL, 0, &x, w, &run), 0u);
    CHECK_EQ(hitec_pio_tx(&clk, 0, NULL, w, &run), 0u);
    CHECK_EQ(hitec_pio_tx(&clk, 0, &x, NULL, &run), 0u);
    CHECK_EQ(hitec_pio_tx(&clk, 0, &x, w, NULL), 0u);
    CHECK_EQ(hitec_pio_tx(&clk, 2, &x, w, &run), 0u);
    x.max_rx = 0;
    CHECK_EQ(hitec_pio_tx(&clk, 0, &x, w, &run), 0u);
    hitec_c_xfer(HITEC_C_FIRST, 0, 0, &x);
    CHECK_EQ(hitec_pio_tx(&clk, 0, &x, w, &run), 0u);
}

TEST_CASE(an_answer_no_byte_called_for_is_drained_at_the_window_end)
{
    /* Classic open drain, the servo holding the line high; D series
     * push-pull, the line low on the wire. */
    static const struct {
        uint8_t family;
        uint8_t drive;
        uint8_t answer;
        bool idle;
    } c[] = {
        { HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN, HITEC_C_ACK, true },
        { HITEC_D, HITEC_DRIVE_PUSH_PULL, 0x96, false },
    };

    for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); ++i) {
        hitec_xfer_t x;
        hitec_capture_t cap;

        CHECK(hitec_listen_xfer(c[i].family, 2000u, 2, true, c[i].answer,
                                &x));
        memset(&m, 0, sizeof(m));
        ext_clear(c[i].idle);
        CHECK(transact(150000000u, c[i].drive, &x, 2, 0));
        /* At idle, waiting for the next stream: the answer word is gone,
         * not taken for a lead. */
        CHECK(m.finished);
        CHECK_EQ(m.answers, 0u);
        CHECK_EQ(m.n_rx, 0u);
        CHECK_EQ(next_dir(m.release, true), 0u);
        CHECK(!pad_ever_high(m.release));
        captured(&cap, false);
        CHECK_EQ(cap.n, 0);
        CHECK_EQ(cap.answered, 0);
        CHECK_EQ(cap.overflow, 0);
    }
}

TEST_CASE(a_one_byte_listen_holds_its_restart_until_the_answer_is_out)
{
    hitec_xfer_t x;
    uint8_t got[4] = { 0 };
    uint64_t at[4] = { 0 };
    uint64_t edge;
    uint64_t due_end;

    CHECK(hitec_listen_xfer(HITEC_CLASSIC, 20000u, 1, true, HITEC_C_ACK,
                            &x));
    memset(&m, 0, sizeof(m));
    m.due_run = &run;
    ext_clear(true);
    ext_byte(HITEC_CLASSIC, 1000000.0, bit_ns(HITEC_CLASSIC), 0x2B, true);
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 1, 0));
    CHECK_EQ(m.answers, 1u);
    CHECK_EQ(m.n_rx, 1u);
    /* The channel has its 1 byte at the push; the driver's restart waits
     * until hitec_pio_answer_due() is false.  By then the pad has made
     * its last change and the whole answer is on the wire. */
    edge = next_dir(m.release, true);
    CHECK(edge > 0u);
    CHECK(m.due_end > edge);
    CHECK(m.pad[m.n_pad - 1u].cyc < m.due_end);
    CHECK_EQ(wire_bytes(edge - 1u, m.due_end, false, got, at, 4), 1u);
    CHECK_EQ(got[0], HITEC_C_ACK);
    /* And it turns false at the release after the answer's 10 bit
     * times, not later. */
    CHECK(m.due_end <= edge + HITEC_PIO_ANSWER_BITS * 16u + 2u);
    due_end = m.due_end;

    /* An abort inside the answer cuts it: the capture says so.  One after
     * it does not. */
    for (unsigned k = 0; k < 2u; ++k) {
        const uint64_t cut_at = k == 0u ? edge + 3u * 16u : due_end + 4u;
        hitec_capture_t cap;
        bool cut;

        memset(&m, 0, sizeof(m));
        ext_clear(true);
        ext_byte(HITEC_CLASSIC, 1000000.0, bit_ns(HITEC_CLASSIC), 0x2B, true);
        CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 1, cut_at));
        cut = hitec_pio_answer_due(&run, m.n_rx, m.abort_pc);
        CHECK_EQ(cut, k == 0u);
        /* Cut: the released line reads as ones, not as the answer. */
        got[0] = 0;
        (void)wire_bytes(edge - 1u, cut_at + 400u, false, got, at, 4);
        CHECK_EQ(got[0] == HITEC_C_ACK, k != 0u);
        hitec_pio_capture(&clk, &run, m.rx, m.n_rx, false, false, true, cut,
                          &cap);
        CHECK_EQ(cap.n, 1);
        CHECK_EQ(cap.answered, k == 0u ? 0 : 1);
    }

    /* The addresses it answers for, and the refusals. */
    CHECK(hitec_pio_answer_due(&run, 1, HITEC_PIO_ASK_PC));
    CHECK(hitec_pio_answer_due(&run, 1, HITEC_PIO_ANSWER_PC));
    CHECK(hitec_pio_answer_due(&run, 1, HITEC_PIO_ANSWER_PC + 1u));
    CHECK(hitec_pio_answer_due(&run, 1, HITEC_PIO_ANSWER_PC + 2u));
    CHECK(hitec_pio_answer_due(&run, 1, HITEC_PIO_BIT_PC));
    CHECK(hitec_pio_answer_due(&run, 1, HITEC_PIO_RELEASE_PC - 1u));
    CHECK(!hitec_pio_answer_due(&run, 1, HITEC_PIO_BIT_PC - 1u));
    CHECK(!hitec_pio_answer_due(&run, 1, HITEC_PIO_RELEASE_PC));
    CHECK(!hitec_pio_answer_due(&run, 1, HITEC_PIO_ASK_PC + 1u));
    CHECK(!hitec_pio_answer_due(&run, 1, HITEC_PIO_ANSWER_PC + 3u));
    CHECK(!hitec_pio_answer_due(&run, 1, HITEC_PIO_IDLE_PC));
    CHECK(!hitec_pio_answer_due(&run, 1, HITEC_PIO_DRAIN_PC));
    CHECK(!hitec_pio_answer_due(&run, 0, HITEC_PIO_ASK_PC));
    CHECK(!hitec_pio_answer_due(&run, 2, HITEC_PIO_ASK_PC));
    CHECK(!hitec_pio_answer_due(NULL, 1, HITEC_PIO_ASK_PC));
    run.answer = 0;
    CHECK(!hitec_pio_answer_due(&run, 1, HITEC_PIO_ASK_PC));
}

TEST_CASE(the_span_covers_every_run_of_the_model)
{
    static const uint8_t a[] = { 0x69, 0x00, 0x0C, 0x02, 0x00, 0x20, 0x2E };
    hitec_xfer_t x;

    /* A D read whose answer comes late in its window, 7 bytes, and the
     * line held at the idle level after them: the window runs out. */
    hitec_d_read_xfer(0x00, 0x0C, &x);
    memset(&m, 0, sizeof(m));
    ext_clear(true);
    ext_edge(300000.0, false);
    for (unsigned i = 0; i < sizeof(a); ++i) {
        ext_byte(HITEC_D, 24000000.0 + 10.0 * bit_ns(HITEC_D) * i,
                 bit_ns(HITEC_D), a[i], true);
    }
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 8, 0));
    CHECK(m.finished);
    CHECK(cyc_ns(m.end) < (double)run.span_us * 1000.0);
    /* A listen that answers and takes its count. */
    CHECK(hitec_listen_xfer(HITEC_CLASSIC, 20000u, 2, true, HITEC_C_ACK, &x));
    memset(&m, 0, sizeof(m));
    ext_clear(true);
    ext_byte(HITEC_CLASSIC, 1000.0, bit_ns(HITEC_CLASSIC), 0x2B, true);
    ext_byte(HITEC_CLASSIC, 19000000.0, bit_ns(HITEC_CLASSIC), 0x2B, true);
    CHECK(transact(150000000u, HITEC_DRIVE_OPEN_DRAIN, &x, 2, 0));
    CHECK(m.finished);
    CHECK(cyc_ns(m.end) < (double)run.span_us * 1000.0);
    CHECK((double)run.span_us * 1000.0 < cyc_ns(m.end) + 2e6);
    /* The longest lead and the longest window: more ns than 32 bits hold,
     * so the span is counted in us from the cycles. */
    {
        uint32_t w[HITEC_PIO_TX_WORDS];

        hitec_d_read_xfer(0x00, 0x0C, &x);
        x.lead_us = HITEC_TIME_MAX_US;
        x.window_us = HITEC_TIME_MAX_US;
        CHECK(hitec_pio_clock(150000000u, HITEC_D, &clk));
        CHECK(hitec_pio_tx(&clk, HITEC_DRIVE_OPEN_DRAIN, &x, w, &run) > 0u);
        CHECK(run.span_us >= 2u * HITEC_TIME_MAX_US);
        CHECK(run.span_us < 2u * HITEC_TIME_MAX_US + 2000u);
        /* At 1.2282MHz, the lowest clock classic takes, too. */
        hitec_c_xfer(HITEC_C_READ_EEPROM, 0x05, 0x00, &x);
        x.lead_us = HITEC_TIME_MAX_US;
        x.window_us = HITEC_TIME_MAX_US;
        CHECK(hitec_pio_clock(1228200u, HITEC_CLASSIC, &clk));
        CHECK(hitec_pio_tx(&clk, HITEC_DRIVE_OPEN_DRAIN, &x, w, &run) > 0u);
        CHECK(run.span_us >= 2u * HITEC_TIME_MAX_US);
        CHECK(run.span_us < 2u * HITEC_TIME_MAX_US + 60000u);
    }
}

int main(void)
{
    if (!assemble(HITEC_LINE_PIO)) {
        printf("FAIL  cannot assemble %s\n", HITEC_LINE_PIO);
        return 1;
    }
    RUN(the_words_are_the_ones_the_firmware_build_checks);
    RUN(a_classic_command_is_16_cycles_a_bit_and_never_drives_high);
    RUN(a_classic_reply_comes_back_with_its_times_and_decodes);
    RUN(a_start_bit_at_any_phase_of_the_sampling_is_stamped_within_a_cycle);
    RUN(a_d_read_drives_low_88_us_then_sends_back_to_back_and_releases);
    RUN(a_d_answer_is_taken_with_the_byte_after_it_and_decodes);
    RUN(the_release_after_the_answer_reads_as_a_byte_with_a_bad_stop_bit);
    RUN(a_push_pull_d_line_drives_both_levels_and_releases_after_the_stop);
    RUN(a_listen_answers_the_first_byte_10_bit_times_after_its_start);
    RUN(a_byte_right_after_the_answer_is_taken);
    RUN(a_listen_without_an_answer_never_drives);
    RUN(a_level_holds_the_line_for_its_time_then_releases);
    RUN(a_byte_with_a_low_stop_bit_is_marked_and_the_next_one_is_taken);
    RUN(the_window_takes_a_byte_just_inside_it_and_not_one_just_after);
    RUN(an_abort_releases_the_pad_and_the_next_transaction_runs);
    RUN(bytes_past_the_channel_are_dropped_and_the_pad_ends_released);
    RUN(a_stamp_no_count_gives_is_an_overflow);
    RUN(other_system_clocks_give_the_bit_within_0_05_percent);
    RUN(a_clock_below_4_cycles_a_state_machine_cycle_is_refused);
    RUN(the_stream_pads_its_front_to_a_word_and_refuses_what_it_cannot_run);
    RUN(an_answer_no_byte_called_for_is_drained_at_the_window_end);
    RUN(a_one_byte_listen_holds_its_restart_until_the_answer_is_out);
    RUN(the_span_covers_every_run_of_the_model);
    return test_summary("hitec_pio");
}
