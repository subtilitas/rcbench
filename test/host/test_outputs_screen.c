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

#include "link_pages.h"
#include "outputs_pages.h"
#include "outputs_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

/* Geometry the screen draws to; a test that hard-codes it is a test that
 * notices when the layout moves under the hit testing. */
#define COL_X  16
#define DD_X   16
#define DD_Y   44
#define DD_W   266
#define DD_H   54
#define POP_ROW 46
#define GRID_X 288
#define GRID_Y 16
#define CELL_GAP 4
#define CELL_W ((496 - 3 * CELL_GAP) / 4)
#define CELL_H 53
#define GRID_ROWS 7

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
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 } };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)x, (int16_t)y, 40 } };
    scr()->event(&d);
    scr()->event(&u);
}

static void press_at(int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 } };
    scr()->event(&d);
}

static void release_at(int x, int y)
{
    touch_event_t u = { TOUCH_EVENT_UP, { 0, (int16_t)x, (int16_t)y, 40 } };
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
        char label[24];
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

static void pose_three_protocols(void)
{
    fresh();
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
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

TEST_CASE(every_state_renders_without_reading_off_the_canvas)
{
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

    fresh();
    for (int p = 0; p < (int)OUTBIND_PROTOS; ++p) {
        choose_proto(p);
        for (uint8_t g = 0; g < 29u; ++g) {
            if (outbind_index_of(OUTBIND_BOARD_PICO_HEADER, g) < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)) {
                tap_pin(g);
            }
        }
        scr()->render(&c, 0);
        /* And with the list open over the top of whatever was chosen. */
        tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);
        scr()->render(&c, 1);
        tap(700, 400);
    }
    for (int r = 0; r <= (int)OUTPUTS_REFUSED; ++r) {
        outputs_screen_set_result((outputs_result_t)r);
        scr()->render(&c, 2);
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

    /* What was loaded from storage is what the screen now shows and edits. */
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

/* SERVO PWM selected: its two pins are ticked and show their pad. */
#define TRACE_SERVO                                                           \
    "GP0 held 'DSHOT600 BIDIR'\n"                                             \
    "GP1 held 'MOTOR PWM'\n"                                                  \
    "GP2 ticked 'PAD 4'\n"                                                    \
    TRACE_GP3_TO_GP12                                                         \
    "GP13 ticked 'PAD 17'\n"                                                  \
    TRACE_GP14_TO_GP28

/*
 * The protocol selected on entry is the lowest-numbered one that holds a
 * pin: SERVO PWM on this bench.  Its own pins show their pad, the others'
 * show their protocol.
 */
TEST_CASE(the_screen_opens_on_the_lowest_protocol_that_holds_a_pin)
{
    pose_three_protocols();
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("SERVO PWM"));
    CHECK_EQ(outputs_screen_cell(idx(0), NULL, 0), OUTPUTS_CELL_HELD);
    CHECK_EQ(outputs_screen_cell(idx(2), NULL, 0), OUTPUTS_CELL_TICKED);
    char l[24];
    (void)outputs_screen_cell(idx(0), l, sizeof(l));
    CHECK_STR_EQ(l, "DSHOT600 BIDIR");
    (void)outputs_screen_cell(idx(1), l, sizeof(l));
    CHECK_STR_EQ(l, "MOTOR PWM");
    /* A pin ticked in the selected protocol shows "PAD n". */
    (void)outputs_screen_cell(idx(2), l, sizeof(l));
    CHECK_STR_EQ(l, "PAD 4");
    (void)outputs_screen_cell(idx(13), l, sizeof(l));
    CHECK_STR_EQ(l, "PAD 17");
    CHECK_STR_EQ(reason(), "");

    /* With nothing bound it opens on OFF, as a page with no pin names. */
    fresh();
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
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
    CHECK_STR_EQ(reason(), "OFF SHOWS ALL, EDITS NOTHING");

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
    CHECK_STR_EQ(reason(), "OFF SHOWS ALL, EDITS NOTHING");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "OFF ZEIGT ALLES, ÄNDERT NICHTS");
    ui_text_set_language(UI_LANG_EN);
}

/*
 * OFF is a pick like any other.  It stays across a reading of the same
 * pages, a second one, a repaint, a link that goes and comes back, and a
 * reading of other pins, until another entry is picked or the screen is
 * left.
 */
