/*
 * SETUP > UPDATES' steps (upd_wiz_view.c, upd_wiz_card.c): in each current
 * step only its control, SKIP THIS STEP, CHECK and OPTIONS take a press, on
 * a 16px grid over the body; the rows take none; every step's 2s hold
 * fires its own latch at 2.00s and not at 1.99s, once; a grey hold says
 * why on the band and fires nothing; a hold slid off, or whose step
 * changes under it (link lost, ARMED, a newer release found), ends
 * unfired; SKIP passes the step and the next becomes current, SHOW
 * SKIPPED STEPS brings it back; SKIP on the IOmcu's step declines the
 * offer UPDATES shows; a newer release moves the current step back to
 * PANEL; CANCEL UPDATE stands only while a device's transfer runs and is
 * grey from END; a device's phases on the hold's face; a failed attempt's
 * reason on the card with the hold live; the last card names the steps
 * not connected, not updatable and skipped; the WI-FI button only while no
 * step blocks; OPTIONS grey while a check or an update runs; STOP and
 * ARMED during each step; a second finger starts nothing and STOP is not
 * shadowed; every state inside its canvas in both languages; the rows'
 * and controls' rectangles.
 *
 * SPDX-License-Identifier: MIT
 */
#include "upd_rig.h"

#include "coproc_update_screen.h"
#include "ui_band.h"
#include "ui_footer.h"
#include "upd_dev.h"
#include "upd_wiz_view.h"
#include "upd_wizard.h"

/* ------------------------------------------------------------ the rig */

static upd_cu_view_t  g_cu;
static upd_c6_view_t  g_c6;
static upd_dev_view_t g_d[UPD_DEV_COUNT];

/** @brief Hands UPDATES every view, as the application does each frame. */
static void hand(void)
{
    push();
    upd_screen_set_coproc(&g_cu);
    upd_c6_set(&g_c6);
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        upd_dev_set((upd_dev_id_t)i, &g_d[i]);
    }
}

/** @brief Every step current: a check that found nothing newer, the IOmcu,
 *         the C6 and each device on the carried version. */
static void all_current(void)
{
    base_view();
    g_v.job = UPD_JOB_CHECK;
    g_v.phase = UPD_PH_ENDED;
    g_v.got = UPD_GOT_CURRENT;
    snprintf(g_v.found, sizeof(g_v.found), "%s", "0.19.0");
    g_cu = (upd_cu_view_t){ .sec = CU_SEC_SAME, .known = true,
                            .have_image = true, .running = { 0, 19, 0 },
                            .carried = { 0, 19, 0 } };
    g_c6 = (upd_c6_view_t){ .row = FW_C6_ROW_REINSTALL, .known = true,
                            .have_image = true, .running = { 2u, 13u, 0u },
                            .carried = { 2u, 13u, 0u } };
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        memset(&g_d[i], 0, sizeof(g_d[i]));
        g_d[i].row = DEVUPD_ROW_REINSTALL;
        g_d[i].bound = true;
        g_d[i].known = true;
        g_d[i].have_image = true;
        g_d[i].running = (fw_version_t){ 0u, 19u, 0u };
        g_d[i].carried = (fw_version_t){ 0u, 19u, 0u };
    }
}

/** @brief Step @p i with another version and its hold live; the rest
 *         current. */
static void open_only(unsigned i)
{
    all_current();
    switch (i) {
    case UPD_WIZ_PANEL:
        set_newer();
        break;
    case UPD_WIZ_IOMCU:
        g_cu.sec = CU_SEC_READY;
        g_cu.running = (cu_version_t){ 0, 18, 0 };
        break;
    case UPD_WIZ_C6:
        g_c6.row = FW_C6_ROW_READY;
        g_c6.carried = (fw_version_t){ 2u, 14u, 0u };
        break;
    default:
        g_d[i - UPD_WIZ_DIAL].row = DEVUPD_ROW_READY;
        g_d[i - UPD_WIZ_DIAL].running = (fw_version_t){ 0u, 18u, 0u };
        break;
    }
}

/** @brief The latches' bits. */
enum {
    F_DL = 0x01, F_CU = 0x02, F_C6 = 0x04, F_DIAL = 0x08, F_PNODE = 0x10,
    F_BAL = 0x20, F_CHECK = 0x40, F_CANCEL = 0x80,
};

/** @brief Each step's latch. */
static const unsigned k_fire[UPD_WIZ_STEPS] = {
    F_DL, F_CU, F_C6, F_DIAL, F_PNODE, F_BAL,
};

/** @brief Every latch the screens set since the last call, drained. */
static unsigned drain(void)
{
    unsigned f = 0u;
    const unsigned r = upd_screen_take();
    f |= (r & UPD_REQ_DOWNLOAD) != 0u ? F_DL : 0u;
    f |= (r & (UPD_REQ_CHECK | UPD_REQ_CANCEL)) != 0u ? F_CHECK : 0u;
    f |= upd_screen_take_coproc() ? F_CU : 0u;
    f |= upd_c6_take() ? F_C6 : 0u;
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        f |= upd_dev_take((upd_dev_id_t)i) ? (unsigned)F_DIAL << i : 0u;
    }
    f |= upd_dev_take_cancel() ? F_CANCEL : 0u;
    return f;
}

