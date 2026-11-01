/*
 * MOTOR & ESC's PROGRAM: its list (the registry's MOTOR rows, no modelled
 * protocol among them), the STICK MENU row that opens ESC STICK full width,
 * the output strip in place of the tabs and the output card, its text per
 * state and its fit in both languages, the session's hold on every press,
 * the home tag's levels and the DISARM on leaving.
 *
 * Driven through the router, in panel coordinates, as the panel drives it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "shot_facts_rig.h"
#include "touch_feed.h"

#include "esc_stick_panel.h"
#include "esc_wire_panel.h"
#include "link_pages.h"
#include "gfx.h"
#include "motor_screen.h"
#include "prog_registry.h"
#include "settings.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_workbench.h"
#include "motor_spots.h"

#define W UI_PANEL_W
#define H UI_PANEL_H
#define P(y) (UI_BAND_H + (y))

/* From motor_spots.h: the strip's tabs, 150px a tab 4px apart from x 9,
 * 40px high from y 8; the CONTROL card's ARM; PROGRAM's first row, in the
 * page from y 56. */
#define TAB_Y       P(MO_TAB_Y)
#define TAB_X(i)    MO_TAB_X(i)
#define ARM_X       MO_ARM_X
#define ARM_Y       P(MO_ARM_Y)
#define ROW_X       MO_PROG_ROW_X
#define ROW_Y       P(MO_PROG_ROW_Y)
/* The stick session's makers, from y 70 in rows of 36. */
#define SP_ROW_Y(n) P(70 + (n) * 36 + 16)

static gfx_color_t *fb;
static gfx_canvas_t cv;

static bool mem_load(float *values, int count)
{
    (void)values;
    (void)count;
    return false;
}

static bool mem_save(const float *values, int count)
{
    (void)values;
    (void)count;
    return true;
}

static const settings_store_t k_store = { mem_load, mem_save, NULL, NULL };

/* -------------------------------------------------------------- the rig */

static motor_cmd_t took(void)
{
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    (void)motor_screen_poll_cmd(&c);
    return c;
}

static void drain(void)
{
    while (took().kind != MOTOR_CMD_NONE) { }
}

static int s_frame;

static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(0.026f);
        ui_router_render(&cv, s_frame & 1);
        ++s_frame;
    }
}

static gfx_color_t px(int x, int y) { return fb[(size_t)y * W + x]; }

/* A supply sample as the panel takes it. */
static void supply(bool online, bool output, uint8_t ok, float v, float i)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = online;
    st.output = output;
    st.ok = ok;
    st.v = v;
    st.i = i;
    st.mode = output ? SUPPLY_MODE_CV : SUPPLY_MODE_OFF;
    supply_screen_push(&st);
}

/* MOTOR & ESC opened from its tile on a SAFE bench, DSHOT300 on GP0 and
 * GP1, nothing on the glass, PROGRAM's list up. */
static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    settings_set_store(&k_store);
    settings_init();
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    supply(false, false, 0u, 0.0f, 0.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    feed_reset();
    motor_screen_set_armed(false);
    motor_screen_set_throttle_pins(3u, true);
    motor_screen_set_throttle_protocol(4u);     /* DSHOT300 */
    esc_stick_panel_set_throttle(true, 1);
    drain();
    feed_tap(FEED_LONE, TAB_X(1), TAB_Y);
    frames(2);
}

static void open_session(void)
{
    CHECK(!motor_screen()->covered());
    feed_tap(FEED_LONE, ROW_X, ROW_Y);
    frames(2);
    CHECK(motor_screen()->covered());
    CHECK(!shot_facts_now().no_session);
}

static void home_tag(void)
{
    const gfx_rect_t r = ui_home_tag_rect(ui_router_title(ui_router_current()));
    feed_tap(FEED_LONE, r.x + r.w / 2, r.y + r.h / 2);
}

