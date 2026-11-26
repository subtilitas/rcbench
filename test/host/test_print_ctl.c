/**
 * @file test_print_ctl.c
 * @brief The label printer's controller (print_ctl.h) on the scripted
 *        stack and printer of print_rig.h: a label from the request to
 *        DONE, with the GATT steps H0 settles (no descriptor discovery,
 *        the CCCD at 0x000F written and never read back, every write a
 *        write request of 16 or 128 bytes); a 23-character id with spaces
 *        and a 23-character DUT NAME through to the stream, decoded and
 *        compared with the layout's bitmap; the test label; a QUERY; a
 *        request during a job refused; an abort in SENDING; the linger's
 *        disconnect at 30s and a second label inside it on the same link;
 *        a scan that lists the printer by its address prefix with no
 *        service in its advertising; the frame task's requests.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "lzlit_dec.h"
#include "print_rig.h"

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

/**
 * @brief Arm the rig's printer to print the label of @p id on its roll.
 * @param r   the rig
 * @param id  the id, NULL for the test label
 * @param dut DUT NAME
 * @return the stream's length
 */
static size_t expect_label(pr_rig_t *r, const char *id, const char *dut)
{
    const label_roll_t roll = { .w_mm = r->sim.roll.w_mm,
                                .h_mm = r->sim.roll.h_mm };
    label_bmp_t bmp;
    const size_t n = pr_expect(&roll, id, dut, "0.23.17", s_want,
                               sizeof s_want, &bmp);
    r->sim.expect = s_want;
    r->sim.expect_len = (uint32_t)n;
    return n;
}

TEST_CASE(a_label_goes_from_the_request_to_done)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    const size_t n = expect_label(r, "KST_X10-0000A", "");
    REQUIRE(n > 0u);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, "KST_X10-0000A", ""));
    CHECK_EQ(r->c.phase, PRINT_CONNECTING);
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK_EQ(r->c.err, PRINT_OK);
    CHECK_EQ(r->c.left, 294);
    CHECK(r->sim.printed);
    CHECK_EQ(r->sim.bad_frames, 0u);
    CHECK_EQ(r->sim.nstream, (uint32_t)sv_chunk_count((uint32_t)n) * SV_CHUNK);
    CHECK(memcmp(r->sim.stream, s_want, n) == 0);
    /* one hurried connect; the H0 GATT steps */
    CHECK_EQ(r->connects, 1u);
    CHECK_EQ(r->dsc_disc, 0u);
    CHECK_EQ(r->subs, 1u);
    CHECK_EQ(r->sub_h, PR_FFE1_CCCD);
    CHECK_EQ(r->reads, 0u);
    CHECK_EQ(r->bad_ext, 0u);
    CHECK_EQ(r->frags, 4u * sv_chunk_count((uint32_t)n));
    /* the block: given once, back once, 41,763 bytes or less for 40 x
     * 30mm */
    CHECK_EQ(r->allocs, 1u);
    CHECK_EQ(r->frees, 1u);
    CHECK(r->last_want <= 41763u);
    /* the view */
    print_view_t v;
    print_ctl_view_of(&r->c, &v);
    CHECK_EQ(v.phase, PRINT_DONE);
    CHECK_EQ(v.w_mm, 40);
    CHECK_EQ(v.h_mm, 30);
    CHECK_EQ(v.gap_mm, 3);
    CHECK_EQ(v.left, 294);
    CHECK_EQ(v.mtu, 240);
    CHECK(v.linked);
    CHECK_STR_EQ(v.id, "KST_X10-0000A");
    CHECK_STR_EQ(v.printer, "T0205C2608247368");
    CHECK(!print_ctl_busy(&r->c));
}

TEST_CASE(a_23_character_id_with_spaces_and_dut_name_reach_the_stream)
{
    static const char id[] = "KST X10 SERVO ABC-0000Z";
    static const char dut[] = "WING LEFT AILERON 12345";
    CHECK_EQ(strlen(id), 23u);
    CHECK_EQ(strlen(dut), 23u);
    pr_rig_t *r = &s_r;
    pr_init(r);
    const size_t n = expect_label(r, id, dut);
    REQUIRE(n > 0u);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, id, dut));
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK(r->sim.printed);
    /* The stream the printer joined, decoded, is the layout's print
     * buffers; their columns are the layout's bitmap. */
    static uint8_t raw[16u * SV_BUF_SIZE];
    const size_t got = lzd_decode(r->sim.stream, n, raw, sizeof raw);
    const label_roll_t roll = { .w_mm = 40u, .h_mm = 30u };
    label_bmp_t bmp;
    static uint8_t again[SIM_STREAM_MAX];
    (void)pr_expect(&roll, id, dut, "", again, sizeof again, &bmp);
    REQUIRE(got == (size_t)sv_buf_count(bmp.cols) * SV_BUF_SIZE);
    size_t col = 0u;
    for (uint16_t i = 0; i < sv_buf_count(bmp.cols); ++i) {
        const uint8_t *b = raw + (size_t)i * SV_BUF_SIZE;
        const unsigned cols = (unsigned)(b[4] | (b[5] << 8));
        CHECK(memcmp(b + SV_BUF_HEAD, bmp.bits + col * LABEL_COL_BYTES,
                     cols * LABEL_COL_BYTES) == 0);
        col += cols;
    }
    CHECK_EQ(col, bmp.cols);
    /* the id and the name are drawn: the layout kept both lines */
    CHECK(s_pr_info.line[0].face != NULL);
    CHECK(s_pr_info.line[2].face != NULL);
}