TEST_CASE(off_stays_until_another_entry_is_picked_or_the_screen_is_left)
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

    /* OFF again, and the screen is left: the next entry opens on the
     * lowest-numbered protocol holding a pin, as it does with no pick. */
    choose_named("OFF");
    scr()->leave();
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("SERVO PWM"));
    scr()->enter();
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("SERVO PWM"));

    /* A picked protocol other than OFF outlasts the visit, as before: the
     * pin picker joins its taps to it. */
    choose_named("PPM");
    scr()->leave();
    scr()->enter();
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));

    /* OFF never survives a change of board, or a protocol named from
     * outside. */
    choose_named("OFF");
    outbind_t named = r.bind;
    outbind_set_proto(&named, (uint8_t)proto_row("PPM"));
    outputs_screen_set_binding(&named);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));
    choose_named("OFF");
    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
    outputs_screen_set_binding(&other);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("SERVO PWM"));
}

/* OFF with nothing bound is where a bench starts: a reading does not move
 * it, and the first pick is what makes a pin tickable. */
TEST_CASE(off_with_nothing_bound_is_left_by_a_pick)
{
    fresh();
    CHECK_STR_EQ(reason(), "OFF SHOWS ALL, EDITS NOTHING");
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
    CHECK_STR_EQ(reason(), "BINDING NOT READ - NO EDITS");
    scr()->render(&c, 0);
    CHECK(reason_ink(&c) != quiet);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "BINDUNG NICHT GELESEN - GESPERRT");
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
    CHECK_STR_EQ(reason(), "BINDING NOT READ - NO EDITS");
    choose_named("SERVO PWM");

    /* Pages no binding describes: the same, with its own reason. */
    bind_reading_t odd = ok;
    odd.state = BIND_READ_ODD;
    outbind_init(&odd.bind);
    outbind_set_board(&odd.bind, OUTBIND_BOARD_PICO_HEADER);
    outputs_screen_set_reading(&odd);
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_ODD);
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    CHECK_STR_EQ(reason(), "PAGES HOLD NO VALID BINDING");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "PAGES OHNE GÜLTIGE BINDUNG");
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
    CHECK_STR_EQ(trace(), TRACE_SERVO);
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

int main(void)
{
    RUN(a_pick_calls_the_apply_seam_for_no_entry);
    RUN(the_screen_opens_on_the_lowest_protocol_that_holds_a_pin);
    RUN(off_shows_the_protocol_of_every_bound_pin);
    RUN(a_tap_on_a_pin_does_nothing_while_off_is_selected);
    RUN(off_stays_until_another_entry_is_picked_or_the_screen_is_left);
    RUN(off_with_nothing_bound_is_left_by_a_pick);
    RUN(a_binding_that_did_not_read_is_kept_marked_and_not_edited);
    RUN(a_failed_read_for_another_board_keeps_no_binding);
    RUN(the_longest_protocol_name_fits_the_list_and_a_cell);
    RUN(an_empty_read_back_does_not_clear_the_chosen_protocol);
    RUN(a_protocol_named_from_outside_lands_over_the_one_on_screen);
    RUN(a_second_protocol_survives_the_read_back);
    RUN(a_change_of_board_takes_the_protocol_with_it);
    RUN(the_protocol_list_opens_and_a_choice_closes_it);
    RUN(an_open_list_can_be_left_without_choosing);
    RUN(a_release_away_from_the_press_does_nothing);
    RUN(ticking_a_pin_applies_once_and_unticking_applies_again);
    RUN(a_reserved_pin_never_reaches_the_apply_seam);
    RUN(a_pin_too_many_does_not_apply);
    RUN(nothing_can_be_ticked_while_the_protocol_is_off);
    RUN(every_state_renders_without_reading_off_the_canvas);
    RUN(a_board_that_can_take_nothing_says_why);
    RUN(a_protocol_that_has_all_its_pins_says_so);
    RUN(the_binding_survives_being_set_from_outside);
    RUN(a_protocol_index_from_outside_cannot_run_off_the_table);
    RUN(null_events_are_refused_rather_than_dereferenced);
    RUN(a_cancelled_press_applies_nothing);
    return test_summary("outputs_screen");
}
