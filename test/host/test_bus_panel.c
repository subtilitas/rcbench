/*
 * The BUS page of the SERVO screen (shared/ui/bus_panel.c) end to end: the
 * page's taps, its commands through the panel's link module
 * (shared/bench/bus_link.c) to the coprocessor's page
 * (shared/outputs/bus_port.c) and the simulated Feetech servos of
 * feetech_sim.h, and what the page shows of their answers.  OPEN and CLOSE,
 * SCAN at the ends of the ID range, the keypad's limits, torque and goal in
 * both byte orders, an EEPROM write behind the 2 s hold and RESTORE newest
 * first, STOP during a scan and during an EEPROM write, an OPEN on an armed
 * bench, the page that goes away, a coprocessor without the page and a
 * command nobody answers.
 *
 * Every state is rendered as it goes, so a draw that reads past a table
 * falls over under the sanitizers.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "bus_link.h"
#include "bus_panel.h"
#include "bus_port.h"
#include "feetech_sim.h"
#include "servo_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

#define W 800
#define H 480
#define PORT 0u
#define PIN  13u

/* The SERVO screen's overlay, as servo_screen.c hands it over. */
static const gfx_rect_t AREA = { 6, 6, 488, 420 };

static gfx_color_t *fb;
static gfx_canvas_t cv;

static ftsim_bus_t sim;
static bus_port_t g_port;
static bus_link_t g_link;
static uint32_t now_us;
static uint32_t now_ms;
static bool g_armed;
static uint32_t g_stops, g_stops_sent;
static bool g_pending;
static bus_link_cmd_t g_cmd;
static bool g_deaf;           /* the control task loses every command */
static bool g_mute;           /* the coprocessor answers no exchange */
static unsigned g_commands;   /* commands the control task took */

static uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return port == PORT ? PIN : 0xFFu;
}

static bool hw_held(void *ctx, uint8_t port)
{
    (void)ctx;
    (void)port;
    return false;
}

static bool hw_take(void *ctx, uint8_t port, uint8_t pin, uint32_t baud)
{
    (void)ctx;
    (void)port;
    (void)pin;
    sim.baud = baud;
    return true;
}

static void hw_give(void *ctx)
{
    (void)ctx;
}

static void show(void)
{
    bus_panel_render(&cv);
}

static void fresh_minor(uint16_t minor)
{
    bus_port_hw_t hw;

    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);

    ftsim_init(&sim, 1000000u);
    memset(&hw, 0, sizeof(hw));
    hw.ports = 1;
    hw.pin = hw_pin;
    hw.held = hw_held;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.driver = ftsim_driver(&sim);
    now_us = 0u;
    now_ms = 1000u;
    g_armed = false;
    g_stops = g_stops_sent = 0u;
    g_pending = false;
    g_deaf = false;
    g_mute = false;
    g_commands = 0u;
    CHECK(bus_port_init(&g_port, &hw));
    bus_link_init(&g_link);
    bus_link_came_up(&g_link, minor);
    bus_panel_init(AREA);
    bus_panel_show(true);
}

static void fresh(void)
{
    fresh_minor(BUS_LINK_MINOR);
}

/* One exchange the link module owes, carried to the page. */
static bool exchange(void)
{
    bus_link_xfer_t x;
    uint16_t regs[32];

    if (!bus_link_next(&g_link, now_ms, &x)) {
        return false;
    }
    if (g_mute) {
        bus_link_done(&g_link, BUS_LINK_NO_ANSWER, NULL);
        return true;
    }
    if (x.write) {
        const uint8_t nack = bus_port_write(&g_port, x.off, x.n, x.regs);

        bus_link_done(&g_link, nack == 0u ? BUS_LINK_ACK : (int)nack, NULL);
        return true;
    }
    if (x.page == LINK_PAGE_BUS_IDS) {
        CHECK((unsigned)x.off + x.n <= LINK_BI_COUNT);
        bus_port_read_ids(&g_port, x.off, x.n, regs);
    } else {
        CHECK_EQ(x.page, LINK_PAGE_BUS);
        CHECK((unsigned)x.off + x.n <= LINK_BS_COUNT);
        bus_port_read(&g_port, x.off, x.n, regs);
    }
    bus_link_done(&g_link, BUS_LINK_ACK, regs);
    return true;
}

