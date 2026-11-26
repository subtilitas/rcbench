/**
 * @file test_wireless_printer.c
 * @brief SETUP > WIRELESS > PRINTER (wireless_printer.c): the entry and
 *        its rows with no printer, a bound one idle, a roll read, NO LABEL
 *        and a job in work; SEARCH fed by the controller's own scan lists
 *        only the A4:93:40 printers and a tap binds one; UNBIND let go at
 *        1.9s keeps the bind, held to 2s clears it; scan, bind and unbind
 *        refused while a job or a device update runs, the scan also while
 *        ARMED; Read printer and Test label with their locks; the QUERY
 *        when the entry opens; the tick's band notices; a BIND reads the
 *        roll at once unless it is known, after a request in work, never
 *        while ARMED; SEARCH marks a bound address SETUP holds in small
 *        letters.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "settings_rig.h"
#include "wl_bind_rig.h"

#include "print_ctl.h"
#include "print_rig.h"
#include "wireless_printer.h"

/** @brief The controller's rig, too big for a stack frame. */
static pr_rig_t s_r;

/** @brief Fail the case and return when @p c is false. */
#define REQUIRE(c)                                                         \
    do {                                                                   \
        if (!(c)) {                                                        \
            T_FAIL("required: %s", #c);                                    \
            return;                                                        \
        }                                                                  \
    } while (0)

/** @brief The printer the cases bind. @{ */
#define ADDR_A "A4:93:40:30:FA:71 P"
#define NAME_A "T0205C2608247368"
/** @} */

/** @brief The bench ARMED or SAFE, on the router's status. */
static void set_armed(bool armed)
{
    ui_bench_status_t st;
    memset(&st, 0, sizeof st);
    st.armed = armed;
    ui_router_set_status(&st);
}

/** @brief The requests the screen posted, dropped; the count. */
static int drain(void)
{
    print_req_t q;
    int n = 0;
    while (print_ctl_take(&q)) {
        ++n;
    }
    return n;
}

/** @brief The request the screen posted, PRINT_REQ_NONE for none. */
static uint8_t taken(void)
{
    print_req_t q;
    return print_ctl_take(&q) ? q.kind : (uint8_t)PRINT_REQ_NONE;
}

/** @brief The view @p v kept for the frame task with a rev of its own,
 *         as the controller counts each change, and a frame's tick. */
static void show(const print_view_t *v)
{
    static uint32_t rev;
    print_view_t c = *v;
    c.rev = ++rev;
    print_ctl_show(&c);
    wireless_printer_tick();
    ui_router_tick(0.02f);
}

/** @brief A view of the printer bound, idle, nothing read. */
static print_view_t idle_view(void)
{
    print_view_t v;
    memset(&v, 0, sizeof v);
    v.bound = true;
    v.left = -1;
    snprintf(v.addr, sizeof v.addr, "%s", ADDR_A);
    snprintf(v.name, sizeof v.name, "%s", NAME_A);
    snprintf(v.printer, sizeof v.printer, "%s", NAME_A);
    return v;
}

/** @brief SETUP's printer: @p addr and @p name, "" none. */
static void bind_to(const char *addr, const char *name)
{
    settings_set_text(SET_TEXT_PRN_ADDR, addr);
    settings_set_text(SET_TEXT_PRN_NAME, name);
}

/** @brief SETUP > WIRELESS opened on PRINTER by a tap, SAFE, English,
 *         @p bound with ADDR_A or none, the view all zero; the requests
 *         the entry's opening posted dropped. */
static void open_printer(bool bound)
{
    fresh_hub();
    ui_text_set_language(UI_LANG_EN);
    set_armed(false);
    print_ctl_reset();
    wireless_printer_reset();
    bind_to(bound ? ADDR_A : "", bound ? NAME_A : "");
    open_category(SET_CAT_WIRELESS);
    tap(entry_x(WL_ENTRY_PRINTER), entry_y(WL_ENTRY_PRINTER));
    (void)drain();
    wireless_printer_tick();
    ui_router_set_alert("");
}

/** @brief A tap on the PRINTER entry's row @p c. */
static void tap_row(const ui_setlist_custom_t *c)
{
    int x, y;
    wbr_row_mid(WL_ENTRY_PRINTER, c, &x, &y);
    tap(x, y);
}

/** @brief The entry's line on the left list. */
static const char *entry_line(void)
{
    static char buf[64];
    gfx_color_t ink;
    return wireless_pane_state(WL_ENTRY_PRINTER, buf, sizeof buf, &ink);
}

TEST_CASE(the_entry_holds_eight_rows_and_says_not_bound)
{
    open_printer(false);
    CHECK_EQ(wireless_pane_selected(), WL_ENTRY_PRINTER);
    wl_row_t rows[WL_ENTRY_ROWS_MAX];
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_PRINTER, rows, WL_ENTRY_ROWS_MAX),
             WL_PRINTER_ROWS);
    CHECK(rows[0].custom == &wireless_printer_scan_row);
    CHECK(rows[1].custom == &wireless_printer_bind_row);
    CHECK(rows[7].custom == &wireless_printer_test_row);
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_PRINTER, NULL, 0), WL_PRINTER_ROWS);
    char name[32];
    CHECK_STR_EQ(wireless_pane_name(WL_ENTRY_PRINTER, name, sizeof name),
                 "PRINTER");
    CHECK_STR_EQ(entry_line(), "NOT BOUND");
    CHECK_STR_EQ(wbr_value(&wireless_printer_scan_row), "SCAN");
    CHECK_STR_EQ(wbr_value(&wireless_printer_bind_row), "NONE");
    CHECK_STR_EQ(wbr_value(&wireless_printer_addr_row), "--");
    CHECK_STR_EQ(wbr_value(&wireless_printer_state_row), "NOT BOUND");
    CHECK_STR_EQ(wbr_value(&wireless_printer_roll_row), "--");
    CHECK_STR_EQ(wbr_value(&wireless_printer_left_row), "--");
    CHECK_STR_EQ(wireless_printer_query_row.locked(),
                 ui_tr(TX_PRN_LOCK_UNBOUND_HERE));
    CHECK_STR_EQ(wireless_printer_test_row.locked(),
                 ui_tr(TX_PRN_LOCK_UNBOUND_HERE));
    /* RESET keeps the bind; the pane holds no setting of the printer */
    CHECK_EQ(wireless_pane_entry_of(SET_DIAL_EN), WL_ENTRY_DIAL);
    bind_to(ADDR_A, NAME_A);
    wireless_pane_reset(WL_ENTRY_PRINTER);
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), ADDR_A);
}

