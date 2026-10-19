/*
 * The outputs screen: a protocol list that opens, and a pin grid that ticks.
 *
 * The screen owns no rules -- out_bind does -- so what is under test here is
 * the touching: that a press and a release on the same thing acts once, that
 * a release somewhere else acts not at all, that an open list can be left
 * without choosing, and that a refused pin does not reach the apply seam.
 * The last one matters most: the application writes the wire from that seam,
 * so a call it should not have received is a page write nobody asked for.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "link_pages.h"
#include "outputs_pages.h"
#include "outputs_screen.h"
#include "servo_screen.h"
#include "settings.h"
#include "settings_screen.h"
#include "sense_link.h"
#include "tone_link.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "touch_feed.h"

/* Geometry the screen draws to; a test that hard-codes it is a test that
 * notices when the layout moves under the hit testing. */
#define COL_X  16
#define DD_X   16
#define DD_Y   44
#define DD_W   266
#define DD_H   54
/* 41 px: the 8 protocols and 1 PROG PORT entry in the screen's height. */
#define POP_ROW 41
#define GRID_X 288
#define GRID_Y 16
#define CELL_GAP 4
#define CELL_W ((496 - 3 * CELL_GAP) / 4)
#define CELL_H 53
#define GRID_ROWS 7

/* The programming port on GP13, for the clash lines. */
static const uint8_t k_gp13[1] = { 13u };

static int s_applied;
static outbind_t s_last;

static void on_apply(const outbind_t *b)
{
    ++s_applied;
    s_last = *b;
}

static const ui_screen_t *scr(void) { return outputs_screen(); }

static void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    outputs_screen_set_apply(on_apply);
    scr()->reset();
    outputs_screen_set_apply(on_apply);   /* reset clears the seam */
    {   /* the screen shows nothing until it is told which board answered */
        outbind_t b;
        outbind_init(&b);
        outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
        outputs_screen_set_binding(&b);
    }
    s_applied = 0;
}

static void tap(int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr()->event(&d);
    scr()->event(&u);
}

static void press_at(int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr()->event(&d);
}

static void release_at(int x, int y)
{
    touch_event_t u = { TOUCH_EVENT_UP, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr()->event(&u);
}

static void cell_centre(uint8_t gpio, int *x, int *y)
{
    const uint8_t i = outbind_index_of(OUTBIND_BOARD_PICO_HEADER, gpio);
    const int col = i / GRID_ROWS, row = i % GRID_ROWS;
    *x = GRID_X + col * (CELL_W + CELL_GAP) + CELL_W / 2;
    *y = GRID_Y + row * (CELL_H + CELL_GAP) + CELL_H / 2;
}

static void choose_proto(int index)
{
    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);              /* open  */
    tap(DD_X + 40, DD_Y + 4 + index * POP_ROW + POP_ROW / 2);
}

/* By name, because the row a protocol sits on is the catalogue's business:
 * an entry added between two others must not silently retarget a test at a
 * protocol it was not written for. */
static int proto_row(const char *name)
{
    const outbind_proto_t *k = outbind_protos();
    for (int i = 0; i < (int)OUTBIND_PROTOS; ++i) {
        if (strcmp(k[i].name, name) == 0) {
            return i;
        }
    }
    T_FAIL("the catalogue offers no %s", name);
    return 0;
}

static void choose_named(const char *name) { choose_proto(proto_row(name)); }

static void tap_pin(uint8_t gpio)
{
    int x, y;
    cell_centre(gpio, &x, &y);
    tap(x, y);
}

static uint8_t idx(uint8_t gpio)
{
    return outbind_index_of(OUTBIND_BOARD_PICO_HEADER, gpio);
}

/* The bench of the report that OFF is for: GP0 DSHOT600 BIDIR, GP1 MOTOR
 * PWM, GP2 and GP13 SERVO PWM. */
static void three_protocols(outbind_t *b)
{
    outbind_init(b);
    outbind_set_board(b, OUTBIND_BOARD_PICO_HEADER);
    outbind_set_proto(b, (uint8_t)proto_row("DSHOT600 BIDIR"));
    (void)outbind_toggle(b, idx(0));
    outbind_set_proto(b, (uint8_t)proto_row("MOTOR PWM"));
    (void)outbind_toggle(b, idx(1));
    outbind_set_proto(b, (uint8_t)proto_row("SERVO PWM"));
    (void)outbind_toggle(b, idx(2));
    (void)outbind_toggle(b, idx(13));
}

/* The same binding as the panel reads it: off the two pages it renders to,
 * so the protocol it names is the one a page names. */
static void read_back(const outbind_t *b, bind_reading_t *r)
{
    uint16_t slots[LINK_OS_COUNT], cc[LINK_CC_COUNT];
    (void)outbind_to_slots(b, slots);
    outbind_to_chan_cfg(b, cc, 1000u, 2000u);
    (void)bind_link_classify(r, b->board, slots, cc);
}

/* What the screen prints for every pin of the board, one line a pin: the
 * state of its cell and the line under its name. */
static const char *trace(void)
{
    static char out[2048];
    static const char *const k_state[] = {
        "none", "free", "ticked", "held", "reserved",
    };
    size_t at = 0;
    out[0] = '\0';
    const uint8_t n = outbind_pin_count(OUTBIND_BOARD_PICO_HEADER);
    for (uint8_t i = 0; i < n; ++i) {
        char label[32];
        const outputs_cell_t st = outputs_screen_cell(i, label,
                                                      sizeof(label));
        const int w = snprintf(out + at, sizeof(out) - at, "GP%u %s '%s'\n",
            (unsigned)outbind_pins(OUTBIND_BOARD_PICO_HEADER)[i].gpio,
            k_state[st], label);
        at += (size_t)w;
    }
    return out;
}

static const char *reason(void)
{
    static char buf[64];
    (void)outputs_screen_reason(buf, sizeof(buf));
    return buf;
}

/* Whether @p s ends in @p tail: "GP1" at the end, not inside "GP17". */
static bool ends_with(const char *s, const char *tail)
{
    const size_t n = strlen(s), t = strlen(tail);
    return n >= t && strcmp(s + n - t, tail) == 0;
}

static void pose_three_protocols(void)
{
    fresh();
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    /* The board answered and the selection went to OFF; SERVO PWM is
     * picked, as an operator does to edit it. */
    outputs_screen_select((uint8_t)proto_row("SERVO PWM"));
}

/* --------------------------------------------------------------- the list */

TEST_CASE(the_protocol_list_opens_and_a_choice_closes_it)
{
    fresh();
    choose_named("SERVO PWM");
    CHECK_EQ(outputs_screen_binding()->proto, 1);
    /* A pick is not a change of the binding: the seam is not called. */
    CHECK_EQ(s_applied, 0);

    /* And it is shut: a tap where a pin cell is now works as a pin again. */
    tap_pin(0);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    CHECK_EQ(s_applied, 1);
}

/*
 * A pick in the list writes nothing, whichever entry it is.  It changes
 * what the screen shows and which set the next tick joins; the pages a
 * binding renders to are the same before and after, and a write of them
 * would be 18 exchanges that can fail and change nothing.
 */
TEST_CASE(a_pick_calls_the_apply_seam_for_no_entry)
{
    fresh();
    uint16_t before[LINK_OS_COUNT], after[LINK_OS_COUNT];
    for (int round = 0; round < 2; ++round) {
        for (int p = 0; p < (int)OUTBIND_PROTOS; ++p) {
            (void)outbind_to_slots(outputs_screen_binding(), before);
            choose_proto(p);
            CHECK_EQ((int)outputs_screen_binding()->proto, p);
            CHECK_EQ(s_applied, 0);
            (void)outbind_to_slots(outputs_screen_binding(), after);
            CHECK(memcmp(before, after, sizeof(before)) == 0);
        }
        if (round == 0) {
            /* With pins bound under three protocols as well. */
            outbind_t b;
            three_protocols(&b);
            outputs_screen_set_binding(&b);
        }
    }
    /* The same entry picked again, and a tick: one call. */
    choose_named("SERVO PWM");
    choose_named("SERVO PWM");
    CHECK_EQ(s_applied, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    tap_pin(4);
    CHECK_EQ(s_applied, 2);
}

TEST_CASE(an_open_list_can_be_left_without_choosing)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);              /* open */
    tap(700, 400);                                      /* somewhere else */
    CHECK_EQ(outputs_screen_binding()->proto, 1);
    CHECK_EQ(s_applied, was);

    /* The list is closed, so the same tap now reaches the grid under it. */
    tap_pin(0);
    CHECK_EQ(s_applied, was + 1);
}

/*
 * The home tag over the open list (ui_screen_t.back) closes it choosing
 * nothing and keeps the screen; with no list open the tag leaves.
 */
TEST_CASE(the_home_tag_closes_the_open_list_first)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;
    CHECK(!scr()->back());                              /* nothing open */
    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);              /* open */
    CHECK(scr()->back());
    CHECK_EQ(outputs_screen_binding()->proto, 1);
    CHECK_EQ(s_applied, was);
    tap_pin(0);                                         /* the grid again */
    CHECK_EQ(s_applied, was + 1);
    CHECK(!scr()->back());
}

/*
 * A cell press applies its binding change on the release, so a press left
 * latched after a lost event is a change waiting for any release that lands
 * on the cell.  The GT911 reuses track ids, so that release need not belong
 * to the same contact.
 */
TEST_CASE(a_cancelled_press_applies_nothing)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    int x, y;
    cell_centre(0, &x, &y);
    press_at(x, y);
    scr()->cancel();

    release_at(x, y);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was);

    /* And nothing is stuck: a fresh press and release still applies. */
    press_at(x, y);
    release_at(x, y);
    CHECK_EQ(s_applied, was + 1);
}

TEST_CASE(a_release_away_from_the_press_does_nothing)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    int x, y;
    cell_centre(0, &x, &y);
    press_at(x, y);
    release_at(x + 400, y);          /* finger slid off the cell */
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was);
}

/* A contact the tracker ends for a jump: its release is on the control it
 * pressed and the finger did not make it.  No pin is ticked, no protocol
 * picked and the list is not opened. */
