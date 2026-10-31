/*
 * The servo programmer's last message when the session goes, and what opens
 * CHECK THIS FILE: the soft limits' base, the servo as the session found it.
 *
 * Under test: a line about the servo or the draft leaves with the session
 * when the link goes or the IOmcu starts again, and comes back with no
 * value read; a past write, a saved file, a refusal and a danger line stay;
 * a session the port ended still says so.  A file between the servo and the
 * base opens no page after a write, the same file opens it in a session that
 * found the servo below it, ENABLE plays no part, and a file with no check
 * value goes the same way under its own line.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_prog_fix.h"

#include "kst_panel.h"

/* A file whose image is the bench servo's with Left Angle at @p raw. */
static fake_file_t *file_left(const char *name, uint16_t raw)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_edit(&img, KST_F_LEFT_ANGLE, raw));
    return fake_card_put(&g_fc, name, &img);
}

static void load(const char *name)
{
    CHECK_EQ(kst_prog_load(&g_p, name), KST_PROG_MSG_NONE);
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
}

/* The model's last message is @p id, as an operation left it. */
static void said(unsigned id)
{
    g_p.msg.id = (uint8_t)id;
}

/* The link goes between two operations. */
static void link_goes(void)
{
    kst_link_lost(&g_k);
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK(!kst_prog_have_servo(&g_p));
}

TEST_CASE(the_read_line_goes_with_a_lost_link)
{
    up();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    const uint32_t seq = kst_prog_message(&g_p)->seq;
    link_goes();
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    /* The panel redraws on the sequence number. */
    CHECK(kst_prog_message(&g_p)->seq != seq);
    char text[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    CHECK_EQ(kst_panel_message_lines(kst_prog_message(&g_p), text), 0);
    /* And stays gone when the link comes back to a port in PWM. */
    link_up();
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
}

TEST_CASE(the_loaded_line_goes_with_a_lost_link)
{
    const kst_image_t sv = sim_bench_image();

    up();
    (void)file_left("KST004",
                    (uint16_t)(kst_field_get(&sv, KST_F_LEFT_ANGLE) - 2u));
    load("KST004");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    link_goes();
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
}

/* The IOmcu starts again, the port back in PWM by nobody's command: the
 * read line is replaced by the session's end, which the line's going must not
 * hide. */
TEST_CASE(a_session_the_port_ended_is_still_said_over)
{
    up();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    port_fresh();
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(msg(), KST_PROG_MSG_SESSION_ENDED);
    CHECK(!kst_prog_have_servo(&g_p));
}

/* A read that ends in the same feed as the port's fall to PWM: its line goes
 * with the session, and the session's end is said in its place. */
TEST_CASE(a_line_of_the_session_said_in_the_last_feed_gives_way)
{
    kst_link_outcome_t o;

    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = KST_SES_OK;
    g_f.rd.port = LINK_KST_PWM;
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_SESSION_ENDED);
    CHECK(!kst_prog_have_servo(&g_p));

    /* A message said in that feed that is not the session's stands: the
     * NACK of a command the module refused. */
    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = KST_SES_ERR_NO_SERVO;
    g_f.rd.port = LINK_KST_PWM;
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_NO_SERVO);
}

/* What happened stays true. */
TEST_CASE(a_past_write_a_saved_file_and_a_refusal_stay)
{
    up_enabled();
    edit(KST_F_BOOST, (uint16_t)(kst_field_get(kst_prog_servo(&g_p),
                                               KST_F_BOOST) - 1u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    link_goes();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);

    up();
    g_card = true;
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    frames(3);
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    link_goes();
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);

    up();
    said(KST_PROG_MSG_LOAD_REFUSED);
    link_goes();
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_REFUSED);

    /* A danger line: the servo may hold a torn write. */
    up();
    said(KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_TORN);
    link_goes();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_TORN);
}

/* Every line that speaks of what stood, at the session's end. */
TEST_CASE(every_line_of_the_session_goes)
{
    static const unsigned k_lines[] = {
        KST_PROG_MSG_READ, KST_PROG_MSG_READ_ONLY, KST_PROG_MSG_VERIFIED,
        KST_PROG_MSG_LOAD_SAME, KST_PROG_MSG_LOAD_SOFT, KST_PROG_MSG_LOADED,
        KST_PROG_MSG_LOADED_EDITED, KST_PROG_MSG_LOADED_UNTYPED,
        KST_PROG_MSG_NO_CHANGES, KST_PROG_MSG_NEED_ENABLE,
        KST_PROG_MSG_NEED_READ, KST_PROG_MSG_NEED_TYPE,
        KST_PROG_MSG_EDIT_HARD, KST_PROG_MSG_EDIT_LOCKED,
        KST_PROG_MSG_JOURNAL_SAME, KST_PROG_MSG_JOURNAL_DONE,
        KST_PROG_MSG_BACKUP_OTHER,
    };
    for (size_t i = 0u; i < sizeof(k_lines) / sizeof(k_lines[0]); ++i) {
        up();
        said(k_lines[i]);
        link_goes();
        CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    }
}

