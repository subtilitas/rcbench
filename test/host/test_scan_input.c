/**
 * @file
 * @brief A scan arrives while input is live: the scan flow (scan_flow.h)
 *        touches no input state.
 *
 * The line comes through the frame task's mailbox (scan_flow_post(), then
 * scan_flow_tick() in the frame), as the console's `scan <text>` and the
 * scanner send it.  Each case runs the same touches twice, once without
 * and once with the scan in the middle, and holds the two to the same
 * outcome: only the active servo and the notice differ.  A flow that
 * closed a keypad, cancelled a hold or dropped a contact would make the
 * two runs differ.
 *
 * Cases:
 * - a_scan_leaves_an_open_keypad_open_with_its_digits
 * - a_scan_during_the_stop_resets_hold_neither_ends_nor_moves_it
 * - a_scan_under_three_contacts_keeps_them_and_stop_counts_on_the_press
 *
 * SPDX-License-Identifier: MIT
 */
#include "nav_rig.h"

#include "scan_flow.h"
#include "servo_active.h"
#include "stop_hold.h"
#include "supply_spots.h"
#include "ui_keypad.h"
#include "ui_setlist.h"

/** @brief One frame at 60 frames a second, s. */
#define FRAME_S (1.0f / 60.0f)
/** @brief The label every case scans. */
#define LABEL "KST_X10-0000A"

/** @brief The scan's clock, far from any other case's. */
static uint32_t s_ms = 9000000u;

/** @brief The frame task's part of a frame: the waiting line runs. */
static void scan_frame(void)
{
    s_ms += 100000u;
    scan_flow_tick(s_ms, NULL);
}

/** @brief No servo active, no line waiting, no notice. */
static void scan_fresh(void)
{
    scan_flow_reset();
    (void)servo_active_clear(false);
    ui_router_set_alert(NULL);
}

