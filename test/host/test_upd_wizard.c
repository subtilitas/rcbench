/*
 * SETUP > UPDATES' step model (upd_wizard.c), the order of the steps: the
 * current step as the lowest that blocks and a running update as current
 * whatever its number; a newer panel release making PANEL current again
 * from step 4 and reading LATER on the linked steps; PENDING blocking;
 * ABSENT waiting only in a run; SKIP passing a step until the mask
 * clears; a failed attempt keeping its step current with its result; the
 * warning for an older carried image; NULL and zeroed inputs.  Each view's
 * mapping to a step state is in test_upd_wizard_map.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include "upd_wiz_fx.h"

TEST_CASE(nothing_checked_and_all_current_has_no_current_step)
{
    fx_t f;
    all_current(&f);
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_NONE);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_DONE, UPD_WIZ_TAG_NOT_CHECKED);
    for (unsigned i = UPD_WIZ_IOMCU; i < UPD_WIZ_STEPS; ++i) {
        CHECK_STEP(w, i, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);
        CHECK(w.step[i].result == NULL);
        CHECK(!w.step[i].older);
    }
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(!w.act_live);
    CHECK(!w.can_skip);
    CHECK(!w.cancel);
    CHECK(!w.show_skipped);
    CHECK_STR_EQ(w.step[UPD_WIZ_PANEL].ver, "0.23.15");
    CHECK_STR_EQ(w.step[UPD_WIZ_PANEL].next, "");
    CHECK_STR_EQ(w.step[UPD_WIZ_IOMCU].ver, "0.23.15");
    CHECK_STR_EQ(w.step[UPD_WIZ_C6].ver, "2.13.0");
    CHECK_STR_EQ(w.step[UPD_WIZ_C6].next, "2.13.0");
    CHECK_STR_EQ(w.step[UPD_WIZ_DIAL].ver, "0.23.15");

    /* A check that found nothing newer reads CURRENT; one that failed reads
     * NOT CHECKED; neither blocks, in a run neither. */
    static const struct {
        upd_got_t     got;
        upd_wiz_tag_t tag;
    } k[] = {
        { UPD_GOT_CURRENT, UPD_WIZ_TAG_CURRENT },
        { UPD_GOT_PAST_LINE, UPD_WIZ_TAG_CURRENT },
        { UPD_GOT_UNVERSIONED, UPD_WIZ_TAG_CURRENT },
        { UPD_GOT_TIMEOUT, UPD_WIZ_TAG_NOT_CHECKED },
        { UPD_GOT_JOIN, UPD_WIZ_TAG_NOT_CHECKED },
        { UPD_GOT_ABORTED, UPD_WIZ_TAG_NOT_CHECKED },
        { UPD_GOT_NO_NET, UPD_WIZ_TAG_NOT_CHECKED },
        { UPD_GOT_BLOCKED, UPD_WIZ_TAG_NOT_CHECKED },
    };
    for (unsigned i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        for (unsigned run = 0u; run < 2u; ++run) {
            all_current(&f);
            panel_ended(&f, k[i].got, "0.23.15");
            f.in.run = run != 0u;
            w = eval(&f);
            CHECK_EQ(w.current, UPD_WIZ_NONE);
            check_want(&w.step[UPD_WIZ_PANEL],
                       (want_t){ UPD_WIZ_DONE, k[i].tag, UPD_WIZ_WHY_NONE },
                       "got", i, run);
            CHECK_STR_EQ(w.step[UPD_WIZ_PANEL].next, "");
        }
    }
}

