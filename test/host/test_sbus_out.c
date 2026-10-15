/*
 * S.BUS and S.BUS2 output on a fake driver: the frame period, the footer
 * rotation, the listening window, the stop, a late or refused frame, the
 * clock's wrap, and the slot table filled from the windows.
 *
 * The fake driver ends a transaction 3.2 ms after its start, plus the
 * window, as the pin driver does, and answers an S.BUS2 window with the
 * records of the sensors it was given, at 660 us or 700 us spacing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "sbus.h"
#include "sbus2.h"
#include "sbus_out.h"

#define MAX_STARTS 64u
#define FRAME_US   3201u

typedef struct {
    uint32_t now;
    bool refuse;
    bool busy;
    uint32_t end_at;
    uint32_t window_us;
    uint8_t footer;
    unsigned n_starts;
    unsigned n_ends;
    uint32_t start_at[MAX_STARTS];
    uint8_t footers[MAX_STARTS];
    uint32_t windows[MAX_STARTS];
    uint8_t last[SBUS_FRAME_BYTES];
    /* The sensors: a record in every slot whose bit is set. */
    uint32_t slots;
    unsigned spacing;
} fake_t;

static bool fake_start(void *ctx, const uint8_t *frame, uint32_t window_us)
{
    fake_t *f = (fake_t *)ctx;

    if (f->refuse || f->busy) {
        return false;
    }
    if (f->n_starts < MAX_STARTS) {
        f->start_at[f->n_starts] = f->now;
        f->footers[f->n_starts] = frame[24];
        f->windows[f->n_starts] = window_us;
    }
    ++f->n_starts;
    memcpy(f->last, frame, SBUS_FRAME_BYTES);
    f->busy = true;
    f->footer = frame[24];
    f->window_us = window_us;
    f->end_at = f->now + FRAME_US + window_us;
    return true;
}

static bool fake_poll(void *ctx, sbus2_capture_t *out)
{
    fake_t *f = (fake_t *)ctx;

    if (!f->busy || (int32_t)(f->now - f->end_at) < 0) {
        return false;
    }
    f->busy = false;
    ++f->n_ends;
    memset(out, 0, sizeof(*out));
    const int g = sbus2_footer_group(f->footer);

    if (f->window_us == 0u || g < 0) {
        return true;
    }
    for (unsigned k = 0; k < SBUS2_GROUP_SLOTS; ++k) {
        const unsigned s = (unsigned)g * 8u + k;

        if ((f->slots & (1u << s)) == 0u) {
            continue;
        }
        const uint8_t b[3] = { sbus2_slot_id(s), (uint8_t)(100u + s),
                               (uint8_t)s };
        for (unsigned i = 0; i < 3u; ++i) {
            sbus2_rx_byte_t *x = &out->byte[out->n++];

            x->data = b[i];
            x->t_ns = (2000u + k * f->spacing + 120u * i) * 1000u;
            x->parity_ok = true;
            x->stop_ok = true;
        }
    }
    return true;
}

static uint32_t fake_now(void *ctx)
{
    return ((fake_t *)ctx)->now;
}

static void setup(fake_t *f, sbus_out_t *o, uint32_t t0)
{
    memset(f, 0, sizeof(*f));
    f->now = t0;
    f->spacing = 660u;
    const sbus_driver_t d = { f, fake_start, fake_poll, fake_now };

    sbus_out_init(o, &d);
}

/* Step every @p step_us for @p total_us. */
static void run(fake_t *f, sbus_out_t *o, uint32_t step_us, uint32_t total_us)
{
    for (uint32_t t = 0; t < total_us; t += step_us) {
        sbus_out_step(o);
        f->now += step_us;
    }
}

TEST_CASE(sbus_frames_go_out_every_14_ms_with_footer_0_and_no_window)
{
    fake_t f;
    sbus_out_t o;
    sbus_frame_t got;
    sbus_decoder_t d;

    setup(&f, &o, 5000);
    CHECK_EQ(o.period_us, SBUS_PERIOD_US);
    CHECK_EQ(o.mode, SBUS_MODE_SBUS);
    run(&f, &o, 100, 50000);
    CHECK_EQ(f.n_starts, 0u);            /* not started */

    sbus_out_start(&o);
    run(&f, &o, 100, 14000u * 10u);
    CHECK_EQ(f.n_starts, 10u);
    for (unsigned i = 0; i < 10u; ++i) {
        CHECK_EQ(f.start_at[i], 55000u + 14000u * i);
        CHECK_EQ(f.footers[i], SBUS_FOOTER);
        CHECK_EQ(f.windows[i], 0u);
    }
    CHECK_EQ(o.frames, 10u);
    CHECK_EQ(o.late, 0u);
    CHECK_EQ(o.refused, 0u);

    /* Every channel at 1500 us, the flags 0. */
    sbus_decoder_reset(&d);
    uint32_t now = 0;
    for (unsigned i = 0; i < SBUS_FRAME_BYTES; ++i, now += 120u) {
        sbus_decode_byte(&d, f.last[i], now, &got);
    }
    CHECK_EQ(d.frames, 1u);
    for (unsigned c = 0; c < SBUS_CHANNELS; ++c) {
        CHECK_EQ(got.channel[c], 992u);
    }
    CHECK(!got.failsafe && !got.frame_lost && !got.ch17 && !got.ch18);
}

