/*
 * The Guition JC3636K518C's input (shared/dial/dk518_input.c): the
 * encoder's two pulse lines turned into detents, and the still touch that
 * is its screen button.
 *
 * The failures this is about: a pulse's bounce that counts two detents; a
 * detent lost or turned the wrong way; a count at start that turns the
 * counter; a swipe that sends PRESS; a touch short of 1,000ms that sends
 * HOLD; a hold that never reaches the switch-off or the forget; a TAP that
 * lands at the wrong point; and an update's accept taken from a swipe,
 * run through bleota's own confirmation and the `ota` task's 10ms polls
 * with their 2-poll debounce (firmware/devota/devota.c).
 * The input task steps both every 5ms; so do these cases.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bleota.h"
#include "dk518_input.h"
#include "dialknob.h"

/** @brief The input task's period, ms. */
#define STEP 5u
/** @brief The face task's period: the question draws this long after
 *         the offer opens, ms. */
#define FACE_MS 50u

/** @brief What a run of steps gave. */
typedef struct {
    unsigned any;   /**< every DIAL_BTN_* bit seen */
    int      press; /**< DIAL_BTN_PRESS steps */
    int      hold;  /**< DIAL_BTN_HOLD steps */
    int      warn;  /**< DIAL_BTN_WARN steps */
    int      forget; /**< DIAL_BTN_FORGET steps */
    int      taps;  /**< TAPs */
    int      lifts; /**< still lifts */
    uint8_t  tap_x; /**< the last TAP's column */
    uint8_t  tap_y; /**< the last TAP's row */
} seen_t;

/** @brief The clock of the case, started near its wrap. */
static uint32_t s_now;

/** @brief Step the still touch every STEP ms for @p ms.
 *  @param s the state @param touch a finger is down @param x column
 *  @param y row @param yield an update has the screen @param ms how long
 *  @param out added to */
static void run(dk518_still_t *s, bool touch, uint16_t x, uint16_t y,
                bool yield, uint32_t ms, seen_t *out)
{
    for (uint32_t t = 0; t < ms; t += STEP) {
        dk518_still_out_t o;
        const unsigned a = dk518_still_step(s, touch, x, y, 0, yield, s_now,
                                            &o);
        out->any |= a;
        out->press += (a & DIAL_BTN_PRESS) != 0u;
        out->hold += (a & DIAL_BTN_HOLD) != 0u;
        out->warn += (a & DIAL_BTN_WARN) != 0u;
        out->forget += (a & DIAL_BTN_FORGET) != 0u;
        out->lifts += o.still_lift;
        if (o.tap) {
            ++out->taps;
            out->tap_x = o.tap_x;
            out->tap_y = o.tap_y;
        }
        s_now += STEP;
    }
}

/** @brief A fresh still touch, the clock near its wrap.
 *  @param s the state @param out cleared */
static void start(dk518_still_t *s, seen_t *out)
{
    dk518_still_init(s);
    memset(out, 0, sizeof *out);
    s_now = 0xFFFFF000u;
}

TEST_CASE(the_first_step_takes_the_totals_and_turns_nothing)
{
    dk518_enc_t e;
    dk518_enc_init(&e);
    CHECK_EQ(0, dk518_enc_step(&e, 517, -3));
    CHECK_EQ(0, dk518_enc_step(&e, 517, -3));
}

TEST_CASE(one_pulse_is_one_detent_each_way)
{
    dk518_enc_t e;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, 0, 0);
    CHECK_EQ(1, dk518_enc_step(&e, 1, 0));
    CHECK_EQ(0, dk518_enc_step(&e, 1, 0));
    CHECK_EQ(-1, dk518_enc_step(&e, 1, 1));
    CHECK_EQ(0, dk518_enc_step(&e, 1, 1));
}

TEST_CASE(a_pulse_and_its_bounce_count_once)
{
    /* Four edges in one step, then two more in the next: one pulse. */
    dk518_enc_t e;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, 10, 20);
    CHECK_EQ(1, dk518_enc_step(&e, 14, 20));
    CHECK_EQ(0, dk518_enc_step(&e, 16, 20));
    CHECK_EQ(0, dk518_enc_step(&e, 16, 20));
    CHECK_EQ(-1, dk518_enc_step(&e, 16, 23));
    CHECK_EQ(0, dk518_enc_step(&e, 16, 24));
}

