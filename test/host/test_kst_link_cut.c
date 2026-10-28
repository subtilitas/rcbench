/*
 * An error while the KST programmer holds a port (error_stop.h): the port's
 * supply off outside a write, on the bench of kst_rig.h (the simulated
 * servo, the IOmcu's port and link dispatch, the panel's link module) with
 * the panel's cut as panel_supply.c runs it every pass
 * (port_cut_follow()): the KST outcomes kst_link_outcome_fault() names
 * counted, the cut asked and stepped, its OFF taken by the SUPPLY page.
 *
 *  - kst_link_writing() for a WRITE, a RESTORE and a RELEASE, planning
 *    included: no write frame reaches the servo while it says no write
 *    runs, also when the module's status reads stop behind another page's
 *    exchange;
 *  - an error during each of the three: the write ends verified, then the
 *    supply goes off, in the pass after the module last saw it, within
 *    ERROR_PORT_CUT_MS; the port finds the supply asked off, keeps it off
 *    KST_PORT_RAIL_OFF_MS and returns to PWM, and the cut ends;
 *  - an error with no write: off in the same pass; a REBOOT's power cycle
 *    that switches the supply on again is switched off again;
 *  - a READ ALL the servo does not answer is a session fault and an
 *    error: off at once;
 *  - which outcomes are faults;
 *  - the longest write the code allows (protocols/kst): its planning, at
 *    most 2,161 slices of 892us, the read of 32 registers in 3 passes
 *    before and after, KST_PLAN_MAX_STEPS writes with 2 read-backs each,
 *    at the frame lengths, reply windows and gaps of kst_wire.h and
 *    kst_session.h, and the module's KST_LINK_POLL_MS to see its end,
 *    under ERROR_PORT_CUT_MS / 2.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "greatest.h"

#include "error_stop.h"
#include "kst_rig.h"

/* --------------------------------------------------------- the panel's cut */

static port_cut_t    g_cut;
static error_watch_t g_watch;
static uint32_t      g_faults;      /* kst_publish()'s count             */
static uint32_t      g_outs_seen;
static unsigned      g_offs;        /* switch-offs the cut made          */
static uint32_t      g_cut_ms;      /* the first one's time              */
static uint32_t      g_write_end_ms;  /* the last pass that saw a write  */
static unsigned      g_unseen;      /* write frames no write was seen for */

static bool taken(void)
{
    kst_link_readout_t rd;

    kst_link_readout(&g_rig.link, &rd);
    return kst_prog_port_taken(&rd);
}

/* One pass: the bench's millisecond, then port_cut_follow() as
 * panel_supply.c runs it, the SUPPLY page taking its OFF. */
static void pass(void)
{
    const bool before = kst_link_writing(&g_rig.link);
    const unsigned n = g_rig.sim.n_writes;
    bool writing;
    error_stop_t e;

    rig_frame();
    writing = kst_link_writing(&g_rig.link);
    if (g_rig.sim.n_writes != n && !before && !writing) {
        g_unseen++;
    }
    if (g_rig.outcomes != g_outs_seen) {
        g_outs_seen = g_rig.outcomes;
        if (kst_link_outcome_fault(&g_rig.outcome)) {
            g_faults++;
        }
    }
    if (writing) {
        g_write_end_ms = g_rig.ms;
    }
    e = error_stop_take(&g_watch, g_faults, false, g_rig.sup_on, taken());
    if (e.supply_off) {
        g_rig.sup_on = false;
    }
    if (e.port_cut) {
        port_cut_ask(&g_cut, g_rig.ms);
    }
    if (port_cut_step(&g_cut, taken(), writing, g_rig.sup_on, g_rig.ms)) {
        g_rig.sup_on = false;
        if (g_offs++ == 0u) {
            g_cut_ms = g_rig.ms;
        }
    }
}

static void passes(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        pass();
    }
}

static kst_link_request_t req(link_kst_op_t op)
{
    kst_link_request_t r;

    memset(&r, 0, sizeof(r));
    r.op = op;
    r.port = RIG_PORT;
    r.supply_mv = 5000u;
    r.supply_ma = 1000u;
    r.write_enabled = true;
    r.confirm = op == LINK_KST_OP_RESTORE;
    return r;
}

