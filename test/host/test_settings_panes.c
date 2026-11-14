/*
 * SETUP's split panes (settings_panes.c): SENSORS and WIRELESS, each
 * call handed to its own pane, a category without one and a pane out of
 * range answered with nothing.
 *
 * SPDX-License-Identifier: MIT
 */
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

int main(void)
{
    RUN(the_split_panes_dispatch_to_their_own_pane);
    return test_summary("settings_panes");
}