TEST_CASE(sbus2_rotates_the_four_footers_and_listens_8_ms_after_each)
{
    fake_t f;
    sbus_out_t o;

    setup(&f, &o, 0);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS2, 14000), SBUS_OUT_OK);
    sbus_out_start(&o);
    run(&f, &o, 100, 14000u * 9u);
    CHECK_EQ(f.n_starts, 9u);
    for (unsigned i = 0; i < 9u; ++i) {
        CHECK_EQ(f.footers[i], sbus2_footer(i));
        CHECK_EQ(f.windows[i], SBUS2_WINDOW_US);
        CHECK_EQ(f.start_at[i], 14000u * i);
    }
    /* A restart begins again at group 0. */
    sbus_out_stop(&o);
    run(&f, &o, 100, 30000);
    sbus_out_start(&o);
    run(&f, &o, 100, 1000);
    CHECK_EQ(f.footers[9], SBUS2_FOOTER_0);
}

TEST_CASE(the_period_takes_7_ms_for_sbus_and_12_ms_up_for_sbus2)
{
    fake_t f;
    sbus_out_t o;

    setup(&f, &o, 0);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS, SBUS_PERIOD_FAST_US),
             SBUS_OUT_OK);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS, 6999), SBUS_OUT_ERR_PERIOD);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS, SBUS_PERIOD_MAX_US),
             SBUS_OUT_OK);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS, SBUS_PERIOD_MAX_US + 1u),
             SBUS_OUT_ERR_PERIOD);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS2, 7000), SBUS_OUT_ERR_PERIOD);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS2, 11999), SBUS_OUT_ERR_PERIOD);
    CHECK_EQ(sbus_out_config(&o, 2, 14000), SBUS_OUT_ERR_ARG);
    CHECK_EQ(sbus_out_config(NULL, SBUS_MODE_SBUS, 14000), SBUS_OUT_ERR_ARG);
    /* The refusals changed nothing. */
    CHECK_EQ(o.mode, SBUS_MODE_SBUS);
    CHECK_EQ(o.period_us, SBUS_PERIOD_MAX_US);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS2, SBUS2_PERIOD_MIN_US),
             SBUS_OUT_OK);

    /* 7 ms S.BUS: the 3.2 ms frame ends before the next is due. */
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS, 7000), SBUS_OUT_OK);
    sbus_out_start(&o);
    run(&f, &o, 100, 70000);
    CHECK_EQ(f.n_starts, 10u);
    for (unsigned i = 1; i < 10u; ++i) {
        CHECK_EQ(f.start_at[i] - f.start_at[i - 1u], 7000u);
    }
    /* 12 ms S.BUS2: frame and window end 11.2 ms after the start. */
    setup(&f, &o, 0);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS2, 12000), SBUS_OUT_OK);
    sbus_out_start(&o);
    run(&f, &o, 100, 120000);
    CHECK_EQ(f.n_starts, 10u);
    CHECK_EQ(o.late, 0u);
    CHECK_EQ(f.n_ends, 10u);
}

TEST_CASE(a_coarse_step_jitters_the_start_but_the_schedule_does_not_drift)
{
    fake_t f;
    sbus_out_t o;

    setup(&f, &o, 333);
    sbus_out_start(&o);
    run(&f, &o, 900, 14000u * 40u);
    CHECK(f.n_starts >= 40u);
    for (unsigned i = 0; i < 40u; ++i) {
        const uint32_t due = 333u + 14000u * i;

        if (f.start_at[i] < due || f.start_at[i] - due >= 900u) {
            T_FAIL("frame %u at %u us, due %u us", i, f.start_at[i], due);
        }
    }
    CHECK_EQ(o.late, 0u);
}

