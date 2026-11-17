/*
 * The update run across its two restarts (upd_run.c over fw_run.c): the
 * run note in a store in memory that survives each simulated start, the
 * driver and UPDATES' steps fed as the panel feeds them once a frame.
 *
 * The failures under test: UPDATES opening on a screen the operator is
 * using, while ARMED, with the supply on, the radio up or an install under
 * way, or twice in one start; a run that outlives the home tag, a
 * rollback or its two openings (a restart loop); a run that ends before
 * the targets have reported, or under the C6's restart and erases the note
 * that restart needs; a screen forced over UPDATES taken for the
 * operator's "not now"; the start-up check switching the radio on during
 * a run; a waiting step that does not advance when its device links; the
 * run and an IOmcu offer opening UPDATES twice, or both speaking on the
 * band in one frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include "upd_rig.h"

#include "coproc_update_screen.h"
#include "fw_install.h"
#include "fw_run.h"
#include "ui_band.h"
#include "upd_dev.h"
#include "upd_run.h"
#include "upd_wiz_view.h"
#include "upd_wizard.h"

/* ----------------------------------------------------- the note's store */

/* The run note's store: NVS on the panel, memory here; it outlives a start. */
typedef struct {
    uint8_t  buf[64];
    uint32_t len;          /* 0: no entry */
    unsigned saves, erases;
} store_t;

static store_t g_store;

static uint32_t s_load(void *ctx, uint8_t *buf, uint32_t cap)
{
    const store_t *s = ctx;
    if (s->len > 0u && s->len <= cap) {
        memcpy(buf, s->buf, s->len);
    }
    return s->len;
}

static bool s_save(void *ctx, const uint8_t *buf, uint32_t len)
{
    store_t *s = ctx;
    ++s->saves;
    if (len > sizeof(s->buf)) {
        return false;
    }
    memcpy(s->buf, buf, len);
    s->len = len;
    return true;
}

static bool s_erase(void *ctx)
{
    store_t *s = ctx;
    ++s->erases;
    s->len = 0u;
    return true;
}

static const fw_run_ops_t k_ops = { s_load, s_save, s_erase, &g_store };

/* -------------------------------------------------------------- a start */

static fw_run_t       g_run;    /* fw_ota.c's s_run */
static upd_run_t      g_drv;    /* panel_updrun.c's driver */
static upd_run_feed_t g_feed;   /* what the panel knows this frame */
static upd_cu_view_t  g_cu;
static upd_c6_view_t  g_c6;
static upd_dev_view_t g_d[UPD_DEV_COUNT];

/* The build that runs after a panel install, and the one before it. */
#define K_NEW_TEXT "0.23.15"
#define K_OLD_TEXT "0.23.14"

/* Every step on the carried version; the nodes not bound. */
static void views_current(void)
{
    base_view();
    snprintf(g_v.running, sizeof(g_v.running), "%s", K_NEW_TEXT);
    g_cu = (upd_cu_view_t){ .sec = CU_SEC_SAME, .known = true,
                            .have_image = true, .running = { 0, 23, 15 },
                            .carried = { 0, 23, 15 } };
    g_c6 = (upd_c6_view_t){ .row = FW_C6_ROW_REINSTALL, .known = true,
                            .have_image = true, .running = { 2u, 13u, 0u },
                            .carried = { 2u, 13u, 0u } };
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        memset(&g_d[i], 0, sizeof(g_d[i]));
        g_d[i].row = DEVUPD_ROW_NOT_LINKED;
        g_d[i].have_image = true;
        g_d[i].carried = (fw_version_t){ 0u, 23u, 15u };
    }
    g_d[UPD_DEV_DIAL].row = DEVUPD_ROW_REINSTALL;
    g_d[UPD_DEV_DIAL].bound = true;
    g_d[UPD_DEV_DIAL].known = true;
    g_d[UPD_DEV_DIAL].running = (fw_version_t){ 0u, 23u, 15u };
}

/* The IOmcu on the build before: its check said DIFFERENT. */
static void iomcu_behind(void)
{
    g_cu.sec = CU_SEC_READY;
    g_cu.running = (cu_version_t){ 0, 23, 14 };
}

