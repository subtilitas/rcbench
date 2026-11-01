/*
 * The card's side of the ESC programmer's files (esc_card.c): a save under the
 * proposed name, a list in pages, a load, and what each says of a card that is not
 * there or takes nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "esc_card.h"
#include "greatest.h"

#define FILES 40
static char s_names[FILES][SERVO_STORE_NAME_MAX];
static char s_texts[FILES][64];
static int  s_n;
static bool s_in;
static bool s_denied;
static esc_card_req_t  s_req;
static esc_card_done_t s_done;

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
    return find(n) < 0 ? -1 : (int32_t)strlen(s_texts[find(n)]);
}

static int32_t t_read(void *c, const char *n, char *b, size_t cap)
{
    (void)c;
    if (find(n) < 0) {
        return -1;
    }
    const size_t l = strlen(s_texts[find(n)]);

    memcpy(b, s_texts[find(n)], l < cap ? l : cap);
    return (int32_t)(l < cap ? l : cap);
}

static servo_store_io_result_t t_create(void *c, const char *n, const char *d, size_t l)
{
    (void)c;
    if (s_denied) {
        return SERVO_STORE_IO_DENIED;
    }
    snprintf(s_names[s_n], sizeof(s_names[0]), "%s", n);
    snprintf(s_texts[s_n], sizeof(s_texts[0]), "%.*s", (int)l, d);
    ++s_n;
    return SERVO_STORE_IO_OK;
}

static const servo_store_io_t k_io = { NULL, EW_FILE_STEM, t_mounted, t_walk, t_size,
                                       t_read, t_create };

static void fresh(void)
{
    s_n = 0;
    s_in = true;
    s_denied = false;
    memset(&s_req, 0, sizeof(s_req));
}

TEST_CASE(a_save_takes_the_next_number)
{
    fresh();
    s_req.op = ESC_CARD_SAVE;
    s_req.id = 7u;
    memcpy(s_req.text, "abc", 3u);
    s_req.len = 3u;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_OK);
    CHECK_EQ(s_done.id, 7u);
    CHECK_EQ(s_done.op, ESC_CARD_SAVE);
    CHECK_STR_EQ(s_done.name, "ESCW001");
    CHECK_STR_EQ(s_texts[0], "abc");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_STR_EQ(s_done.name, "ESCW002");
}

TEST_CASE(a_save_says_what_the_card_said)
{
    fresh();
    s_in = false;
    s_req.op = ESC_CARD_SAVE;
    s_req.len = 1u;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_NO_CARD);
    CHECK_STR_EQ(s_done.name, "");
    fresh();
    s_denied = true;
    s_req.op = ESC_CARD_SAVE;
    s_req.len = 1u;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_DENIED);
}

TEST_CASE(a_list_gives_a_page_and_the_total)
{
    fresh();
    for (int i = 0; i < 30; ++i) {
        snprintf(s_names[i], sizeof(s_names[0]), "ESCW%03d.TXT", i + 1);
        snprintf(s_texts[i], sizeof(s_texts[0]), "t%d", i);
    }
    s_n = 30;
    s_req.op = ESC_CARD_LIST;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_OK);
    CHECK_EQ(s_done.total, 30u);
    CHECK_EQ(s_done.count, ESC_CARD_FILES);
    /* The store's order: the newest number first. */
    CHECK_STR_EQ(s_done.names[0], "ESCW030");
    s_req.first = 24u;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.count, 6u);
    CHECK_STR_EQ(s_done.names[0], "ESCW006");
    CHECK_STR_EQ(s_done.names[5], "ESCW001");
}

TEST_CASE(a_list_finds_a_late_page)
{
    fresh();
    for (int i = 0; i < FILES; ++i) {
        snprintf(s_names[i], sizeof(s_names[0]), "ESCW%03d.TXT", i + 1);
        s_texts[i][0] = 'x';
        s_texts[i][1] = '\0';
    }
    s_n = FILES;
    s_req.op = ESC_CARD_LIST;
    s_req.first = 36u;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.count, 4u);
    CHECK_STR_EQ(s_done.names[3], "ESCW001");
}

TEST_CASE(a_load_gives_the_text)
{
    fresh();
    snprintf(s_names[0], sizeof(s_names[0]), "ESCW001.TXT");
    snprintf(s_texts[0], sizeof(s_texts[0]), "hello");
    s_n = 1;
    s_req.op = ESC_CARD_LOAD;
    snprintf(s_req.name, sizeof(s_req.name), "ESCW001");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_OK);
    CHECK_EQ(s_done.len, 5u);
    CHECK_STR_EQ(s_done.text, "hello");
    snprintf(s_req.name, sizeof(s_req.name), "ESCW002");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_MISSING);
    s_req.op = ESC_CARD_RECORD;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_READ);
}

int main(void)
{
    RUN(a_save_takes_the_next_number);
    RUN(a_save_says_what_the_card_said);
    RUN(a_list_gives_a_page_and_the_total);
    RUN(a_list_finds_a_late_page);
    RUN(a_load_gives_the_text);
    return test_summary("esc_card");
}