/** @brief UPDATES on the router, step @p i alone open, nothing pending. */
static void on_step(unsigned i)
{
    fresh();
    ui_router_goto(SCREEN_UPDATES);
    ui_router_set_alert(NULL);
    open_only(i);
    hand();
    (void)drain();
    (void)upd_screen_take_skip_coproc();
}

/** @brief The steps now. */
static upd_wiz_t wiz(void)
{
    upd_wiz_t w;
    upd_wiz_now(&w);
    return w;
}

/** @brief A press of finger @p id held @p s seconds, not let go. */
static void hold_for(uint8_t id, int x, int y, float s)
{
    ev(upd(), TOUCH_EVENT_DOWN, id, x, y, 0);
    while (s > 0.0f) {
        const float dt = s > UI_HOLD_MAX_CREDIT_S ? UI_HOLD_MAX_CREDIT_S : s;
        upd()->tick(dt);
        s -= dt;
    }
}

/** @brief Whether the band says @p text. */
static bool band_says(const char *text)
{
    const char *a = ui_router_alert();
    return a != NULL && text != NULL && strcmp(a, text) == 0;
}

/** @brief Whether the card has a paragraph that holds @p part. */
static bool card_has(const char *part)
{
    const upd_wiz_t w = wiz();
    const int n = upd_wiz_card_build(&w);
    for (int i = 0; i < n; ++i) {
        if (strstr(upd_wiz_card_para(i), part) != NULL) {
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------------- the layout */

/* The rows 460 x 48px from x 16, y 88, 56px apart, the last over the
 * footer; the card x 484 to 1016, y 16 to 436; the control 500 x 64px at
 * y 292, SKIP 500 x 56px at y 364, 16px over the card's foot. */
TEST_CASE(the_rows_and_controls_keep_their_rectangles)
{
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        const gfx_rect_t r = upd_wiz_row_rect(i);
        CHECK_EQ(r.x, 16);
        CHECK_EQ(r.y, UP_ROW_T + (int)i * UP_ROW_PITCH);
        CHECK_EQ(r.w, 460);
        CHECK_EQ(r.h, 48);
    }
    CHECK(upd_wiz_row_rect(5).y + 48 <= UI_FOOTER_FLOOR);
    const gfx_rect_t c = upd_wiz_card_rect();
    CHECK(c.x == 484 && c.y == 16 && c.w == 532 && c.h == 420);
    const gfx_rect_t a = upd_wiz_act_rect();
    CHECK(a.x == 500 && a.y == UP_ACT_T && a.w == 500 && a.h == 64);
    const gfx_rect_t s = upd_wiz_skip_rect();
    CHECK(s.x == 500 && s.y == UP_SKIP_T && s.w == 500 && s.h == 56);
    CHECK_EQ(s.y + s.h + 16, c.y + c.h);
    CHECK(gfx_rect_contains(a, UP_ACT_X, UP_ACT_Y));
    CHECK(gfx_rect_contains(s, UP_ACT_X, UP_SKIP_Y));
}

/* Each step current in turn, a 2.25s press at every point of a 16px grid
 * over the body: the control fires the step's latch and no other, SKIP
 * passes the step, CHECK asks for a check, OPTIONS opens its list, and
 * every other point does nothing. */
TEST_CASE(only_the_current_steps_control_takes_a_press)
{
    const gfx_rect_t act = upd_wiz_act_rect();
    const gfx_rect_t skip = upd_wiz_skip_rect();
    const gfx_rect_t check = gfx_rect_make(16, 16, 460, 56);
    const gfx_rect_t opts = ui_footer_rect(UI_FOOTER_SETTINGS);
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        for (int y = 8; y < UI_BODY_H; y += 16) {
            for (int x = 8; x < UI_PANEL_W; x += 16) {
                on_step(i);
                if (wiz().current != i) {
                    T_FAIL("step %u is not current", i + 1u);
                    return;
                }
                hold_for(1, x, y, 2.25f);
                ev(upd(), TOUCH_EVENT_UP, 1, x, y, 0);
                const unsigned f = drain();
                const upd_wiz_t w = wiz();
                const bool covered = upd()->covered();
                if (gfx_rect_contains(act, x, y)) {
                    CHECK_EQ(f, k_fire[i]);
                } else if (gfx_rect_contains(skip, x, y)) {
                    CHECK_EQ(f, 0u);
                    CHECK_EQ(w.current, (unsigned)UPD_WIZ_NONE);
                } else if (gfx_rect_contains(check, x, y)) {
                    CHECK_EQ(f, (unsigned)F_CHECK);
                } else if (gfx_rect_contains(opts, x, y)) {
                    CHECK(covered);
                } else if (f != 0u || covered || w.current != i) {
                    T_FAIL("step %u: a press at %d,%d acted", i + 1u, x, y);
                    return;
                }
            }
        }
    }
}

/* A tap and a 3s press on every row change nothing. */
TEST_CASE(step_rows_take_no_press)
{
    on_step(UPD_WIZ_DIAL);
    for (unsigned r = 0u; r < UPD_WIZ_STEPS; ++r) {
        const gfx_rect_t row = upd_wiz_row_rect(r);
        tap(upd(), row.x + row.w / 2, row.y + row.h / 2);
        hold_for(1, row.x + 4, row.y + 4, 3.0f);
        ev(upd(), TOUCH_EVENT_UP, 1, row.x + 4, row.y + 4, 0);
        CHECK_EQ(drain(), 0u);
        CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_DIAL);
        CHECK(ui_router_alert() == NULL);
    }
}