TEST_CASE(a_newer_release_makes_the_panel_current_and_every_linked_step_later)
{
    fx_t f;
    all_current(&f);
    (void)snprintf(f.p.running, sizeof(f.p.running), "0.23.14");
    panel_ended(&f, UPD_GOT_NEWER, "0.23.15");
    f.cu.running = K_CU_OLD;
    f.cu.carried = K_CU_OLD;
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_REINSTALL, true, true, K_NEW);
    f.d[UPD_DEV_PNODE] = dev_of(DEVUPD_ROW_NOT_LINKED, true, false, K_OLD);
    f.d[UPD_DEV_BAL] = dev_of(DEVUPD_ROW_NOT_LINKED, false, false, K_OLD);
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PANEL);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT);
    CHECK_STR_EQ(w.step[UPD_WIZ_PANEL].ver, "0.23.14");
    CHECK_STR_EQ(w.step[UPD_WIZ_PANEL].next, "0.23.15");
    CHECK_EQ(w.act, UPD_WIZ_ACT_DOWNLOAD);
    CHECK(w.act_live);
    CHECK(w.can_skip);
    CHECK(!w.cancel);
    /* The release carries newer IOmcu and device images; the C6's image
     * follows esp_hosted, so its row stays CURRENT. */
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_DONE, UPD_WIZ_TAG_LATER);
    CHECK_STEP(w, UPD_WIZ_C6, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_DONE, UPD_WIZ_TAG_LATER);
    CHECK_STEP(w, UPD_WIZ_PNODE, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_CONNECTED);
    CHECK_STEP(w, UPD_WIZ_BAL, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_BOUND);
    CHECK_STR_EQ(w.step[UPD_WIZ_PNODE].ver, "-");
    CHECK_STR_EQ(w.step[UPD_WIZ_BAL].ver, "-");

    /* An OPEN IOmcu reads LATER as well. */
    f.cu.sec = CU_SEC_READY;
    f.cu.carried = K_CU_NEW;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PANEL);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_OPEN, UPD_WIZ_TAG_LATER);

    /* While the check runs nothing newer is known: no LATER on DONE. */
    f.cu.sec = CU_SEC_SAME;
    f.p.phase = UPD_PH_ASKING;
    f.p.got = UPD_GOT_NONE;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PANEL);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_RUNNING, UPD_WIZ_TAG_CHECKING);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(!w.can_skip);

    /* The download runs, then the install: still the release's steps. */
    f.p.job = UPD_JOB_DOWNLOAD;
    f.p.phase = UPD_PH_IMAGE;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PANEL);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_DONE, UPD_WIZ_TAG_LATER);
    CHECK_STR_EQ(w.step[UPD_WIZ_PANEL].next, "0.23.15");
    f.p.phase = UPD_PH_ENDED;
    f.p.got = UPD_GOT_STAGED;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PANEL);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(!w.cancel);

    /* A failed download passes: its hold needs a new check. */
    f.p.got = UPD_GOT_TIMEOUT;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_NONE);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_DONE, UPD_WIZ_TAG_FAILED);
    CHECK_STR_EQ(w.step[UPD_WIZ_PANEL].next, "0.23.15");
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);

    /* Wi-Fi that could not be set up offers no download. */
    panel_ended(&f, UPD_GOT_NEWER, "0.23.15");
    f.p.unavailable = true;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_NONE);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_DONE, UPD_WIZ_TAG_NOT_CHECKED);
}

TEST_CASE(a_newer_release_found_at_step_4_moves_back_to_step_1)
{
    fx_t f;
    all_current(&f);
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);
    CHECK_EQ(w.act, UPD_WIZ_ACT_DIAL);
    CHECK(w.act_live);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT);

    panel_ended(&f, UPD_GOT_NEWER, "0.23.16");
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PANEL);
    CHECK_EQ(w.act, UPD_WIZ_ACT_DOWNLOAD);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_OPEN, UPD_WIZ_TAG_LATER);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_DONE, UPD_WIZ_TAG_LATER);

    /* Held by ARMED, still step 1: the hold is grey there. */
    f.p.dl_block = UPD_WHY_ARMED;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PANEL);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT);
    CHECK_EQ(w.act, UPD_WIZ_ACT_DOWNLOAD);
    CHECK(!w.act_live);

    /* Skipped: back to the Dial. */
    f.in.skip = UPD_WIZ_BIT(UPD_WIZ_PANEL);
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);
}

