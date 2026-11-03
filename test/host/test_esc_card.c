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

#define FILES 1000
static char s_names[FILES][SERVO_STORE_NAME_MAX];
static char s_texts[FILES][64];
static int  s_n;
static bool s_in;
static bool s_denied;
static bool s_full;
static int32_t s_size_is;
static servo_store_io_result_t s_remove_says;
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
    if (s_size_is != 0 && find(n) >= 0) {
        return s_size_is;
    }
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
    if (s_full) {
        return SERVO_STORE_IO_FULL;
    }
    snprintf(s_names[s_n], sizeof(s_names[0]), "%s", n);
    snprintf(s_texts[s_n], sizeof(s_texts[0]), "%.*s", (int)l, d);
    ++s_n;
    return SERVO_STORE_IO_OK;
}

static servo_store_io_result_t t_remove(void *c, const char *n)
{
    const int i = find(n);

    (void)c;
    if (i < 0) {
        return SERVO_STORE_IO_ERROR;
    }
    if (s_remove_says != SERVO_STORE_IO_OK) {
        return s_remove_says;
    }
    for (int k = i; k + 1 < s_n; ++k) {
        memcpy(s_names[k], s_names[k + 1], sizeof(s_names[0]));
        memcpy(s_texts[k], s_texts[k + 1], sizeof(s_texts[0]));
    }
    --s_n;
    return SERVO_STORE_IO_OK;
}

static const servo_store_io_t k_io = { NULL, EW_FILE_STEM, t_mounted, t_walk, t_size,
                                       t_read, t_create, true, t_remove };

static void fresh(void)
{
    s_n = 0;
    s_in = true;
    s_denied = false;
    s_full = false;
    s_size_is = 0;
    s_remove_says = SERVO_STORE_IO_OK;
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
    for (int i = 0; i < 40; ++i) {
        snprintf(s_names[i], sizeof(s_names[0]), "ESCW%03d.TXT", i + 1);
        s_texts[i][0] = 'x';
        s_texts[i][1] = '\0';
    }
    s_n = 40;
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

TEST_CASE(each_refusal_of_the_card_has_its_answer)
{
    /* A full card. */
    fresh();
    s_full = true;
    s_req.op = ESC_CARD_SAVE;
    s_req.len = 1u;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_FULL);
    fresh();
    /* A name that is no file's. */
    s_req.op = ESC_CARD_LOAD;
    s_req.name[0] = '\0';
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_MISSING);
    /* A file larger than any settings file. */
    fresh();
    snprintf(s_names[0], sizeof(s_names[0]), "ESCW001.TXT");
    s_texts[0][0] = 'x';
    s_texts[0][1] = '\0';
    s_n = 1;
    s_size_is = (int32_t)EW_FILE_MAX + 1;
    s_req.op = ESC_CARD_LOAD;
    snprintf(s_req.name, sizeof(s_req.name), "ESCW001");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_LARGE);
    /* A list with no card. */
    fresh();
    s_in = false;
    s_req.op = ESC_CARD_LIST;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_NO_CARD);
}

/** @brief A file on the card. */
static void put(const char *name)
{
    snprintf(s_names[s_n], sizeof(s_names[0]), "%s", name);
    snprintf(s_texts[s_n], sizeof(s_texts[0]), "t");
    ++s_n;
}

/** @brief A save with the tag @p tag; the name it took. */
static const char *save_tagged(const char *tag)
{
    s_req.op = ESC_CARD_SAVE;
    snprintf(s_req.tag, sizeof(s_req.tag), "%s", tag);
    memcpy(s_req.text, "abc", 3u);
    s_req.len = 3u;
    esc_card_run(&k_io, &s_req, &s_done);
    return s_done.name;
}