/* The Dial linked on the build before. */
static void dial_behind(void)
{
    g_d[UPD_DEV_DIAL].row = DEVUPD_ROW_READY;
    g_d[UPD_DEV_DIAL].known = true;
    g_d[UPD_DEV_DIAL].running = (fw_version_t){ 0u, 23u, 14u };
}

/* The Dial bound and not connected yet. */
static void dial_away(void)
{
    g_d[UPD_DEV_DIAL].row = DEVUPD_ROW_NOT_LINKED;
    g_d[UPD_DEV_DIAL].known = false;
    g_d[UPD_DEV_DIAL].running = (fw_version_t){ 0u, 0u, 0u };
}

/* A start of @p running: the note read once, the screens fresh on the
 * splash, the driver zeroed.  The views are the caller's. */
static fw_run_seen_t boot(const char *running)
{
    memset(&g_run, 0, sizeof(g_run));
    const fw_run_seen_t seen = fw_run_settle(&k_ops, &g_run, running);
    memset(&g_drv, 0, sizeof(g_drv));
    fresh();
    ui_router_set_alert(NULL);
    memset(&g_feed, 0, sizeof(g_feed));
    g_feed.known = true;
    return seen;
}

/* One frame: the run flag and the views handed over, the driver asked,
 * and an end ended as panel_updrun.c ends it. */
static upd_run_end_t frame(void)
{
    g_feed.run = g_run.active;
    upd_screen_set_run(g_run.active);
    push();
    upd_screen_set_coproc(&g_cu);
    upd_c6_set(&g_c6);
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        upd_dev_set((upd_dev_id_t)i, &g_d[i]);
    }
    const upd_run_end_t e = upd_run_drive(&g_drv, &g_feed);
    if (e != UPD_RUN_GOES_ON) {
        (void)fw_run_end(&k_ops, &g_run);
    }
    return e;
}

static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        (void)frame();
    }
}

/* The splash has ended: the menu. */
static void past_splash(void) { ui_router_goto(SCREEN_OVERVIEW); }

/* The install's last step, written whole and checked, as fw_install_run(). */
static void install(const char *version)
{
    CHECK(fw_run_after_install(&k_ops, &g_run, FW_INSTALL_DONE, version));
}

static upd_wiz_t wiz(void)
{
    upd_wiz_t w;
    upd_wiz_now(&w);
    return w;
}

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

static bool band_says(ui_text_id_t id)
{
    const char *a = ui_router_alert();
    return a != NULL && strcmp(a, ui_tr(id)) == 0;
}

static void home_tag(void)
{
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_UPDATES));
    router_tap(home.x + home.w / 2, home.y + home.h / 2);
}

/* A start after the panel's install, on the menu, the IOmcu behind. */
static void after_install(void)
{
    store_t none = { 0 };
    g_store = none;
    install(K_NEW_TEXT);
    CHECK_EQ(boot(K_NEW_TEXT), FW_RUN_STANDS);
    views_current();
    iomcu_behind();
    past_splash();
}

/* ------------------------------------------------------------ the cases */

TEST_CASE(a_run_opens_updates_once_on_overview_and_never_on_another_screen)
{
    static const ui_screen_id_t k_other[] = {
        SCREEN_SETUP, SCREEN_MOTOR, SCREEN_SUPPLY, SCREEN_LOGS, SCREEN_WIFI,
    };
    for (size_t i = 0; i < sizeof(k_other) / sizeof(k_other[0]); ++i) {
        after_install();
        ui_router_goto(k_other[i]);
        frames(5);
        CHECK_EQ(ui_router_current(), k_other[i]);
        CHECK(!g_drv.opened);
    }
    after_install();
    CHECK_EQ(frame(), UPD_RUN_GOES_ON);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
    /* Once per start: back on the menu by another way, it stays there. */
    ui_router_goto(SCREEN_OVERVIEW);
    frames(5);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(g_run.active);
}

