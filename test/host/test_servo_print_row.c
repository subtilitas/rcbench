/**
 * @file test_servo_print_row.c
 * @brief SERVO LABEL's PRINT row (servo_label_rows.c): under NEW ID on
 *        every list that carries the block; each lock with its reason; a
 *        tap hands print_ctl the active servo's id and DUT NAME, 23
 *        characters each with spaces; PRINT against PRINT AGAIN for a new,
 *        a picked, a scanned, a typed and a printed servo, and a printed
 *        servo picked again after another (the session's last 8 ids); the job's phase
 *        and progress, its verdict and every error's text on the row and
 *        the band; a print that ends while the list is shut keeps its
 *        verdict for the next visit.  The bench is servo_bench_rig.h; the
 *        controller's view is handed in as the panel's glue does.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "print_ctl.h"
#include "scan_flow.h"
#include "servo_active.h"
#include "servo_label_rows.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "wireless_printer.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/** @brief The printer the cases bind. @{ */
#define ADDR_A "A4:93:40:30:FA:71 P"
#define NAME_A "T0205C2608247368"
/** @} */

/** @brief The view the cases hand in; counts its own rev. */
static print_view_t s_v;

/** @brief s_v handed in, as panel_print_frame() does each frame. */
static void show(void)
{
    ++s_v.rev;
    print_ctl_show(&s_v);
    wireless_printer_tick();
    servo_label_tick();
}

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/** @brief PRINT's value as the open list shows it. */
static const char *value(void)
{
    static char buf[64];
    buf[0] = '\0';
    svl_print_row.value_text(buf, sizeof(buf));
    return buf;
}

/** @brief PRINT's name now. */
static const char *name(void)
{
    return ui_tr(ui_setlist_custom_label(&svl_print_row));
}

/** @brief PRINT's lock now, "" for none. */
static const char *lock(void)
{
    const char *why = svl_print_row.locked();
    return why != NULL ? why : "";
}

/** @brief The request the row posted, PRINT_REQ_NONE for none. */
static print_req_t taken(void)
{
    print_req_t q;
    memset(&q, 0, sizeof(q));
    (void)print_ctl_take(&q);
    return q;
}

/** @brief A fresh SAFE bench on the hub's list, the printer bound and
 *         idle, @p id active from @p from (NULL none), DUT NAME "". */
static void fresh_bench(const char *id, servo_active_from_t from)
{
    bench_fresh();
    short_runs();
    go(SERVO_FN_HUB);
    servo_screen_set_armed(false);
    bench_frames(100u);
    scan_flow_reset();
    (void)servo_active_clear(false);
    if (id != NULL) {
        (void)servo_active_set(id, from, false);
    }
    settings_set_text(SET_TEXT_DUT_NAME, "");
    settings_set_text(SET_TEXT_PRN_ADDR, ADDR_A);
    settings_set_text(SET_TEXT_PRN_NAME, NAME_A);
    print_ctl_reset();
    wireless_printer_reset();
    memset(&s_v, 0, sizeof(s_v));
    s_v.left = -1;
    s_v.bound = true;
    show();
    ui_router_set_alert(NULL);
    settings_request_save();
    tap_settings();
}

/** @brief The controller took the row's label request: s_v in work. */
static void job_starts(const char *id)
{
    s_v.kind = PRINT_REQ_LABEL;
    s_v.phase = PRINT_CONNECTING;
    s_v.err = PRINT_OK;
    snprintf(s_v.id, sizeof(s_v.id), "%s", id);
    ++s_v.jobs;
    show();
}

TEST_CASE(the_row_ends_the_block_on_every_list)
{
    static const struct {
        servo_fn_t fn;
        int        print;
    } lists[] = {
        { SERVO_FN_HUB, HL_PRINT },     { SERVO_FN_LIVE, HL_PRINT },
        { SERVO_FN_TEST, TL_PRINT },    { SERVO_FN_LIMRUN, LL_PRINT },
        { SERVO_FN_RAUTO, RAL_PRINT },
    };
    fresh_bench(NULL, SERVO_ACTIVE_NONE);
    (void)home_tag();
    for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
        settings_request_save();
        go(lists[i].fn);
        tap_settings();
        const ui_setlist_t *sl = servo_screen_list();
        CHECK(sl->open);
        CHECK(sl->host->row(lists[i].print - 1).custom == &svl_new_row);
        CHECK(sl->host->row(lists[i].print).custom == &svl_print_row);
        CHECK(home_tag());
    }
    CHECK_EQ(SVL_ROW_PRINT, 4);
}

