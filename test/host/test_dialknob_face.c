/*
 * The Dial's model of its face (protocols/dialknob, dk_face_t).
 *
 * The failures this is about: a face that shows the value of the screen
 * before after the panel moved on, a refused or repeated packet that
 * changes what the face shows, and a face that does not say LINK LOST when
 * the panel's stream stops.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "dialknob.h"

/** Feed a SCREEN. */
static bool screen(dk_face_t *f, uint8_t seq, uint8_t face, const char *title,
                   uint32_t ms)
{
    dk_screen_t s = { seq, face, DK_LANG_EN, DK_SCR_SOUND, "" };
    uint8_t b[DK_LEN_SCREEN];
    dk_text_copy(s.title, sizeof s.title, title);
    CHECK_EQ(dk_screen_encode(&s, b), DK_LEN_SCREEN);
    return dk_face_feed(f, b, sizeof b, ms);
}

/** Feed a VALUE. */
static bool value(dk_face_t *f, uint8_t seq, int32_t v, uint32_t ms)
{
    dk_value_t x = { seq, v, 0, DK_VAL_SET };
    uint8_t b[DK_LEN_VALUE];
    CHECK_EQ(dk_value_encode(&x, b), DK_LEN_VALUE);
    return dk_face_feed(f, b, sizeof b, ms);
}

/** Feed a NAV row. */
static bool nav(dk_face_t *f, uint8_t seq, uint8_t index, uint8_t count,
                uint8_t slot, const char *item)
{
    dk_nav_t n = { seq, index, count, slot, "" };
    uint8_t b[DK_LEN_NAV];
    dk_text_copy(n.item, sizeof n.item, item);
    CHECK_EQ(dk_nav_encode(&n, b), DK_LEN_NAV);
    return dk_face_feed(f, b, sizeof b, 0);
}

TEST_CASE(a_new_screen_empties_the_value_of_the_screen_before)
{
    dk_face_t f;
    dk_value_meta_t m = { 3, "%", 1, 0, 1000, 5 };
    uint8_t b[DK_LEN_VALUE_META];

    dk_face_init(&f);
    CHECK(screen(&f, 1, DK_FACE_VALUE, "DIM AFTER", 0));
    CHECK_EQ(dk_value_meta_encode(&m, b), DK_LEN_VALUE_META);
    CHECK(dk_face_feed(&f, b, sizeof b, 5));
    CHECK(value(&f, 4, 300, 10));
    CHECK(f.have_meta && f.have_value);
    CHECK_EQ(f.value.value, 300);
    CHECK_EQ(f.changed, DK_CH_SCREEN | DK_CH_NAV | DK_CH_META | DK_CH_VALUE);

    /* The same screen again (the 5s repeat) keeps them. */
    f.changed = 0;
    CHECK(screen(&f, 5, DK_FACE_VALUE, "DIM AFTER", 20));
    CHECK(f.have_meta && f.have_value);
    CHECK_EQ(f.changed, DK_CH_SCREEN);
    /* Another title empties them; so does another face. */
    CHECK(screen(&f, 6, DK_FACE_VALUE, "SLEEP AFTER", 30));
    CHECK(!f.have_meta && !f.have_value);
    CHECK(value(&f, 7, 1, 40));
    CHECK(screen(&f, 8, DK_FACE_OTHER, "SLEEP AFTER", 50));
    CHECK(!f.have_value);
    CHECK_STR_EQ(f.screen.title, "SLEEP AFTER");
    CHECK_EQ(f.screen.face, DK_FACE_OTHER);
}

TEST_CASE(nav_rows_of_another_position_replace_the_rows_before)
{
    dk_face_t f;

    dk_face_init(&f);
    CHECK(screen(&f, 1, DK_FACE_NAV, "SETUP", 0));
    CHECK(nav(&f, 2, 3, 9, DK_SLOT_PREV, "SERVO"));
    CHECK(nav(&f, 3, 3, 9, DK_SLOT_FOCUS, "PANEL"));
    CHECK(nav(&f, 4, 3, 9, DK_SLOT_NEXT, "WIRELESS"));
    CHECK_EQ(f.have_nav, 0x07);
    CHECK_STR_EQ(f.nav[DK_SLOT_FOCUS].item, "PANEL");
    /* The next move's first row empties the other two. */
    CHECK(nav(&f, 5, 4, 9, DK_SLOT_FOCUS, "WIRELESS"));
    CHECK_EQ(f.have_nav, 1u << DK_SLOT_FOCUS);
    CHECK(nav(&f, 6, 4, 9, DK_SLOT_PREV, "PANEL"));
    CHECK_EQ(f.have_nav, 0x03);
    /* A list that grows (count changes) is another position too. */
    CHECK(nav(&f, 7, 4, 10, DK_SLOT_NEXT, "IMU"));
    CHECK_EQ(f.have_nav, 1u << DK_SLOT_NEXT);
}