TEST_CASE(a_save_carries_the_firmware_in_its_name)
{
    fresh();
    CHECK_STR_EQ(save_tagged(esc_card_fw_tag(EW_FW_AM)), "ESCW001-AM32");
    CHECK_STR_EQ(s_names[0], "ESCW001-AM32.TXT");
    CHECK_STR_EQ(save_tagged(esc_card_fw_tag(EW_FW_S)), "ESCW002-BLHELI_S");
    CHECK_STR_EQ(save_tagged(esc_card_fw_tag(EW_FW_BJ)), "ESCW003-BLUEJAY");
    CHECK_STR_EQ(save_tagged(esc_card_fw_tag(EW_FW_E32)), "ESCW004-ESCAPE32");
    /* No firmware known: no tag. */
    CHECK_STR_EQ(save_tagged(esc_card_fw_tag(EW_FW_NONE)), "ESCW005");
    CHECK_STR_EQ(esc_card_fw_tag(0xEEu), "");
}

TEST_CASE(a_mixed_folder_never_takes_a_number_twice_and_lists_newest_first)
{
    fresh();
    put("ESCW001.TXT");
    put("ESCW007-AM32.TXT");
    put("escw003-bluejay.txt");
    put("WING.TXT");
    put("ESCW009-.TXT");   /* no tag after the hyphen: a name of its own */
    put("ESCW0100-AM32.TXT");
    CHECK_STR_EQ(save_tagged("AM32"), "ESCW008-AM32");
    s_req.op = ESC_CARD_LIST;
    s_req.first = 0u;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_OK);
    CHECK_EQ(s_done.total, 7u);
    CHECK_STR_EQ(s_done.names[0], "ESCW008-AM32");
    CHECK_STR_EQ(s_done.names[1], "ESCW007-AM32");
    CHECK_STR_EQ(s_done.names[2], "escw003-bluejay");
    CHECK_STR_EQ(s_done.names[3], "ESCW001");
    /* Old and new names load alike. */
    s_req.op = ESC_CARD_LOAD;
    snprintf(s_req.name, sizeof(s_req.name), "ESCW001");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_OK);
    snprintf(s_req.name, sizeof(s_req.name), "ESCW007-AM32");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_OK);
}

TEST_CASE(at_999_files_the_lowest_free_number_and_then_full)
{
    static char name[SERVO_STORE_NAME_MAX];

    fresh();
    for (int n = 1; n <= 999; ++n) {
        snprintf(name, sizeof(name), n % 2 != 0 ? "ESCW%03d.TXT" : "ESCW%03d-AM32.TXT", n);
        put(name);
    }
    CHECK_STR_EQ(save_tagged("AM32"), "");
    CHECK_EQ(s_done.result, ESC_CARD_DONE_FULL);
    CHECK_EQ(s_n, 999);
    /* 500 goes: the gap is the one number free. */
    s_req.op = ESC_CARD_DELETE;
    snprintf(s_req.name, sizeof(s_req.name), "ESCW500-AM32");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_OK);
    CHECK_STR_EQ(save_tagged("BLUEJAY"), "ESCW500-BLUEJAY");
    s_req.op = ESC_CARD_LIST;
    s_req.first = 0u;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.total, 999u);
    CHECK_STR_EQ(s_done.names[0], "ESCW999");
}