/* Until the operation's outcome; 60s is the bound. */
static kst_link_outcome_t ended(void)
{
    const uint32_t outs = g_rig.outcomes;

    for (unsigned i = 0; i < 60000u && g_rig.outcomes == outs; ++i) {
        pass();
    }
    CHECK(g_rig.outcomes != outs);
    return g_rig.outcome;
}

static kst_link_outcome_t run(const kst_link_request_t *r)
{
    CHECK_EQ(kst_link_start(&g_rig.link, r), KST_LINK_START_OK);
    return ended();
}

/* A PD mini that powers the servo; the port entered and the servo read. */
static void up(void)
{
    kst_link_request_t r;

    rig_fresh();
    g_rig.rail = true;
    g_rig.on_rail = true;
    memset(&g_cut, 0, sizeof(g_cut));
    error_watch_init(&g_watch, 0u);
    g_faults = 0;
    g_outs_seen = 0;
    g_offs = 0;
    g_cut_ms = 0;
    g_write_end_ms = 0;
    g_unseen = 0;
    CHECK(rig_link_up(LINK_PROTOCOL_MINOR));
    r = req(LINK_KST_OP_ENTER);
    CHECK_EQ(run(&r).result, KST_SES_OK);
    r = req(LINK_KST_OP_READ_ALL);
    CHECK_EQ(run(&r).result, KST_SES_OK);
    CHECK(g_rig.sup_on);
    CHECK(taken());
}

/* A write of 3 edits to the image the module holds. */
static kst_link_request_t edits(void)
{
    kst_link_request_t r = req(LINK_KST_OP_WRITE);

    CHECK(kst_link_image(&g_rig.link, &r.image));
    CHECK(kst_field_edit(&r.image, KST_F_DEAD_BAND, 50));
    CHECK(kst_field_edit(&r.image, KST_F_BOOST,
                         (uint16_t)(kst_field_get(&r.image, KST_F_BOOST)
                                    - 1u)));
    CHECK(kst_field_edit(&r.image, KST_F_SOFT_START,
                         kst_field_get(&r.image, KST_F_SOFT_START) ^ 1u));
    return r;
}

/* The request of @p kind (0 WRITE, 1 RESTORE, 2 RELEASE) on a bench that
 * is up; a RESTORE after a write, from the backup. */
static kst_link_request_t of_kind(unsigned kind)
{
    kst_link_request_t r;

    if (kind == 0u) {
        return edits();
    }
    if (kind == 1u) {
        r = edits();
        CHECK_EQ(run(&r).result, KST_SES_OK);
        r = req(LINK_KST_OP_RESTORE);
        CHECK(kst_link_backup(&g_rig.link, &r.image));
        return r;
    }
    return req(LINK_KST_OP_RELEASE);
}

/* Images whose write plans long enough for a status read to see it plan
 * (test_kst_link: a_write_shows_as_planning_until_its_plan_is_there). */
static const char k_long_a[] =
    "00f5f514487704eff4b20fc710091400969600010b000000000000e301100000";
static const char k_long_b[] =
    "00f5f51448770465cb840d8e66041400969600010b000000000000e301100000";

static kst_image_t image_of(const char *hex)
{
    kst_image_t img;

    for (unsigned i = 0; i < KST_REG_COUNT; ++i) {
        unsigned v = 0;

        (void)sscanf(hex + 2u * i, "%2x", &v);
        img.r[i] = (uint8_t)v;
    }
    return img;
}

/* ----------------------------------------------------------------- cases */

