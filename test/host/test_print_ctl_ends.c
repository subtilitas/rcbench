/**
 * @file test_print_ctl_ends.c
 * @brief The label printer's controller (print_ctl.h) on the scripted
 *        stack and printer of print_rig.h, each way a request ends without
 *        a label: every error of print_err_t reached, with its text in
 *        English and German; the printer gone (the
 *        connect unanswered for 10s, and the link dropped in SENDING with a
 *        reconnect inside 5s that prints, one whose counter stays, and
 *        none); a second drop after the reconnect, in the counter check
 *        and in step 11, giving no verdict; a request that does not wait
 *        out a retry gap grown to 30s; an abort while connecting ending
 *        the connect; the MTU at 130 refused and 131 taken; the heap block
 *        refused; a device update running, and print_ctl_busy() for the
 *        update's refusal.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "print_rig.h"
#include "ui_text.h"

/** @brief The rig, too big for a stack frame. */
static pr_rig_t s_r;
/** @brief The stream a label must carry. */
static uint8_t s_want[SIM_STREAM_MAX];

/** @brief Fail the case and return when @p c is false. */
#define REQUIRE(c)                                                         \
    do {                                                                   \
        if (!(c)) {                                                        \
            T_FAIL("required: %s", #c);                                    \
            return;                                                        \
        }                                                                  \
    } while (0)

/** @brief The id the cases print. */
#define ID "KST_X10-0000A"

/**
 * @brief Arm the printer to print the label of ID on its roll.
 * @param r the rig
 */
static void expect_id(pr_rig_t *r)
{
    const label_roll_t roll = { .w_mm = r->sim.roll.w_mm,
                                .h_mm = r->sim.roll.h_mm };
    label_bmp_t bmp;
    const size_t n = pr_expect(&roll, ID, "", "", s_want, sizeof s_want,
                               &bmp);
    r->sim.expect = n != 0u ? s_want : NULL;
    r->sim.expect_len = (uint32_t)n;
}

/**
 * @brief Whether an error's text exists in both languages and is the
 *        table's.
 * @param err  print_err_t
 * @param want its text id
 * @return true when it is
 */
static bool text_is(uint8_t err, ui_text_id_t want)
{
    const ui_text_id_t t = print_err_text(err);
    return t == want && ui_tr_in(UI_LANG_EN, t)[0] != '\0'
           && ui_tr_in(UI_LANG_DE, t)[0] != '\0'
           && strcmp(ui_tr_in(UI_LANG_EN, t), ui_tr_in(UI_LANG_DE, t)) != 0;
}

/**
 * @brief Run a label to its end and check its error.
 * @param r    the rig, set up
 * @param err  the error it must end with
 * @param text its text
 * @param ms   the time it may take
 */
static void ends_with(pr_rig_t *r, uint8_t err, ui_text_id_t text,
                      uint32_t ms)
{
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    CHECK(pr_run(r, ms));
    CHECK_EQ(r->c.phase, PRINT_FAILED);
    CHECK_EQ(r->c.err, err);
    CHECK(text_is(err, text));
    CHECK(r->c.left != 294);
    CHECK_EQ(r->allocs, r->frees);
}

TEST_CASE(the_requests_refused_before_a_link)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    print_ctl_bind(&r->c, "", "", r->now);
    ends_with(r, PRINT_ERR_NO_PRINTER, TX_PRINT_ERR_NO_PRINTER, 10u);
    CHECK_EQ(r->connects, 0u);

    pr_init(r);
    print_ctl_gate(&r->c, false, true, r->now);
    ends_with(r, PRINT_ERR_UPDATE, TX_PRINT_ERR_UPDATE, 10u);
    CHECK_EQ(r->connects, 0u);

    pr_init(r);
    print_ctl_gate(&r->c, true, false, r->now);
    ends_with(r, PRINT_ERR_ARMED, TX_PRINT_ERR_ARMED, 10u);
    CHECK(!pr_ask(r, PRINT_REQ_TEST, NULL, NULL) || r->c.err == PRINT_ERR_ARMED);
    CHECK_EQ(r->connects, 0u);

    pr_init(r);
    print_ctl_radio(&r->c, IMU_RADIO_NO_SYNC, r->now);
    ends_with(r, PRINT_ERR_RADIO, TX_PRINT_ERR_RADIO, 10u);
    /* Bluetooth that starts and never syncs: RADIO at 10s */
    pr_init(r);
    print_ctl_radio(&r->c, IMU_RADIO_STARTING, r->now);
    ends_with(r, PRINT_ERR_RADIO, TX_PRINT_ERR_RADIO, 10001u);
    CHECK_EQ(r->connects, 0u);
}