TEST_CASE(a_release_the_tracker_makes_for_a_jump_picks_nothing)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;
    int x, y;
    cell_centre(0, &x, &y);
    feed_reset();
    feed_to_screen(scr());
    /* Off every control: the left column's foot. */
    const int off_x = COL_X + 100, off_y = 400 + UI_BAND_H;

    finger(FEED_LONE, x, y + UI_BAND_H);
    finger(FEED_LONE, off_x, off_y);
    CHECK_EQ(feed_ups, 1);                    /* the tracker's release */
    lift(FEED_LONE);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was);

    /* The protocol field: not opened, so a pin cell is still a pin cell. */
    finger(FEED_LONE, DD_X + DD_W / 2, DD_Y + DD_H / 2 + UI_BAND_H);
    finger(FEED_LONE, off_x, off_y);
    lift(FEED_LONE);
    feed_tap(FEED_LONE, x, y + UI_BAND_H);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    CHECK_EQ(s_applied, was + 1);

    /* A row of the open list: not picked. */
    feed_tap(FEED_LONE, DD_X + DD_W / 2, DD_Y + DD_H / 2 + UI_BAND_H);
    finger(FEED_LONE, DD_X + 40,
           DD_Y + 4 + POP_ROW / 2 + UI_BAND_H);         /* row 0: off */
    finger(FEED_LONE, DD_X + 40 + 300, DD_Y + 4 + POP_ROW / 2 + UI_BAND_H);
    lift(FEED_LONE);
    CHECK_EQ(outputs_screen_binding()->proto, proto_row("SERVO PWM"));
}

/* ---------------------------------------------------------------- the pins */

TEST_CASE(ticking_a_pin_applies_once_and_unticking_applies_again)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    tap_pin(4);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    CHECK_EQ(s_applied, was + 1);
    CHECK_EQ(outbind_chosen(&s_last), 1);

    tap_pin(4);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was + 2);
}

TEST_CASE(a_reserved_pin_never_reaches_the_apply_seam)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    /* The application writes the wire from that seam, so a call here would
     * be a page write for a pin the far end is going to refuse. */
    tap_pin(3);                      /* heartbeat */
    tap_pin(10);                     /* CAN SCK */
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was);
}

TEST_CASE(the_programming_ports_pin_is_taken_and_takes_no_tick)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
    guard_canvas_t g;
    gfx_canvas_t c;
    char l[32];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        choose_named("SERVO PWM");
        const int was = s_applied;

        /* No port read: GP13 is a pin like the others. */
        CHECK_EQ(outputs_screen_cell(idx(13), l, sizeof(l)),
                 OUTPUTS_CELL_FREE);
        outputs_screen_set_prog_pin(13u);
        CHECK_EQ(outputs_screen_cell(idx(13), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_PROG_PORT));
        tap_pin(13);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        CHECK_EQ(s_applied, was);
        /* The pin beside it is not the port's. */
        CHECK_EQ(outputs_screen_cell(idx(14), NULL, 0), OUTPUTS_CELL_FREE);
        guard_canvas_init(&g, &c, px, 800, 432);
        scr()->render(&c, 0);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        /* The screen left and entered again: the pin is still the port's. */
        scr()->reset();
        outputs_screen_set_apply(on_apply);
        {
            outbind_t b;
            outbind_init(&b);
            outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
            outputs_screen_set_binding(&b);
        }
        CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0),
                 OUTPUTS_CELL_RESERVED);

        /* A port without a pin, or on a pin this board does not list. */
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
        CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0), OUTPUTS_CELL_FREE);
        outputs_screen_set_prog_pin(200u);
        CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0), OUTPUTS_CELL_FREE);
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
        choose_named("SERVO PWM");
        tap_pin(13);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);

        /* A slot that holds the pin keeps it -- the IOmcu gives the
         * port no pin then -- and it can be unticked. */
        outputs_screen_set_prog_pin(13u);
        CHECK_EQ(outputs_screen_cell(idx(13), l, sizeof(l)),
                 OUTPUTS_CELL_TICKED);
        tap_pin(13);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        tap_pin(13);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * What SETUP > INTERFACES holds is taken: the sensor bus on Sensor SDA and
 * the SCL after it while the INA228, the INA3221 or the AS5600 is on, the
 * PD mini's TX and RX while it is on, the tap pin while the phase tap is
 * on.  The cell names the function and takes no tick; the pins follow the
 * settings, and a pin set back to -1, a function switched off or a bus
 * moved frees the pin it held.  A protocol that holds the pin already
 * keeps it.
 */