/* The output strip's text for the bench fresh() sets up. */
static void strip(char *out, size_t size, bool armed, bool *warn)
{
    (void)motor_output_strip(out, size, 4u, 3u, true, armed, warn);
}

/* ------------------------------------------------------------- the list */

/*
 * PROGRAM is the strip's second tab, between LIVE and SETTINGS, and its
 * list is the registry's MOTOR rows: one, STICK MENU's ESC STICK, live
 * without a capability and never one of the modelled protocols.
 */
TEST_CASE(program_lists_the_stick_menu_and_no_modelled_protocol)
{
    fresh();
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(!motor_screen_session_shown());

    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    int motor = 0;
    for (size_t i = 0; i < n; ++i) {
        if (rows[i].device != PROG_DEV_MOTOR) {
            continue;
        }
        ++motor;
        CHECK_EQ(rows[i].task, (int)UI_WB_PROGRAM);
        if (rows[i].page_set == &esc_wire_pages) {
            /* Dimmed until the IOmcu reports the capability. */
            CHECK_EQ(rows[i].needs_capability, (unsigned)LINK_CAP_PROGRAM);
            CHECK_EQ(prog_reg_state(rows, n, i, 0u), (int)PROG_ROW_NO_CAP);
            CHECK_EQ(prog_reg_state(rows, n, i, LINK_CAP_PROGRAM),
                     (int)PROG_ROW_LIVE);
            CHECK(rows[i].full_width);
            continue;
        }
        CHECK(rows[i].page_set == &esc_stick_pages);
        CHECK(rows[i].full_width);
        CHECK_EQ(rows[i].group, (int)TX_PL_GRP_STICK);
        CHECK_EQ(prog_reg_state(rows, n, i, 0u), (int)PROG_ROW_LIVE);
    }
    CHECK_EQ(motor, 2);
    CHECK_STR_EQ(TR(PL_GRP_STICK), "STICK MENU");
    CHECK_STR_EQ(TR(PL_STICK), "ESCs by throttle stick");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(TR(PL_GRP_STICK), "STICK-MENÜ");
    CHECK_STR_EQ(TR(PL_STICK), "ESCs per Gasknüppel");
    ui_text_set_language(UI_LANG_EN);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
}

/* A press on the row that slides off it before the release opens nothing;
 * a cancel (the router's gestures dropped) drops the press with it. */
TEST_CASE(a_row_press_that_slides_off_or_is_cancelled_opens_nothing)
{
    fresh();
    finger(FEED_LONE, ROW_X, ROW_Y);
    glide(FEED_LONE, ROW_X, ROW_Y + 120, 6);
    lift(FEED_LONE);
    frames(1);
    CHECK(!motor_screen_session_shown());

    finger(FEED_LONE, ROW_X, ROW_Y);
    ui_router_cancel_gestures();
    lift(FEED_LONE);
    frames(1);
    CHECK(!motor_screen_session_shown());
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);

    /* And the plain tap opens it, on the makers. */
    open_session();
    CHECK(motor_screen_session_shown());
    CHECK_EQ(esc_stick_panel_level(), 0);
}

/* ---------------------------------------------------------- the session */

/*
 * What MOTOR & ESC names to the router as a press that stops the bench is
 * the session's while one is shown: DISARM on the rail on an armed bench
 * without one, and nothing at DISARM's place under ESC STICK, whose own
 * ABORT is named while a run is under way (test_esc_stick_panel_touch.c).
 */
TEST_CASE(the_session_answers_for_what_stops_the_bench)
{
    fresh();
    const ui_screen_t *scr = motor_screen();
    motor_screen_set_armed(true);
    CHECK(scr->safe_at(ARM_X, ARM_Y - UI_BAND_H));
    motor_screen_set_armed(false);
    CHECK(!scr->safe_at(ARM_X, ARM_Y - UI_BAND_H));
    open_session();
    CHECK(motor_screen_session_shown());
    motor_screen_set_armed(true);
    CHECK(!scr->safe_at(ARM_X, ARM_Y - UI_BAND_H));
    motor_screen_set_armed(false);
}