TEST_CASE(the_rows_follow_the_view)
{
    open_printer(true);
    print_view_t v = idle_view();
    show(&v);
    CHECK_STR_EQ(entry_line(), NAME_A);
    CHECK_STR_EQ(wbr_value(&wireless_printer_bind_row), NAME_A);
    CHECK_STR_EQ(wbr_value(&wireless_printer_addr_row), ADDR_A);
    CHECK_STR_EQ(wbr_value(&wireless_printer_state_row), "IDLE");
    CHECK_STR_EQ(wbr_value(&wireless_printer_query_row), NAME_A);
    CHECK_STR_EQ(wbr_value(&wireless_printer_test_row), NAME_A);
    CHECK(wireless_printer_query_row.locked() == NULL);
    CHECK(wireless_printer_test_row.locked() == NULL);
    /* a name that never came: the address stands for it */
    settings_set_text(SET_TEXT_PRN_NAME, "");
    CHECK_STR_EQ(wbr_value(&wireless_printer_bind_row), ADDR_A);
    settings_set_text(SET_TEXT_PRN_NAME, NAME_A);
    /* a QUERY in work, then its roll */
    v.kind = PRINT_REQ_QUERY;
    v.phase = PRINT_READING;
    v.linked = true;
    ++v.jobs;
    show(&v);
    CHECK_STR_EQ(wbr_value(&wireless_printer_state_row), "READING ROLL");
    CHECK_STR_EQ(entry_line(), "READING ROLL");
    CHECK_STR_EQ(wireless_printer_query_row.locked(),
                 ui_tr(TX_PRN_LOCK_BUSY));
    v.phase = PRINT_DONE;
    v.w_mm = 40u;
    v.h_mm = 30u;
    v.gap_mm = 3u;
    v.left = 293;
    show(&v);
    CHECK_STR_EQ(wbr_value(&wireless_printer_state_row), "ROLL READ");
    CHECK_STR_EQ(wbr_value(&wireless_printer_roll_row), "40x30mm GAP 3mm");
    CHECK_STR_EQ(wbr_value(&wireless_printer_left_row), "293");
    /* the test label in work, then the link alone */
    v.kind = PRINT_REQ_TEST;
    v.phase = PRINT_SENDING;
    v.frame = 3u;
    v.frames = 5u;
    show(&v);
    CHECK_STR_EQ(wbr_value(&wireless_printer_state_row), "SENDING 3/5");
    v.phase = PRINT_IDLE;
    v.kind = PRINT_REQ_NONE;
    show(&v);
    CHECK_STR_EQ(wbr_value(&wireless_printer_state_row), "LINK UP");
    /* NO LABEL: the roll reads it */
    v.kind = PRINT_REQ_QUERY;
    v.phase = PRINT_FAILED;
    v.err = PRINT_ERR_NO_LABEL;
    v.w_mm = 0u;
    v.h_mm = 0u;
    ++v.jobs;
    show(&v);
    CHECK_STR_EQ(wbr_value(&wireless_printer_roll_row), "NO LABEL");
    CHECK_STR_EQ(wbr_value(&wireless_printer_state_row), "NO LABEL");
    CHECK_STR_EQ(wbr_band(), "NO LABEL");
    /* a bound printer out of range */
    v.err = PRINT_ERR_NOT_FOUND;
    v.linked = false;
    ++v.jobs;
    show(&v);
    CHECK_STR_EQ(wbr_value(&wireless_printer_state_row),
                 "PRINTER NOT FOUND");
    CHECK_STR_EQ(wbr_band(), "PRINTER NOT FOUND");
}