TEST_CASE(an_interface_pin_is_taken_named_and_takes_no_tick)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
    guard_canvas_t g;
    gfx_canvas_t c;
    char l[32];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        settings_set_store(NULL);
        settings_init();
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        choose_named("SERVO PWM");
        const int was = s_applied;

        /* Nothing on: the default pins hold nothing. */
        for (uint8_t gp = 16u; gp <= 22u; ++gp) {
            CHECK_EQ(outputs_screen_cell(idx(gp), NULL, 0), OUTPUTS_CELL_FREE);
        }
        CHECK_EQ(outputs_iface_at(16u), OUTPUTS_IFACE_NONE);

        /* The INA228 on: GP16 and GP17, the default Sensor SDA and SCL. */
        settings_set(SET_INA228_EN, 1.0f);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE),
                 (1ull << 16) | (1ull << 17));
        for (uint8_t gp = 16u; gp <= 17u; ++gp) {
            CHECK_EQ(outputs_screen_cell(idx(gp), l, sizeof(l)),
                     OUTPUTS_CELL_RESERVED);
            CHECK_STR_EQ(l, TR(OUT_IFACE_SENSE));
            tap_pin(gp);
        }
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        CHECK_EQ(s_applied, was);
        CHECK_EQ(outputs_screen_cell(idx(18u), NULL, 0), OUTPUTS_CELL_FREE);
        /* The INA3221 or the AS5600 alone holds the bus as well. */
        settings_set(SET_INA228_EN, 0.0f);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_set(SET_ENC_EN, 1.0f);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        settings_set(SET_ENC_EN, 0.0f);
        settings_set(SET_INA3221_EN, 1.0f);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);

        /* The PD mini on with RX GP18 and TX GP19. */
        settings_set(SET_PDMINI_TX, 19.0f);
        settings_set(SET_PDMINI_RX, 18.0f);
        CHECK_EQ(outputs_screen_cell(idx(18u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_set(SET_PDMINI_EN, 1.0f);
        for (uint8_t gp = 18u; gp <= 19u; ++gp) {
            CHECK_EQ(outputs_screen_cell(idx(gp), l, sizeof(l)),
                     OUTPUTS_CELL_RESERVED);
            CHECK_STR_EQ(l, TR(OUT_IFACE_PDMINI));
            tap_pin(gp);
        }
        CHECK_EQ(s_applied, was);
        /* A pin at -1, or both on one pin: the panel writes the page OFF,
         * and neither pin is held. */
        settings_set(SET_PDMINI_TX, -1.0f);
        CHECK_EQ(outputs_screen_cell(idx(19u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(18u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_set(SET_PDMINI_TX, 18.0f);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_PDMINI), 0ull);
        settings_set(SET_PDMINI_TX, 19.0f);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_PDMINI),
                 (1ull << 18) | (1ull << 19));

        /* The phase tap on its default GP22. */
        settings_set(SET_TONE_EN, 1.0f);
        CHECK_EQ(outputs_screen_cell(idx(22u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_IFACE_TAP));

        /* Drawn, inside the canvas, every name in its cell. */
        guard_canvas_init(&g, &c, px, 800, 432);
        scr()->render(&c, 0);
        CHECK_EQ(guard_canvas_touched(&g), 0u);

        /* The bus moved to GP14: GP16 and GP17 free again, a tick on GP16
         * binds it, and GP14 and GP15 are the bus's.  The frame drawn
         * before is repainted without a touch on this screen. */
        settings_set(SET_SENSE_SDA, 14.0f);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL), 15);
        for (int y = 0; y < 432; y += 7) {
            for (int x = 0; x < 800; x += 13) {
                c.pixels[(size_t)y * c.stride + (size_t)x] =
                    GFX_RGB(255, 0, 255);
            }
        }
        scr()->render(&c, 0);
        bool repainted = true;
        for (int y = 0; y < 432; y += 7) {
            for (int x = 0; x < 800; x += 13) {
                repainted = repainted
                            && c.pixels[(size_t)y * c.stride + (size_t)x]
                                   != GFX_RGB(255, 0, 255);
            }
        }
        CHECK(repainted);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(14u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_IFACE_SENSE));
        CHECK_EQ(outputs_screen_cell(idx(15u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        tap_pin(16u);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
        CHECK_EQ(s_applied, was + 1);

        /* A pin a protocol holds stays the protocol's when the bus is set
         * onto it -- the IOmcu refuses the bus then -- and is taken once
         * it is unticked. */
        settings_set(SET_SENSE_SDA, 16.0f);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0), OUTPUTS_CELL_TICKED);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        tap_pin(16u);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        tap_pin(16u);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);

        /* No sensor bus: -1 frees both pins, whatever is on. */
        settings_set(SET_SENSE_SDA, -1.0f);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE), 0ull);

        /* A programming port's pin stays the port's. */
        settings_set(SET_TONE_PIN, 13.0f);
        outputs_screen_set_prog_pin(13u);
        CHECK_EQ(outputs_screen_cell(idx(13u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_PROG_PORT));
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
        CHECK_EQ(outputs_screen_cell(idx(13u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_IFACE_TAP));
        CHECK_EQ(outputs_screen_cell(idx(22u), NULL, 0), OUTPUTS_CELL_FREE);

        /* A tap pin the TONE page refuses, an ADC pin, holds nothing;
         * nor does a bus or a tap the IOmcu refused, until it holds it
         * again. */
        settings_set(SET_TONE_PIN, 26.0f);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_TAP), 0ull);
        CHECK_EQ(outputs_screen_cell(idx(26u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_set(SET_TONE_PIN, 21.0f);
        CHECK_EQ(outputs_screen_cell(idx(21u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        settings_screen_set_unheld(0u, TONE_LINK_ROWS_TAP);
        CHECK_EQ(outputs_screen_cell(idx(21u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_screen_set_unheld(0u, 0u);
        CHECK_EQ(outputs_screen_cell(idx(21u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        settings_set(SET_SENSE_SDA, 16.0f);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_screen_set_unheld(0u, 0u);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        settings_set(SET_SENSE_SDA, -1.0f);

        /* Out of range and none have no pins and no name. */
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_NONE), 0ull);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_COUNT), 0ull);
        CHECK_STR_EQ(outputs_iface_name(OUTPUTS_IFACE_NONE), "");
        CHECK_EQ(outputs_iface_at(200u), OUTPUTS_IFACE_NONE);
    }
    settings_init();
    ui_text_set_language(UI_LANG_EN);
}

/*
 * A PWM pin on the programming port's slice is not offered.  The
 * IOmcu's prog_port_slots_check() refuses a PWM slot that shares a
 * slice with a port's pin, and GP28 is slice 6 with GP13.  The cell names
 * the port's pin, a tap writes nothing, and the line under the protocol
 * names both pins.  DShot runs on PIO, not on a slice, and takes the pin.
 */
TEST_CASE(a_pwm_pin_on_the_programming_ports_slice_is_not_offered)
{
    static const char *const pwm[2] = { "SERVO PWM", "MOTOR PWM" };
    char l[32];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (unsigned k = 0; k < 2u; ++k) {
            fresh();
            ui_text_set_language((ui_lang_t)lang);
            outputs_screen_set_prog_pin(13u);
            choose_named(pwm[k]);
            const int was = s_applied;

            CHECK_EQ(outputs_screen_cell(idx(28), l, sizeof(l)),
                     OUTPUTS_CELL_HELD);
            CHECK(ends_with(l, "GP13"));
            /* Slice 7 is not the port's. */
            CHECK_EQ(outputs_screen_cell(idx(14), NULL, 0),
                     OUTPUTS_CELL_FREE);
            /* The cell says it before a tap; the line answers the tap. */
            CHECK_STR_EQ(reason(), "");
            tap_pin(28);
            CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
            CHECK_EQ(s_applied, was);
            CHECK(strncmp(reason(), "GP28", 4) == 0);
            CHECK(ends_with(reason(), "GP13"));
            /* Another pick ends the answer. */
            choose_named(pwm[k]);
            CHECK_STR_EQ(reason(), "");

            /* So does a change of board: the line keeps a catalogue index,
             * which names another pin, or none, on another board. */
            tap_pin(28);
            CHECK(strncmp(reason(), "GP28", 4) == 0);
            outbind_t b = *outputs_screen_binding();
            outbind_t other = b;
            other.board = (uint16_t)(b.board + 1u);
            outputs_screen_set_binding(&other);
            outputs_screen_set_binding(&b);
            CHECK_STR_EQ(reason(), "");
        }

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        outputs_screen_set_prog_pin(13u);
        choose_named("DSHOT600");
        CHECK_EQ(outputs_screen_cell(idx(28), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_STR_EQ(reason(), "");
        tap_pin(28);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
        CHECK_EQ(s_applied, 1);

        /* No port pin: GP28 is a PWM pin like the others. */
        fresh();
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
        choose_named("SERVO PWM");
        CHECK_EQ(outputs_screen_cell(idx(28), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_STR_EQ(reason(), "");
    }
    outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
    ui_text_set_language(UI_LANG_EN);
}

/*
 * GP0 and GP16 are one compare register on two pads, and the IOmcu's
 * out_pwm_bind() refuses the second pin of the pair.  Beside a pin bound as
 * PWM in either PWM protocol, its partner is not offered to a PWM protocol;
 * the cell names the bound pin, and a refused tap names both under the
 * protocol.  Giving the bound pin back offers the partner again.
 */
TEST_CASE(a_pwm_pin_on_a_bound_pins_compare_register_is_not_offered)
{
    char l[32];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        choose_named("SERVO PWM");
        tap_pin(0);
        tap_pin(1);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 2);
        const int was = s_applied;

        CHECK_EQ(outputs_screen_cell(idx(16), l, sizeof(l)),
                 OUTPUTS_CELL_HELD);
        CHECK(ends_with(l, "GP0"));
        CHECK_EQ(outputs_screen_cell(idx(17), l, sizeof(l)),
                 OUTPUTS_CELL_HELD);
        CHECK(ends_with(l, "GP1"));
        /* The other channel of the slice is a register of its own. */
        CHECK_EQ(outputs_screen_cell(idx(18), NULL, 0), OUTPUTS_CELL_FREE);

        /* The line names the pin tapped, not the lowest one refused. */
        CHECK_STR_EQ(reason(), "");
        tap_pin(17);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 2);
        CHECK_EQ(s_applied, was);
        CHECK(strncmp(reason(), "GP17", 4) == 0);
        CHECK(ends_with(reason(), "GP1"));
        tap_pin(16);
        CHECK_EQ(s_applied, was);
        CHECK(strncmp(reason(), "GP16", 4) == 0);
        CHECK(ends_with(reason(), "GP0"));
        /* A tick that lands ends it. */
        tap_pin(4);
        CHECK_EQ(s_applied, was + 1);
        CHECK_STR_EQ(reason(), "");

        /* The other PWM protocol meets the same register. */
        choose_named("MOTOR PWM");
        CHECK_EQ(outputs_screen_cell(idx(16), NULL, 0), OUTPUTS_CELL_HELD);
        tap_pin(16);
        CHECK_EQ(s_applied, was + 1);

        /* DShot does not drive a compare register. */
        choose_named("DSHOT300");
        CHECK_EQ(outputs_screen_cell(idx(16), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_STR_EQ(reason(), "");

        choose_named("SERVO PWM");
        tap_pin(0);
        CHECK_EQ(outputs_screen_cell(idx(16), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(17), NULL, 0), OUTPUTS_CELL_HELD);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_pin_too_many_does_not_apply)
{
    fresh();
    choose_named("PPM");             /* one pin */
    const int was = s_applied;

    tap_pin(0);
    CHECK_EQ(s_applied, was + 1);
    tap_pin(1);                      /* refused, and silently doing it would
                                      * write a page that drops the second */
    CHECK_EQ(s_applied, was + 1);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
}

TEST_CASE(nothing_can_be_ticked_while_the_protocol_is_off)
{
    fresh();
    tap_pin(0);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, 0);
}

/* --------------------------------------------------------------- rendering */

/* Every protocol with every pin ticked, and its list open over that:
 * each frame paints the whole canvas and no pixel beside it, and each
 * protocol's grid is a picture of its own. */
TEST_CASE(every_state_renders_without_reading_off_the_canvas)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
    guard_canvas_t g;
    gfx_canvas_t c;
    uint32_t sum[OUTBIND_PROTOS];

    fresh();
    for (int p = 0; p < (int)OUTBIND_PROTOS; ++p) {
        guard_canvas_init(&g, &c, px, 800, 432);
        choose_proto(p);
        for (uint8_t pin = 0; pin < 29u; ++pin) {
            if (outbind_index_of(OUTBIND_BOARD_PICO_HEADER, pin) < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)) {
                tap_pin(pin);
            }
        }
        scr()->render(&c, 0);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        CHECK_EQ(guard_canvas_unpainted(&g), 0u);
        sum[p] = guard_canvas_sum(&g);
        for (int q = 0; q < p; ++q) {
            CHECK(sum[p] != sum[q]);
        }
        /* And with the list open over the top of whatever was chosen. */
        tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);
        scr()->render(&c, 1);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        CHECK_EQ(guard_canvas_unpainted(&g), 0u);
        CHECK(guard_canvas_sum(&g) != sum[p]);
        tap(700, 400);
    }
    for (int r = 0; r <= (int)OUTPUTS_REFUSED; ++r) {
        outputs_screen_set_result((outputs_result_t)r);
        scr()->render(&c, 2);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        CHECK_EQ(guard_canvas_unpainted(&g), 0u);
    }
}

/* The strip under the protocol where the reason is drawn, as a sum: a line
 * there is ink, no line is the background. */
static unsigned reason_ink(gfx_canvas_t *c)
{
    unsigned long sum = 0;
    for (int y = DD_Y + DD_H + 56; y < DD_Y + DD_H + 76; ++y) {
        for (int x = COL_X; x < COL_X + 210; ++x) {
            sum += (unsigned long)c->pixels[y * c->stride + x];
        }
    }
    return (unsigned)(sum & 0xffffffffUL);
}

TEST_CASE(a_board_that_can_take_nothing_says_why)
{
    /*
     * The rules refusing every pin are out_bind's and they are right; what
     * is under test is that the screen states them. A board drawn entirely
     * in grey with nothing beside it reads as a fault, and an operator
     * reported it as one.
     */
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

    fresh();
    choose_named("SERVO PWM");
    scr()->render(&c, 0);
    const unsigned quiet = reason_ink(&c);

    /*
     * Four servo pins, then PPM: eight channels needed against four free,
     * so nothing can be ticked at all. This is the state that was asked
     * about.
     */
    static const uint8_t gp[4] = { 0, 1, 2, 4 };
    for (unsigned i = 0; i < 4u; ++i) {
        tap_pin(gp[i]);
    }
    choose_named("PPM");
    scr()->render(&c, 0);
    CHECK(reason_ink(&c) != quiet);

    /* And it goes away again when the protocol can take a pin. */
    choose_named("SERVO PWM");
    scr()->render(&c, 0);
    CHECK_EQ(reason_ink(&c), quiet);
}

TEST_CASE(a_protocol_that_has_all_its_pins_says_so)
{
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

    fresh();
    choose_named("SERVO PWM");                    /* eight pins */
    scr()->render(&c, 0);
    const unsigned quiet = reason_ink(&c);

    uint8_t taken = 0;
    for (uint8_t g = 0; g < 29u && taken < 8u; ++g) {
        const uint8_t i = outbind_index_of(OUTBIND_BOARD_PICO_HEADER, g);
        if (i < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)
            && outbind_can_add(outputs_screen_binding(), i)) {
            tap_pin(g);
            ++taken;
        }
    }
    CHECK_EQ(taken, 8u);
    scr()->render(&c, 0);
    CHECK(reason_ink(&c) != quiet);
}

TEST_CASE(the_binding_survives_being_set_from_outside)
{
    fresh();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
    const int dshot600 = proto_row("DSHOT600");
    outbind_set_proto(&b, (uint8_t)dshot600);
    (void)outbind_toggle(&b, outbind_index_of(OUTBIND_BOARD_PICO_HEADER, 7));
    outputs_screen_set_binding(&b);

    /* What was loaded from storage is what the screen now shows, on the
     * overview the board's first answer opened, and edits once its
     * protocol is picked. */
    CHECK_EQ(outputs_screen_binding()->proto, 0);
    CHECK_EQ(outputs_screen_cell(idx(7), NULL, 0), OUTPUTS_CELL_HELD);
    choose_named("DSHOT600");
    CHECK_EQ(outputs_screen_binding()->proto, dshot600);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    tap_pin(13);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 2);
}

TEST_CASE(a_protocol_index_from_outside_cannot_run_off_the_table)
{
    /* The binding arrives from the wire and from flash, so the index is not
     * this screen's to trust.  Reading past the protocol table would render
     * a name from whatever followed it. */
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

    fresh();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
    b.proto = (uint8_t)(OUTBIND_PROTOS + 40u);
    for (uint8_t g = 0; g < OUTBIND_PROTOS; ++g) {
        b.pins[g] = 0xFFFFFFFFu;
    }
    outputs_screen_set_binding(&b);
    scr()->render(&c, 0);

    /* And it is still usable: the list picks up from OFF rather than wedging. */
    choose_named("SERVO PWM");
    CHECK_EQ(outputs_screen_binding()->proto, 1);
}

TEST_CASE(null_events_are_refused_rather_than_dereferenced)
{
    fresh();
    scr()->event(NULL);
    scr()->enter();
    scr()->leave();
    CHECK(scr()->title != NULL);
}

/*
 * A protocol the operator has just picked survives an empty binding arriving
 * from the far end.
 *
 * A pick writes nothing, but a binding is read at every link-up and after
 * every write.  With nothing bound yet -- every first boot on an unwritten
 * store -- what is read is empty, and an empty binding says nothing about
 * which protocol is being worked in: OFF there is indistinguishable from
 * "nothing is configured".  Letting it land wholesale puts the list back to
 * OFF, and no pin can be ticked, because OFF takes none.
 */
TEST_CASE(an_empty_read_back_does_not_clear_the_chosen_protocol)
{
    fresh();
    const int bidir = proto_row("DSHOT600 BIDIR");

    /* The operator picks a protocol on the screen.  Nothing is ticked yet,
     * which is the only way to reach a first pin: OFF takes none. */
    choose_named("DSHOT600 BIDIR");
    CHECK_EQ((int)outputs_screen_binding()->proto, bidir);

    /* A read arrives: the link came up.  With an empty store -- every first
     * boot on this build -- what is read is empty, and an empty binding says
     * nothing about which protocol is being worked in. */
    outbind_t empty;
    outbind_init(&empty);
    outbind_set_board(&empty, OUTBIND_BOARD_PICO_HEADER);
    CHECK_EQ((int)outbind_chosen_total(&empty), 0);
    outputs_screen_set_binding(&empty);

    /* Still the operator's protocol, so a pin can be ticked at all. */
    CHECK_EQ((int)outputs_screen_binding()->proto, bidir);
    tap_pin(7);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 1);

    /* And nothing at all leaves the screen as it was.  The caller passes what
     * the far end gave it, and a link that answered nothing gives NULL. */
    outputs_screen_set_binding(NULL);
    CHECK_EQ((int)outputs_screen_binding()->proto, bidir);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 1);
}

/*
 * A protocol being started while another is already bound survives the
 * read-back as well.
 *
 * A page carries pins, and the protocol read back out of one is the lowest
 * that holds a pin -- there is nowhere on the page to say which one is being
 * edited.  A protocol just chosen holds no pin yet, so the read-back names
 * the one already bound and, landing whole, moves the screen off the choice
 * within one poll.  A bench wired for an ESC and then for servos meets this
 * on its second protocol.
 */
TEST_CASE(a_second_protocol_survives_the_read_back)
{
    fresh();
    const int dshot = proto_row("DSHOT600");
    const int servo = proto_row("SERVO PWM");

    choose_named("DSHOT600");
    tap_pin(7);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 1);

    /* The second protocol, with no pin of its own yet. */
    choose_named("SERVO PWM");
    CHECK_EQ((int)outputs_screen_binding()->proto, servo);

    /* What the far end answers: the page this binding renders to, read back
     * through the code the panel reads it with, rather than a page written
     * here to say what this test wants said. */
    uint16_t slots[LINK_OS_COUNT], cc[LINK_CC_COUNT];
    (void)outbind_to_slots(outputs_screen_binding(), slots);
    outbind_to_chan_cfg(outputs_screen_binding(), cc, 1000u, 2000u);
    outbind_t back;
    CHECK(outbind_from_slots(&back, OUTBIND_BOARD_PICO_HEADER, slots, cc));
    CHECK_EQ((int)back.proto, dshot);      /* the page can say nothing else */
    outputs_screen_set_binding(&back);

    /* Still the protocol being worked in, so its first pin can be ticked. */
    CHECK_EQ((int)outputs_screen_binding()->proto, servo);
    tap_pin(13);
    CHECK_EQ((int)outbind_chosen(outputs_screen_binding()), 1);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 2);
}

