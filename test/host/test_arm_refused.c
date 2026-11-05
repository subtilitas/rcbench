/**
 * @file
 * @brief A refused ARM returns the screen that asked to SAFE, and nothing
 *        else.
 *
 * Every way the bench refuses an ARM, from the code: on the panel, the
 * arming policy's handshake (a programmer on the port, a servo release not
 * taken, a servo rate not settled, the pulse endpoints owed before CLEAR,
 * after CLEAR and after the frame that arms, the arm's deadline passed, the
 * line never trusted) and the drain of the screens' commands (a pressed STOP
 * standing, a programmer on the port, an ask out of date); on the IOmcu, a
 * NACK (negative acknowledgement) of CLEAR or of the frame that arms
 * (LINK_NACK_NOT_ARMED for a failsafe, a programming port out of PWM or an
 * update, LINK_NACK_BAD_VALUE for a change of roles or a pole count).  Each
 * is driven through the policy as panel_arm.c drives it, and the stamp it
 * publishes is passed to the screen as panel_frame.c passes it: every frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "arming.h"
#include "gfx.h"
#include "heartbeat.h"
#include "kst_panel.h"
#include "link_control.h"
#include "link_msg.h"
#include "link_pages.h"
#include "motor_screen.h"
#include "outputs.h"
#include "safety_gate.h"
#include "servo_screen.h"
#include "settings.h"
#include "splash_screen.h"
#include "ui_band.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"

#include "motor_spots.h"
#include "servo_spots.h"

#define W UI_PANEL_W
#define H UI_PANEL_H
#define P(y) (UI_BAND_H + (y))

/* MOTOR & ESC's ARM and its SETTINGS tab; SERVO's ARM. */
#define MO_ARM_PX   MO_ARM_X
#define MO_ARM_PY   P(MO_ARM_Y)
#define MO_SET_PX   MO_TAB_X(2)
#define MO_SET_PY   P(MO_TAB_Y)
#define SV_ARM_PX   (SV_RC_X + SV_RC_W / 2)
#define SV_ARM_PY   P(SV_ARM_Y + SV_ARM_H / 2)
/* The keypad's frame, as the workbench's tests probe it. */
#define KP_AREA     ((gfx_rect_t){ UI_MARGIN, 3, 620, UI_BODY_H - 3 - UI_MARGIN })

#define SETTLE_MS HEARTBEAT_SETTLE_MS
#define WAIT_MS   ARMING_LINE_WAIT_MS
/* Frames of 26ms past ARM's 2s hold. */
#define HOLD_FRAMES ((int)(2.3f / 0.026f))

static gfx_color_t *fb;
static gfx_color_t *fb_safe;
static gfx_canvas_t cv;
static arming_t     a;

/* ------------------------------------------------------------- the glass */

/** @brief Frames of 26ms, drawn into both buffers. */
static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(0.026f);
        ui_router_render(&cv, i & 1);
    }
}

/** @brief The whole panel as a full redraw draws it, into @p into. */
static void picture(gfx_color_t *into)
{
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    if (into != fb) {
        memcpy(into, fb, (size_t)W * H * sizeof(gfx_color_t));
    }
}

/** @brief Whether the panel draws as it did when it was put up SAFE. */
static bool looks_safe(void)
{
    picture(fb);
    return memcmp(fb, fb_safe, (size_t)W * H * sizeof(gfx_color_t)) == 0;
}

/** @brief The servo programmer's port as the IOmcu's page gives it. */
static void kst_port(link_kst_state_t st)
{
    kst_prog_feed_t f;
    memset(&f, 0, sizeof(f));
    f.rd.phase = KST_LINK_READY;
    f.rd.known = true;
    f.rd.port  = st;
    kst_panel_feed(&f);
}

/** @brief @p screen up on a SAFE bench, nothing on the glass or queued,
 *         its SAFE picture kept. */