TEST_CASE(the_test_label_prints_the_version)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    REQUIRE(expect_label(r, NULL, "") > 0u);
    CHECK(pr_ask(r, PRINT_REQ_TEST, NULL, NULL));
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK(r->sim.printed);
    print_view_t v;
    print_ctl_view_of(&r->c, &v);
    CHECK_STR_EQ(v.id, "");
    CHECK_EQ(v.kind, PRINT_REQ_TEST);
}

TEST_CASE(a_query_reads_the_roll_and_prints_nothing)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK(pr_run(r, 5000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK_EQ(r->c.left, 295);
    CHECK_EQ(sim_count(&r->sim, SV_C_START), 0u);
    CHECK_EQ(r->allocs, 0u);
    /* a QUERY while ARMED is taken: it prints nothing */
    pr_init(r);
    print_ctl_gate(&r->c, true, false, r->now);
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK(pr_run(r, 5000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
}

TEST_CASE(a_request_during_a_job_is_refused_and_changes_nothing)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    REQUIRE(expect_label(r, "KST_X10-0000A", "") > 0u);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, "KST_X10-0000A", ""));
    CHECK(pr_run_to(r, PRINT_SENDING, 5000u));
    const uint32_t rev = r->c.rev;
    CHECK(!pr_ask(r, PRINT_REQ_LABEL, "KST_X10-0000B", ""));
    CHECK(!pr_ask(r, PRINT_REQ_TEST, NULL, NULL));
    CHECK(!pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK_EQ(r->c.rev, rev);
    CHECK_STR_EQ(r->c.req.id, "KST_X10-0000A");
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK(r->sim.printed);
    /* a label whose id is not a servo id is refused too */
    CHECK(!pr_ask(r, PRINT_REQ_LABEL, "KST_X10-000A", ""));
    CHECK(!pr_ask(r, PRINT_REQ_LABEL, "", ""));
    /* an abort with nothing in work does nothing */
    CHECK(!pr_ask(r, PRINT_REQ_ABORT, NULL, NULL));
}

TEST_CASE(an_abort_in_sending_frees_the_block_and_stops_the_print)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    REQUIRE(expect_label(r, "KST_X10-0000A", "") > 0u);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, "KST_X10-0000A", ""));
    for (uint32_t k = 0; k < 5000u && r->frags < 2u; ++k) {
        pr_ms(r);
    }
    REQUIRE(r->c.phase == PRINT_SENDING);
    CHECK_EQ(r->allocs, 1u);
    CHECK_EQ(r->frees, 0u);
    CHECK(pr_ask(r, PRINT_REQ_ABORT, NULL, NULL));
    CHECK(pr_run(r, 3000u));
    CHECK_EQ(r->c.phase, PRINT_FAILED);
    CHECK_EQ(r->c.err, PRINT_ERR_ABORTED);
    CHECK_EQ(print_err_text(r->c.err), TX_PRINT_ERR_ABORTED);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 1u);
    CHECK_EQ(r->frees, 1u);
    CHECK(!r->sim.printed);
    /* the link stays for the linger */
    CHECK(bleu_link_up(&r->c.b));
}

TEST_CASE(the_linger_ends_the_link_at_30s_and_a_label_inside_it_reuses_it)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    REQUIRE(expect_label(r, "KST_X10-0000A", "") > 0u);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, "KST_X10-0000A", ""));
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    const uint32_t quiet = r->c.t_quiet;
    /* a second label 10s later: no connect, the same link */
    pr_wait(r, 10000u);
    CHECK_EQ(r->connects, 1u);
    sim_init(&r->sim);
    r->sim.roll.left = 294u;
    REQUIRE(expect_label(r, "KST_X10-0000B", "") > 0u);
    CHECK(pr_ask(r, PRINT_REQ_LABEL, "KST_X10-0000B", ""));
    CHECK_EQ(r->c.phase, PRINT_READING);
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK_EQ(r->c.left, 293);
    CHECK_EQ(r->connects, 1u);
    CHECK_EQ(r->allocs, 2u);
    CHECK_EQ(r->frees, 2u);
    CHECK(r->c.t_quiet != quiet);
    /* the linger: still up 29,999ms after the last answer, down at
     * 30,000ms */
    const uint32_t last = r->c.t_quiet;
    while (r->now - last < PRINT_LINGER_MS) {
        pr_ms(r);
    }
    CHECK_EQ(r->discs, 0u);
    CHECK(bleu_link_up(&r->c.b));
    pr_ms(r);
    CHECK_EQ(r->discs, 1u);
    pr_wait(r, 20u);
    CHECK(!bleu_link_up(&r->c.b));
    /* and it stays down: no connect without a request */
    pr_wait(r, 5000u);
    CHECK_EQ(r->connects, 1u);
}

