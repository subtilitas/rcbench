/*
 * The bench console over the link: the IOmcu's page and console, the panel's
 * relay, and between them both ends of the link over a bus that can drop frames.
 * A line in, its output out, overflow, a lost frame, a restart inside a line, the
 * link down, an IOmcu with no page.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "console_relay.h"
#include "console_tunnel.h"
#include "link_can.h"
#include "link_dev.h"
#include "link_host.h"
#include "link_pages.h"

static page_console_t g_console;
static console_tunnel_t g_tunnel;
static console_relay_t g_relay;
static link_dev_t g_dev;
static link_host_t g_host;
static uint32_t g_now;
static uint16_t g_status[4];
/** @brief Reply frames to drop, from this many on; -1 for none. */
static int g_drop_reply_at;
/** @brief Exchanges made. */
static int g_exchanges;
/** @brief Whether the far end has the page. */
static bool g_has_page;

/** @brief The tunnel's read as a page's. */
static void cn_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    console_tunnel_read(&g_tunnel, off, n, out);
}

/** @brief The tunnel's write as a page's. */
static uint8_t cn_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return console_tunnel_write(&g_tunnel, off, n, in);
}

/** @brief A page for the console's `r` to read. */
static void st_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, &g_status[off], (size_t)n * sizeof(*out));
}

static const link_page_t k_with[] = {
    { LINK_PAGE_STATUS, 4u, st_read, NULL },
    { LINK_PAGE_CONSOLE, LINK_CN_COUNT, cn_read, cn_write },
};
static const link_page_t k_without[] = {
    { LINK_PAGE_STATUS, 4u, st_read, NULL },
};

/** @brief Both ends from a start. */
static void up(bool has_page)
{
    g_has_page = has_page;
    g_now = 1000u;
    g_drop_reply_at = -1;
    g_exchanges = 0;
    memset(g_status, 0, sizeof(g_status));
    g_status[1] = 0x1234u;
    link_dev_init(&g_dev, has_page ? k_with : k_without, has_page ? 2u : 1u, NULL, g_now);
    link_host_init(&g_host, g_now);
    page_console_init(&g_console, &g_dev);
    console_tunnel_init(&g_tunnel, &g_console);
    console_relay_init(&g_relay);
}

/**
 * @brief A request and its answer over the bus, frame by frame.
 * @param req   the request
 * @param reply takes the answer
 * @return true when it came whole
 */
static bool wire(const link_msg_t *req, link_msg_t *reply)
{
    static link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    static link_msg_t ans;
    const size_t n = link_can_encode(req, f, LINK_CAN_MAX_FRAMES);
    bool asked = false;

    for (size_t i = 0u; i < n; ++i) {
        link_msg_t part;

        if (link_can_decode(&f[i], &part)
            && link_dev_dispatch(&g_dev, &part, &ans, g_now)) {
            asked = true;
        }
    }
    if (!asked) {
        link_host_abandon(&g_host);
        return false;
    }
    const size_t m = link_can_encode(&ans, f, LINK_CAN_MAX_FRAMES);

    for (size_t i = 0u; i < m; ++i) {
        link_msg_t part;

        if (g_drop_reply_at >= 0 && (int)i >= g_drop_reply_at) {
            g_drop_reply_at = -1;
            link_host_abandon(&g_host);
            return false;
        }
        if (link_can_decode(&f[i], &part) && link_host_accept(&g_host, &part, g_now, reply)) {
            return true;
        }
    }
    link_host_abandon(&g_host);
    return false;
}

/** @brief The IOmcu's output as console_hw_pass() moves it, with no USB host. */
static void iomcu_pass(void)
{
    static char buf[PAGE_CONSOLE_OUT_LINE * 2u];

    console_tunnel_pass(&g_tunnel, g_now);
    if (console_tunnel_active(&g_tunnel, g_now)) {
        const size_t room = console_tunnel_room(&g_tunnel);
        const size_t n = page_console_take_part(&g_console, buf, room);

        if (n > 0u) {
            console_tunnel_put(&g_tunnel, buf, n);
        }
    }
}

