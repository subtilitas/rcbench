/*
 * UPDATES' settings list: the footer's SETTINGS opens it while no check or
 * download runs, its rows switch the reminder and the releases, and only
 * SAVE writes them.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_rig.h"

TEST_CASE(the_reminder_row_switches_it_and_save_writes_it)
{
    fresh();
    CHECK(settings_get_bool(SET_UPD_REMIND));    /* on by default */
    list_step(0, false);
    CHECK(!settings_get_bool(SET_UPD_REMIND));
    /* The list writes nothing by itself: SAVE does. */
    CHECK(!settings_save_asked());
    const gfx_rect_t sv = ui_setlist_strip_rect(2);
    tap(upd(), sv.x + sv.w / 2, sv.y + sv.h / 2);
    CHECK(settings_save_asked());
    CHECK(settings_save_tick(true));
    list_step(0, true);
    CHECK(settings_get_bool(SET_UPD_REMIND));
    CHECK(settings_save_tick(true));
    /* It asks the application for nothing: the setting is the request. */
    CHECK_EQ(upd_screen_take(), 0u);
    /* The home tag closes the list, the screen stays. */
    list_back();
    CHECK(!upd()->covered());
    /* And it works without a home network: turning reminders back on
     * needs none. */
    g_v.have_net = false;
    push();
    list_step(0, true);
    CHECK(!settings_get_bool(SET_UPD_REMIND));
}

TEST_CASE(the_releases_row_switches_the_channel_while_no_job_runs)
{
    fresh();
    CHECK_EQ(settings_get_int(SET_UPD_CHANNEL), 0);    /* STABLE */
    list_step(1, true);
    CHECK_EQ(settings_get_int(SET_UPD_CHANNEL), 1);    /* TESTING */
    CHECK(settings_save_tick(true));
    list_step(1, true);
    CHECK_EQ(settings_get_int(SET_UPD_CHANNEL), 0);
    CHECK(settings_save_tick(true));
    list_back();
    /* During a check SETTINGS takes no press and says why. */
    ui_router_set_alert(NULL);
    g_v.have_net = false;
    g_v.phase = UPD_PH_ASKING;
    push();
    tap(upd(), UP_SET_X, UP_SET_Y);
    CHECK(!upd()->covered());
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), TR(FOOT_SETTINGS_BUSY)) == 0);
    CHECK_EQ(settings_get_int(SET_UPD_CHANNEL), 0);
    /* A check that starts under the open list closes it. */
    g_v.phase = UPD_PH_IDLE;
    push();
    tap(upd(), UP_SET_X, UP_SET_Y);
    CHECK(upd()->covered());
    g_v.phase = UPD_PH_ASKING;
    push();
    ticks(upd(), 1);
    CHECK(!upd()->covered());
    CHECK_EQ(upd_screen_take(), 0u);
}

/* An unsaved change and the home tag: the leave warning holds the list;
 * DISCARD puts the reminder back and closes the list.  A cancel (a lost
 * touch) drops a press under way and leaves the list as it is. */
TEST_CASE(the_home_tag_warns_of_an_unsaved_change_and_discard_restores_it)
{
    fresh();
    list_step(0, false);
    CHECK(!settings_get_bool(SET_UPD_REMIND));
    list_back();
    CHECK(upd()->covered());
    CHECK(upd_screen_settings_list()->warn);
    const gfx_rect_t ds = ui_setlist_strip_rect(1);
    tap(upd(), ds.x + ds.w / 2, ds.y + ds.h / 2);
    CHECK(settings_get_bool(SET_UPD_REMIND));
    CHECK(!upd()->covered());
    CHECK(!settings_save_asked());

    tap(upd(), UP_SET_X, UP_SET_Y);
    CHECK(upd()->covered());
    const ui_setlist_t *sl = upd_screen_settings_list();
    const gfx_rect_t k = ui_setlist_row_part(sl, 0, UI_SL_PART_PLUS);
    ev(upd(), TOUCH_EVENT_DOWN, 1, k.x + k.w / 2, k.y + k.h / 2, 0);
    if (upd()->cancel != NULL) {
        upd()->cancel();
    }
    ev(upd(), TOUCH_EVENT_UP, 1, k.x + k.w / 2, k.y + k.h / 2, 0);
    CHECK(settings_get_bool(SET_UPD_REMIND));
    CHECK(upd()->covered());
    list_back();
    CHECK(!upd()->covered());
}

int main(void)
{
    RUN(the_reminder_row_switches_it_and_save_writes_it);
    RUN(the_releases_row_switches_the_channel_while_no_job_runs);
    RUN(the_home_tag_warns_of_an_unsaved_change_and_discard_restores_it);
    return test_summary("upd_settings");
}