/*
 * The session is full width: the tabs and the output card are gone, the
 * output strip is in the strip row, and no press reaches a tab or ARM.
 */
TEST_CASE(a_full_width_session_shows_the_strip_not_the_card)
{
    fresh();
    const gfx_color_t tab_fill = px(TAB_X(1) - 40, TAB_Y);
    const gfx_color_t arm_fill = px(ARM_X - 80, ARM_Y);
    const gfx_color_t bg = ui_theme_color(UI_C_BG);
    CHECK(tab_fill != bg);
    CHECK(arm_fill != bg);

    open_session();
    CHECK(motor_screen_session_shown());
    CHECK(px(TAB_X(1) - 40, TAB_Y) != tab_fill);
    CHECK(px(ARM_X - 80, ARM_Y) != arm_fill);

    /* The strip: TARGET dim from x 12 in the strip row, the bench's SAFE in
     * the OK colour after it. */
    int dim = 0, ok = 0;
    for (int y = P(PROG_FULL_STRIP_Y);
         y < P(PROG_FULL_STRIP_Y + PROG_FULL_STRIP_H); ++y) {
        for (int x = 0; x < W; ++x) {
            if (px(x, y) == ui_theme_color(UI_C_TEXT_DIM)) {
                ++dim;
            }
            if (px(x, y) == ui_theme_color(UI_C_OK)) {
                ++ok;
            }
        }
    }
    CHECK(dim > 0);
    CHECK(ok > 0);

    /* The tabs and ARM take nothing. */
    feed_tap(FEED_LONE, TAB_X(2), TAB_Y);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(motor_screen_session_shown());
    finger(FEED_LONE, ARM_X, ARM_Y);
    for (int i = 0; i < (int)(2.3f / 0.026f); ++i) {
        ui_router_tick(0.026f);
    }
    lift(FEED_LONE);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK(motor_screen_session_shown());
}

/*
 * The strip's text per state: the bound target, the binding not read, no
 * pin; the supply not answering, off, on without readings and on with
 * them; SAFE and ARMED.  English and German.
 */
