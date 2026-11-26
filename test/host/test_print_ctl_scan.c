/**
 * @file test_print_ctl_scan.c
 * @brief The label printer's scan (print_ctl.h, PRINT_REQ_SCAN) on the
 *        scripted stack of print_rig.h: the list holds only public
 *        addresses A4:93:40:xx:xx:xx, with their names and RSSI, never a
 *        device for the service it advertises, and no connect follows the
 *        scan; a random address binds nothing; the refusals while a job or a scan
 *        runs, while ARMED and while a device update runs, and the busy
 *        flag the update reads; a switch to ARMED stops the scan; a label
 *        stops it and prints; the wait for Bluetooth and its end at 10s; a
 *        lingering link ends before a scan, which then lists the bound
 *        printer, and a job's link is left; a bind of the bound address
 *        keeps the roll and labels left, another address clears them; the
 *        frame task's ask.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "print_rig.h"

/** @brief The rig, too big for a stack frame. */
static pr_rig_t s_r;

/** @brief Fail the case and return when @p c is false. */
#define REQUIRE(c)                                                         \
    do {                                                                   \
        if (!(c)) {                                                        \
            T_FAIL("required: %s", #c);                                    \
            return;                                                        \
        }                                                                  \
    } while (0)

/** @brief The stream a label must carry. */
static uint8_t s_want[SIM_STREAM_MAX];

/** @brief Arm the rig's printer to print the label of @p id, @p dut. */
static void expect_label(pr_rig_t *r, const char *id, const char *dut)
{
    const label_roll_t roll = { .w_mm = r->sim.roll.w_mm,
                                .h_mm = r->sim.roll.h_mm };
    label_bmp_t bmp;
    const size_t n = pr_expect(&roll, id, dut, "0.23.17", s_want,
                               sizeof s_want, &bmp);
    r->sim.expect = s_want;
    r->sim.expect_len = (uint32_t)n;
}

/** @brief The view now. */
static const print_view_t *view(pr_rig_t *r)
{
    static print_view_t v;
    print_ctl_view_of(&r->c, &v);
    return &v;
}

/** @brief @p ms of the rig's clock, the poll and the actions each ms. */
static void run_ms(pr_rig_t *r, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        pr_ms(r);
    }
}

TEST_CASE(a_scan_lists_only_the_ouis_printers_and_connects_nothing)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    CHECK(print_ctl_busy(&r->c));
    CHECK(view(r)->scanning);
    run_ms(r, 5u);
    CHECK_EQ(r->scans, 1u);
    REQUIRE(r->tok_scan != 0u);
    /* the bound printer, another Supvan printer, a device of another
     * maker with the same name, the prefix as a random address */
    pr_adv(r, PR_ADDR, "T0205C2608247368", -55);
    pr_adv(r, "A4:93:40:00:00:01 P", "T0205C2600000001", -70);
    pr_adv(r, "11:22:33:44:55:66 P", "T0205C2608247368", -40);
    pr_adv(r, "A4:93:40:30:FA:72 R", "T0205C2608247368", -45);
    /* the printer's service UUID from a random and from a public address
     * of another maker: listed by the prefix alone, so neither */
    pr_adv_svc(r, "5A:11:22:33:44:55 R", -30);
    pr_adv_svc(r, "00:1B:DC:01:02:03 P", -35);
    run_ms(r, 2000u);
    const print_view_t *v = view(r);
    CHECK(v->scanning);
    CHECK_EQ(v->scan_left_s, 8u);
    CHECK_EQ(v->found_n, 2u);
    CHECK_STR_EQ(v->found[0].addr, "A4:93:40:30:FA:71 P");
    CHECK_STR_EQ(v->found[0].name, "T0205C2608247368");
    CHECK_EQ(v->found[0].rssi, -55);
    CHECK_STR_EQ(v->found[1].addr, "A4:93:40:00:00:01 P");
    CHECK_EQ(v->found[1].rssi, -70);
    /* the scan ends after 10s: the list stays, the central goes off */
    run_ms(r, 8100u);
    v = view(r);
    CHECK(!v->scanning);
    CHECK(v->scan_done);
    CHECK(!v->scan_cut);
    CHECK_EQ(v->found_n, 2u);
    CHECK(!print_ctl_busy(&r->c));
    CHECK(!r->c.on);
    run_ms(r, 2000u);
    CHECK_EQ(r->connects, 0u);
    CHECK_EQ(view(r)->phase, PRINT_IDLE);
}

