/*
 * SETUP > UPDATES' step model (upd_wizard.c), each view's mapping: every
 * IOmcu section state, every C6 row with every reason and every device
 * row with every link state mapped to one step state, tag and reason; the
 * device feed telling a device that is not bound from one that is bound
 * and not connected; a C6 that named no version; a held step naming its
 * reason on every step.
 *
 * SPDX-License-Identifier: MIT
 */
#include "upd_wiz_fx.h"

TEST_CASE(every_cu_sec_maps_to_one_step_state)
{
    static const struct {
        cu_sec_t sec;
        want_t   out;     /**< outside a run */
        want_t   in_run;  /**< in a run */
    } k[] = {
#define SAME_IN_RUN(s, t, y) { s, t, y }, { s, t, y }
        { CU_SEC_NO_IMAGE,
          SAME_IN_RUN(UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_WHY_NONE) },
        { CU_SEC_BUSY, SAME_IN_RUN(UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING,
                                   UPD_WIZ_WHY_NONE) },
        { CU_SEC_NO_LINK,
          { UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_LINK, UPD_WIZ_WHY_NONE },
          { UPD_WIZ_WAITING, UPD_WIZ_TAG_NO_LINK, UPD_WIZ_WHY_NONE } },
        { CU_SEC_UNKNOWN, SAME_IN_RUN(UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING,
                                      UPD_WIZ_WHY_NONE) },
        { CU_SEC_CHECKING, SAME_IN_RUN(UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING,
                                       UPD_WIZ_WHY_NONE) },
        { CU_SEC_FAILED, SAME_IN_RUN(UPD_WIZ_ABSENT,
                                     UPD_WIZ_TAG_CHECK_FAILED,
                                     UPD_WIZ_WHY_NONE) },
        { CU_SEC_USB,
          SAME_IN_RUN(UPD_WIZ_ABSENT, UPD_WIZ_TAG_USB_ONLY, UPD_WIZ_WHY_NONE) },
        { CU_SEC_NO_BOARD,
          SAME_IN_RUN(UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_WHY_NONE) },
        { CU_SEC_SAME,
          SAME_IN_RUN(UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT, UPD_WIZ_WHY_NONE) },
        { CU_SEC_UNSAFE,
          SAME_IN_RUN(UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_UNSAFE) },
        { CU_SEC_RADIO,
          SAME_IN_RUN(UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_RADIO) },
        { CU_SEC_INSTALL,
          SAME_IN_RUN(UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_INSTALL) },
        { CU_SEC_READY,
          SAME_IN_RUN(UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_NONE) },
#undef SAME_IN_RUN
    };
    CHECK_EQ(sizeof(k) / sizeof(k[0]), 13);
    CHECK_EQ(CU_SEC_READY + 1, 13);
    for (unsigned i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_EQ(k[i].sec, i);
        for (unsigned run = 0u; run < 2u; ++run) {
            fx_t f;
            all_current(&f);
            f.cu.sec = k[i].sec;
            f.cu.running = K_CU_OLD;
            f.in.run = run != 0u;
            const upd_wiz_t w = eval(&f);
            const want_t want = run != 0u ? k[i].in_run : k[i].out;
            check_want(&w.step[UPD_WIZ_IOMCU], want, "cu_sec", i, run);
            /* Every other step is current: the IOmcu is current exactly
             * when it blocks. */
            CHECK_EQ(w.current == UPD_WIZ_IOMCU, upd_wiz_blocks(want.state));
            CHECK_STR_EQ(w.step[UPD_WIZ_IOMCU].ver, "0.23.14");
            CHECK_STR_EQ(w.step[UPD_WIZ_IOMCU].next, "0.23.15");
        }
    }
}