/*
 * A change of board takes the protocol with it.
 *
 * outbind_set_board() clears the selection because a pin index means a
 * different pin -- or no pin -- in another catalogue.  A protocol chosen for
 * the hardware that was there is worth no more than the pins were, and
 * keeping it would let the first tap on the board now in front of the
 * operator bind under it.
 */
TEST_CASE(a_change_of_board_takes_the_protocol_with_it)
{
    fresh();
    choose_named("DSHOT600");
    CHECK_EQ(outputs_screen_binding()->proto, proto_row("DSHOT600"));

    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
    CHECK_EQ((int)other.proto, 0);
    outputs_screen_set_binding(&other);

    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 0);

    /* Back to the Pico header with pins bound: OFF, not the protocol the
     * pins name and not the one picked before, and a tap writes nothing. */
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 0);
}

/*
 * A protocol picked while the IOmcu is not there -- the list works
 * with the link down -- is not carried to the board that then answers: the
 * selection goes to OFF, and the next tap writes nothing.  The same for an
 * IOmcu swapped for another board after a pick.
 */
TEST_CASE(a_pick_made_for_one_board_is_not_carried_to_another)
{
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);

    scr()->reset();
    outputs_screen_set_apply(on_apply);
    s_applied = 0;
    bind_reading_t none;
    (void)bind_link_classify(&none, 0u, NULL, NULL);
    outputs_screen_set_reading(&none);
    scr()->enter();
    choose_named("DSHOT600");
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("DSHOT600"));

    outputs_screen_set_reading(&r);
    CHECK(outputs_screen_editable());
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    tap_pin(4);
    CHECK_EQ(s_applied, 0);

    /* Swapped after a pick: by binding, and by a reading with no pin map. */
    choose_named("DSHOT600");
    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
    outputs_screen_set_binding(&other);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 0);

    choose_named("MOTOR PWM");
    bind_reading_t unmapped = r;
    unmapped.state = BIND_READ_NO_BOARD;
    outbind_init(&unmapped.bind);
    outbind_set_board(&unmapped.bind, (uint16_t)0xBEEFu);
    outputs_screen_set_reading(&unmapped);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 0);

    /* The link comes up on another board and its first read fails: the
     * failed read names the board, and the read that follows names the
     * same one.  The pick made before is still not carried. */
    scr()->reset();
    outputs_screen_set_apply(on_apply);
    outputs_screen_set_reading(&none);
    scr()->enter();
    choose_named("DSHOT600");
    bind_reading_t failed;
    (void)bind_link_classify(&failed, OUTBIND_BOARD_PICO_HEADER, NULL, NULL);
    outputs_screen_set_reading(&failed);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 0);

    /* The same board read again keeps a pick made on it. */
    choose_named("DSHOT600");
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("DSHOT600"));
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(4)), proto_row("DSHOT600"));
}

/*
 * A protocol somebody names still lands, over the one on screen.
 *
 * The rule turns on what a page could have said.  A caller naming a protocol
 * the pins do not name is not a page -- it is the screen being posed, or told
 * what to show at start-up -- and what it names is the choice.
 */
TEST_CASE(a_protocol_named_from_outside_lands_over_the_one_on_screen)
{
    fresh();
    const int ppm = proto_row("PPM");
    choose_named("DSHOT600");
    tap_pin(7);

    /* PPM with the DShot pin still bound: no page renders to this, because
     * the protocol a page names is the lowest one holding a pin. */
    outbind_t b = *outputs_screen_binding();
    outbind_set_proto(&b, (uint8_t)ppm);
    CHECK(b.proto != outbind_wire_proto(&b));
    outputs_screen_set_binding(&b);

    CHECK_EQ(outputs_screen_binding()->proto, ppm);
}

/* ---------------------------------------------------------------------- OFF */

/* The pins past GP2 of the bench of three_protocols(): free pins print the
 * pad number on the board, reserved ones what holds them. */
#define TRACE_GP3_TO_GP12                                                     \
    "GP3 reserved 'heartbeat'\n"                                              \
    "GP4 free 'PAD 6'\n"                                                      \
    "GP5 free 'PAD 7'\n"                                                      \
    "GP6 free 'PAD 9'\n"                                                      \
    "GP7 free 'PAD 10'\n"                                                     \
    "GP8 reserved 'CAN INT'\n"                                                \
    "GP9 reserved 'CAN CS'\n"                                                 \
    "GP10 reserved 'CAN SCK'\n"                                               \
    "GP11 reserved 'CAN MOSI'\n"                                              \
    "GP12 reserved 'CAN MISO'\n"
#define TRACE_GP14_TO_GP28                                                    \
    "GP14 free 'PAD 19'\n"                                                    \
    "GP15 free 'PAD 20'\n"                                                    \
    "GP16 free 'PAD 21'\n"                                                    \
    "GP17 free 'PAD 22'\n"                                                    \
    "GP18 free 'PAD 24'\n"                                                    \
    "GP19 free 'PAD 25'\n"                                                    \
    "GP20 free 'PAD 26'\n"                                                    \
    "GP21 free 'PAD 27'\n"                                                    \
    "GP22 free 'PAD 29'\n"                                                    \
    "GP26 free 'PAD 31'\n"                                                    \
    "GP27 free 'PAD 32'\n"                                                    \
    "GP28 free 'PAD 34'\n"

/* OFF selected: all four bound pins name their protocol. */
#define TRACE_OFF                                                             \
    "GP0 held 'DSHOT600 BIDIR'\n"                                             \
    "GP1 held 'MOTOR PWM'\n"                                                  \
    "GP2 held 'SERVO PWM'\n"                                                  \
    TRACE_GP3_TO_GP12                                                         \
    "GP13 held 'SERVO PWM'\n"                                                 \
    TRACE_GP14_TO_GP28

/*
 * SERVO PWM selected: its two pins are ticked and show their pad.  GP17 and
 * GP18 share a compare register with the PWM pins GP1 and GP2, so they are
 * held by them; GP16 is GP0's partner, and GP0 is DShot, which drives no
 * compare register.
 */
#define TRACE_SERVO                                                           \
    "GP0 held 'DSHOT600 BIDIR'\n"                                             \
    "GP1 held 'MOTOR PWM'\n"                                                  \
    "GP2 ticked 'PAD 4'\n"                                                    \
    TRACE_GP3_TO_GP12                                                         \
    "GP13 ticked 'PAD 17'\n"                                                  \
    "GP14 free 'PAD 19'\n"                                                    \
    "GP15 free 'PAD 20'\n"                                                    \
    "GP16 free 'PAD 21'\n"                                                    \
    "GP17 held 'PWM CH OF GP1'\n"                                  \
    "GP18 held 'PWM CH OF GP2'\n"                                  \
    "GP19 free 'PAD 25'\n"                                                    \
    "GP20 free 'PAD 26'\n"                                                    \
    "GP21 free 'PAD 27'\n"                                                    \
    "GP22 free 'PAD 29'\n"                                                    \
    "GP26 free 'PAD 31'\n"                                                    \
    "GP27 free 'PAD 32'\n"                                                    \
    "GP28 free 'PAD 34'\n"

