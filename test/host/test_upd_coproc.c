/*
 * The IOmcu's update on SETUP > UPDATES: an offer opens UPDATES at step 2
 * (IOMCU) once per offer, only on the menu of a SAFE bench with the radio
 * down and no panel install; SKIP THIS STEP and the home tag decline the
 * offer UPDATES showed and no other; a replaced offer takes the new
 * versions and ends a hold under way; an offer under the fault screen
 * stands; a START the control task spends leaves step 2 open; an IOmcu of
 * another protocol major is offered as any other.  And SINGLE UPDATES'
 * IOmcu section, each case opening the page from OPTIONS first: the state
 * coproc_update_section() names, in its order; the line each state draws,
 * in both languages and in two lines of the left column; the hold at 1.99s
 * and 2.00s, slid off, cancelled and left; a link lost under it; a second
 * hold while one runs; the progress the driver opens for it and the way
 * back to UPDATES, after the update and across a snapshot that shows the
 * START taken before the update active; a mismatched IOmcu's build check
 * owed again after an update.
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
#include "upd_wiz_view.h"
#include "upd_wizard.h"

/* UPDATES' step control and SKIP, REMINDER AT START and HOLD 2s: UPDATE
 * IOMCU, in the body's coordinates. */
#include "upd_spots.h"

#define TICK_S     (1.0f / 39.0f)
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

static const cu_version_t k_old = { 0, 17, 0 };
static const cu_version_t k_new = { 0, 18, 0 };

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
    ui_router_set_alert(NULL);
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

/* An offer, and the frame that opens UPDATES for it. */
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

/* SINGLE UPDATES open on a READY section: SETUP's tile opens UPDATES,
 * OPTIONS' row Single updates the page. */
static void on_updates(void)
{
    drive_fresh();
    feed_ready();
    ui_router_goto(SCREEN_UPDATES);
    upd_open_single(upd());
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

/* Hold the current step's control on UPDATES for @p seconds, as hold_for()
 * does on SINGLE UPDATES. */
static void step_hold_for(float seconds)
{
    upd_ev(TOUCH_EVENT_DOWN, UP_ACT_X, UP_ACT_Y);
    float left = seconds;
    while (left > 0.0f) {
        const float dt = (left > UI_HOLD_MAX_CREDIT_S) ? UI_HOLD_MAX_CREDIT_S
                                                       : left;
        upd()->tick(dt);
        left -= dt;
    }
}

/* SKIP THIS STEP, tapped. */
static void skip_tap(void)
{
    upd_ev(TOUCH_EVENT_DOWN, UP_ACT_X, UP_SKIP_Y);
    upd_ev(TOUCH_EVENT_UP, UP_ACT_X, UP_SKIP_Y);
}

/* The home tag of UPDATES, tapped through the router. */
static void home_tag(void)
{
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_UPDATES));
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1,
                                   .x = (int16_t)(home.x + home.w / 2),
                                   .y = (int16_t)(home.y + home.h / 2),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/* The steps now. */
static upd_wiz_t wiz(void)
{
    upd_wiz_t w;
    upd_wiz_now(&w);
    return w;
}

/* UPDATES shows, its IOmcu step current with its hold live. */
static void check_on_step_2(void)
{
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    const upd_wiz_t w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_IOMCU);
    CHECK_EQ(w.act, UPD_WIZ_ACT_IOMCU);
    CHECK(w.act_live);
}

static bool band_says(ui_text_id_t id)
{
    const char *a = ui_router_alert();
    return a != NULL && strcmp(a, ui_tr(id)) == 0;
}

/* ------------------------------------------------- the link-up's offer */

/* The offer opens UPDATES at step 2, says so on the band, and does it once:
 * back on the menu by any way but a decline, the same offer opens nothing;
 * the next link-up's offer opens it again. */