/** @brief The C6 row's expected step for a BLOCKED reason. */
static want_t c6_blocked_want(fw_c6_why_t why)
{
    switch (why) {
    case FW_C6_WHY_ARMED:
        return (want_t){ UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_ARMED };
    case FW_C6_WHY_SUPPLY:
        return (want_t){ UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_SUPPLY };
    case FW_C6_WHY_RUN:
        return (want_t){ UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_RUN };
    case FW_C6_WHY_RADIO:
        return (want_t){ UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_RADIO };
    case FW_C6_WHY_DRAWING:
        return (want_t){ UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT,
                         UPD_WIZ_WHY_DRAWING };
    case FW_C6_WHY_BLE:
        return (want_t){ UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING,
                         UPD_WIZ_WHY_BLE };
    case FW_C6_WHY_IOMCU:
    case FW_C6_WHY_INSTALL:
    case FW_C6_WHY_DEVICE:
    case FW_C6_WHY_ENDING:
        return (want_t){ UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING,
                         UPD_WIZ_WHY_BUSY };
    default:
        return (want_t){ UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_NONE };
    }
}

TEST_CASE(every_c6_row_and_why_maps_to_one_step_state)
{
    CHECK_EQ(FW_C6_ROW_BLOCKED + 1, 7);
    CHECK_EQ(FW_C6_WHY_ENDING + 1, 11);
    for (unsigned row = 0u; row <= (unsigned)FW_C6_ROW_BLOCKED; ++row) {
        for (unsigned why = 0u; why <= (unsigned)FW_C6_WHY_ENDING; ++why) {
            want_t want;
            switch ((fw_c6_row_t)row) {
            case FW_C6_ROW_READY:
                want = (want_t){ UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT,
                                 UPD_WIZ_WHY_NONE };
                break;
            case FW_C6_ROW_NO_IMAGE:
                want = (want_t){ UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE,
                                 UPD_WIZ_WHY_NONE };
                break;
            case FW_C6_ROW_BAD_IMAGE:
                want = (want_t){ UPD_WIZ_ABSENT, UPD_WIZ_TAG_BAD_IMAGE,
                                 UPD_WIZ_WHY_NONE };
                break;
            case FW_C6_ROW_UPDATING:
                want = (want_t){ UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING,
                                 UPD_WIZ_WHY_NONE };
                break;
            case FW_C6_ROW_LINK_FAILED:
                want = (want_t){ UPD_WIZ_ABSENT, UPD_WIZ_TAG_LINK_FAILED,
                                 UPD_WIZ_WHY_NONE };
                break;
            case FW_C6_ROW_REINSTALL:
                want = (want_t){ UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT,
                                 UPD_WIZ_WHY_NONE };
                break;
            default:
                want = c6_blocked_want((fw_c6_why_t)why);
                break;
            }
            for (unsigned run = 0u; run < 2u; ++run) {
                fx_t f;
                all_current(&f);
                f.c6.row = (fw_c6_row_t)row;
                f.c6.why = (fw_c6_why_t)why;
                f.c6.carried = K_C6_NEW;
                f.in.run = run != 0u;
                const upd_wiz_t w = eval(&f);
                check_want(&w.step[UPD_WIZ_C6], want, "c6", row, why);
                CHECK_EQ(w.current == UPD_WIZ_C6, upd_wiz_blocks(want.state));
                CHECK_STR_EQ(w.step[UPD_WIZ_C6].ver, "2.13.0");
                CHECK_STR_EQ(w.step[UPD_WIZ_C6].next, "2.14.0");
            }
        }
    }
    /* A gate on the carried version holds nothing back: DONE. */
    for (unsigned why = 0u; why <= (unsigned)FW_C6_WHY_ENDING; ++why) {
        fx_t f;
        all_current(&f);
        f.c6.row = FW_C6_ROW_BLOCKED;
        f.c6.why = (fw_c6_why_t)why;
        const upd_wiz_t w = eval(&f);
        check_want(&w.step[UPD_WIZ_C6],
                   (want_t){ UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT,
                             UPD_WIZ_WHY_NONE },
                   "c6 same", FW_C6_ROW_BLOCKED, why);
        CHECK_EQ(w.current, UPD_WIZ_NONE);
    }
}