TEST_CASE(search_lists_only_the_ouis_printers_and_a_tap_binds)
{
    open_printer(false);
    /* The controller scans: two printers, a device of another maker and
     * a random address with the printer's prefix. */
    pr_rig_t *r = &s_r;
    pr_init(r);
    print_ctl_bind(&r->c, "", "", r->now);
    tap_row(&wireless_printer_scan_row);
    CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
    print_req_t q;
    REQUIRE(print_ctl_take(&q));
    CHECK_EQ(q.kind, PRINT_REQ_SCAN);
    CHECK(print_ctl_request(&r->c, &q, r->now));
    for (int k = 0; k < 5; ++k) {
        pr_ms(r);
    }
    pr_adv(r, ADDR_A, NAME_A, -55);
    pr_adv(r, "A4:93:40:00:00:01 P", "T0205C2600000001", -70);
    pr_adv(r, "11:22:33:44:55:66 P", NAME_A, -40);
    pr_adv(r, "A4:93:40:30:FA:72 R", NAME_A, -45);
    for (int k = 0; k < 10100; ++k) {
        pr_ms(r);
    }
    print_view_t v;
    print_ctl_view_of(&r->c, &v);
    show(&v);
    wls_view_t w;
    wl_search_view(&w);
    CHECK(w.scan_done);
    CHECK_EQ(w.found_n, 2u);
    CHECK_STR_EQ(w.found[0].addr, ADDR_A);
    CHECK_STR_EQ(w.found[1].addr, "A4:93:40:00:00:01 P");
    CHECK_STR_EQ(w.bound_addr, "");
    /* the second tapped, then BIND */
    gfx_rect_t row;
    REQUIRE(wbr_found(1, &row));
    tap(row.x + row.w / 2, row.y + row.h / 2);
    REQUIRE(wl_search_asking());
    const gfx_rect_t b = wbr_part(WLS_PART_BIND);
    tap(b.x + b.w / 2, b.y + b.h / 2);
    CHECK(!wl_search_asking());
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), "A4:93:40:00:00:01 P");
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_NAME), "T0205C2600000001");
    CHECK(settings_save_asked());
    wl_search_view(&w);
    CHECK_STR_EQ(w.bound_addr, "A4:93:40:00:00:01 P");
    CHECK_STR_EQ(wbr_value(&wireless_printer_bind_row), "T0205C2600000001");
}