TEST_CASE(each_lock_says_its_reason)
{
    fresh_bench(NULL, SERVO_ACTIVE_NONE);
    CHECK_STR_EQ(lock(), ui_tr(TX_SVL_PRINT_NO_ID));
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_NONE);
    CHECK(servo_active_set("KST-00001", SERVO_ACTIVE_NEW, false));
    CHECK_STR_EQ(lock(), "");
    /* no printer bound */
    settings_set_text(SET_TEXT_PRN_ADDR, "");
    CHECK_STR_EQ(lock(), ui_tr(TX_PRN_LOCK_UNBOUND));
    CHECK_STR_EQ(value(), "");
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_NONE);
    settings_set_text(SET_TEXT_PRN_ADDR, ADDR_A);
    /* a job runs, a scan runs */
    job_starts("KST-00009");
    CHECK_STR_EQ(lock(), ui_tr(TX_PRN_LOCK_BUSY));
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_NONE);
    s_v.phase = PRINT_DONE;
    s_v.scanning = true;
    show();
    CHECK_STR_EQ(lock(), ui_tr(TX_PRN_LOCK_BUSY));
    s_v.scanning = false;
    show();
    CHECK_STR_EQ(lock(), "");
    /* ARMED, from SERVO and on the router's status */
    servo_screen_set_armed(true);
    CHECK_STR_EQ(lock(), ui_tr(TX_PRN_LOCK_SAFE));
    servo_screen_set_armed(false);
    ui_bench_status_t st;
    memset(&st, 0, sizeof(st));
    st.armed = true;
    ui_router_set_status(&st);
    CHECK_STR_EQ(lock(), ui_tr(TX_PRN_LOCK_SAFE));
    st.armed = false;
    ui_router_set_status(&st);
    /* a device update */
    s_v.update = true;
    show();
    CHECK_STR_EQ(lock(), ui_tr(TX_PRN_LOCK_UPDATE));
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_NONE);
    s_v.update = false;
    show();
    CHECK_STR_EQ(lock(), "");
}

TEST_CASE(print_hands_the_active_id_and_dut_name)
{
    /* 23 characters each, spaces inside */
    static const char id[] = "KST X10 SERVO BEN-0000A";
    static const char dut[] = "WING LEFT AILERON 12345";
    CHECK_EQ(strlen(id), 23u);
    CHECK_EQ(strlen(dut), 23u);
    fresh_bench(id, SERVO_ACTIVE_NEW);
    CHECK_STR_EQ(servo_active_id(), id);
    settings_set_text(SET_TEXT_DUT_NAME, dut);
    CHECK_STR_EQ(value(), NAME_A);
    sl_tap(HL_PRINT);
    const print_req_t q = taken();
    CHECK_EQ(q.kind, PRINT_REQ_LABEL);
    CHECK_STR_EQ(q.id, id);
    CHECK_STR_EQ(q.dut, dut);
    /* two taps before the controller takes one: one waits, the second
     * is refused on the band */
    sl_tap(HL_PRINT);
    CHECK_STR_EQ(alert(), "");
    sl_tap(HL_PRINT);
    CHECK_STR_EQ(alert(), ui_tr(TX_PRN_LOCK_BUSY));
    CHECK_EQ(taken().kind, PRINT_REQ_LABEL);
    CHECK_EQ(taken().kind, PRINT_REQ_NONE);
}

