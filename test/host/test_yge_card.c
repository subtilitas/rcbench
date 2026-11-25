/*
 * The card's side of programming for YGE ESCs (yge_card.c): the backup saved
 * under the next number with the type code as its tag, a list in pages, a
 * load, what each says of a card that is not there, full or refusing, and how
 * a save's answer reaches the model.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "yge_card.h"

#define FILES 64
static char s_names[FILES][SERVO_STORE_NAME_MAX];
static char s_texts[FILES][64];
static int s_n;
static bool s_in;
static bool s_denied;
static bool s_full;
static bool s_fail;
static int32_t s_size_is;
static yge_card_req_t s_req;
static yge_card_done_t s_done;

static bool t_mounted(void *c)
{
    (void)c;
    return s_in;
}

static bool t_walk(void *c, servo_store_visit_fn v, void *x)
{
    (void)c;
    for (int i = 0; i < s_n; ++i) {
        v(s_names[i], false, x);
    }
    return true;
}

static int find(const char *n)
{
    for (int i = 0; i < s_n; ++i) {
        if (strcmp(s_names[i], n) == 0) {
            return i;
        }
    }
    return -1;
}

static int32_t t_size(void *c, const char *n)
{
    (void)c;
    if (s_size_is != 0 && find(n) >= 0) {
        return s_size_is;
    }
    return find(n) < 0 ? -1 : (int32_t)strlen(s_texts[find(n)]);
}

static int32_t t_read(void *c, const char *n, char *b, size_t cap)
{
    (void)c;
    if (find(n) < 0 || s_fail) {
        return -1;
    }
    const size_t l = strlen(s_texts[find(n)]);

    memcpy(b, s_texts[find(n)], l < cap ? l : cap);
    return (int32_t)(l < cap ? l : cap);
}

static servo_store_io_result_t t_create(void *c, const char *n, const char *d,
                                        size_t l)
{
    (void)c;
    if (s_denied) {
        return SERVO_STORE_IO_DENIED;
    }
    if (s_full) {
        return SERVO_STORE_IO_FULL;
    }
    if (s_fail) {
        return SERVO_STORE_IO_ERROR;
    }
    snprintf(s_names[s_n], sizeof(s_names[0]), "%s", n);
    snprintf(s_texts[s_n], sizeof(s_texts[0]), "%.*s", (int)l, d);
    ++s_n;
    return SERVO_STORE_IO_OK;
}

static servo_store_io_result_t t_remove(void *c, const char *n)
{
    (void)c;
    (void)n;
    return SERVO_STORE_IO_ERROR;
}

static const servo_store_io_t k_io = { NULL, OY_FILE_STEM, t_mounted, t_walk,
                                       t_size, t_read, t_create, true, t_remove };

static void fresh(void)
{
    s_n = 0;
    s_in = true;
    s_denied = false;
    s_full = false;
    s_fail = false;
    s_size_is = 0;
    memset(&s_req, 0, sizeof(s_req));
    memset(&s_done, 0x5A, sizeof(s_done));
}

/* A save of "abc" with the tag @p tag; the name it took. */
static const char *save_tagged(const char *tag)
{
    s_req.op = YGE_CARD_SAVE;
    s_req.id = 9u;
    snprintf(s_req.tag, sizeof(s_req.tag), "%s", tag);
    memcpy(s_req.text, "abc", 3u);
    s_req.len = 3u;
    yge_card_run(&k_io, &s_req, &s_done);
    return s_done.name;
}

TEST_CASE(a_save_takes_the_next_number_with_the_type_code)
{
    fresh();
    CHECK_STR_EQ(save_tagged("5712"), "YGE001-5712");
    CHECK_EQ(s_done.result, YGE_CARD_DONE_OK);
    CHECK_EQ(s_done.op, YGE_CARD_SAVE);
    CHECK_EQ(s_done.id, 9u);
    CHECK_STR_EQ(s_names[0], "YGE001-5712.TXT");
    CHECK_STR_EQ(s_texts[0], "abc");
    /* One number through the folder, tagged or not. */
    CHECK_STR_EQ(save_tagged(""), "YGE002");
    CHECK_STR_EQ(save_tagged("848"), "YGE003-848");
    /* A length past the text's room is cut to it. */
    s_req.len = sizeof(s_req.text) + 1u;
    s_req.text[0] = 'z';
    s_req.text[1] = '\0';
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_OK);
}