TEST_CASE(unbind_at_1900ms_keeps_the_bind_and_at_2000ms_clears_it)
{
    open_printer(true);
    print_view_t v = idle_view();
    show(&v);
    tap_row(&wireless_printer_bind_row);
    CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
    CHECK_EQ(taken(), PRINT_REQ_NONE);   /* no scan from the bind row */
    REQUIRE(wl_search_unbind_ready());
    const gfx_rect_t r = wbr_part(WLS_PART_UNBIND);
    const int x = r.x + r.w / 2, y = r.y + r.h / 2;
    wbr_down(x, y);
    for (int k = 0; k < 7; ++k) {
        ui_router_tick(0.25f);
    }
    ui_router_tick(0.15f);
    wbr_up(x, y);
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), ADDR_A);
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_NAME), NAME_A);
    wbr_hold_unbind(NULL, 2000u);
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), "");
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_NAME), "");
    CHECK(!wl_search_unbind_ready());
}

TEST_CASE(search_bind_and_unbind_are_refused_during_a_job_and_an_update)
{
    static const struct {
        bool    job;       /**< a job in work, else an update */
        ui_text_id_t scan; /**< the scan's refusal */
        ui_text_id_t bind; /**< the bind's */
    } k[] = {
        { true,  TX_PRN_SCAN_JOB, TX_PRN_BIND_JOB },
        { false, TX_PRN_SCAN_UPD, TX_PRN_BIND_UPD },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        open_printer(true);
        print_view_t v = idle_view();
        v.scan_done = true;
        v.found_n = 2u;
        snprintf(v.found[0].addr, sizeof v.found[0].addr, "%s", ADDR_A);
        snprintf(v.found[1].addr, sizeof v.found[1].addr,
                 "A4:93:40:00:00:01 P");
        if (k[i].job) {
            v.kind = PRINT_REQ_LABEL;
            v.phase = PRINT_SENDING;
        } else {
            v.update = true;
        }
        show(&v);
        ui_router_set_alert("");
        tap_row(&wireless_printer_scan_row);
        CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
        CHECK_STR_EQ(wbr_band(), ui_tr(k[i].scan));
        CHECK_EQ(taken(), PRINT_REQ_NONE);
        gfx_rect_t row;
        REQUIRE(wbr_found(1, &row));
        tap(row.x + row.w / 2, row.y + row.h / 2);
        const gfx_rect_t b = wbr_part(WLS_PART_BIND);
        ui_router_set_alert("");
        tap(b.x + b.w / 2, b.y + b.h / 2);
        CHECK_STR_EQ(wbr_band(), ui_tr(k[i].bind));
        CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), ADDR_A);
        ui_router_set_alert("");
        wbr_hold_unbind(NULL, 2000u);
        CHECK_STR_EQ(wbr_band(), ui_tr(k[i].bind));
        CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), ADDR_A);
    }
}

TEST_CASE(a_scan_is_refused_while_armed_or_running)
{
    open_printer(true);
    print_view_t v = idle_view();
    show(&v);
    set_armed(true);
    tap_row(&wireless_printer_scan_row);
    CHECK_STR_EQ(wbr_band(), ui_tr(TX_PRN_SCAN_ARMED));
    CHECK_EQ(taken(), PRINT_REQ_NONE);
    set_armed(false);
    v.scanning = true;
    v.scan_left_s = 7u;
    show(&v);
    CHECK_STR_EQ(wbr_value(&wireless_printer_scan_row), "SCANNING");
    CHECK_STR_EQ(entry_line(), "SCANNING");
    ui_router_set_alert("");
    tap(wbr_part(WLS_PART_AGAIN).x + 10, wbr_part(WLS_PART_AGAIN).y + 10);
    CHECK_STR_EQ(wbr_band(), ui_tr(TX_NODE_SCAN_RUNNING));
    CHECK_EQ(taken(), PRINT_REQ_NONE);
    /* the controller refused one: the band says why */
    v.scanning = false;
    v.scan_refused = 1u;
    ui_router_set_alert("");
    show(&v);
    CHECK_STR_EQ(wbr_band(), ui_tr(TX_PRN_SCAN_OFF));
    /* the scan stopped by an arm */
    v.scan_done = true;
    v.scan_cut = true;
    show(&v);
    CHECK_STR_EQ(wbr_value(&wireless_printer_scan_row), "STOPPED BY ARM");
}