TEST_CASE(print_against_print_again)
{
    static const struct {
        servo_active_from_t from;
        ui_text_id_t        want;
    } k[] = {
        { SERVO_ACTIVE_NEW, TX_SVL_PRINT },
        { SERVO_ACTIVE_PICK, TX_SVL_PRINT },
        { SERVO_ACTIVE_SCAN, TX_SVL_PRINT_AGAIN },
        { SERVO_ACTIVE_TYPED, TX_SVL_PRINT_AGAIN },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh_bench("KST-00003", k[i].from);
        CHECK_STR_EQ(name(), ui_tr(k[i].want));
    }
    /* a new servo printed: PRINT AGAIN once its label printed */
    fresh_bench("KST-00004", SERVO_ACTIVE_NEW);
    CHECK_STR_EQ(name(), "PRINT");
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_LABEL);
    job_starts("KST-00004");
    CHECK_STR_EQ(name(), "PRINT");
    s_v.phase = PRINT_DONE;
    s_v.left = 293;
    show();
    CHECK(servo_active_printed());
    CHECK_STR_EQ(name(), "PRINT AGAIN");
    /* PRINT AGAIN asks the same label again */
    sl_tap(HL_PRINT);
    const print_req_t q = taken();
    CHECK_EQ(q.kind, PRINT_REQ_LABEL);
    CHECK_STR_EQ(q.id, "KST-00004");
    /* a label of another id printed does not mark this one */
    CHECK(servo_active_set("KST-00005", SERVO_ACTIVE_NEW, false));
    CHECK_STR_EQ(name(), "PRINT");
    job_starts("KST-00004");
    s_v.phase = PRINT_DONE;
    show();
    CHECK(!servo_active_printed());
    CHECK_STR_EQ(name(), "PRINT");
    /* a failed print marks nothing */
    job_starts("KST-00005");
    s_v.phase = PRINT_FAILED;
    s_v.err = PRINT_ERR_NOT_PRINTED;
    show();
    CHECK(!servo_active_printed());
    CHECK_STR_EQ(name(), "PRINT");
}

TEST_CASE(print_again_survives_another_servo_in_between)
{
    /* A printed, B picked, A picked again */
    fresh_bench("KST-00011", SERVO_ACTIVE_NEW);
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_LABEL);
    job_starts("KST-00011");
    s_v.phase = PRINT_DONE;
    s_v.left = 293;
    show();
    CHECK_STR_EQ(name(), "PRINT AGAIN");
    CHECK(servo_active_set("KST-00012", SERVO_ACTIVE_PICK, false));
    CHECK_STR_EQ(name(), "PRINT");
    CHECK(servo_active_set("KST-00011", SERVO_ACTIVE_PICK, false));
    CHECK_STR_EQ(name(), "PRINT AGAIN");
    CHECK_STR_EQ(value(), "PRINTED, 293 LEFT");
    /* C asked, D picked before C's DONE, C picked again */
    CHECK(servo_active_set("KST-00013", SERVO_ACTIVE_NEW, false));
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_LABEL);
    job_starts("KST-00013");
    CHECK(servo_active_set("KST-00014", SERVO_ACTIVE_PICK, false));
    s_v.phase = PRINT_DONE;
    s_v.left = 292;
    show();
    CHECK_STR_EQ(name(), "PRINT");
    CHECK(servo_active_set("KST-00013", SERVO_ACTIVE_PICK, false));
    CHECK_STR_EQ(name(), "PRINT AGAIN");
    CHECK_STR_EQ(value(), "PRINTED, 292 LEFT");
    /* the session keeps the last 8: a ninth gives up the oldest */
    for (int k = 0; k < 8; ++k) {
        char id[SERVO_ID_SIZE];
        snprintf(id, sizeof(id), "KST-0002%d", k);
        job_starts(id);
        s_v.phase = PRINT_DONE;
        show();
    }
    CHECK(servo_active_set("KST-00011", SERVO_ACTIVE_PICK, false));
    CHECK_STR_EQ(name(), "PRINT");
    CHECK(servo_active_set("KST-00027", SERVO_ACTIVE_PICK, false));
    CHECK_STR_EQ(name(), "PRINT AGAIN");
}