TEST_CASE(it_waits_for_safe_the_supply_off_the_radio_down_and_the_splash)
{
    /* Each alone holds it back; it opens the frame the last one goes. */
    for (int k = 0; k < 4; ++k) {
        after_install();
        if (k == 0) {
            g_feed.unsafe = true;     /* ARMED, an arm waiting, the supply */
        } else if (k == 1) {
            g_feed.radio_up = true;
        } else if (k == 2) {
            g_feed.installing = true;
        } else {
            ui_router_init();        /* the splash */
        }
        frames(5);
        CHECK(ui_router_current() != SCREEN_UPDATES);
        CHECK(!g_drv.opened);
        g_feed.unsafe = false;
        g_feed.radio_up = false;
        g_feed.installing = false;
        if (k == 3) {
            past_splash();
        }
        (void)frame();
        CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    }
    /* The order of the reasons. */
    upd_run_feed_t f = { .unsafe = true, .radio_up = true, .installing = true };
    CHECK_EQ(upd_run_wait(&f), UPD_RUN_WAIT_SAFE);
    f.unsafe = false;
    CHECK_EQ(upd_run_wait(&f), UPD_RUN_WAIT_RADIO);
    f.radio_up = false;
    CHECK_EQ(upd_run_wait(&f), UPD_RUN_WAIT_INSTALL);
    CHECK_EQ(upd_run_wait(NULL), UPD_RUN_WAIT_NONE);
}

TEST_CASE(the_band_names_the_wait_once_and_again_when_it_changes)
{
    after_install();
    g_feed.unsafe = true;
    (void)frame();
    CHECK(band_says(TX_ALERT_RUN_WAIT_SAFE));
    /* Said once: a line the band shows since stays. */
    ui_router_set_alert("other");
    frames(5);
    CHECK_STR_EQ(ui_router_alert(), "other");
    /* Another reason: said again. */
    g_feed.unsafe = false;
    g_feed.radio_up = true;
    (void)frame();
    CHECK(band_says(TX_ALERT_RUN_WAIT_WIFI));
    g_feed.radio_up = false;
    g_feed.installing = true;
    (void)frame();
    CHECK(band_says(TX_ALERT_RUN_WAIT_INSTALL));
    /* The splash has no band: nothing is said until it has one. */
    after_install();
    ui_router_init();
    g_feed.unsafe = true;
    frames(3);
    CHECK(ui_router_alert() == NULL);
    past_splash();
    (void)frame();
    CHECK(band_says(TX_ALERT_RUN_WAIT_SAFE));
    /* No run: no line. */
    after_install();
    (void)fw_run_end(&k_ops, &g_run);
    g_feed.unsafe = true;
    frames(3);
    CHECK(ui_router_alert() == NULL);
    /* Each wait has its own line, in both languages. */
    static const ui_text_id_t k_line[] = {
        TX_ALERT_RUN_WAIT_SAFE, TX_ALERT_RUN_WAIT_WIFI,
        TX_ALERT_RUN_WAIT_INSTALL,
    };
    for (int w = UPD_RUN_WAIT_SAFE; w <= UPD_RUN_WAIT_INSTALL; ++w) {
        CHECK_EQ(upd_run_wait_text((upd_run_wait_t)w), k_line[w - 1]);
    }
    CHECK_EQ(upd_run_wait_text(UPD_RUN_WAIT_NONE), TX_ALERT_RUN_WAIT_SAFE);
    ui_text_set_language(UI_LANG_DE);
    CHECK(strstr(ui_tr(TX_ALERT_RUN_WAIT_SAFE), "SAFE") != NULL);
    CHECK(strcmp(ui_tr(TX_ALERT_RUN_WAIT_SAFE),
                 "Update run: UPDATES opens once SAFE with the supply off.")
          != 0);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_home_tag_during_a_run_ends_it_and_the_next_start_opens_nothing)
{
    after_install();
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    home_tag();
    CHECK(ui_router_current() != SCREEN_UPDATES);
    CHECK_EQ(frame(), UPD_RUN_END_LEFT);
    CHECK(!g_run.active);
    CHECK_EQ(g_store.len, 0u);            /* the note erased */
    CHECK_EQ(frame(), UPD_RUN_GOES_ON);   /* ended once */
    /* The next start, a power cut later: no run, nothing opens. */
    CHECK_EQ(boot(K_NEW_TEXT), FW_RUN_NONE);
    iomcu_behind();
    past_splash();
    frames(5);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* A home tag over a list or SINGLE UPDATES climbs and ends nothing. */
    after_install();
    (void)frame();
    upd_open_single(upd());
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK_EQ(frame(), UPD_RUN_GOES_ON);
    CHECK(g_run.active);
    /* A home tag without a run is drained: a run later ends on its own. */
    CHECK(upd()->back() == false);
    (void)fw_run_end(&k_ops, &g_run);
    (void)frame();
    CHECK(!upd_screen_take_left());
}

