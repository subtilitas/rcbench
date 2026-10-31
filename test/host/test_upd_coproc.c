/*
 * SETUP > UPDATES' IOmcu section: the state coproc_update_section()
 * names, in its order; the line each state draws, in both languages and in
 * two lines of the left column; the hold at 1.99s and 2.00s, slid off,
 * cancelled and left; a link lost under it; a second hold while one runs;
 * the progress the driver opens for it and the way back to UPDATES, after
 * the update, after a START the control task spent and across a snapshot
 * that shows the START taken before the update active; a mismatched
 * IOmcu's build check owed again after an update; and the question's
 * gate on the radio and a panel install.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "coproc_update_screen.h"
#include "link_update_host.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_screen.h"

/* The question's buttons, REMINDER AT START and HOLD 2s: UPDATE IOMCU,
 * in the body's coordinates. */
#include "upd_spots.h"

#define TICK_S     (1.0f / 39.0f)
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

static const cu_version_t k_old = { 0, 17, 0 };
static const cu_version_t k_new = { 0, 18, 0 };

static const ui_screen_t *scr(void) { return coproc_update_screen(); }
static const ui_screen_t *upd(void) { return upd_screen(); }

/* ------------------------------------------------- a store, in memory */

static float g_kept[SETTING_COUNT];
static bool  g_have_kept;

static bool mem_load(float *values, int count)
{
    if (!g_have_kept) {
        return false;
    }
    memcpy(values, g_kept, (size_t)count * sizeof(float));
    return true;
}

static bool mem_save(const float *values, int count)
{
    memcpy(g_kept, values, (size_t)count * sizeof(float));
    g_have_kept = true;
    return true;
}

static const settings_store_t k_store = { mem_load, mem_save, NULL, NULL };

/* ------------------------------------------------------------- touches */

static void scr_ev(const ui_screen_t *s, touch_event_type_t type, uint8_t id,
                   int x, int y, uint8_t flags)
{
    touch_event_t e = { .type = type, .flags = flags,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    s->event(&e);
}

static void scr_ticks(const ui_screen_t *s, int n)
{
    for (int i = 0; i < n; ++i) {
        s->tick(TICK_S);
    }
}

/* The question's own touches. */
static void ev(touch_event_type_t type, uint8_t id, int x, int y,
               uint8_t flags)
{
    scr_ev(scr(), type, id, x, y, flags);
}

static void ticks(int n) { scr_ticks(scr(), n); }

/* UPDATES' own, by the first finger. */
static void upd_ev(touch_event_type_t type, int x, int y)
{
    scr_ev(upd(), type, 1, x, y, 0);
}

/* ---------------------------------------------------- the two screens */

/* UPDATES with a home network kept and nothing asked yet. */
static void upd_fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    g_have_kept = false;
    settings_set_store(&k_store);
    settings_init();
    ui_router_init();
    upd()->reset();
    upd_view_t v;
    memset(&v, 0, sizeof(v));
    v.have_net = true;
    snprintf(v.net, sizeof(v.net), "%s", "workshop");
    snprintf(v.running, sizeof(v.running), "%s", "0.19.0");
    upd_screen_set(&v);
    (void)upd_screen_take();
}

/* And with a newer release found: the download's hold in the third row. */
static void upd_newer(void)
{
    upd_view_t v = *upd_screen_view();
    v.job = UPD_JOB_CHECK;
    v.phase = UPD_PH_ENDED;
    v.got = UPD_GOT_NEWER;
    snprintf(v.found, sizeof(v.found), "%s", "0.20.0");
    upd_screen_set(&v);
}

static cu_driver_t g_drv;
static cu_feed_t   g_feed;

/* The menu, nothing offered, the driver fresh. */
static void drive_fresh(void)
{
    upd_fresh();
    ui_router_goto(SCREEN_OVERVIEW);
    scr()->reset();
    memset(&g_drv, 0, sizeof(g_drv));
    memset(&g_feed, 0, sizeof(g_feed));
    g_feed.running = k_old;
    g_feed.carried = k_new;
}