/*
 * A pass of 1 ms, as the firmware runs it: the page fed and its command
 * queued (render loop), the command and the STOP taken, up to 4 exchanges
 * (control task), 10 steps of the coprocessor's page.
 */
static void pass(void)
{
    bus_panel_feed_t f;
    bus_link_cmd_t c;

    memset(&f, 0, sizeof(f));
    bus_link_readout(&g_link, &f.rd);
    f.now_ms = now_ms;
    f.armed = g_armed;
    f.stops = g_stops;
    f.port = PORT;
    f.pin = PIN;
    bus_panel_feed(&f);
    if (!g_pending && bus_panel_take(&c)) {
        g_commands++;
        if (!g_deaf) {
            g_cmd = c;
            g_pending = true;
        }
    }
    if (g_pending && (bus_link_send(&g_link, &g_cmd)
                      || !bus_link_has_page(&g_link))) {
        g_pending = false;
    }
    if (g_stops != g_stops_sent) {
        g_stops_sent = g_stops;
        g_pending = false;
        bus_link_stop(&g_link);
    }
    for (int k = 0; k < 4 && exchange(); ++k) {
    }
    for (int k = 0; k < 10; ++k) {
        bus_port_step(&g_port, true, false, now_us);
        now_us += 100u;
    }
    now_ms += 1u;
}

static void run(unsigned ms)
{
    for (unsigned i = 0; i < ms; ++i) {
        pass();
    }
    show();
}

static void ev(int x, int y, touch_event_type_t t)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = 1, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };

    bus_panel_event(&e);
}

static int cx(gfx_rect_t r) { return r.x + r.w / 2; }
static int cy(gfx_rect_t r) { return r.y + r.h / 2; }

static void tap(gfx_rect_t r)
{
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    show();
}

static void tap_row(int row, int col)
{
    tap(bus_panel_value_rect(row, col));
}

static void key(ui_key_t k)
{
    tap(ui_keypad_key_rect(bus_panel_keypad(), k));
}

/* A number into the open keypad, and OK. */
static void enter(unsigned v)
{
    char d[8];

    snprintf(d, sizeof(d), "%u", v);
    key(UI_KEY_CLR);
    for (const char *p = d; *p != '\0'; ++p) {
        static const ui_key_t digit[10] = {
            UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
            UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
        };
        key(digit[*p - '0']);
    }
    key(UI_KEY_OK);
}

/* The entry of the open choice list whose label is @p label. */
static bool choose(const char *label)
{
    for (int i = 0; i < BUS_PANEL_CHOICES; ++i) {
        const char *l = bus_panel_choice_label(i);

        if (l != NULL && strcmp(l, label) == 0) {
            tap(bus_panel_choice_rect(i));
            return true;
        }
    }
    return false;
}

/* Hold the warning page's button for @p secs, the passes running, and let
 * go. */
static void hold(float secs)
{
    const gfx_rect_t r = bus_panel_apply_rect();
    const int n = (int)(secs * 39.0f + 0.5f);

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < n; ++i) {
        bus_panel_tick(1.0f / 39.0f);
        pass();
    }
    show();
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    show();
}

static bool page_says(int line, const char *text)
{
    return strstr(bus_panel_line(line), text) != NULL;
}

static uint8_t page_state(void)
{
    bus_link_readout_t r;

    bus_link_readout(&g_link, &r);
    return LINK_BS_STATE_OF(r.status[LINK_BS_STATE]);
}

static void open_bus(void)
{
    run(5);
    CHECK_STR_EQ(bus_panel_button(1, 1), "OPEN");
    tap_row(1, 1);
    run(300);
    CHECK_EQ(page_state(), LINK_BUS_OPEN);
    CHECK_STR_EQ(bus_panel_button(1, 1), "CLOSE");
    CHECK(bus_panel_holds_port());
}