TEST_CASE(a_forced_screen_over_updates_ends_nothing)
{
    static const ui_screen_id_t k_to[] = {
        SCREEN_BUSFAULT, SCREEN_UPDATE, SCREEN_COPROC_UPDATE, SCREEN_WIFI,
        SCREEN_OVERVIEW,
    };
    for (size_t i = 0; i < sizeof(k_to) / sizeof(k_to[0]); ++i) {
        after_install();
        (void)frame();
        CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
        ui_router_goto(k_to[i]);
        for (int n = 0; n < 5; ++n) {
            CHECK_EQ(frame(), UPD_RUN_GOES_ON);
        }
        CHECK(g_run.active);
        CHECK(g_store.len > 0u);
        CHECK_EQ(ui_router_current(), k_to[i]);   /* not opened again */
    }
}

TEST_CASE(a_run_with_no_blocking_step_after_the_links_are_up_ends_itself)
{
    /* Before the device rows are known a zeroed view passes every step:
     * no end. */
    after_install();
    memset(&g_cu, 0, sizeof(g_cu));
    memset(&g_c6, 0, sizeof(g_c6));
    memset(g_d, 0, sizeof(g_d));
    g_feed.known = false;
    frames(5);
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_NONE);
    CHECK(g_run.active);
    /* Known: the IOmcu's link and its check, the Dial current. */
    views_current();
    g_feed.known = true;
    CHECK_EQ(frame(), UPD_RUN_END_DONE);
    CHECK(!g_run.active);
    CHECK_EQ(g_store.len, 0u);
    /* UPDATES shows the last card; the run flag is off. */
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    (void)frame();
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_NONE);
    CHECK(!wiz().show_skipped);
}

TEST_CASE(a_restart_due_ends_nothing_and_keeps_the_note_it_wrote)
{
    /* The C6 step in a run: its update ends, the note for the start after
     * the restart is written, the restart is 3,000ms off. */
    after_install();
    g_cu.sec = CU_SEC_SAME;
    g_c6.row = FW_C6_ROW_READY;
    g_c6.carried = (fw_version_t){ 2u, 14u, 0u };
    (void)frame();
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_C6);
    g_feed.restart_due = true;
    g_c6.row = FW_C6_ROW_UPDATING;
    frames(3);
    g_c6.row = FW_C6_ROW_REINSTALL;
    g_c6.running = g_c6.carried;
    CHECK(fw_run_begin(&k_ops, &g_run, K_NEW_TEXT));
    /* Nothing blocks, and the home tag: neither ends it now. */
    home_tag();
    for (int n = 0; n < 5; ++n) {
        CHECK_EQ(frame(), UPD_RUN_GOES_ON);
    }
    CHECK(g_store.len > 0u);
}

TEST_CASE(absent_steps_wait_in_a_run_and_advance_when_the_device_links)
{
    after_install();
    g_cu.sec = CU_SEC_SAME;
    dial_away();
    (void)frame();
    upd_wiz_t w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_DIAL);
    CHECK_EQ(w.step[UPD_WIZ_DIAL].state, UPD_WIZ_WAITING);
    CHECK_EQ(w.act, UPD_WIZ_ACT_NONE);
    CHECK(w.can_skip);
    frames(20);
    CHECK(g_run.active);
    /* It links on the build before: the hold stands, nothing tapped. */
    dial_behind();
    (void)frame();
    w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_DIAL);
    CHECK_EQ(w.step[UPD_WIZ_DIAL].state, UPD_WIZ_OPEN);
    CHECK_EQ(w.act, UPD_WIZ_ACT_DIAL);
    CHECK(w.act_live);
    /* The IOmcu without a link waits too, a node not bound never. */
    g_cu.sec = CU_SEC_NO_LINK;
    (void)frame();
    w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_IOMCU);
    CHECK_EQ(w.step[UPD_WIZ_IOMCU].state, UPD_WIZ_WAITING);
    CHECK_EQ(w.step[UPD_WIZ_PNODE].tag, UPD_WIZ_TAG_NOT_BOUND);
    CHECK(!upd_wiz_blocks(w.step[UPD_WIZ_PNODE].state));
    /* Outside a run the same Dial away is passed. */
    (void)fw_run_end(&k_ops, &g_run);
    g_cu.sec = CU_SEC_SAME;
    dial_away();
    (void)frame();
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_NONE);
}