TEST_CASE(an_offer_opens_updates_at_the_iomcu_step_once_per_offer)
{
    drive_fresh();
    feed_ready();
    CHECK(!coproc_update_opened());
    offer();
    check_on_step_2();
    CHECK(band_says(TX_ALERT_CU_OFFER));
    CHECK(coproc_update_opened());
    CHECK_EQ(g_drv.shown_gen, g_feed.offer_gen);

    ui_router_goto(SCREEN_OVERVIEW);
    for (int i = 0; i < 5; ++i) {
        const cu_hand_t h = frame();
        CHECK(!h.decline);
        CHECK_EQ(h.starts, 0u);
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    }
    /* The next link-up: withdrawn, then made anew. */
    g_feed.offered = false;
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    offer();
    check_on_step_2();

    /* An offer that comes while UPDATES shows is shown there: no line, and
     * it opens nothing later. */
    drive_fresh();
    feed_ready();
    ui_router_goto(SCREEN_UPDATES);
    offer();
    check_on_step_2();
    CHECK(ui_router_alert() == NULL);
    CHECK(!coproc_update_opened());
    ui_router_goto(SCREEN_OVERVIEW);
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* Each condition alone holds the opening back and names itself on the
 * band; it opens the frame the last one goes.  ARMED, UPDATES opened by
 * hand shows step 2 held: the grey hold starts nothing. */
TEST_CASE(it_opens_only_on_overview_safe_radio_down_no_install)
{
    static const ui_text_id_t k_line[] = {
        TX_ALERT_CU_OFFER_SAFE, TX_ALERT_CU_OFFER_WIFI,
        TX_ALERT_CU_OFFER_INSTALL,
    };
    for (int k = 0; k < 5; ++k) {
        drive_fresh();
        feed_ready();
        bool unsafe = false;
        if (k == 0) {
            unsafe = true;
        } else if (k == 1) {
            g_feed.radio_up = true;
        } else if (k == 2) {
            g_feed.installing = true;
        } else if (k == 3) {
            ui_router_goto(SCREEN_MOTOR);
        } else {
            ui_router_init();            /* the splash */
        }
        g_feed.offered = true;
        g_feed.offer_gen = 1u;
        for (int i = 0; i < 3; ++i) {
            cu_hand_t h;
            coproc_update_drive(&g_drv, &g_feed, unsafe, &h);
            CHECK(!h.decline);
            CHECK(ui_router_current() != SCREEN_UPDATES);
        }
        if (k < 3) {
            CHECK(band_says(k_line[k]));
        } else if (k == 3) {
            CHECK(band_says(TX_ALERT_CU_OFFER));
        } else {
            CHECK(ui_router_alert() == NULL);   /* the splash: no band */
        }
        g_feed.radio_up = false;
        g_feed.installing = false;
        if (k >= 3) {
            ui_router_goto(SCREEN_OVERVIEW);
        }
        (void)frame();
        check_on_step_2();
    }

    /* ARMED, opened by hand: step 2 held, its hold grey. */
    drive_fresh();
    feed_ready();
    g_feed.offered = true;
    g_feed.offer_gen = 1u;
    ui_router_goto(SCREEN_UPDATES);
    cu_hand_t h;
    coproc_update_drive(&g_drv, &g_feed, true, &h);
    upd_wiz_t w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_IOMCU);
    CHECK_EQ(w.step[UPD_WIZ_IOMCU].state, UPD_WIZ_HELD);
    CHECK(!w.act_live);
    step_hold_for(2.50f);
    upd_ev(TOUCH_EVENT_UP, UP_ACT_X, UP_ACT_Y);
    CHECK(!upd_screen_take_coproc());
    coproc_update_drive(&g_drv, &g_feed, true, &h);
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
}

/* SKIP declines the offer UPDATES showed, once; a SKIP given just before a
 * newer link-up's offer replaced it declines nothing, and that offer brings
 * the step back.  A second offer after a decline opens UPDATES at step 2
 * again.  Without an offer SKIP declines nothing. */