TEST_CASE(the_strip_reads_the_target_the_supply_and_the_bench)
{
    fresh();
    char line[160];
    bool warn = true;

    supply(false, false, 0u, 0.0f, 0.0f);
    strip(line, sizeof(line), false, &warn);
    CHECK_STR_EQ(line, "TARGET DSHOT300 GPIO 0 GPIO 1   SUPPLY --   SAFE");
    CHECK(!warn);

    supply(true, false, 0u, 0.0f, 0.0f);
    strip(line, sizeof(line), false, NULL);
    CHECK_STR_EQ(line, "TARGET DSHOT300 GPIO 0 GPIO 1   SUPPLY OFF   SAFE");

    supply(true, true, SUPPLY_OK_VOLTAGE, 7.6f, 0.0f);
    strip(line, sizeof(line), false, NULL);
    CHECK_STR_EQ(line, "TARGET DSHOT300 GPIO 0 GPIO 1   SUPPLY ON   SAFE");

    supply(true, true, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT, 7.6f, 1.0f);
    strip(line, sizeof(line), true, NULL);
    CHECK_STR_EQ(line,
                 "TARGET DSHOT300 GPIO 0 GPIO 1   SUPPLY 7.60V  1.00A   ARMED");

    /* A reading below zero is no reading. */
    supply(true, true, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT, 7.6f, -0.1f);
    strip(line, sizeof(line), false, NULL);
    CHECK(strstr(line, "SUPPLY ON") != NULL);

    /* The target not read, then no pin bound: both in the warning colour. */
    (void)motor_output_strip(line, sizeof(line), 4u, 3u, false, false, &warn);
    CHECK(warn);
    CHECK(strstr(line, "DSHOT300") == NULL);
    (void)motor_output_strip(line, sizeof(line), 4u, 0u, true, false, &warn);
    CHECK(warn);
    CHECK(strncmp(line, "TARGET ", 7) == 0);

    ui_text_set_language(UI_LANG_DE);
    supply(true, true, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT, 7.6f, 1.0f);
    strip(line, sizeof(line), false, NULL);
    CHECK_STR_EQ(line,
                 "ZIEL DSHOT300 GPIO 0 GPIO 1   NETZTEIL 7.60V  1.00A   SAFE");
    supply(true, false, 0u, 0.0f, 0.0f);
    strip(line, sizeof(line), true, NULL);
    CHECK_STR_EQ(line, "ZIEL DSHOT300 GPIO 0 GPIO 1   NETZTEIL AUS   ARMED");
    supply(false, false, 0u, 0.0f, 0.0f);
    strip(line, sizeof(line), false, NULL);
    CHECK_STR_EQ(line, "ZIEL DSHOT300 GPIO 0 GPIO 1   NETZTEIL --   SAFE");
    ui_text_set_language(UI_LANG_EN);

    /* No buffer, or none of it: nothing written, nothing read past. */
    CHECK_EQ(motor_output_strip(NULL, 10u, 4u, 3u, true, false, NULL), 0u);
    CHECK_EQ(motor_output_strip(line, 0u, 4u, 3u, true, false, NULL), 0u);
    /* A short buffer: cut, terminated, the length what it holds. */
    char small[12];
    memset(small, 'x', sizeof(small));
    CHECK_EQ(motor_output_strip(small, sizeof(small), 4u, 3u, true, false,
                                NULL), sizeof(small) - 1u);
    CHECK_EQ(small[sizeof(small) - 1u], '\0');
}

/*
 * The widest strip fits the row in every language: every pin bound (the
 * target cut to its 26 cells, "+N" at the end), the widest supply reading
 * (99.99V and 99.99A, beyond the 21V and 5A a PPS source gives) and
 * ARMED.  The drawn strip puts 8 + 24 + 24px between its four parts, the
 * text 1 + 3 + 3 spaces: the same width.
 */
TEST_CASE(the_widest_strip_fits_the_row_in_both_languages)
{
    fresh();
    char line[160];
    static const struct { bool online, output; uint8_t ok; } k[] = {
        { false, false, 0u },
        { true, false, 0u },
        { true, true, 0u },
        { true, true, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT },
    };
    static const uint64_t pins[] = { 0u, 1u, 0xFFFFFFFFFFFFull };
    int widest = 0;
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t s = 0; s < sizeof(k) / sizeof(k[0]); ++s) {
            supply(k[s].online, k[s].output, k[s].ok, 99.994f, 99.994f);
            for (int armed = 0; armed < 2; ++armed) {
                for (size_t p = 0; p < 3u; ++p) {
                    for (int known = 0; known < 2; ++known) {
                        (void)motor_output_strip(line, sizeof(line),
                                                 MOTOR_PROTO_MIXED, pins[p],
                                                 known != 0, armed != 0,
                                                 NULL);
                        const int cells = gfx_text_cells(line);
                        if (cells > widest) {
                            widest = cells;
                        }
                        if (cells > PROG_FULL_STRIP_CELLS) {
                            T_FAIL("lang %d: \"%s\" is %d cells", lang, line,
                                   cells);
                        }
                    }
                }
            }
        }
    }
    /* The strip's line in PROG_FULL_STRIP_FONT, 10px cells, ends 12px
     * inside the body's right edge, and above the session's area. */
    CHECK(PROG_FULL_STRIP_X + PROG_FULL_STRIP_CELLS * 10 <= W - 12);
    CHECK_EQ(PROG_FULL_STRIP_FONT->width, 10);
    CHECK_EQ(PROG_FULL_STRIP_FONT->height, PROG_FULL_STRIP_H);
    CHECK(PROG_FULL_STRIP_Y + PROG_FULL_STRIP_H <= PROG_FULL_Y);
    CHECK(widest > 40);
    ui_text_set_language(UI_LANG_EN);
    supply(true, true, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT, 99.994f,
           99.994f);
    (void)motor_output_strip(line, sizeof(line), 4u, 3u, true, false, NULL);
    CHECK(strstr(line, "99.99V  99.99A") != NULL);
}