static void fresh(ui_screen_id_t screen)
{
    if (fb == NULL) {
        fb      = calloc((size_t)W * H, sizeof(gfx_color_t));
        fb_safe = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
        REQUIRE_ALLOC(fb_safe);
    }
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    settings_set_store(NULL);
    settings_init();
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    kst_panel_init((gfx_rect_t){ 0, UI_BAND_H, W, H - UI_BAND_H });
    kst_port(LINK_KST_PWM);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(screen);
    feed_reset();
    motor_screen_set_armed(false);
    motor_screen_set_throttle_pins(3u, true);
    motor_screen_set_throttle_protocol(4u);     /* DSHOT300 */
    servo_screen_feedback(0u, 0.0f, false);
    servo_screen_set_armed(false);
    motor_cmd_t mc;
    while (motor_screen_poll_cmd(&mc)) { }
    servo_cmd_t sc;
    while (servo_screen_take(&sc)) { }
    frames(UI_HOLD_FLASH_MS / 10 + 2);
    picture(fb_safe);
}

/** @brief One frame of the panel's loop after the control task's pass:
 *         the newest refusal and the bench's armed report, disarmed. */
static void frame_after(uint32_t refused)
{
    motor_screen_arm_refused(refused);
    servo_screen_arm_refused(refused);
    motor_screen_set_armed(false);
    servo_screen_set_armed(false);
    frames(1);
}

/** @brief MOTOR & ESC's ARM held until it posts, finger lifted, after the
 *         commands already posted are taken, as the panel takes them each
 *         frame.
 *  @return the stamp of the ARM posted, 0 for none */
static uint32_t motor_hold(void)
{
    motor_cmd_t old;
    while (motor_screen_poll_cmd(&old)) { }
    finger(FEED_LONE, MO_ARM_PX, MO_ARM_PY);
    frames(HOLD_FRAMES);
    lift(FEED_LONE);
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f, 0u };
    uint32_t ask = 0u;
    while (motor_screen_poll_cmd(&c)) {
        if (c.kind == MOTOR_CMD_ARM) {
            ask = c.ask;
        }
    }
    return ask;
}

/** @brief SERVO's ARM held until it posts, finger lifted, after the
 *         commands already posted are taken.
 *  @return the stamp of the ARM posted, 0 for none */
static uint32_t servo_hold(void)
{
    servo_cmd_t old;
    while (servo_screen_take(&old)) { }
    finger(FEED_LONE, SV_ARM_PX, SV_ARM_PY);
    frames(HOLD_FRAMES);
    lift(FEED_LONE);
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    uint32_t ask = 0u;
    while (servo_screen_take(&c)) {
        if (c.kind == SERVO_CMD_ARM) {
            ask = c.ask;
        }
    }
    return ask;
}

/** @brief SETTINGS' row of @p id on MOTOR & ESC; -1 when it has none. */
static int row_of(setting_id_t id)
{
    setting_id_t ids[16];
    const int n = motor_settings_rows(ids, 16);
    for (int i = 0; i < n; ++i) {
        if (ids[i] == id) {
            return i;
        }
    }
    return -1;
}

/** @brief Whether the keypad of @p id opens on a tap of its row, closed
 *         again after. */
static bool keypad_opens(setting_id_t id)
{
    const gfx_rect_t b = motor_settings_box(row_of(id));
    feed_tap(FEED_LONE, b.x + b.w / 2, P(b.y + b.h / 2));
    const bool open = motor_screen_keypad_setting() == (int)id;
    if (open) {
        ui_keypad_t probe;
        memset(&probe, 0, sizeof(probe));
        ui_keypad_open(&probe, KP_AREA, "", "", 0.0f, 0.0f, 1.0f, 0);
        const gfx_rect_t r = ui_keypad_key_rect(&probe, UI_KEY_CANCEL);
        feed_tap(FEED_LONE, r.x + r.w / 2, P(r.y + r.h / 2));
    }
    return open;
}

/* ------------------------------------------------- the bench's refusals */