/*
 * Every visit opens on the overview: OFF selected, every bound pin naming
 * its protocol, the line under the list saying what to do.  Entering calls
 * the apply seam for nothing.  A pick then selects as it always does.
 */
TEST_CASE(the_screen_opens_on_the_overview)
{
    pose_three_protocols();
    choose_named("SERVO PWM");          /* a pick made before this visit */
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(s_applied, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "PROTOKOLL WÄHLEN, DANN PIN TIPPEN");
    ui_text_set_language(UI_LANG_EN);

    /* SERVO PWM picked: its own pins show their pad, the others' their
     * protocol, and nothing is written. */
    choose_named("SERVO PWM");
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("SERVO PWM"));
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    CHECK_STR_EQ(reason(), "");
    CHECK_EQ(s_applied, 0);
}

/* No pin bound: the overview shows every pin free or reserved, and no tap
 * on any of them writes. */
TEST_CASE(the_overview_with_nothing_bound_writes_nothing)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
    guard_canvas_t g;
    gfx_canvas_t c;

    fresh();
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 0);
    const uint8_t n = outbind_pin_count(OUTBIND_BOARD_PICO_HEADER);
    for (uint8_t i = 0; i < n; ++i) {
        const outputs_cell_t st = outputs_screen_cell(i, NULL, 0);
        CHECK(st == OUTPUTS_CELL_FREE || st == OUTPUTS_CELL_RESERVED);
    }
    CHECK_STR_EQ(reason(), TR(OUT_OFF_VIEW));
    for (uint8_t gp = 0; gp < 29u; ++gp) {
        if (idx(gp) < n) {
            tap_pin(gp);
        }
    }
    CHECK_EQ(s_applied, 0);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 0);
    guard_canvas_init(&g, &c, px, 800, 432);
    scr()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
}

/* All 8 slots bound over six protocols, as a reading gives them. */
static void every_slot_bound(outbind_t *b)
{
    static const struct { const char *proto; uint8_t gp; } k[8] = {
        { "SERVO PWM", 2 },       { "SERVO PWM", 4 },
        { "MOTOR PWM", 5 },       { "DSHOT600", 6 },
        { "DSHOT600", 7 },        { "DSHOT300", 14 },
        { "DSHOT600 BIDIR", 15 }, { "DSHOT300 BIDIR", 16 },
    };
    outbind_init(b);
    outbind_set_board(b, OUTBIND_BOARD_PICO_HEADER);
    for (unsigned i = 0; i < 8u; ++i) {
        outbind_set_proto(b, (uint8_t)proto_row(k[i].proto));
        CHECK(outbind_toggle(b, idx(k[i].gp)));
    }
    CHECK_EQ((int)outbind_chosen_total(b), (int)LINK_OUT_SLOTS);
}

/*
 * Every slot bound: the overview names the protocol of each of the 8 pins,
 * its line says to choose a protocol rather than that the slots are full,
 * and no tap changes the binding or writes.
 */
TEST_CASE(the_overview_with_every_slot_bound_names_every_pin)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
    guard_canvas_t g;
    gfx_canvas_t c;

    fresh();
    outbind_t b;
    every_slot_bound(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    const uint8_t n = outbind_pin_count(OUTBIND_BOARD_PICO_HEADER);
    unsigned named = 0;
    for (uint8_t i = 0; i < n; ++i) {
        char l[32];
        const outputs_cell_t st = outputs_screen_cell(i, l, sizeof(l));
        const uint8_t held = outbind_group_of(&b, i);
        if (held != 0u) {
            CHECK_EQ(st, OUTPUTS_CELL_HELD);
            CHECK_STR_EQ(l, outbind_protos()[held].name);
            ++named;
        } else {
            CHECK(st == OUTPUTS_CELL_FREE || st == OUTPUTS_CELL_RESERVED);
        }
    }
    CHECK_EQ(named, (unsigned)LINK_OUT_SLOTS);
    CHECK_STR_EQ(reason(), TR(OUT_OFF_VIEW));

    const outbind_t before = *outputs_screen_binding();
    for (uint8_t gp = 0; gp < 29u; ++gp) {
        if (idx(gp) < n) {
            tap_pin(gp);
        }
    }
    CHECK_EQ(s_applied, 0);
    CHECK(memcmp(&before, outputs_screen_binding(), sizeof(before)) == 0);
    guard_canvas_init(&g, &c, px, 800, 432);
    scr()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);

    /* A protocol picked: the slots are full, and the line says so. */
    choose_named("SERVO PWM");
    CHECK_STR_EQ(reason(), "ALL 8 SLOTS IN USE");
    CHECK_EQ(s_applied, 0);
}

/* Whether the line under GP@p gpio's name has a pixel of @p ink. */
static bool label_inked(const gfx_canvas_t *c, uint8_t gpio, gfx_color_t ink)
{
    int x, y;
    cell_centre(gpio, &x, &y);
    const int x0 = x - CELL_W / 2 + 5, y0 = y - CELL_H / 2 + 33;
    for (int yy = y0; yy < y0 + 16; ++yy) {
        for (int xx = x0; xx < x0 + 112; ++xx) {
            if (c->pixels[yy * c->stride + xx] == ink) {
                return true;
            }
        }
    }
    return false;
}

/* Whether any pixel of @p ink lies in the row the pin count is drawn on. */
static bool count_row_inked(const gfx_canvas_t *c, gfx_color_t ink)
{
    const int y0 = DD_Y + DD_H + 16;
    for (int yy = y0; yy < y0 + 14; ++yy) {
        for (int xx = DD_X; xx < DD_X + DD_W; ++xx) {
            if (c->pixels[yy * c->stride + xx] == ink) {
                return true;
            }
        }
    }
    return false;
}

/*
 * OFF takes no pin, so the overview shows no pin count, and a press on a
 * pin draws no pressed face: the cell does not look as if it would act.
 * A protocol picked brings both back.
 */
TEST_CASE(the_overview_shows_no_count_and_no_pressed_face)
{
    static gfx_color_t px[800 * 432], was[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

    pose_three_protocols();
    scr()->enter();
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    CHECK(!count_row_inked(&c, UI_TEXT_DIM));
    memcpy(was, px, sizeof(px));

    int x, y;
    cell_centre(4, &x, &y);                     /* a free pin */
    press_at(x, y);
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    CHECK(memcmp(was, px, sizeof(px)) == 0);
    release_at(x, y);
    CHECK_EQ(s_applied, 0);

    choose_named("SERVO PWM");
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    CHECK(count_row_inked(&c, UI_TEXT_DIM));
    memcpy(was, px, sizeof(px));
    press_at(x, y);
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    CHECK(memcmp(was, px, sizeof(px)) != 0);
    release_at(x, y);
    CHECK_EQ(s_applied, 1);
}

/*
 * The overview draws the protocol under each bound pin in full ink, apart
 * from the pad numbers of free pins.  With a protocol picked, the pins of
 * the others are dimmed as before, and so is the overview while the
 * binding is not confirmed.
 */
TEST_CASE(the_overview_draws_each_bound_pins_protocol_in_full_ink)
{
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

    pose_three_protocols();
    scr()->enter();
    scr()->render(&c, 0);
    CHECK(label_inked(&c, 0, UI_TEXT));
    CHECK(label_inked(&c, 13, UI_TEXT));
    CHECK(!label_inked(&c, 4, UI_TEXT));        /* free: PAD 6, faint */

    choose_named("SERVO PWM");
    scr()->render(&c, 0);
    CHECK(!label_inked(&c, 0, UI_TEXT));
    CHECK(label_inked(&c, 0, UI_TEXT_DIM));

    choose_named("OFF");
    bind_reading_t none;
    (void)bind_link_classify(&none, OUTBIND_BOARD_PICO_HEADER, NULL, NULL);
    outputs_screen_set_reading(&none);
    scr()->render(&c, 0);
    CHECK(!label_inked(&c, 0, UI_TEXT));
    CHECK(label_inked(&c, 0, UI_TEXT_DIM));
}

/*
 * A protocol picked and OFF picked again: back on the overview, where the
 * pin just ticked names its protocol and a tap on it writes nothing.  The
 * same protocol picked again takes the tap.
 */
TEST_CASE(a_pick_and_off_again_go_to_a_protocol_and_back_to_the_overview)
{
    pose_three_protocols();
    scr()->enter();
    choose_named("SERVO PWM");
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outputs_screen_cell(idx(4), NULL, 0), OUTPUTS_CELL_TICKED);

    choose_named("OFF");
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(s_applied, 1);
    char l[32];
    CHECK_EQ(outputs_screen_cell(idx(4), l, sizeof(l)), OUTPUTS_CELL_HELD);
    CHECK_STR_EQ(l, "SERVO PWM");
    CHECK_STR_EQ(reason(), TR(OUT_OFF_VIEW));
    tap_pin(4);
    tap_pin(5);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(outputs_screen_binding(), idx(4)),
             proto_row("SERVO PWM"));
    CHECK_EQ(outbind_group_of(outputs_screen_binding(), idx(5)), 0);

    choose_named("SERVO PWM");
    tap_pin(4);                                 /* unticked */
    CHECK_EQ(s_applied, 2);
    CHECK_EQ(outbind_group_of(&s_last, idx(4)), 0);
}

/*
 * OFF selected, three protocols bound: every bound pin prints its
 * protocol's name, every free pin its pad, every reserved pin its holder.
 * The whole board, as a text trace of what is drawn under each pin.
 */
TEST_CASE(off_shows_the_protocol_of_every_bound_pin)
{
    pose_three_protocols();
    choose_named("OFF");
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(s_applied, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");

    /* The same pins under SERVO PWM: its own two show their pad. */
    choose_named("SERVO PWM");
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    CHECK_STR_EQ(reason(), "");
    CHECK_EQ(s_applied, 0);
}

/*
 * With OFF selected no pin can be ticked or unticked: a tap changes nothing
 * and calls nothing, and the line under the protocol says what OFF is for.
 */
TEST_CASE(a_tap_on_a_pin_does_nothing_while_off_is_selected)
{
    pose_three_protocols();
    choose_named("OFF");
    const outbind_t before = *outputs_screen_binding();
    for (uint8_t g = 0; g < 29u; ++g) {
        if (idx(g) < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)) {
            tap_pin(g);
        }
    }
    CHECK_EQ(s_applied, 0);
    CHECK(memcmp(&before, outputs_screen_binding(), sizeof(before)) == 0);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "PROTOKOLL WÄHLEN, DANN PIN TIPPEN");
    ui_text_set_language(UI_LANG_EN);
}