/* The strip repaints when what it says changes: the supply's reading and
 * the bench arming; an idle frame leaves it as it is. */
TEST_CASE(the_strip_repaints_on_the_supply_and_the_bench)
{
    fresh();
    open_session();
    frames(2);
    const size_t row = (size_t)P(0) * W;
    const size_t cnt = (size_t)PROG_FULL_Y * W;
    const size_t len = cnt * sizeof(gfx_color_t);
    gfx_color_t *was = malloc(len);
    REQUIRE_ALLOC(was);
    memcpy(was, fb + row, len);
    frames(2);
    CHECK(memcmp(was, fb + row, len) == 0);

    supply(true, true, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT, 12.0f, 2.5f);
    frames(2);
    CHECK(memcmp(was, fb + row, len) != 0);
    memcpy(was, fb + row, len);

    motor_screen_set_armed(true);
    frames(2);
    CHECK(memcmp(was, fb + row, len) != 0);
    int danger = 0;
    for (size_t k = 0; k < cnt; ++k) {
        if (fb[row + k] == ui_theme_color(UI_C_DANGER)) {
            ++danger;
        }
    }
    CHECK(danger > 0);
    free(was);
}

/*
 * The home tag climbs one level at a time: a maker's models to the makers,
 * the makers to PROGRAM's list, the list out of the screen, which posts
 * the DISARM.
 */
TEST_CASE(the_home_tag_climbs_the_session_then_leaves)
{
    fresh();
    open_session();
    CHECK_EQ(esc_stick_panel_level(), 0);
    feed_tap(FEED_LONE, 400, SP_ROW_Y(0));
    frames(1);
    CHECK_EQ(esc_stick_panel_level(), 1);

    home_tag();
    frames(1);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    CHECK(motor_screen_session_shown());
    CHECK_EQ(esc_stick_panel_level(), 0);

    home_tag();
    frames(1);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    CHECK(!motor_screen_session_shown());
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);

    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
}

/* Leaving with the session open, armed: the DISARM is posted, the session
 * is closed, and the screen comes back on LIVE with PROGRAM's row a tab
 * away. */
TEST_CASE(leaving_with_a_session_open_disarms_and_closes_it)
{
    fresh();
    open_session();
    motor_screen_set_armed(true);
    drain();
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
    CHECK_EQ(esc_stick_panel_stage(), 0);

    ui_router_goto(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    frames(2);
    CHECK(!motor_screen_session_shown());
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);
    feed_tap(FEED_LONE, TAB_X(1), TAB_Y);
    open_session();
    CHECK(motor_screen_session_shown());
}

/*
 * The signal-wire session holds or would take a pin of its own: the strip names
 * that one, and not the throttle's protocol.
 */
TEST_CASE(the_wire_session_strip_names_its_own_pin)
{
    const esc_wire_target_t pin = { LINK_EW_TARGET_PORT | 1u, 14u };
    ui_bench_status_t st = *ui_router_status();
    char line[96];
    bool warn = true;

    fresh();
    st.capabilities |= LINK_CAP_PROGRAM;
    ui_router_set_status(&st);
    esc_wire_panel_set_targets(&pin, 1u, true);
    feed_tap(FEED_LONE, ROW_X, ROW_Y + 56);
    frames(2);
    CHECK(motor_screen()->covered());
    strip(line, sizeof(line), false, &warn);
    CHECK(!warn);
    CHECK(strstr(line, "PORT 2 GPIO 14") != NULL);
    CHECK(strstr(line, "DSHOT300") == NULL);
    esc_wire_panel_set_targets(NULL, 0u, true);
    strip(line, sizeof(line), false, &warn);
    CHECK(warn);
    CHECK(strstr(line, "DSHOT300") == NULL);
}

