/*
 * The output value's state (ui_value_gate.h) against every control that
 * changes a value on MOTOR & ESC and on SERVO.
 *
 * Four states -- OFF, ARMING, LIVE, LEAVING, the last entered three ways: a
 * DISARM waiting to be taken, a DISARM taken, and a STOP -- times every
 * value control, times three events: an input arrives, the state's edge
 * arrives while the control is active, and a callback arrives late.  Each
 * cell holds the value, the picture, the dimming and the command queue to
 * the one rule: only LIVE takes input, the owners of the value end on the
 * edge out of LIVE, and nothing changes a value outside LIVE.
 *
 * The release a run of the automatic test ends with needs a supply and a
 * servo model; its row is in test_servo.c
 * (a_run_follows_the_value_state_at_every_edge).
 *
 * Touch is fed through the tracker and the router (touch_feed.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "gfx.h"
#include "motor_screen.h"
#include "servo_screen.h"
#include "settings.h"
#include "splash_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"

#define W 800
#define H 480
#define P(y) (UI_BAND_H + (y))

/* The states a cell starts in.  LEAVING three ways. */
enum { ST_OFF = 0, ST_ARMING, ST_LIVE, ST_LEAVING_POSTED, ST_LEAVING_TAKEN,
       ST_LEAVING_STOP, ST_COUNT };

static bool is_live(int st)    { return st == ST_LIVE; }
static bool is_leaving(int st) { return st >= ST_LEAVING_POSTED; }

/* The edges out of LIVE. */
enum { EDGE_DISARM_TAP = 0, EDGE_STOP, EDGE_REPORT_OFF, EDGE_LEAVE,
       EDGE_COUNT };

static gfx_color_t *fb;
static gfx_color_t *fb_was;
static gfx_canvas_t cv;

static void router_up(ui_screen_id_t id)
{
    if (fb == NULL) {
        fb     = calloc((size_t)W * H, sizeof(gfx_color_t));
        fb_was = calloc((size_t)W * H, sizeof(gfx_color_t));
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

static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(0.026f);
    }
}

/* Frames drawn into both buffers, so a flash, spent one a drawn frame, is
 * over. */
static void settle(void)
{
    for (int i = 0; i < 2 * UI_HOLD_FLASH_FRAMES + 2; ++i) {
        ui_router_tick(0.026f);
        ui_router_render(&cv, i & 1);
    }
}

static void picture(gfx_color_t *into)
{
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    if (into != fb) {
        memcpy(into, fb, (size_t)W * H * sizeof(gfx_color_t));
    }
}

static bool picture_unchanged(void)
{
    picture(fb);
    return memcmp(fb, fb_was, (size_t)W * H * sizeof(gfx_color_t)) == 0;
}

static gfx_color_t px(int x, int y) { return fb[(size_t)y * W + x]; }

static int count_px(int x0, int x1, int y0, gfx_color_t col)
{
    int n = 0;
    for (int y = y0; y < y0 + 16; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (px(x, y) == col) {
                ++n;
            }
        }
    }
    return n;
}

static bool tap_stop(void)
{
    const gfx_rect_t r = ui_band_stop_rect();
    feed_tap(3, r.x + r.w / 2, r.y + r.h / 2);
    return ui_router_take_stop();
}

/* ------------------------------------------------------------ the gate */

/* Every state under every event the gate takes: the state after it, and
 * what it did to LIVE. */