/*
 * OFF is a pick like any other.  It stays across a reading of the same
 * pages, a second one, a repaint, a link that goes and comes back, a
 * reading of other pins, a leave and a change of board, until another
 * entry is picked.
 */
TEST_CASE(off_stays_until_another_entry_is_picked)
{
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

    pose_three_protocols();
    scr()->enter();
    choose_named("OFF");
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    CHECK_EQ((int)r.bind.proto, proto_row("SERVO PWM"));   /* the page's */

    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_binding(&r.bind);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);

    /* Repaints, in every framebuffer. */
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    scr()->render(&c, 1);
    scr()->cancel();
    scr()->render(&c, 0);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    /* The link goes: nothing read.  It comes back: the link-up read. */
    bind_reading_t none;
    (void)bind_link_classify(&none, OUTBIND_BOARD_PICO_HEADER, NULL, NULL);
    outputs_screen_set_reading(&none);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);
    CHECK(outputs_screen_editable());

    /* A reading with other pins: still OFF, showing them. */
    outbind_t fewer = b;
    outbind_set_proto(&fewer, (uint8_t)proto_row("SERVO PWM"));
    (void)outbind_toggle(&fewer, idx(13));
    read_back(&fewer, &r);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0), OUTPUTS_CELL_FREE);
    CHECK_EQ(outputs_screen_cell(idx(2), NULL, 0), OUTPUTS_CELL_HELD);

    /* Another entry is picked: that one is kept from here on. */
    choose_named("MOTOR PWM");
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("MOTOR PWM"));

    /* OFF again, and the screen is left: the selection stays OFF, and the
     * next entry opens on the overview as every entry does. */
    choose_named("OFF");
    scr()->leave();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    scr()->enter();
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    /* A picked protocol other than OFF outlasts the visit; the next entry
     * opens on the overview. */
    choose_named("PPM");
    scr()->leave();
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(s_applied, 0);

    /* A protocol named from outside lands, as a pick does. */
    choose_named("OFF");
    outbind_t named = r.bind;
    outbind_set_proto(&named, (uint8_t)proto_row("PPM"));
    outputs_screen_set_binding(&named);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));

    /* A change of board keeps OFF: no protocol was picked for the board
     * that answers now.  Out to another board and back, by binding and by
     * reading, a board with no pin map included. */
    choose_named("OFF");
    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
    outputs_screen_set_binding(&other);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    bind_reading_t unmapped = r;
    unmapped.state = BIND_READ_NO_BOARD;
    outbind_init(&unmapped.bind);
    outbind_set_board(&unmapped.bind, (uint16_t)0xBEEFu);
    outputs_screen_set_reading(&unmapped);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    tap_pin(13);
    tap_pin(2);
    CHECK_EQ(s_applied, 0);
}

/*
 * The panel boots with the IOmcu off: the screen knows no board, and
 * opens on the overview.  The IOmcu comes up and its first reading
 * names a board and a binding.  Nobody picked a protocol, so a tap still
 * writes nothing -- the overview must not hand the next tap the lowest
 * protocol the reading holds.
 */
TEST_CASE(a_board_that_answers_during_a_visit_keeps_the_overview)
{
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);

    /* No board yet: the read the panel posts while the link is down. */
    scr()->reset();
    outputs_screen_set_apply(on_apply);
    s_applied = 0;
    bind_reading_t none;
    (void)bind_link_classify(&none, 0u, NULL, NULL);
    outputs_screen_set_reading(&none);
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    outputs_screen_set_reading(&r);
    CHECK(outputs_screen_editable());
    CHECK_EQ((int)outputs_screen_binding()->board,
             (int)OUTBIND_BOARD_PICO_HEADER);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    for (uint8_t gp = 0; gp < 30u; ++gp) {
        if (idx(gp) < OUTBIND_PINS) { tap_pin(gp); }
    }
    CHECK_EQ(s_applied, 0);

    /* A pick is what makes a pin tickable. */
    choose_named("SERVO PWM");
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(4)), proto_row("SERVO PWM"));
}

/* An IOmcu swapped for another board type while the screen is open:
 * the overview stays, and a tap writes nothing. */
TEST_CASE(a_board_swapped_during_a_visit_keeps_the_overview)
{
    fresh();
    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
    outputs_screen_set_binding(&other);
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->board,
             (int)OUTBIND_BOARD_PICO_HEADER);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    tap_pin(13);
    CHECK_EQ(s_applied, 0);
    CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0), OUTPUTS_CELL_HELD);
}

/* OFF with nothing bound is where a bench starts: a reading does not move
 * it, and the first pick is what makes a pin tickable. */
TEST_CASE(off_with_nothing_bound_is_left_by_a_pick)
{
    fresh();
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    tap_pin(4);
    CHECK_EQ(s_applied, 0);
    choose_named("SERVO PWM");
    CHECK_STR_EQ(reason(), "");
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
}

/* ------------------------------------------------- a binding not confirmed */

/*
 * A reading that is not a binding leaves the last one on the screen, takes
 * back an edit nobody confirmed, says why, and refuses every tap on a pin.
 * The next reading that is a binding ends that.
 */
TEST_CASE(a_binding_that_did_not_read_is_kept_marked_and_not_edited)
{
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

    pose_three_protocols();
    CHECK(outputs_screen_editable());
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_OK);
    scr()->render(&c, 0);
    const unsigned quiet = reason_ink(&c);

    /* An edit, and then the read that should have confirmed it fails. */
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 5);
    bind_reading_t none;
    (void)bind_link_classify(&none, OUTBIND_BOARD_PICO_HEADER, NULL, NULL);
    outputs_screen_set_reading(&none);

    CHECK(!outputs_screen_editable());
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_NONE);
    CHECK_STR_EQ(trace(), TRACE_SERVO);         /* GP4 is free again */
    CHECK_STR_EQ(reason(), "ASSIGNMENT NOT READ. EDITING LOCKED.");
    scr()->render(&c, 0);
    CHECK(reason_ink(&c) != quiet);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "ZUORDNUNG NICHT AUSGELESEN. ÄNDERN GESPERRT.");
    ui_text_set_language(UI_LANG_EN);

    /* Ticked, free, held or reserved: no tap acts. */
    for (uint8_t g = 0; g < 29u; ++g) {
        if (idx(g) < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)) {
            tap_pin(g);
        }
    }
    CHECK_EQ(s_applied, 1);
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    /* A press that began while the binding was confirmed does not act on
     * its release either. */
    bind_reading_t ok;
    outbind_t b;
    three_protocols(&b);
    read_back(&b, &ok);
    outputs_screen_set_reading(&ok);
    int x, y;
    cell_centre(4, &x, &y);
    press_at(x, y);
    outputs_screen_set_reading(&none);
    release_at(x, y);
    CHECK_EQ(s_applied, 1);

    /* The list still opens and a pick still changes what is shown. */
    choose_named("MOTOR PWM");
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outputs_screen_cell(idx(1), NULL, 0), OUTPUTS_CELL_TICKED);
    CHECK_STR_EQ(reason(), "ASSIGNMENT NOT READ. EDITING LOCKED.");
    choose_named("SERVO PWM");

    /* Pages no binding describes: the same, with its own reason. */
    bind_reading_t odd = ok;
    odd.state = BIND_READ_ODD;
    outbind_init(&odd.bind);
    outbind_set_board(&odd.bind, OUTBIND_BOARD_PICO_HEADER);
    outputs_screen_set_reading(&odd);
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_ODD);
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    CHECK_STR_EQ(reason(), "NO VALID ASSIGNMENT IN THE PAGES");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "KEINE GÜLTIGE ZUORDNUNG IN DEN PAGES");
    ui_text_set_language(UI_LANG_EN);
    tap_pin(4);
    tap_pin(2);
    CHECK_EQ(s_applied, 1);
    scr()->render(&c, 0);
    scr()->render(&c, 1);

    /* A read succeeds: the binding it gives, and edits again. */
    outputs_screen_set_reading(&ok);
    CHECK(outputs_screen_editable());
    CHECK_STR_EQ(reason(), "");
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    tap_pin(4);
    CHECK_EQ(s_applied, 2);
    outputs_screen_set_reading(NULL);
    CHECK(outputs_screen_editable());
}

/* A reading that is not a binding, for another board than the one shown:
 * nothing read earlier is a binding of it, so nothing is kept. */
TEST_CASE(a_failed_read_for_another_board_keeps_no_binding)
{
    pose_three_protocols();
    bind_reading_t none;
    (void)bind_link_classify(&none, (uint16_t)0x7777u, NULL, NULL);
    outputs_screen_set_reading(&none);
    CHECK_EQ(outputs_screen_binding()->board, 0x7777u);
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 0);
    CHECK(!outputs_screen_editable());
    CHECK_EQ(outputs_screen_cell(0, NULL, 0), OUTPUTS_CELL_NONE);

    /* A board this build has no pin map for shows no pins and takes no
     * edit; the reading for a known board ends that. */
    pose_three_protocols();
    uint16_t slots[LINK_OS_COUNT], cc[LINK_CC_COUNT];
    (void)outbind_to_slots(outputs_screen_binding(), slots);
    outbind_to_chan_cfg(outputs_screen_binding(), cc, 1000u, 2000u);
    bind_reading_t r;
    CHECK_EQ(bind_link_classify(&r, (uint16_t)0x7777u, slots, cc),
             BIND_READ_NO_BOARD);
    outputs_screen_set_reading(&r);
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_NO_BOARD);
    CHECK(!outputs_screen_editable());
    CHECK_EQ(outbind_pin_count(outputs_screen_binding()->board), 0);
    outbind_t b;
    three_protocols(&b);
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    CHECK(outputs_screen_editable());
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);  /* another board */
    CHECK_STR_EQ(trace(), TRACE_OFF);
}

/* ------------------------------------------------------------------ the fit */

/*
 * The longest protocol names inside what they are drawn in: the closed list
 * in the head face (16 px a cell) between its 10 px inset and the chevron's
 * column, and the line under a pin in the label face (8 px a cell) inside
 * the cell.  One cell more than the longest name does not fit either.
 */