TEST_CASE(bluetooth_that_syncs_inside_10s_connects)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    print_ctl_radio(&r->c, IMU_RADIO_OFF, r->now);
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    pr_wait(r, 3000u);
    CHECK_EQ(r->connects, 0u);
    print_ctl_radio(&r->c, IMU_RADIO_READY, r->now);
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK(r->sim.printed);
}

TEST_CASE(a_printer_that_never_answers_the_connect_is_not_found_at_10s)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->present = false;
    expect_id(r);
    const uint32_t t0 = r->now;
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    while (r->now - t0 < PRINT_FIND_MS) {
        pr_ms(r);
    }
    CHECK_EQ(r->c.phase, PRINT_CONNECTING);
    pr_ms(r);
    CHECK_EQ(r->c.phase, PRINT_FAILED);
    CHECK_EQ(r->c.err, PRINT_ERR_NOT_FOUND);
    CHECK(text_is(PRINT_ERR_NOT_FOUND, TX_PRINT_ERR_NOT_FOUND));
    CHECK(r->connects >= 1u);
    /* the connects end with it */
    const unsigned n = r->connects;
    pr_wait(r, 20000u);
    CHECK_EQ(r->connects, n);
}

TEST_CASE(a_device_without_the_service_is_not_a_printer)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->no_svc = true;
    ends_with(r, PRINT_ERR_NOT_PRINTER, TX_PRINT_ERR_NOT_PRINTER, 10000u);
}

TEST_CASE(an_mtu_of_130_is_refused_and_131_taken)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->mtu = 130u;
    ends_with(r, PRINT_ERR_MTU, TX_PRINT_ERR_MTU, 10000u);
    CHECK_EQ(r->cmds, 0u);
    CHECK(!bleu_link_up(&r->c.b));
    pr_init(r);
    r->mtu = 131u;
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK(r->sim.printed);
}

TEST_CASE(the_printers_flags_end_the_label_with_their_errors)
{
    static const struct {
        uint8_t byte, bit, err;
        ui_text_id_t text;
    } k[] = {
        { 2u, 0x08u, PRINT_ERR_COVER, TX_PRINT_ERR_COVER },
        { 3u, 0x01u, PRINT_ERR_NO_LABEL, TX_PRINT_ERR_NO_LABEL },
        { 0u, 0x04u, PRINT_ERR_NO_LABEL, TX_PRINT_ERR_NO_LABEL },
        { 0u, 0x02u, PRINT_ERR_LABEL, TX_PRINT_ERR_LABEL },
        { 0u, 0x08u, PRINT_ERR_LABEL, TX_PRINT_ERR_LABEL },
        { 0u, 0x40u, PRINT_ERR_BATTERY, TX_PRINT_ERR_BATTERY },
        { 1u, 0x08u, PRINT_ERR_HOT, TX_PRINT_ERR_HOT },
    };
    pr_rig_t *r = &s_r;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        pr_init(r);
        r->sim.flags[k[i].byte] = k[i].bit;
        ends_with(r, k[i].err, k[i].text, 10000u);
        CHECK_EQ(sim_count(&r->sim, SV_C_START), 0u);
    }
}