TEST_CASE(skip_declines_the_offer_it_was_shown_for_and_no_other)
{
    drive_fresh();
    feed_ready();
    offer();
    skip_tap();
    ++g_feed.offer_gen;                 /* replaced between two frames */
    cu_hand_t h = frame();
    CHECK(!h.decline);
    CHECK_EQ(g_drv.shown_gen, g_feed.offer_gen);
    check_on_step_2();                  /* the new offer, not skipped */

    skip_tap();
    h = frame();
    CHECK(h.decline);
    h = frame();
    CHECK(!h.decline);                  /* once */
    CHECK(wiz().current != (unsigned)UPD_WIZ_IOMCU);
    CHECK_EQ(wiz().step[UPD_WIZ_IOMCU].state, UPD_WIZ_SKIPPED);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);

    /* Declined: the control task takes the offer back. */
    g_feed.offered = false;
    (void)frame();
    /* A second offer, another link-up later: UPDATES opens at step 2. */
    ui_router_goto(SCREEN_OVERVIEW);
    offer();
    check_on_step_2();

    /* No offer stands: SKIP passes the step and declines nothing. */
    g_feed.offered = false;
    (void)frame();
    skip_tap();
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(wiz().step[UPD_WIZ_IOMCU].state, UPD_WIZ_SKIPPED);
}

/* The home tag that leaves UPDATES declines the offer it showed, as CANCEL
 * on the question did; over SINGLE UPDATES it closes the page and declines
 * nothing; given just before a newer offer replaced it, it declines
 * nothing and the new offer opens UPDATES. */
TEST_CASE(the_home_tag_on_updates_declines_as_cancel_did)
{
    drive_fresh();
    feed_ready();
    offer();
    home_tag();
    CHECK(ui_router_current() != SCREEN_UPDATES);
    cu_hand_t h = frame();
    CHECK(h.decline);
    h = frame();
    CHECK(!h.decline);
    CHECK(ui_router_current() != SCREEN_UPDATES);   /* not reopened */

    drive_fresh();
    feed_ready();
    offer();
    upd_open_single(upd());
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK(!upd()->covered());
    h = frame();
    CHECK(!h.decline);

    drive_fresh();
    feed_ready();
    offer();
    home_tag();
    ++g_feed.offer_gen;
    ui_router_goto(SCREEN_OVERVIEW);
    h = frame();
    CHECK(!h.decline);
    check_on_step_2();
    CHECK_EQ(g_drv.shown_gen, g_feed.offer_gen);
}

/* A newer link-up's offer while UPDATES shows: the step names the new
 * versions, and a hold under way on it ends unfired; the new offer's hold
 * takes the whole 2s. */
TEST_CASE(a_replaced_offer_updates_the_versions_and_clears_a_hold_in_progress)
{
    drive_fresh();
    feed_ready();
    offer();
    step_hold_for(1.50f);
    g_feed.running = (cu_version_t){ 0, 16, 3 };
    ++g_feed.offer_gen;
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(upd_screen_coproc()->running.minor, 16u);
    CHECK_EQ(upd_screen_coproc()->running.patch, 3u);
    for (int i = 0; i < 8; ++i) {
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
    }
    upd_ev(TOUCH_EVENT_UP, UP_ACT_X, UP_ACT_Y);
    CHECK(!upd_screen_take_coproc());
    h = frame();
    CHECK_EQ(h.starts, 0u);
    check_on_step_2();

    step_hold_for(1.99f);
    CHECK(!upd_screen_take_coproc());
    upd()->tick(0.01f);
    upd_ev(TOUCH_EVENT_UP, UP_ACT_X, UP_ACT_Y);
    h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
}

/* UPDATES the fault screen covered: not declined, not opened again for the
 * same offer after the acknowledgement; opened by hand, step 2 takes the
 * update once the link is back. */
TEST_CASE(an_offer_under_the_fault_screen_stands)
{
    drive_fresh();
    feed_ready();
    offer();
    check_on_step_2();
    g_feed.reachable = false;
    ui_router_goto(SCREEN_BUSFAULT);
    cu_hand_t h = frame();
    CHECK(!h.decline);
    ui_router_goto(SCREEN_OVERVIEW);    /* acknowledged */
    for (int i = 0; i < 3; ++i) {
        h = frame();
        CHECK(!h.decline);
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    }
    ui_router_goto(SCREEN_UPDATES);
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_NO_LINK);
    CHECK(!wiz().act_live);

    g_feed.reachable = true;
    (void)frame();
    check_on_step_2();
    step_hold_for(2.00f);
    upd_ev(TOUCH_EVENT_UP, UP_ACT_X, UP_ACT_Y);
    h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
}