/** @brief Every way an ARM is refused, from panel_arm.c and the IOmcu. */
typedef enum {
    R_PROG_HANDED,      /**< ARMING_HS_PROG: the port taken after the ask */
    R_SERVO_HELD,       /**< ARMING_HS_SERVO_HELD, ALERT_SERVO_NOT_RELEASED */
    R_SERVO_RATE,       /**< ARMING_HS_SERVO_RATE, ALERT_SERVO_RATE */
    R_ENDPOINTS,        /**< ARMING_HS_ENDPOINTS before CLEAR */
    R_CLOSED,           /**< ARMING_HS_CLOSED: the deadline before CLEAR */
    R_CLEAR_NACK,       /**< the IOmcu refuses CLEAR: BAD_VALUE */
    R_CLEARED_ENDPOINTS,/**< ARMING_HS_ENDPOINTS after CLEAR */
    R_ARM_FAILSAFE,     /**< the frame that arms: NOT_ARMED, failsafe */
    R_ARM_PORT_HELD,    /**< NOT_ARMED: a programming port out of PWM */
    R_ARM_ROLES,        /**< BAD_VALUE: armed for other roles */
    R_ARM_POLES,        /**< BAD_VALUE: an odd pole count */
    R_ARMED_ENDPOINTS,  /**< ARMING_HS_ENDPOINTS after the frame that arms */
    R_GIVE_UP,          /**< ARMING_ACT_GIVE_UP: the line never trusted */
    R_STOP_HELD,        /**< the drain: a pressed STOP stands */
    R_PROG_DRAIN,       /**< the drain: a programmer holds the port */
    R_STALE,            /**< the drain: a stop count or let-go moved */
    R_COUNT
} reason_t;

static const char *const k_reason[R_COUNT] = {
    "prog handed", "servo held", "servo rate", "endpoints", "closed",
    "clear nack", "cleared endpoints", "arm failsafe", "arm port held",
    "arm roles", "arm poles", "armed endpoints", "give up", "stop held",
    "prog drain", "stale",
};

/** @brief The IOmcu's answer to the frame that arms under @p r: 0 or the
 *         NACK reason, from its own rules (link_control_write(),
 *         safety_gate_hold_refuses_arm()). */
static uint8_t iomcu_arm_frame(reason_t r)
{
    uint16_t page[LINK_CT_COUNT] = { 0 };
    uint16_t in[LINK_CT_ARM_FRAME] = { LINK_ARM_THROTTLE, 0u, 14u };
    bool cleared = false;
    switch (r) {
    case R_ARM_PORT_HELD:
        return safety_gate_hold_refuses_arm(true, LINK_CT_ARM,
                                            LINK_CT_ARM_FRAME, in)
                   ? LINK_NACK_NOT_ARMED : 0u;
    case R_ARM_ROLES:
        page[LINK_CT_ARM] = LINK_ARM_SURFACE;
        break;
    case R_ARM_POLES:
        in[LINK_CT_MOTOR_POLES] = 13u;
        break;
    default:
        break;
    }
    return link_control_write(page, LINK_CT_ARM, LINK_CT_ARM_FRAME, in,
                              r != R_ARM_FAILSAFE, &cleared);
}

/** @brief The IOmcu's answer to CLEAR under @p r. */
static uint8_t iomcu_clear(reason_t r)
{
    uint16_t page[LINK_CT_COUNT] = { 0 };
    const uint16_t magic = (r == R_CLEAR_NACK) ? 0u : LINK_CLEAR_MAGIC;
    bool cleared = false;
    return link_control_write(page, LINK_CT_CLEAR, 1u, &magic, true,
                              &cleared);
}

/**
 * @brief The ARM stamped @p ask, for @p roles, refused by @p r, the way the
 *        control task meets it: a fresh policy that asks the far end, touch
 *        answering, the request taken at 1000ms.
 * @return true when the steps went as the reason says
 */