TEST_CASE(the_startup_check_does_not_start_during_a_run)
{
    fresh();
    past_splash();
    upd_driver_t d;
    memset(&d, 0, sizeof(d));
    upd_feed_t f = { .now_ms = 1000u, .remind = true, .available = true,
                     .have_net = true, .gate_open = true, .run = true };
    for (uint32_t t = 0u; t < 60000u; t += 1000u) {
        f.now_ms = 1000u + t;
        CHECK(!upd_startup_due(&d, &f));
    }
    /* The run ended within the start's wait: the check starts once. */
    f.run = false;
    CHECK(upd_startup_due(&d, &f));
    CHECK(!upd_startup_due(&d, &f));
    /* A run that outlasts UPD_START_WAIT_MS: no check at this start. */
    memset(&d, 0, sizeof(d));
    f.run = true;
    f.now_ms = 0u;
    CHECK(!upd_startup_due(&d, &f));
    f.now_ms = UPD_START_WAIT_MS;
    CHECK(!upd_startup_due(&d, &f));
    f.run = false;
    CHECK(!upd_startup_due(&d, &f));
}

TEST_CASE(a_restart_in_the_middle_resumes_at_the_first_blocking_step)
{
    /* After the panel's install: the IOmcu behind, the Dial behind. */
    after_install();
    dial_behind();
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
    /* The IOmcu updated; the C6 behind its carried image: step 3. */
    g_cu.sec = CU_SEC_SAME;
    g_cu.running = g_cu.carried;
    g_c6.row = FW_C6_ROW_READY;
    g_c6.carried = (fw_version_t){ 2u, 14u, 0u };
    (void)frame();
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_C6);
    CHECK(g_run.active);
    /* The C6's update ends: note, restart. */
    CHECK(fw_run_begin(&k_ops, &g_run, K_NEW_TEXT));
    CHECK_EQ(boot(K_NEW_TEXT), FW_RUN_STANDS);
    CHECK_EQ(g_run.opens_left, 1u);
    views_current();
    g_c6.running = (fw_version_t){ 2u, 14u, 0u };
    g_c6.carried = g_c6.running;
    dial_away();                      /* not linked yet after the start */
    past_splash();
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_DIAL);
    CHECK_EQ(wiz().step[UPD_WIZ_DIAL].state, UPD_WIZ_WAITING);
    dial_behind();
    (void)frame();
    CHECK_EQ(wiz().act, UPD_WIZ_ACT_DIAL);
    /* The Dial updated: the run ends, the note is gone. */
    g_d[UPD_DEV_DIAL].row = DEVUPD_ROW_REINSTALL;
    g_d[UPD_DEV_DIAL].running = (fw_version_t){ 0u, 23u, 15u };
    CHECK_EQ(frame(), UPD_RUN_END_DONE);
    CHECK_EQ(g_store.len, 0u);
    CHECK_EQ(boot(K_NEW_TEXT), FW_RUN_NONE);
}

TEST_CASE(two_openings_at_most_then_the_run_is_over)
{
    /* A power cut after the panel's restart, before step 2, twice. */
    after_install();
    CHECK_EQ(g_run.opens_left, 1u);
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    /* Power cut: no end.  The second start spends the last opening. */
    CHECK_EQ(boot(K_NEW_TEXT), FW_RUN_STANDS);
    CHECK_EQ(g_run.opens_left, 0u);
    CHECK_EQ(g_store.len, 0u);            /* the last one erased it */
    views_current();
    iomcu_behind();
    past_splash();
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    /* Power cut again: the third start finds no run and opens nothing. */
    CHECK_EQ(boot(K_NEW_TEXT), FW_RUN_NONE);
    views_current();
    iomcu_behind();
    past_splash();
    frames(5);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(!g_drv.opened);
}