/* ARM with a signal-wire session holding a pin: no ask, and the alert names it. */
TEST_CASE(arm_under_a_wire_session_says_the_esc_programmer_holds_it)
{
    esc_prog_feed_t f;

    fresh();
    feed_tap(FEED_LONE, TAB_X(0), TAB_Y);
    frames(2);
    memset(&f, 0, sizeof(f));
    f.rd.known = true;
    f.rd.held = true;
    f.rd.state = LINK_ESC_READY;
    esc_wire_panel_feed(&f);
    ui_router_set_alert(NULL);
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames(3);
    CHECK_STR_EQ(ui_router_alert(), TR(ALERT_ARM_ESC));
    lift(FEED_LONE);
    frames(2);
    CHECK(took().kind != MOTOR_CMD_ARM);
    f.rd.held = false;
    f.rd.state = 0u;
    esc_wire_panel_feed(&f);
    ui_router_set_alert(NULL);
    frames(2);
}

/*
 * PROGRAM's list on an armed bench, the throttle at 40%: a second finger
 * opens ESC STICK while the first holds DISARM.  The press on DISARM has
 * asked for SAFE, and the opening asks again while the bench has not
 * answered; the session takes the first finger's release.  An opening with
 * no finger on DISARM switches the bench to SAFE itself.
 */
TEST_CASE(opening_a_session_on_an_armed_bench_disarms_it)
{
    for (int held = 0; held < 2; ++held) {
        fresh();
        motor_screen_set_armed(true);
        motor_screen_set_throttle(40.0f);
        drain();
        frames(2);
        if (held) {
            finger(1, ARM_X, ARM_Y);        /* DISARM, pressed */
            frames(1);
            CHECK_EQ(took().kind, MOTOR_CMD_DISARM);   /* on the press */
        }
        feed_tap(2, ROW_X, ROW_Y);
        frames(1);
        CHECK(motor_screen_session_shown());
        CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
        if (held) {
            lift(1);                         /* the release the session takes */
            frames(1);
        }
        CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    }
    /* On a SAFE bench the opening asks for nothing. */
    fresh();
    open_session();
    CHECK(motor_screen_session_shown());
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
}

/* A second contact while the row is pressed opens nothing of its own, and
 * the first one's release still opens the row. */
TEST_CASE(a_second_finger_on_the_list_does_not_take_the_row)
{
    fresh();
    finger(1, ROW_X, ROW_Y);
    feed_tap(2, ROW_X, ROW_Y + 200);
    frames(1);
    CHECK(!motor_screen_session_shown());
    lift(1);
    frames(1);
    CHECK(motor_screen_session_shown());
}

int main(void)
{
    RUN(program_lists_the_stick_menu_and_no_modelled_protocol);
    RUN(the_session_answers_for_what_stops_the_bench);
    RUN(a_row_press_that_slides_off_or_is_cancelled_opens_nothing);
    RUN(a_full_width_session_shows_the_strip_not_the_card);
    RUN(the_strip_reads_the_target_the_supply_and_the_bench);
    RUN(the_widest_strip_fits_the_row_in_both_languages);
    RUN(the_strip_repaints_on_the_supply_and_the_bench);
    RUN(the_home_tag_climbs_the_session_then_leaves);
    RUN(leaving_with_a_session_open_disarms_and_closes_it);
    RUN(the_wire_session_strip_names_its_own_pin);
    RUN(arm_under_a_wire_session_says_the_esc_programmer_holds_it);
    RUN(opening_a_session_on_an_armed_bench_disarms_it);
    RUN(a_second_finger_on_the_list_does_not_take_the_row);
    free(fb);
    return test_summary("motor_program");
}