/* A START the control task spends -- the radio came up in between -- and
 * the offer it makes again: back on UPDATES, step 2 held by the radio,
 * then open again; UPDATES is not opened a second time. */
TEST_CASE(a_start_the_control_task_spends_leaves_step_2_open)
{
    drive_fresh();
    feed_ready();
    offer();
    step_hold_for(2.00f);
    upd_ev(TOUCH_EVENT_UP, UP_ACT_X, UP_ACT_Y);
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    g_feed.starts_taken = 1u;
    g_feed.radio_up = true;
    ++g_feed.offer_gen;                 /* the offer made again */
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_RADIO);
    upd_wiz_t w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_IOMCU);
    CHECK_EQ(w.step[UPD_WIZ_IOMCU].state, UPD_WIZ_HELD);
    g_feed.radio_up = false;
    (void)frame();
    check_on_step_2();
    CHECK_EQ(g_drv.shown_gen, g_feed.offer_gen);
}

/* An IOmcu of another protocol major answers its identity while the link
 * stays down: its offer opens UPDATES at step 2 and its hold starts the
 * update, as for any other build. */
TEST_CASE(a_mismatched_protocol_major_is_offered_as_before)
{
    drive_fresh();
    g_feed.have_image = true;
    g_feed.reachable = true;            /* the identity, no link-up */
    g_feed.check = CU_CHECK_DIFFERENT;
    g_feed.running = (cu_version_t){ 1, 0, 0 };
    offer();
    check_on_step_2();
    CHECK_EQ(upd_screen_coproc()->running.major, 1u);
    step_hold_for(2.00f);
    upd_ev(TOUCH_EVENT_UP, UP_ACT_X, UP_ACT_Y);
    const cu_hand_t h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
}

/* ------------------------------------------------ SINGLE UPDATES' section */

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
    /* The progress carries no band. */
    CHECK(!ui_router_stop_live());
    /* Taken and not yet running: the progress stays, the section is BUSY. */
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_BUSY);
}

/* A declined offer still starts from SINGLE UPDATES; the progress returns
 * to UPDATES, which the link-up's check fills again. */
TEST_CASE(a_declined_offer_starts_from_single_updates_and_returns_there)
{
    drive_fresh();
    feed_ready();
    offer();
    skip_tap();
    cu_hand_t h = frame();
    CHECK(h.decline);
    /* Declined: the control task takes the offer back, the check stays. */
    g_feed.offered = false;

    upd_open_single(upd());
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
    CHECK_EQ(wiz().step[UPD_WIZ_IOMCU].state, UPD_WIZ_DONE);
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
    upd_open_single(upd());
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
    upd_open_single(upd());
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_BUSY);
    hold_for(2.50f);
    CHECK(!upd_screen_take_coproc());
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
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

    /* READY names an older image, and the same number of another
     * build. */
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
        upd_open_single(upd());
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
    upd_open_single(upd());
    upd_screen_set_coproc(&v);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_CU_Y, 0);
    scr_ticks(upd(), HOLD_TICKS / 2);
    scr_ev(upd(), TOUCH_EVENT_MOVE, 1, UP_BTN_X, UP_CU_Y - 60, 0);
    scr_ticks(upd(), HOLD_TICKS);
    CHECK(!upd_screen_take_coproc());
    /* Cancelled by the router. */
    upd_fresh();
    upd_open_single(upd());
    upd_screen_set_coproc(&v);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_CU_Y, 0);
    scr_ticks(upd(), HOLD_TICKS / 2);
    upd()->cancel();
    scr_ticks(upd(), HOLD_TICKS);
    CHECK(!upd_screen_take_coproc());
    /* The screen left. */
    upd_fresh();
    upd_open_single(upd());
    upd_screen_set_coproc(&v);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_CU_Y, 0);
    scr_ticks(upd(), HOLD_TICKS / 2);
    upd()->leave();
    upd()->enter();
    scr_ticks(upd(), HOLD_TICKS);
    CHECK(!upd_screen_take_coproc());
    /* A second finger on BACK neither ends the first one's hold nor closes
     * the page. */
    upd_fresh();
    upd_open_single(upd());
    upd_screen_set_coproc(&v);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_CU_Y, 0);
    scr_ev(upd(), TOUCH_EVENT_DOWN, 2, UP_SET_X, UP_SET_Y, 0);
    scr_ev(upd(), TOUCH_EVENT_UP, 2, UP_SET_X, UP_SET_Y, 0);
    scr_ticks(upd(), HOLD_TICKS);
    CHECK(upd_screen_take_coproc());
    CHECK(upd()->covered());
}

