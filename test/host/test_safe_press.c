/*
 * A press that stops what the bench drives acts on the first press, whatever
 * the router draws over it: the band's help bubble takes any press while it
 * is open, and DISARM disarms through it.  Never the reverse: a press on ARM
 * under the bubble starts no hold.  SERVO's ARM / DISARM ends above the
 * alert band (panel y 448 to 479, the band from 556): a press on it with an
 * alert showing is its own, and the alert stays; a press on the alert band
 * closes the alert and reaches no screen.
 *
 * Touch is fed as the panel produces it, through the tracker and the router
 * (touch_feed.h), in panel coordinates.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "gfx.h"
#include "motor_screen.h"
#include "prog_registry.h"
#include "servo_screen.h"
#include "servo_spots.h"
#include "settings.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W UI_PANEL_W
#define H UI_PANEL_H
#define P(y) (UI_BAND_H + (y))

/* SERVO's ARM / DISARM in panel coordinates, mirrored from servo_screen.c:
 * the right card's column (servo_spots.h), body y 388 to 419. */
#define SV_ARM_L   SV_RC_X
#define SV_ARM_R   (SV_RC_X + SV_RC_W - 1)
#define SV_ARM_T   P(388)
#define SV_ARM_B   P(419)
#define SV_ARM_X   ((SV_ARM_L + SV_ARM_R) / 2)
#define SV_ARM_Y   P(404)

/* MOTOR & ESC's ARM / DISARM, as test_motor_arm.c has it. */
#define MO_ARM_X   676
#define MO_ARM_Y   P(324 + UI_BTN_H / 2)

#define FRAME_S    0.026f

static gfx_color_t *fb;
static gfx_canvas_t cv;

static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(FRAME_S);
    }
}

static void up_on(ui_screen_id_t id)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(id);
    feed_reset();
}

/* ------------------------------------------------------------------ SERVO */

static servo_cmd_kind_t sv_took(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_screen_take(&c);
    return c.kind;
}

static void sv_drain(void)
{
    while (sv_took() != SERVO_CMD_NONE) { }
}

/* SERVO on a disarmed bench, nothing on the glass. */
static void sv_fresh(void)
{
    up_on(SCREEN_SERVO);
    servo_screen_feedback(0u, 0.0f, false);
    servo_screen_set_armed(false);
    sv_drain();
}