TEST_CASE(the_gate_moves_between_its_four_states)
{
    static const ui_value_gate_t from[4] = {
        [UI_VALUE_OFF]     = { false, false, false },
        [UI_VALUE_ARMING]  = { false, true,  false },
        [UI_VALUE_LIVE]    = { true,  false, false },
        [UI_VALUE_LEAVING] = { true,  false, true  },
    };
    enum { EV_ASK_ARM = 0, EV_DROP_ARM, EV_ASK_LEAVE, EV_HIDE, EV_ARMED,
           EV_DISARMED, EV_COUNT };
    static const ui_value_state_t to[4][EV_COUNT] = {
        [UI_VALUE_OFF] = { UI_VALUE_ARMING, UI_VALUE_OFF, UI_VALUE_OFF,
                           UI_VALUE_OFF, UI_VALUE_LIVE, UI_VALUE_OFF },
        [UI_VALUE_ARMING] = { UI_VALUE_ARMING, UI_VALUE_OFF, UI_VALUE_OFF,
                              UI_VALUE_OFF, UI_VALUE_LIVE, UI_VALUE_ARMING },
        [UI_VALUE_LIVE] = { UI_VALUE_LIVE, UI_VALUE_LIVE, UI_VALUE_LEAVING,
                            UI_VALUE_OFF, UI_VALUE_LIVE, UI_VALUE_OFF },
        [UI_VALUE_LEAVING] = { UI_VALUE_LEAVING, UI_VALUE_LEAVING,
                               UI_VALUE_LEAVING, UI_VALUE_OFF,
                               UI_VALUE_LEAVING, UI_VALUE_OFF },
    };
    for (int st = 0; st < 4; ++st) {
        CHECK_EQ(ui_value_state(&from[st]), (ui_value_state_t)st);
        CHECK_EQ(ui_value_live(&from[st]), st == UI_VALUE_LIVE);
        for (int ev = 0; ev < EV_COUNT; ++ev) {
            ui_value_gate_t g = from[st];
            ui_value_edge_t edge = UI_VALUE_EDGE_NONE;
            switch (ev) {
            case EV_ASK_ARM:   ui_value_ask_arm(&g); break;
            case EV_DROP_ARM:  ui_value_drop_arm(&g); break;
            case EV_ASK_LEAVE: edge = ui_value_ask_leave(&g); break;
            case EV_HIDE:      edge = ui_value_hide(&g); break;
            case EV_ARMED:     edge = ui_value_report(&g, true); break;
            default:           edge = ui_value_report(&g, false); break;
            }
            CHECK_EQ(ui_value_state(&g), to[st][ev]);
            if (ev >= EV_ASK_LEAVE) {
                const ui_value_edge_t want =
                    (st != UI_VALUE_LIVE && to[st][ev] == UI_VALUE_LIVE)
                        ? UI_VALUE_EDGE_LIVE
                    : (st == UI_VALUE_LIVE && to[st][ev] != UI_VALUE_LIVE)
                        ? UI_VALUE_EDGE_LEFT : UI_VALUE_EDGE_NONE;
                CHECK_EQ(edge, want);
            }
        }
    }
    /* A screen left with a DISARM asked reads the bench as disarmed, and
     * the armed reports that follow are not a new arm: LEAVING until the
     * bench answers, then OFF, and only then LIVE again. */
    ui_value_gate_t g = from[UI_VALUE_LIVE];
    CHECK_EQ(ui_value_ask_leave(&g), UI_VALUE_EDGE_LEFT);
    CHECK_EQ(ui_value_hide(&g), UI_VALUE_EDGE_NONE);
    CHECK_EQ(ui_value_state(&g), UI_VALUE_OFF);
    CHECK_EQ(ui_value_report(&g, true), UI_VALUE_EDGE_NONE);
    CHECK_EQ(ui_value_state(&g), UI_VALUE_LEAVING);
    CHECK_EQ(ui_value_report(&g, false), UI_VALUE_EDGE_NONE);
    CHECK_EQ(ui_value_report(&g, true), UI_VALUE_EDGE_LIVE);
}

/* ============================================================ MOTOR & ESC */

#define M_TRACK_X  72
#define M_TRACK_Y  P(356)
#define M_DOWN_X   33
#define M_UP_X     519
#define M_ARM_X    676
#define M_ARM_Y    P(318)
#define M_HINT_Y   P(390)
#define M_PX_PCT   (100.0f / 413.0f)

enum { MC_TRACK = 0, MC_PLUS, MC_MINUS, MC_KNOB, MC_COUNT };

static motor_cmd_t m_took(void)
{
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    (void)motor_screen_poll_cmd(&c);
    return c;
}

/* Every command waiting, as a bit per kind; the last throttle in @p pct. */
static unsigned m_kinds(float *pct)
{
    unsigned seen = 0u;
    for (motor_cmd_t c = m_took(); c.kind != MOTOR_CMD_NONE; c = m_took()) {
        seen |= 1u << (unsigned)c.kind;
        if (c.kind == MOTOR_CMD_THROTTLE && pct != NULL) {
            *pct = c.value;
        }
    }
    return seen;
}

#define M_BIT(k) (1u << (unsigned)(k))

static void m_knob(float span)
{
    motor_screen_knob_frame();
    motor_screen_knob(span);
}

static bool m_arm(void)
{
    finger(FEED_LONE, M_ARM_X, M_ARM_Y);
    frames((int)(2.3f / 0.026f));
    const motor_cmd_t c = m_took();
    lift(FEED_LONE);
    if (c.kind != MOTOR_CMD_ARM) {
        return false;
    }
    motor_screen_set_armed(true);
    return true;
}

/* MOTOR & ESC in state @p st.  Past OFF and ARMING the bench has been
 * driven to 24.2 %, 100 px of the track. */