static void scan(void)
{
    tap_row(3, 0);
    run(3000);
    CHECK_STR_EQ(bus_panel_button(3, 0), "SCAN");
}

/* Pick @p id in the ID list the scan left. */
static void pick_id(unsigned id)
{
    char l[8];

    tap_row(3, 1);
    snprintf(l, sizeof(l), "%u", id);
    CHECK(choose(l));
    run(300);
    CHECK_EQ(bus_panel_id(), (int)id);
}

/* EEPROM, @p item, the old value read; the editor open. */
static void eeprom_item(const char *item)
{
    tap_row(5, 1);
    CHECK_EQ(bus_panel_editor(), 2);
    CHECK(choose(item));
    run(300);
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_page_starts_closed_with_the_scan_range_1_to_20)
{
    fresh();
    run(5);
    CHECK_STR_EQ(bus_panel_button(0, 0), "FEETECH");
    CHECK_STR_EQ(bus_panel_button(0, 1), "1000000");
    CHECK_STR_EQ(bus_panel_button(1, 0), "STS/SMS");
    CHECK_STR_EQ(bus_panel_button(2, 0), "1");
    CHECK_STR_EQ(bus_panel_button(2, 1), "20");
    CHECK_STR_EQ(bus_panel_button(3, 1), "--");
    CHECK_STR_EQ(bus_panel_button(4, 1), "1000");
    CHECK_STR_EQ(bus_panel_button(5, 0), "0");
    /* It names the pin OPEN takes. */
    CHECK(page_says(0, "GP13"));
    CHECK(page_says(0, "OPEN"));
    /* Closed, nothing reaches the bus. */
    CHECK(!bus_panel_row_live(3, 0));
    CHECK(!bus_panel_row_live(4, 0));
    CHECK(!bus_panel_row_live(5, 1));
    CHECK(bus_panel_row_live(1, 1));
    CHECK(!bus_panel_holds_port());
    CHECK_EQ(sim.n_start, 0u);
}

TEST_CASE(open_names_the_pin_and_close_gives_it_back)
{
    fresh();
    open_bus();
    CHECK(page_says(0, "PROG PORT GP13: FEETECH at 1000000 baud."));
    tap_row(1, 1);
    run(300);
    CHECK_EQ(page_state(), LINK_BUS_CLOSED);
    CHECK_STR_EQ(bus_panel_button(1, 1), "OPEN");
    CHECK(!bus_panel_holds_port());
}

TEST_CASE(a_scan_finds_the_servos_and_offers_their_ids)
{
    fresh();
    (void)ftsim_add(&sim, 1u, FEETECH_STS);
    (void)ftsim_add(&sim, 5u, FEETECH_STS);
    (void)ftsim_add(&sim, 30u, FEETECH_STS);   /* outside 1 to 20 */
    open_bus();
    scan();
    CHECK(page_says(1, "SCAN 1 to 20: 2 found, 0 not decoded"));
    CHECK_STR_EQ(bus_panel_label(3, 0), "FOUND 2");
    tap_row(3, 1);
    CHECK_STR_EQ(bus_panel_choice_label(0), "1");
    CHECK_STR_EQ(bus_panel_choice_label(1), "5");
    CHECK_STR_EQ(bus_panel_choice_label(2), "OTHER ID");
    CHECK(bus_panel_choice_label(3) == NULL);
    CHECK(choose("5"));
    run(300);
    CHECK_EQ(bus_panel_id(), 5);
    /* The watch: 7.4 V and 31 C from the simulated servo, no flags. */
    CHECK(page_says(1, "ID 5"));
    CHECK(page_says(1, "7.4 V"));
    CHECK(page_says(1, "31 C"));
    CHECK(page_says(1, "mA"));          /* STS has a current register */
    CHECK_STR_EQ(bus_panel_line(2), "FLAGS: none");
}