/** @brief One control pass: at most one exchange, as panel_console_pass() makes. */
static void panel_pass(bool link)
{
    static link_msg_t reply;
    console_relay_op_t op;

    console_relay_link(&g_relay, link, g_now);
    if (!console_relay_next(&g_relay, g_now, &op)) {
        return;
    }
    ++g_exchanges;
    link_msg_t req;
    bool built;

    if (op.write) {
        built = link_host_write(&g_host, LINK_PAGE_CONSOLE, op.off, op.n, op.regs, g_now, &req);
    } else {
        built = link_host_read(&g_host, LINK_PAGE_CONSOLE, op.off, op.n, g_now, &req);
    }
    int result = CONSOLE_RELAY_NO_ANSWER;

    if (built && wire(&req, &reply)) {
        result = reply.op == LINK_OP_NACK ? (int)reply.regs[0] : 0;
    }
    console_relay_done(&g_relay, result, result == 0 && !op.write ? reply.regs : NULL);
}

/** @brief Passes of both ends, 5ms each. */
static void run(int passes, bool link)
{
    for (int i = 0; i < passes; ++i) {
        g_now += 5u;
        iomcu_pass();
        panel_pass(link);
    }
}

/** @brief What the operator read so far, appended to @p all. */
static char g_all[16384];
static size_t g_all_len;

/** @brief Moves the relay's lines to g_all. */
static void read_out(void)
{
    static char line[256];
    size_t n;

    while ((n = console_relay_take(&g_relay, line, sizeof(line))) != 0u) {
        if (g_all_len + n < sizeof(g_all)) {
            memcpy(&g_all[g_all_len], line, n);
            g_all_len += n;
            g_all[g_all_len] = '\0';
        }
    }
}

/** @brief A line typed at the panel. */
static void type(const char *s)
{
    for (; *s != '\0'; ++s) {
        console_relay_key(&g_relay, (unsigned char)*s);
    }
    console_relay_key(&g_relay, '\r');
}

/** @brief A start, the link up, and the screen read clean. */
static void linked(bool has_page)
{
    up(has_page);
    g_all_len = 0u;
    g_all[0] = '\0';
    run(4, true);
    read_out();
}

TEST_CASE(a_line_goes_over_the_page_and_its_output_comes_back)
{
    linked(true);
    CHECK_EQ(g_relay.state, CONSOLE_RELAY_UP);
    CHECK(strstr(g_all, "console: link up t_ms=") != NULL);
    type("r 0x01 1");
    run(40, true);
    read_out();
    CHECK(strstr(g_all, "data page=0x01 off=1 n=1: 0x1234\n") != NULL);
}

TEST_CASE(at_most_one_exchange_a_pass_and_a_poll_at_rest_is_rare)
{
    linked(true);
    run(40, true);
    const int before = g_exchanges;

    /* 1000ms at rest: a poll every 50ms. */
    run(200, true);
    CHECK(g_exchanges - before <= 21);
    CHECK(g_exchanges - before >= 19);
}

TEST_CASE(a_long_output_arrives_whole_and_in_order)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    type("help");
    run(400, true);
    read_out();
    CHECK(strstr(g_all, "help | pages | r <page>") != NULL);
    CHECK(strstr(g_all, "more, last on send") != NULL);
    CHECK(strstr(g_all, "more, last on send") > strstr(g_all, "help | pages"));
}

TEST_CASE(a_long_line_comes_in_chunks_and_runs_at_its_end)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    /* 145 characters: 25 chunks. */
    char line[160];

    memset(line, ' ', sizeof(line));
    memcpy(line, "r 1 1", 5u);
    line[150] = '\0';
    type(line);
    run(20, true);
    read_out();
    CHECK(strstr(g_all, "data page=0x01") == NULL);
    run(40, true);
    read_out();
    CHECK(strstr(g_all, "data page=0x01 off=1 n=1: 0x1234\n") != NULL);
}