TEST_CASE(a_write_frame_goes_out_only_while_a_write_is_seen)
{
    for (unsigned kind = 0; kind < 3u; ++kind) {
        kst_link_request_t r;
        kst_link_outcome_t o;
        unsigned writes;

        up();
        r = of_kind(kind);
        writes = g_rig.sim.n_writes;
        CHECK(!kst_link_writing(&g_rig.link));
        CHECK_EQ(kst_link_start(&g_rig.link, &r), KST_LINK_START_OK);
        CHECK(kst_link_writing(&g_rig.link));
        o = ended();
        CHECK_EQ(o.kind, KST_LINK_OUT_DONE);
        CHECK_EQ(o.result, KST_SES_OK);
        CHECK(g_rig.sim.n_writes > writes);
        CHECK(!kst_link_writing(&g_rig.link));
        CHECK_EQ(g_unseen, 0u);
        CHECK_EQ(g_offs, 0u);
    }
    /* Not a write: a READ ALL, a VERIFY, a REBOOT. */
    up();
    {
        kst_link_request_t r = req(LINK_KST_OP_READ_ALL);

        CHECK_EQ(kst_link_start(&g_rig.link, &r), KST_LINK_START_OK);
        CHECK(!kst_link_writing(&g_rig.link));
        (void)ended();
        r = req(LINK_KST_OP_REBOOT);
        CHECK_EQ(kst_link_start(&g_rig.link, &r), KST_LINK_START_OK);
        CHECK(!kst_link_writing(&g_rig.link));
        (void)ended();
    }
    CHECK(!kst_link_writing(NULL));
}

TEST_CASE(an_error_during_each_write_kind_cuts_after_it_ends)
{
    for (unsigned kind = 0; kind < 3u; ++kind) {
        kst_link_request_t r;
        kst_link_outcome_t o;
        uint32_t asked;
        unsigned writes;

        up();
        r = of_kind(kind);
        writes = g_rig.sim.n_writes;
        CHECK_EQ(kst_link_start(&g_rig.link, &r), KST_LINK_START_OK);
        for (unsigned i = 0; i < 60000u && g_rig.sim.n_writes == writes;
             ++i) {
            pass();
        }
        CHECK(g_rig.sim.n_writes > writes);
        asked = g_rig.ms;
        port_cut_ask(&g_cut, asked);
        o = ended();
        CHECK_EQ(o.result, KST_SES_OK);
        passes(5u);
        CHECK_EQ(g_offs, 1u);
        CHECK(!g_rig.sup_on);
        CHECK(g_cut_ms > g_write_end_ms);
        CHECK_EQ(g_cut_ms - g_write_end_ms, 1u);
        CHECK(g_cut_ms - asked < ERROR_PORT_CUT_MS);
        /* The port: the supply asked off, KST_PORT_RAIL_OFF_MS, PWM. */
        passes(KST_PORT_RAIL_OFF_MS + 200u);
        CHECK(!taken());
        CHECK(!g_cut.armed);
        CHECK(!g_rig.sim.powered);
        CHECK_EQ(g_offs, 1u);
        CHECK_EQ(g_unseen, 0u);
    }
}

TEST_CASE(a_write_that_plans_is_waited_for)
{
    kst_link_t k;

    /* The module as a WRITE leaves it while the page last read says it
     * plans: the plan may have ended and its frames gone out since.  A
     * short plan ends before a status read sees it, so the state is set
     * here. */
    kst_link_init(&k);
    k.active = true;
    k.op = (uint8_t)LINK_KST_OP_WRITE;
    k.phase = (uint8_t)KST_LINK_RUNNING;
    k.status[LINK_KS_STATE] = (uint16_t)(LINK_KST_PROGRAMMING | LINK_KS_F_BUSY);
    k.status[LINK_KS_RESULT] = LINK_KST_RESULT_PLANNING;
    CHECK_EQ(kst_link_phase(&k), KST_LINK_PLANNING);
    CHECK(kst_link_writing(&k));
    k.status[LINK_KS_RESULT] = 1u;
    CHECK(kst_link_writing(&k));
    /* Sent, not yet read back as started: waited for. */
    k.phase = (uint8_t)KST_LINK_SENDING;
    CHECK(kst_link_writing(&k));
    /* Ended at the page: the results are read, no write runs. */
    k.phase = (uint8_t)KST_LINK_FETCHING;
    CHECK(!kst_link_writing(&k));
    /* A RESTORE and a RELEASE the same; a VERIFY and a REBOOT never. */
    k.phase = (uint8_t)KST_LINK_RUNNING;
    k.op = (uint8_t)LINK_KST_OP_RESTORE;
    CHECK(kst_link_writing(&k));
    k.op = (uint8_t)LINK_KST_OP_RELEASE;
    CHECK(kst_link_writing(&k));
    k.op = (uint8_t)LINK_KST_OP_VERIFY;
    CHECK(!kst_link_writing(&k));
    k.op = (uint8_t)LINK_KST_OP_REBOOT;
    CHECK(!kst_link_writing(&k));
    k.op = (uint8_t)LINK_KST_OP_WRITE;
    k.active = false;
    CHECK(!kst_link_writing(&k));
}