static bool m_enter(int st)
{
    router_up(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    (void)m_kinds(NULL);
    if (st == ST_OFF) {
        return true;
    }
    if (st == ST_ARMING) {
        finger(FEED_LONE, M_ARM_X, M_ARM_Y);
        frames((int)(2.3f / 0.026f));
        const bool asked = m_took().kind == MOTOR_CMD_ARM;
        lift(FEED_LONE);
        return asked;
    }
    if (!m_arm()) {
        return false;
    }
    finger(FEED_LONE, M_TRACK_X + 28, M_TRACK_Y);
    glide(FEED_LONE, M_TRACK_X + 128, M_TRACK_Y, 8);
    lift(FEED_LONE);
    (void)m_kinds(NULL);
    if (st == ST_LEAVING_POSTED || st == ST_LEAVING_TAKEN) {
        feed_tap(FEED_LONE, M_ARM_X, M_ARM_Y);
        if (st == ST_LEAVING_TAKEN) {
            if (m_took().kind != MOTOR_CMD_DISARM) {
                return false;
            }
            motor_screen_set_armed(true);       /* not yet answered */
        }
    } else if (st == ST_LEAVING_STOP) {
        if (!tap_stop()) {
            return false;
        }
        motor_screen_cancel_arm();
        motor_screen_set_armed(true);           /* not yet answered */
    }
    return true;
}

/* The control used: what it posts on a live bench, from @p from. */
static float m_input(int control, float from)
{
    switch (control) {
    case MC_TRACK:
        finger(FEED_LONE, M_TRACK_X + 200, M_TRACK_Y);
        glide(FEED_LONE, M_TRACK_X + 240, M_TRACK_Y, 8);
        lift(FEED_LONE);
        return from + 40.0f * M_PX_PCT;
    case MC_PLUS:
        feed_tap(FEED_LONE, M_UP_X, M_TRACK_Y);
        return from + 1.0f;
    case MC_MINUS:
        feed_tap(FEED_LONE, M_DOWN_X, M_TRACK_Y);
        return from - 1.0f;
    default:
        m_knob(0.1f);
        return from + 10.0f;
    }
}

/* The dimming as drawn: the step buttons in their own colour and the hint
 * under the track, or dimmed with ARM FIRST. */
static bool m_drawn_live(void)
{
    picture(fb);
    const gfx_color_t sunk = ui_theme_color(UI_C_PANEL_SUNK);
    const bool keys = px(M_DOWN_X - 16, M_TRACK_Y - 12) == sunk
                      && px(M_UP_X - 16, M_TRACK_Y - 12) == sunk;
    const int warn = count_px(12, 100, M_HINT_Y, ui_theme_color(UI_C_WARN));
    return keys && warn == 0;
}

static bool m_drawn_refused(void)
{
    picture(fb);
    const gfx_color_t sunk = ui_theme_color(UI_C_PANEL_SUNK);
    return px(M_DOWN_X - 16, M_TRACK_Y - 12) != sunk
           && px(M_UP_X - 16, M_TRACK_Y - 12) != sunk
           && count_px(12, 100, M_HINT_Y, ui_theme_color(UI_C_WARN)) > 0;
}

/* An input arrives: taken in LIVE, refused everywhere else with the value,
 * the picture and the queue as they were. */
TEST_CASE(motor_an_input_is_taken_only_in_live)
{
    for (int st = 0; st < ST_COUNT; ++st) {
        for (int control = 0; control < MC_COUNT; ++control) {
            CHECK(m_enter(st));
            settle();
            const float before = motor_screen_throttle();
            CHECK_NEAR(before, (st >= ST_LIVE) ? 100.0f * M_PX_PCT : 0.0f,
                       0.01f);
            CHECK(is_live(st) ? m_drawn_live() : m_drawn_refused());
            picture(fb_was);

            const float want = m_input(control, before);
            float posted = -1.0f;
            const unsigned kinds = m_kinds(&posted);
            if (is_live(st)) {
                CHECK_EQ(kinds, M_BIT(MOTOR_CMD_THROTTLE));
                CHECK_NEAR(posted, want, 0.01f);
                CHECK_NEAR(motor_screen_throttle(), want, 0.01f);
                CHECK(m_drawn_live());
            } else {
                CHECK_EQ(kinds, (st == ST_LEAVING_POSTED)
                                    ? M_BIT(MOTOR_CMD_DISARM) : 0u);
                CHECK_EQ(motor_screen_throttle(), before);
                CHECK(picture_unchanged());
                CHECK(m_drawn_refused());
            }
            /* The bench's answer to what was asked: LEAVING ends at 0 %. */
            if (is_leaving(st)) {
                motor_screen_set_armed(false);
                CHECK_EQ(motor_screen_throttle(), 0.0f);
                CHECK_EQ(m_kinds(NULL), 0u);
            }
        }
    }
}

/* The control active on a live bench: a drag under way, a step button held
 * with its command waiting, the knob's command waiting. */
static void m_activate(int control)
{
    switch (control) {
    case MC_TRACK:
        finger(1, M_TRACK_X + 200, M_TRACK_Y);
        glide(1, M_TRACK_X + 240, M_TRACK_Y, 8);
        break;
    case MC_PLUS:  finger(1, M_UP_X, M_TRACK_Y); break;
    case MC_MINUS: finger(1, M_DOWN_X, M_TRACK_Y); break;
    default:       m_knob(0.1f); break;
    }
}

/* The edge out of LIVE while the control is active: its command is not
 * sent, the finger's further travel moves nothing, and the value stays
 * until the bench reports disarmed, which returns it to 0 %. */
TEST_CASE(motor_the_edge_out_of_live_ends_an_active_control)
{
    for (int edge = 0; edge < EDGE_COUNT; ++edge) {
        for (int control = 0; control < MC_COUNT; ++control) {
            CHECK(m_enter(ST_LIVE));
            m_activate(control);
            const float at_edge = motor_screen_throttle();
            CHECK(at_edge != 100.0f * M_PX_PCT);
            unsigned want = 0u;
            switch (edge) {
            case EDGE_DISARM_TAP:
                feed_tap(2, M_ARM_X, M_ARM_Y);
                want = M_BIT(MOTOR_CMD_DISARM);
                break;
            case EDGE_STOP:
                motor_screen_cancel_arm();
                break;
            case EDGE_REPORT_OFF:
                motor_screen_set_armed(false);
                break;
            default:
                ui_router_goto(SCREEN_OVERVIEW);
                ui_router_goto(SCREEN_MOTOR);
                want = M_BIT(MOTOR_CMD_DISARM);
                break;
            }
            CHECK_EQ(m_kinds(NULL), want);
            const float left = (edge == EDGE_REPORT_OFF) ? 0.0f : at_edge;
            CHECK_EQ(motor_screen_throttle(), left);
            if (edge != EDGE_REPORT_OFF) {
                motor_screen_set_armed(true);   /* not yet answered */
            }
            if (control != MC_KNOB) {
                glide(1, M_TRACK_X + 320, M_TRACK_Y, 8);
                lift(1);
            }
            motor_screen_knob_cancel();
            m_knob(0.1f);
            frames(4);
            CHECK_EQ(m_kinds(NULL), 0u);
            CHECK_EQ(motor_screen_throttle(), left);
            CHECK(m_drawn_refused());
            motor_screen_set_armed(false);
            CHECK_EQ(motor_screen_throttle(), 0.0f);
            CHECK_EQ(m_kinds(NULL), 0u);
        }
    }
}

/* The edge into LIVE, and the edge from LEAVING to OFF, under a finger
 * that pressed the control while it was refused: the press is no drag and
 * no step afterwards, and the value is 0 %. */
TEST_CASE(motor_an_edge_under_a_refused_press_starts_nothing)
{
    for (int st = 0; st < ST_COUNT; ++st) {
        if (is_live(st)) {
            continue;
        }
        for (int control = 0; control < MC_KNOB; ++control) {
            CHECK(m_enter(st));
            const int x = (control == MC_TRACK) ? M_TRACK_X + 200
                          : (control == MC_PLUS) ? M_UP_X : M_DOWN_X;
            finger(1, x, M_TRACK_Y);
            const unsigned kept = (st == ST_LEAVING_POSTED)
                                      ? M_BIT(MOTOR_CMD_DISARM) : 0u;
            CHECK_EQ(m_kinds(NULL), kept);
            /* OFF and ARMING arm; LEAVING is answered. */
            motor_screen_set_armed(!is_leaving(st));
            CHECK_EQ(motor_screen_throttle(), 0.0f);
            glide(1, x + 80, M_TRACK_Y, 8);
            lift(1);
            CHECK_EQ(m_kinds(NULL), 0u);
            CHECK_EQ(motor_screen_throttle(), 0.0f);
            if (!is_leaving(st)) {
                /* Live: the next press is taken, from 0 %. */
                CHECK(m_drawn_live());
                feed_tap(FEED_LONE, M_UP_X, M_TRACK_Y);
                CHECK_NEAR(m_took().value, 1.0f, 0.001f);
            } else {
                CHECK(m_drawn_refused());
            }
        }
    }
}

/* What arrives late -- the knob's withdrawal, and the bench's report said
 * again -- changes no value and posts nothing, in any state. */
TEST_CASE(motor_a_late_callback_changes_nothing)
{
    for (int st = 0; st < ST_COUNT; ++st) {
        CHECK(m_enter(st));
        if (st == ST_LEAVING_POSTED) {
            CHECK_EQ(m_kinds(NULL), M_BIT(MOTOR_CMD_DISARM));
        }
        const float before = motor_screen_throttle();
        motor_screen_knob_cancel();
        motor_screen_set_armed(st >= ST_LIVE);
        motor_screen_knob_cancel();
        frames(4);
        CHECK_EQ(motor_screen_throttle(), before);
        CHECK_EQ(m_kinds(NULL), 0u);
    }
}

/* ================================================================= SERVO */

#define SHAFT_X     300
#define SHAFT_Y     P((H - UI_BAND_H) / 2)
#define CENTRE_X    556
#define SWEEP_X     647
#define RELEASE_X   739
#define BTN_Y       P(366)
#define S_ARM_X     648
#define S_ARM_Y     P(404)
#define SPEED_Y     P(307)

enum { SC_DIAL = 0, SC_KNOB, SC_CENTRE, SC_SWEEP, SC_HOOK, SC_RELEASE,
       SC_COUNT };

#define S_BIT(k) (1u << (unsigned)(k))
#define S_DRIVES (S_BIT(SERVO_CMD_POSITION) | S_BIT(SERVO_CMD_CENTRE) \
                  | S_BIT(SERVO_CMD_SWEEP) | S_BIT(SERVO_CMD_HOLD))