TEST_CASE(a_scan_reaches_both_ends_of_the_id_range)
{
    fresh();
    (void)ftsim_add(&sim, 0u, FEETECH_STS);
    (void)ftsim_add(&sim, 253u, FEETECH_STS);
    open_bus();
    tap_row(2, 0);
    enter(0);
    tap_row(2, 1);
    enter(253);
    CHECK_STR_EQ(bus_panel_button(2, 0), "0");
    CHECK_STR_EQ(bus_panel_button(2, 1), "253");
    tap_row(3, 0);
    run(20000);
    CHECK(page_says(1, "SCAN 0 to 253: 2 found"));
    tap_row(3, 1);
    CHECK_STR_EQ(bus_panel_choice_label(0), "0");
    CHECK_STR_EQ(bus_panel_choice_label(1), "253");
}

TEST_CASE(the_keypad_keeps_from_at_most_to_and_refuses_254)
{
    fresh();
    run(5);
    tap_row(2, 0);
    enter(30);
    CHECK_STR_EQ(bus_panel_button(2, 0), "30");
    CHECK_STR_EQ(bus_panel_button(2, 1), "30");   /* TO follows */
    tap_row(2, 1);
    enter(10);
    CHECK_STR_EQ(bus_panel_button(2, 0), "10");   /* FROM follows */
    CHECK_STR_EQ(bus_panel_button(2, 1), "10");
    /* 254 is the broadcast ID's neighbour and not a servo's: refused, the
     * keypad stays open with the old value kept. */
    tap_row(2, 1);
    enter(254);
    CHECK_EQ(bus_panel_editor(), 1);
    CHECK_STR_EQ(bus_panel_button(2, 1), "10");
    key(UI_KEY_CANCEL);
    CHECK_EQ(bus_panel_editor(), 0);
    /* An ID above 253 neither. */
    tap_row(3, 1);
    enter(254);
    CHECK_EQ(bus_panel_editor(), 1);
    key(UI_KEY_CANCEL);
    CHECK_EQ(bus_panel_id(), -1);
}

TEST_CASE(torque_and_goal_reach_an_sts_servo_little_endian)
{
    ftsim_servo_t *v;

    fresh();
    v = ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    scan();
    pick_id(1);
    tap_row(4, 0);
    run(300);
    CHECK_EQ(v->regs[FEETECH_REG_TORQUE], 1);
    CHECK_STR_EQ(bus_panel_button(4, 0), "ON");
    /* The slider's right end: 4095, at speed 1000, acceleration 0. */
    {
        const gfx_rect_t s = bus_panel_slider_rect();

        ev(s.x + s.w - 1, cy(s), TOUCH_EVENT_DOWN);
        ev(s.x + s.w - 1, cy(s), TOUCH_EVENT_UP);
    }
    run(300);
    CHECK_EQ(v->regs[FEETECH_REG_GOAL], 0xFF);
    CHECK_EQ(v->regs[FEETECH_REG_GOAL + 1], 0x0F);
    CHECK_EQ(v->regs[FEETECH_REG_SPEED], 1000u & 0xFFu);
    CHECK_EQ(v->regs[FEETECH_REG_SPEED + 1], 1000u >> 8);
    CHECK_EQ(v->regs[FEETECH_REG_ACC], 0);
    tap_row(4, 0);
    run(300);
    CHECK_EQ(v->regs[FEETECH_REG_TORQUE], 0);
    CHECK_STR_EQ(bus_panel_button(4, 0), "OFF");
}