static bool refuse_by(reason_t r, uint8_t roles, uint32_t ask)
{
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + SETTLE_MS;
    arming_init(&a, t0 - 10u, SETTLE_MS);
    arming_set_line_wait(&a, WAIT_MS);
    arming_touch_seen(&a, t0);
    switch (r) {
    case R_STOP_HELD:
        arming_stop_pressed(&a);
        arming_request_arm_ask(&a, t0, roles, ask);  /* ignored */
        arming_refuse_ask(&a, ask);
        return arming_held(&a);
    case R_PROG_DRAIN:
    case R_STALE:
        arming_refuse_ask(&a, ask);
        return true;
    default:
        break;
    }
    arming_request_arm_ask(&a, t0, roles, ask);
    if (r == R_GIVE_UP) {
        arming_touch_seen(&a, at + WAIT_MS);
        return arming_step(&a, at + WAIT_MS) == ARMING_ACT_GIVE_UP;
    }
    arming_touch_seen(&a, at);
    arming_line_report(&a, true);
    if (arming_step(&a, at) != ARMING_ACT_ARM) {
        return false;
    }
    arming_hs_facts_t f = { .link_up = true, .rate_settled = true,
                            .port_taken = r == R_PROG_HANDED };
    const arming_hs_t handed = arming_handshake_step(&a, ARMING_HS_HANDED,
                                                     &f, at);
    if (r == R_PROG_HANDED) {
        return handed == ARMING_HS_PROG;
    }
    f.release_owed      = r == R_SERVO_HELD;
    f.rate_settled      = r != R_SERVO_RATE;
    f.endpoints_blocked = r == R_ENDPOINTS;
    const uint32_t when = (r == R_CLOSED) ? at + WAIT_MS + 1u : at;
    const arming_hs_t prepared =
        arming_handshake_step(&a, ARMING_HS_PREPARED, &f, when);
    switch (r) {
    case R_SERVO_HELD:
    case R_SERVO_RATE:
    case R_ENDPOINTS:
        /* arm_write_failed() with every exchange answered. */
        (void)arming_write_failed(&a, true);
        return prepared == (r == R_SERVO_HELD   ? ARMING_HS_SERVO_HELD
                            : r == R_SERVO_RATE ? ARMING_HS_SERVO_RATE
                                                : ARMING_HS_ENDPOINTS);
    case R_CLOSED:
        return prepared == ARMING_HS_CLOSED;
    default:
        break;
    }
    if (prepared != ARMING_HS_GO) {
        return false;
    }
    if (iomcu_clear(r) != 0u) {
        (void)arming_write_failed(&a, true);
        return r == R_CLEAR_NACK;
    }
    f.endpoints_blocked = r == R_CLEARED_ENDPOINTS;
    const arming_hs_t cleared =
        arming_handshake_step(&a, ARMING_HS_CLEARED, &f, at);
    if (r == R_CLEARED_ENDPOINTS) {
        return cleared == ARMING_HS_ENDPOINTS;
    }
    if (cleared != ARMING_HS_GO) {
        return false;
    }
    const uint8_t nack = iomcu_arm_frame(r);
    if (nack != 0u) {
        (void)arming_write_failed(&a, true);
        return (r == R_ARM_ROLES || r == R_ARM_POLES)
                   ? nack == LINK_NACK_BAD_VALUE
                   : nack == LINK_NACK_NOT_ARMED;
    }
    f.endpoints_blocked = r == R_ARMED_ENDPOINTS;
    return arming_handshake_step(&a, ARMING_HS_ARMED, &f, at)
           == ARMING_HS_ENDPOINTS;
}

/** @brief Whether the policy after @p r arms nothing, holds the latch as
 *         the reason left it and counts no stop. */
static bool nothing_armed(reason_t r)
{
    return !a.armed && !a.arming
           && arming_held(&a) == (r == R_STOP_HELD)
           && arming_stopped(&a) == (r == R_STOP_HELD)
           && arming_stop_count(&a) == (r == R_STOP_HELD ? 1u : 0u);
}

/* ----------------------------------------------------------- the stamps */

/* One count for every screen, 0 never given: 0xFFFFFFFF is followed by 1. */
TEST_CASE(the_stamp_after_0xffffffff_is_1)
{
    ui_value_ask_seed(0xFFFFFFFEu);
    CHECK_EQ(ui_value_new_ask(), 0xFFFFFFFFu);
    CHECK_EQ(ui_value_new_ask(), 1u);
    CHECK_EQ(ui_value_new_ask(), 2u);
    ui_value_ask_seed(0u);
    CHECK_EQ(ui_value_new_ask(), 1u);
}

/* The gate drops the ask waiting under the stamp refused, and nothing
 * else: no ask, another stamp, an answered ask, the same stamp again. */
