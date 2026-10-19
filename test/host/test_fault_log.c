/*
 * The fault log under its 1 MiB cap, fault_log_note(), over a card held in
 * memory: a line that ends the file at the cap exactly, one that would pass
 * it, RCBENCH.OLD replaced, the folder made, and every failure that writes
 * nothing past the cap.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "fault_log.h"

/* ------------------------------------------------------------ the card */

/* A file is its size and its last line; the bytes themselves do not
 * matter to the cap. */
typedef struct {
    bool     there;
    bool     unread;              /* its size does not read            */
    int64_t  size;
    char     last[256];
} fake_file_t;

typedef struct {
    bool        mounted;
    bool        bench;            /* the folder BENCH is there          */
    bool        bench_is_file;
    bool        protect;          /* write-protected                    */
    bool        rename_fails;
    bool        append_fails;
    bool        remove_fails;
    fake_file_t log;
    fake_file_t old;
    fake_file_t tmp;
    const char *fail_from;        /* the one rename that fails, or NULL */
    int         renames;
    int         removes;
    int         appends;
} fake_t;

static fake_t g;

static fake_file_t *file(const char *path)
{
    if (strcmp(path, CARD_FAULT_LOG) == 0) {
        return &g.log;
    }
    if (strcmp(path, CARD_FAULT_OLD) == 0) {
        return &g.old;
    }
    if (strcmp(path, CARD_FAULT_TMP) == 0) {
        return &g.tmp;
    }
    T_FAIL("no file %s on this card", path);
    return NULL;
}

static bool fake_mounted(void *ctx)
{
    (void)ctx;
    return g.mounted;
}

static card_dir_t fake_make(void *ctx, const char *level)
{
    (void)ctx;
    CHECK_STR_EQ(level, CARD_BENCH);
    if (g.bench_is_file) {
        return CARD_DIR_FILE;
    }
    if (!g.bench && g.protect) {
        return CARD_DIR_DENIED;
    }
    g.bench = true;
    return CARD_DIR_OK;
}

static int32_t fake_size(void *ctx, const char *path)
{
    (void)ctx;
    const fake_file_t *f = file(path);
    if (f != NULL && f->unread) {
        return FAULT_SIZE_UNREAD;
    }
    if (f == NULL || !f->there) {
        return FAULT_SIZE_NONE;
    }
    return (f->size > INT32_MAX) ? INT32_MAX : (int32_t)f->size;
}

static bool fake_remove(void *ctx, const char *path)
{
    (void)ctx;
    fake_file_t *f = file(path);
    ++g.removes;
    if (g.remove_fails) {
        return false;
    }
    if (f != NULL) {
        memset(f, 0, sizeof(*f));
    }
    return true;
}

static bool fake_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    fake_file_t *a = file(from);
    fake_file_t *b = file(to);
    ++g.renames;
    if (g.rename_fails || a == NULL || b == NULL || !a->there || b->there
        || (g.fail_from != NULL && strcmp(from, g.fail_from) == 0)) {
        return false;
    }
    *b = *a;
    memset(a, 0, sizeof(*a));
    return true;
}

static bool fake_append(void *ctx, const char *path, const char *line)
{
    (void)ctx;
    fake_file_t *f = file(path);
    ++g.appends;
    if (g.append_fails || g.protect || f == NULL) {
        return false;
    }
    f->there = true;
    f->size += (int64_t)strlen(line) + 1;
    snprintf(f->last, sizeof(f->last), "%s", line);
    return true;
}

static const fault_log_io_t k_io = {
    .ctx      = &g,
    .mounted  = fake_mounted,
    .make_dir = fake_make,
    .size     = fake_size,
    .remove   = fake_remove,
    .rename   = fake_rename,
    .append   = fake_append,
};

/* A line of the busfault screen's longest kind: 207 characters and its
 * line feed, 208 bytes. */
static char g_line[208];

static void reset(void)
{
    memset(&g, 0, sizeof(g));
    g.mounted = true;
    memset(g_line, 'x', sizeof(g_line) - 1u);
    g_line[sizeof(g_line) - 1u] = '\0';
}