TEST_CASE(an_scs_servo_takes_its_goal_big_endian_and_has_no_acc)
{
    ftsim_servo_t *v;

    fresh();
    v = ftsim_add(&sim, 3u, FEETECH_SCS);
    open_bus();
    scan();
    pick_id(3);
    tap_row(1, 0);
    CHECK_EQ(bus_panel_editor(), 2);
    CHECK(choose("SCS"));
    run(300);
    CHECK_EQ(bus_panel_series(), 0);
    CHECK_STR_EQ(bus_panel_button(5, 0), "--");
    CHECK(!bus_panel_row_live(5, 0));
    CHECK(!page_says(1, "mA"));         /* SCS has no current register */
    {
        const gfx_rect_t s = bus_panel_slider_rect();

        ev(s.x + s.w - 1, cy(s), TOUCH_EVENT_DOWN);
        ev(s.x + s.w - 1, cy(s), TOUCH_EVENT_UP);
    }
    run(300);
    /* 1023, high byte first. */
    CHECK_EQ(v->regs[FEETECH_REG_GOAL], 0x03);
    CHECK_EQ(v->regs[FEETECH_REG_GOAL + 1], 0xFF);
}

TEST_CASE(an_eeprom_write_waits_for_the_2_s_hold_and_keeps_the_old_value)
{
    ftsim_servo_t *v;

    fresh();
    v = ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    scan();
    pick_id(1);
    eeprom_item("MIN ANGLE");
    CHECK_EQ(bus_panel_editor(), 1);    /* the keypad, at the old value */
    enter(100);
    CHECK_EQ(bus_panel_editor(), 3);
    CHECK_STR_EQ(bus_panel_warning_line(0), "ID 1, MIN ANGLE: 0 to 100");
    /* Let go after 1 s: nothing is written. */
    hold(1.0f);
    run(300);
    CHECK_EQ(bus_panel_editor(), 3);
    CHECK_EQ(v->regs[FEETECH_REG_MIN_ANGLE], 0);
    hold(2.2f);
    run(300);
    CHECK_EQ(bus_panel_editor(), 0);
    CHECK_EQ(v->regs[FEETECH_REG_MIN_ANGLE], 100);
    CHECK_EQ(v->regs[FEETECH_REG_MIN_ANGLE + 1], 0);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);    /* locked again */
    CHECK_EQ(bus_panel_backups(), 1u);
    CHECK(page_says(3, "Written and read back."));
}

TEST_CASE(cancel_on_the_warning_writes_nothing)
{
    ftsim_servo_t *v;

    fresh();
    v = ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    scan();
    pick_id(1);
    eeprom_item("MAX ANGLE");
    enter(3000);
    CHECK_EQ(bus_panel_editor(), 3);
    tap(bus_panel_cancel_rect());
    run(300);
    CHECK_EQ(bus_panel_editor(), 0);
    CHECK_EQ(feetech_get16(FEETECH_STS, &v->regs[FEETECH_REG_MAX_ANGLE]),
             4095u);
    CHECK_EQ(bus_panel_backups(), 0u);
}

TEST_CASE(restore_writes_back_newest_first_across_an_id_change)
{
    ftsim_servo_t *v;

    fresh();
    v = ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    scan();
    pick_id(1);
    eeprom_item("MIN ANGLE");
    enter(100);
    hold(2.2f);
    run(300);
    eeprom_item("ID");
    enter(9);
    CHECK(strstr(bus_panel_warning_line(2), "ID 9") != NULL);
    hold(2.2f);
    run(300);
    CHECK_EQ(v->regs[FEETECH_REG_ID], 9);
    CHECK_EQ(bus_panel_id(), 9);        /* the selection follows */
    CHECK_EQ(bus_panel_backups(), 2u);
    /* RESTORE 2: the ID first, then the angle on ID 1 again. */
    tap_row(5, 1);
    CHECK(choose("RESTORE 2"));
    CHECK_EQ(bus_panel_editor(), 3);
    CHECK_STR_EQ(bus_panel_warning_line(0), "ID 9, ID back to 1");
    CHECK_STR_EQ(bus_panel_warning_line(1), "ID 1, MIN ANGLE back to 0");
    hold(2.2f);
    run(1000);
    CHECK_EQ(v->regs[FEETECH_REG_ID], 1);
    CHECK_EQ(v->regs[FEETECH_REG_MIN_ANGLE], 0);
    CHECK_EQ(bus_panel_backups(), 0u);
    CHECK_EQ(bus_panel_id(), 1);
    CHECK(page_says(3, "Restored."));
}