TEST_CASE(a_refused_or_repeated_packet_changes_nothing)
{
    dk_face_t f;
    uint8_t b[DK_LEN_VALUE];
    dk_value_t v = { 9, 42, 0, 0 };

    dk_face_init(&f);
    CHECK(value(&f, 8, 10, 0));
    CHECK_EQ(dk_value_encode(&v, b), DK_LEN_VALUE);
    b[10] = 0x80;                               /* a reserved flag */
    CHECK(!dk_face_feed(&f, b, sizeof b, 10));
    CHECK_EQ(f.bad, 1u);
    CHECK_EQ(f.value.value, 10);
    CHECK(!dk_face_feed(&f, b, sizeof b - 1u, 10));     /* short */
    CHECK_EQ(f.bad, 2u);
    CHECK(!dk_face_feed(&f, NULL, 0, 10));
    CHECK_EQ(f.bad, 3u);

    CHECK(!value(&f, 8, 99, 20));               /* seq 8 again */
    CHECK_EQ(f.repeats, 1u);
    CHECK_EQ(f.value.value, 10);
    CHECK_EQ(f.last_ms, 0u);                    /* a repeat is no packet */
    CHECK(value(&f, 11, 99, 30));               /* 9 and 10 skipped */
    CHECK_EQ(f.skipped, 2u);
    CHECK_EQ(f.packets, 2u);
    CHECK(!dk_face_feed(NULL, b, sizeof b, 0));
}

TEST_CASE(the_face_says_link_lost_500ms_after_the_last_panel_packet)
{
    dk_face_t f;
    dk_state_t s = { 1, 123456u, 10, 10, DK_STA_ARMED };
    uint8_t b[DK_LEN_STATE];

    dk_face_init(&f);
    CHECK(dk_face_lost(&f, 0));                 /* nothing came yet */
    CHECK_EQ(dk_state_encode(&s, b), DK_LEN_STATE);
    CHECK(dk_face_feed(&f, b, sizeof b, 0xFFFFFF80u));
    CHECK(f.have_state);
    CHECK_EQ(f.state.panel_ms, 123456u);
    CHECK(!dk_face_lost(&f, 0xFFFFFF80u + DK_DIAL_LOST_MS - 1u));
    CHECK(dk_face_lost(&f, 0xFFFFFF80u + DK_DIAL_LOST_MS));  /* wraps */
    CHECK(dk_face_lost(NULL, 0));
}