TEST_CASE(pulses_10ms_apart_all_count)
{
    /* 20 clockwise detents 10ms apart: every one counts. */
    dk518_enc_t e;
    int32_t sum = 0;
    int32_t cw = 0;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, 0, 0);
    for (int i = 0; i < 20; ++i) {
        cw += 2;
        sum += dk518_enc_step(&e, cw, 0);
        sum += dk518_enc_step(&e, cw, 0);
    }
    CHECK_EQ(20, sum);
}

TEST_CASE(both_lines_in_one_step_cancel)
{
    dk518_enc_t e;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, 0, 0);
    CHECK_EQ(0, dk518_enc_step(&e, 1, 1));
}

TEST_CASE(a_total_that_wraps_still_counts)
{
    dk518_enc_t e;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, INT32_MAX, 0);
    CHECK_EQ(1, dk518_enc_step(&e, INT32_MIN, 0));
}

TEST_CASE(a_short_still_touch_is_press_and_tap_at_its_start)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 300, 90, false, 200, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.press);
    CHECK_EQ(0, v.hold);
    CHECK_EQ(1, v.taps);
    CHECK_EQ(1, v.lifts);
    CHECK_EQ(200, v.tap_x);
    CHECK_EQ(60, v.tap_y);
}

TEST_CASE(a_still_touch_past_500ms_is_press_alone)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, false, 800, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.press);
    CHECK_EQ(0, v.taps);
    CHECK_EQ(1, v.lifts);
}

TEST_CASE(a_drift_within_20px_stays_a_press)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 100, 100, false, 100, &v);
    run(&s, true, 120, 80, false, 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.press);
    CHECK_EQ(1, v.taps);
    CHECK_EQ(100 * 2 / 3, v.tap_x);
}

TEST_CASE(a_swipe_sends_nothing_and_the_next_touch_presses)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 100, 100, false, 100, &v);
    run(&s, true, 121, 100, false, 1500, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(0u, v.any);
    CHECK_EQ(0, v.taps);
    CHECK_EQ(0, v.lifts);
    /* The next touch is a press again. */
    run(&s, true, 50, 50, false, 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.press);
}

TEST_CASE(hold_comes_at_1000ms_and_a_move_after_it_keeps_it)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, false, DK_HOLD_MS - STEP, &v);
    CHECK_EQ(0, v.hold);
    run(&s, true, 180, 180, false, 2 * STEP, &v);
    CHECK_EQ(1, v.hold);
    run(&s, true, 250, 250, false, 500, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.hold);
    CHECK_EQ(0, v.press);
    CHECK_EQ(0, v.lifts);
}

TEST_CASE(the_5s_hold_warns_and_the_10s_hold_forgets)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, false, DIAL_OFF_MS + STEP, &v);
    CHECK_EQ(1, v.warn);
    CHECK(dial_button_local(&s.button));
    run(&s, true, 180, 180, false, DK_FORGET_MS - DIAL_OFF_MS, &v);
    CHECK_EQ(1, v.forget);
    CHECK(!(dial_button_local(&s.button)));
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(0, v.press);
}

TEST_CASE(a_lift_after_the_warning_opens_the_choice)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, false, DIAL_OFF_MS + 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(DIAL_OFF_ASK, s.button.stage);
    /* A tap in the choice takes KEEP ON, its focus. */
    run(&s, true, 90, 90, false, 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK((v.any & DIAL_BTN_KEEP) != 0u);
    CHECK_EQ(0, v.press);
}

TEST_CASE(under_an_update_offer_only_the_still_lift_counts)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, true, 200, &v);
    run(&s, false, 0, 0, true, 20, &v);
    CHECK_EQ(0u, v.any);
    CHECK_EQ(1, v.lifts);
    /* A swipe under the offer accepts nothing. */
    run(&s, true, 180, 180, true, 100, &v);
    run(&s, true, 180, 260, true, 100, &v);
    run(&s, false, 0, 0, true, 20, &v);
    CHECK_EQ(1, v.lifts);
    /* A touch from the offer stays nobody's after it. */
    run(&s, true, 180, 180, true, 100, &v);
    run(&s, true, 180, 180, false, 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(0, v.press);
}