TEST_CASE(every_devupd_row_maps_to_one_step_state)
{
    /* Three links per row: not bound; bound, not connected; connected on
     * another version than the carried one (REINSTALL on the same). */
    static const want_t k_unbound = { UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_BOUND,
                                      UPD_WIZ_WHY_NONE };
    static const want_t k_away = { UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_CONNECTED,
                                   UPD_WIZ_WHY_NONE };
    static const struct {
        devupd_row_t row;
        want_t       want[3];
    } k[] = {
        { DEVUPD_ROW_NO_IMAGE,
          { { UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_WHY_NONE },
            { UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_WHY_NONE },
            { UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_WHY_NONE } } },
        { DEVUPD_ROW_BAD_IMAGE,
          { { UPD_WIZ_ABSENT, UPD_WIZ_TAG_BAD_IMAGE, UPD_WIZ_WHY_NONE },
            { UPD_WIZ_ABSENT, UPD_WIZ_TAG_BAD_IMAGE, UPD_WIZ_WHY_NONE },
            { UPD_WIZ_ABSENT, UPD_WIZ_TAG_BAD_IMAGE, UPD_WIZ_WHY_NONE } } },
        { DEVUPD_ROW_NOT_LINKED, { k_unbound, k_away, k_away } },
        { DEVUPD_ROW_ARMED,
          { k_unbound, k_away,
            { UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_ARMED } } },
        { DEVUPD_ROW_BUSY,
          { k_unbound, k_away,
            { UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING, UPD_WIZ_WHY_BUSY } } },
        { DEVUPD_ROW_READY,
          { k_unbound, k_away,
            { UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_NONE } } },
        { DEVUPD_ROW_REINSTALL,
          { k_unbound, k_away,
            { UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT, UPD_WIZ_WHY_NONE } } },
        { DEVUPD_ROW_UPDATING,
          { { UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING, UPD_WIZ_WHY_NONE },
            { UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING, UPD_WIZ_WHY_NONE },
            { UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING, UPD_WIZ_WHY_NONE } } },
        { DEVUPD_ROW_CHECKING,
          { k_unbound, k_away,
            { UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING, UPD_WIZ_WHY_NONE } } },
    };
    CHECK_EQ(sizeof(k) / sizeof(k[0]), 9);
    CHECK_EQ(DEVUPD_ROW_CHECKING + 1, 9);
    for (unsigned i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_EQ(k[i].row, i);
        for (unsigned link = 0u; link < 3u; ++link) {
            for (unsigned d = 0u; d < UPD_DEV_COUNT; ++d) {
                fx_t f;
                all_current(&f);
                const fw_version_t run_ver =
                    k[i].row == DEVUPD_ROW_REINSTALL ? K_NEW : K_OLD;
                f.d[d] = dev_of(k[i].row, link != 0u, link == 2u, run_ver);
                const upd_wiz_t w = eval(&f);
                const unsigned s = UPD_WIZ_DIAL + d;
                check_want(&w.step[s], k[i].want[link], "devupd", i, link);
                CHECK_EQ(w.current == s, upd_wiz_blocks(k[i].want[link].state));
            }
        }
    }
    /* ARMED or another update on the carried version: nothing to do. */
    fx_t f;
    all_current(&f);
    f.d[UPD_DEV_DIAL] = dev_of(DEVUPD_ROW_ARMED, true, true, K_NEW);
    f.d[UPD_DEV_PNODE] = dev_of(DEVUPD_ROW_BUSY, true, true, K_NEW);
    const upd_wiz_t w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_DIAL, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);
    CHECK_STEP(w, UPD_WIZ_PNODE, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT);
    CHECK_EQ(w.current, UPD_WIZ_NONE);
}

TEST_CASE(the_feed_tells_not_bound_from_not_connected)
{
    /* As panel_devupd.c builds the row: linked is bound and linked, the
     * version known only while linked, and bound published beside it. */
    static const struct {
        bool          bound, linked, armed;
        upd_wiz_tag_t tag;
    } k[] = {
        { false, false, false, UPD_WIZ_TAG_NOT_BOUND },
        { false, true, false, UPD_WIZ_TAG_NOT_BOUND },
        { true, false, false, UPD_WIZ_TAG_NOT_CONNECTED },
        { false, false, true, UPD_WIZ_TAG_NOT_BOUND },
        { true, false, true, UPD_WIZ_TAG_NOT_CONNECTED },
        { true, true, false, UPD_WIZ_TAG_NEXT },
        { true, true, true, UPD_WIZ_TAG_NEXT },
    };
    for (unsigned i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        const bool linked = k[i].bound && k[i].linked;
        const devupd_row_in_t in = {
            .have_image = true, .image_ok = true, .linked = linked,
            .known = linked, .running = K_OLD, .carried = K_NEW,
            .armed = k[i].armed,
        };
        fx_t f;
        all_current(&f);
        f.d[UPD_DEV_DIAL] = (upd_dev_view_t){
            .row = devupd_row(&in), .bound = k[i].bound, .known = linked,
            .running = K_OLD, .have_image = true, .carried = K_NEW,
        };
        for (unsigned run = 0u; run < 2u; ++run) {
            f.in.run = run != 0u;
            const upd_wiz_t w = eval(&f);
            if (w.step[UPD_WIZ_DIAL].tag != k[i].tag) {
                T_FAIL("case %u run %u: tag %d, want %d", i, run,
                       (int)w.step[UPD_WIZ_DIAL].tag, (int)k[i].tag);
            }
            /* Not connected waits in a run; not bound never does. */
            const bool waits = run != 0u
                               && k[i].tag == UPD_WIZ_TAG_NOT_CONNECTED;
            CHECK_EQ(w.step[UPD_WIZ_DIAL].state == UPD_WIZ_WAITING, waits);
        }
    }
}