TEST_CASE(a_busy_or_silent_printer_and_a_refused_write)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->sim.busy_until = r->now + 60000u;
    ends_with(r, PRINT_ERR_BUSY, TX_PRINT_ERR_BUSY, 10000u);

    pr_init(r);
    r->sim.silent_cmd = SV_C_CHECK;
    ends_with(r, PRINT_ERR_SILENT, TX_PRINT_ERR_SILENT, 10000u);

    pr_init(r);
    r->sim.write_cmd = SV_C_BULK;
    r->sim.write_do = SIM_W_REFUSE;
    ends_with(r, PRINT_ERR_WRITE, TX_PRINT_ERR_WRITE, 10000u);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 1u);
}

TEST_CASE(the_counter_and_the_roll_decide)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->sim.prints = false;
    ends_with(r, PRINT_ERR_NOT_PRINTED, TX_PRINT_ERR_NOT_PRINTED, 20000u);
    CHECK_EQ(sim_count(&r->sim, SV_C_BUF_FULL), 1u);

    pr_init(r);
    r->sim.change_uid = true;
    r->sim.uid_after[0] = 0x55u;
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    CHECK(pr_run(r, 20000u));
    CHECK_EQ(r->c.err, PRINT_ERR_ROLL);
    CHECK(text_is(PRINT_ERR_ROLL, TX_PRINT_ERR_ROLL));
}

TEST_CASE(a_roll_out_of_range_or_zeroed_prints_nothing)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->sim.roll.w_mm = 14u;
    ends_with(r, PRINT_ERR_SMALL, TX_PRINT_ERR_SMALL, 10000u);
    CHECK_EQ(r->allocs, 0u);
    pr_init(r);
    r->sim.roll.h_mm = 101u;
    ends_with(r, PRINT_ERR_LARGE, TX_PRINT_ERR_LARGE, 10000u);
    CHECK_EQ(r->allocs, 0u);
    pr_init(r);
    r->sim.roll.w_mm = 0u;
    r->sim.roll.h_mm = 0u;
    ends_with(r, PRINT_ERR_NO_LABEL, TX_PRINT_ERR_NO_LABEL, 10000u);
    CHECK_EQ(r->allocs, 0u);
}

TEST_CASE(a_refused_block_ends_with_no_memory_and_no_job)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->no_mem = true;
    ends_with(r, PRINT_ERR_MEMORY, TX_PRINT_ERR_MEMORY, 10000u);
    CHECK_EQ(sim_count(&r->sim, SV_C_START), 0u);
    CHECK(r->last_want > 0u);
    /* the link stays for the linger */
    CHECK(bleu_link_up(&r->c.b));
}

TEST_CASE(an_update_ends_a_lingering_link_and_a_job_holds_the_update_off)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    expect_id(r);
    CHECK(!print_ctl_busy(&r->c));
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    CHECK(print_ctl_busy(&r->c));
    /* an update asked during the job leaves it running */
    CHECK(pr_run_to(r, PRINT_SENDING, 5000u));
    print_ctl_gate(&r->c, false, true, r->now);
    CHECK(print_ctl_busy(&r->c));
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK(r->sim.printed);
    /* once it ended, the update takes the spare link */
    pr_wait(r, 20u);
    CHECK(!bleu_link_up(&r->c.b));
    CHECK_EQ(r->discs, 1u);
    CHECK(!print_ctl_busy(&r->c));
}

/**
 * @brief Run a label into SENDING with 2 data writes out, then drop the
 *        link.
 * @param r       the rig
 * @param present the printer answers the reconnects
 */
static void drop_in_sending(pr_rig_t *r, bool present)
{
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    for (uint32_t k = 0; k < 5000u && r->frags < 2u; ++k) {
        pr_ms(r);
    }
    CHECK_EQ(r->c.phase, PRINT_SENDING);
    pr_drop(r, present);
}

TEST_CASE(a_drop_in_sending_with_no_reconnect_is_link_lost)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    drop_in_sending(r, false);
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_FAILED);
    CHECK_EQ(r->c.err, PRINT_ERR_LINK);
    CHECK(text_is(PRINT_ERR_LINK, TX_PRINT_ERR_LINK));
    CHECK(!r->c.relinked);
    CHECK_EQ(r->allocs, r->frees);
    CHECK(r->connects >= 2u);
}