TEST_CASE(a_step_a_whole_period_late_sends_one_frame_and_restarts_the_schedule)
{
    fake_t f;
    sbus_out_t o;

    setup(&f, &o, 0);
    sbus_out_start(&o);
    run(&f, &o, 100, 14000);          /* frame 0 at 0 */
    CHECK_EQ(f.n_starts, 1u);
    f.now = 50000;                    /* no step for 36 ms */
    sbus_out_step(&o);
    CHECK_EQ(f.n_starts, 2u);
    CHECK_EQ(o.late, 1u);
    run(&f, &o, 100, 14000u * 3u);
    CHECK_EQ(f.start_at[2], 64000u);
    CHECK_EQ(f.start_at[3], 78000u);
    CHECK_EQ(o.late, 1u);
}

TEST_CASE(stop_sends_no_frame_after_it_and_reads_the_window_in_flight)
{
    fake_t f;
    sbus_out_t o;

    setup(&f, &o, 0);
    f.slots = 1u << 1;
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS2, 14000), SBUS_OUT_OK);
    sbus_out_start(&o);
    sbus_out_step(&o);
    CHECK_EQ(f.n_starts, 1u);
    CHECK(f.busy);
    sbus_out_stop(&o);
    run(&f, &o, 100, 100000);
    CHECK_EQ(f.n_starts, 1u);
    CHECK(!f.busy);
    CHECK(!o.busy);
    CHECK_EQ(o.table.windows, 1u);
    CHECK(sbus2_slot_live(&o.table, 1));
    CHECK_EQ(o.table.slot[1].data[0], 101u);

    /* A frame in flight holds the next one back until it has ended. */
    setup(&f, &o, 0);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS, 7000), SBUS_OUT_OK);
    sbus_out_start(&o);
    sbus_out_step(&o);
    f.end_at = 9000;                  /* the driver takes 9 ms */
    run(&f, &o, 100, 9500);
    CHECK_EQ(f.n_starts, 2u);
    CHECK_EQ(f.start_at[1], 9000u);
    run(&f, &o, 100, 4600);
    CHECK_EQ(f.start_at[2], 14000u);   /* back on the schedule */
}

TEST_CASE(a_refused_frame_is_counted_and_skipped_without_moving_the_footer)
{
    fake_t f;
    sbus_out_t o;

    setup(&f, &o, 0);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS2, 14000), SBUS_OUT_OK);
    sbus_out_start(&o);
    f.refuse = true;
    run(&f, &o, 100, 14000u * 2u);
    CHECK_EQ(o.refused, 2u);
    CHECK_EQ(f.n_starts, 0u);
    f.refuse = false;
    run(&f, &o, 100, 14000);
    CHECK_EQ(f.n_starts, 1u);
    CHECK_EQ(f.start_at[0], 28000u);
    CHECK_EQ(f.footers[0], SBUS2_FOOTER_0);
}

TEST_CASE(the_slot_table_fills_from_the_windows_at_both_spacings)
{
    for (unsigned spacing = 660; spacing <= 700u; spacing += 40u) {
        fake_t f;
        sbus_out_t o;
        /* Temperature at 1, GPS at 8 to 15, the servo sensor at 16 to 21,
         * current at 24 to 26. */
        const uint32_t slots = (1u << 1) | 0xFF00u | (0x3Fu << 16)
                               | (0x7u << 24);

        setup(&f, &o, 0);
        f.slots = slots;
        f.spacing = spacing;
        CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS2, 14000), SBUS_OUT_OK);
        sbus_out_start(&o);
        run(&f, &o, 100, 14000u * 4u);
        CHECK_EQ(o.table.windows, 4u);
        for (unsigned s = 0; s < SBUS2_SLOTS; ++s) {
            const bool want = (slots & (1u << s)) != 0u;

            CHECK_EQ(sbus2_slot_live(&o.table, s), want);
            if (want) {
                CHECK_EQ(o.table.slot[s].data[0], 100u + s);
                CHECK_EQ(o.table.slot[s].data[1], s);
            }
        }
        CHECK_EQ(o.table.taken, 1u + 8u + 6u + 3u);
        CHECK_EQ(o.table.early + o.table.late + o.table.wrong_id, 0u);

        /* The sensors go quiet: three windows of each group later no slot
         * is live and every value is still there. */
        f.slots = 0;
        run(&f, &o, 100, 14000u * 4u * SBUS2_LIVE_WINDOWS);
        for (unsigned s = 0; s < SBUS2_SLOTS; ++s) {
            CHECK(!sbus2_slot_live(&o.table, s));
            CHECK_EQ(o.table.slot[s].valid, (slots & (1u << s)) != 0u);
        }
        sbus_out_clear_slots(&o);
        CHECK_EQ(o.table.windows, 0u);
        CHECK(!o.table.slot[1].valid);
    }
}