TEST_CASE(a_scan_lists_the_printer_by_its_address_prefix)
{
    /* H0's advertising: flags 02, the name, manufacturer data; no
     * service UUID. */
    static const uint8_t ad[] = {
        0x02, 0x01, 0x02, 0x11, 0x09, 'T', '0', '2', '0', '5', 'C', '2',
        '6', '0', '8', '2', '4', '7', '3', '6', '8', 0x09, 0xFF, 0x00,
        0x00, 0xA4, 0x93, 0x40, 0x30, 0xFA, 0x71 };
    bleu_list_t l;
    bleu_list_clear(&l);
    bleu_addr_t a;
    REQUIRE(bleu_addr_parse(PR_ADDR, &a));
    bleu_list_report_for(&l, &print_profile, &a, -55, ad, sizeof ad);
    CHECK_EQ(l.found_n, 1u);
    CHECK((l.found[0].flags & BLEU_DEV_OUI) != 0u);
    CHECK_STR_EQ(l.found[0].name, "T0205C2608247368");
    /* another prefix, or the prefix as a random address, is not listed */
    bleu_addr_t o;
    REQUIRE(bleu_addr_parse("A4:93:41:30:FA:71 P", &o));
    bleu_list_report_for(&l, &print_profile, &o, -50, ad, sizeof ad);
    REQUIRE(bleu_addr_parse("A4:93:40:30:FA:72 R", &o));
    bleu_list_report_for(&l, &print_profile, &o, -50, ad, sizeof ad);
    CHECK_EQ(l.found_n, 1u);
    CHECK(bleu_profile_valid(&print_profile));
}

TEST_CASE(the_frame_tasks_requests_wait_for_the_view)
{
    print_ctl_reset();
    CHECK_EQ(print_ctl_view()->left, -1);
    print_req_t q;
    CHECK(!print_ctl_take(&q));
    CHECK(print_ctl_label("KST_X10-0000A", "WING"));
    CHECK(!print_ctl_test());             /* one waits */
    CHECK(print_ctl_take(&q));
    CHECK_EQ(q.kind, PRINT_REQ_LABEL);
    CHECK_STR_EQ(q.id, "KST_X10-0000A");
    CHECK_STR_EQ(q.dut, "WING");
    CHECK(!print_ctl_take(&q));
    /* a view in work refuses the next */
    print_view_t v;
    memset(&v, 0, sizeof(v));
    v.phase = PRINT_SENDING;
    print_ctl_show(&v);
    CHECK(!print_ctl_test());
    print_ctl_query();
    CHECK(!print_ctl_take(&q));
    /* the abort goes whatever is in work */
    print_ctl_abort();
    CHECK(print_ctl_take(&q));
    CHECK_EQ(q.kind, PRINT_REQ_ABORT);
    v.phase = PRINT_FAILED;
    print_ctl_show(&v);
    CHECK(print_ctl_test());
    CHECK(print_ctl_take(&q));
    CHECK_EQ(q.kind, PRINT_REQ_TEST);
    CHECK(!print_ctl_label("not an id", ""));
    print_ctl_query();
    CHECK(print_ctl_take(&q));
    CHECK_EQ(q.kind, PRINT_REQ_QUERY);
    print_ctl_reset();
}

int main(void)
{
    RUN(a_label_goes_from_the_request_to_done);
    RUN(a_23_character_id_with_spaces_and_dut_name_reach_the_stream);
    RUN(the_test_label_prints_the_version);
    RUN(a_query_reads_the_roll_and_prints_nothing);
    RUN(a_request_during_a_job_is_refused_and_changes_nothing);
    RUN(an_abort_in_sending_frees_the_block_and_stops_the_print);
    RUN(the_linger_ends_the_link_at_30s_and_a_label_inside_it_reuses_it);
    RUN(a_scan_lists_the_printer_by_its_address_prefix);
    RUN(the_frame_tasks_requests_wait_for_the_view);
    return test_summary("print_ctl");
}