/** @brief The alert band's text, "" for none. */
static const char *notice(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/** @brief The active id, "" for none. */
static const char *active(void)
{
    const char *id = servo_active_id();
    return id != NULL ? id : "";
}

/* ------------------------------------------------------------ a keypad */

/** @brief A tap on @p key of SUPPLY's settings keypad. */
static void key(ui_key_t key)
{
    const gfx_rect_t r = ui_keypad_key_rect(supply_settings_keypad(), key);
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/** @brief What a keypad run leaves. */
typedef struct {
    bool open_after_scan;   /**< open in the frame after the line   */
    char entry_after_scan[UI_KEYPAD_ENTRY_MAX + 1];
    bool open_at_end;       /**< open after the last key             */
    char entry_at_end[UI_KEYPAD_ENTRY_MAX + 1];
} keypad_run_t;

/** @brief SUPPLY's VOLTAGE MAX keypad: 1 and 2, the frame (with the scan
 *         when @p scan), then 3. */
static keypad_run_t keypad_run(bool scan)
{
    keypad_run_t out;
    memset(&out, 0, sizeof(out));
    on_screen(SCREEN_SUPPLY);
    scan_fresh();
    tap(SU_SETTINGS_X, UI_BAND_H + SU_SETTINGS_Y);
    const gfx_rect_t v = ui_setlist_row_part(
        supply_settings_list(), supply_settings_row(SET_SUPPLY_V_MAX),
        UI_SL_PART_VALUE);
    tap(v.x + v.w / 2, UI_BAND_H + v.y + v.h / 2);
    key(UI_KEY_1);
    key(UI_KEY_2);
    if (scan) {
        CHECK(scan_flow_post(LABEL));
    }
    scan_frame();
    ui_router_tick(FRAME_S);
    out.open_after_scan = supply_settings_keypad()->open;
    memcpy(out.entry_after_scan, supply_settings_keypad()->entry,
           sizeof(out.entry_after_scan));
    key(UI_KEY_3);
    out.open_at_end = supply_settings_keypad()->open;
    memcpy(out.entry_at_end, supply_settings_keypad()->entry,
           sizeof(out.entry_at_end));
    return out;
}

TEST_CASE(a_scan_leaves_an_open_keypad_open_with_its_digits)
{
    const keypad_run_t plain = keypad_run(false);
    CHECK(plain.open_after_scan);
    CHECK_STR_EQ(plain.entry_after_scan, "12");
    CHECK_STR_EQ(notice(), "");

    const keypad_run_t scanned = keypad_run(true);
    CHECK(scanned.open_after_scan);
    CHECK_STR_EQ(scanned.entry_after_scan, "12");
    CHECK(scanned.open_at_end);
    CHECK_STR_EQ(scanned.entry_at_end, plain.entry_at_end);
    CHECK_STR_EQ(scanned.entry_at_end, "123");
    CHECK_STR_EQ(active(), LABEL);
    CHECK_STR_EQ(notice(), "SERVO " LABEL);
    CHECK_EQ(ui_router_current(), SCREEN_SUPPLY);
}

/* ---------------------------------------------------------- a 2s hold */

/** @brief The middle of STOP. */
static void stop_at(int *x, int *y)
{
    const gfx_rect_t r = ui_band_stop_rect();
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
}

/** @brief A DOWN on STOP the control task marked latched. */
static void stop_down(touch_event_type_t type, uint8_t flags)
{
    int x, y;
    stop_at(&x, &y);
    touch_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.flags = flags;
    e.point.id = 1u;
    e.point.x = (int16_t)x;
    e.point.y = (int16_t)y;
    e.point.strength = 40;
    ui_router_event(&e);
}

/** @brief What a hold run leaves. */
typedef struct {
    float before;     /**< the hold's time before the scan's frame   */
    float after;      /**< and after it                              */
    int   fired_at;   /**< the frame of 0.25s the reset was asked at  */
} hold_run_t;

/** @brief STOP latched, its reset held for 1.0s, the frame (with the scan
 *         when @p scan), then held on until the reset is asked. */
static hold_run_t hold_run(bool scan)
{
    hold_run_t out = { 0.0f, 0.0f, -1 };
    on_screen(SCREEN_OVERVIEW);
    scan_fresh();
    ui_bench_status_t st = *ui_router_status();
    st.link_up = true;
    st.stop_latched = true;
    ui_router_set_status(&st);
    (void)ui_router_take_stop();
    (void)stop_hold_take_reset();
    stop_down(TOUCH_EVENT_DOWN, TOUCH_FLAG_STOP_LATCHED);
    ui_router_tick(0.016f);
    ui_router_tick(0.016f);     /* the two frames around the press */
    for (int i = 0; i < 4; ++i) {
        ui_router_tick(0.25f);
    }
    out.before = stop_hold_held_s();
    if (scan) {
        CHECK(scan_flow_post(LABEL));
    }
    scan_frame();
    out.after = stop_hold_held_s();
    CHECK(!stop_hold_take_reset());
    for (int i = 0; i < 8 && out.fired_at < 0; ++i) {
        ui_router_tick(0.25f);
        if (stop_hold_take_reset()) {
            out.fired_at = i;
        }
    }
    stop_down(TOUCH_EVENT_UP, 0u);
    return out;
}

TEST_CASE(a_scan_during_the_stop_resets_hold_neither_ends_nor_moves_it)
{
    const hold_run_t plain = hold_run(false);
    CHECK(plain.before > 0.9f);
    CHECK_FEQ(plain.after, plain.before);
    CHECK(plain.fired_at >= 0);

    const hold_run_t scanned = hold_run(true);
    CHECK_FEQ(scanned.before, plain.before);
    CHECK_FEQ(scanned.after, scanned.before);
    CHECK_EQ(scanned.fired_at, plain.fired_at);
    CHECK_STR_EQ(active(), LABEL);
}

/* --------------------------------------------------- three contacts */

/** @brief The shaft in panel coordinates. */
#define SHAFT_P_X SV_SHAFT_X
#define SHAFT_P_Y (UI_BAND_H + SV_SHAFT_Y)

/** @brief A point on the dial's arc at @p deg, 110px from the shaft. */
static void dial_at(float deg, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SHAFT_P_X + (int)(110.0f * cosf(deg * k) + 0.5f);
    *y = SHAFT_P_Y - (int)(110.0f * sinf(deg * k) + 0.5f);
}

/** @brief What a contacts run leaves. */
typedef struct {
    uint16_t held;       /**< the pulse before the frame                  */
    uint16_t moved;      /**< the pulse the drag commands after the frame */
    bool     stopped;    /**< STOP counted in the scan's frame            */
    bool     again;      /**< a second STOP asked from the same press     */
} contacts_run_t;

/** @brief SERVO LIVE armed: contact 1 drags the dial, contacts 2 and 3
 *         rest beside it; the frame (with the scan when @p scan); contact
 *         1 moves on; then contact 4 presses STOP in the frame of a
 *         second scan. */
static contacts_run_t contacts_run(bool scan)
{
    contacts_run_t out;
    memset(&out, 0, sizeof(out));
    on_screen(SCREEN_SERVO_LIVE);
    scan_fresh();
    servo_screen_set_armed(true);
    drain_servo();
    float deg = 30.0f;
    int x, y;
    dial_at(deg, &x, &y);
    finger(1u, x, y);
    for (; deg > 0.0f; deg -= 2.0f) {
        dial_at(deg, &x, &y);
        finger(1u, x, y);
        ui_router_tick(FRAME_S);
    }
    finger(2u, 40, UI_BAND_H + 500);
    finger(3u, 120, UI_BAND_H + 500);
    ui_router_tick(FRAME_S);
    drain_servo();
    if (scan) {
        CHECK(scan_flow_post(LABEL));
    }
    scan_frame();
    ui_router_tick(FRAME_S);
    out.held = servo_screen_commanded();
    /* Contact 1 moves on: its drag is still its own. */
    for (int i = 0; i < 10; ++i) {
        deg -= 2.0f;
        dial_at(deg, &x, &y);
        finger(1u, x, y);
        ui_router_tick(FRAME_S);
    }
    out.moved = servo_screen_commanded();
    drain_servo();
    /* The frame of another line, and STOP pressed in it. */
    if (scan) {
        CHECK(scan_flow_post("KST_X10-0000B"));
    }
    (void)ui_router_take_stop();
    scan_frame();
    int sx, sy;
    stop_at(&sx, &sy);
    finger(4u, sx, sy);
    out.stopped = ui_router_take_stop();
    ui_router_tick(FRAME_S);
    out.again = ui_router_take_stop();
    lift(4u);
    lift(3u);
    lift(2u);
    lift(1u);
    ui_router_tick(FRAME_S);
    servo_screen_set_armed(false);
    drain_servo();
    return out;
}

TEST_CASE(a_scan_under_three_contacts_keeps_them_and_stop_counts_on_the_press)
{
    const contacts_run_t plain = contacts_run(false);
    CHECK(plain.stopped);
    CHECK(!plain.again);
    CHECK(plain.moved != plain.held);   /* the drag drives after the frame */

    const contacts_run_t scanned = contacts_run(true);
    CHECK_EQ(scanned.moved, plain.moved);
    CHECK(scanned.stopped);
    CHECK(!scanned.again);
    /* LIVE ARMED is a run (servo_screen_run_active()): both lines run
     * through the flow in their frames and are refused, the bench keeping
     * no servo. */
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_REFUSED_RUN);
    CHECK_STR_EQ(active(), "");
}

int main(void)
{
    RUN(a_scan_leaves_an_open_keypad_open_with_its_digits);
    RUN(a_scan_during_the_stop_resets_hold_neither_ends_nor_moves_it);
    RUN(a_scan_under_three_contacts_keeps_them_and_stop_counts_on_the_press);
    return test_summary("scan_input");
}