#define LINE_BYTES ((int64_t)sizeof(g_line))   /* the text and its LF */

/* ------------------------------------------------------------ the cases */

TEST_CASE(the_first_line_makes_bench_and_the_file)
{
    reset();
    bool rotated = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, &rotated), FAULT_LOG_OK);
    CHECK(!rotated);
    CHECK(g.bench);
    CHECK(g.log.there);
    CHECK_EQ(g.log.size, LINE_BYTES);
    CHECK(!g.old.there);
}

TEST_CASE(a_line_that_ends_the_file_at_the_cap_is_appended)
{
    reset();
    g.log.there = true;
    g.log.size = (int64_t)FAULT_LOG_CAP - LINE_BYTES;
    bool rotated = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, &rotated), FAULT_LOG_OK);
    CHECK(!rotated);
    CHECK_EQ(g.log.size, (int64_t)FAULT_LOG_CAP);
    CHECK_EQ(g.renames, 0);
    CHECK(!g.old.there);
}

TEST_CASE(a_line_one_byte_past_the_cap_starts_a_new_file)
{
    reset();
    g.log.there = true;
    g.log.size = (int64_t)FAULT_LOG_CAP - LINE_BYTES + 1;
    bool rotated = false;
    CHECK_EQ(fault_log_note(&k_io, g_line, &rotated), FAULT_LOG_OK);
    CHECK(rotated);
    CHECK(g.old.there);
    CHECK_EQ(g.old.size, (int64_t)FAULT_LOG_CAP - LINE_BYTES + 1);
    CHECK_EQ(g.log.size, LINE_BYTES);
}

TEST_CASE(a_file_at_the_cap_takes_no_further_line)
{
    reset();
    g.log.there = true;
    g.log.size = (int64_t)FAULT_LOG_CAP;
    bool rotated = false;
    /* The shortest line, a line feed alone, passes the cap too. */
    CHECK_EQ(fault_log_note(&k_io, "", &rotated), FAULT_LOG_OK);
    CHECK(rotated);
    CHECK_EQ(g.old.size, (int64_t)FAULT_LOG_CAP);
    CHECK_EQ(g.log.size, 1);
}

TEST_CASE(the_old_file_is_replaced_not_kept_twice)
{
    reset();
    g.old.there = true;
    g.old.size = 4242;
    snprintf(g.old.last, sizeof(g.old.last), "%s", "the oldest");
    g.log.there = true;
    g.log.size = (int64_t)FAULT_LOG_CAP;
    snprintf(g.log.last, sizeof(g.log.last), "%s", "the last of the full");
    CHECK_EQ(fault_log_note(&k_io, "new", NULL), FAULT_LOG_OK);
    CHECK_EQ(g.removes, 1);
    CHECK_STR_EQ(g.old.last, "the last of the full");
    CHECK_EQ(g.old.size, (int64_t)FAULT_LOG_CAP);
    CHECK_STR_EQ(g.log.last, "new");
    /* 2 MiB at most on the card: the two files, each at the cap or
     * under. */
    CHECK(g.old.size + g.log.size <= 2 * (int64_t)FAULT_LOG_CAP);
}

TEST_CASE(a_rename_that_fails_writes_nothing_past_the_cap)
{
    reset();
    g.log.there = true;
    g.log.size = (int64_t)FAULT_LOG_CAP - 10;
    g.rename_fails = true;
    bool rotated = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, &rotated), FAULT_LOG_NO_ROOM);
    CHECK(!rotated);
    CHECK_EQ(g.appends, 0);
    CHECK_EQ(g.log.size, (int64_t)FAULT_LOG_CAP - 10);
    /* The next line tries again: the rename is not latched off. */
    g.rename_fails = false;
    CHECK_EQ(fault_log_note(&k_io, g_line, &rotated), FAULT_LOG_OK);
    CHECK(rotated);
}

TEST_CASE(no_card_no_folder_and_a_failed_append_are_said)
{
    reset();
    g.mounted = false;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_NO_CARD);
    CHECK(!g.bench);
    reset();
    g.bench_is_file = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_NO_FOLDER);
    CHECK_EQ(g.appends, 0);
    reset();
    g.protect = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_NO_FOLDER);
    reset();
    g.append_fails = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_WRITE);
    CHECK(!g.log.there);
}