TEST_CASE(a_save_says_what_the_card_said)
{
    static const struct {
        bool in, denied, full, fail;
        uint8_t result, prog;
    } k_case[] = {
        { false, false, false, false, YGE_CARD_DONE_NO_CARD, YGE_PROG_CARD_NO_CARD },
        { true, true, false, false, YGE_CARD_DONE_DENIED, YGE_PROG_CARD_WRITE },
        { true, false, true, false, YGE_CARD_DONE_FULL, YGE_PROG_CARD_FULL },
        { true, false, false, true, YGE_CARD_DONE_WRITE, YGE_PROG_CARD_WRITE },
    };

    for (size_t i = 0u; i < sizeof(k_case) / sizeof(k_case[0]); ++i) {
        fresh();
        s_in = k_case[i].in;
        s_denied = k_case[i].denied;
        s_full = k_case[i].full;
        s_fail = k_case[i].fail;
        CHECK_STR_EQ(save_tagged("5712"), "");
        CHECK_EQ(s_done.result, k_case[i].result);
        CHECK_EQ(yge_card_prog_result(s_done.result), k_case[i].prog);
    }
    CHECK_EQ(yge_card_prog_result(YGE_CARD_DONE_OK), YGE_PROG_CARD_OK);
    CHECK_EQ(yge_card_prog_result(YGE_CARD_DONE_READ), YGE_PROG_CARD_UNREADABLE);
    CHECK_EQ(yge_card_prog_result(YGE_CARD_DONE_MISSING),
             YGE_PROG_CARD_UNREADABLE);
    CHECK_EQ(yge_card_prog_result(YGE_CARD_DONE_LARGE), YGE_PROG_CARD_WRITE);
}

TEST_CASE(a_list_gives_a_page_and_the_total)
{
    fresh();
    for (int i = 0; i < 30; ++i) {
        snprintf(s_names[i], sizeof(s_names[0]), "YGE%03d-5712.TXT", i + 1);
        snprintf(s_texts[i], sizeof(s_texts[0]), "t%d", i);
    }
    s_n = 30;
    s_req.op = YGE_CARD_LIST;
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_OK);
    CHECK_EQ(s_done.total, 30u);
    CHECK_EQ(s_done.count, YGE_CARD_FILES);
    /* The newest number first, the extension off. */
    CHECK_STR_EQ(s_done.names[0], "YGE030-5712");
    s_req.first = 24u;
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.count, 6u);
    CHECK_STR_EQ(s_done.names[0], "YGE006-5712");
    CHECK_STR_EQ(s_done.names[5], "YGE001-5712");
}

TEST_CASE(a_list_past_one_walk_finds_its_page)
{
    fresh();
    for (int i = 0; i < FILES; ++i) {
        snprintf(s_names[i], sizeof(s_names[0]), "YGE%03d.TXT", i + 1);
        s_texts[i][0] = 'x';
        s_texts[i][1] = '\0';
    }
    s_n = FILES;
    s_req.op = YGE_CARD_LIST;
    s_req.first = 60u;
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_OK);
    CHECK_EQ(s_done.count, 4u);
    CHECK_STR_EQ(s_done.names[3], "YGE001");
    /* No card. */
    s_in = false;
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_NO_CARD);
    CHECK_EQ(s_done.count, 0u);
}

TEST_CASE(a_load_gives_the_text_or_says_why_not)
{
    fresh();
    snprintf(s_names[0], sizeof(s_names[0]), "YGE001-5712.TXT");
    snprintf(s_texts[0], sizeof(s_texts[0]), "hello");
    s_n = 1;
    s_req.op = YGE_CARD_LOAD;
    snprintf(s_req.name, sizeof(s_req.name), "YGE001-5712");
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_OK);
    CHECK_EQ(s_done.len, 5u);
    CHECK_STR_EQ(s_done.text, "hello");
    /* No such file. */
    snprintf(s_req.name, sizeof(s_req.name), "YGE002");
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_MISSING);
    /* A name that is no file's. */
    s_req.name[0] = '\0';
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_MISSING);
    /* One byte over the largest settings file. */
    snprintf(s_req.name, sizeof(s_req.name), "YGE001-5712");
    s_size_is = (int32_t)OY_FILE_MAX + 1;
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_LARGE);
    /* A read that fails. */
    s_size_is = 0;
    s_fail = true;
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_READ);
    CHECK_EQ(s_done.len, 0u);
}

TEST_CASE(an_unknown_request_is_answered_and_changes_nothing)
{
    fresh();
    s_req.op = YGE_CARD_NONE;
    s_req.id = 3u;
    yge_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, YGE_CARD_DONE_READ);
    CHECK_EQ(s_done.id, 3u);
    CHECK_EQ(s_done.count, 0u);
    CHECK_STR_EQ(s_done.name, "");
    CHECK_EQ(s_n, 0);
}

int main(void)
{
    RUN(a_save_takes_the_next_number_with_the_type_code);
    RUN(a_save_says_what_the_card_said);
    RUN(a_list_gives_a_page_and_the_total);
    RUN(a_list_past_one_walk_finds_its_page);
    RUN(a_load_gives_the_text_or_says_why_not);
    RUN(an_unknown_request_is_answered_and_changes_nothing);
    return test_summary("yge_card");
}