/* The download's hold: nothing at 1.99s, the request once at 2.00s, no
 * more while the finger stays. */
TEST_CASE(the_download_hold_at_1_99_s_and_2_00_s_requests_once)
{
    on_step(UPD_WIZ_PANEL);
    hold_for(1, UP_DL_X, UP_DL_Y, 1.99f);
    CHECK_EQ(drain(), 0u);
    upd()->tick(0.01f);
    CHECK_EQ(drain(), (unsigned)F_DL);
    upd()->tick(1.0f);
    ev(upd(), TOUCH_EVENT_UP, 1, UP_DL_X, UP_DL_Y, 0);
    CHECK_EQ(drain(), 0u);
}

/* Each step's hold fires its own latch at 2.00s and no other; at 1.99s
 * nothing. */
TEST_CASE(each_steps_hold_fires_its_own_latch_and_no_other)
{
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        on_step(i);
        const upd_wiz_t w = wiz();
        CHECK_EQ(w.current, i);
        CHECK(w.act_live);
        hold_for(1, UP_ACT_X, UP_ACT_Y, 1.99f);
        CHECK_EQ(drain(), 0u);
        upd()->tick(0.01f);
        CHECK_EQ(drain(), k_fire[i]);
        ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_ACT_Y, 0);
        CHECK_EQ(drain(), 0u);
    }
}

/** @brief One held state of a step: its view and the band's reason. */
typedef struct {
    unsigned    step;   /**< the step */
    int         what;   /**< the reason, per step */
} held_t;

/**
 * @brief Holds step @p h.step for reason @p h.what in the views, as the
 *        application would hand them, and hands them.
 * @param h the step and its reason
 */
static void set_held(held_t h)
{
    switch (h.step) {
    case UPD_WIZ_PANEL:
        if (h.what == 0) {
            g_v.have_net = false;
        } else {
            static const upd_why_t k[] = {
                UPD_WHY_ARMED, UPD_WHY_SUPPLY, UPD_WHY_RUN, UPD_WHY_STOP,
                UPD_WHY_FILES, UPD_WHY_INSTALL, UPD_WHY_DRAWING,
            };
            g_v.dl_block = k[h.what - 1];
        }
        break;
    case UPD_WIZ_IOMCU: {
        static const cu_sec_t k[] = { CU_SEC_UNSAFE, CU_SEC_RADIO,
                                      CU_SEC_INSTALL };
        g_cu.sec = k[h.what];
        break;
    }
    case UPD_WIZ_C6: {
        static const fw_c6_why_t k[] = {
            FW_C6_WHY_ARMED, FW_C6_WHY_SUPPLY, FW_C6_WHY_RUN,
            FW_C6_WHY_RADIO, FW_C6_WHY_DRAWING,
        };
        g_c6.row = FW_C6_ROW_BLOCKED;
        g_c6.why = k[h.what];
        break;
    }
    default:
        g_d[h.step - UPD_WIZ_DIAL].row = DEVUPD_ROW_ARMED;
        break;
    }
    hand();
}

/**
 * @brief UPDATES with step @p h.step held for reason @p h.what.
 * @param h the step and its reason
 * @return the band's line a press on the grey hold must show
 */
static const char *make_held(held_t h)
{
    on_step(h.step);
    set_held(h);
    static char why[UI_ALERT_MAX];
    const upd_wiz_t w = wiz();
    upd_wiz_why_text(&w, why, sizeof(why));
    return why;
}

/** @brief ARMED, as each step's gate reads it. */
static held_t armed_of(unsigned i)
{
    return (held_t){ i, i == UPD_WIZ_PANEL ? 1 : 0 };
}

/* Every step held by each reason a gate gives: the hold grey, a press of
 * any length fires nothing and says why on the band; the reason is a
 * line of the card too. */