TEST_CASE(a_line_too_long_says_so_and_sends_nothing)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    char line[200];

    memset(line, 'x', sizeof(line));
    line[199] = '\0';
    type(line);
    run(10, true);
    read_out();
    CHECK(strstr(g_all, "err line longer than 160 characters\n") != NULL);
    CHECK(!g_tunnel.line_open);
}

TEST_CASE(an_overflow_shows_the_count_the_iomcu_dropped)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    /* More than the buffer in one go. */
    for (int i = 0; i < 40; ++i) {
        page_console_say(&g_console, "%0100d", i);
    }
    CHECK(g_console.dropped != 0u);
    run(600, true);
    read_out();
    CHECK(strstr(g_all, "err output full, ") != NULL);
    CHECK(strstr(g_all, " bytes dropped\n") != NULL);
    CHECK_EQ(g_console.used, 0u);
}

TEST_CASE(a_lost_reply_frame_costs_a_second_read_and_no_byte)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    page_console_say(&g_console, "%s", "one two three four five six seven eight nine ten eleven");
    /* Into the window; the poll sees it, then the read of it loses a frame. */
    run(10, true);
    g_drop_reply_at = 3;
    run(200, true);
    read_out();
    const char *first = strstr(g_all, "one two three");

    CHECK(first != NULL);
    CHECK(strstr(first + 1, "one two three") == NULL);
}

TEST_CASE(a_lost_ack_does_not_print_a_window_twice)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    page_console_say(&g_console, "%s", "only once");
    /* Pass by pass until the relay owes the acknowledgement; then it goes missing. */
    for (int i = 0; i < 50 && !g_relay.ack_owed; ++i) {
        run(1, true);
    }
    CHECK(g_relay.ack_owed);
    g_drop_reply_at = 0;
    run(40, true);
    read_out();
    const char *first = strstr(g_all, "only once");

    CHECK(first != NULL);
    CHECK(strstr(first + 1, "only once") == NULL);
    CHECK_EQ(g_tunnel.win_len, 0u);
}

TEST_CASE(an_iomcu_restart_inside_a_line_loses_that_line_and_says_so)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    char line[80];

    memset(line, ' ', sizeof(line));
    memcpy(line, "r 1 1", 5u);
    line[79] = '\0';
    type(line);
    /* Some of its chunks go, then the IOmcu starts again. */
    run(3, true);
    page_console_init(&g_console, &g_dev);
    console_tunnel_init(&g_tunnel, &g_console);
    run(40, true);
    read_out();
    CHECK(strstr(g_all, "err line lost: the IOmcu restarted inside it\n") != NULL);
    CHECK(strstr(g_all, "data page=0x01") == NULL);
    /* The next line runs. */
    type("r 1 1");
    run(40, true);
    read_out();
    CHECK(strstr(g_all, "data page=0x01 off=1 n=1: 0x1234\n") != NULL);
}

TEST_CASE(an_iomcu_restart_says_where_the_output_stands_and_its_first_lines_come)
{
    linked(true);
    page_console_say(&g_console, "%s", "before");
    run(60, true);
    read_out();
    g_all_len = 0u;
    page_console_init(&g_console, &g_dev);
    console_tunnel_init(&g_tunnel, &g_console);
    page_console_say(&g_console, "%s", "rcbench-iomcu: reset watchdog");
    run(60, true);
    read_out();
    CHECK(strstr(g_all, "console: the IOmcu's output restarted at 0") != NULL);
    CHECK(strstr(g_all, "rcbench-iomcu: reset watchdog\n") != NULL);
}