TEST_CASE(wrong_arguments_touch_nothing)
{
    reset();
    bool rotated = true;
    CHECK_EQ(fault_log_note(NULL, g_line, &rotated), FAULT_LOG_ARG);
    CHECK(!rotated);
    CHECK_EQ(fault_log_note(&k_io, NULL, NULL), FAULT_LOG_ARG);
    fault_log_io_t io = k_io;
    io.rename = NULL;
    CHECK_EQ(fault_log_note(&io, g_line, NULL), FAULT_LOG_ARG);
    CHECK(!g.bench);
    CHECK_EQ(g.appends, 0);
}

TEST_CASE(a_line_as_long_as_the_cap_fits_only_an_empty_file)
{
    static char big[FAULT_LOG_CAP + 1u];
    reset();
    memset(big, 'y', FAULT_LOG_CAP - 1u);       /* and its LF: the cap */
    big[FAULT_LOG_CAP - 1u] = '\0';
    CHECK_EQ(fault_log_note(&k_io, big, NULL), FAULT_LOG_OK);
    CHECK_EQ(g.log.size, (int64_t)FAULT_LOG_CAP);
    big[FAULT_LOG_CAP - 1u] = 'y';              /* one byte more */
    big[FAULT_LOG_CAP] = '\0';
    const int appends = g.appends;
    CHECK_EQ(fault_log_note(&k_io, big, NULL), FAULT_LOG_ARG);
    CHECK_EQ(g.appends, appends);
    CHECK_EQ(g.renames, 0);
}

TEST_CASE(a_failed_rename_of_the_full_file_keeps_the_old_one)
{
    /* The full file cannot take the name (open for a download, a read
     * error): RCBENCH.OLD comes back as it was. */
    reset();
    g.old.there = true;
    g.old.size = 5000;
    snprintf(g.old.last, sizeof(g.old.last), "%s", "the oldest");
    g.log.there = true;
    g.log.size = (int64_t)FAULT_LOG_CAP;
    g.fail_from = CARD_FAULT_LOG;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_NO_ROOM);
    CHECK(g.old.there);
    CHECK_STR_EQ(g.old.last, "the oldest");
    CHECK_EQ(g.old.size, 5000);
    CHECK(!g.tmp.there);
    CHECK_EQ(g.log.size, (int64_t)FAULT_LOG_CAP);
    CHECK_EQ(g.appends, 0);
    /* Moving the old one aside fails: nothing changes. */
    g.fail_from = CARD_FAULT_OLD;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_NO_ROOM);
    CHECK_STR_EQ(g.old.last, "the oldest");
    CHECK_EQ(g.log.size, (int64_t)FAULT_LOG_CAP);
    CHECK(!g.tmp.there);
}

TEST_CASE(a_tmp_left_by_a_power_cut_is_cleared_first)
{
    /* Cut after the old one went aside: no OLD, the TMP is it. */
    reset();
    g.tmp.there = true;
    g.tmp.size = 777;
    snprintf(g.tmp.last, sizeof(g.tmp.last), "%s", "aside");
    g.log.there = true;
    g.log.size = (int64_t)FAULT_LOG_CAP;
    snprintf(g.log.last, sizeof(g.log.last), "%s", "full");
    CHECK_EQ(fault_log_note(&k_io, "next", NULL), FAULT_LOG_OK);
    CHECK_STR_EQ(g.old.last, "full");
    CHECK(!g.tmp.there);
    CHECK_STR_EQ(g.log.last, "next");
    /* Cut before the TMP was removed: an OLD stands, the TMP goes. */
    reset();
    g.tmp.there = true;
    g.tmp.size = 777;
    g.old.there = true;
    g.old.size = (int64_t)FAULT_LOG_CAP;
    snprintf(g.old.last, sizeof(g.old.last), "%s", "kept");
    g.log.there = true;
    g.log.size = 10;
    CHECK_EQ(fault_log_note(&k_io, "a line", NULL), FAULT_LOG_OK);
    CHECK(!g.tmp.there);             /* gone before the line */
    CHECK_STR_EQ(g.old.last, "kept");
    CHECK_STR_EQ(g.log.last, "a line");
}