static servo_cmd_t s_took(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_screen_take(&c);
    return c;
}

/* Every command waiting, as a bit per kind; the last one in @p last. */
static unsigned s_kinds(servo_cmd_t *last)
{
    unsigned seen = 0u;
    for (servo_cmd_t c = s_took(); c.kind != SERVO_CMD_NONE; c = s_took()) {
        seen |= S_BIT(c.kind);
        if (last != NULL) {
            *last = c;
        }
    }
    return seen;
}

/* The same over @p n frames. */
static unsigned s_kinds_over(int n)
{
    unsigned seen = s_kinds(NULL);
    for (int i = 0; i < n; ++i) {
        ui_router_tick(0.026f);
        seen |= s_kinds(NULL);
    }
    return seen;
}

static void s_knob(float span)
{
    servo_screen_knob_frame();
    servo_screen_knob(span);
}

static void dial_at(float deg, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SHAFT_X + (int)(110.0f * cosf(deg * k) + 0.5f);
    *y = SHAFT_Y - (int)(110.0f * sinf(deg * k) + 0.5f);
}

static bool s_arm(void)
{
    finger(FEED_LONE, S_ARM_X, S_ARM_Y);
    frames((int)(2.3f / 0.026f));
    const servo_cmd_t c = s_took();
    lift(FEED_LONE);
    if (c.kind != SERVO_CMD_ARM) {
        return false;
    }
    servo_screen_set_armed(true);
    return true;
}