/** @brief The update's offer under the still touch: the key as the
 *         `ota` task takes it, into bleota's own confirmation. */
typedef struct {
    dk518_still_t s;   /**< the still touch */
    dk518_key_t   k;   /**< the confirming key */
    dk518_glass_t g;   /**< the glass as the reads tell it */
    bleota_dev_t  d;   /**< bleota's device side */
    bool          raw; /**< the `ota` task's last key read */
    bool          key; /**< its debounced key */
    uint32_t      raw_offer; /**< the offer of the last read */
    uint32_t      key_offer; /**< the offer of the debounced key */
    uint32_t      t;   /**< the clock */
    int           cues; /**< accept cues */
    uint32_t      draw_at; /**< when the face draws the offer's question */
    bool          drawn;   /**< the face sent a question whole */
    uint32_t      drawn_id; /**< the offer whose question it sent */
} offer_t;

/** @brief No offer yet.  @param o the rig */
static void offer_init(offer_t *o)
{
    memset(o, 0, sizeof *o);
    dk518_still_init(&o->s);
    dk518_key_init(&o->k);
    dk518_glass_init(&o->g);
    o->d.cfg.confirm = BLEOTA_CONFIRM_PRESS;
    o->d.st = BLEOTA_DEV_IDLE;
    o->t = 0xFFFFF000u;
}

/** @brief The offer opens, as bleota's ask_confirm() opens it.
 *  @param o the rig */
static void offer_open(offer_t *o)
{
    o->draw_at = o->t + FACE_MS;
    ++o->d.offer;
    o->d.st = BLEOTA_DEV_CONFIRM;
    o->d.key_armed = !o->d.key_down;
    o->d.code = BLEOTA_OK;
}

/** @brief The input task's step in the firmware's order: the offer and
 *         its identity read together (devota_offer()), then the glass,
 *         then the still touch and the key; @p opens opens an offer
 *         after the glass was read, as a central can between the two.
 *  @param o the rig @param down a finger is down @param x its column
 *  @param y its row @param opens an offer opens after the read */
static void input_read_at(offer_t *o, dk518_read_t r, uint16_t x,
                          uint16_t y, bool opens)
{
    dk518_still_out_t e;
    uint32_t id = 0u;
    if (o->d.st == BLEOTA_DEV_CONFIRM && (int32_t)(o->t - o->draw_at) >= 0) {
        o->drawn = true;                    /* the face task's draw */
        o->drawn_id = o->d.offer;
    }
    const bool open = bleota_dev_offer(&o->d, &id);
    const bool shown = o->drawn && o->drawn_id == id;
    const bool yield = o->d.st != BLEOTA_DEV_IDLE;
    const bool empty = r == DK518_READ_UP;  /* the glass, read after */
    if (dk518_glass_read(&o->g, r, x, y)) {
        dk518_still_lost(&o->s, o->t);
    }
    if (opens) {
        offer_open(o);
    }
    (void)dk518_still_step(&o->s, o->g.down, o->g.x, o->g.y, 0, yield,
                           o->t, &e);
    o->cues += dk518_key_step(&o->k, open, id, shown, empty, &e, o->t);
}

/** @brief input_read_at() with a read that worked.
 *  @param o the rig @param down a finger is down @param x its column
 *  @param y its row @param opens an offer opens after the read */
static void input_step_at(offer_t *o, bool down, uint16_t x, uint16_t y,
                          bool opens)
{
    input_read_at(o, down ? DK518_READ_DOWN : DK518_READ_UP, x, y, opens);
}

/** @brief input_step_at() with no offer opening inside it.
 *  @param o the rig @param down a finger is down @param x its column
 *  @param y its row */
static void input_step(offer_t *o, bool down, uint16_t x, uint16_t y)
{
    input_step_at(o, down, x, y, false);
}

/** @brief The `ota` task's 10ms poll: the key with the offer it belongs
 *         to, debounced over 2 polls of the same level and offer, into
 *         bleota_dev_key_for().
 *  @param o the rig */