TEST_CASE(the_row_shows_the_phase_the_progress_and_the_verdict)
{
    fresh_bench("KST-00006", SERVO_ACTIVE_PICK);
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_LABEL);
    const uint32_t r0 = servo_label_repaints();
    job_starts("KST-00006");
    CHECK(servo_label_repaints() > r0);
    CHECK_STR_EQ(value(), "CONNECTING");
    s_v.phase = PRINT_READING;
    show();
    CHECK_STR_EQ(value(), "READING ROLL");
    s_v.phase = PRINT_BUILDING;
    show();
    CHECK_STR_EQ(value(), "BUILDING LABEL");
    s_v.phase = PRINT_SENDING;
    s_v.frame = 3u;
    s_v.frames = 5u;
    show();
    CHECK_STR_EQ(value(), "SENDING 3/5");
    s_v.phase = PRINT_PRINTING;
    show();
    CHECK_STR_EQ(value(), "PRINTING");
    s_v.phase = PRINT_CHECKING;
    show();
    CHECK_STR_EQ(value(), "CHECKING COUNT");
    s_v.phase = PRINT_DONE;
    s_v.left = 293;
    show();
    CHECK_STR_EQ(value(), "PRINTED, 293 LEFT");
    CHECK_STR_EQ(alert(), "");
    s_v.left = -1;
    show();
    CHECK_STR_EQ(value(), "PRINTED");
    /* every error's text reaches the row and the band */
    for (unsigned e = PRINT_ERR_NO_PRINTER; e < PRINT_ERR_COUNT; ++e) {
        ui_router_set_alert(NULL);
        job_starts("KST-00006");
        s_v.phase = PRINT_FAILED;
        s_v.err = (uint8_t)e;
        show();
        CHECK_STR_EQ(value(), ui_tr(print_err_text((uint8_t)e)));
        CHECK_STR_EQ(alert(), ui_tr(print_err_text((uint8_t)e)));
    }
    CHECK_STR_EQ(value(), "SAFE ONLY");
    /* the same error again is a new verdict: the band says it again */
    ui_router_set_alert(NULL);
    job_starts("KST-00006");
    s_v.phase = PRINT_FAILED;
    s_v.err = PRINT_ERR_ARMED;
    show();
    CHECK_STR_EQ(alert(), "SAFE ONLY");
    /* a test label in work shows here too; another servo's verdict not */
    s_v.kind = PRINT_REQ_TEST;
    s_v.phase = PRINT_SENDING;
    ++s_v.jobs;
    show();
    CHECK_STR_EQ(value(), "SENDING 3/5");
    job_starts("KST-00001");
    s_v.phase = PRINT_DONE;
    show();
    CHECK_STR_EQ(value(), NAME_A);
}

TEST_CASE(a_print_that_ends_while_the_list_is_shut_keeps_its_verdict)
{
    fresh_bench("KST-00007", SERVO_ACTIVE_NEW);
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_LABEL);
    job_starts("KST-00007");
    (void)home_tag();
    CHECK(!servo_screen_list()->open);
    s_v.phase = PRINT_SENDING;
    show();
    s_v.phase = PRINT_DONE;
    s_v.left = 292;
    show();
    CHECK(servo_active_printed());
    go(SERVO_FN_LIMRUN);
    tap_settings();
    CHECK(servo_screen_list()->open);
    CHECK_STR_EQ(value(), "PRINTED, 292 LEFT");
    CHECK_STR_EQ(name(), "PRINT AGAIN");
    /* an error while away: on the band at its end, on the row after */
    (void)home_tag();
    ui_router_set_alert(NULL);
    job_starts("KST-00007");
    s_v.phase = PRINT_FAILED;
    s_v.err = PRINT_ERR_COVER;
    show();
    CHECK_STR_EQ(alert(), "COVER OPEN");
    go(SERVO_FN_HUB);
    tap_settings();
    CHECK_STR_EQ(value(), "COVER OPEN");
    /* until the next tap asks again */
    sl_tap(HL_PRINT);
    CHECK_EQ(taken().kind, PRINT_REQ_LABEL);
}

int main(void)
{
    RUN(the_row_ends_the_block_on_every_list);
    RUN(each_lock_says_its_reason);
    RUN(print_hands_the_active_id_and_dut_name);
    RUN(print_against_print_again);
    RUN(print_again_survives_another_servo_in_between);
    RUN(the_row_shows_the_phase_the_progress_and_the_verdict);
    RUN(a_print_that_ends_while_the_list_is_shut_keeps_its_verdict);
    return test_summary("servo_print_row");
}