TEST_CASE(a_full_backup_list_asks_for_restore_first)
{
    fresh();
    (void)ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    scan();
    pick_id(1);
    for (unsigned k = 0; k < BUS_PANEL_BACKUPS; ++k) {
        eeprom_item("MAX ANGLE");
        enter(4000u - k);
        hold(2.2f);
        run(300);
    }
    CHECK_EQ(bus_panel_backups(), BUS_PANEL_BACKUPS);
    eeprom_item("MAX ANGLE");
    CHECK_EQ(bus_panel_editor(), 0);
    CHECK(page_says(3, "8 changes kept: RESTORE first."));
}

TEST_CASE(stop_during_a_scan_ends_it_and_sends_torque_off)
{
    fresh();
    (void)ftsim_add(&sim, 200u, FEETECH_STS);
    open_bus();
    tap_row(2, 0);
    enter(0);
    tap_row(2, 1);
    enter(253);
    tap_row(3, 0);
    run(100);
    CHECK_STR_EQ(bus_panel_button(3, 0), "STOP");
    sim.n_log = 0;
    g_stops++;
    run(300);
    CHECK_STR_EQ(bus_panel_button(3, 0), "SCAN");
    CHECK(page_says(3, "STOP: bus traffic ended, torque off sent."));
    {
        bool off = false;

        for (unsigned i = 0; i < sim.n_log; ++i) {
            off = off || ftsim_is_write1(&sim.log[i], FEETECH_ID_BROADCAST,
                                       FEETECH_REG_TORQUE, 0u);
        }
        CHECK(off);
    }
    /* The scan did not reach ID 200, and the page takes a command again. */
    CHECK(!page_says(1, "1 found"));
    CHECK(bus_panel_row_live(3, 0));
    CHECK_EQ(page_state(), LINK_BUS_OPEN);
}

TEST_CASE(stop_during_an_eeprom_write_leaves_the_page_usable)
{
    ftsim_servo_t *v;

    fresh();
    v = ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    scan();
    pick_id(1);
    eeprom_item("MIN ANGLE");
    enter(50);
    /* The hold completes; STOP in the same millisecond the command goes. */
    {
        const gfx_rect_t r = bus_panel_apply_rect();

        ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
        for (int i = 0; i < 90 && bus_panel_editor() == 3; ++i) {
            bus_panel_tick(1.0f / 39.0f);
        }
        ev(cx(r), cy(r), TOUCH_EVENT_UP);
    }
    CHECK_EQ(bus_panel_editor(), 0);
    pass();                             /* the command is taken */
    g_stops++;
    run(500);
    CHECK(page_says(3, "STOP"));
    CHECK(bus_panel_row_live(5, 1));    /* EEPROM again */
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
    /* Whether the write landed or not, the old value is kept. */
    CHECK(bus_panel_backups() <= 1u);
    if (v->regs[FEETECH_REG_MIN_ANGLE] == 50) {
        CHECK_EQ(bus_panel_backups(), 1u);
    }
}

TEST_CASE(stop_drops_a_command_not_sent_and_the_warning)
{
    fresh();
    (void)ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    scan();
    pick_id(1);
    eeprom_item("MIN ANGLE");
    enter(70);
    CHECK_EQ(bus_panel_editor(), 3);
    g_stops++;
    run(50);
    CHECK_EQ(bus_panel_editor(), 0);
    CHECK_EQ(bus_panel_backups(), 0u);
    CHECK(page_says(3, "STOP"));
}

TEST_CASE(an_armed_bench_gets_no_open)
{
    fresh();
    g_armed = true;
    run(5);
    tap_row(1, 1);
    run(300);
    CHECK_EQ(page_state(), LINK_BUS_CLOSED);
    CHECK_EQ(g_commands, 0u);
    CHECK(page_says(3, "Refused: bench ARMED, switch to SAFE first."));
    CHECK(!bus_panel_holds_port());
}