TEST_CASE(the_longest_protocol_name_fits_the_list_and_a_cell)
{
    int longest = 0;
    for (unsigned i = 0; i < OUTBIND_PROTOS; ++i) {
        const int n = (int)strlen(outbind_protos()[i].name);
        if (n > longest) {
            longest = n;
        }
    }
    CHECK_EQ(longest, 14);                      /* "DSHOT600 BIDIR" */
    const int head = gfx_text_width(UI_FONT_HEAD, "DSHOT600 BIDIR", 1);
    const int lab  = gfx_text_width(UI_FONT_LABEL, "DSHOT600 BIDIR", 1);
    CHECK_EQ(head, 224);
    CHECK_EQ(lab, 112);
    /* The list: text from x + 10, the chevron's column from x + w - 28. */
    CHECK(10 + head <= DD_W - 28);
    CHECK(10 + head + 16 > DD_W - 28);
    /* A cell: text from x + 5, the border at x + w - 1. */
    CHECK(5 + lab <= CELL_W - 1);
    CHECK(5 + lab + 8 > CELL_W - 1);
    /* And the grid ends inside the 800 px screen with its 16 px margin. */
    CHECK_EQ(GRID_X + 4 * CELL_W + 3 * CELL_GAP, 800 - 16);
    CHECK(DD_X + DD_W < GRID_X);
}

/* The two entry points the pin picker uses: a selection from outside goes
 * through the same checks as a pick, and the reason by itself takes no
 * missing binding or buffer. */
TEST_CASE(a_selection_from_the_picker_is_a_pick)
{
    fresh();
    outputs_screen_select((uint8_t)proto_row("DSHOT300"));
    CHECK_EQ(outputs_screen_binding()->proto, proto_row("DSHOT300"));
    CHECK_EQ(s_applied, 0);
    outputs_screen_select((uint8_t)OUTBIND_PROTOS);
    CHECK_EQ(outputs_screen_binding()->proto, proto_row("DSHOT300"));
    outputs_screen_select(0u);
    CHECK_STR_EQ(reason(), TR(OUT_OFF_VIEW));
    CHECK_EQ(s_applied, 0);

    char buf[8] = "x";
    CHECK(!outputs_reason_of(NULL, buf, sizeof(buf)));
    CHECK_STR_EQ(buf, "");
    CHECK(!outputs_reason_of(outputs_screen_binding(), NULL, 0));

    /* The PWM clash lines take no missing binding, buffer or pin either. */
    strcpy(buf, "x");
    CHECK(!outputs_clash_reason(NULL, 0, k_gp13, 1u, buf, sizeof(buf)));
    CHECK_STR_EQ(buf, "");
    CHECK(!outputs_clash_reason(outputs_screen_binding(), -1, k_gp13, 1u, buf,
                                sizeof(buf)));
    CHECK(!outputs_clash_reason(outputs_screen_binding(),
                                (int)OUTBIND_PINS, k_gp13, 1u, buf,
                                sizeof(buf)));
    CHECK(!outputs_clash_reason(outputs_screen_binding(), 0, k_gp13, 1u,
                                NULL, 0));
    strcpy(buf, "x");
    CHECK(!outputs_clash_label(outputs_screen_binding(), 0u, k_gp13, 1u, buf,
                               sizeof(buf)));
    CHECK_STR_EQ(buf, "");
    CHECK(!outputs_clash_label(outputs_screen_binding(), 0u, k_gp13, 1u,
                               NULL, 0));
}

/*
 * The column's lines that break onto a second line where they need it: the
 * reason under the protocol (266 px), the label of the button that unbinds
 * every pin (250 px inside it) and the last write in the head face
 * (266 px).  ui_text.def holds each to both lines' cells; each has to break
 * into the two, in every language, a pin number at its widest.
 */
TEST_CASE(every_two_line_string_of_the_column_breaks_into_two)
{
    static const struct {
        ui_text_id_t id;
        int w;
        bool head;
    } k[] = {
        { TX_OUT_OFF_VIEW, 266, false },    { TX_OUT_NOT_READ, 266, false },
        { TX_OUT_NOT_MAPPED, 266, false },  { TX_OUT_SLICE_WHY, 266, false },
        { TX_OUT_COMPARE_WHY, 266, false }, { TX_OUT_HOLD_UNBIND, 250, false },
        { TX_OUT_WRITTEN, 266, true },      { TX_OUT_REFUSED, 266, true },
        { TX_OUT_NOT_WRITTEN, 266, true },  { TX_OUT_NO_LINK, 266, true },
        { TX_OUT_PORT_CARRIES, 266, false }, { TX_OUT_PORT_HELD, 266, false },
        { TX_OUT_PORT_LOCKED, 266, false },
        { TX_OUT_PORT_PWM_NEEDS, 266, false },
        { TX_OUT_PORT_TESTER_ONE, 266, false },
    };
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
            char s[96];
            snprintf(s, sizeof(s), ui_tr_in((ui_lang_t)l, k[i].id), 28u,
                     13u);
            const int n = outputs_wrap(NULL, gfx_rect_make(0, 0, k[i].w, 16),
                                       18, s, 3,
                                       k[i].head ? UI_FONT_HEAD
                                                 : UI_FONT_LABEL,
                                       0, false);
            if (n > 2) {
                T_FAIL("%s in language %d takes %d lines",
                       ui_text_name(k[i].id), l, n);
            }
        }
    }
}

/* ------------------------------------------------------ the PROG PORT entry */

/* SERVO TESTER (y 204 to 240) and PORT PWM (y 274 to 310), two segments of
 * 128 px 4 px apart from x 19, under their labels. */
#define SEG_X(i)      (19 + 64 + (i) * 132)
#define SEG_TESTER_Y  (204 + 18)
#define SEG_PWM_Y     (274 + 18)

static void ports(uint8_t count, uint8_t pin0)
{
    const uint8_t pins[4] = { pin0, 26u, OUTPUTS_NO_PIN, 27u };
    outputs_screen_set_prog_ports(count, pins);
}

/* The list's rows as the screen sizes them: 46 px, or less where the
 * protocols and the entries would pass the screen's 432 px. */
static int row_h(void)
{
    const int fit = (432 - DD_Y - 16)
                    / ((int)OUTBIND_PROTOS + outputs_screen_port_entries());
    return fit < 46 ? fit : 46;
}

static void choose_port(int n)
{
    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);
    const int i = (int)OUTBIND_PROTOS + n;
    tap(DD_X + 40, DD_Y + 4 + i * row_h() + row_h() / 2);
}

static void fresh_port(void)
{
    fresh();
    settings_set_store(NULL);
    settings_init();
    settings_set_hold(NULL);
    servo_screen_set_bench_armed(false);
    ports(1u, 13u);
}

/* The panel's start for the other cases: the count not known, no pin. */
static void ports_back(void)
{
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
    settings_set_hold(NULL);
}

static bool s_held;
static bool hold_port(setting_id_t id)
{
    return s_held && (id == SET_SERVO_TARGET || id == SET_SERVO_PORT_PWM);
}

/*
 * One entry a port the IOmcu reports, after the protocols: one while the
 * count is not known, none for a count of 0, at most LINK_PG_PORTS_MAX
 * however many the page claims.  A pick writes nothing; a protocol picked after leaves the entry,
 * and an entry the count drops goes back to the overview.  Every list fits
 * the screen.
 */
TEST_CASE(the_list_ends_in_one_prog_port_entry_per_port_reported)
{
    static const struct { uint8_t count, entries; } k[] = {
        { OUTPUTS_PORTS_UNKNOWN, 1u }, { 0u, 0u }, { 1u, 1u }, { 2u, 2u },
        { 4u, 4u }, { 5u, 4u }, { 254u, 4u },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh_port();
        ports(k[i].count, 13u);
        CHECK_EQ(outputs_screen_port_entries(), k[i].entries);
        CHECK(DD_Y + 8 + row_h() * ((int)OUTBIND_PROTOS + k[i].entries)
              <= 432);
        for (int n = 0; n < (int)k[i].entries; ++n) {
            choose_port(n);
            CHECK_EQ(outputs_screen_port_shown(), n);
            CHECK_EQ((int)outputs_screen_binding()->proto, 0);
        }
        CHECK_EQ(s_applied, 0);
        /* No port, no pin; a count not known keeps port 1's as the PIN
         * register reads it. */
        CHECK_EQ(outputs_screen_port_pin(0),
                 k[i].count == 0u ? OUTPUTS_NO_PIN : 13u);
        CHECK_EQ(outputs_screen_cell(idx(13u), NULL, 0),
                 k[i].count == 0u ? OUTPUTS_CELL_FREE
                                  : OUTPUTS_CELL_RESERVED);
        CHECK_EQ(outputs_screen_port_pin(k[i].entries), OUTPUTS_NO_PIN);
    }
    /* The pins of ports 2 to 4, as reported; none past the count. */
    ports(4u, 13u);
    CHECK_EQ(outputs_screen_port_pin(1), 26u);
    CHECK_EQ(outputs_screen_port_pin(2), OUTPUTS_NO_PIN);
    CHECK_EQ(outputs_screen_port_pin(3), 27u);
    ports(2u, 13u);
    CHECK_EQ(outputs_screen_port_pin(3), OUTPUTS_NO_PIN);

    /* A protocol after the entry: the entry goes. */
    fresh_port();
    choose_port(0);
    choose_named("SERVO PWM");
    CHECK_EQ(outputs_screen_port_shown(), -1);
    CHECK_STR_EQ(outbind_protos()[outputs_screen_binding()->proto].name,
                 "SERVO PWM");

    /* The count drops under the entry shown: the overview. */
    ports(4u, 13u);
    choose_port(3);
    CHECK_EQ(outputs_screen_port_shown(), 3);
    ports(3u, 13u);
    CHECK_EQ(outputs_screen_port_shown(), -1);
    outputs_screen_select_port(3u);              /* past the list */
    CHECK_EQ(outputs_screen_port_shown(), -1);
    ports(OUTPUTS_PORTS_UNKNOWN, 13u);
    outputs_screen_select_port(0u);
    CHECK_EQ(outputs_screen_port_shown(), 0);
    ports(1u, 13u);                              /* known now: stays */
    CHECK_EQ(outputs_screen_port_shown(), 0);
    ports(0u, 13u);                              /* known: no port */
    CHECK_EQ(outputs_screen_port_shown(), -1);
    outputs_screen_select_port(0u);
    CHECK_EQ(outputs_screen_port_shown(), -1);
    ports_back();
}

/*
 * The port's pin is read-only: its entry ticks no pin and writes nothing,
 * a reading of the binding keeps it, and the port's cell is lit -- drawn
 * in the accent -- where every protocol draws it as the port's, struck in
 * red.
 */