TEST_CASE(a_held_steps_grey_hold_says_why_on_the_band)
{
    static const held_t k[] = {
        { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 0, 6 },
        { 0, 7 }, { 1, 0 }, { 1, 1 }, { 1, 2 }, { 2, 0 }, { 2, 1 }, { 2, 2 },
        { 2, 3 }, { 2, 4 }, { 3, 0 }, { 4, 0 }, { 5, 0 },
    };
    for (size_t n = 0; n < sizeof(k) / sizeof(k[0]); ++n) {
        const char *why = make_held(k[n]);
        const upd_wiz_t w = wiz();
        CHECK_EQ(w.current, k[n].step);
        CHECK_EQ(w.step[k[n].step].state, UPD_WIZ_HELD);
        CHECK(!w.act_live);
        CHECK(w.can_skip);
        CHECK(why[0] != '\0');
        hold_for(1, UP_ACT_X, UP_ACT_Y, 3.0f);
        ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_ACT_Y, TOUCH_FLAG_NO_TAP);
        CHECK_EQ(drain(), 0u);
        CHECK(band_says(why));
        ui_router_set_alert(NULL);
        tap(upd(), UP_ACT_X, UP_ACT_Y);
        CHECK(band_says(why));
    }
}

/* A hold slid off its control ends unfired and does not resume on the
 * way back; a lost gesture ends it too. */
TEST_CASE(the_hold_slides_off_cancels_and_leaves_unfired)
{
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        on_step(i);
        hold_for(1, UP_ACT_X, UP_ACT_Y, 1.0f);
        ev(upd(), TOUCH_EVENT_MOVE, 1, UP_ACT_X, UP_ACT_T - 1, 0);
        ev(upd(), TOUCH_EVENT_MOVE, 1, UP_ACT_X, UP_ACT_Y, 0);
        for (int t = 0; t < 12; ++t) {
            upd()->tick(UI_HOLD_MAX_CREDIT_S);
        }
        ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_ACT_Y, 0);
        CHECK_EQ(drain(), 0u);

        on_step(i);
        hold_for(1, UP_ACT_X, UP_ACT_Y, 1.0f);
        upd()->cancel();
        for (int t = 0; t < 12; ++t) {
            upd()->tick(UI_HOLD_MAX_CREDIT_S);
        }
        ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_ACT_Y, 0);
        CHECK_EQ(drain(), 0u);
    }
}

/* Half a hold, then the step changes under the finger: the IOmcu's link
 * lost, ARMED on a device, a newer release found while a later step is
 * current.  The hold ends unfired and does not come back with the state. */
TEST_CASE(the_step_changing_under_a_hold_ends_it_unfired)
{
    for (int k = 0; k < 3; ++k) {
        const unsigned step = k == 0 ? (unsigned)UPD_WIZ_IOMCU
                                     : (unsigned)UPD_WIZ_DIAL;
        on_step(step);
        hold_for(1, UP_ACT_X, UP_ACT_Y, 1.0f);
        if (k == 0) {
            g_cu.sec = CU_SEC_NO_LINK;
        } else if (k == 1) {
            g_d[0].row = DEVUPD_ROW_ARMED;
        } else {
            set_newer();
        }
        hand();
        hold_for(1, UP_ACT_X, UP_ACT_Y, 0.0f);
        upd()->tick(0.5f);
        open_only(step);                 /* back as it was */
        hand();
        upd()->tick(2.0f);
        ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_ACT_Y, 0);
        CHECK_EQ(drain(), 0u);
    }
}

/* A newer release found while a device's step is current makes PANEL
 * current again; IOMCU and the devices read LATER under it. */
TEST_CASE(a_newer_release_moves_the_current_step_back_to_panel)
{
    on_step(UPD_WIZ_BAL);
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_BAL);
    set_newer();
    hand();
    const upd_wiz_t w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_PANEL);
    CHECK_EQ(w.act, UPD_WIZ_ACT_DOWNLOAD);
    CHECK_EQ(w.step[UPD_WIZ_BAL].tag, UPD_WIZ_TAG_LATER);
    CHECK_EQ(w.step[UPD_WIZ_IOMCU].tag, UPD_WIZ_TAG_LATER);
    CHECK(card_has("0.20.0"));
}

/* SKIP passes the current step and the next blocking one becomes
 * current; with none left the last card names the skipped steps and SHOW
 * SKIPPED STEPS brings them back.  A tap that was no tap skips nothing. */
TEST_CASE(skip_advances_and_show_skipped_returns)
{
    on_step(UPD_WIZ_IOMCU);
    g_d[1].row = DEVUPD_ROW_READY;
    g_d[1].running = (fw_version_t){ 0u, 18u, 0u };
    hand();
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
    CHECK_EQ(wiz().step[UPD_WIZ_PNODE].tag, UPD_WIZ_TAG_LATER);
    ev(upd(), TOUCH_EVENT_DOWN, 1, UP_ACT_X, UP_SKIP_Y, 0);
    ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_SKIP_Y, TOUCH_FLAG_NO_TAP);
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
    tap(upd(), UP_ACT_X, UP_SKIP_Y);
    upd_wiz_t w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_PNODE);
    CHECK_EQ(w.step[UPD_WIZ_IOMCU].tag, UPD_WIZ_TAG_SKIPPED);
    CHECK(!w.show_skipped);
    tap(upd(), UP_ACT_X, UP_SKIP_Y);
    w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_NONE);
    CHECK(w.show_skipped);
    CHECK_EQ(drain(), 0u);
    char names[64];
    snprintf(names, sizeof(names), "%s, %s", upd_wiz_name(UPD_WIZ_IOMCU),
             upd_wiz_name(UPD_WIZ_PNODE));
    CHECK(card_has(names));
    tap(upd(), UP_ACT_X, UP_SKIP_Y);              /* SHOW SKIPPED STEPS */
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
    /* The mask lives until the panel's start: UPDATES' reset. */
    tap(upd(), UP_ACT_X, UP_SKIP_Y);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_UPDATES);
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_PNODE);
    upd()->reset();
    hand();                              /* the reset dropped the section */
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
}

