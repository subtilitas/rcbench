/**
 * @file test_print_ctl_scan.c
 * @brief The label printer's scan (print_ctl.h, PRINT_REQ_SCAN) on the
 *        scripted stack of print_rig.h: the list holds only public
 *        addresses A4:93:40:xx:xx:xx, with their names and RSSI, and no
 *        connect follows the scan; the refusals while a job or a scan
 *        runs, while ARMED and while a device update runs, and the busy
 *        flag the update reads; a switch to ARMED stops the scan; a label
 *        stops it and prints; the wait for Bluetooth and its end at 10s; a
 *        lingering link stays through a scan; the frame task's ask.
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

TEST_CASE(a_lingering_link_stays_through_a_scan)
{
    pr_rig_t *r = &s_r;
    pr_init(r);
    CHECK(pr_ask(r, PRINT_REQ_QUERY, NULL, NULL));
    CHECK(pr_run(r, 10000u));
    REQUIRE(r->connected);
    CHECK(pr_ask(r, PRINT_REQ_SCAN, NULL, NULL));
    run_ms(r, 10100u);
    CHECK(view(r)->scan_done);
    CHECK(r->connected);
    CHECK_EQ(r->discs, 0u);
    /* the linger still ends it, 30s after the last answer */
    run_ms(r, 20000u);
    CHECK(!r->connected);
    CHECK_EQ(r->discs, 1u);
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
    RUN(a_lingering_link_stays_through_a_scan);
    RUN(the_frame_task_asks_a_scan_once);
    return test_summary("print_ctl_scan");
}