static cu_hand_t frame(void)
{
    cu_hand_t h;
    coproc_update_drive(&g_drv, &g_feed, false, &h);
    return h;
}

/* An offer, and the frame that opens the question for it. */
static void offer(void)
{
    g_feed.offered = true;
    ++g_feed.offer_gen;
    (void)frame();
}

/* An IOmcu on 0.17.0 that answers and runs another build than the
 * 0.18.0 this panel carries: the section is READY. */
static void feed_ready(void)
{
    g_feed.have_image = true;
    g_feed.reachable = true;
    g_feed.check = CU_CHECK_DIFFERENT;
}

/* UPDATES open on a READY section, as SETUP's tile opens it. */
static void on_updates(void)
{
    drive_fresh();
    feed_ready();
    ui_router_goto(SCREEN_UPDATES);
    (void)frame();
}

/* Hold the section's button for @p s seconds in exact steps of
 * UI_HOLD_MAX_CREDIT_S and less, without letting go. */
static void hold_for(float seconds)
{
    upd_ev(TOUCH_EVENT_DOWN, UP_BTN_X, UP_CU_Y);
    float left = seconds;
    while (left > 0.0f) {
        const float dt = (left > UI_HOLD_MAX_CREDIT_S) ? UI_HOLD_MAX_CREDIT_S
                                                       : left;
        upd()->tick(dt);
        left -= dt;
    }
}

TEST_CASE(the_section_names_the_first_reason_in_its_order)
{
    drive_fresh();
    cu_feed_t f;
    memset(&f, 0, sizeof(f));
    /* All false: no image, whatever else holds. */
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_NO_IMAGE);
    f.active = true;
    f.reachable = true;
    CHECK_EQ(coproc_update_section(&g_drv, &f, true), CU_SEC_NO_IMAGE);
    CHECK_EQ(coproc_update_section(NULL, NULL, false), CU_SEC_NO_IMAGE);

    /* An update, the BUY of an image on trial, a START not yet taken: busy,
     * with or without a link. */
    memset(&f, 0, sizeof(f));
    f.have_image = true;
    f.active = true;
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_BUSY);
    f.active = false;
    f.check = CU_CHECK_UPDATING;
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_BUSY);
    f.check = CU_CHECK_DIFFERENT;
    g_drv.starts_sent = 1u;
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_BUSY);
    f.starts_taken = 1u;
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_NO_LINK);
    /* Without the driver, no START is counted. */
    f.starts_taken = 0u;
    CHECK_EQ(coproc_update_section(NULL, &f, false), CU_SEC_NO_LINK);
    g_drv.starts_sent = 0u;

    /* No link beats every check, and a check beats the gates. */
    static const struct {
        cu_check_t check;
        cu_sec_t   sec;
    } k_by_check[] = {
        { CU_CHECK_NONE,      CU_SEC_UNKNOWN },
        { CU_CHECK_RUNNING,   CU_SEC_CHECKING },
        { CU_CHECK_FAILED,    CU_SEC_FAILED },
        { CU_CHECK_USB,       CU_SEC_USB },
        { CU_CHECK_NO_BOARD,  CU_SEC_NO_BOARD },
        { CU_CHECK_SAME,      CU_SEC_SAME },
        { CU_CHECK_DIFFERENT, CU_SEC_UNSAFE },
        { CU_CHECK_UPDATING,  CU_SEC_BUSY },
        { (cu_check_t)99,     CU_SEC_BUSY },
    };
    for (size_t i = 0; i < sizeof(k_by_check) / sizeof(k_by_check[0]);
         ++i) {
        memset(&f, 0, sizeof(f));
        f.have_image = true;
        f.check = k_by_check[i].check;
        f.radio_up = true;
        f.installing = true;
        const cu_sec_t want_unlinked =
            (k_by_check[i].check == CU_CHECK_UPDATING) ? CU_SEC_BUSY
                                                       : CU_SEC_NO_LINK;
        CHECK_EQ(coproc_update_section(&g_drv, &f, true), want_unlinked);
        f.reachable = true;
        CHECK_EQ(coproc_update_section(&g_drv, &f, true), k_by_check[i].sec);
    }

    /* Another build: the bench, then the radio, then an install. */
    memset(&f, 0, sizeof(f));
    f.have_image = true;
    f.reachable = true;
    f.check = CU_CHECK_DIFFERENT;
    f.radio_up = true;
    f.installing = true;
    CHECK_EQ(coproc_update_section(&g_drv, &f, true), CU_SEC_UNSAFE);
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_RADIO);
    f.radio_up = false;
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_INSTALL);
    f.installing = false;
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_READY);
    /* An older image is READY, as the question offers it, and so is the
     * same number of another build. */
    f.running = k_new;
    f.carried = k_old;
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_READY);
    f.running = k_new;
    f.carried = k_new;
    CHECK_EQ(coproc_update_section(&g_drv, &f, false), CU_SEC_READY);
}