TEST_CASE(channels_and_flags_reach_the_next_frame_and_are_clamped)
{
    fake_t f;
    sbus_out_t o;
    sbus_decoder_t d;
    sbus_frame_t got;

    setup(&f, &o, 0);
    CHECK(sbus_out_set_us(&o, 0, 1000));
    CHECK(sbus_out_set_us(&o, 1, 2000));
    CHECK(sbus_out_set_us(&o, 2, 3000));         /* 2047 */
    CHECK(sbus_out_set_raw(&o, 3, 0));
    CHECK(sbus_out_set_raw(&o, 4, 2047));
    CHECK(sbus_out_set_raw(&o, 5, 4000));        /* clamped */
    CHECK(!sbus_out_set_raw(&o, 16, 1000));
    CHECK(!sbus_out_set_us(&o, 16, 1500));
    CHECK(!sbus_out_set_raw(NULL, 0, 1000));
    sbus_out_set_flags(&o, true, false, true, false);
    sbus_out_start(&o);
    sbus_out_step(&o);

    sbus_decoder_reset(&d);
    uint32_t now = 0;
    for (unsigned i = 0; i < SBUS_FRAME_BYTES; ++i, now += 120u) {
        sbus_decode_byte(&d, f.last[i], now, &got);
    }
    CHECK_EQ(d.frames, 1u);
    CHECK_EQ(got.channel[0], 172u);
    CHECK_EQ(got.channel[1], 1811u);
    CHECK_EQ(got.channel[2], 2047u);
    CHECK_EQ(got.channel[3], 0u);
    CHECK_EQ(got.channel[4], 2047u);
    CHECK_EQ(got.channel[5], 2047u);
    CHECK_EQ(got.channel[6], 992u);
    CHECK(got.ch17);
    CHECK(!got.ch18);
    CHECK(got.frame_lost);
    CHECK(!got.failsafe);
    sbus_out_set_flags(NULL, true, true, true, true);
}

TEST_CASE(the_schedule_runs_on_across_the_wrap_of_the_microsecond_clock)
{
    fake_t f;
    sbus_out_t o;
    const uint32_t t0 = 0xFFFFFFFFu - 30000u;

    setup(&f, &o, t0);
    CHECK_EQ(sbus_out_config(&o, SBUS_MODE_SBUS2, 14000), SBUS_OUT_OK);
    f.slots = 1u;
    sbus_out_start(&o);
    run(&f, &o, 100, 14000u * 6u);
    CHECK_EQ(f.n_starts, 6u);
    for (unsigned i = 1; i < 6u; ++i) {
        CHECK_EQ(f.start_at[i] - f.start_at[i - 1u], 14000u);
    }
    CHECK_EQ(o.late, 0u);
    CHECK_EQ(o.table.slot[0].records, 2u);
}

TEST_CASE(null_and_a_driver_without_functions_do_nothing)
{
    sbus_out_t o;
    const sbus_driver_t none = { NULL, NULL, NULL, NULL };

    sbus_out_init(NULL, &none);
    sbus_out_step(NULL);
    sbus_out_start(NULL);
    sbus_out_stop(NULL);
    sbus_out_clear_slots(NULL);
    sbus_out_init(&o, NULL);
    sbus_out_start(&o);
    CHECK(!o.running);
    sbus_out_step(&o);
    sbus_out_init(&o, &none);
    sbus_out_step(&o);
    CHECK_EQ(o.frames, 0u);
}

int main(void)
{
    RUN(sbus_frames_go_out_every_14_ms_with_footer_0_and_no_window);
    RUN(sbus2_rotates_the_four_footers_and_listens_8_ms_after_each);
    RUN(the_period_takes_7_ms_for_sbus_and_12_ms_up_for_sbus2);
    RUN(a_coarse_step_jitters_the_start_but_the_schedule_does_not_drift);
    RUN(a_step_a_whole_period_late_sends_one_frame_and_restarts_the_schedule);
    RUN(stop_sends_no_frame_after_it_and_reads_the_window_in_flight);
    RUN(a_refused_frame_is_counted_and_skipped_without_moving_the_footer);
    RUN(the_slot_table_fills_from_the_windows_at_both_spacings);
    RUN(channels_and_flags_reach_the_next_frame_and_are_clamped);
    RUN(the_schedule_runs_on_across_the_wrap_of_the_microsecond_clock);
    RUN(null_and_a_driver_without_functions_do_nothing);
    return test_summary("sbus_out");
}