/* ARM held 2.3 s with no alert showing, and the bench's answer. */
static bool sv_armed(void)
{
    sv_fresh();
    finger(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    frames((int)(2.3f / FRAME_S));
    const servo_cmd_kind_t k = sv_took();
    lift(FEED_LONE);
    servo_screen_set_armed(true);
    sv_drain();
    return k == SERVO_CMD_ARM;
}

/* An alert arriving, as the panel's frame loop hands one over. */
static void alert(void)
{
    ui_router_set_alert("NO CARD. THIS RUN IS NOT RECORDED.");
}

static bool alert_shows(void) { return ui_router_alert_number() != 0u; }

/* DISARM ends above the alert band, so no alert covers it. */
TEST_CASE(disarm_ends_above_the_alert_band)
{
    CHECK(SV_ARM_B < UI_ALERT_Y);
    CHECK(MO_ARM_Y < UI_ALERT_Y);
}

/* DISARM tapped 1.0 s after the hold, with an alert that arrived with the
 * arm: the tap disarms, and the alert stays. */
TEST_CASE(disarm_beside_the_alert_disarms_at_once)
{
    CHECK(sv_armed());
    alert();
    frames((int)(1.0f / FRAME_S));
    CHECK(alert_shows());
    feed_tap(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    CHECK_EQ(sv_took(), SERVO_CMD_DISARM);
    CHECK(alert_shows());
}

/* The same in the frame the alert arrives in, and 15 s into its 30 s. */
TEST_CASE(disarm_beside_the_alert_from_its_arrival_to_its_end)
{
    const int at[] = { 0, 1, (int)(15.0f / FRAME_S),
                       (int)(UI_ALERT_SHOW_S / FRAME_S) - 2 };
    for (size_t i = 0; i < sizeof(at) / sizeof(at[0]); ++i) {
        CHECK(sv_armed());
        alert();
        frames(at[i]);
        CHECK(alert_shows());
        feed_tap(FEED_LONE, SV_ARM_X, SV_ARM_Y);
        CHECK_EQ(sv_took(), SERVO_CMD_DISARM);
        CHECK(alert_shows());
    }
}

/*
 * The edges.  On DISARM -- its top and bottom rows, its left and right
 * columns -- the press disarms and the alert stays.  Between DISARM and the
 * alert band, and beside DISARM, the press is the screen's and asks
 * nothing.  On the alert band's top row the tap closes the alert and
 * reaches no screen.
 */
TEST_CASE(disarm_at_the_alerts_edges)
{
    static const struct { int x, y; bool disarms, alert_stays, alerts; } k[] = {
        { SV_ARM_X, SV_ARM_T,       true,  true,  false },
        { SV_ARM_X, SV_ARM_B,       true,  true,  false },
        { SV_ARM_L, SV_ARM_Y,       true,  true,  false },
        { SV_ARM_R, SV_ARM_Y,       true,  true,  false },
        { SV_ARM_X, SV_ARM_B + 1,   false, true,  false },
        { SV_ARM_X, UI_ALERT_Y - 1, false, true,  false },
        { SV_ARM_L - 1, SV_ARM_Y,   false, true,  false },
        { SV_ARM_R + 1, SV_ARM_Y,   false, true,  false },
        { SV_ARM_X, UI_ALERT_Y,     false, false, true  },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK(sv_armed());
        alert();
        const uint32_t handed = ui_router_dispatched();
        feed_tap(FEED_LONE, k[i].x, k[i].y);
        CHECK_EQ(sv_took(), k[i].disarms ? SERVO_CMD_DISARM : SERVO_CMD_NONE);
        CHECK_EQ(alert_shows(), k[i].alert_stays);
        if (k[i].alerts) {
            CHECK_EQ(ui_router_dispatched(), handed);   /* the alert's tap */
        }
    }
}

/* An alert arriving under a finger already down on DISARM: the release is
 * the press's, and disarms. */
TEST_CASE(an_alert_arriving_under_the_press_does_not_take_its_release)
{
    CHECK(sv_armed());
    finger(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    frames(10);
    alert();
    frames(10);
    lift(FEED_LONE);
    CHECK_EQ(sv_took(), SERVO_CMD_DISARM);
}

/* ARM with an alert showing: the hold is ARM's, as without one, and the
 * alert stays. */
TEST_CASE(arm_beside_the_alert_holds_as_without_one)
{
    sv_fresh();
    alert();
    finger(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    frames((int)(2.3f / FRAME_S));
    CHECK_EQ(sv_took(), SERVO_CMD_ARM);
    lift(FEED_LONE);
    CHECK_EQ(sv_took(), SERVO_CMD_NONE);
    CHECK(alert_shows());
}

/*
 * An ARM asked for and not yet answered -- refused by the IOmcu, or still
 * on its way -- leaves the button reading ARM, and a tap there is ARM's: a
 * tap is no hold, and there is nothing to disarm.  An answer that arrives
 * later makes the next press a DISARM.
 */
TEST_CASE(an_arm_not_answered_is_no_disarm)
{
    sv_fresh();
    finger(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    frames((int)(2.3f / FRAME_S));
    CHECK_EQ(sv_took(), SERVO_CMD_ARM);
    lift(FEED_LONE);
    alert();
    feed_tap(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    CHECK_EQ(sv_took(), SERVO_CMD_NONE);
    CHECK(alert_shows());
    servo_screen_set_armed(true);            /* the answer, late */
    sv_drain();
    feed_tap(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    CHECK_EQ(sv_took(), SERVO_CMD_DISARM);
}

/* A tap on the alert closes the alert and reaches no screen. */
TEST_CASE(a_tap_on_the_alert_only_closes_it)
{
    CHECK(sv_armed());
    alert();
    const uint32_t handed = ui_router_dispatched();
    feed_tap(FEED_LONE, 100, UI_ALERT_Y + UI_ALERT_H / 2);
    CHECK(!alert_shows());
    CHECK_EQ(ui_router_dispatched(), handed);
    CHECK_EQ(sv_took(), SERVO_CMD_NONE);
    /* Still armed: the next tap on DISARM disarms. */
    feed_tap(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    CHECK_EQ(sv_took(), SERVO_CMD_DISARM);
}

/* A press on DISARM with an alert showing is DISARM's press like any
 * other: one that slides off and lifts away asks nothing, and one that
 * wanders over the button and lifts on it disarms. */
TEST_CASE(a_disarm_press_beside_the_alert_slides_as_without_one)
{
    CHECK(sv_armed());
    alert();
    finger(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    glide(FEED_LONE, SV_ARM_X, P(300), 8);
    lift(FEED_LONE);
    CHECK_EQ(sv_took(), SERVO_CMD_NONE);

    alert();
    finger(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    glide(FEED_LONE, SV_ARM_R - 4, SV_ARM_B, 8);
    lift(FEED_LONE);
    CHECK_EQ(sv_took(), SERVO_CMD_DISARM);
}

/* Touch lost while a press on DISARM is down: the press whose release went
 * missing is a DISARM all the same. */
TEST_CASE(touch_lost_under_a_disarm_press_disarms)
{
    CHECK(sv_armed());
    alert();
    finger(FEED_LONE, SV_ARM_X, SV_ARM_Y);
    ui_router_cancel_gestures();
    CHECK_EQ(sv_took(), SERVO_CMD_DISARM);
    lift(FEED_LONE);
    CHECK_EQ(sv_took(), SERVO_CMD_NONE);
}

/* The FAULT chip's bubble: a tap at its middle opens it. */
static void open_bubble(bool armed)
{
    static ui_bench_status_t st;
    st = (ui_bench_status_t){ .link_up = true, .armed = armed,
                              .armed_roles = armed ? UI_ARMED_SERVO : 0u,
                              .faults = 0x10, .run_seconds = 5,
                              .mode = "BENCH" };
    ui_router_set_status(&st);
    gfx_rect_t r;
    CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_FAULT, &r));
    feed_tap(FEED_LONE, r.x + r.w / 2, r.y + r.h / 2);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_FAULT);
}

/* With a help bubble open, DISARM disarms on the first tap and the bubble
 * closes; ARM held 3 s closes the bubble and starts no hold. */
TEST_CASE(the_help_bubble_takes_no_disarm_and_starts_no_arm)
{
    CHECK(sv_armed());
    open_bubble(true);
    feed_tap(FEED_LONE, SV_ARM_X, P(400));
    CHECK_EQ(sv_took(), SERVO_CMD_DISARM);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);

    sv_fresh();
    open_bubble(false);
    finger(FEED_LONE, SV_ARM_X, P(400));
    frames((int)(3.0f / FRAME_S));
    lift(FEED_LONE);
    CHECK_EQ(sv_took(), SERVO_CMD_NONE);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

/* What the screen names, read straight: DISARM while the button reads
 * DISARM, and nothing on ARM, on CENTRE, or on the dial. */
TEST_CASE(servo_names_disarm_and_not_arm)
{
    const ui_screen_t *scr = servo_screen();
    if (scr->safe_at == NULL) {
        T_FAIL("SERVO names no control to the router");
        return;
    }
    sv_fresh();
    CHECK(!scr->safe_at(SV_ARM_X, SV_ARM_Y - UI_BAND_H));
    CHECK(sv_armed());
    CHECK(scr->safe_at(SV_ARM_X, SV_ARM_Y - UI_BAND_H));
    CHECK(!scr->safe_at(SV_ARM_X, SV_ARM_T - UI_BAND_H - 1));
    CHECK(!scr->safe_at(SV_CENTRE_X, SV_BTN_Y));  /* CENTRE */
    CHECK(!scr->safe_at(400, 216));               /* the dial */
    servo_screen_set_armed(false);
    CHECK(!scr->safe_at(SV_ARM_X, SV_ARM_Y - UI_BAND_H));
    /* OUTPUT OFF while the output is on; OUTPUT ON is none. */
    CHECK(!scr->safe_at(SV_SUP_OUT_X, SV_SUP_Y));
    supply_screen_set_output(true);
    CHECK(scr->safe_at(SV_SUP_OUT_X, SV_SUP_Y));
    supply_screen_set_output(false);
    CHECK(!scr->safe_at(SV_SUP_OUT_X, SV_SUP_Y));
}

/* ------------------------------------------------------------ MOTOR & ESC */

static motor_cmd_kind_t mo_took(void)
{
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    (void)motor_screen_poll_cmd(&c);
    return c.kind;
}

static bool mo_armed(void)
{
    up_on(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    while (mo_took() != MOTOR_CMD_NONE) { }
    finger(FEED_LONE, MO_ARM_X, MO_ARM_Y);
    frames((int)(2.3f / FRAME_S));
    const motor_cmd_kind_t k = mo_took();
    lift(FEED_LONE);
    motor_screen_set_armed(true);
    while (mo_took() != MOTOR_CMD_NONE) { }
    return k == MOTOR_CMD_ARM;
}

/* MOTOR & ESC's DISARM is above the alert band and acts under the help
 * bubble; RESET PEAKS under the alert is the alert's. */
TEST_CASE(motor_disarm_acts_under_the_bubble_and_beside_the_alert)
{
    CHECK(mo_armed());
    open_bubble(true);
    feed_tap(FEED_LONE, MO_ARM_X, MO_ARM_Y);
    CHECK_EQ(mo_took(), MOTOR_CMD_DISARM);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);

    CHECK(mo_armed());
    alert();
    feed_tap(FEED_LONE, MO_ARM_X, UI_ALERT_Y + 4);  /* RESET PEAKS, under it */
    CHECK_EQ(mo_took(), MOTOR_CMD_NONE);
    CHECK(!alert_shows());
    alert();
    feed_tap(FEED_LONE, MO_ARM_X, MO_ARM_Y);
    CHECK_EQ(mo_took(), MOTOR_CMD_DISARM);
    CHECK(alert_shows());                          /* not under it */
}

/* ARM on MOTOR & ESC under the bubble starts no hold. */
TEST_CASE(motor_arm_under_the_bubble_starts_no_hold)
{
    up_on(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    while (mo_took() != MOTOR_CMD_NONE) { }
    open_bubble(false);
    finger(FEED_LONE, MO_ARM_X, MO_ARM_Y);
    frames((int)(3.0f / FRAME_S));
    lift(FEED_LONE);
    CHECK_EQ(mo_took(), MOTOR_CMD_NONE);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

/* ----------------------------------------------------------------- SUPPLY */

#define SU_OUT_X 676
#define SU_OUT_Y P(318)

static bool su_took(supply_cmd_t *c)
{
    memset(c, 0, sizeof(*c));
    return supply_screen_poll_cmd(c);
}

/* SUPPLY's OUTPUT OFF acts under the help bubble on the first tap; OUTPUT
 * ON held 3 s under it switches nothing on. */
TEST_CASE(output_off_acts_under_the_bubble_and_output_on_does_not)
{
    supply_cmd_t c;
    up_on(SCREEN_SUPPLY);
    supply_screen_settings_loaded();
    while (su_took(&c)) { }
    supply_screen_set_output(true);
    open_bubble(false);
    feed_tap(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    CHECK(su_took(&c));
    CHECK(c.off);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);

    supply_screen_set_output(false);
    while (su_took(&c)) { }
    open_bubble(false);
    finger(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    frames((int)(3.0f / FRAME_S));
    lift(FEED_LONE);
    CHECK(!su_took(&c));
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

/* ------------------------------------------------- under the alert band */

/* The points of the body under the alert band that @p scr names as a stop,
 * every 2 px. */
static int stops_under_the_band(const ui_screen_t *scr)
{
    int n = 0;
    for (int y = UI_ALERT_Y - UI_BAND_H; y < UI_BODY_H; y += 2) {
        for (int x = 0; x < W; x += 2) {
            n += scr->safe_at(x, y) ? 1 : 0;
        }
    }
    return n;
}

/* Every stop control ends above the alert band, so an alert covers none:
 * SERVO's DISARM and OUTPUT OFF, MOTOR & ESC's DISARM, SUPPLY's OUTPUT OFF.
 * ESC STICK's ABORT is held in test_esc_stick_panel_touch. */
TEST_CASE(no_stop_control_lies_under_the_alert_band)
{
    CHECK(sv_armed());
    supply_screen_set_output(true);
    CHECK(servo_screen()->safe_at(SV_ARM_X, SV_ARM_Y - UI_BAND_H));
    CHECK_EQ(stops_under_the_band(servo_screen()), 0);
    supply_screen_set_output(false);

    CHECK(mo_armed());
    CHECK(motor_screen()->safe_at(MO_ARM_X, MO_ARM_Y - UI_BAND_H));
    CHECK_EQ(stops_under_the_band(motor_screen()), 0);

    up_on(SCREEN_SUPPLY);
    supply_screen_settings_loaded();
    supply_screen_set_output(true);
    CHECK(supply_screen()->safe_at(SU_OUT_X, SU_OUT_Y - UI_BAND_H));
    CHECK_EQ(stops_under_the_band(supply_screen()), 0);
    supply_screen_set_output(false);
}

/*
 * A session on MOTOR & ESC's PROGRAM whose STOP lies under the alert band:
 * no product control does, so this one holds the router's way through the
 * band for a stop.  Its STOP is body x 400 to 599, y 500 to 535.
 */
static bool s_fake_open;
static int  s_fake_stops;
static int  s_fake_downs;
static const gfx_rect_t k_fake_stop = { 400, 500, 200, 36 };

static void fake_init(gfx_rect_t a)  { (void)a; s_fake_open = false; }
static void fake_none(void)          { }
static bool fake_is_open(void)       { return s_fake_open; }
static void fake_back(void)          { s_fake_open = false; }
static bool fake_false(void)         { return false; }
static void fake_armed(bool a)       { (void)a; }
static void fake_tick(float dt)      { (void)dt; }
static bool fake_has_press(uint8_t id) { (void)id; return false; }
static void fake_render(gfx_canvas_t *c) { (void)c; }
static uint32_t fake_rev(void)       { return 0u; }
static gfx_rect_t fake_hold(void)    { return (gfx_rect_t){ 0, 0, 0, 0 }; }
static uint8_t fake_pin(void)        { return PROG_REG_NO_PIN; }
static bool fake_safe_at(int x, int y)
{
    return s_fake_open && gfx_rect_contains(k_fake_stop, x, y);
}
static void fake_event(const touch_event_t *e)
{
    if (e->type != TOUCH_EVENT_DOWN) {
        return;
    }
    ++s_fake_downs;
    if (fake_safe_at(e->point.x, e->point.y)) {
        ++s_fake_stops;
    }
}

static const prog_page_set_t k_fake_pages = {
    .init = fake_init, .entry = fake_none, .open = fake_none,
    .is_open = fake_is_open, .back = fake_back, .holds_port = fake_false,
    .armed = fake_armed, .tick = fake_tick, .has_press = fake_has_press,
    .event = fake_event, .render = fake_render, .revision = fake_rev,
    .hold_revision = fake_rev, .hold_rect = fake_hold, .leave = fake_none,
    .cancel = fake_none, .pin = fake_pin, .safe_at = fake_safe_at,
};

/* MOTOR & ESC on PROGRAM with the fake session open over the whole body. */
static void fake_session(void)
{
    static const prog_reg_row_t k_row = {
        PROG_DEV_MOTOR, UI_WB_PROGRAM, TX_PL_STICK, TX_PL_GRP_STICK,
        TX_MO_SET_THROTTLE, PROG_REG_NO_PORT, 0u, &k_fake_pages, true };
    prog_registry_use(&k_row, 1);
    up_on(SCREEN_MOTOR);
    feed_tap(FEED_LONE, 181, P(18));                 /* the PROGRAM tab */
    s_fake_open = true;
    s_fake_stops = 0;
    s_fake_downs = 0;
}

/* A stop under the alert band acts on the first press, and the press
 * closes the alert; the same place is the alert's while it is no stop. */
TEST_CASE(a_stop_under_the_alert_acts_and_closes_it)
{
    fake_session();
    const int x = 500;
    const int y = P(518);
    CHECK(y >= UI_ALERT_Y);
    CHECK(motor_screen()->safe_at(x, y - UI_BAND_H));
    alert();
    feed_tap(FEED_LONE, x, y);
    CHECK_EQ(s_fake_stops, 1);
    CHECK(!alert_shows());

    /* Beside the stop, the press is the alert's and reaches no screen. */
    alert();
    feed_tap(FEED_LONE, x - 120, y);
    CHECK_EQ(s_fake_downs, 1);
    CHECK(!alert_shows());

    /* Closed, the session names no stop: the alert takes the press. */
    s_fake_open = false;
    alert();
    feed_tap(FEED_LONE, x, y);
    CHECK_EQ(s_fake_stops, 1);
    CHECK(!alert_shows());
    prog_registry_use(NULL, 0);
}

int main(void)
{
    RUN(disarm_ends_above_the_alert_band);
    RUN(no_stop_control_lies_under_the_alert_band);
    RUN(a_stop_under_the_alert_acts_and_closes_it);
    RUN(disarm_beside_the_alert_disarms_at_once);
    RUN(disarm_beside_the_alert_from_its_arrival_to_its_end);
    RUN(disarm_at_the_alerts_edges);
    RUN(an_alert_arriving_under_the_press_does_not_take_its_release);
    RUN(arm_beside_the_alert_holds_as_without_one);
    RUN(an_arm_not_answered_is_no_disarm);
    RUN(a_tap_on_the_alert_only_closes_it);
    RUN(a_disarm_press_beside_the_alert_slides_as_without_one);
    RUN(touch_lost_under_a_disarm_press_disarms);
    RUN(the_help_bubble_takes_no_disarm_and_starts_no_arm);
    RUN(servo_names_disarm_and_not_arm);
    RUN(motor_disarm_acts_under_the_bubble_and_beside_the_alert);
    RUN(motor_arm_under_the_bubble_starts_no_hold);
    RUN(output_off_acts_under_the_bubble_and_output_on_does_not);
    return test_summary("safe_press");
}