TEST_CASE(a_rolled_back_panel_image_opens_nothing_and_keeps_the_rollback_alert)
{
    store_t none = { 0 };
    g_store = none;
    install(K_NEW_TEXT);
    /* The trial went back: the build before runs. */
    CHECK_EQ(boot(K_OLD_TEXT), FW_RUN_MISMATCH);
    CHECK(!g_run.active);
    CHECK_EQ(g_store.len, 0u);
    views_current();
    iomcu_behind();
    past_splash();
    ui_router_set_alert("rolled back");
    g_feed.unsafe = true;              /* no wait line either */
    frames(5);
    g_feed.unsafe = false;
    frames(5);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK_STR_EQ(ui_router_alert(), "rolled back");
    CHECK_EQ(boot(K_OLD_TEXT), FW_RUN_NONE);
}

TEST_CASE(the_link_lost_while_step_2_waits_keeps_the_run_and_the_skip)
{
    after_install();
    (void)frame();
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
    g_cu.sec = CU_SEC_NO_LINK;
    g_cu.known = false;
    frames(10);
    const upd_wiz_t w = wiz();
    CHECK_EQ(w.current, (unsigned)UPD_WIZ_IOMCU);
    CHECK_EQ(w.step[UPD_WIZ_IOMCU].state, UPD_WIZ_WAITING);
    CHECK(w.can_skip);
    CHECK(g_run.active);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    /* It answers again and is checked: the hold is back. */
    iomcu_behind();
    g_cu.known = true;
    (void)frame();
    CHECK_EQ(wiz().act, UPD_WIZ_ACT_IOMCU);
    CHECK(wiz().act_live);
}

TEST_CASE(a_declined_step_in_a_run_goes_to_the_next_and_the_end_card_names_it)
{
    after_install();
    dial_behind();
    (void)frame();
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
    tap(upd(), UP_ACT_X, UP_SKIP_Y);
    CHECK(upd_screen_take_skip_coproc());
    (void)frame();
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_DIAL);
    CHECK(g_run.active);
    /* The Dial's step declined too: nothing blocks, the run ends, the
     * last card names both. */
    tap(upd(), UP_ACT_X, UP_SKIP_Y);
    CHECK_EQ(frame(), UPD_RUN_END_DONE);
    CHECK_EQ(g_store.len, 0u);
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_NONE);
    char skipped[64];
    snprintf(skipped, sizeof(skipped), ui_tr(TX_UPD_WIZ_END_SKIPPED),
             "IOMCU, DIAL");
    CHECK(card_has(skipped));
}

/* ------------------------------------------------ the run and the offer */

static cu_driver_t g_cu_drv;    /* panel_update.c's cu_render_service() */
static cu_feed_t   g_cu_feed;

/* The IOmcu behind, offered: what the control task publishes after the
 * link-up's check, matching iomcu_behind(). */
static void cu_offered(void)
{
    memset(&g_cu_drv, 0, sizeof(g_cu_drv));
    memset(&g_cu_feed, 0, sizeof(g_cu_feed));
    g_cu_feed.have_image = true;
    g_cu_feed.reachable = true;
    g_cu_feed.check = CU_CHECK_DIFFERENT;
    g_cu_feed.running = (cu_version_t){ 0, 23, 14 };
    g_cu_feed.carried = (cu_version_t){ 0, 23, 15 };
    g_cu_feed.offered = true;
    g_cu_feed.offer_gen = 1u;
}

/* One frame of both, in panel_frame.c's order: the run first, then the
 * offer, told whether the run waits to open UPDATES. */
static cu_hand_t both(void)
{
    const upd_run_end_t e = frame();
    g_cu_feed.radio_up = g_feed.radio_up;
    g_cu_feed.installing = g_feed.installing;
    g_cu_feed.run_waits = e == UPD_RUN_GOES_ON
                          && upd_run_waits(&g_drv, &g_feed);
    cu_hand_t h;
    coproc_update_drive(&g_cu_drv, &g_cu_feed, g_feed.unsafe, &h);
    return h;
}

/* A run and an offer both due: UPDATES opens once, by the run, and the
 * offer counts as shown there.  On a trial start the run is read after
 * the offer opened UPDATES: the run's opening is spent, and after a home
 * tag before the read it does not open UPDATES a second time. */