TEST_CASE(the_current_step_is_the_lowest_that_blocks)
{
    fx_t f;
    all_current(&f);
    f.c6.row = FW_C6_ROW_READY;
    f.c6.carried = K_C6_NEW;
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    f.d[UPD_DEV_BAL] = dev_of(DEVUPD_ROW_ARMED, true, true, K_OLD);
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_C6);
    CHECK_EQ(w.act, UPD_WIZ_ACT_C6);
    CHECK(w.act_live);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_OPEN, UPD_WIZ_TAG_LATER);
    CHECK_STEP(w, UPD_WIZ_BAL, UPD_WIZ_HELD, UPD_WIZ_TAG_LATER);

    f.c6.carried = K_C6;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);
    CHECK_EQ(w.act, UPD_WIZ_ACT_DIAL);
    CHECK_STEP(w, UPD_WIZ_C6, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);

    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_REINSTALL, true, true, K_NEW);
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_BAL);
    CHECK_EQ(w.act, UPD_WIZ_ACT_BAL);
    CHECK(!w.act_live);
    CHECK_STEP(w, UPD_WIZ_BAL, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT);

    /* An ABSENT step before it passes outside a run. */
    f.cu.sec = CU_SEC_NO_LINK;
    f.d[UPD_DEV_PNODE] = dev_of(DEVUPD_ROW_NOT_LINKED, true, false, K_OLD);
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_BAL);

    /* Each step alone is current when it is the only one that blocks. */
    for (unsigned s = 0u; s < UPD_WIZ_STEPS; ++s) {
        all_open(&f);
        f.in.skip = UPD_WIZ_ALL & ~UPD_WIZ_BIT(s);
        w = eval(&f);
        CHECK_EQ(w.current, s);
        CHECK_EQ(w.act, UPD_WIZ_ACT_DOWNLOAD + s);
        CHECK(w.act_live);
        CHECK(w.can_skip);
    }
}

TEST_CASE(pending_blocks_later_steps)
{
    fx_t f;
    all_current(&f);
    f.cu.sec = CU_SEC_CHECKING;
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_IOMCU);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(!w.act_live);
    CHECK(!w.can_skip);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_OPEN, UPD_WIZ_TAG_LATER);

    /* The check ends SAME: the Dial is next. */
    f.cu.sec = CU_SEC_SAME;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);

    /* Bluetooth LE starting holds the C6 step PENDING, and the Dial with
     * it; a device's image check alike. */
    f.c6.row = FW_C6_ROW_BLOCKED;
    f.c6.why = FW_C6_WHY_BLE;
    f.c6.carried = K_C6_NEW;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_C6);
    CHECK_EQ(w.step[UPD_WIZ_C6].why, UPD_WIZ_WHY_BLE);
    CHECK(!w.can_skip);
    f.c6 = (upd_c6_view_t){ .row = FW_C6_ROW_REINSTALL, .known = true,
                            .running = K_C6, .have_image = true,
                            .carried = K_C6 };
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_CHECKING, true, true, K_OLD);
    f.d[UPD_DEV_PNODE] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);
    CHECK_STEP(w, UPD_WIZ_PNODE, UPD_WIZ_OPEN, UPD_WIZ_TAG_LATER);
}