TEST_CASE(the_driver_hands_updates_its_section_every_frame)
{
    on_updates();
    const upd_cu_view_t *v = upd_screen_coproc();
    CHECK_EQ(v->sec, CU_SEC_READY);
    CHECK(v->known);
    CHECK(v->have_image);
    CHECK_EQ(v->running.minor, 17u);
    CHECK_EQ(v->carried.minor, 18u);
    /* No link: the version an IOmcu named is not known any more. */
    g_feed.reachable = false;
    (void)frame();
    CHECK_EQ(v->sec, CU_SEC_NO_LINK);
    CHECK(!v->known);
    /* An IOmcu of another protocol major: reachable, the link down. */
    g_feed.reachable = true;
    g_feed.check = CU_CHECK_NONE;
    (void)frame();
    CHECK_EQ(v->sec, CU_SEC_UNKNOWN);
    CHECK(!v->known);
    /* No image: nothing carried. */
    g_feed.have_image = false;
    (void)frame();
    CHECK_EQ(v->sec, CU_SEC_NO_IMAGE);
    CHECK(!v->have_image);
    /* ARMED. */
    feed_ready();
    cu_hand_t h;
    coproc_update_drive(&g_drv, &g_feed, true, &h);
    CHECK_EQ(v->sec, CU_SEC_UNSAFE);
}

TEST_CASE(the_updates_hold_at_1_99_s_starts_nothing)
{
    on_updates();
    hold_for(1.99f);
    CHECK(!upd_screen_take_coproc());
    upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);
    upd()->tick(UI_HOLD_MAX_CREDIT_S);
    CHECK(!upd_screen_take_coproc());
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
}

TEST_CASE(the_updates_hold_at_2_00_s_starts_once_and_opens_the_progress)
{
    on_updates();
    hold_for(2.00f);
    /* The hold completes in its tick, not on the release. */
    CHECK(upd_screen_take_coproc());
    /* Held on: it fires once. */
    upd()->tick(UI_HOLD_MAX_CREDIT_S);
    upd()->tick(UI_HOLD_MAX_CREDIT_S);
    CHECK(!upd_screen_take_coproc());
    upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);

    /* Again, handed to the driver this time. */
    on_updates();
    hold_for(2.00f);
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_PROGRESS);
    /* The progress carries no band, as the question's does not. */
    CHECK(!ui_router_stop_live());
    /* Taken and not yet running: the progress stays, the section is BUSY. */
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_BUSY);
}

/* A question the fault screen covered: not declined, and UPDATES takes it as
 * any open offer once the link is back; while it is gone, it says so. */
TEST_CASE(an_offer_under_the_fault_screen_stands_for_updates)
{
    drive_fresh();
    feed_ready();
    offer();
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    g_feed.reachable = false;
    ui_router_goto(SCREEN_BUSFAULT);
    cu_hand_t h = frame();
    CHECK(!h.decline);
    ui_router_goto(SCREEN_OVERVIEW);    /* acknowledged */
    upd()->reset();
    ui_router_goto(SCREEN_UPDATES);
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_NO_LINK);

    g_feed.reachable = true;
    (void)frame();
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_READY);
    hold_for(2.00f);
    upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);
    h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
}