/*
 * CHECK THIS FILE comes from the soft limits, whose base is the servo as the
 * session found it.  Left Angle below the value found raises nothing in that
 * session, written or not; the next session finds the lower value, and the
 * same file is a raise.  ENABLE is not asked.
 */
TEST_CASE(the_same_file_is_compared_with_the_servo_each_session_found)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t found = kst_field_get(&sv, KST_F_LEFT_ANGLE);
    const uint16_t low = (uint16_t)(found - 2u);
    const uint16_t mid = (uint16_t)(found - 1u);

    up();
    g_card = true;
    frame();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    /* The first version, written. */
    fake_file_t *f = file_left("KST004", low);
    load("KST004");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_field_get(kst_prog_servo(&g_p), KST_F_LEFT_ANGLE), low);
    CHECK_EQ(kst_field_get(kst_prog_soft_base(&g_p), KST_F_LEFT_ANGLE), found);
    /* The file edited up by one: under the base, no page. */
    CHECK(kst_field_edit(&f->img, KST_F_LEFT_ANGLE, mid));
    load("KST004");
    CHECK(!kst_prog_soft_is_load(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(draft(KST_F_LEFT_ANGLE), mid);
    /* The same with the switch off: ENABLE is not asked. */
    kst_prog_discard(&g_p);
    CHECK_EQ(kst_prog_enable(&g_p, false), KST_PROG_MSG_NONE);
    load("KST004");
    CHECK(!kst_prog_soft_is_load(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    kst_prog_discard(&g_p);

    /* A new session finds Left Angle at the value written. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_type_set(&g_p, kst_prog_type_guess(&g_p)),
             KST_PROG_MSG_NONE);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_field_get(kst_prog_soft_base(&g_p), KST_F_LEFT_ANGLE), low);
    load("KST004");
    CHECK(kst_prog_soft_is_load(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SOFT);
    CHECK((kst_prog_soft_rules(&g_p) & KST_RULE_BIT(KST_S_LEFT_RAISED)) != 0u);
    /* The draft is untouched until the hold. */
    CHECK_EQ(draft(KST_F_LEFT_ANGLE), low);
    /* The same with the switch on: the page opens all the same. */
    kst_prog_soft_cancel(&g_p);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    load("KST004");
    CHECK(kst_prog_soft_is_load(&g_p));
    kst_prog_soft_cancel(&g_p);

    /* A value at the base is no raise; one over it, in the same session as a
     * write, is asked. */
    CHECK(kst_field_edit(&f->img, KST_F_LEFT_ANGLE, low));
    load("KST004");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
}

/* A write under the base, then a file over the base in the same session:
 * the raise is asked whatever the session wrote. */
TEST_CASE(a_file_over_the_found_value_is_asked_after_a_write)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t found = kst_field_get(&sv, KST_F_LEFT_ANGLE);

    up();
    g_card = true;
    frame();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_LEFT_ANGLE, (uint16_t)(found - 2u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    (void)file_left("OVER", (uint16_t)(found + 1u));
    load("OVER");
    CHECK(kst_prog_soft_is_load(&g_p));
    CHECK((kst_prog_soft_rules(&g_p) & KST_RULE_BIT(KST_S_LEFT_RAISED)) != 0u);
    kst_prog_soft_cancel(&g_p);
}

/* The page and the file's kind are apart: an edited file has no check value,
 * opens no page under the base, and still says it is edited. */
TEST_CASE(a_file_with_no_check_value_never_reads_as_a_verified_one)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t found = kst_field_get(&sv, KST_F_LEFT_ANGLE);

    up();
    g_card = true;
    frame();
    fake_file_t *f = file_left("ED", (uint16_t)(found - 1u));
    f->kind = KST_PROG_FILE_EDITED;
    load("ED");
    CHECK(!kst_prog_soft_is_load(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_EDITED);
    kst_prog_discard(&g_p);
    /* Past the base: the page opens, its kind known to it. */
    CHECK(kst_field_edit(&f->img, KST_F_LEFT_ANGLE, (uint16_t)(found + 1u)));
    load("ED");
    CHECK(kst_prog_soft_is_load(&g_p));
    CHECK_EQ(kst_prog_load_kind(&g_p), KST_PROG_FILE_EDITED);
    kst_prog_soft_confirm(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_EDITED);
}

int main(void)
{
    RUN(the_read_line_goes_with_a_lost_link);
    RUN(the_loaded_line_goes_with_a_lost_link);
    RUN(a_session_the_port_ended_is_still_said_over);
    RUN(a_line_of_the_session_said_in_the_last_feed_gives_way);
    RUN(a_past_write_a_saved_file_and_a_refusal_stay);
    RUN(every_line_of_the_session_goes);
    RUN(the_same_file_is_compared_with_the_servo_each_session_found);
    RUN(a_file_over_the_found_value_is_asked_after_a_write);
    RUN(a_file_with_no_check_value_never_reads_as_a_verified_one);
    return test_summary("kst_prog_gone");
}