TEST_CASE(a_run_and_an_offer_due_together_open_updates_once)
{
    after_install();
    cu_offered();
    cu_hand_t h = both();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK(g_drv.opened);
    CHECK(!coproc_update_opened());       /* the run's opening */
    CHECK_EQ(g_cu_drv.shown_gen, 1u);
    CHECK_EQ(wiz().current, (unsigned)UPD_WIZ_IOMCU);
    ui_router_goto(SCREEN_OVERVIEW);      /* forced away, not left */
    for (int i = 0; i < 5; ++i) {
        h = both();
        CHECK(!h.decline);
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    }

    /* The trial start: the note is read once the build is kept, after
     * the offer opened UPDATES and the home tag declined it. */
    after_install();
    const fw_run_t kept = g_run;
    memset(&g_run, 0, sizeof(g_run));     /* not read yet */
    cu_offered();
    (void)both();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK(coproc_update_opened());
    home_tag();
    h = both();
    CHECK(h.decline);
    CHECK(ui_router_current() != SCREEN_UPDATES);
    g_cu_feed.offered = false;            /* the decline taken */
    ui_router_goto(SCREEN_OVERVIEW);
    g_run = kept;                         /* kept: the note read */
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(both().starts, 0u);
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    }
    CHECK(g_drv.opened);
    CHECK(g_run.active);

    /* Read while the offer's UPDATES shows: spent the same way. */
    after_install();
    memset(&g_run, 0, sizeof(g_run));
    cu_offered();
    (void)both();
    g_run = kept;
    (void)both();
    CHECK(g_drv.opened);
    ui_router_goto(SCREEN_OVERVIEW);
    (void)both();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* Both held back: the band carries the run's line only, in every wait, and
 * the offer's line is not said after the run has opened UPDATES. */
TEST_CASE(one_line_on_the_band_per_frame_while_both_wait)
{
    static const ui_text_id_t k_run[] = {
        TX_ALERT_RUN_WAIT_SAFE, TX_ALERT_RUN_WAIT_WIFI,
        TX_ALERT_RUN_WAIT_INSTALL,
    };
    after_install();
    cu_offered();
    for (int w = 0; w < 3; ++w) {
        g_feed.unsafe = w == 0;
        g_feed.radio_up = w == 1;
        g_feed.installing = w == 2;
        (void)both();
        CHECK(band_says(k_run[w]));
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    }
    g_feed.installing = false;
    ui_router_set_alert(NULL);
    (void)both();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(g_cu_drv.shown_gen, 1u);

    /* Without a run the offer's own line names the wait. */
    after_install();
    (void)fw_run_end(&k_ops, &g_run);
    cu_offered();
    g_feed.unsafe = true;
    (void)both();
    CHECK(band_says(TX_ALERT_CU_OFFER_SAFE));
}

int main(void)
{
    RUN(a_run_opens_updates_once_on_overview_and_never_on_another_screen);
    RUN(it_waits_for_safe_the_supply_off_the_radio_down_and_the_splash);
    RUN(the_band_names_the_wait_once_and_again_when_it_changes);
    RUN(the_home_tag_during_a_run_ends_it_and_the_next_start_opens_nothing);
    RUN(a_forced_screen_over_updates_ends_nothing);
    RUN(a_run_with_no_blocking_step_after_the_links_are_up_ends_itself);
    RUN(a_restart_due_ends_nothing_and_keeps_the_note_it_wrote);
    RUN(absent_steps_wait_in_a_run_and_advance_when_the_device_links);
    RUN(the_startup_check_does_not_start_during_a_run);
    RUN(a_restart_in_the_middle_resumes_at_the_first_blocking_step);
    RUN(two_openings_at_most_then_the_run_is_over);
    RUN(a_rolled_back_panel_image_opens_nothing_and_keeps_the_rollback_alert);
    RUN(the_link_lost_while_step_2_waits_keeps_the_run_and_the_skip);
    RUN(a_declined_step_in_a_run_goes_to_the_next_and_the_end_card_names_it);
    RUN(a_run_and_an_offer_due_together_open_updates_once);
    RUN(one_line_on_the_band_per_frame_while_both_wait);
    return test_summary("upd_run");
}