TEST_CASE(a_declined_offer_starts_from_updates_and_returns_there)
{
    drive_fresh();
    feed_ready();
    offer();
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    cu_hand_t h = frame();
    CHECK(h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* Declined: the control task takes the offer back, the check stays. */
    g_feed.offered = false;

    upd()->reset();
    ui_router_goto(SCREEN_UPDATES);
    (void)frame();
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_READY);
    hold_for(2.00f);
    upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);
    h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);

    /* Taken, running, its blocks going out. */
    g_feed.active = true;
    g_feed.starts_taken = 1u;
    g_feed.step = CU_STEP_SEND;
    g_feed.blocks = 2994u;
    g_feed.check = CU_CHECK_UPDATING;
    g_feed.reachable = false;
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_BUSY);

    /* Ended: back on UPDATES, which the link-up's check fills again. */
    g_feed.active = false;
    g_feed.check = CU_CHECK_NONE;
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_NO_LINK);
    g_feed.reachable = true;
    g_feed.check = CU_CHECK_SAME;
    g_feed.running = k_new;
    (void)frame();
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_SAME);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);

    /* The question's next update goes back to the menu, not to UPDATES. */
    g_feed.check = CU_CHECK_DIFFERENT;
    g_feed.running = k_old;
    ui_router_goto(SCREEN_OVERVIEW);
    offer();
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    ev(TOUCH_EVENT_UP, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    h = frame();
    CHECK_EQ(h.starts, 1u);
    g_feed.offered = false;
    g_feed.starts_taken = 2u;
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(a_start_from_updates_the_control_task_spends_returns_to_updates)
{
    on_updates();
    hold_for(2.00f);
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    /* Spent -- the radio came up in between -- without an update. */
    g_feed.starts_taken = 1u;
    g_feed.radio_up = true;
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_RADIO);
}

/*
 * The START taken a snapshot before the update shows active: the frame
 * between finds the START spent and goes to UPDATES; the next opens the
 * progress again, and its end still returns to UPDATES, not the menu.
 */
TEST_CASE(a_start_seen_taken_before_active_still_returns_to_updates)
{
    on_updates();
    hold_for(2.00f);
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    g_feed.starts_taken = 1u;          /* taken, not active yet */
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    g_feed.active = true;
    g_feed.check = CU_CHECK_UPDATING;
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    g_feed.active = false;
    g_feed.check = CU_CHECK_NONE;
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
}

/*
 * An IOmcu of another protocol major answers every probe while the
 * link stays down.  Its check is owed once per mismatch, and again after
 * an update that ended with the old image still answering, so UPDATES does
 * not wait for a check that never comes.
 */
TEST_CASE(a_mismatched_coprocessor_is_checked_again_after_an_update)
{
    link_uph_mismatch_t m;
    memset(&m, 0, sizeof(m));
    CHECK(!link_uph_mismatch_answers(&m));
    CHECK(link_uph_mismatch_probe(&m, true));    /* first answer: check */
    CHECK(link_uph_mismatch_answers(&m));
    CHECK(!link_uph_mismatch_probe(&m, true));   /* every later one: none */
    CHECK(!link_uph_mismatch_probe(&m, true));
    /* An update ended; the old image answers the next probe. */
    link_uph_mismatch_rearm(&m);
    CHECK(link_uph_mismatch_answers(&m));        /* still there meanwhile */
    CHECK(link_uph_mismatch_probe(&m, true));
    CHECK(!link_uph_mismatch_probe(&m, true));
    /* Rearmed, then nobody answers: the next answer is news anyway. */
    link_uph_mismatch_rearm(&m);
    CHECK(!link_uph_mismatch_probe(&m, false));
    CHECK(!link_uph_mismatch_answers(&m));
    CHECK(link_uph_mismatch_probe(&m, true));
    CHECK(!link_uph_mismatch_probe(&m, true));
    /* A matching answer ends the mismatch. */
    CHECK(!link_uph_mismatch_probe(&m, false));
    CHECK(link_uph_mismatch_probe(&m, true));
    /* No state: nothing owed, nothing answers. */
    CHECK(!link_uph_mismatch_probe(NULL, true));
    CHECK(!link_uph_mismatch_answers(NULL));
    link_uph_mismatch_rearm(NULL);

    /* UPDATES through it: a mismatched IOmcu's update failed, the
     * check owed again runs and finds the other build: the hold is back. */
    on_updates();
    g_feed.check = CU_CHECK_NONE;      /* the failed update's end */
    (void)frame();
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_UNKNOWN);
    link_uph_mismatch_rearm(&m);
    if (link_uph_mismatch_probe(&m, true)) {
        g_feed.check = CU_CHECK_DIFFERENT;   /* cu_link_up()'s check */
    }
    (void)frame();
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_READY);
}