TEST_CASE(a_gate_drops_only_the_ask_waiting_under_the_stamp)
{
    ui_value_gate_t g = { 0 };
    CHECK(!ui_value_refused(&g, 0u));
    CHECK_EQ(ui_value_state(&g), UI_VALUE_OFF);
    ui_value_ask_arm(&g, 7u);
    CHECK(!ui_value_refused(&g, 6u));
    CHECK(!ui_value_refused(&g, 8u));
    CHECK_EQ(ui_value_state(&g), UI_VALUE_ARMING);
    CHECK(ui_value_refused(&g, 7u));
    CHECK_EQ(ui_value_state(&g), UI_VALUE_OFF);
    CHECK(!ui_value_refused(&g, 7u));
    /* Answered armed: a late refusal leaves it LIVE. */
    ui_value_ask_arm(&g, 9u);
    (void)ui_value_report(&g, true);
    CHECK(!ui_value_refused(&g, 9u));
    CHECK_EQ(ui_value_state(&g), UI_VALUE_LIVE);
}

/* The policy names the stamp of the arm it gives up, keeps it through an
 * unstamped refusal, and a request it ignores takes no stamp. */
TEST_CASE(the_policy_names_the_newest_stamped_refusal)
{
    arming_init(&a, 0u, SETTLE_MS);
    arming_touch_seen(&a, 0u);
    CHECK_EQ(arming_refused_ask(&a), 0u);
    arming_request_arm_ask(&a, 0u, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 41u);
    CHECK_EQ(a.ask, 41u);
    arming_refused(&a);
    CHECK_EQ(arming_refused_ask(&a), 41u);
    /* An arm without a stamp, refused: the stamp stands. */
    arming_request_arm_roles(&a, 0u, OUT_ROLE_BIT(OUT_ROLE_THROTTLE));
    CHECK_EQ(a.ask, 0u);
    arming_refused(&a);
    arming_refuse_ask(&a, 0u);
    CHECK_EQ(arming_refused_ask(&a), 41u);
    /* Ignored under a pressed STOP: the stamp of the arm before stays. */
    arming_request_arm_ask(&a, 0u, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 42u);
    arming_stop_pressed(&a);
    arming_request_arm_ask(&a, 0u, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 43u);
    CHECK_EQ(a.ask, 42u);
    CHECK_EQ(arming_refused_ask(&a), 41u);
    CHECK(arming_refused_ask(NULL) == 0u);
    arming_refuse_ask(NULL, 5u);
    arming_request_arm_ask(NULL, 0u, 1u, 5u);
}

/* An arm that goes through names no refusal. */
TEST_CASE(an_arm_that_goes_through_names_no_refusal)
{
    const uint32_t t0 = 1000u;
    arming_init(&a, t0 - 10u, SETTLE_MS);
    arming_set_line_wait(&a, WAIT_MS);
    arming_touch_seen(&a, t0);
    arming_request_arm_ask(&a, t0, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 3u);
    arming_touch_seen(&a, t0 + SETTLE_MS);
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, t0 + SETTLE_MS), ARMING_ACT_ARM);
    arming_hs_facts_t f = { .link_up = true, .rate_settled = true };
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, t0 + SETTLE_MS),
             ARMING_HS_GO);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, t0 + SETTLE_MS),
             ARMING_HS_GO);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_ARMED, &f, t0 + SETTLE_MS),
             ARMING_HS_GO);
    CHECK(a.armed);
    CHECK_EQ(arming_refused_ask(&a), 0u);
}

/* ------------------------------------------------- MOTOR & ESC and SERVO */

/* Every refusal, on MOTOR & ESC: SAFE again at once, TARGET, Idle pulse,
 * Full pulse and ARM taking a press, the panel drawn as SAFE; no latch
 * moved and nothing armed. */