TEST_CASE(read_printer_and_test_label_ask_and_lock)
{
    open_printer(true);
    print_view_t v = idle_view();
    v.w_mm = 40u;
    v.h_mm = 30u;
    show(&v);
    wireless_printer_query_row.tap();
    CHECK_EQ(taken(), PRINT_REQ_QUERY);
    wireless_printer_test_row.tap();
    CHECK_EQ(taken(), PRINT_REQ_TEST);
    /* the test label is SAFE only; the query is not */
    set_armed(true);
    CHECK_STR_EQ(wireless_printer_test_row.locked(),
                 ui_tr(TX_PRN_LOCK_SAFE));
    CHECK(wireless_printer_query_row.locked() == NULL);
    set_armed(false);
    v.update = true;
    show(&v);
    CHECK_STR_EQ(wireless_printer_test_row.locked(),
                 ui_tr(TX_PRN_LOCK_UPDATE));
    CHECK_STR_EQ(wireless_printer_query_row.locked(),
                 ui_tr(TX_PRN_LOCK_UPDATE));
}

TEST_CASE(the_entry_opened_reads_an_unknown_roll_once)
{
    /* bound, roll unknown: opening PRINTER asks a QUERY */
    fresh_hub();
    set_armed(false);
    print_ctl_reset();
    bind_to(ADDR_A, NAME_A);
    open_category(SET_CAT_WIRELESS);
    tap(entry_x(WL_ENTRY_PRINTER), entry_y(WL_ENTRY_PRINTER));
    CHECK_EQ(taken(), PRINT_REQ_QUERY);
    /* tapped again: no second */
    tap(entry_x(WL_ENTRY_PRINTER), entry_y(WL_ENTRY_PRINTER));
    CHECK_EQ(taken(), PRINT_REQ_NONE);
    /* a known roll, ARMED, or no printer: none */
    print_view_t v = idle_view();
    v.w_mm = 40u;
    v.h_mm = 30u;
    show(&v);
    tap(entry_x(WL_ENTRY_DIAL), entry_y(WL_ENTRY_DIAL));
    tap(entry_x(WL_ENTRY_PRINTER), entry_y(WL_ENTRY_PRINTER));
    CHECK_EQ(taken(), PRINT_REQ_NONE);
    v.w_mm = 0u;
    show(&v);
    set_armed(true);
    tap(entry_x(WL_ENTRY_DIAL), entry_y(WL_ENTRY_DIAL));
    tap(entry_x(WL_ENTRY_PRINTER), entry_y(WL_ENTRY_PRINTER));
    CHECK_EQ(taken(), PRINT_REQ_NONE);
    set_armed(false);
    bind_to("", "");
    tap(entry_x(WL_ENTRY_DIAL), entry_y(WL_ENTRY_DIAL));
    tap(entry_x(WL_ENTRY_PRINTER), entry_y(WL_ENTRY_PRINTER));
    CHECK_EQ(taken(), PRINT_REQ_NONE);
}

TEST_CASE(a_random_address_is_not_bound)
{
    open_printer(false);
    print_view_t v = idle_view();
    v.bound = false;
    v.scan_done = true;
    v.found_n = 1u;
    snprintf(v.found[0].addr, sizeof v.found[0].addr, "A4:93:40:30:FA:71 R");
    show(&v);
    tap_row(&wireless_printer_bind_row);
    gfx_rect_t row;
    REQUIRE(wbr_found(0, &row));
    tap(row.x + row.w / 2, row.y + row.h / 2);
    REQUIRE(wl_search_asking());
    const gfx_rect_t b = wbr_part(WLS_PART_BIND);
    ui_router_set_alert("");
    tap(b.x + b.w / 2, b.y + b.h / 2);
    CHECK_STR_EQ(wbr_band(), ui_tr(TX_PRN_BIND_GONE));
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), "");
    /* one kept in SETUP by hand reads as none bound */
    bind_to("A4:93:40:30:FA:71 R", NAME_A);
    CHECK(!wireless_printer_bound());
    CHECK_STR_EQ(wbr_value(&wireless_printer_bind_row), "NONE");
}