/** @brief The update side's driver and feed. */
static cu_driver_t g_drv;
static cu_feed_t   g_feed;

/** @brief A frame of the update's driver. */
static cu_hand_t cu_frame(void)
{
    cu_hand_t h;
    coproc_update_drive(&g_drv, &g_feed, false, &h);
    return h;
}

/* SKIP on the IOmcu's step hands the decline over while the offer UPDATES
 * shows stands: once, for that offer.  An offer UPDATES has shown does not
 * open it again from OVERVIEW, skipped or not, and its band line is not
 * said.  Without an offer SKIP declines nothing. */
TEST_CASE(skip_on_the_iomcu_step_hands_over_the_decline_while_an_offer_stands)
{
    for (int k = 0; k < 3; ++k) {
        const bool offered = k != 0, skip = k != 2;
        on_step(UPD_WIZ_IOMCU);
        coproc_update_screen()->reset();
        memset(&g_drv, 0, sizeof(g_drv));
        memset(&g_feed, 0, sizeof(g_feed));
        g_feed.running = (cu_version_t){ 0, 18, 0 };
        g_feed.carried = (cu_version_t){ 0, 19, 0 };
        g_feed.have_image = true;
        g_feed.reachable = true;
        g_feed.check = CU_CHECK_DIFFERENT;
        g_feed.offered = offered;
        g_feed.offer_gen = 4u;
        g_drv.said_gen = 4u;             /* its band line already said */
        CHECK(!cu_frame().decline);
        CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
        if (skip) {
            tap(upd(), UP_ACT_X, UP_SKIP_Y);
        }
        CHECK_EQ(cu_frame().decline, offered && skip);
        CHECK(!cu_frame().decline);
        ui_router_goto(SCREEN_OVERVIEW);
        ui_router_set_alert(NULL);
        (void)cu_frame();
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        CHECK(ui_router_alert() == NULL);
    }
}

/* CANCEL UPDATE stands in the footer only while a device's transfer runs:
 * a tap before END sets the cancel latch once; from END it is grey and a
 * tap says the device installs. */
TEST_CASE(cancel_update_stands_only_while_a_device_transfer_runs)
{
    const gfx_rect_t cr = ui_footer_rect(UI_FOOTER_HOLD);
    const int cx = cr.x + cr.w / 2, cy = cr.y + cr.h / 2;
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        on_step(i);
        CHECK(!wiz().cancel);
        tap(upd(), cx, cy);
        CHECK_EQ(drain(), 0u);
    }
    for (unsigned d = 0u; d < UPD_DEV_COUNT; ++d) {
        on_step(UPD_WIZ_DIAL + d);
        g_d[d].row = DEVUPD_ROW_UPDATING;
        g_d[d].phase = DEVUPD_SEND;
        hand();
        CHECK(wiz().cancel);
        CHECK(!wiz().can_skip);
        tap(upd(), cx, cy);
        CHECK_EQ(drain(), (unsigned)F_CANCEL);
        CHECK_EQ(drain(), 0u);
        g_d[d].phase = DEVUPD_END;
        hand();
        tap(upd(), cx, cy);
        CHECK_EQ(drain(), 0u);
        CHECK(band_says(TR(DEV_WHY_INSTALLING)));
        g_d[d].row = DEVUPD_ROW_REINSTALL;
        hand();
        CHECK(!wiz().cancel);
    }
}

/* The device's transfer on the hold's face: connecting, the ask with the
 * seconds the device gave, the share it took; the card says how to
 * accept; the hold takes no press then. */