TEST_CASE(absent_blocks_only_in_a_run)
{
    fx_t f;
    all_current(&f);
    f.cu.sec = CU_SEC_NO_LINK;
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    f.d[UPD_DEV_PNODE] = dev_of(DEVUPD_ROW_NOT_LINKED, true, false, K_OLD);
    f.d[UPD_DEV_BAL] = dev_of(DEVUPD_ROW_NOT_LINKED, false, false, K_OLD);
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_LINK);
    CHECK_STEP(w, UPD_WIZ_PNODE, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_CONNECTED);

    f.in.run = true;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_IOMCU);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_WAITING, UPD_WIZ_TAG_NO_LINK);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(w.can_skip);
    CHECK_STEP(w, UPD_WIZ_PNODE, UPD_WIZ_WAITING, UPD_WIZ_TAG_NOT_CONNECTED);
    CHECK_STEP(w, UPD_WIZ_BAL, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_BOUND);

    /* The link comes up and the check says SAME: the Dial, then the power
     * node waits; skipped, the balance node that is not bound never does. */
    f.cu.sec = CU_SEC_SAME;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_REINSTALL, true, true, K_NEW);
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PNODE);
    CHECK(w.can_skip);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    f.in.skip = UPD_WIZ_BIT(UPD_WIZ_PNODE);
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_NONE);
    CHECK(w.show_skipped);

    /* Targets that never come up by themselves pass in a run too. */
    f.in.skip = 0u;
    f.d[UPD_DEV_PNODE] = dev_of(DEVUPD_ROW_NO_IMAGE, true, false, K_OLD);
    static const cu_sec_t k_cu[] = { CU_SEC_NO_IMAGE, CU_SEC_FAILED,
                                     CU_SEC_USB, CU_SEC_NO_BOARD };
    for (unsigned i = 0u; i < sizeof(k_cu) / sizeof(k_cu[0]); ++i) {
        f.cu.sec = k_cu[i];
        w = eval(&f);
        CHECK_EQ(w.step[UPD_WIZ_IOMCU].state, UPD_WIZ_ABSENT);
        CHECK_EQ(w.current, UPD_WIZ_NONE);
    }
    f.cu.sec = CU_SEC_SAME;
    static const fw_c6_row_t k_c6_rows[] = {
        FW_C6_ROW_NO_IMAGE, FW_C6_ROW_BAD_IMAGE, FW_C6_ROW_LINK_FAILED };
    for (unsigned i = 0u; i < sizeof(k_c6_rows) / sizeof(k_c6_rows[0]); ++i) {
        f.c6.row = k_c6_rows[i];
        w = eval(&f);
        CHECK_EQ(w.step[UPD_WIZ_C6].state, UPD_WIZ_ABSENT);
        CHECK_EQ(w.current, UPD_WIZ_NONE);
    }
    f.c6.row = FW_C6_ROW_READY;
    f.c6.known = false;
    w = eval(&f);
    CHECK_EQ(w.step[UPD_WIZ_C6].state, UPD_WIZ_ABSENT);
    CHECK_EQ(w.current, UPD_WIZ_NONE);
}

TEST_CASE(a_skipped_step_does_not_block_and_keeps_its_tag)
{
    fx_t f;
    all_current(&f);
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    f.d[UPD_DEV_PNODE] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    f.in.skip = UPD_WIZ_BIT(UPD_WIZ_DIAL);
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PNODE);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_SKIPPED, UPD_WIZ_TAG_SKIPPED);
    CHECK_STR_EQ(w.step[UPD_WIZ_DIAL].ver, "0.23.14");

    /* The Dial's facts change; the step stays passed. */
    static const struct {
        devupd_row_t row;
        bool         bound, known;
    } k[] = {
        { DEVUPD_ROW_ARMED, true, true },
        { DEVUPD_ROW_BUSY, true, true },
        { DEVUPD_ROW_CHECKING, true, true },
        { DEVUPD_ROW_NOT_LINKED, true, false },
    };
    f.in.run = true;
    for (unsigned i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        f.d[UPD_DEV_DIAL] = dev_of(k[i].row, k[i].bound, k[i].known, K_OLD);
        w = eval(&f);
        CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_SKIPPED, UPD_WIZ_TAG_SKIPPED);
        CHECK_EQ(w.step[UPD_WIZ_DIAL].why, UPD_WIZ_WHY_NONE);
        CHECK_EQ(w.current, UPD_WIZ_PNODE);
    }
    /* A failed attempt, skipped, passes as well. */
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    f.in.result[UPD_WIZ_DIAL] = "The Dial refused the update.";
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_SKIPPED, UPD_WIZ_TAG_SKIPPED);
    CHECK_EQ(w.current, UPD_WIZ_PNODE);

    /* An update started elsewhere is never passed. */
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_UPDATING, true, true, K_OLD);
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);

    /* Once on the carried version the row says so; ABSENT stays ABSENT. */
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_REINSTALL, true, true, K_NEW);
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);
    f.in.run = false;
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_NOT_LINKED, false, false, K_OLD);
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_BOUND);
    CHECK_EQ(w.current, UPD_WIZ_PNODE);
}