/* SERVO in state @p st.  Past OFF and ARMING the servo has been driven to
 * 40 degrees and the position taken. */
static bool s_enter(int st)
{
    router_up(SCREEN_SERVO);
    servo_screen_feedback(0u, 0.0f, false);
    servo_screen_set_sweep(true);
    servo_screen_set_armed(false);
    (void)s_kinds(NULL);
    if (st == ST_OFF) {
        return true;
    }
    if (st == ST_ARMING) {
        finger(FEED_LONE, S_ARM_X, S_ARM_Y);
        frames((int)(2.3f / 0.026f));
        const bool asked = s_took().kind == SERVO_CMD_ARM;
        lift(FEED_LONE);
        return asked;
    }
    if (!s_arm()) {
        return false;
    }
    int x, y, x2, y2;
    dial_at(10.0f, &x, &y);
    dial_at(40.0f, &x2, &y2);
    finger(FEED_LONE, x, y);
    glide(FEED_LONE, x2, y2, 8);
    lift(FEED_LONE);
    (void)s_kinds(NULL);
    frames(40);
    if (st == ST_LEAVING_POSTED || st == ST_LEAVING_TAKEN) {
        feed_tap(FEED_LONE, S_ARM_X, S_ARM_Y);
        if (st == ST_LEAVING_TAKEN) {
            if (s_took().kind != SERVO_CMD_DISARM) {
                return false;
            }
            servo_screen_set_armed(true);       /* not yet answered */
        }
    } else if (st == ST_LEAVING_STOP) {
        if (!tap_stop()) {
            return false;
        }
        servo_screen_cancel_arm();
        servo_screen_set_armed(true);           /* not yet answered */
    }
    return true;
}