TEST_CASE(a_tmp_the_card_does_not_remove_stops_the_lines_until_it_goes)
{
    /* The rotation goes through, the old OLD does not go: the line is not
     * written, and no line is until the TMP is gone, so the card never
     * holds more than the OLD and the TMP. */
    reset();
    g.old.there = true;
    g.old.size = (int64_t)FAULT_LOG_CAP;
    g.log.there = true;
    g.log.size = (int64_t)FAULT_LOG_CAP;
    snprintf(g.log.last, sizeof(g.log.last), "%s", "full");
    g.remove_fails = true;
    CHECK_EQ(fault_log_note(&k_io, "one", NULL), FAULT_LOG_NO_ROOM);
    CHECK_STR_EQ(g.old.last, "full");
    CHECK(g.tmp.there);
    CHECK(!g.log.there);
    CHECK_EQ(fault_log_note(&k_io, "two", NULL), FAULT_LOG_NO_ROOM);
    CHECK_EQ(g.appends, 0);
    CHECK(g.old.size + g.tmp.size <= 2 * (int64_t)FAULT_LOG_CAP);
    /* The card takes the removal again: the next line clears it first. */
    g.remove_fails = false;
    CHECK_EQ(fault_log_note(&k_io, "three", NULL), FAULT_LOG_OK);
    CHECK(!g.tmp.there);
    CHECK_STR_EQ(g.log.last, "three");
    CHECK_EQ(g.log.size, 6);
}

TEST_CASE(a_size_that_does_not_read_takes_no_line)
{
    reset();
    g.log.there = true;
    g.log.unread = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_UNREAD);
    CHECK_EQ(g.appends, 0);
    /* The OLD's size does not read at a rotation: the full file stays. */
    reset();
    g.log.there = true;
    g.log.size = (int64_t)FAULT_LOG_CAP;
    g.old.there = true;
    g.old.unread = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_UNREAD);
    CHECK_EQ(g.renames, 0);
    CHECK_EQ(g.appends, 0);
    /* A TMP whose size, or whose OLD's size, does not read: no line. */
    reset();
    g.tmp.there = true;
    g.tmp.unread = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_UNREAD);
    g.tmp.unread = false;
    g.old.there = true;
    g.old.unread = true;
    CHECK_EQ(fault_log_note(&k_io, g_line, NULL), FAULT_LOG_UNREAD);
    CHECK_EQ(g.appends, 0);
    CHECK_EQ(g.removes, 0);
    /* A file larger than INT32_MAX reads as INT32_MAX: past the cap. */
    reset();
    g.log.there = true;
    g.log.size = (int64_t)INT32_MAX + 5;
    bool rotated = false;
    CHECK_EQ(fault_log_note(&k_io, g_line, &rotated), FAULT_LOG_OK);
    CHECK(rotated);
    CHECK_EQ(g.log.size, LINE_BYTES);
}

int main(void)
{
    RUN(the_first_line_makes_bench_and_the_file);
    RUN(a_line_that_ends_the_file_at_the_cap_is_appended);
    RUN(a_line_one_byte_past_the_cap_starts_a_new_file);
    RUN(a_file_at_the_cap_takes_no_further_line);
    RUN(the_old_file_is_replaced_not_kept_twice);
    RUN(a_rename_that_fails_writes_nothing_past_the_cap);
    RUN(no_card_no_folder_and_a_failed_append_are_said);
    RUN(wrong_arguments_touch_nothing);
    RUN(a_line_as_long_as_the_cap_fits_only_an_empty_file);
    RUN(a_failed_rename_of_the_full_file_keeps_the_old_one);
    RUN(a_tmp_left_by_a_power_cut_is_cleared_first);
    RUN(a_tmp_the_card_does_not_remove_stops_the_lines_until_it_goes);
    RUN(a_size_that_does_not_read_takes_no_line);
    return test_summary("fault_log");
}