TEST_CASE(every_refusal_returns_motor_and_esc_to_safe_with_every_lock_lifted)
{
    for (int r = 0; r < R_COUNT; ++r) {
        fresh(SCREEN_MOTOR);
        const uint32_t ask = motor_hold();
        if (ask == 0u || !motor_screen_target_locked()) {
            T_FAIL("%s: no ARM waiting", k_reason[r]);
            continue;
        }
        if (!refuse_by((reason_t)r, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), ask)
            || !nothing_armed((reason_t)r)
            || arming_refused_ask(&a) != ask) {
            T_FAIL("%s: the policy did not refuse %u", k_reason[r],
                   (unsigned)ask);
        }
        /* Before the refusal is passed on, the screen still waits. */
        motor_screen_set_armed(false);
        frames(1);
        if (!motor_screen_target_locked()) {
            T_FAIL("%s: unlocked before the refusal", k_reason[r]);
        }
        frame_after(arming_refused_ask(&a));
        frames(UI_HOLD_FLASH_MS / 10 + 2);
        if (motor_screen_target_locked() || !looks_safe()) {
            T_FAIL("%s: not SAFE after the refusal", k_reason[r]);
        }
        feed_tap(FEED_LONE, MO_SET_PX, MO_SET_PY);
        if (!keypad_opens(SET_OUT_MIN_US) || !keypad_opens(SET_OUT_MAX_US)) {
            T_FAIL("%s: a pulse row stays locked", k_reason[r]);
        }
        const uint32_t again = motor_hold();
        if (again == 0u || again == ask) {
            T_FAIL("%s: ARM takes no new hold", k_reason[r]);
        }
    }
}

/* Every refusal, on SERVO: SAFE again at once, ARM taking a hold, the
 * panel drawn as SAFE; no latch moved and nothing armed. */
TEST_CASE(every_refusal_returns_servo_to_safe)
{
    for (int r = 0; r < R_COUNT; ++r) {
        fresh(SCREEN_SERVO);
        const uint32_t ask = servo_hold();
        if (ask == 0u || !servo_screen_arm_asked()) {
            T_FAIL("%s: no ARM waiting", k_reason[r]);
            continue;
        }
        if (!refuse_by((reason_t)r, OUT_ROLE_BIT(OUT_ROLE_SURFACE), ask)
            || !nothing_armed((reason_t)r)
            || arming_refused_ask(&a) != ask) {
            T_FAIL("%s: the policy did not refuse %u", k_reason[r],
                   (unsigned)ask);
        }
        servo_screen_set_armed(false);
        frames(1);
        if (!servo_screen_arm_asked()) {
            T_FAIL("%s: SAFE before the refusal", k_reason[r]);
        }
        frame_after(arming_refused_ask(&a));
        frames(UI_HOLD_FLASH_MS / 10 + 2);
        if (servo_screen_arm_asked() || !looks_safe()) {
            T_FAIL("%s: not SAFE after the refusal", k_reason[r]);
        }
        const uint32_t again = servo_hold();
        if (again == 0u || again == ask || !servo_screen_arm_asked()) {
            T_FAIL("%s: ARM takes no new hold", k_reason[r]);
        }
    }
}

/* A refusal that lands under a finger still resting on ARM after its hold
 * posted: the hold ends, the lift asks for nothing, the pulse rows open. */
TEST_CASE(a_refusal_under_a_finger_on_arm_ends_the_hold)
{
    fresh(SCREEN_MOTOR);
    finger(FEED_LONE, MO_ARM_PX, MO_ARM_PY);
    frames(HOLD_FRAMES);
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f, 0u };
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_ARM);
    frame_after(c.ask);
    CHECK(!motor_screen_target_locked());
    frames(HOLD_FRAMES);
    lift(FEED_LONE);
    CHECK(!motor_screen_poll_cmd(&c));
    feed_tap(FEED_LONE, MO_SET_PX, MO_SET_PY);
    CHECK(keypad_opens(SET_OUT_MIN_US));

    fresh(SCREEN_SERVO);
    finger(FEED_LONE, SV_ARM_PX, SV_ARM_PY);
    frames(HOLD_FRAMES);
    servo_cmd_t s = { .kind = SERVO_CMD_NONE };
    CHECK(servo_screen_take(&s));
    CHECK_EQ(s.kind, SERVO_CMD_ARM);
    frame_after(s.ask);
    CHECK(!servo_screen_arm_asked());
    frames(HOLD_FRAMES);
    lift(FEED_LONE);
    CHECK(!servo_screen_take(&s));
}