TEST_CASE(a_page_not_shown_closes_the_bus_with_torque_off)
{
    fresh();
    (void)ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    scan();
    pick_id(1);
    tap_row(4, 0);
    run(300);
    sim.n_log = 0;
    bus_panel_leave();
    run(500);
    CHECK_EQ(page_state(), LINK_BUS_CLOSED);
    CHECK(!bus_panel_holds_port());
    {
        bool off = false;

        for (unsigned i = 0; i < sim.n_log; ++i) {
            off = off || ftsim_is_write1(&sim.log[i], FEETECH_ID_BROADCAST,
                                       FEETECH_REG_TORQUE, 0u);
        }
        CHECK(off);
    }
}

TEST_CASE(leaving_during_a_scan_closes_the_bus)
{
    fresh();
    open_bus();
    tap_row(2, 0);
    enter(0);
    tap_row(2, 1);
    enter(253);
    tap_row(3, 0);
    run(100);
    bus_panel_show(false);
    run(1000);
    CHECK_EQ(page_state(), LINK_BUS_CLOSED);
}

TEST_CASE(a_coprocessor_without_the_page_is_named_and_sent_nothing)
{
    fresh_minor(LINK_MINOR_BUS - 1u);
    run(5);
    CHECK_STR_EQ(bus_panel_line(0),
                 "Coprocessor without BUS page: flash both from one release.");
    CHECK(!bus_panel_row_live(1, 1));
    tap_row(1, 1);
    run(100);
    CHECK_EQ(g_commands, 0u);
}

TEST_CASE(a_link_down_is_named)
{
    fresh();
    bus_link_lost(&g_link);
    run(5);
    CHECK_STR_EQ(bus_panel_line(0), "No coprocessor answers: no bus.");
}

TEST_CASE(a_command_the_control_task_never_takes_times_out_after_3_s)
{
    fresh();
    run(5);
    g_deaf = true;
    tap_row(1, 1);
    run(BUS_PANEL_ANSWER_MS - 100u);
    CHECK_EQ(g_commands, 1u);
    CHECK(!bus_panel_row_live(1, 1));   /* still waiting */
    run(200);
    CHECK(page_says(3, "The coprocessor did not answer."));
    CHECK(bus_panel_row_live(1, 1));
}

TEST_CASE(a_coprocessor_that_stops_answering_ends_the_command)
{
    fresh();
    run(5);
    g_mute = true;
    tap_row(1, 1);
    run(300);
    CHECK(page_says(3, "The coprocessor did not answer."));
}

TEST_CASE(the_baud_list_has_the_eight_feetech_rates)
{
    static const char *const rates[8] = {
        "1000000", "500000", "250000", "128000", "115200", "76800", "57600",
        "38400",
    };

    fresh();
    run(5);
    tap_row(0, 1);
    for (int i = 0; i < 8; ++i) {
        CHECK_STR_EQ(bus_panel_choice_label(i), rates[i]);
    }
    CHECK(bus_panel_choice_label(8) == NULL);
    CHECK(choose("115200"));
    CHECK_STR_EQ(bus_panel_button(0, 1), "115200");
    open_bus();
    CHECK_EQ(sim.baud, 115200u);
    CHECK(page_says(0, "at 115200 baud."));
}

TEST_CASE(the_german_page_keeps_the_protocol_names)
{
    fresh();
    open_bus();
    ui_text_set_language(UI_LANG_DE);
    run(5);
    CHECK(page_says(0, "PROG-PORT GP13: FEETECH mit 1000000 Baud."));
    CHECK_STR_EQ(bus_panel_button(1, 1), "SCHLIESSEN");
    tap_row(1, 0);
    CHECK_STR_EQ(bus_panel_choice_label(0), "SCS");
    CHECK_STR_EQ(bus_panel_choice_label(1), "STS/SMS");
    tap(bus_panel_choice_cancel_rect());
    ui_text_set_language(UI_LANG_EN);
}

/* The SETTINGS button on the SERVO screen's right card, and the overlay's
 * five tabs from x 16, 368 wide, at y 14 to 40. */