TEST_CASE(skip_all_reaches_the_end_and_show_skipped_returns_to_the_first)
{
    fx_t f;
    all_open(&f);
    for (unsigned s = 0u; s < UPD_WIZ_STEPS; ++s) {
        const upd_wiz_t w = eval(&f);
        CHECK_EQ(w.current, s);
        CHECK(w.can_skip);
        CHECK(!w.show_skipped);
        for (unsigned i = 0u; i < s; ++i) {
            CHECK_STEP(w, i, UPD_WIZ_SKIPPED, UPD_WIZ_TAG_SKIPPED);
        }
        for (unsigned i = s + 1u; i < UPD_WIZ_STEPS; ++i) {
            CHECK_EQ(w.step[i].tag, UPD_WIZ_TAG_LATER);
        }
        f.in.skip |= UPD_WIZ_BIT(w.current);
    }
    CHECK_EQ(f.in.skip, UPD_WIZ_ALL);
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_NONE);
    CHECK(w.show_skipped);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(!w.can_skip);

    /* SHOW SKIPPED STEPS clears the mask. */
    f.in.skip = 0u;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PANEL);
    CHECK(!w.show_skipped);

    /* A bit set on a step that passes anyway shows nothing. */
    all_current(&f);
    f.in.skip = UPD_WIZ_ALL;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_NONE);
    CHECK(!w.show_skipped);
    /* Bits above the six steps are ignored. */
    f.in.skip = ~UPD_WIZ_ALL;
    f.d[UPD_DEV_BAL] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_BAL);
}

TEST_CASE(a_failed_step_stays_current_with_its_result)
{
    static const char k_why[] = "The Dial refused the update.";
    fx_t f;
    all_current(&f);
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    f.in.result[UPD_WIZ_DIAL] = k_why;
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_FAILED, UPD_WIZ_TAG_FAILED);
    CHECK(w.step[UPD_WIZ_DIAL].result == k_why);
    CHECK_EQ(w.act, UPD_WIZ_ACT_DIAL);
    CHECK(w.act_live);
    CHECK(w.can_skip);

    /* An empty text is no failure. */
    f.in.result[UPD_WIZ_DIAL] = "";
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT);
    CHECK(w.step[UPD_WIZ_DIAL].result == NULL);

    /* Held, the reason is kept for the card and the step stays HELD. */
    f.in.result[UPD_WIZ_DIAL] = k_why;
    f.d[UPD_DEV_DIAL].row = DEVUPD_ROW_ARMED;
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT);
    CHECK(w.step[UPD_WIZ_DIAL].result == k_why);
    CHECK_EQ(w.current, UPD_WIZ_DIAL);

    /* The retry runs, then installs: DONE wins over the old result. */
    f.d[UPD_DEV_DIAL].row = DEVUPD_ROW_UPDATING;
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING);
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_REINSTALL, true, true, K_NEW);
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);
    CHECK_EQ(w.current, UPD_WIZ_NONE);

    /* The IOmcu and the C6 alike; a failure after the current step reads
     * LATER. */
    all_current(&f);
    f.cu.sec = CU_SEC_READY;
    f.cu.running = K_CU_OLD;
    f.c6.row = FW_C6_ROW_READY;
    f.c6.carried = K_C6_NEW;
    f.in.result[UPD_WIZ_IOMCU] = "IOmcu update failed: no answer.";
    f.in.result[UPD_WIZ_C6] = "C6 update failed: write.";
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_IOMCU);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_FAILED, UPD_WIZ_TAG_FAILED);
    CHECK_STEP(w, UPD_WIZ_C6, UPD_WIZ_FAILED, UPD_WIZ_TAG_LATER);
    CHECK_EQ(w.act, UPD_WIZ_ACT_IOMCU);
    CHECK(w.act_live);

    /* A result on PANEL is handed through and changes nothing: its own
     * view says how the download ended. */
    all_current(&f);
    f.in.result[UPD_WIZ_PANEL] = "Download failed.";
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_DONE, UPD_WIZ_TAG_NOT_CHECKED);
    CHECK(w.step[UPD_WIZ_PANEL].result != NULL);
    CHECK_EQ(w.current, UPD_WIZ_NONE);
}