/* A refusal after a DISARM changes nothing: a hold begun since runs on
 * and posts its own ARM.  On MOTOR & ESC the ARM was answered armed and
 * DISARM tapped; on SERVO the ARM still waited when the screen was left,
 * which posts DISARM, and the policy withdrew it under that disarm. */
TEST_CASE(a_refusal_after_a_disarm_changes_nothing)
{
    fresh(SCREEN_MOTOR);
    const uint32_t ask = motor_hold();
    CHECK(ask != 0u);
    motor_screen_set_armed(true);
    frames(1);
    feed_tap(FEED_LONE, MO_ARM_PX, MO_ARM_PY);        /* DISARM */
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f, 0u };
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
    frame_after(0u);
    finger(FEED_LONE, MO_ARM_PX, MO_ARM_PY);
    frames(HOLD_FRAMES / 2);
    frame_after(ask);
    frames(HOLD_FRAMES / 2 + 2);
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_ARM);
    CHECK(c.ask != ask);
    lift(FEED_LONE);
    CHECK(motor_screen_target_locked());              /* the new one waits */

    fresh(SCREEN_SERVO);
    const uint32_t sask = servo_hold();
    CHECK(sask != 0u);
    arming_init(&a, 0u, SETTLE_MS);
    arming_touch_seen(&a, 0u);
    arming_request_arm_ask(&a, 0u, OUT_ROLE_BIT(OUT_ROLE_SURFACE), sask);
    ui_router_goto(SCREEN_OVERVIEW);
    servo_cmd_t s = { .kind = SERVO_CMD_NONE };
    CHECK(servo_screen_take(&s));
    CHECK_EQ(s.kind, SERVO_CMD_DISARM);
    ui_router_goto(SCREEN_SERVO);
    arming_hs_facts_t f = { .link_up = true, .rate_settled = true,
                            .disarm_asked = true };
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, 0u),
             ARMING_HS_WITHDRAWN);
    CHECK_EQ(arming_refused_ask(&a), sask);
    while (servo_screen_take(&s)) { }
    finger(FEED_LONE, SV_ARM_PX, SV_ARM_PY);
    frames(HOLD_FRAMES / 2);
    frame_after(arming_refused_ask(&a));
    frames(HOLD_FRAMES / 2 + 2);
    CHECK(servo_screen_take(&s));
    CHECK_EQ(s.kind, SERVO_CMD_ARM);
    CHECK(s.ask != sask);
    lift(FEED_LONE);
    CHECK(servo_screen_arm_asked());
}

/* After a STOP the stop's own cancel has made the screen SAFE; the
 * refusal of the ARM it overtook changes nothing, and the latch stands:
 * an ARM asked under it is refused in its turn, latch kept. */
TEST_CASE(a_refusal_after_a_stop_changes_nothing_and_keeps_the_latch)
{
    fresh(SCREEN_MOTOR);
    const uint32_t ask = motor_hold();
    arming_init(&a, 0u, SETTLE_MS);
    arming_touch_seen(&a, 0u);
    arming_request_arm_ask(&a, 0u, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), ask);
    arming_stop_pressed(&a);
    motor_screen_cancel_arm();
    frame_after(0u);
    CHECK(!motor_screen_target_locked());
    /* The handshake under way meets the stop: withdrawn. */
    arming_hs_facts_t f = { .link_up = true, .rate_settled = true };
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, 0u),
             ARMING_HS_WITHDRAWN);
    frame_after(arming_refused_ask(&a));
    CHECK(!motor_screen_target_locked());
    CHECK(arming_held(&a));
    CHECK(arming_stopped(&a));
    /* An ARM held under the standing STOP: refused at the drain. */
    const uint32_t under = motor_hold();
    CHECK(under != 0u);
    CHECK(motor_screen_target_locked());
    arming_request_arm_ask(&a, 0u, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), under);
    CHECK(!a.arming);
    arming_refuse_ask(&a, under);
    frame_after(arming_refused_ask(&a));
    CHECK(!motor_screen_target_locked());
    CHECK(arming_held(&a));
    CHECK(arming_stopped(&a));
    CHECK(!a.armed);
    CHECK_EQ(arming_stop_count(&a), 1u);
}