static void s_input(int control)
{
    int x, y;
    switch (control) {
    case SC_DIAL:
        dial_at(-30.0f, &x, &y);
        feed_tap(FEED_LONE, x, y);
        break;
    case SC_KNOB:    s_knob(0.1f); break;
    case SC_CENTRE:  feed_tap(FEED_LONE, CENTRE_X, BTN_Y); break;
    case SC_SWEEP:   feed_tap(FEED_LONE, SWEEP_X, BTN_Y); break;
    case SC_HOOK:    servo_screen_set_commanded(25.0f); break;
    default:         feed_tap(FEED_LONE, RELEASE_X, BTN_Y); break;
    }
}

/* The dimming as drawn: CENTRE in the accent and no ARM FIRST on the left
 * card, or CENTRE dimmed with the line. */
static bool s_drawn_live(void)
{
    picture(fb);
    return px(CENTRE_X - 36, BTN_Y - 10) == ui_theme_color(UI_C_ACCENT)
           && count_px(52, 200, P(396), ui_theme_color(UI_C_WARN)) == 0;
}

static bool s_drawn_refused(void)
{
    picture(fb);
    return px(CENTRE_X - 36, BTN_Y - 10) != ui_theme_color(UI_C_ACCENT)
           && px(SHAFT_X + 22, SHAFT_Y) != ui_theme_color(UI_C_ACCENT)
           && count_px(52, 200, P(396), ui_theme_color(UI_C_WARN)) > 0;
}

/*
 * An input arrives.  In LIVE each control posts its command and the value
 * is that command's; RELEASE posts and the value is the rest.  In every
 * other state nothing that drives is posted and the value, the horn and
 * the picture are as they were; RELEASE is still sent, behind no DISARM,
 * and moves no value.
 */
TEST_CASE(servo_an_input_is_taken_only_in_live)
{
    static const servo_cmd_kind_t posts[SC_COUNT] = {
        SERVO_CMD_POSITION, SERVO_CMD_POSITION, SERVO_CMD_CENTRE,
        SERVO_CMD_SWEEP, SERVO_CMD_POSITION, SERVO_CMD_RELEASE,
    };
    for (int st = 0; st < ST_COUNT; ++st) {
        for (int control = 0; control < SC_COUNT; ++control) {
            CHECK(s_enter(st));
            settle();
            const uint16_t before = servo_screen_commanded();
            const uint16_t drawn  = servo_screen_drawn();
            if (st >= ST_LIVE) {
                CHECK(before > 1700);
            } else {
                CHECK_EQ(before, 1500);
            }
            CHECK(is_live(st) ? s_drawn_live() : s_drawn_refused());
            picture(fb_was);

            s_input(control);
            servo_cmd_t last = { .kind = SERVO_CMD_NONE };
            const unsigned kinds = s_kinds(&last);
            if (is_live(st)) {
                CHECK_EQ(kinds, S_BIT(posts[control]));
                switch (control) {
                case SC_DIAL:
                    CHECK(last.value_us < 1400);
                    CHECK_EQ(servo_screen_commanded(), last.value_us);
                    break;
                case SC_KNOB:
                case SC_HOOK:
                    CHECK(last.value_us != before);
                    CHECK_EQ(servo_screen_commanded(), last.value_us);
                    break;
                case SC_SWEEP:
                    CHECK(servo_screen_sweeping());
                    break;
                default:        /* CENTRE and RELEASE */
                    CHECK_EQ(servo_screen_commanded(), 1500);
                    break;
                }
                CHECK(s_drawn_live());
            } else {
                unsigned want = 0u;
                if (st == ST_LEAVING_POSTED) {
                    want = S_BIT(SERVO_CMD_DISARM);
                } else if (control == SC_RELEASE) {
                    want = S_BIT(SERVO_CMD_RELEASE);
                }
                CHECK_EQ(kinds, want);
                CHECK_EQ(servo_screen_commanded(), before);
                CHECK(!servo_screen_sweeping());
                frames(40);
                CHECK_EQ(servo_screen_drawn(), drawn);
                CHECK_EQ(s_kinds(NULL), 0u);
                CHECK(picture_unchanged());
                CHECK(s_drawn_refused());
            }
            if (is_leaving(st)) {
                servo_screen_set_armed(false);
                CHECK_EQ(servo_screen_commanded(), before);
                CHECK_EQ(s_kinds(NULL), 0u);
            }
        }
    }
}

/* What owns the value on a live bench. */
enum { OWN_DRAG = 0, OWN_KNOB, OWN_CENTRE, OWN_SWEEP_ASKED, OWN_SWEEP,
       OWN_PAUSE, OWN_HELD, OWN_COUNT };