TEST_CASE(a_panel_restart_inside_a_line_leaves_nothing_of_it)
{
    linked(true);
    run(10, true);
    char line[80];

    memset(line, ' ', sizeof(line));
    memcpy(line, "r 1 1", 5u);
    line[79] = '\0';
    type(line);
    run(3, true);
    CHECK(g_tunnel.line_open);
    console_relay_init(&g_relay);
    g_all_len = 0u;
    run(10, true);
    type("r 1 2");
    run(40, true);
    read_out();
    CHECK(strstr(g_all, "data page=0x01 off=1 n=1:") == NULL);
    CHECK(strstr(g_all, "data page=0x01 off=2 n=1: 0x0000\n") != NULL);
}

TEST_CASE(link_down_and_up_are_lines_with_their_times_and_a_line_with_no_link_goes)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    run(1, false);
    const uint32_t down = g_now;

    type("r 1 1");
    const int before = g_exchanges;

    run(4, false);
    read_out();
    char want[64];

    snprintf(want, sizeof(want), "console: link down t_ms=%lu\n", (unsigned long)down);
    CHECK(strstr(g_all, want) != NULL);
    CHECK(strstr(g_all, "err no link: line dropped\n") != NULL);
    CHECK_EQ(g_exchanges, before);
    run(1, true);
    snprintf(want, sizeof(want), "console: link up t_ms=%lu\n", (unsigned long)g_now);
    read_out();
    CHECK(strstr(g_all, want) != NULL);
    CHECK_EQ(g_relay.state, CONSOLE_RELAY_UP);
}

TEST_CASE(a_link_that_falls_inside_a_line_says_the_line_is_lost)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    char line[80];

    memset(line, ' ', sizeof(line));
    memcpy(line, "r 1 1", 5u);
    line[79] = '\0';
    type(line);
    run(3, true);
    run(1, false);
    read_out();
    CHECK(strstr(g_all, "err line lost: the link went down inside it\n") != NULL);
}

TEST_CASE(an_iomcu_with_no_page_gets_one_line_and_a_look_every_5s)
{
    linked(false);
    run(10, true);
    read_out();
    CHECK_EQ(g_relay.state, CONSOLE_RELAY_NO_PAGE);
    const char *first = strstr(g_all, "console: the IOmcu has no console. Take the update");

    CHECK(first != NULL);
    CHECK(strstr(first + 30, "has no console") == NULL);
    type("r 1 1");
    run(1, true);
    read_out();
    CHECK(strstr(g_all, "err the IOmcu has no console\n") != NULL);
    const int before = g_exchanges;

    /* 10s: 2 looks; and still one line of it. */
    run(2000, true);
    read_out();
    CHECK(g_exchanges - before >= 1);
    CHECK(g_exchanges - before <= 3);
    first = strstr(g_all, "console: the IOmcu has no console. Take");
    CHECK(strstr(first + 1, "console: the IOmcu has no console. Take") == NULL);
}

TEST_CASE(an_update_to_a_console_image_finds_the_page_at_the_next_look)
{
    linked(false);
    run(10, true);
    CHECK_EQ(g_relay.state, CONSOLE_RELAY_NO_PAGE);
    link_dev_init(&g_dev, k_with, 2u, NULL, g_now);
    page_console_init(&g_console, &g_dev);
    console_tunnel_init(&g_tunnel, &g_console);
    run(1100, true);
    CHECK_EQ(g_relay.state, CONSOLE_RELAY_UP);
}