/* A late refusal of an older ask while a newer one waits leaves the newer
 * one waiting; its own refusal ends it.  Passed every frame, the stamp
 * already met changes nothing. */
TEST_CASE(a_late_refusal_of_an_older_ask_leaves_the_newer_one_waiting)
{
    fresh(SCREEN_MOTOR);
    const uint32_t first = motor_hold();
    const uint32_t second = motor_hold();
    CHECK(first != 0u);
    CHECK(second != 0u && second != first);
    frame_after(first);
    CHECK(motor_screen_target_locked());
    frame_after(second);
    CHECK(!motor_screen_target_locked());
    const uint32_t third = motor_hold();
    for (int i = 0; i < 3; ++i) {
        frame_after(second);
    }
    CHECK(motor_screen_target_locked());
    frame_after(third);
    CHECK(!motor_screen_target_locked());

    fresh(SCREEN_SERVO);
    const uint32_t s1 = servo_hold();
    const uint32_t s2 = servo_hold();
    CHECK(s2 != s1);
    frame_after(s1);
    CHECK(servo_screen_arm_asked());
    frame_after(s2);
    CHECK(!servo_screen_arm_asked());
}

/* An ARM asked on the other screen is not this one's: MOTOR & ESC asks,
 * is left, SERVO asks; the late refusal of MOTOR & ESC's ask leaves
 * SERVO's waiting, and SERVO's refusal reaches no MOTOR & ESC ask. */
TEST_CASE(a_refusal_of_another_screens_ask_changes_nothing)
{
    fresh(SCREEN_MOTOR);
    const uint32_t motor_ask = motor_hold();
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SERVO);
    const uint32_t servo_ask = servo_hold();
    CHECK(motor_ask != 0u && servo_ask != 0u && motor_ask != servo_ask);
    frame_after(motor_ask);
    CHECK(servo_screen_arm_asked());
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    const uint32_t motor_again = motor_hold();
    frame_after(servo_ask);
    CHECK(motor_screen_target_locked());
    frame_after(motor_again);
    CHECK(!motor_screen_target_locked());
}

/* The stamps across the 32-bit wrap: 0xFFFFFFFF refused, then 1 asked;
 * the old stamp, passed on every frame, leaves 1 waiting until its own
 * refusal. */
TEST_CASE(a_refusal_across_the_stamps_wrap_reaches_only_its_own_ask)
{
    fresh(SCREEN_MOTOR);
    ui_value_ask_seed(0xFFFFFFFEu);
    const uint32_t last = motor_hold();
    CHECK_EQ(last, 0xFFFFFFFFu);
    CHECK(refuse_by(R_ENDPOINTS, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), last));
    frame_after(arming_refused_ask(&a));
    CHECK(!motor_screen_target_locked());
    const uint32_t first = motor_hold();
    CHECK_EQ(first, 1u);
    frame_after(0xFFFFFFFFu);
    frame_after(0u);
    CHECK(motor_screen_target_locked());
    CHECK(refuse_by(R_SERVO_RATE, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), first));
    frame_after(arming_refused_ask(&a));
    CHECK(!motor_screen_target_locked());
}

int main(void)
{
    RUN(the_stamp_after_0xffffffff_is_1);
    RUN(a_gate_drops_only_the_ask_waiting_under_the_stamp);
    RUN(the_policy_names_the_newest_stamped_refusal);
    RUN(an_arm_that_goes_through_names_no_refusal);
    RUN(every_refusal_returns_motor_and_esc_to_safe_with_every_lock_lifted);
    RUN(every_refusal_returns_servo_to_safe);
    RUN(a_refusal_under_a_finger_on_arm_ends_the_hold);
    RUN(a_refusal_after_a_disarm_changes_nothing);
    RUN(a_refusal_after_a_stop_changes_nothing_and_keeps_the_latch);
    RUN(a_late_refusal_of_an_older_ask_leaves_the_newer_one_waiting);
    RUN(a_refusal_of_another_screens_ask_changes_nothing);
    RUN(a_refusal_across_the_stamps_wrap_reaches_only_its_own_ask);
    return test_summary("arm_refused");
}