TEST_CASE(a_link_loss_during_the_hold_ends_it_unfired)
{
    on_updates();
    hold_for(1.75f);
    g_feed.reachable = false;
    (void)frame();
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_NO_LINK);
    /* Held on past 2s: the press was dropped with the state. */
    for (int i = 0; i < 8; ++i) {
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
    }
    CHECK(!upd_screen_take_coproc());
    /* The link back under the same finger: the hold does not resume. */
    g_feed.reachable = true;
    (void)frame();
    for (int i = 0; i < 8; ++i) {
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
    }
    CHECK(!upd_screen_take_coproc());
    upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);

    /* Lost in the frame between the hold's end and the driver: the hold
     * completed, and the driver starts nothing on a section gone. */
    on_updates();
    hold_for(2.00f);
    g_feed.reachable = false;
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK(!upd_screen_take_coproc());
}

TEST_CASE(every_gate_greys_the_hold_and_takes_no_press)
{
    static const struct {
        bool       unsafe, radio, install, reach, image;
        cu_check_t check;
    } k[] = {
        { true,  false, false, true,  true,  CU_CHECK_DIFFERENT },
        { false, true,  false, true,  true,  CU_CHECK_DIFFERENT },
        { false, false, true,  true,  true,  CU_CHECK_DIFFERENT },
        { false, false, false, false, true,  CU_CHECK_DIFFERENT },
        { false, false, false, true,  false, CU_CHECK_DIFFERENT },
        { false, false, false, true,  true,  CU_CHECK_NONE },
        { false, false, false, true,  true,  CU_CHECK_RUNNING },
        { false, false, false, true,  true,  CU_CHECK_SAME },
        { false, false, false, true,  true,  CU_CHECK_USB },
        { false, false, false, true,  true,  CU_CHECK_FAILED },
        { false, false, false, true,  true,  CU_CHECK_UPDATING },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        on_updates();
        g_feed.radio_up = k[i].radio;
        g_feed.installing = k[i].install;
        g_feed.reachable = k[i].reach;
        g_feed.have_image = k[i].image;
        g_feed.check = k[i].check;
        cu_hand_t h;
        coproc_update_drive(&g_drv, &g_feed, k[i].unsafe, &h);
        CHECK(upd_screen_coproc()->sec != CU_SEC_READY);
        hold_for(2.50f);
        upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);
        CHECK(!upd_screen_take_coproc());
        coproc_update_drive(&g_drv, &g_feed, k[i].unsafe, &h);
        CHECK_EQ(h.starts, 0u);
        CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    }
}

TEST_CASE(a_second_hold_while_one_runs_starts_nothing)
{
    on_updates();
    hold_for(2.00f);
    upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 1u);
    /* Not yet taken: an alert's jump back to UPDATES finds the hold grey. */
    ui_router_goto(SCREEN_UPDATES);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_BUSY);
    hold_for(2.50f);
    upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);
    CHECK(!upd_screen_take_coproc());
    h = frame();
    CHECK_EQ(h.starts, 0u);
    /* UPDATES stays while the START waits to be taken, and the update's
     * start takes the screen. */
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    g_feed.active = true;
    g_feed.starts_taken = 1u;
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    /* Running, an alert's jump to UPDATES: grey, and undone next frame. */
    ui_router_goto(SCREEN_UPDATES);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_BUSY);
    hold_for(2.50f);
    CHECK(!upd_screen_take_coproc());
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
}