TEST_CASE(c6_version_not_reported_is_absent_not_open)
{
    for (unsigned run = 0u; run < 2u; ++run) {
        for (unsigned row = 0u; row < 2u; ++row) {
            fx_t f;
            all_current(&f);
            f.c6.row = row == 0u ? FW_C6_ROW_READY : FW_C6_ROW_BLOCKED;
            f.c6.why = FW_C6_WHY_ARMED;
            f.c6.known = false;
            f.c6.carried = K_C6_NEW;
            f.in.run = run != 0u;
            const upd_wiz_t w = eval(&f);
            CHECK_STEP(w, UPD_WIZ_C6, UPD_WIZ_ABSENT,
                       UPD_WIZ_TAG_NOT_REPORTED);
            CHECK_STR_EQ(w.step[UPD_WIZ_C6].ver, "-");
            CHECK_STR_EQ(w.step[UPD_WIZ_C6].next, "2.14.0");
            CHECK(!w.step[UPD_WIZ_C6].older);
            CHECK_EQ(w.current, UPD_WIZ_NONE);
        }
    }
    /* No image while READY is not offered either. */
    fx_t f;
    all_current(&f);
    f.c6.row = FW_C6_ROW_READY;
    f.c6.have_image = false;
    const upd_wiz_t w = eval(&f);
    CHECK_STEP(w, UPD_WIZ_C6, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE);
    CHECK_STR_EQ(w.step[UPD_WIZ_C6].next, "");
}