TEST_CASE(a_drop_in_sending_with_a_reconnect_gives_the_counters_verdict)
{
    /* Back inside 5s: the half-sent label never prints, the counter
     * stays: LINK LOST, NOT PRINTED. */
    pr_rig_t *r = &s_r;
    pr_init(r);
    drop_in_sending(r, true);
    CHECK(pr_run(r, 15000u));
    CHECK_EQ(r->c.phase, PRINT_FAILED);
    CHECK_EQ(r->c.err, PRINT_ERR_LINK_NOT_PRINTED);
    CHECK(text_is(PRINT_ERR_LINK_NOT_PRINTED,
                  TX_PRINT_ERR_LINK_NOT_PRINTED));
    CHECK(r->c.relinked);
    CHECK_EQ(r->connects, 2u);
    CHECK_EQ(r->c.left, 295);
    /* never sent a second time */
    CHECK_EQ(sim_count(&r->sim, SV_C_START), 1u);
}

TEST_CASE(a_drop_after_buf_full_with_a_reconnect_reads_printed)
{
    /* The label prints while the link is down: the counter moved, the
     * job is DONE after the reconnect. */
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->sim.print_ms = 1500u;
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    REQUIRE(pr_run_to(r, PRINT_PRINTING, 10000u));
    pr_drop(r, true);
    CHECK(pr_run(r, 20000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK(r->c.relinked);
    CHECK_EQ(r->c.left, 294);
    CHECK_EQ(r->connects, 2u);
    print_view_t v;
    print_ctl_view_of(&r->c, &v);
    CHECK(v.relinked);
}

/**
 * @brief Run until the job is in @p step after its reconnect.
 * @param r    the rig
 * @param step sv_step_t
 * @return true when it got there
 */
static bool to_relinked_step(pr_rig_t *r, uint8_t step)
{
    for (uint32_t k = 0; k < 10000u; ++k) {
        if (r->c.job_on && r->c.job.relinked && r->c.job.step == step) {
            return true;
        }
        pr_ms(r);
    }
    return false;
}

TEST_CASE(a_second_drop_in_the_counter_check_gives_no_verdict)
{
    /* Dropped in SENDING, back inside 5s, dropped again before the
     * counter was read 3 times: no verdict, LINK LOST. */
    pr_rig_t *r = &s_r;
    pr_init(r);
    drop_in_sending(r, true);
    REQUIRE(to_relinked_step(r, SV_STEP_COUNTER));
    pr_drop(r, false);
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_FAILED);
    CHECK_EQ(r->c.err, PRINT_ERR_LINK);
    CHECK(!r->c.job.counted);
}

TEST_CASE(a_second_drop_in_step_11_gives_no_verdict)
{
    /* Dropped after BUF_FULL, back inside 5s, dropped again while step
     * 11 waits for the print: the label may be out, LINK LOST. */
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->sim.print_ms = 3000u;
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    REQUIRE(pr_run_to(r, PRINT_PRINTING, 10000u));
    pr_drop(r, true);
    REQUIRE(to_relinked_step(r, SV_STEP_DONE));
    pr_drop(r, false);
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_FAILED);
    CHECK_EQ(r->c.err, PRINT_ERR_LINK);
}