TEST_CASE(a_write_unseen_behind_another_pages_exchange_is_waited_for)
{
    kst_link_request_t r;
    kst_link_outcome_t o;
    unsigned writes;

    /* The page read as planning, then no KST exchange for as long as the
     * first write frame takes to go out: another page's exchange waits for
     * its 1000ms timeout and the pump runs on.  The module still shows
     * PLANNING. */
    up();
    {
        const kst_image_t a = image_of(k_long_a);

        memcpy(g_rig.sim.reg, a.r, sizeof(g_rig.sim.reg));
    }
    r = req(LINK_KST_OP_READ_ALL);
    CHECK_EQ(run(&r).result, KST_SES_OK);
    r = req(LINK_KST_OP_WRITE);
    r.image = image_of(k_long_b);
    r.unlock = ~(kst_field_set_t)0;
    writes = g_rig.sim.n_writes;
    CHECK_EQ(kst_link_start(&g_rig.link, &r), KST_LINK_START_OK);
    for (unsigned i = 0; i < 5000u
                         && kst_link_phase(&g_rig.link) != KST_LINK_PLANNING;
         ++i) {
        pass();
    }
    CHECK_EQ(kst_link_phase(&g_rig.link), KST_LINK_PLANNING);
    g_rig.held = true;
    for (unsigned i = 0; i < 5000u && g_rig.sim.n_writes == writes; ++i) {
        pass();
    }
    CHECK(g_rig.sim.n_writes > writes);
    CHECK_EQ(kst_link_phase(&g_rig.link), KST_LINK_PLANNING);
    CHECK(kst_link_writing(&g_rig.link));
    port_cut_ask(&g_cut, g_rig.ms);
    passes(5u);
    CHECK_EQ(g_offs, 0u);
    CHECK(g_rig.sup_on);
    g_rig.held = false;
    o = ended();
    CHECK_EQ(o.result, KST_SES_OK);
    passes(5u);
    CHECK_EQ(g_offs, 1u);
    CHECK(!g_rig.sup_on);
}

TEST_CASE(an_error_with_no_write_cuts_at_once_and_again_after_a_cycle)
{
    kst_link_request_t r;

    up();
    port_cut_ask(&g_cut, g_rig.ms);
    pass();
    CHECK_EQ(g_offs, 1u);
    CHECK(!g_rig.sup_on);
    passes(KST_PORT_RAIL_OFF_MS + 200u);
    CHECK(!taken());
    CHECK(!g_cut.armed);
    /* A REBOOT from PWM under an error: the cycle's ON is switched off
     * again and the port ends in PWM with the supply off. */
    g_rig.sup_on = true;
    r = req(LINK_KST_OP_REBOOT);
    CHECK_EQ(kst_link_start(&g_rig.link, &r), KST_LINK_START_OK);
    for (unsigned i = 0; i < 60000u
                         && LINK_KS_STATE_OF(g_rig.link.status[LINK_KS_STATE])
                                != (uint8_t)LINK_KST_RAIL_OFF;
         ++i) {
        pass();
    }
    port_cut_ask(&g_cut, g_rig.ms);
    (void)ended();
    passes(KST_PORT_RAIL_OFF_MS + 200u);
    CHECK(g_offs >= 2u);
    CHECK(!g_rig.sup_on);
    CHECK(!taken());
    CHECK(!g_cut.armed);
}