TEST_CASE(the_device_step_shows_connecting_the_ask_with_seconds_and_the_share)
{
    char want[96], got[96];
    for (unsigned d = 0u; d < UPD_DEV_COUNT; ++d) {
        static const uint8_t k_ph[] = { DEVUPD_LINK, DEVUPD_CONFIRM,
                                        DEVUPD_SEND };
        for (size_t p = 0; p < sizeof(k_ph); ++p) {
            on_step(UPD_WIZ_DIAL + d);
            g_d[d].row = DEVUPD_ROW_UPDATING;
            g_d[d].phase = k_ph[p];
            g_d[d].secs = 27u;
            g_d[d].percent = 43u;
            hand();
            const upd_wiz_t w = wiz();
            CHECK_EQ(w.step[UPD_WIZ_DIAL + d].tag, UPD_WIZ_TAG_UPDATING);
            upd_dev_label((upd_dev_id_t)d, &g_d[d], want, sizeof(want));
            upd_wiz_act_label(&w, got, sizeof(got));
            CHECK_STR_EQ(got, want);
            CHECK(strstr(got, k_ph[p] == DEVUPD_CONFIRM ? "27" : "") != NULL);
            CHECK(k_ph[p] != DEVUPD_SEND || strstr(got, "43") != NULL);
            if (k_ph[p] == DEVUPD_CONFIRM) {
                CHECK(card_has(d == 0u ? TR(UPD_WIZ_ASK_DIAL)
                                       : TR(UPD_WIZ_ASK_NODE)));
            }
            hold_for(1, UP_ACT_X, UP_ACT_Y, 2.5f);
            ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_ACT_Y, 0);
            CHECK_EQ(drain(), 0u);
        }
    }
    /* The C6's update: its share on the face. */
    on_step(UPD_WIZ_C6);
    g_c6.row = FW_C6_ROW_UPDATING;
    g_c6.percent = 61u;
    hand();
    const upd_wiz_t w = wiz();
    upd_wiz_act_label(&w, got, sizeof(got));
    CHECK(strstr(got, "61") != NULL);
    CHECK(!w.cancel);
}

/* A failed C6 update whose retry waits for the supply output: the card
 * keeps the failure's reason in both languages and lets the restart note
 * go first when the card is full (Codex P2 on #874). */
TEST_CASE(a_held_c6_retry_keeps_the_failure_reason_on_the_card)
{
    static char why[128];
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        set_held((held_t){ UPD_WIZ_C6, 1 });         /* FW_C6_WHY_SUPPLY */
        on_step(UPD_WIZ_C6);
        set_held((held_t){ UPD_WIZ_C6, 1 });
        (void)snprintf(why, sizeof(why), TR(ALERT_C6_FAILED),
                       TR(C6_FAIL_VERIFY));
        upd_screen_set_result(UPD_WIZ_C6, why);
        CHECK_EQ(wiz().current, UPD_WIZ_C6);
        CHECK(card_has(why));
        upd_screen_set_result(UPD_WIZ_C6, NULL);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A failed attempt: FAILED in the row, the reason on the card, the hold
 * live as the retry; a success clears it. */
TEST_CASE(a_failed_attempt_shows_its_reason_and_keeps_the_hold)
{
    static const char k_why[] = "Dial update stopped: no answer.";
    for (unsigned i = UPD_WIZ_IOMCU; i < UPD_WIZ_STEPS; ++i) {
        on_step(i);
        upd_screen_set_result(i, k_why);
        upd_wiz_t w = wiz();
        CHECK_EQ(w.current, i);
        CHECK_EQ(w.step[i].state, UPD_WIZ_FAILED);
        CHECK_EQ(w.step[i].tag, UPD_WIZ_TAG_FAILED);
        CHECK(w.act_live && w.can_skip);
        CHECK(card_has(k_why));
        hold_for(1, UP_ACT_X, UP_ACT_Y, 2.0f);
        ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_ACT_Y, 0);
        CHECK_EQ(drain(), k_fire[i]);
        upd_screen_set_result(i, NULL);
        w = wiz();
        CHECK_EQ(w.step[i].state, UPD_WIZ_OPEN);
        CHECK(!card_has(k_why));
    }
    upd_screen_set_result(UPD_WIZ_STEPS, k_why);      /* out of range */
    CHECK(!card_has(k_why));
}

/* After a failed or STOP-aborted download PANEL reads FAILED and blocks
 * nothing; the last card shows the result, CHECK is the way back. */
TEST_CASE(a_failed_download_blocks_nothing_and_check_is_the_way_back)
{
    static const upd_got_t k_got[] = { UPD_GOT_HASH, UPD_GOT_ABORTED };
    for (size_t k = 0; k < 2; ++k) {
        on_step(UPD_WIZ_PANEL);
        g_v.job = UPD_JOB_DOWNLOAD;
        g_v.got = k_got[k];
        g_v.why = UPD_WHY_STOP;
        hand();
        const upd_wiz_t w = wiz();
        CHECK_EQ(w.current, (unsigned)UPD_WIZ_NONE);
        CHECK_EQ(w.step[UPD_WIZ_PANEL].tag, UPD_WIZ_TAG_FAILED);
        char line[96], second[96];
        upd_result_text(&g_v, line, sizeof(line), second, sizeof(second));
        CHECK(card_has(line));
        tap(upd(), UP_BTN_X, UP_CHECK_Y);
        CHECK_EQ(drain(), (unsigned)F_CHECK);
    }
}

/* The last card: all firmware current, the IOmcu without a link and a
 * device not connected named, a step without firmware named, a skipped
 * step named; a device not bound named nowhere. */
