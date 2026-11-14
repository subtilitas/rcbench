/*
 * SETUP's split panes (settings_panes.c): SENSORS and WIRELESS, each
 * call handed to its own pane, a category without one and a pane out of
 * range answered with nothing; a power node view that changes repaints
 * SETUP, the same view does not.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "sensors_pane.h"
#include "settings.h"
#include "settings_screen_int.h"
#include "ui_text.h"
#include "wireless_pane.h"

TEST_CASE(the_split_panes_dispatch_to_their_own_pane)
{
    settings_set_store(NULL);
    settings_init();
    ui_text_set_language(UI_LANG_EN);
    CHECK_EQ(stg_pane(SET_CAT_SENSORS), STG_PANE_SENSORS);
    CHECK_EQ(stg_pane(SET_CAT_WIRELESS), STG_PANE_WIRELESS);
    CHECK_EQ(stg_pane(SET_CAT_APP), -1);
    CHECK_EQ(stg_pane_count(STG_PANE_SENSORS), SENS_ENTRY_COUNT);
    CHECK_EQ(stg_pane_count(STG_PANE_WIRELESS), WL_ENTRY_COUNT);
    CHECK_EQ(stg_pane_count(-1), 0);
    stg_pane_select(STG_PANE_WIRELESS, WL_ENTRY_DIAL);
    CHECK_EQ(stg_pane_selected(STG_PANE_WIRELESS), WL_ENTRY_DIAL);
    CHECK_EQ(stg_pane_selected(-1), 0);
    stg_pane_select(-1, 3);
    CHECK_EQ(wireless_pane_selected(), WL_ENTRY_DIAL);
    char buf[32] = "x";
    gfx_color_t ink = 0;
    CHECK_STR_EQ(stg_pane_name(STG_PANE_WIRELESS, WL_ENTRY_DIAL, buf,
                               sizeof buf), TR(DIAL_ENTRY));
    CHECK_STR_EQ(stg_pane_name(-1, 0, buf, sizeof buf), "");
    CHECK_STR_EQ(stg_pane_name(-1, 0, NULL, 0), "");
    CHECK_STR_EQ(stg_pane_state(STG_PANE_WIRELESS, WL_ENTRY_DIAL, buf,
                                sizeof buf, &ink), TR(OFF));
    CHECK_STR_EQ(stg_pane_state(-1, 0, buf, sizeof buf, &ink), "");
    CHECK_STR_EQ(stg_pane_state(-1, 0, NULL, 0, &ink), "");
    /* RESET of the WIRELESS entry through the dispatch. */
    settings_set(SET_DIAL_EN, 1.0f);
    settings_set(SET_DIAL_SRV, 3.0f);
    stg_pane_reset(STG_PANE_WIRELESS, WL_ENTRY_DIAL);
    CHECK(!settings_get_bool(SET_DIAL_EN));
    CHECK_EQ(settings_get_int(SET_DIAL_SRV), 1);
    stg_pane_reset(-1, 0);
}

/** @brief A power node view with two nodes found, the second bound. */
static pnode_view_t two_found(void)
{
    pnode_view_t v;
    memset(&v, 0, sizeof v);
    v.link = PNODE_LINK_UP;
    v.scan_done = true;
    v.found_n = 2u;
    snprintf(v.found[0].addr, sizeof v.found[0].addr, "7C:DF:A1:12:34:01 P");
    snprintf(v.found[1].addr, sizeof v.found[1].addr, "7C:DF:A1:12:34:02 P");
    snprintf(v.bound_addr, sizeof v.bound_addr, "7C:DF:A1:12:34:02 P");
    v.have_status = true;
    v.vbat_mv = 3916u;
    return v;
}

/** @brief Whether setting @p v repainted SETUP: its chrome marked stale. */
static bool repaints(const pnode_view_t *v)
{
    memset(s.chrome_valid, 1, sizeof s.chrome_valid);
    wireless_pane_set_pnode(v);
    return !s.chrome_valid[0];
}

TEST_CASE(a_view_change_repaints_and_the_same_view_does_not)
{
    const pnode_view_t base = two_found();
    (void)repaints(&base);
    CHECK(!repaints(&base));
    pnode_view_t v;
#define TRY(mutate) do { v = base; mutate; CHECK(repaints(&v)); \
                         CHECK(repaints(&base)); } while (0)
    TRY(v.link = PNODE_LINK_STALE);
    TRY(v.scan_done = false);
    TRY(v.scan_cut = true);
    TRY(v.found_n = 1u);
    TRY(v.hidden = 3u);
    TRY(v.proto = 1u);
    TRY(v.have_status = false);
    TRY(v.bat_low = true);
    TRY(v.bat_none = true);
    TRY(v.bat_err = true);
    TRY(v.vbat_mv = 3915u);
    TRY(v.bound_addr[0] = 'X');
    TRY(v.bound_name[0] = 'X');
    TRY(v.found[1].addr[0] = 'X');
    TRY(v.found[1].name[0] = 'X');
    TRY(v.found[PNODE_FOUND_MAX - 1u].rssi = -10);
#undef TRY
    wireless_pane_set_pnode(NULL);
}

int main(void)
{
    RUN(the_split_panes_dispatch_to_their_own_pane);
    RUN(a_view_change_repaints_and_the_same_view_does_not);
    return test_summary("settings_panes");
}