TEST_CASE(a_prog_port_entry_lights_its_pin_and_takes_no_tick)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
    guard_canvas_t g;
    gfx_canvas_t c;
    int x, y;
    cell_centre(13u, &x, &y);
    const int top = y - CELL_H / 2;

    fresh_port();
    choose_port(0);
    tap_pin(0u);
    tap_pin(13u);
    tap_pin(2u);
    CHECK_EQ(s_applied, 0);
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 0);
    char label[32];
    CHECK_EQ(outputs_screen_cell(idx(13u), label, sizeof(label)),
             OUTPUTS_CELL_RESERVED);
    CHECK_STR_EQ(label, TR(OUT_PROG_PORT));
    guard_canvas_init(&g, &c, px, 800, 432);
    scr()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
    CHECK_EQ(c.pixels[(size_t)top * (size_t)c.stride + (size_t)x], UI_ACCENT);

    /* A reading keeps the entry. */
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    CHECK_EQ(outputs_screen_port_shown(), 0);
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 4);

    /* Under a protocol the port's cell is the port's, not lit. */
    fresh_port();
    choose_named("SERVO PWM");
    scr()->render(&c, 0);
    CHECK(c.pixels[(size_t)top * (size_t)c.stride + (size_t)x] != UI_ACCENT);
    tap_pin(13u);
    CHECK_EQ(s_applied, 0);

    /* With no pin known the entry lights none, and every pin of every
     * entry draws inside the canvas. */
    for (uint8_t pin0 = 0; pin0 < 2u; ++pin0) {
        fresh_port();
        ports(4u, pin0 == 0u ? OUTPUTS_NO_PIN : 13u);
        for (int n = 0; n < 4; ++n) {
            choose_port(n);
            guard_canvas_init(&g, &c, px, 800, 432);
            scr()->render(&c, 0);
            CHECK_EQ(guard_canvas_touched(&g), 0u);
            CHECK_EQ(guard_canvas_unpainted(&g), 0u);
            tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);  /* the list over it */
            scr()->render(&c, 1);
            CHECK_EQ(guard_canvas_touched(&g), 0u);
            tap(DD_X + DD_W / 2, 420);               /* closes it */
        }
    }
    ports_back();
}

/*
 * outputs_screen_open_on(): the next entry opens on that protocol or port,
 * once; a protocol past the table, MIXED, or a port the list does not show
 * opens the overview, and a reset forgets what was asked.
 */
TEST_CASE(the_next_entry_opens_on_the_entry_asked_for_once)
{
    fresh_port();
    const uint8_t dshot = (uint8_t)proto_row("DSHOT300");
    outputs_screen_open_on(dshot);
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, (int)dshot);
    CHECK_EQ(outputs_screen_port_shown(), -1);
    scr()->leave();
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    outputs_screen_open_on_port(0u);
    scr()->enter();
    CHECK_EQ(outputs_screen_port_shown(), 0);
    scr()->leave();
    scr()->enter();
    CHECK_EQ(outputs_screen_port_shown(), -1);

    static const uint8_t bad[] = { OUTBIND_PROTOS, 0xFFu };
    for (size_t i = 0; i < sizeof(bad); ++i) {
        outputs_screen_open_on(bad[i]);
        scr()->enter();
        CHECK_EQ((int)outputs_screen_binding()->proto, 0);
        CHECK_EQ(outputs_screen_port_shown(), -1);
    }
    outputs_screen_open_on_port(1u);             /* 1 entry: port 1 only */
    scr()->enter();
    CHECK_EQ(outputs_screen_port_shown(), -1);
    outputs_screen_open_on_port(LINK_PG_PORTS_MAX);
    ports(4u, 13u);
    scr()->enter();
    CHECK_EQ(outputs_screen_port_shown(), -1);
    outputs_screen_open_on_port(0u);             /* no port: the overview */
    ports(0u, 13u);
    scr()->enter();
    CHECK_EQ(outputs_screen_port_shown(), -1);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    outputs_screen_open_on(dshot);
    scr()->reset();
    outputs_screen_set_apply(on_apply);
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    ports_back();
}

/*
 * SERVO TESTER and PORT PWM set their settings, a tap each, and say why
 * they take none: PORT PWM with OUTPUTS as the tester's target, the bench
 * ARMED, the programmer holding the port.  A press that slides off its
 * segment, is cancelled, or is locked under it sets nothing.  Port 2's
 * entry has neither.  A change from outside repaints the column.
 */
TEST_CASE(the_port_segments_set_the_tester_and_say_why_they_are_locked)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
    guard_canvas_t g;
    gfx_canvas_t c;
    char why[96];

    fresh_port();
    choose_port(0);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    /* OUTPUTS as target: PORT PWM is locked and says so. */
    CHECK(outputs_screen_port_reason(why, sizeof(why)));
    CHECK_STR_EQ(why, TR(OUT_PORT_PWM_NEEDS));
    CHECK_STR_EQ(reason(), TR(OUT_PORT_PWM_NEEDS));
    tap(SEG_X(1), SEG_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));

    tap(SEG_X(1), SEG_TESTER_Y);                 /* PROG PORT */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    CHECK(!outputs_screen_port_reason(why, sizeof(why)));
    CHECK_STR_EQ(why, "");
    tap(SEG_X(1), SEG_PWM_Y);                    /* OFF */
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    tap(SEG_X(0), SEG_PWM_Y);                    /* ON */
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    tap(SEG_X(1), SEG_TESTER_Y);                 /* the same: no change */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    CHECK_EQ(s_applied, 0);

    /* A press that slides to the other segment, and one cancelled. */
    press_at(SEG_X(0), SEG_TESTER_Y);
    release_at(SEG_X(1), SEG_TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    press_at(SEG_X(0), SEG_TESTER_Y);
    scr()->cancel();
    release_at(SEG_X(0), SEG_TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);

    /* The bench ARMED (for the motor): the target stays, the pulse goes
     * off and not on. */
    servo_screen_set_bench_armed(true);
    CHECK(outputs_screen_port_reason(why, sizeof(why)));
    CHECK_STR_EQ(why, TR(OUT_PORT_LOCKED));
    tap(SEG_X(0), SEG_TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    tap(SEG_X(1), SEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    tap(SEG_X(0), SEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    /* A press begun before the bench armed acts on nothing. */
    servo_screen_set_bench_armed(false);
    press_at(SEG_X(0), SEG_TESTER_Y);
    servo_screen_set_bench_armed(true);
    release_at(SEG_X(0), SEG_TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    servo_screen_set_bench_armed(false);

    /* The programmer holds the port. */
    settings_set_hold(hold_port);
    s_held = true;
    CHECK(outputs_screen_port_reason(why, sizeof(why)));
    CHECK_STR_EQ(why, TR(OUT_PORT_HELD));
    tap(SEG_X(0), SEG_TESTER_Y);
    tap(SEG_X(0), SEG_PWM_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    /* Every lock draws inside the canvas. */
    guard_canvas_init(&g, &c, px, 800, 432);
    scr()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
    s_held = false;
    settings_set_hold(NULL);

    /* A change from outside repaints the column on the next tick. */
    scr()->tick(0.02f);
    scr()->render(&c, 0);
    const uint32_t before = guard_canvas_sum(&g);
    scr()->render(&c, 0);                        /* nothing changed */
    CHECK_EQ(guard_canvas_sum(&g), before);
    settings_set(SET_SERVO_TARGET, 0.0f);
    scr()->tick(0.02f);
    scr()->render(&c, 0);
    CHECK(guard_canvas_sum(&g) != before);

    /* Port 2's entry: neither segment, no reason; taps there set
     * nothing. */
    ports(2u, 13u);
    choose_port(1);
    CHECK(!outputs_screen_port_reason(why, sizeof(why)));
    tap(SEG_X(1), SEG_TESTER_Y);
    tap(SEG_X(0), SEG_PWM_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    ports_back();
}

int main(void)
{
    RUN(a_pick_calls_the_apply_seam_for_no_entry);
    RUN(the_screen_opens_on_the_overview);
    RUN(the_overview_shows_no_count_and_no_pressed_face);
    RUN(the_overview_with_nothing_bound_writes_nothing);
    RUN(the_overview_with_every_slot_bound_names_every_pin);
    RUN(the_overview_draws_each_bound_pins_protocol_in_full_ink);
    RUN(a_pick_and_off_again_go_to_a_protocol_and_back_to_the_overview);
    RUN(off_shows_the_protocol_of_every_bound_pin);
    RUN(a_tap_on_a_pin_does_nothing_while_off_is_selected);
    RUN(off_stays_until_another_entry_is_picked);
    RUN(a_board_that_answers_during_a_visit_keeps_the_overview);
    RUN(a_board_swapped_during_a_visit_keeps_the_overview);
    RUN(off_with_nothing_bound_is_left_by_a_pick);
    RUN(a_binding_that_did_not_read_is_kept_marked_and_not_edited);
    RUN(a_failed_read_for_another_board_keeps_no_binding);
    RUN(the_longest_protocol_name_fits_the_list_and_a_cell);
    RUN(a_selection_from_the_picker_is_a_pick);
    RUN(an_empty_read_back_does_not_clear_the_chosen_protocol);
    RUN(a_protocol_named_from_outside_lands_over_the_one_on_screen);
    RUN(a_second_protocol_survives_the_read_back);
    RUN(a_change_of_board_takes_the_protocol_with_it);
    RUN(a_pick_made_for_one_board_is_not_carried_to_another);
    RUN(the_protocol_list_opens_and_a_choice_closes_it);
    RUN(an_open_list_can_be_left_without_choosing);
    RUN(the_home_tag_closes_the_open_list_first);
    RUN(a_release_away_from_the_press_does_nothing);
    RUN(ticking_a_pin_applies_once_and_unticking_applies_again);
    RUN(a_reserved_pin_never_reaches_the_apply_seam);
    RUN(a_pin_too_many_does_not_apply);
    RUN(the_programming_ports_pin_is_taken_and_takes_no_tick);
    RUN(an_interface_pin_is_taken_named_and_takes_no_tick);
    RUN(a_pwm_pin_on_the_programming_ports_slice_is_not_offered);
    RUN(a_pwm_pin_on_a_bound_pins_compare_register_is_not_offered);
    RUN(nothing_can_be_ticked_while_the_protocol_is_off);
    RUN(every_state_renders_without_reading_off_the_canvas);
    RUN(a_board_that_can_take_nothing_says_why);
    RUN(a_protocol_that_has_all_its_pins_says_so);
    RUN(the_binding_survives_being_set_from_outside);
    RUN(a_protocol_index_from_outside_cannot_run_off_the_table);
    RUN(null_events_are_refused_rather_than_dereferenced);
    RUN(a_cancelled_press_applies_nothing);
    RUN(a_release_the_tracker_makes_for_a_jump_picks_nothing);
    RUN(every_two_line_string_of_the_column_breaks_into_two);
    RUN(the_list_ends_in_one_prog_port_entry_per_port_reported);
    RUN(a_prog_port_entry_lights_its_pin_and_takes_no_tick);
    RUN(the_next_entry_opens_on_the_entry_asked_for_once);
    RUN(the_port_segments_set_the_tester_and_say_why_they_are_locked);
    return test_summary("outputs_screen");
}