/* The owner made active; the value the edge has to leave on the screen. */
static uint16_t s_activate(int owner, uint16_t *pause_seq)
{
    int x, y, x2, y2;
    const uint16_t driven = servo_screen_commanded();
    servo_cmd_t c;
    *pause_seq = 0u;
    switch (owner) {
    case OWN_DRAG:
        dial_at(-20.0f, &x, &y);
        dial_at(-40.0f, &x2, &y2);
        finger(1, x, y);
        glide(1, x2, y2, 8);
        return servo_screen_commanded();
    case OWN_KNOB:
        /* Withdrawn at the edge with the value it replaced. */
        s_knob(0.1f);
        return driven;
    case OWN_CENTRE:
        feed_tap(FEED_LONE, CENTRE_X, BTN_Y);
        return servo_screen_commanded();
    case OWN_SWEEP_ASKED:
        feed_tap(FEED_LONE, SWEEP_X, BTN_Y);
        return servo_screen_commanded();
    case OWN_SWEEP:
    case OWN_PAUSE:
        feed_tap(FEED_LONE, SWEEP_X, BTN_Y);
        c = s_took();
        servo_screen_sweep_started(c.start_seq, 0u, SERVO_SWEEP_FROM_REST,
                                   0u);
        frames(12);
        if (owner == OWN_PAUSE) {
            feed_tap(FEED_LONE, SWEEP_X, BTN_Y);
            c = s_took();
            *pause_seq = c.pause_seq;
        }
        return servo_screen_commanded();
    default:
        return driven;      /* the position held since s_enter() */
    }
}

/*
 * The edge out of LIVE while something owns the value: a drag, the knob's
 * command, CENTRE's, a sweep asked for, running or paused, a held
 * position.  The owner ends at the edge: nothing that drives is posted
 * from it on -- not by a frame, the finger's travel, a tap on the sweep
 * button, a change of SPEED, or the panel's late notifications -- and the
 * value stays, through the bench's answer, until the next arm sets the
 * rest.
 */
TEST_CASE(servo_the_edge_out_of_live_ends_every_owner)
{
    for (int edge = 0; edge < EDGE_COUNT; ++edge) {
        for (int owner = 0; owner < OWN_COUNT; ++owner) {
            CHECK(s_enter(ST_LIVE));
            uint16_t pause_seq = 0u;
            const uint16_t left = s_activate(owner, &pause_seq);
            unsigned want = 0u;
            switch (edge) {
            case EDGE_DISARM_TAP:
                feed_tap(2, S_ARM_X, S_ARM_Y);
                want = S_BIT(SERVO_CMD_DISARM);
                break;
            case EDGE_STOP:
                servo_screen_cancel_arm();
                break;
            case EDGE_REPORT_OFF:
                servo_screen_set_armed(false);
                break;
            default:
                ui_router_goto(SCREEN_OVERVIEW);
                ui_router_goto(SCREEN_SERVO);
                want = S_BIT(SERVO_CMD_DISARM);
                break;
            }
            CHECK(!servo_screen_sweeping());
            CHECK(!servo_screen_paused());
            CHECK_EQ(s_kinds(NULL), want);
            CHECK_EQ(servo_screen_commanded(), left);
            if (edge != EDGE_REPORT_OFF) {
                servo_screen_set_armed(true);   /* not yet answered */
            }

            unsigned seen = s_kinds_over(40);
            if (owner == OWN_DRAG) {
                int x, y;
                dial_at(-70.0f, &x, &y);
                glide(1, x, y, 8);
                lift(1);
            }
            servo_screen_knob_cancel();
            s_knob(0.1f);
            feed_tap(FEED_LONE, SWEEP_X, BTN_Y);
            seen |= s_kinds_over(4);
            feed_tap(FEED_LONE, 521, SPEED_Y);
            seen |= s_kinds_over(4);
            /* Late: the panel lets go of the pause, a start is
             * acknowledged, a reading arrives and goes. */
            servo_screen_released(pause_seq);
            servo_screen_sweep_started(1u, 0u, SERVO_SWEEP_FROM_REST, 0u);
            servo_screen_sweep_held(pause_seq, 100u);
            seen |= s_kinds_over(40);
            CHECK_EQ(seen & S_DRIVES, 0u);
            CHECK_EQ(seen & S_BIT(SERVO_CMD_ARM), 0u);
            CHECK(!servo_screen_sweeping());
            CHECK(!servo_screen_paused());
            CHECK_EQ(servo_screen_commanded(), left);
            CHECK(s_drawn_refused());

            servo_screen_set_armed(false);
            CHECK_EQ(s_kinds_over(4) & S_DRIVES, 0u);
            CHECK_EQ(servo_screen_commanded(), left);
            CHECK(s_arm());
            CHECK_EQ(servo_screen_commanded(), 1500);
            CHECK_EQ(s_kinds(NULL), 0u);
        }
    }
}

