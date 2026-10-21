/*
 * The outputs screen's PROG PORT entries: one per port reported, the pin a
 * port lights and the tester it sets.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen_rig.h"

/* ------------------------------------------------------ the PROG PORT entry */

/* SERVO TESTER (y 262 to 298) and PORT PWM (y 326 to 362), two segments
 * of 128 px 4 px apart from x 19, under their labels
 * (outputs_port_entry.c). */
#define SEG_X(i)      (19 + 64 + (i) * 132)

#define SEG_TESTER_Y  (262 + 18)

#define SEG_PWM_Y     (326 + 18)

/* The ports as the screens see them: port 1 SERVO PWM on @p pin0, the
 * others NONE on GP26, none and GP27, from an IOmcu with FUNC. */
static void ports(uint8_t count, uint8_t pin0)
{
    const uint8_t pins[4] = { pin0, 26u, OUTPUTS_NO_PIN, 27u };
    outputs_screen_set_prog_ports(count, pins);
    port_view_t v;
    port_view_unknown(&v);
    if (count != OUTPUTS_PORTS_UNKNOWN) {
        v.count    = count;
        v.has_func = true;
        for (unsigned n = 0; n < 4u; ++n) {
            v.pin[n]  = pins[n];
            v.func[n] = (n == 0u) ? (uint8_t)LINK_PG_FN_SERVO_PWM
                                  : (uint8_t)LINK_PG_FN_NONE;
        }
    }
    port_view_set(&v);
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
    port_view_t v;
    port_view_unknown(&v);
    port_view_set(&v);
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
 * entry, a NONE port, takes neither.  A change from outside repaints the
 * column.
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

    tap(SEG_X(1), SEG_TESTER_Y);                 /* PORT 1 */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    /* Why FUNCTION takes no press now. */
    CHECK(outputs_screen_port_reason(why, sizeof(why)));
    CHECK_STR_EQ(why, "SERVO TESTER DRIVES PROG PORT 1");
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

    /* Port 2's entry, a NONE port: the tester takes no PORT 2 and PORT
     * PWM is port 1's business; taps there set nothing. */
    ports(2u, 13u);
    choose_port(1);
    CHECK(outputs_screen_port_reason(why, sizeof(why)));
    CHECK_STR_EQ(why, TR(OUT_PORT_TESTER_PWM));
    tap(SEG_X(1), SEG_TESTER_Y);
    tap(SEG_X(0), SEG_PWM_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    ports_back();
}

int main(void)
{
    RUN(the_list_ends_in_one_prog_port_entry_per_port_reported);
    RUN(a_prog_port_entry_lights_its_pin_and_takes_no_tick);
    RUN(the_next_entry_opens_on_the_entry_asked_for_once);
    RUN(the_port_segments_set_the_tester_and_say_why_they_are_locked);
    return test_summary("outputs_screen_port");
}