TEST_CASE(the_end_card_names_skipped_and_absent_steps)
{
    on_step(UPD_WIZ_DIAL);
    g_cu.sec = CU_SEC_NO_LINK;
    g_d[1].row = DEVUPD_ROW_NOT_LINKED;
    g_d[1].known = false;
    g_d[2] = g_d[1];
    g_d[2].bound = false;
    g_c6.row = FW_C6_ROW_NO_IMAGE;
    hand();
    tap(upd(), UP_ACT_X, UP_SKIP_Y);
    const upd_wiz_t w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_NONE);
    CHECK(card_has(TR(UPD_WIZ_DONE_HEAD)));
    char line[96], names[64];
    snprintf(names, sizeof(names), "%s, %s", upd_wiz_name(UPD_WIZ_IOMCU),
             upd_wiz_name(UPD_WIZ_PNODE));
    snprintf(line, sizeof(line), TR(UPD_WIZ_END_NOT_CONN), names);
    CHECK(card_has(line));
    snprintf(line, sizeof(line), TR(UPD_WIZ_END_OTHER),
             upd_wiz_name(UPD_WIZ_C6));
    CHECK(card_has(line));
    snprintf(line, sizeof(line), TR(UPD_WIZ_END_SKIPPED),
             upd_wiz_name(UPD_WIZ_DIAL));
    CHECK(card_has(line));
    CHECK(!card_has(upd_wiz_name(UPD_WIZ_BAL)));
}

/* SAVE A NETWORK: OPEN WI-FI stands in the control's place only on the
 * last card, with Wi-Fi set up and no network kept; a step that blocks
 * takes the place. */
TEST_CASE(no_home_network_shows_the_wifi_button_only_while_no_step_blocks)
{
    on_step(UPD_WIZ_IOMCU);
    g_v.have_net = false;
    hand();
    upd_wiz_t w = wiz();
    CHECK(!upd_wiz_net_shown(&w));
    tap(upd(), UP_ACT_X, UP_ACT_Y);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    g_cu.sec = CU_SEC_SAME;
    hand();
    w = wiz();
    CHECK(upd_wiz_net_shown(&w));
    g_v.unavailable = true;
    hand();
    w = wiz();
    CHECK(!upd_wiz_net_shown(&w));
    g_v.unavailable = false;
    hand();
    tap(upd(), UP_ACT_X, UP_ACT_Y);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
}

/* OPTIONS opens with nothing running; a check, a download, the IOmcu's,
 * the C6's or a device's update greys it and a tap says why. */
TEST_CASE(options_is_grey_while_any_update_or_check_runs)
{
    for (int k = 0; k < 6; ++k) {
        on_step(UPD_WIZ_DIAL);
        switch (k) {
        case 0: break;
        case 1: g_v.phase = UPD_PH_ASKING; break;
        case 2: g_v.job = UPD_JOB_DOWNLOAD; g_v.phase = UPD_PH_IMAGE; break;
        case 3: g_cu.sec = CU_SEC_BUSY; break;
        case 4: g_c6.row = FW_C6_ROW_UPDATING; break;
        default: g_d[0].row = DEVUPD_ROW_UPDATING; break;
        }
        hand();
        tap(upd(), UP_SET_X, UP_SET_Y);
        CHECK_EQ(upd()->covered(), k == 0);
        CHECK(k == 0 || band_says(TR(FOOT_SETTINGS_BUSY)));
    }
    /* A finger on the step's hold greys it as well. */
    on_step(UPD_WIZ_DIAL);
    hold_for(1, UP_ACT_X, UP_ACT_Y, 0.5f);
    ev(upd(), TOUCH_EVENT_DOWN, 2, UP_SET_X, UP_SET_Y, 0);
    ev(upd(), TOUCH_EVENT_UP, 2, UP_SET_X, UP_SET_Y, 0);
    CHECK(!upd()->covered());
}

/* Each step half held, then ARMED: the step HELD, the hold ends unfired
 * and is grey; a STOP tapped on the band while a hold is down is taken,
 * and PANEL's download, which STOP blocks, ends unfired. */
TEST_CASE(stop_and_armed_during_each_step)
{
    const gfx_rect_t stop = ui_band_stop_rect();
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        on_step(i);
        hold_for(1, UP_ACT_X, UP_ACT_Y, 1.0f);
        set_held(armed_of(i));
        upd()->tick(2.0f);
        open_only(i);                    /* SAFE again under the finger */
        hand();
        upd()->tick(2.0f);
        ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_ACT_Y, 0);
        CHECK_EQ(drain(), 0u);
        set_held(armed_of(i));
        CHECK(!wiz().act_live);
        CHECK_EQ(wiz().step[i].state, UPD_WIZ_HELD);

        on_step(i);
        touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                            .point = { .id = 1, .x = UP_ACT_X,
                                       .y = (int16_t)(UI_BAND_H + UP_ACT_Y),
                                       .strength = 40 } };
        ui_router_event(&e);
        ui_router_tick(1.0f);
        touch_event_t s = { .type = TOUCH_EVENT_DOWN,
                            .point = { .id = 2,
                                       .x = (int16_t)(stop.x + stop.w / 2),
                                       .y = (int16_t)(stop.y + stop.h / 2),
                                       .strength = 40 } };
        ui_router_event(&s);
        s.type = TOUCH_EVENT_UP;
        ui_router_event(&s);
        CHECK(ui_router_take_stop());
        if (i == UPD_WIZ_PANEL) {
            g_v.dl_block = UPD_WHY_STOP;
            hand();
        }
        ui_router_tick(0.5f);
        CHECK(i != UPD_WIZ_PANEL || drain() == 0u);
        e.type = TOUCH_EVENT_UP;
        ui_router_event(&e);
        (void)drain();
    }
}

