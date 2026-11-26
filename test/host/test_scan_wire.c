/**
 * @file test_scan_wire.c
 * @brief The scanner wired to the scan flow: a line of the port
 *        (scan_port.h) reaches scan_flow through scan_flow_pull() and
 *        scan_port_take_line(), as the panel's frame task moves it before
 *        scan_flow_tick().
 *
 * Cases:
 * - a_port_line_sets_the_active_servo
 * - two_lines_in_one_frame_are_both_taken_over_two_frames
 * - a_long_line_arrives_as_too_long
 * - a_console_line_waiting_keeps_the_port_line_for_the_next_frame
 * - an_empty_queue_leaves_the_mailbox_free
 * - a_console_line_is_taken_while_the_port_queue_is_empty
 * - a_line_is_cut_to_the_room_given
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "nav_rig.h"

#include "scan_flow.h"
#include "scan_port.h"
#include "servo_active.h"

/** @brief The scanner's port, off the stack. */
static scan_port_t s_port;

/** @brief The port's lines as the flow takes them. */
static bool take(char *line, size_t n, bool *too_long)
{
    return scan_port_take_line(&s_port, line, n, too_long);
}

/** @brief Whether the port holds a line. */
static bool has(void)
{
    return scan_port_has_line(&s_port);
}

/** @brief A take that must not run: the queue is empty. */
static bool take_never(char *line, size_t n, bool *too_long)
{
    (void)line;
    (void)n;
    (void)too_long;
    T_FAIL("take called with an empty queue");
    return false;
}

/** @brief A has that answers empty, and posts a console line while it
 *         is asked, as the console task may in that window. */
static bool has_empty_posting(void)
{
    CHECK(scan_flow_post("KST_X10-0000F"));
    return false;
}

/** @brief A case's start: the menu up, no servo, a started port. */
static uint32_t start(void)
{
    static uint32_t t = 7000000u;
    settings_set_store(NULL);
    settings_init();
    fresh();
    to_overview();
    scan_flow_reset();
    (void)servo_active_clear(false);
    ui_router_set_alert(NULL);
    scan_port_init(&s_port);
    scan_port_start(&s_port, GM861_MODE_SENSING);
    t += 100000u;
    return t;
}

/** @brief The scanner sends @p s at @p t. */
static void scanned(const char *s, uint32_t t)
{
    scan_port_rx(&s_port, (const uint8_t *)s, strlen(s), t);
}

/** @brief The frame task's part of one frame. */
static void frame(uint32_t t)
{
    (void)scan_flow_pull(has, take);
    scan_flow_tick(t, NULL);
}

/** @brief The active id, "" for none. */
static const char *active(void)
{
    const char *id = servo_active_id();
    return id != NULL ? id : "";
}

TEST_CASE(a_port_line_sets_the_active_servo)
{
    const uint32_t t = start();
    scanned("KST_X10-0000A\r\n", t);
    frame(t + 16u);
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_SET);
    CHECK_STR_EQ(active(), "KST_X10-0000A");
}

TEST_CASE(two_lines_in_one_frame_are_both_taken_over_two_frames)
{
    const uint32_t t = start();
    scanned("KST_X10-0000A\rKST_X10-0000B\r", t);
    frame(t + 16u);
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    CHECK_EQ(s_port.q_n, 1u);
    frame(t + 33u);
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_SET);
    CHECK_STR_EQ(active(), "KST_X10-0000B");
    CHECK_EQ(s_port.q_n, 0u);
    CHECK_EQ(s_port.drops, 0u);
}

TEST_CASE(a_long_line_arrives_as_too_long)
{
    const uint32_t t = start();
    /* 70 id characters: the port hands out 64 of them as SCAN_EV_LONG,
     * which alone would read as a malformed line of 64 bytes. */
    char line[80];
    memset(line, 'A', 70u);
    line[70] = '\r';
    line[71] = '\0';
    scanned(line, t);
    frame(t + 16u);
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_TOO_LONG);
    CHECK_STR_EQ(active(), "");
}

TEST_CASE(a_console_line_waiting_keeps_the_port_line_for_the_next_frame)
{
    const uint32_t t = start();
    CHECK(scan_flow_post("KST_X10-0000C"));
    scanned("KST_X10-0000D\r", t);
    CHECK(!scan_flow_pull(has, take));
    CHECK_EQ(s_port.q_n, 1u);
    scan_flow_tick(t + 16u, NULL);
    CHECK_STR_EQ(active(), "KST_X10-0000C");
    frame(t + 33u);
    CHECK_STR_EQ(active(), "KST_X10-0000D");
}

TEST_CASE(an_empty_queue_leaves_the_mailbox_free)
{
    (void)start();
    CHECK(!scan_flow_pull(has, take_never));
    CHECK(!scan_flow_pull(NULL, take));
    CHECK(!scan_flow_pull(has, NULL));
    CHECK(!scan_port_has_line(NULL));
    CHECK(scan_flow_post("KST_X10-0000E"));
    char buf[4];
    bool cut = true;
    CHECK(!scan_port_take_line(&s_port, buf, 0u, &cut));
    CHECK(!scan_port_take_line(&s_port, NULL, sizeof buf, &cut));
    CHECK(!scan_port_take_line(&s_port, buf, sizeof buf, NULL));
    CHECK(!scan_port_take_line(NULL, buf, sizeof buf, &cut));
}

/* A console line posted while the frame task asks an empty queue is
 * taken: the mailbox is not reserved for an empty queue. */
TEST_CASE(a_console_line_is_taken_while_the_port_queue_is_empty)
{
    const uint32_t t = start();
    CHECK(!scan_flow_pull(has_empty_posting, take_never));
    scan_flow_tick(t + 16u, NULL);
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_SET);
    CHECK_STR_EQ(active(), "KST_X10-0000F");
}

/* A line cut to the room the caller gives is NUL ended inside it. */
TEST_CASE(a_line_is_cut_to_the_room_given)
{
    (void)start();
    scanned("KST_X10-0000G\r", 1u);
    char buf[5];
    bool cut = true;
    CHECK(scan_port_take_line(&s_port, buf, sizeof buf, &cut));
    CHECK_STR_EQ(buf, "KST_");
    CHECK(!cut);
}

int main(void)
{
    RUN(a_port_line_sets_the_active_servo);
    RUN(two_lines_in_one_frame_are_both_taken_over_two_frames);
    RUN(a_long_line_arrives_as_too_long);
    RUN(a_console_line_waiting_keeps_the_port_line_for_the_next_frame);
    RUN(an_empty_queue_leaves_the_mailbox_free);
    RUN(a_console_line_is_taken_while_the_port_queue_is_empty);
    RUN(a_line_is_cut_to_the_room_given);
    return test_summary("scan_wire");
}