TEST_CASE(unsafe_holds_the_current_step_and_names_the_reason)
{
    /* PANEL: UPDATES' download gate, and no home network. */
    static const struct {
        upd_why_t     gate;
        upd_wiz_why_t why;
    } k_panel[] = {
        { UPD_WHY_ARMED, UPD_WIZ_WHY_ARMED },
        { UPD_WHY_SUPPLY, UPD_WIZ_WHY_SUPPLY },
        { UPD_WHY_RUN, UPD_WIZ_WHY_RUN },
        { UPD_WHY_STOP, UPD_WIZ_WHY_STOP },
        { UPD_WHY_FILES, UPD_WIZ_WHY_RADIO },
        { UPD_WHY_COPROC, UPD_WIZ_WHY_BUSY },
        { UPD_WHY_INSTALL, UPD_WIZ_WHY_INSTALL },
        { UPD_WHY_CANCEL, UPD_WIZ_WHY_BUSY },
        { UPD_WHY_BUSY, UPD_WIZ_WHY_BUSY },
        { UPD_WHY_DRAWING, UPD_WIZ_WHY_DRAWING },
    };
    fx_t f;
    for (unsigned i = 0u; i < sizeof(k_panel) / sizeof(k_panel[0]); ++i) {
        all_open(&f);
        f.p.dl_block = k_panel[i].gate;
        const upd_wiz_t w = eval(&f);
        check_want(&w.step[UPD_WIZ_PANEL],
                   (want_t){ UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, k_panel[i].why },
                   "panel", i, 0u);
        CHECK_EQ(w.current, UPD_WIZ_PANEL);
        CHECK_EQ(w.act, UPD_WIZ_ACT_DOWNLOAD);
        CHECK(!w.act_live);
        CHECK(w.can_skip);
    }
    all_open(&f);
    f.p.have_net = false;
    upd_wiz_t w = eval(&f);
    CHECK_EQ(w.step[UPD_WIZ_PANEL].why, UPD_WIZ_WHY_NETWORK);
    CHECK(!w.act_live);

    /* IOMCU: its section's words. */
    static const struct {
        cu_sec_t      sec;
        upd_wiz_why_t why;
    } k_cu[] = {
        { CU_SEC_UNSAFE, UPD_WIZ_WHY_UNSAFE },
        { CU_SEC_RADIO, UPD_WIZ_WHY_RADIO },
        { CU_SEC_INSTALL, UPD_WIZ_WHY_INSTALL },
    };
    for (unsigned i = 0u; i < sizeof(k_cu) / sizeof(k_cu[0]); ++i) {
        all_open(&f);
        f.in.skip = UPD_WIZ_BIT(UPD_WIZ_PANEL);
        f.cu.sec = k_cu[i].sec;
        w = eval(&f);
        CHECK_EQ(w.current, UPD_WIZ_IOMCU);
        CHECK_EQ(w.step[UPD_WIZ_IOMCU].why, k_cu[i].why);
        CHECK_EQ(w.act, UPD_WIZ_ACT_IOMCU);
        CHECK(!w.act_live);
        CHECK(w.can_skip);
    }

    /* C6 RADIO: ARMED, supply, log run, Wi-Fi, drawing. */
    static const struct {
        fw_c6_why_t   gate;
        upd_wiz_why_t why;
    } k_c6w[] = {
        { FW_C6_WHY_ARMED, UPD_WIZ_WHY_ARMED },
        { FW_C6_WHY_SUPPLY, UPD_WIZ_WHY_SUPPLY },
        { FW_C6_WHY_RUN, UPD_WIZ_WHY_RUN },
        { FW_C6_WHY_RADIO, UPD_WIZ_WHY_RADIO },
        { FW_C6_WHY_DRAWING, UPD_WIZ_WHY_DRAWING },
    };
    for (unsigned i = 0u; i < sizeof(k_c6w) / sizeof(k_c6w[0]); ++i) {
        all_open(&f);
        f.in.skip = UPD_WIZ_BIT(UPD_WIZ_PANEL) | UPD_WIZ_BIT(UPD_WIZ_IOMCU);
        f.c6.row = FW_C6_ROW_BLOCKED;
        f.c6.why = k_c6w[i].gate;
        w = eval(&f);
        CHECK_EQ(w.current, UPD_WIZ_C6);
        CHECK_EQ(w.step[UPD_WIZ_C6].state, UPD_WIZ_HELD);
        CHECK_EQ(w.step[UPD_WIZ_C6].why, k_c6w[i].why);
        CHECK_EQ(w.act, UPD_WIZ_ACT_C6);
        CHECK(!w.act_live);
        CHECK(w.can_skip);
    }

    /* Each device: ARMED. */
    for (unsigned d = 0u; d < UPD_DEV_COUNT; ++d) {
        all_open(&f);
        f.in.skip = UPD_WIZ_BIT(UPD_WIZ_DIAL + d) - 1u;
        f.d[d].row = DEVUPD_ROW_ARMED;
        w = eval(&f);
        CHECK_EQ(w.current, UPD_WIZ_DIAL + d);
        CHECK_EQ(w.step[UPD_WIZ_DIAL + d].state, UPD_WIZ_HELD);
        CHECK_EQ(w.step[UPD_WIZ_DIAL + d].why, UPD_WIZ_WHY_ARMED);
        CHECK_EQ(w.act, UPD_WIZ_ACT_DIAL + d);
        CHECK(!w.act_live);
        CHECK(w.can_skip);
        CHECK(!w.cancel);
    }
}

int main(void)
{
    RUN(every_cu_sec_maps_to_one_step_state);
    RUN(every_c6_row_and_why_maps_to_one_step_state);
    RUN(every_devupd_row_maps_to_one_step_state);
    RUN(the_feed_tells_not_bound_from_not_connected);
    RUN(c6_version_not_reported_is_absent_not_open);
    RUN(unsafe_holds_the_current_step_and_names_the_reason);
    return test_summary("upd_wizard_map");
}