/** @brief A view with ADDR_A found by the last scan. */
static print_view_t found_view(void)
{
    print_view_t v = idle_view();
    v.scan_done = true;
    v.found_n = 1u;
    snprintf(v.found[0].addr, sizeof v.found[0].addr, "%s", ADDR_A);
    snprintf(v.found[0].name, sizeof v.found[0].name, "%s", NAME_A);
    return v;
}

/** @brief SEARCH's first found printer tapped, then BIND. */
static void bind_first(void)
{
    gfx_rect_t row;
    if (!wbr_found(0, &row)) {
        T_FAIL("no found row");
        return;
    }
    tap(row.x + row.w / 2, row.y + row.h / 2);
    const gfx_rect_t b = wbr_part(WLS_PART_BIND);
    tap(b.x + b.w / 2, b.y + b.h / 2);
}

TEST_CASE(a_bind_reads_the_roll_at_once)
{
    /* UNBIND, then BIND of the same printer: the roll is read at once */
    open_printer(false);
    print_view_t v = found_view();
    show(&v);
    tap_row(&wireless_printer_bind_row);
    REQUIRE(ui_router_current() == SCREEN_WL_SEARCH);
    bind_first();
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), ADDR_A);
    show(&v);
    CHECK_EQ(taken(), PRINT_REQ_QUERY);
    show(&v);
    CHECK_EQ(taken(), PRINT_REQ_NONE);
    /* the same printer with its roll known: no read */
    v.w_mm = 40u;
    v.h_mm = 30u;
    show(&v);
    bind_first();
    show(&v);
    CHECK_EQ(taken(), PRINT_REQ_NONE);
    /* a request in work: the read follows its end */
    bind_to("", "");
    v = found_view();
    v.kind = PRINT_REQ_TEST;
    v.phase = PRINT_SENDING;
    show(&v);
    bind_first();
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), "");  /* refused */
    v.phase = PRINT_DONE;
    show(&v);
    bind_first();
    REQUIRE(strcmp(settings_text(SET_TEXT_PRN_ADDR), ADDR_A) == 0);
    v.phase = PRINT_SENDING;        /* another request took the slot */
    show(&v);
    CHECK_EQ(taken(), PRINT_REQ_NONE);
    v.phase = PRINT_DONE;
    show(&v);
    CHECK_EQ(taken(), PRINT_REQ_QUERY);
    /* ARMED: no read, not after the switch to SAFE either */
    bind_to("", "");
    v = found_view();
    show(&v);
    set_armed(true);
    bind_first();
    REQUIRE(strcmp(settings_text(SET_TEXT_PRN_ADDR), ADDR_A) == 0);
    show(&v);
    set_armed(false);
    show(&v);
    CHECK_EQ(taken(), PRINT_REQ_NONE);
}

TEST_CASE(search_marks_a_bound_address_in_small_letters)
{
    open_printer(false);
    bind_to("a4:93:40:30:fa:71 P", NAME_A);
    (void)drain();
    print_view_t v = found_view();
    show(&v);
    tap_row(&wireless_printer_bind_row);
    REQUIRE(ui_router_current() == SCREEN_WL_SEARCH);
    wls_view_t w;
    wl_search_view(&w);
    CHECK_STR_EQ(w.bound_addr, ADDR_A);
    CHECK_STR_EQ(w.found[0].addr, w.bound_addr);
}

int main(void)
{
    RUN(the_entry_holds_eight_rows_and_says_not_bound);
    RUN(the_rows_follow_the_view);
    RUN(search_lists_only_the_ouis_printers_and_a_tap_binds);
    RUN(unbind_at_1900ms_keeps_the_bind_and_at_2000ms_clears_it);
    RUN(search_bind_and_unbind_are_refused_during_a_job_and_an_update);
    RUN(a_scan_is_refused_while_armed_or_running);
    RUN(read_printer_and_test_label_ask_and_lock);
    RUN(the_entry_opened_reads_an_unknown_roll_once);
    RUN(a_random_address_is_not_bound);
    RUN(a_bind_reads_the_roll_at_once);
    RUN(search_marks_a_bound_address_in_small_letters);
    return test_summary("wireless_printer");
}