TEST_CASE(the_tunnel_refuses_what_is_no_frame_of_it)
{
    uint16_t v[4] = { 0u, 0u, 0u, 0u };

    up(true);
    v[0] = LINK_CN_IN_LEN_MASK;
    CHECK_EQ(console_tunnel_write(&g_tunnel, LINK_CN_W_IN, 4u, v), LINK_NACK_BAD_VALUE);
    v[0] = 2u;
    CHECK_EQ(console_tunnel_write(&g_tunnel, LINK_CN_W_IN, 4u, v), LINK_NACK_BAD_VALUE);
    CHECK_EQ(console_tunnel_write(&g_tunnel, 9u, 1u, v), LINK_NACK_BAD_RANGE);
    CHECK_EQ(console_tunnel_write(&g_tunnel, LINK_CN_W_ACK, 2u, v), LINK_NACK_BAD_RANGE);
    /* A chunk's bytes need the registers to hold them. */
    v[0] = (uint16_t)(LINK_CN_IN_START | 4u);
    CHECK_EQ(console_tunnel_write(&g_tunnel, LINK_CN_W_IN, 2u, v), LINK_NACK_BAD_VALUE);
    /* A line that runs past the console's is refused, and left. */
    v[0] = (uint16_t)(LINK_CN_IN_START | 6u);
    CHECK_EQ(console_tunnel_write(&g_tunnel, LINK_CN_W_IN, 4u, v), 0u);
    v[0] = 6u;
    uint8_t last = 0u;

    for (int i = 0; i < 30 && last == 0u; ++i) {
        last = console_tunnel_write(&g_tunnel, LINK_CN_W_IN, 4u, v);
    }
    CHECK_EQ(last, LINK_NACK_BAD_RANGE);
    CHECK(!g_tunnel.line_open);
    /* A wrong acknowledgement leaves the window. */
    console_tunnel_put(&g_tunnel, "abc", 3u);
    v[0] = 99u;
    CHECK_EQ(console_tunnel_write(&g_tunnel, LINK_CN_W_ACK, 1u, v), 0u);
    CHECK_EQ(g_tunnel.win_len, 3u);
    /* And a window taken only when empty. */
    CHECK_EQ(console_tunnel_room(&g_tunnel), 0u);
    console_tunnel_put(&g_tunnel, "z", 1u);
    CHECK_EQ(g_tunnel.win[0], 'a');
}

TEST_CASE(a_line_before_the_last_has_run_is_refused_busy_and_sent_again)
{
    linked(true);
    run(10, true);
    read_out();
    g_all_len = 0u;
    type("r 1 1");
    type("r 1 2");
    /* The IOmcu passes only now and then. */
    for (int i = 0; i < 400; ++i) {
        g_now += 5u;
        if (i % 8 == 0) {
            iomcu_pass();
        }
        panel_pass(true);
    }
    read_out();
    CHECK(strstr(g_all, "off=1 n=1: 0x1234\n") != NULL);
    CHECK(strstr(g_all, "off=2 n=1: 0x0000\n") != NULL);
}

int main(void)
{
    RUN(a_line_goes_over_the_page_and_its_output_comes_back);
    RUN(at_most_one_exchange_a_pass_and_a_poll_at_rest_is_rare);
    RUN(a_long_output_arrives_whole_and_in_order);
    RUN(a_long_line_comes_in_chunks_and_runs_at_its_end);
    RUN(a_line_too_long_says_so_and_sends_nothing);
    RUN(an_overflow_shows_the_count_the_iomcu_dropped);
    RUN(a_lost_reply_frame_costs_a_second_read_and_no_byte);
    RUN(a_lost_ack_does_not_print_a_window_twice);
    RUN(an_iomcu_restart_inside_a_line_loses_that_line_and_says_so);
    RUN(an_iomcu_restart_says_where_the_output_stands_and_its_first_lines_come);
    RUN(a_panel_restart_inside_a_line_leaves_nothing_of_it);
    RUN(link_down_and_up_are_lines_with_their_times_and_a_line_with_no_link_goes);
    RUN(a_link_that_falls_inside_a_line_says_the_line_is_lost);
    RUN(an_iomcu_with_no_page_gets_one_line_and_a_look_every_5s);
    RUN(an_update_to_a_console_image_finds_the_page_at_the_next_look);
    RUN(the_tunnel_refuses_what_is_no_frame_of_it);
    RUN(a_line_before_the_last_has_run_is_refused_busy_and_sent_again);
    return test_summary("console_tunnel");
}