TEST_CASE(an_older_carried_iomcu_image_is_open_with_the_warning)
{
    fx_t f;
    all_current(&f);
    f.cu.sec = CU_SEC_READY;
    f.cu.running = (cu_version_t){ 0u, 23u, 16u };
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_IOMCU);
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT);
    CHECK(w.step[UPD_WIZ_IOMCU].older);
    CHECK_STR_EQ(w.step[UPD_WIZ_IOMCU].ver, "0.23.16");
    CHECK_STR_EQ(w.step[UPD_WIZ_IOMCU].next, "0.23.15");

    /* A newer carried image, or an IOmcu that named no version: no
     * warning. */
    f.cu.running = K_CU_OLD;
    w = eval(&f);
    CHECK(!w.step[UPD_WIZ_IOMCU].older);
    f.cu.running = (cu_version_t){ 1u, 0u, 0u };
    f.cu.known = false;
    w = eval(&f);
    CHECK(!w.step[UPD_WIZ_IOMCU].older);
    CHECK_STR_EQ(w.step[UPD_WIZ_IOMCU].ver, "-");

    /* The C6 and the devices the same way. */
    all_current(&f);
    f.c6.row = FW_C6_ROW_READY;
    f.c6.running = K_C6_NEW;
    f.d[UPD_DEV_PNODE] = dev_of(DEVUPD_ROW_READY, true, true,
                                (fw_version_t){ 0u, 24u, 0u });
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_C6, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT);
    CHECK(w.step[UPD_WIZ_C6].older);
    CHECK(w.step[UPD_WIZ_PNODE].older);
    CHECK(!w.step[UPD_WIZ_DIAL].older);

    /* Older by the patch and by the major number; newer by the patch. */
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_READY, true, true,
                               (fw_version_t){ 0u, 23u, 16u });
    f.d[UPD_DEV_BAL] = dev_of(DEVUPD_ROW_READY, true, true,
                              (fw_version_t){ 1u, 0u, 0u });
    f.d[UPD_DEV_PNODE] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    w = eval(&f);
    CHECK(w.step[UPD_WIZ_DIAL].older);
    CHECK(w.step[UPD_WIZ_BAL].older);
    CHECK(!w.step[UPD_WIZ_PNODE].older);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_OPEN, UPD_WIZ_TAG_LATER);
}

TEST_CASE(one_running_update_is_the_current_step_whatever_its_number)
{
    fx_t f;
    for (unsigned d = 0u; d < UPD_DEV_COUNT; ++d) {
        all_open(&f);
        f.d[d].row = DEVUPD_ROW_UPDATING;
        f.d[d].phase = DEVUPD_CONFIRM;
        f.d[d].secs = 27u;
        const upd_wiz_t w = eval(&f);
        CHECK_EQ(w.current, UPD_WIZ_DIAL + d);
        CHECK_STEP(w, UPD_WIZ_DIAL + d, UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING);
        CHECK_EQ(w.act, UPD_WIZ_ACT_DIAL + d);
        CHECK(!w.act_live);
        CHECK(!w.can_skip);
        CHECK(w.cancel);
        /* The steps before it keep their own tags. */
        CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT);
        CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT);
        /* Skipped, it is still current. */
        f.in.skip = UPD_WIZ_ALL;
        CHECK_EQ(eval(&f).current, UPD_WIZ_DIAL + d);
    }

    all_open(&f);
    f.c6.row = FW_C6_ROW_UPDATING;
    f.c6.percent = 43u;
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_C6);
    CHECK_EQ(w.act, UPD_WIZ_ACT_C6);
    CHECK(!w.act_live);
    CHECK(!w.cancel);
    CHECK(!w.can_skip);

    all_open(&f);
    f.cu.sec = CU_SEC_BUSY;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_IOMCU);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(!w.cancel);
    CHECK(!w.can_skip);

    all_open(&f);
    f.p.phase = UPD_PH_JOINING;
    f.d[UPD_DEV_BAL].row = DEVUPD_ROW_UPDATING;
    w = eval(&f);
    CHECK_EQ(w.current, UPD_WIZ_PANEL);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(!w.cancel);
}