TEST_CASE(the_question_waits_for_the_radio_and_an_install)
{
    drive_fresh();
    g_feed.offered = true;
    g_feed.offer_gen = 1u;
    g_feed.radio_up = true;
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    g_feed.radio_up = false;
    g_feed.installing = true;
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    g_feed.installing = false;
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_QUESTION);
}

static upd_cu_view_t cu_view(cu_sec_t sec)
{
    upd_cu_view_t v = {
        .sec = sec, .known = true, .have_image = true,
        .running = { 0, 17, 0 }, .carried = { 0, 18, 0 },
    };
    return v;
}

/* Every state has its line, in both languages, and only READY takes the
 * hold; the line says why in the column's 360px, two lines at most. */
TEST_CASE(every_section_state_says_why_in_two_lines)
{
    const gfx_rect_t line = gfx_rect_make(16, 0, 360, 18);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int sec = CU_SEC_NO_IMAGE; sec <= CU_SEC_READY; ++sec) {
            static const cu_version_t k_run[] = {
                { 0, 17, 0 }, { 0, 19, 0 }, { 0, 18, 0 },
            };
            for (size_t r = 0; r < sizeof(k_run) / sizeof(k_run[0]); ++r) {
                upd_cu_view_t v = cu_view((cu_sec_t)sec);
                v.running = k_run[r];
                const char *t = NULL;
                gfx_color_t col = 0;
                upd_cu_text(&v, &t, &col);
                CHECK(t != NULL);
                CHECK(t[0] != '\0');
                const int n = ui_text_wrap(NULL, line, 20, t, UI_FONT_LABEL,
                                           UI_TEXT);
                if (n < 1 || n > 2) {
                    T_FAIL("language %d: section %d takes %d lines: \"%s\"",
                           lang, sec, n, t);
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);

    /* READY names an older image as the question does, and the same
     * number of another build. */
    const char *t = NULL;
    gfx_color_t col = 0;
    upd_cu_view_t v = cu_view(CU_SEC_READY);
    upd_cu_text(&v, &t, &col);
    CHECK_STR_EQ(t, TR(CU_Q_WHAT));
    v.running = (cu_version_t){ 0, 19, 0 };
    upd_cu_text(&v, &t, &col);
    CHECK_STR_EQ(t, TR(CU_Q_OLDER));
    CHECK_EQ(col, UI_WARN);
    v.running = v.carried;
    upd_cu_text(&v, &t, &col);
    CHECK_STR_EQ(t, TR(CU_Q_SAME));
    v = cu_view(CU_SEC_UNSAFE);
    upd_cu_text(&v, &t, &col);
    CHECK_STR_EQ(t, TR(UPD_CU_UNSAFE));
    CHECK_EQ(col, UI_WARN);
    v = cu_view(CU_SEC_USB);
    upd_cu_text(&v, &t, &col);
    CHECK_STR_EQ(t, TR(UPD_CU_USB));
    v = cu_view(CU_SEC_BUSY);
    upd_cu_text(&v, &t, &col);
    CHECK_STR_EQ(t, TR(UPD_WAIT_COPROC));
}

TEST_CASE(only_a_ready_section_takes_the_hold)
{
    for (int sec = CU_SEC_NO_IMAGE; sec <= CU_SEC_READY; ++sec) {
        upd_fresh();
        const upd_cu_view_t v = cu_view((cu_sec_t)sec);
        upd_screen_set_coproc(&v);
        scr_ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_CU_Y, 0);
        scr_ticks(upd(), HOLD_TICKS);
        scr_ev(upd(), TOUCH_EVENT_UP, 1, UP_BTN_X, UP_CU_Y, 0);
        CHECK_EQ(upd_screen_take_coproc(), sec == CU_SEC_READY);
        CHECK(!upd_screen_take_coproc());
        /* And asks for nothing else. */
        CHECK_EQ(upd_screen_take(), 0u);
    }
}