static void ota_poll(offer_t *o)
{
    const bool now_raw = dk518_key_down(&o->k, o->t);
    const uint32_t offer = dk518_key_offer(&o->k);
    if (now_raw == o->raw && offer == o->raw_offer) {
        o->key = o->raw;
        o->key_offer = offer;
    }
    o->raw = now_raw;
    o->raw_offer = offer;
    bleota_dev_key_for(&o->d, o->key, o->key_offer, o->t);
}

/**
 * @brief Run the rig every STEP ms for @p ms, every read giving @p r.
 * @param o  the rig
 * @param r  each read's result
 * @param x  the finger's column, for DK518_READ_DOWN
 * @param y  its row
 * @param ms how long
 */
static void offer_read(offer_t *o, dk518_read_t r, uint16_t x, uint16_t y,
                       uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i += STEP) {
        input_read_at(o, r, x, y, false);
        if ((o->t / STEP) % 2u == 0u) {
            ota_poll(o);
        }
        o->t += STEP;
    }
}

/**
 * @brief Run the rig every STEP ms for @p ms with the finger as given.
 * @param o    the rig
 * @param down a finger is on the glass
 * @param x    its column
 * @param y    its row
 * @param ms   how long
 */
static void offer_run(offer_t *o, bool down, uint16_t x, uint16_t y,
                      uint32_t ms)
{
    offer_read(o, down ? DK518_READ_DOWN : DK518_READ_UP, x, y, ms);
}

/** @brief The glass up 60ms, a still tap of 100ms, and 200ms up after
 *         it.  @param o the rig */
static void tap(offer_t *o)
{
    offer_run(o, false, 0, 0, 60);
    offer_run(o, true, 180, 180, 100);
    offer_run(o, false, 0, 0, 200);
}

TEST_CASE(a_still_tap_accepts_the_update)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);
    tap(&o);
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
    CHECK_EQ(o.cues, 1);
}

TEST_CASE(a_swipe_neither_accepts_nor_refuses_the_update)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 100);
    offer_run(&o, true, 260, 180, 200);
    offer_run(&o, false, 0, 0, 3000);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    offer_run(&o, true, 180, 180, 600);   /* moved before 1,000ms */
    offer_run(&o, true, 180, 40, 900);
    offer_run(&o, false, 0, 0, 3000);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    CHECK_EQ(o.cues, 0);
}

TEST_CASE(a_still_hold_refuses_the_update_whenever_it_lifts)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 1200);  /* lifted before 2,000ms */
    offer_run(&o, false, 0, 0, 2000);
    CHECK_EQ(o.d.st, BLEOTA_DEV_IDLE);
    CHECK_EQ(o.d.code, BLEOTA_E_REFUSED);
    offer_init(&o);
    offer_open(&o);
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 1100);  /* moved after 1,000ms */
    offer_run(&o, true, 250, 250, 1400);
    offer_run(&o, false, 0, 0, 500);
    CHECK_EQ(o.d.code, BLEOTA_E_REFUSED);
}

TEST_CASE(a_tap_inside_a_holds_window_neither_shortens_it_nor_accepts)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 1200);
    offer_run(&o, false, 0, 0, 100);      /* the hold's window runs on */
    offer_run(&o, true, 180, 180, 100);   /* a tap at 1,300 to 1,400ms */
    offer_run(&o, false, 0, 0, 2000);
    CHECK_EQ(o.d.st, BLEOTA_DEV_IDLE);
    CHECK_EQ(o.d.code, BLEOTA_E_REFUSED);
    CHECK_EQ(o.cues, 0);
}

TEST_CASE(two_quick_taps_accept_once_and_never_make_a_hold)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);
    for (int i = 0; i < 20; ++i) {        /* a tap every 60ms for 1.2s */
        offer_run(&o, true, 180, 180, 40);
        offer_run(&o, false, 0, 0, 60);
    }
    offer_run(&o, false, 0, 0, 500);
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(a_touch_from_before_the_offer_answers_nothing)
{
    offer_t o;
    offer_init(&o);
    offer_run(&o, true, 180, 180, 200);   /* down at 0ms */
    offer_open(&o);                       /* the offer at 200ms */
    offer_run(&o, true, 180, 180, 100);
    offer_run(&o, false, 0, 0, 500);      /* lifted at 300ms */
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    offer_run(&o, true, 180, 180, 300);   /* held across the opening */
    offer_init(&o);
    offer_run(&o, true, 180, 180, 500);
    offer_open(&o);
    offer_run(&o, true, 180, 180, 2500);
    offer_run(&o, false, 0, 0, 500);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    tap(&o);                              /* a fresh touch answers */
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
    CHECK_EQ(o.cues, 1);
}

