/**
 * @file
 * @brief The panel's layout header: the figures every screen reads, and the
 *        relations between them that the screens assume.
 *
 * The values are the 1024 x 600 px panel's.  A panel of another size changes
 * the figures here and in the same commit; the relations hold for any size.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "injector.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_theme.h"

TEST_CASE(the_panel_is_1024_by_600)
{
    CHECK_EQ(UI_PANEL_W, 1024);
    CHECK_EQ(UI_PANEL_H, 600);
}

TEST_CASE(the_touch_injector_covers_the_panel)
{
    CHECK_EQ((unsigned)UI_PANEL_W, (unsigned)INJ_RES_X);
    CHECK_EQ((unsigned)UI_PANEL_H, (unsigned)INJ_RES_Y);
}

TEST_CASE(the_body_is_the_panel_below_the_band)
{
    CHECK_EQ(UI_BAND_H, 60);
    CHECK_EQ(UI_BODY_H, UI_PANEL_H - UI_BAND_H);
    CHECK_EQ(UI_BODY_H, 540);
}

TEST_CASE(the_alert_band_is_the_bottom_of_the_panel)
{
    CHECK_EQ(UI_ALERT_H, 44);
    CHECK_EQ(UI_ALERT_Y + UI_ALERT_H, UI_PANEL_H);
    /* It sits inside the body and leaves room above it. */
    CHECK(UI_ALERT_Y > UI_BAND_H);
    CHECK(UI_ALERT_H < UI_BODY_H);
}

TEST_CASE(the_shared_metrics_fit_the_panel)
{
    CHECK_EQ(UI_MARGIN, 8);
    CHECK(2 * UI_MARGIN < UI_PANEL_W);
    CHECK(2 * UI_MARGIN < UI_BODY_H);
    CHECK_EQ(UI_TAP_MIN, 48);
    CHECK_EQ(UI_BTN_H, 44);
    CHECK_EQ(UI_BTN_H_TALL, 56);
    CHECK(UI_BTN_H < UI_BTN_H_TALL);
    CHECK_EQ(UI_R_CARD, 10);
    CHECK_EQ(UI_R_CTL, 8);
    CHECK_EQ(UI_R_CHIP, 5);
    CHECK(UI_R_CHIP < UI_R_CTL && UI_R_CTL < UI_R_CARD);
}

int main(void)
{
    RUN(the_panel_is_1024_by_600);
    RUN(the_touch_injector_covers_the_panel);
    RUN(the_body_is_the_panel_below_the_band);
    RUN(the_alert_band_is_the_bottom_of_the_panel);
    RUN(the_shared_metrics_fit_the_panel);
    return test_summary("layout");
}
