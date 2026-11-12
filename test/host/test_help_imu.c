/**
 * @file test_help_imu.c
 * @brief The IMU screen's guide: the left column's page and the card's,
 * both while ARMED as the screen drives no output, none over the found
 * list; every page fits in both languages and themes, SAFE and ARMED, in
 * each link state the card names.
 *
 * Cases:
 * - every_page_of_imu_fits
 * - the_pages_mark_the_controls_the_card_and_the_footer
 * - the_found_list_takes_every_page
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "help_check.h"
#include "imu_screen.h"
#include "imu_screen_int.h"
#include "ui_help.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief The link state pose_view() shows. */
static imu_link_t s_link;

/**
 * @brief The screen entered, showing a sensor bound in @p link: at 50Hz,
 *        3.95V and three angles where it is connected.
 * @param link The link's state.
 */
static void show(imu_link_t link)
{
    imu_screen()->reset();
    imu_screen()->enter();
    imu_view_t v;
    memset(&v, 0, sizeof v);
    v.link = link;
    snprintf(v.version, sizeof v.version, "2.13.0");
    snprintf(v.bound_addr, sizeof v.bound_addr, "00:0C:BF:08:19:29 P");
    snprintf(v.bound_name, sizeof v.bound_name, "HC-06");
    if (link == IMU_LINK_CONNECTED) {
        v.r.valid = true;
        v.r.taken_ms = 1000u;
        v.r.angle[0] = 12.3f;
        v.r.angle[1] = -0.1f;
        v.r.angle[2] = 0.2f;
        v.r.rate_dhz = 500u;
        v.r.battery_v = 3.95f;
    }
    imu_screen_set(&v);
}

/** @brief The screen in s_link. */
static void pose_view(void) { show(s_link); }

/**
 * @brief A tap at the middle of @p r, in the screen's coordinates.
 * @param r The rect.
 */
static void tap_rect(gfx_rect_t r)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1,
                                   .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };
    imu_screen()->event(&e);
    e.type = TOUCH_EVENT_UP;
    imu_screen()->event(&e);
}

/**
 * @brief Whether @p p marks an item with @p text.
 * @param p    The page.
 * @param text The text.
 * @return True when one of its items has it.
 */
static bool marks(const ui_help_page_t *p, ui_text_id_t text)
{
    for (int i = 0; i < p->count; ++i) {
        if (p->item[i].text == text) {
            return true;
        }
    }
    return false;
}

TEST_CASE(every_page_of_imu_fits)
{
    static const imu_link_t k_links[] = {
        IMU_LINK_OFF, IMU_LINK_READY, IMU_LINK_CONNECTING,
        IMU_LINK_CONNECTED,
    };
    for (size_t i = 0; i < sizeof(k_links) / sizeof(k_links[0]); ++i) {
        s_link = k_links[i];
        char what[32];
        snprintf(what, sizeof(what), "link %d", (int)s_link);
        CHECK_EQ(check_every_state(imu_screen(), pose_view, what), 2);
    }
}

TEST_CASE(the_pages_mark_the_controls_the_card_and_the_footer)
{
    ui_help_page_t p;
    show(IMU_LINK_CONNECTED);
    REQUIRE(ui_help_page(imu_screen(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_IM_TITLE_CTL);
    CHECK_EQ(p.side, UI_HELP_DOCK_RIGHT);
    CHECK(marks(&p, TX_HG_IM_SCAN));
    CHECK(marks(&p, TX_HG_IM_ZERO));
    CHECK(marks(&p, TX_HG_IM_UNZERO));
    REQUIRE(ui_help_page(imu_screen(), false, 1, &p));
    CHECK_EQ(p.title, TX_HG_IM_TITLE_READ);
    CHECK_EQ(p.side, UI_HELP_DOCK_LEFT);
    CHECK_EQ(p.count, 5);
    CHECK(marks(&p, TX_HG_IM_ANGLES));
    CHECK(marks(&p, TX_HG_IM_CAL));
    CHECK(marks(&p, TX_HG_IM_OPTIONS));
    /* No output on this screen: both pages while ARMED, and no note. */
    CHECK_EQ(ui_help_count(imu_screen(), true), 2);
    CHECK(!ui_help_armed_note(imu_screen(), true));
}

TEST_CASE(the_found_list_takes_every_page)
{
    show(IMU_LINK_READY);
    REQUIRE(ui_help_count(imu_screen(), false) == 2);
    tap_rect(imu_screen_row_rect(IMS_ROW_SCAN));
    REQUIRE(imu_screen()->covered());
    CHECK_EQ(ui_help_count(imu_screen(), false), 0);
    ui_help_page_t p;
    CHECK(!ui_help_page(imu_screen(), false, 0, &p));
    REQUIRE(imu_screen()->back());
    CHECK_EQ(ui_help_count(imu_screen(), false), 2);
}

int main(void)
{
    RUN(every_page_of_imu_fits);
    RUN(the_pages_mark_the_controls_the_card_and_the_footer);
    RUN(the_found_list_takes_every_page);
    return test_summary("help_imu");
}