TEST_CASE(a_delete_removes_one_file_or_says_why_not)
{
    fresh();
    put("ESCW001.TXT");
    put("ESCW002-AM32.TXT");
    s_req.op = ESC_CARD_DELETE;
    s_req.id = 9u;
    snprintf(s_req.name, sizeof(s_req.name), "ESCW001");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_OK);
    CHECK_EQ(s_done.op, ESC_CARD_DELETE);
    CHECK_EQ(s_done.id, 9u);
    CHECK_STR_EQ(s_done.name, "ESCW001");
    CHECK_EQ(s_n, 1);
    /* Gone already. */
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_MISSING);
    /* The card refuses: the file stays. */
    s_remove_says = SERVO_STORE_IO_ERROR;
    snprintf(s_req.name, sizeof(s_req.name), "ESCW002-AM32");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_DELETE);
    CHECK_EQ(s_n, 1);
    s_remove_says = SERVO_STORE_IO_DENIED;
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_DENIED);
    CHECK_EQ(s_n, 1);
    /* The record, ESCW.TXT, is never a file a delete takes. */
    s_remove_says = SERVO_STORE_IO_OK;
    put("ESCW.TXT");
    snprintf(s_req.name, sizeof(s_req.name), "ESCW");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_MISSING);
    CHECK_EQ(s_n, 2);
    /* A size the card cannot say is no reason to call the file gone. */
    s_size_is = -1;
    put("ESCW003.TXT");
    snprintf(s_req.name, sizeof(s_req.name), "ESCW003");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_OK);
    CHECK_EQ(s_n, 2);
    s_size_is = 0;
    /* No card. */
    s_in = false;
    snprintf(s_req.name, sizeof(s_req.name), "ESCW002-AM32");
    esc_card_run(&k_io, &s_req, &s_done);
    CHECK_EQ(s_done.result, ESC_CARD_DONE_NO_CARD);
    CHECK_EQ(s_n, 2);
}

TEST_CASE(a_delete_runs_only_on_the_volume_of_its_list)
{
    fresh();
    s_req.op = ESC_CARD_DELETE;
    s_req.id = 4u;
    s_req.mount = 3u;
    snprintf(s_req.name, sizeof(s_req.name), "ESCW001");
    CHECK(esc_card_volume_check(&s_req, 3u, &s_done));
    CHECK(!esc_card_volume_check(&s_req, 4u, &s_done));
    CHECK_EQ(s_done.result, ESC_CARD_DONE_CHANGED);
    CHECK_EQ(s_done.id, 4u);
    CHECK_EQ(s_done.mount, 4u);
    CHECK_STR_EQ(s_done.name, "ESCW001");
    /* Any other request runs on whatever volume is in. */
    s_req.op = ESC_CARD_LOAD;
    CHECK(esc_card_volume_check(&s_req, 4u, &s_done));
}

TEST_CASE(a_store_without_a_remove_removes_nothing)
{
    servo_store_io_t io = k_io;

    fresh();
    put("ESCW001.TXT");
    io.remove = NULL;
    CHECK_EQ(servo_store_remove(&io, "ESCW001.TXT"), SERVO_STORE_ARG);
    CHECK_EQ(servo_store_remove(NULL, "ESCW001.TXT"), SERVO_STORE_ARG);
    CHECK_EQ(s_n, 1);
    /* Untagged, a tagged name counts for nothing. */
    io.tagged = false;
    CHECK_EQ(servo_store_number_of(&io, "ESCW004-AM32.TXT"), 0);
    CHECK_EQ(servo_store_number_of(&k_io, "ESCW004-AM32.TXT"), 4);
    CHECK_EQ(servo_store_number_of(&k_io, "ESCW004.TXT"), 4);
    CHECK_EQ(servo_store_number_of(&k_io, NULL), 0);
}

int main(void)
{
    RUN(a_save_takes_the_next_number);
    RUN(a_save_says_what_the_card_said);
    RUN(a_list_gives_a_page_and_the_total);
    RUN(a_list_finds_a_late_page);
    RUN(a_load_gives_the_text);
    RUN(each_refusal_of_the_card_has_its_answer);
    RUN(a_save_carries_the_firmware_in_its_name);
    RUN(a_mixed_folder_never_takes_a_number_twice_and_lists_newest_first);
    RUN(at_999_files_the_lowest_free_number_and_then_full);
    RUN(a_delete_removes_one_file_or_says_why_not);
    RUN(a_delete_runs_only_on_the_volume_of_its_list);
    RUN(a_store_without_a_remove_removes_nothing);
    return test_summary("esc_card");
}