#define SETB_X 734
#define BTN_Y  24
#define TAB_Y  27
static int tab_x(int i) { return 16 + 368 * (2 * i + 1) / 10; }

static void screen_tap(const ui_screen_t *s, int x, int y)
{
    const touch_event_t d = { .type = TOUCH_EVENT_DOWN,
                              .point = { .id = 1, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    touch_event_t u = d;

    u.type = TOUCH_EVENT_UP;
    s->event(&d);
    s->event(&u);
    s->tick(0.02f);
    s->render(&cv, 0);
}

TEST_CASE(the_servo_screen_holds_the_bus_open_only_on_its_bus_tab)
{
    const ui_screen_t *s;

    fresh();
    settings_set_store(NULL);
    settings_init();
    supply_screen()->reset();
    supply_screen_settings_loaded();
    s = servo_screen();
    s->reset();
    s->enter();
    run(5);
    CHECK(!bus_panel_is_shown());
    screen_tap(s, SETB_X, BTN_Y);
    screen_tap(s, tab_x(4), TAB_Y);
    CHECK(bus_panel_is_shown());
    {
        const gfx_rect_t r = bus_panel_value_rect(1, 1);

        screen_tap(s, cx(r), cy(r));
    }
    run(300);
    s->tick(0.02f);
    s->render(&cv, 0);
    CHECK_EQ(page_state(), LINK_BUS_OPEN);
    CHECK(bus_panel_holds_port());
    /* No pulse is on the port the bus holds. */
    CHECK_STR_EQ(servo_screen_commanded_text(), "---");
    /* Another tab: the page is not shown, and the bus closes. */
    screen_tap(s, tab_x(1), TAB_Y);
    CHECK(!bus_panel_is_shown());
    run(500);
    CHECK_EQ(page_state(), LINK_BUS_CLOSED);
    /* Back on BUS, then off the screen: closed again. */
    screen_tap(s, tab_x(4), TAB_Y);
    {
        const gfx_rect_t r = bus_panel_value_rect(1, 1);

        screen_tap(s, cx(r), cy(r));
    }
    run(300);
    CHECK_EQ(page_state(), LINK_BUS_OPEN);
    s->leave();
    run(500);
    CHECK_EQ(page_state(), LINK_BUS_CLOSED);
}

int main(void)
{
    RUN(the_page_starts_closed_with_the_scan_range_1_to_20);
    RUN(open_names_the_pin_and_close_gives_it_back);
    RUN(a_scan_finds_the_servos_and_offers_their_ids);
    RUN(a_scan_reaches_both_ends_of_the_id_range);
    RUN(the_keypad_keeps_from_at_most_to_and_refuses_254);
    RUN(torque_and_goal_reach_an_sts_servo_little_endian);
    RUN(an_scs_servo_takes_its_goal_big_endian_and_has_no_acc);
    RUN(an_eeprom_write_waits_for_the_2_s_hold_and_keeps_the_old_value);
    RUN(cancel_on_the_warning_writes_nothing);
    RUN(restore_writes_back_newest_first_across_an_id_change);
    RUN(a_full_backup_list_asks_for_restore_first);
    RUN(stop_during_a_scan_ends_it_and_sends_torque_off);
    RUN(stop_during_an_eeprom_write_leaves_the_page_usable);
    RUN(stop_drops_a_command_not_sent_and_the_warning);
    RUN(an_armed_bench_gets_no_open);
    RUN(a_page_not_shown_closes_the_bus_with_torque_off);
    RUN(leaving_during_a_scan_closes_the_bus);
    RUN(a_coprocessor_without_the_page_is_named_and_sent_nothing);
    RUN(a_link_down_is_named);
    RUN(a_command_the_control_task_never_takes_times_out_after_3_s);
    RUN(a_coprocessor_that_stops_answering_ends_the_command);
    RUN(the_baud_list_has_the_eight_feetech_rates);
    RUN(the_german_page_keeps_the_protocol_names);
    RUN(the_servo_screen_holds_the_bus_open_only_on_its_bus_tab);
    free(fb);
    return test_summary("bus_panel");
}