TEST_CASE(a_touch_across_a_lost_link_answers_no_later_offer)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 300);
    bleota_dev_lost(&o.d);                /* the link drops */
    offer_run(&o, true, 180, 180, 100);
    offer_open(&o);                       /* a new offer, same finger */
    offer_run(&o, true, 180, 180, 100);
    offer_run(&o, false, 0, 0, 500);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    tap(&o);
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(a_holds_window_ends_with_its_offer_and_the_next_offer_starts_up)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 1500);  /* the key down since 1,000ms */
    CHECK(dk518_key_down(&o.k, o.t));
    bleota_dev_lost(&o.d);                /* lost before the refusal */
    offer_run(&o, true, 180, 180, 50);
    CHECK(!dk518_key_down(&o.k, o.t));
    offer_run(&o, false, 0, 0, 100);
    offer_open(&o);                       /* the window would still run */
    CHECK(o.d.key_armed);
    offer_run(&o, false, 0, 0, 100);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    tap(&o);
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(an_offer_replaced_between_two_steps_gets_no_answer_from_the_touch)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);                       /* offer A */
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 100);
    bleota_dev_lost(&o.d);                /* A ends and B opens before */
    offer_open(&o);                       /* the input task's next step */
    offer_run(&o, false, 0, 0, 500);      /* the lift */
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    CHECK_EQ(o.cues, 0);
    tap(&o);
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(a_key_of_an_ended_offer_answers_no_later_one_in_the_ota_task)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);                       /* offer A */
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 100);
    input_step(&o, false, 0, 0);          /* the lift: A's 50ms window */
    CHECK(dk518_key_down(&o.k, o.t));
    bleota_dev_lost(&o.d);
    offer_open(&o);                       /* B, before the next step */
    for (int i = 0; i < 3; ++i) {         /* the `ota` task polls first */
        ota_poll(&o);
        o.t += 10u;
    }
    offer_run(&o, false, 0, 0, 500);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    tap(&o);
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(an_offer_that_opens_while_the_glass_is_read_gets_no_answer)
{
    offer_t o;
    offer_init(&o);
    offer_run(&o, false, 0, 0, 100);      /* the glass empty, no offer */
    /* The read finds the finger, and the offer opens right after it,
     * before the key's step. */
    input_step_at(&o, true, 180, 180, true);
    o.t += STEP;
    offer_run(&o, true, 180, 180, 100);
    offer_run(&o, false, 0, 0, 500);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    CHECK_EQ(o.cues, 0);
    tap(&o);
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(a_finger_first_read_with_the_offer_needs_an_empty_read_before)
{
    offer_t o;
    offer_init(&o);
    offer_run(&o, false, 0, 0, 100);      /* read empty, no offer */
    offer_open(&o);                       /* the offer, then the finger */
    offer_run(&o, true, 180, 180, 100);   /* first read with it: a finger */
    offer_run(&o, false, 0, 0, 500);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    tap(&o);                              /* after an empty read: taken */
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(a_tap_before_the_question_is_drawn_answers_nothing)
{
    offer_t o;
    offer_init(&o);
    offer_run(&o, false, 0, 0, 100);
    offer_open(&o);                       /* the question draws at 50ms */
    offer_run(&o, false, 0, 0, 30);       /* empty reads, nothing drawn */
    offer_run(&o, true, 180, 180, 100);   /* aimed at the old screen */
    offer_run(&o, false, 0, 0, 500);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    CHECK_EQ(o.cues, 0);
    tap(&o);                              /* after the question: taken */
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(a_question_drawn_for_another_offer_arms_nothing)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);                       /* A, drawn at 50ms */
    offer_run(&o, false, 0, 0, 60);
    bleota_dev_lost(&o.d);
    offer_open(&o);                       /* B, its question not yet out */
    offer_run(&o, false, 0, 0, 20);
    offer_run(&o, true, 180, 180, 100);
    offer_run(&o, false, 0, 0, 500);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    tap(&o);
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(bleota_takes_a_key_for_the_offer_standing_alone)
{
    bleota_dev_t d;
    uint32_t id = 0u;
    memset(&d, 0, sizeof d);
    d.cfg.confirm = BLEOTA_CONFIRM_PRESS;
    CHECK(!bleota_dev_offer(&d, &id));
    d.st = BLEOTA_DEV_CONFIRM;
    d.offer = 7u;
    d.key_armed = true;
    CHECK(bleota_dev_offer(&d, &id));
    CHECK_EQ(id, 7u);
    bleota_dev_key_for(&d, true, 6u, 100u);
    bleota_dev_key_for(&d, false, 6u, 150u);
    CHECK_EQ(d.st, BLEOTA_DEV_CONFIRM);
    bleota_dev_key_for(&d, true, 7u, 200u);
    bleota_dev_key_for(&d, false, 7u, 250u);
    CHECK_EQ(d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(no_offer_keeps_the_key_up)
{
    offer_t o;
    offer_init(&o);
    for (int i = 0; i < 3; ++i) {
        offer_run(&o, true, 180, 180, 100);
        CHECK(!dk518_key_down(&o.k, o.t));
        offer_run(&o, false, 0, 0, 30);
        CHECK(!dk518_key_down(&o.k, o.t));
    }
    offer_run(&o, true, 180, 180, 1500);
    CHECK(!dk518_key_down(&o.k, o.t));
    CHECK_EQ(o.cues, 0);
}

TEST_CASE(native_points_scale_to_0_to_239)
{
    CHECK_EQ(0, dk518_touch_scale(0));
    CHECK_EQ(120, dk518_touch_scale(180));
    CHECK_EQ(239, dk518_touch_scale(359));
    CHECK_EQ(239, dk518_touch_scale(4095));
    CHECK(dk518_touch_scale(359) <= DK_TOUCH_MAX);
}

/* A failed read of the touch controller is no lift: a finger held still
 * through 4 failed reads in a row is still down, and its later hold
 * refuses as a hold does.  The 5th failure in a row cancels the touch:
 * neither the accept nor the refusal, and the next tap still accepts. */
TEST_CASE(a_failed_read_keeps_the_finger_and_five_cancel_it)
{
    offer_t o;
    offer_init(&o);
    offer_open(&o);
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 200);
    offer_read(&o, DK518_READ_FAIL, 0, 0, (DK518_READ_FAILS - 1u) * STEP);
    offer_run(&o, false, 0, 0, 200);       /* now it lifts: the accept */
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
    CHECK_EQ(o.cues, 1);

    offer_init(&o);
    offer_open(&o);
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 200);
    offer_read(&o, DK518_READ_FAIL, 0, 0, (DK518_READ_FAILS - 1u) * STEP);
    offer_run(&o, true, 180, 180, 1200);   /* still down: a hold */
    offer_run(&o, false, 0, 0, 2000);
    CHECK_EQ(o.d.code, BLEOTA_E_REFUSED);
    CHECK_EQ(o.cues, 0);

    offer_init(&o);
    offer_open(&o);
    offer_run(&o, false, 0, 0, 60);
    offer_run(&o, true, 180, 180, 200);
    offer_read(&o, DK518_READ_FAIL, 0, 0, DK518_READ_FAILS * STEP);
    offer_run(&o, true, 180, 180, 100);    /* it reads again: no new touch */
    offer_run(&o, false, 0, 0, 3000);
    CHECK_EQ(o.d.st, BLEOTA_DEV_CONFIRM);
    CHECK_EQ(o.cues, 0);
    tap(&o);
    CHECK_EQ(o.d.st, BLEOTA_DEV_OPEN);
    CHECK_EQ(o.cues, 1);
}

/* The glass's own rule: a failure keeps the last state and its point,
 * a good read resets the count, the 5th failure in a row ends a finger
 * once and an empty glass never, and the lost finger read again starts
 * no touch before the glass reads empty. */
TEST_CASE(the_glass_counts_failed_reads_in_a_row)
{
    dk518_glass_t g;
    dk518_glass_init(&g);
    CHECK(!dk518_glass_read(&g, DK518_READ_DOWN, 100, 200));
    for (unsigned i = 1u; i < DK518_READ_FAILS; ++i) {
        CHECK(!dk518_glass_read(&g, DK518_READ_FAIL, 0, 0));
        CHECK(g.down);
        CHECK_EQ(g.x, 100u);
        CHECK_EQ(g.y, 200u);
    }
    CHECK(!dk518_glass_read(&g, DK518_READ_DOWN, 101, 201));
    CHECK_EQ(g.fails, 0u);
    for (unsigned i = 1u; i < DK518_READ_FAILS; ++i) {
        CHECK(!dk518_glass_read(&g, DK518_READ_FAIL, 0, 0));
    }
    CHECK(dk518_glass_read(&g, DK518_READ_FAIL, 0, 0));
    CHECK(!g.down);
    CHECK(!dk518_glass_read(&g, DK518_READ_FAIL, 0, 0));
    CHECK(!g.down);
    CHECK(!dk518_glass_read(&g, DK518_READ_DOWN, 100, 200));
    CHECK(!g.down);                       /* the lost touch, read again */
    CHECK(!dk518_glass_read(&g, DK518_READ_UP, 0, 0));
    CHECK(!dk518_glass_read(&g, DK518_READ_DOWN, 100, 200));
    CHECK(g.down);                        /* a new touch */
    CHECK(!dk518_glass_read(&g, DK518_READ_UP, 0, 0));
    for (unsigned i = 0u; i < 2u * DK518_READ_FAILS; ++i) {
        CHECK(!dk518_glass_read(&g, DK518_READ_FAIL, 0, 0));
        CHECK(!g.down);
    }
}

int main(void)
{
    RUN(the_first_step_takes_the_totals_and_turns_nothing);
    RUN(one_pulse_is_one_detent_each_way);
    RUN(a_pulse_and_its_bounce_count_once);
    RUN(pulses_10ms_apart_all_count);
    RUN(both_lines_in_one_step_cancel);
    RUN(a_total_that_wraps_still_counts);
    RUN(a_short_still_touch_is_press_and_tap_at_its_start);
    RUN(a_still_touch_past_500ms_is_press_alone);
    RUN(a_drift_within_20px_stays_a_press);
    RUN(a_swipe_sends_nothing_and_the_next_touch_presses);
    RUN(hold_comes_at_1000ms_and_a_move_after_it_keeps_it);
    RUN(the_5s_hold_warns_and_the_10s_hold_forgets);
    RUN(a_lift_after_the_warning_opens_the_choice);
    RUN(under_an_update_offer_only_the_still_lift_counts);
    RUN(a_still_tap_accepts_the_update);
    RUN(a_swipe_neither_accepts_nor_refuses_the_update);
    RUN(a_still_hold_refuses_the_update_whenever_it_lifts);
    RUN(a_tap_inside_a_holds_window_neither_shortens_it_nor_accepts);
    RUN(two_quick_taps_accept_once_and_never_make_a_hold);
    RUN(a_touch_from_before_the_offer_answers_nothing);
    RUN(a_touch_across_a_lost_link_answers_no_later_offer);
    RUN(a_holds_window_ends_with_its_offer_and_the_next_offer_starts_up);
    RUN(an_offer_replaced_between_two_steps_gets_no_answer_from_the_touch);
    RUN(a_key_of_an_ended_offer_answers_no_later_one_in_the_ota_task);
    RUN(an_offer_that_opens_while_the_glass_is_read_gets_no_answer);
    RUN(a_finger_first_read_with_the_offer_needs_an_empty_read_before);
    RUN(a_tap_before_the_question_is_drawn_answers_nothing);
    RUN(a_question_drawn_for_another_offer_arms_nothing);
    RUN(bleota_takes_a_key_for_the_offer_standing_alone);
    RUN(no_offer_keeps_the_key_up);
    RUN(native_points_scale_to_0_to_239);
    RUN(a_failed_read_keeps_the_finger_and_five_cancel_it);
    RUN(the_glass_counts_failed_reads_in_a_row);
    return test_summary("dk518_input");
}