TEST_CASE(the_section_hold_slides_off_cancels_and_leaves_unfired)
{
    const upd_cu_view_t v = cu_view(CU_SEC_READY);
    /* Slid off the button. */
    upd_fresh();
    upd_screen_set_coproc(&v);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_CU_Y, 0);
    scr_ticks(upd(), HOLD_TICKS / 2);
    scr_ev(upd(), TOUCH_EVENT_MOVE, 1, UP_BTN_X, UP_CU_Y - 60, 0);
    scr_ticks(upd(), HOLD_TICKS);
    CHECK(!upd_screen_take_coproc());
    /* Cancelled by the router. */
    upd_fresh();
    upd_screen_set_coproc(&v);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_CU_Y, 0);
    scr_ticks(upd(), HOLD_TICKS / 2);
    upd()->cancel();
    scr_ticks(upd(), HOLD_TICKS);
    CHECK(!upd_screen_take_coproc());
    /* The screen left. */
    upd_fresh();
    upd_screen_set_coproc(&v);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_CU_Y, 0);
    scr_ticks(upd(), HOLD_TICKS / 2);
    upd()->leave();
    upd()->enter();
    scr_ticks(upd(), HOLD_TICKS);
    CHECK(!upd_screen_take_coproc());
    /* A second finger elsewhere does not end the first one's hold. */
    upd_fresh();
    upd_screen_set_coproc(&v);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_CU_Y, 0);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 2, UP_BTN_X, UP_REMIND_Y, 0);
    scr_ev(upd(), TOUCH_EVENT_UP, 2, UP_BTN_X, UP_REMIND_Y, 0);
    scr_ticks(upd(), HOLD_TICKS);
    CHECK(upd_screen_take_coproc());
}

TEST_CASE(the_wifi_hint_names_logs)
{
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(TR(UPD_KEEP_NET), "Save a network under LOGS > WI-FI.");
    ui_text_set_language(UI_LANG_DE);
    CHECK(strstr(TR(UPD_KEEP_NET), "LOGS > WI-FI") != NULL);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(every_section_state_renders_inside_its_canvas)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(UI_PANEL_W, UI_BODY_H)];
    gfx_canvas_t c;
    guard_canvas_t g;
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (int sec = CU_SEC_NO_IMAGE; sec <= CU_SEC_READY; ++sec) {
            for (int newer = 0; newer < 2; ++newer) {
                upd_fresh();
                if (newer != 0) {
                    upd_newer();
                }
                upd_cu_view_t v = cu_view((cu_sec_t)sec);
                v.known = (sec % 2 == 0);
                v.running = (cu_version_t){ 65535, 65535, 65535 };
                upd_screen_set_coproc(&v);
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
    RUN(the_section_names_the_first_reason_in_its_order);
    RUN(the_driver_hands_updates_its_section_every_frame);
    RUN(the_updates_hold_at_1_99_s_starts_nothing);
    RUN(the_updates_hold_at_2_00_s_starts_once_and_opens_the_progress);
    RUN(an_offer_under_the_fault_screen_stands_for_updates);
    RUN(a_declined_offer_starts_from_updates_and_returns_there);
    RUN(a_start_from_updates_the_control_task_spends_returns_to_updates);
    RUN(a_start_seen_taken_before_active_still_returns_to_updates);
    RUN(a_mismatched_coprocessor_is_checked_again_after_an_update);
    RUN(a_link_loss_during_the_hold_ends_it_unfired);
    RUN(every_gate_greys_the_hold_and_takes_no_press);
    RUN(a_second_hold_while_one_runs_starts_nothing);
    RUN(the_question_waits_for_the_radio_and_an_install);
    RUN(every_section_state_says_why_in_two_lines);
    RUN(only_a_ready_section_takes_the_hold);
    RUN(the_section_hold_slides_off_cancels_and_leaves_unfired);
    RUN(the_wifi_hint_names_logs);
    RUN(every_section_state_renders_inside_its_canvas);
    return test_summary("upd_coproc");
}