TEST_CASE(a_scan_is_refused_during_a_job_a_scan_an_arm_and_an_update)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    /* a job runs */
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK(!pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    CHECK_EQ(view(r)->scan_refused, 1u);
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK_EQ(r->scans, 0u);
    /* ARMED, then an update */
    print_ctl_gate(&r->c, true, false, r->now);
    CHECK(!pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    print_ctl_gate(&r->c, false, true, r->now);
    CHECK(!pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    CHECK(view(r)->update);
    print_ctl_gate(&r->c, false, false, r->now);
    CHECK(!view(r)->update);
    /* a scan runs: the second is refused, and the update reads busy */
    CHECK(pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    CHECK(!pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    CHECK(print_ctl_busy(&r->c));
    CHECK_EQ(view(r)->scan_refused, 4u);
    run_ms(r, 500u);
    CHECK_EQ(r->scans, 1u);
}

TEST_CASE(a_switch_to_armed_stops_the_scan)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    run_ms(r, 5u);
    pr_adv(r, PR_ADDR, "T0205C2608247368", -55);
    run_ms(r, 100u);
    print_ctl_gate(&r->c, true, false, r->now);
    run_ms(r, 5u);
    const print_view_t *v = view(r);
    CHECK(!v->scanning);
    CHECK(v->scan_done);
    CHECK(v->scan_cut);
    CHECK_EQ(v->found_n, 1u);
    CHECK_EQ(r->scan_stops, 1u);
    CHECK(!print_ctl_busy(&r->c));
}

TEST_CASE(a_label_stops_the_scan_and_prints)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    run_ms(r, 500u);
    expect_label(r, "KST_X10-0000A", "WING");
    CHECK(pr_ask(r, PRINT_REQ_LABEL, "KST_X10-0000A", "WING"));
    CHECK(!view(r)->scanning);
    CHECK_EQ(r->c.phase, PRINT_CONNECTING);
    CHECK(pr_run(r, 15000u));
    CHECK_EQ(r->c.err, PRINT_OK);
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK_EQ(r->scan_stops, 1u);
    CHECK_EQ(r->connects, 1u);
    CHECK(r->sim.printed);
}

TEST_CASE(a_scan_waits_up_to_10s_for_bluetooth)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    print_ctl_radio(&r->c, IMU_RADIO_STARTING, r->now);
    CHECK(pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    run_ms(r, 2000u);
    CHECK(view(r)->scanning);
    CHECK_EQ(view(r)->scan_left_s, 10u);
    CHECK_EQ(r->scans, 0u);
    print_ctl_radio(&r->c, IMU_RADIO_READY, r->now);
    run_ms(r, 5u);
    CHECK_EQ(r->scans, 1u);
    run_ms(r, 10100u);
    CHECK(view(r)->scan_done);
    /* Bluetooth that never comes: refused at 10s, no list */
    print_ctl_radio(&r->c, IMU_RADIO_STARTING, r->now);
    CHECK(pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    CHECK(!view(r)->scan_done);
    run_ms(r, 9999u);
    CHECK(view(r)->scanning);
    run_ms(r, 2u);
    const print_view_t *v = view(r);
    CHECK(!v->scanning);
    CHECK(!v->scan_done);
    CHECK_EQ(v->scan_refused, 1u);
    CHECK_EQ(r->scans, 1u);
    /* a radio whose link failed refuses at once */
    print_ctl_radio(&r->c, IMU_RADIO_NO_LINK, r->now);
    CHECK(!pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    CHECK_EQ(view(r)->scan_refused, 2u);
}

TEST_CASE(a_lingering_link_ends_before_a_scan)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK(pr_run(r, 10000u));
    REQUIRE(r->connected);
    run_ms(r, 1000u);
    CHECK(pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    CHECK(view(r)->scanning);
    /* the link ends at once; the scan waits for its disconnect */
    run_ms(r, 2u);
    CHECK_EQ(r->discs, 1u);
    CHECK(!r->connected);
    CHECK_EQ(r->scans, 0u);
    CHECK_EQ(view(r)->scan_left_s, 10u);
    run_ms(r, 10u);
    CHECK_EQ(r->scans, 1u);
    REQUIRE(r->tok_scan != 0u);
    /* unlinked, the bound printer advertises again and is listed */
    pr_adv(r, PR_ADDR, "T0205C2608247368", -52);
    run_ms(r, 2000u);
    const print_view_t *v = view(r);
    CHECK(v->scanning);
    CHECK_EQ(v->found_n, 1u);
    CHECK_STR_EQ(v->found[0].addr, PR_ADDR);
    CHECK_EQ(v->found[0].rssi, -52);
    /* the scan ends 10s after it started: no connect followed it, the
     * roll the QUERY read stays */
    run_ms(r, 8100u);
    v = view(r);
    CHECK(!v->scanning);
    CHECK(v->scan_done);
    CHECK_EQ(v->found_n, 1u);
    CHECK(!r->c.on);
    CHECK_EQ(r->connects, 1u);
    CHECK_EQ(v->w_mm, r->sim.roll.w_mm);
    CHECK(v->left >= 0);
    /* the next request links again */
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK_EQ(r->connects, 2u);
}

TEST_CASE(a_scan_during_a_job_leaves_the_link)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    REQUIRE(pr_run_to(r, PRINT_READING, 10000u));
    CHECK(!pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    CHECK(r->connected);
    CHECK_EQ(r->discs, 0u);
    CHECK(pr_run(r, 10000u));
    CHECK_EQ(r->c.phase, PRINT_DONE);
    CHECK_EQ(r->scans, 0u);
}

TEST_CASE(a_bind_of_the_bound_address_keeps_the_roll)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK(pr_run(r, 10000u));
    REQUIRE(view(r)->w_mm != 0u);
    const int32_t left = view(r)->left;
    REQUIRE(left >= 0);
    /* the same address, the same name, and the scan's name anew */
    print_ctl_bind(&r->c, PR_ADDR, "T0205C2608247368", r->now);
    print_ctl_bind(&r->c, PR_ADDR, "", r->now);
    print_ctl_bind(&r->c, PR_ADDR, "T0205C2608247368", r->now);
    /* the same address in small letters, as a console may type it */
    print_ctl_bind(&r->c, "a4:93:40:30:fa:71 P", "T0205C2608247368",
                   r->now);
    const print_view_t *v = view(r);
    CHECK(v->bound);
    CHECK_STR_EQ(v->addr, PR_ADDR);
    CHECK_EQ(v->w_mm, r->sim.roll.w_mm);
    CHECK_EQ(v->h_mm, r->sim.roll.h_mm);
    CHECK_EQ(v->left, left);
    CHECK(r->connected);
    CHECK_EQ(r->discs, 0u);
}

TEST_CASE(a_bind_of_another_address_clears_the_roll)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK(pr_run(r, 10000u));
    REQUIRE(view(r)->w_mm != 0u);
    print_ctl_bind(&r->c, "A4:93:40:00:00:01 P", "T0205C2600000001",
                   r->now);
    const print_view_t *v = view(r);
    CHECK(v->bound);
    CHECK_STR_EQ(v->addr, "A4:93:40:00:00:01 P");
    CHECK_EQ(v->w_mm, 0u);
    CHECK_EQ(v->h_mm, 0u);
    CHECK_EQ(v->left, -1);
    /* the old printer's link ends */
    run_ms(r, 2u);
    CHECK_EQ(r->discs, 1u);
    CHECK(!r->connected);
}

TEST_CASE(a_random_address_binds_nothing)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(view(r)->bound);
    print_ctl_bind(&r->c, "A4:93:40:30:FA:71 R", "T0205C2608247368", r->now);
    CHECK(!view(r)->bound);
    CHECK_STR_EQ(view(r)->addr, "");
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK_EQ(r->c.phase, PRINT_FAILED);
    CHECK_EQ(r->c.err, PRINT_ERR_NO_PRINTER);
    CHECK_EQ(r->connects, 0u);
}

TEST_CASE(the_frame_task_asks_a_scan_once)
{
    print_ctl_reset();
    print_req_t q;
    CHECK(print_ctl_scan());
    CHECK(!print_ctl_scan());
    CHECK(print_ctl_take(&q));
    CHECK_EQ(q.kind, PRINT_REQ_SCAN);
    print_view_t v;
    memset(&v, 0, sizeof(v));
    v.scanning = true;
    print_ctl_show(&v);
    CHECK(!print_ctl_scan());
    v.scanning = false;
    v.phase = PRINT_SENDING;
    print_ctl_show(&v);
    CHECK(!print_ctl_scan());
    CHECK(!print_ctl_take(&q));
    print_ctl_reset();
}

int main(void)
{
    RUN(a_scan_lists_only_the_ouis_printers_and_connects_nothing);
    RUN(a_scan_is_refused_during_a_job_a_scan_an_arm_and_an_update);
    RUN(a_switch_to_armed_stops_the_scan);
    RUN(a_label_stops_the_scan_and_prints);
    RUN(a_scan_waits_up_to_10s_for_bluetooth);
    RUN(a_lingering_link_ends_before_a_scan);
    RUN(a_scan_during_a_job_leaves_the_link);
    RUN(a_bind_of_the_bound_address_keeps_the_roll);
    RUN(a_bind_of_another_address_clears_the_roll);
    RUN(a_random_address_binds_nothing);
    RUN(the_frame_task_asks_a_scan_once);
    return test_summary("print_ctl_scan");
}