TEST_CASE(a_new_link_empties_the_model_and_learns_the_seq_again)
{
    dk_face_t f;
    dk_live_motor_t m = { 40, DK_LIVE_ARMED, DK_MV_RPM, 1000, 9000, 0, 0, 0,
                          0, 0, 5 };
    dk_live_servo_t sv = { 41, 0, 1500, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t b[DK_PKT_MAX];

    dk_face_init(&f);
    CHECK(screen(&f, 39, DK_FACE_MOTOR, "MOTOR & ESC", 0));
    CHECK_EQ(dk_live_motor_encode(&m, b), DK_LEN_LIVE_MOTOR);
    CHECK(dk_face_feed(&f, b, DK_LEN_LIVE_MOTOR, 0));
    CHECK(f.have_motor);
    CHECK_EQ(f.motor.rpm, 9000);
    CHECK_EQ(dk_live_servo_encode(&sv, b), DK_LEN_LIVE_SERVO);
    CHECK(dk_face_feed(&f, b, DK_LEN_LIVE_SERVO, 0));
    CHECK(f.have_servo);
    CHECK_EQ(f.changed & (DK_CH_MOTOR | DK_CH_SERVO),
             DK_CH_MOTOR | DK_CH_SERVO);

    dk_face_link_reset(&f);
    CHECK(!f.have_screen && !f.have_motor && !f.have_servo);
    CHECK(dk_face_lost(&f, 0));
    CHECK_EQ(f.changed, 0xFF);                  /* draw it all again */
    /* The panel restarted: its seq 41 again is taken, not a repeat. */
    CHECK(dk_face_feed(&f, b, DK_LEN_LIVE_SERVO, 100));
    CHECK_EQ(f.repeats, 0u);
    CHECK_EQ(f.packets, 4u);                    /* counts are kept */
    dk_face_init(NULL);
    dk_face_link_reset(NULL);
}

TEST_CASE(the_dial_ends_the_link_when_link_lost_has_held_for_500ms)
{
    dk_face_t f;
    dk_state_t st = { 1, 5u, 10, 10, 0 };
    uint8_t b[DK_LEN_STATE];

    dk_face_init(&f);
    /* No panel packet on this connection yet: the panel may still be
     * discovering the service, and the link is not ended. */
    CHECK(!dk_face_drop(&f, 0));
    CHECK(!dk_face_drop(&f, 600000));
    CHECK_EQ(dk_state_encode(&st, b), DK_LEN_STATE);
    CHECK(dk_face_feed(&f, b, sizeof b, 0xFFFFFE00u));
    CHECK(!dk_face_drop(&f, 0xFFFFFE00u + DK_DIAL_LOST_MS));
    CHECK(dk_face_lost(&f, 0xFFFFFE00u + DK_DIAL_LOST_MS));
    CHECK(!dk_face_drop(&f, 0xFFFFFE00u + DK_DIAL_DROP_MS - 1u));
    CHECK(dk_face_drop(&f, 0xFFFFFE00u + DK_DIAL_DROP_MS));   /* wraps */
    CHECK_EQ(DK_DIAL_DROP_MS, DK_DIAL_LOST_MS + 500u);
    /* A new connection starts the rule again. */
    dk_face_link_reset(&f);
    CHECK(!dk_face_drop(&f, 0xFFFFFE00u + 10u * DK_DIAL_DROP_MS));
    CHECK(!dk_face_drop(NULL, 0));
}

/** Follow a run of counts from detent 0; return the detent at the end and
 *  the moves taken in @p moves. */
static int32_t follow(const int32_t *counts, unsigned n, unsigned *moves)
{
    int32_t d = 0;
    *moves = 0;
    for (unsigned i = 0; i < n; ++i) {
        const int32_t next = dk_detent_follow(d, counts[i]);
        if (next != d) {
            ++*moves;
        }
        d = next;
    }
    return d;
}

TEST_CASE(a_detent_moves_3_counts_from_its_center_and_jitter_never_clicks)
{
    unsigned moves;

    CHECK_EQ(dk_detent_follow(0, 2), 0);
    CHECK_EQ(dk_detent_follow(0, 3), 1);
    CHECK_EQ(dk_detent_follow(0, -2), 0);
    CHECK_EQ(dk_detent_follow(0, -3), -1);
    CHECK_EQ(dk_detent_follow(1, 6), 1);        /* center 4: 2 counts */
    CHECK_EQ(dk_detent_follow(1, 7), 2);
    CHECK_EQ(dk_detent_follow(1, 2), 1);
    CHECK_EQ(dk_detent_follow(1, 1), 0);
    CHECK_EQ(dk_detent_follow(-1, -2), -1);     /* center -4: 2 counts */
    CHECK_EQ(dk_detent_follow(-1, -1), 0);
    CHECK_EQ(dk_detent_follow(-1, -7), -2);
    CHECK_EQ(dk_detent_follow(-1, -6), -1);
    CHECK_EQ(dk_detent_follow(0, 64), 16);      /* one turn at once */
    CHECK_EQ(dk_detent_follow(0, -64), -16);
    CHECK_EQ(dk_detent_follow(16, 0), 0);
    CHECK_EQ(dk_detent_follow(5, 20), 5);
    /* An encoder that rests between counts 2 and 3: one move, no more. */
    {
        static const int32_t rest[] = { 1, 2, 3, 2, 3, 2, 3, 2, 3, 2 };
        CHECK_EQ(follow(rest, 10, &moves), 1);
        CHECK_EQ(moves, 1u);
    }
    /* And one between -2 and -3, and between 1 and 2 after a detent. */
    {
        static const int32_t rest[] = { -2, -3, -2, -3, -2, -3 };
        CHECK_EQ(follow(rest, 6, &moves), -1);
        CHECK_EQ(moves, 1u);
    }
    {
        static const int32_t rest[] = { 3, 4, 5, 6, 5, 6, 5, 6, 5 };
        CHECK_EQ(follow(rest, 9, &moves), 1);
        CHECK_EQ(moves, 1u);
    }
    /* A turn of 4 detents count by count, and back. */
    {
        int32_t d = 0;
        unsigned m = 0;
        for (int32_t c = 0; c <= 16; ++c) {
            const int32_t next = dk_detent_follow(d, c);
            m += next != d ? 1u : 0u;
            d = next;
        }
        CHECK_EQ(d, 4);
        CHECK_EQ(m, 4u);
        for (int32_t c = 16; c >= 0; --c) {
            d = dk_detent_follow(d, c);
        }
        CHECK_EQ(d, 0);
    }
}

int main(void)
{
    RUN(a_new_screen_empties_the_value_of_the_screen_before);
    RUN(nav_rows_of_another_position_replace_the_rows_before);
    RUN(a_refused_or_repeated_packet_changes_nothing);
    RUN(the_face_says_link_lost_500ms_after_the_last_panel_packet);
    RUN(a_new_link_empties_the_model_and_learns_the_seq_again);
    RUN(the_dial_ends_the_link_when_link_lost_has_held_for_500ms);
    RUN(a_detent_moves_3_counts_from_its_center_and_jitter_never_clicks);
    return test_summary("dialknob_face");
}