TEST_CASE(the_wifi_hint_names_one_tile)
{
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(TR(UPD_KEEP_NET), "Save a network on WI-FI.");
    ui_text_set_language(UI_LANG_DE);
    CHECK(strstr(TR(UPD_KEEP_NET), "WI-FI") != NULL);
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
                /* UPDATES, then SINGLE UPDATES over it. */
                for (int page = 0; page < 2; ++page) {
                    if (page == 1) {
                        upd_open_single(upd());
                    }
                    guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_BODY_H);
                    upd_screen_invalidate();
                    upd()->render(&c, 0);
                    CHECK_EQ(guard_canvas_touched(&g), 0u);
                    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The grey hold says why, on the band: a hold of 2.3s on the same build
 * starts nothing and gives the section's line, as the C6 and device rows
 * give theirs; a tap with no IOmcu answering gives that line.
 */
TEST_CASE(the_grey_hold_says_why_on_the_band)
{
    on_updates();
    g_feed.check = CU_CHECK_SAME;
    g_feed.running = k_new;
    (void)frame();
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_SAME);
    hold_for(2.30f);
    upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    const char *a = ui_router_alert();
    CHECK(a != NULL && strcmp(a, TR(UPD_CU_SAME)) == 0);

    g_feed.reachable = false;
    (void)frame();
    CHECK_EQ(upd_screen_coproc()->sec, CU_SEC_NO_LINK);
    upd_ev(TOUCH_EVENT_DOWN, UP_BTN_X, UP_CU_Y);
    upd_ev(TOUCH_EVENT_UP, UP_BTN_X, UP_CU_Y);
    a = ui_router_alert();
    CHECK(a != NULL && strcmp(a, TR(UPD_CU_NO_LINK)) == 0);
}

int main(void)
{
    RUN(an_offer_opens_updates_at_the_iomcu_step_once_per_offer);
    RUN(it_opens_only_on_overview_safe_radio_down_no_install);
    RUN(skip_declines_the_offer_it_was_shown_for_and_no_other);
    RUN(the_home_tag_on_updates_declines_as_cancel_did);
    RUN(a_replaced_offer_updates_the_versions_and_clears_a_hold_in_progress);
    RUN(an_offer_under_the_fault_screen_stands);
    RUN(a_start_the_control_task_spends_leaves_step_2_open);
    RUN(a_mismatched_protocol_major_is_offered_as_before);
    RUN(the_section_names_the_first_reason_in_its_order);
    RUN(the_driver_hands_updates_its_section_every_frame);
    RUN(the_updates_hold_at_1_99_s_starts_nothing);
    RUN(the_updates_hold_at_2_00_s_starts_once_and_opens_the_progress);
    RUN(a_declined_offer_starts_from_single_updates_and_returns_there);
    RUN(a_start_seen_taken_before_active_still_returns_to_updates);
    RUN(a_mismatched_coprocessor_is_checked_again_after_an_update);
    RUN(a_link_loss_during_the_hold_ends_it_unfired);
    RUN(every_gate_greys_the_hold_and_takes_no_press);
    RUN(a_second_hold_while_one_runs_starts_nothing);
    RUN(every_section_state_says_why_in_two_lines);
    RUN(only_a_ready_section_takes_the_hold);
    RUN(the_section_hold_slides_off_cancels_and_leaves_unfired);
    RUN(the_wifi_hint_names_one_tile);
    RUN(every_section_state_renders_inside_its_canvas);
    RUN(the_grey_hold_says_why_on_the_band);
    return test_summary("upd_coproc");
}