/* A second finger on SKIP, CHECK or OPTIONS while the first holds the
 * control starts nothing, and the first finger's hold still completes. */
TEST_CASE(a_second_finger_starts_nothing_and_the_first_completes)
{
    static const int k_spots[][2] = {
        { UP_ACT_X, UP_SKIP_Y }, { UP_BTN_X, UP_CHECK_Y },
        { UP_SET_X, UP_SET_Y },
    };
    for (size_t k = 0; k < 3; ++k) {
        on_step(UPD_WIZ_IOMCU);
        hold_for(1, UP_ACT_X, UP_ACT_Y, 0.5f);
        ev(upd(), TOUCH_EVENT_DOWN, 2, k_spots[k][0], k_spots[k][1], 0);
        ev(upd(), TOUCH_EVENT_UP, 2, k_spots[k][0], k_spots[k][1], 0);
        CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
        CHECK(!upd()->covered());
        hold_for(1, UP_ACT_X, UP_ACT_Y, 0.0f);
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
        CHECK_EQ(drain(), (unsigned)F_CU);
        ev(upd(), TOUCH_EVENT_UP, 1, UP_ACT_X, UP_ACT_Y, 0);
    }
}

/* Every step in every state, the last card, a run, a result, both
 * languages: inside the body's canvas and every pixel painted. */
TEST_CASE(every_state_renders_inside_its_canvas_in_both_languages)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(UI_PANEL_W, UI_BODY_H)];
    gfx_canvas_t c;
    guard_canvas_t g;
    for (int lang = 0; lang < 2; ++lang) {
        for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
            for (int st = 0; st < 6; ++st) {
                on_step(i);
                ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
                if (st == 1) {
                    set_held((held_t){ i, 0 });
                } else if (st == 2) {
                    upd_screen_set_result(i, "x update stopped: no answer.");
                } else if (st == 3 && i >= UPD_WIZ_DIAL) {
                    g_d[i - UPD_WIZ_DIAL].row = DEVUPD_ROW_UPDATING;
                    g_d[i - UPD_WIZ_DIAL].phase = DEVUPD_CONFIRM;
                } else if (st == 4) {
                    upd_screen_set_run(true);
                    g_d[0].row = DEVUPD_ROW_NOT_LINKED;
                    g_d[0].known = false;
                    g_cu.sec = CU_SEC_NO_LINK;
                } else if (st == 5) {
                    tap(upd(), UP_ACT_X, UP_SKIP_Y);   /* next, or end */
                    g_v.have_net = false;
                    g_v.line_capped = true;
                }
                if (st != 1) {
                    hand();
                }
                guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_BODY_H);
                upd_screen_invalidate();
                upd()->render(&c, 0);
                CHECK_EQ(guard_canvas_touched(&g), 0u);
                CHECK_EQ(guard_canvas_unpainted(&g), 0u);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_rows_and_controls_keep_their_rectangles);
    RUN(only_the_current_steps_control_takes_a_press);
    RUN(step_rows_take_no_press);
    RUN(the_download_hold_at_1_99_s_and_2_00_s_requests_once);
    RUN(each_steps_hold_fires_its_own_latch_and_no_other);
    RUN(a_held_steps_grey_hold_says_why_on_the_band);
    RUN(the_hold_slides_off_cancels_and_leaves_unfired);
    RUN(the_step_changing_under_a_hold_ends_it_unfired);
    RUN(a_newer_release_moves_the_current_step_back_to_panel);
    RUN(skip_advances_and_show_skipped_returns);
    RUN(skip_on_the_iomcu_step_hands_over_the_decline_while_an_offer_stands);
    RUN(cancel_update_stands_only_while_a_device_transfer_runs);
    RUN(the_device_step_shows_connecting_the_ask_with_seconds_and_the_share);
    RUN(a_held_c6_retry_keeps_the_failure_reason_on_the_card);
    RUN(a_failed_attempt_shows_its_reason_and_keeps_the_hold);
    RUN(a_failed_download_blocks_nothing_and_check_is_the_way_back);
    RUN(the_end_card_names_skipped_and_absent_steps);
    RUN(no_home_network_shows_the_wifi_button_only_while_no_step_blocks);
    RUN(options_is_grey_while_any_update_or_check_runs);
    RUN(stop_and_armed_during_each_step);
    RUN(a_second_finger_starts_nothing_and_the_first_completes);
    RUN(every_state_renders_inside_its_canvas_in_both_languages);
    return test_summary("upd_wiz_view");
}