TEST_CASE(a_read_the_servo_does_not_answer_is_an_error)
{
    kst_link_request_t r;
    kst_link_outcome_t o;

    up();
    g_rig.on_rail = false;
    sim_power(&g_rig.sim, false);
    r = req(LINK_KST_OP_READ_ALL);
    o = run(&r);
    CHECK(kst_link_outcome_fault(&o));
    CHECK_EQ(g_faults, 1u);
    passes(2u);
    CHECK_EQ(g_offs, 1u);
    CHECK(!g_rig.sup_on);
}

TEST_CASE(which_outcomes_are_faults)
{
    static const unsigned k_fault[] = {
        KST_SES_ERR_NO_SERVO, KST_SES_ERR_READ, KST_SES_ERR_CHANGED,
        KST_SES_ERR_WRITE_FAILED, KST_SES_ERR_ROLLED_BACK, KST_SES_ERR_TORN,
        KST_SES_ERR_UNINTENDED, KST_SES_ERR_VERIFY, KST_SES_ERR_DRIVER,
        LINK_KST_RESULT_SUPPLY,
    };
    kst_link_outcome_t o;
    unsigned faults = 0;

    memset(&o, 0, sizeof(o));
    for (unsigned r = 0; r < 256u; ++r) {
        bool want = false;

        for (unsigned i = 0; i < sizeof(k_fault) / sizeof(k_fault[0]); ++i) {
            want = want || k_fault[i] == r;
        }
        o.kind = KST_LINK_OUT_DONE;
        o.result = (uint8_t)r;
        CHECK_EQ(kst_link_outcome_fault(&o), want);
        faults += want ? 1u : 0u;
        /* Another kind with the same byte is none. */
        o.kind = KST_LINK_OUT_REFUSED;
        CHECK(!kst_link_outcome_fault(&o));
    }
    CHECK_EQ(faults, 10u);
    CHECK(!kst_link_outcome_fault(NULL));
}

TEST_CASE(the_longest_write_on_the_wire_ends_within_half_the_bound)
{
    kst_frame_t f;
    uint32_t read_ns = 0, write_ns = 0, us;

    for (uint8_t reg = 1; reg <= KST_REG_MAX; ++reg) {
        CHECK(kst_frame_read(reg, &f));
        if (kst_frame_duration_ns(&f) > read_ns) {
            read_ns = kst_frame_duration_ns(&f);
        }
        CHECK(kst_frame_write(reg, 0xFFu, &f));
        if (kst_frame_duration_ns(&f) > write_ns) {
            write_ns = kst_frame_duration_ns(&f);
        }
    }
    /* Frame, release, reply window and the gap to the next frame. */
    read_ns += KST_RELEASE_NS + kst_reply_window_ns(KST_EXPECT_READ)
               + KST_GAP_US * 1000u;
    write_ns += KST_RELEASE_NS + kst_reply_window_ns(KST_EXPECT_WRITE)
                + KST_GAP_US * 1000u;
    us = (2u * KST_READ_PASSES * KST_REG_COUNT + 2u * KST_PLAN_MAX_STEPS)
             * (read_ns / 1000u)
         + KST_PLAN_MAX_STEPS * (write_ns / 1000u);
    /* And the planning before them: KST_PLAN_WORK_CAP in slices of at
     * least KST_PLAN_SLICE_UNITS, one a pass of the IOmcu's loop, each at
     * most 892us at 150MHz (docs/Performance.md, computed, not measured;
     * the rest of a pass is not measured). */
    us += (KST_PLAN_WORK_CAP / KST_PLAN_SLICE_UNITS + 1u) * 892u;
    CHECK(us / 1000u + KST_LINK_POLL_MS < ERROR_PORT_CUT_MS / 2u);
}

int main(void)
{
    RUN(a_write_frame_goes_out_only_while_a_write_is_seen);
    RUN(an_error_during_each_write_kind_cuts_after_it_ends);
    RUN(a_write_that_plans_is_waited_for);
    RUN(a_write_unseen_behind_another_pages_exchange_is_waited_for);
    RUN(an_error_with_no_write_cuts_at_once_and_again_after_a_cycle);
    RUN(a_read_the_servo_does_not_answer_is_an_error);
    RUN(which_outcomes_are_faults);
    RUN(the_longest_write_on_the_wire_ends_within_half_the_bound);
    return test_summary("kst_link_cut");
}