/* The edge into LIVE, and the edge from LEAVING to OFF, under a finger
 * that pressed the dial while it was refused: no drag afterwards.  Into
 * LIVE the value is the rest; into OFF it stays. */
TEST_CASE(servo_an_edge_under_a_refused_press_starts_nothing)
{
    for (int st = 0; st < ST_COUNT; ++st) {
        if (is_live(st)) {
            continue;
        }
        CHECK(s_enter(st));
        const uint16_t before = servo_screen_commanded();
        int x, y, x2, y2;
        dial_at(-20.0f, &x, &y);
        dial_at(-50.0f, &x2, &y2);
        finger(1, x, y);
        CHECK_EQ(s_kinds(NULL), (st == ST_LEAVING_POSTED)
                                    ? S_BIT(SERVO_CMD_DISARM) : 0u);
        servo_screen_set_armed(!is_leaving(st));
        glide(1, x2, y2, 8);
        lift(1);
        CHECK_EQ(s_kinds(NULL), 0u);
        CHECK_EQ(servo_screen_commanded(), is_leaving(st) ? before : 1500);
        if (!is_leaving(st)) {
            CHECK(s_drawn_live());
            s_knob(0.01f);
            CHECK_EQ(s_took().value_us, 1510);
        } else {
            CHECK(s_drawn_refused());
        }
    }
}

/*
 * What arrives late changes no value and posts nothing outside LIVE: the
 * panel letting go of a pause, a start and a hold acknowledged, feedback
 * arriving and going, the knob's withdrawal, the bench's report said
 * again.  In LIVE the release of the pause the screen is on sets the rest.
 */
TEST_CASE(servo_a_late_callback_changes_no_value_outside_live)
{
    for (int st = 0; st < ST_COUNT; ++st) {
        CHECK(s_enter(st));
        if (st == ST_LEAVING_POSTED) {
            CHECK_EQ(s_kinds(NULL), S_BIT(SERVO_CMD_DISARM));
        }
        if (is_live(st)) {
            continue;
        }
        const uint16_t before = servo_screen_commanded();
        for (uint16_t seq = 0u; seq < 3u; ++seq) {
            servo_screen_released(seq);
            servo_screen_sweep_started(seq, 0u, SERVO_SWEEP_FROM_REST, 0u);
            servo_screen_sweep_held(seq, 100u);
        }
        servo_screen_feedback(1900u, 0.2f, true);
        servo_screen_feedback(0u, 0.0f, false);
        servo_screen_knob_cancel();
        servo_screen_set_armed(st >= ST_LIVE);
        CHECK_EQ(s_kinds_over(40), 0u);
        CHECK_EQ(servo_screen_commanded(), before);
        CHECK(!servo_screen_sweeping());
        CHECK(!servo_screen_paused());
    }

    /* LIVE, paused, and the panel lets that pause go: the rest. */
    CHECK(s_enter(ST_LIVE));
    uint16_t pause_seq = 0u;
    (void)s_activate(OWN_PAUSE, &pause_seq);
    CHECK(servo_screen_paused());
    servo_screen_released(pause_seq);
    CHECK_EQ(servo_screen_commanded(), 1500);
    s_knob(0.01f);
    CHECK_EQ(s_took().value_us, 1510);

    /* The same pause let go of after each edge out of LIVE: no value. */
    for (int edge = 0; edge < EDGE_COUNT; ++edge) {
        CHECK(s_enter(ST_LIVE));
        (void)s_activate(OWN_PAUSE, &pause_seq);
        const uint16_t left = servo_screen_commanded();
        switch (edge) {
        case EDGE_DISARM_TAP: feed_tap(2, S_ARM_X, S_ARM_Y); break;
        case EDGE_STOP:       servo_screen_cancel_arm(); break;
        case EDGE_REPORT_OFF: servo_screen_set_armed(false); break;
        default:
            ui_router_goto(SCREEN_OVERVIEW);
            ui_router_goto(SCREEN_SERVO);
            break;
        }
        servo_screen_released(pause_seq);
        CHECK_EQ(servo_screen_commanded(), left);
    }
}

int main(void)
{
    RUN(the_gate_moves_between_its_four_states);
    RUN(motor_an_input_is_taken_only_in_live);
    RUN(motor_the_edge_out_of_live_ends_an_active_control);
    RUN(motor_an_edge_under_a_refused_press_starts_nothing);
    RUN(motor_a_late_callback_changes_nothing);
    RUN(servo_an_input_is_taken_only_in_live);
    RUN(servo_the_edge_out_of_live_ends_every_owner);
    RUN(servo_an_edge_under_a_refused_press_starts_nothing);
    RUN(servo_a_late_callback_changes_no_value_outside_live);
    return test_summary("value_state");
}