TEST_CASE(null_and_zeroed_inputs_give_six_absent_steps)
{
    upd_wiz_t w;
    memset(&w, 0xA5, sizeof(w));
    upd_wizard_eval(NULL, &w);
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        CHECK_EQ(w.step[i].state, UPD_WIZ_ABSENT);
        CHECK_STR_EQ(w.step[i].ver, "-");
        CHECK_STR_EQ(w.step[i].next, "");
        CHECK(w.step[i].result == NULL);
        CHECK(!w.step[i].older);
    }
    CHECK_EQ(w.current, UPD_WIZ_NONE);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(!w.can_skip);
    CHECK(!w.cancel);
    CHECK(!w.show_skipped);

    /* A zeroed input is the same, in a run too. */
    upd_wiz_in_t in;
    memset(&in, 0, sizeof(in));
    for (unsigned run = 0u; run < 2u; ++run) {
        in.run = run != 0u;
        in.skip = UPD_WIZ_ALL;
        memset(&w, 0x5A, sizeof(w));
        upd_wizard_eval(&in, &w);
        for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
            CHECK_EQ(w.step[i].state, UPD_WIZ_ABSENT);
        }
        CHECK_EQ(w.current, UPD_WIZ_NONE);
        CHECK(!w.show_skipped);
    }

    /* Zeroed views: PANEL not checked, the rest without an image. */
    fx_t f;
    memset(&f, 0, sizeof(f));
    w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_PANEL, UPD_WIZ_DONE, UPD_WIZ_TAG_NOT_CHECKED);
    CHECK_STR_EQ(w.step[UPD_WIZ_PANEL].ver, "-");
    CHECK_STEP(w, UPD_WIZ_IOMCU, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE);
    CHECK_STEP(w, UPD_WIZ_C6, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_REPORTED);
    for (unsigned d = 0u; d < UPD_DEV_COUNT; ++d) {
        CHECK_STEP(w, UPD_WIZ_DIAL + d, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE);
    }
    CHECK_EQ(w.current, UPD_WIZ_NONE);

    /* No answer to write to: nothing happens. */
    upd_wizard_eval(&f.in, NULL);

    /* The largest version texts fit. */
    all_current(&f);
    f.cu.running = (cu_version_t){ 65535u, 65535u, 65535u };
    f.c6.running = (fw_version_t){ 65535u, 65535u, 65535u };
    memset(f.p.running, '9', sizeof(f.p.running) - 1u);
    f.p.running[sizeof(f.p.running) - 1u] = '\0';
    w = eval(&f);
    CHECK_STR_EQ(w.step[UPD_WIZ_IOMCU].ver, "65535.65535.65535");
    CHECK_STR_EQ(w.step[UPD_WIZ_C6].ver, "65535.65535.65535");
    CHECK_EQ(strlen(w.step[UPD_WIZ_PANEL].ver), WIFI_VER_MAX);
}

int main(void)
{
    RUN(nothing_checked_and_all_current_has_no_current_step);
    RUN(a_newer_release_makes_the_panel_current_and_every_linked_step_later);
    RUN(a_newer_release_found_at_step_4_moves_back_to_step_1);
    RUN(the_current_step_is_the_lowest_that_blocks);
    RUN(pending_blocks_later_steps);
    RUN(absent_blocks_only_in_a_run);
    RUN(a_skipped_step_does_not_block_and_keeps_its_tag);
    RUN(skip_all_reaches_the_end_and_show_skipped_returns_to_the_first);
    RUN(a_failed_step_stays_current_with_its_result);
    RUN(an_older_carried_iomcu_image_is_open_with_the_warning);
    RUN(one_running_update_is_the_current_step_whatever_its_number);
    RUN(null_and_zeroed_inputs_give_six_absent_steps);
    return test_summary("upd_wizard");
}