TEST_CASE(a_printer_gone_after_a_silent_job_is_not_sought_past_the_linger)
{
    /* H1: the printer took the frames and went off while it printed; the
     * job ends PRINTER NOT ANSWERING.  The central may retry within the
     * linger; from PRINT_LINGER_MS after the end on, it seeks nothing. */
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->sim.print_ms = 3000u;
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    REQUIRE(pr_run_to(r, PRINT_PRINTING, 10000u));
    /* off: no answer from here, no connect answered */
    r->sim.silent_cmd = SV_C_INQUIRY;
    r->sim.silent_after = (uint8_t)r->sim.answers[SV_C_INQUIRY];
    r->present = false;
    CHECK(pr_run(r, 20000u));
    CHECK_EQ(r->c.phase, PRINT_FAILED);
    CHECK_EQ(r->c.err, PRINT_ERR_SILENT);
    pr_drop(r, false);
    pr_wait(r, PRINT_LINGER_MS);
    CHECK(!r->c.on);
    const unsigned n = r->connects;
    pr_wait(r, 1000u);
    CHECK_EQ(r->connects, n);
    pr_wait(r, 60000u);
    CHECK_EQ(r->connects, n);
    /* a new request seeks it again */
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    pr_wait(r, 100u);
    CHECK_EQ(r->connects, n + 1u);
}

TEST_CASE(a_request_does_not_wait_out_a_grown_backoff)
{
    /* The printer went off during the linger: the central waits out a
     * retry gap that has grown to 30s.  The printer is back; a request
     * connects at once, not when the gap ends. */
    pr_rig_t *r = &s_r;
    pr_init(r);
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    CHECK(pr_run(r, 10000u));
    REQUIRE(r->c.phase == PRINT_DONE);
    pr_drop(r, false);
    for (uint32_t k = 0; k < 10000u && r->c.b.st != BLEU_ST_WAIT; ++k) {
        pr_ms(r);
    }
    REQUIRE(r->c.b.st == BLEU_ST_WAIT);
    r->c.b.gap_ms = BLEU_BACKOFF_MAX_MS;
    r->c.b.backoff_ms = BLEU_BACKOFF_MAX_MS;
    r->c.b.t_gap = r->now;
    r->present = true;
    sim_init(&r->sim);
    expect_id(r);
    const unsigned n = r->connects;
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    pr_wait(r, 100u);
    CHECK_EQ(r->connects, n + 1u);
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
}

TEST_CASE(an_abort_while_connecting_ends_the_connect)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    r->present = false;
    expect_id(r);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, ID, ""));
    pr_wait(r, 2000u);
    REQUIRE(r->c.phase == PRINT_CONNECTING);
    CHECK(pr_ask(r, PRINT_REQ_ABORT, NULL, NULL));
    CHECK_EQ(r->c.err, PRINT_ERR_ABORTED);
    /* The printer comes on: no connect follows, no link is held. */
    r->present = true;
    const unsigned n = r->connects;
    pr_wait(r, 40000u);
    CHECK_EQ(r->connects, n);
    CHECK(!r->connected);
    CHECK(!bleu_link_up(&r->c.b));
}

int main(void)
{
    RUN(the_requests_refused_before_a_link);
    RUN(bluetooth_that_syncs_inside_10s_connects);
    RUN(a_printer_that_never_answers_the_connect_is_not_found_at_10s);
    RUN(a_device_without_the_service_is_not_a_printer);
    RUN(an_mtu_of_130_is_refused_and_131_taken);
    RUN(the_printers_flags_end_the_label_with_their_errors);
    RUN(a_busy_or_silent_printer_and_a_refused_write);
    RUN(the_counter_and_the_roll_decide);
    RUN(a_roll_out_of_range_or_zeroed_prints_nothing);
    RUN(a_refused_block_ends_with_no_memory_and_no_job);
    RUN(an_update_ends_a_lingering_link_and_a_job_holds_the_update_off);
    RUN(a_drop_in_sending_with_no_reconnect_is_link_lost);
    RUN(a_drop_in_sending_with_a_reconnect_gives_the_counters_verdict);
    RUN(a_drop_after_buf_full_with_a_reconnect_reads_printed);
    RUN(a_second_drop_in_the_counter_check_gives_no_verdict);
    RUN(a_second_drop_in_step_11_gives_no_verdict);
    RUN(a_printer_gone_after_a_silent_job_is_not_sought_past_the_linger);
    RUN(a_request_does_not_wait_out_a_grown_backoff);
    RUN(an_abort_while_connecting_ends_the_connect);
    return test_summary("print_ctl_ends");
}
