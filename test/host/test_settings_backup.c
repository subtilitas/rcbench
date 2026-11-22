/*
 * The settings files on the card (shared/settings/settings_backup.c) over a
 * folder in memory: SAVE SETTINGS' file, the rotation of the automatic
 * copies, a full card, a card that takes no file, no card, the list and
 * the newest copy, a read, and a request that waits while the card's job
 * lock is held by FILES or a write of the Wi-Fi page.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "sbk_card_rig.h"
#include "settings_backup.h"

/* A file's text: a header, its date and a mark. */
static char g_t[256];

static size_t text(const char *written, const char *mark)
{
    return (size_t)snprintf(g_t, sizeof(g_t),
                            "rcbench settings, format 1\nPanel: 0.23.15\n"
                            "Written: %s\n# %s\nend\n", written, mark);
}

static bool holds(const char *name, const char *mark)
{
    const fake_file_t *f = find(name);
    return f != NULL && strstr(f->data, mark) != NULL;
}

static int files(void)
{
    int n = 0;
    for (int i = 0; i < FAKE_FILES; ++i) {
        n += g_fake.f[i].used ? 1 : 0;
    }
    return n;
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(save_settings_replaces_its_one_file)
{
    fake_fresh();
    CHECK_EQ(sb_write_saved(&k_io, g_t, text("not known", "first")), SB_OK);
    CHECK(holds(SB_SAVED, "first"));
    CHECK_EQ(sb_write_saved(&k_io, g_t, text("not known", "second")), SB_OK);
    CHECK(holds(SB_SAVED, "second"));
    CHECK_EQ(files(), 1);
    CHECK(find(SB_SAVED_TMP) == NULL);
}

TEST_CASE(the_automatic_copies_rotate_and_keep_the_last_three)
{
    fake_fresh();
    static const char *const marks[] = { "c1", "c2", "c3", "c4", "c5" };
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(sb_write_auto(&k_io, g_t, text("not known", marks[i])),
                 SB_OK);
        /* After copy i the newest is AUTO1, and AUTOn the one n - 1
         * before it, as far as there are copies. */
        for (int k = 1; k <= SB_AUTO_COPIES; ++k) {
            const int src = i - (k - 1);
            if (src >= 0) {
                CHECK(holds(sb_auto_name(k), marks[src]));
            } else {
                CHECK(find(sb_auto_name(k)) == NULL);
            }
        }
    }
    CHECK_EQ(files(), SB_AUTO_COPIES);
    CHECK(find(SB_AUTO_TMP) == NULL);
    CHECK(sb_auto_name(0) == NULL);
    CHECK(sb_auto_name(SB_AUTO_COPIES + 1) == NULL);
    /* A gap moves on with the copies. */
    CHECK_EQ(f_remove(NULL, "AUTO2.TXT"), SB_IO_OK);
    CHECK_EQ(sb_write_auto(&k_io, g_t, text("not known", "c6")), SB_OK);
    CHECK(holds("AUTO1.TXT", "c6"));
    CHECK(holds("AUTO2.TXT", "c5"));
    CHECK(find("AUTO3.TXT") == NULL);
}

TEST_CASE(a_full_card_or_a_failed_write_leaves_the_copies_as_they_were)
{
    fake_fresh();
    CHECK_EQ(sb_write_auto(&k_io, g_t, text("not known", "c1")), SB_OK);
    CHECK_EQ(sb_write_auto(&k_io, g_t, text("not known", "c2")), SB_OK);
    CHECK_EQ(sb_write_saved(&k_io, g_t, text("not known", "s1")), SB_OK);
    /* Room for what is there and one byte less than another file. */
    const size_t len = text("not known", "c3");
    g_fake.room = used_bytes() + len - 1u;
    CHECK_EQ(sb_write_auto(&k_io, g_t, len), SB_FULL);
    CHECK_EQ(sb_write_saved(&k_io, g_t, len), SB_FULL);
    CHECK(holds("AUTO1.TXT", "c2"));
    CHECK(holds("AUTO2.TXT", "c1"));
    CHECK(holds(SB_SAVED, "s1"));
    CHECK(find(SB_AUTO_TMP) == NULL);
    CHECK(find(SB_SAVED_TMP) == NULL);
    /* Exactly the room: written. */
    g_fake.room = used_bytes() + len;
    CHECK_EQ(sb_write_auto(&k_io, g_t, len), SB_OK);
    CHECK(holds("AUTO1.TXT", "c3"));
    /* A card that takes no file. */
    g_fake.room = 100000u;
    g_fake.deny = true;
    CHECK_EQ(sb_write_auto(&k_io, g_t, text("not known", "c4")), SB_DENIED);
    CHECK_EQ(sb_write_saved(&k_io, g_t, text("not known", "s2")), SB_DENIED);
    CHECK(holds("AUTO1.TXT", "c3"));
    g_fake.deny = false;
    /* A rename that fails is a failed write. */
    g_fake.fail_rename = 1;
    CHECK_EQ(sb_write_auto(&k_io, g_t, text("not known", "c4")), SB_WRITE);
    g_fake.fail_rename = 1;
    CHECK_EQ(sb_write_saved(&k_io, g_t, text("not known", "s2")), SB_WRITE);
    /* A .TMP a cut write left is written over. */
    CHECK_EQ(f_write(NULL, SB_AUTO_TMP, "x", 1u), SB_IO_OK);
    CHECK_EQ(sb_write_auto(&k_io, g_t, text("not known", "c5")), SB_OK);
    CHECK(holds("AUTO1.TXT", "c5"));
}

TEST_CASE(no_card_writes_nothing_and_lists_nothing)
{
    fake_fresh();
    g_fake.mounted = false;
    CHECK_EQ(sb_write_auto(&k_io, g_t, text("x", "c1")), SB_NO_CARD);
    CHECK_EQ(sb_write_saved(&k_io, g_t, text("x", "s1")), SB_NO_CARD);
    CHECK_EQ(g_fake.writes, 0u);
    sb_entry_t e[SB_FILES];
    uint8_t n = 9u;
    CHECK_EQ(sb_list(&k_io, e, &n), SB_NO_CARD);
    CHECK_EQ(n, 0);
    static char buf[SF_FILE_MAX + 1u];
    size_t len = 7u;
    CHECK_EQ(sb_read(&k_io, SB_SAVED, buf, sizeof(buf), &len), SB_NO_CARD);
    CHECK_EQ(len, 0u);
    /* An io without a function is no card at all. */
    sb_io_t half = k_io;
    half.rename = NULL;
    CHECK_EQ(sb_write_auto(&half, g_t, 10u), SB_ARG);
    CHECK_EQ(sb_write_auto(NULL, g_t, 10u), SB_ARG);
    g_fake.mounted = true;
    CHECK_EQ(sb_write_auto(&k_io, NULL, 10u), SB_ARG);
    CHECK_EQ(sb_write_auto(&k_io, g_t, 0u), SB_ARG);
    CHECK_EQ(sb_write_auto(&k_io, g_t, SF_FILE_MAX + 1u), SB_ARG);
}

TEST_CASE(the_list_reads_each_file_s_first_lines_and_names_the_newest)
{
    fake_fresh();
    CHECK_EQ(sb_write_auto(&k_io, g_t,
                           text("2026-11-20 10:00:00 UTC", "c1")), SB_OK);
    CHECK_EQ(sb_write_auto(&k_io, g_t,
                           text("2026-11-21 09:00:00 UTC", "c2")), SB_OK);
    CHECK_EQ(sb_write_saved(&k_io, g_t,
                            text("2026-11-21 12:00:00 UTC", "s1")), SB_OK);
    sb_entry_t e[SB_FILES];
    uint8_t n = 0u;
    CHECK_EQ(sb_list(&k_io, e, &n), SB_OK);
    CHECK_EQ(n, 3);
    CHECK_STR_EQ(e[0].name, "AUTO1.TXT");
    CHECK_STR_EQ(e[1].name, "AUTO2.TXT");
    CHECK_STR_EQ(e[2].name, SB_SAVED);
    CHECK_EQ(e[0].err, SF_OK);
    CHECK_STR_EQ(e[0].head.written, "2026-11-21 09:00:00 UTC");
    CHECK_STR_EQ(e[0].head.version, "0.23.15");
    /* The latest date wins. */
    CHECK_EQ(sb_newest(e, n), 2);
    /* Without dates, the list's order: AUTO1 first. */
    e[2].head.written[0] = '\0';
    CHECK_EQ(sb_newest(e, n), 0);
    /* A file that is no settings file is listed and not the newest. */
    CHECK_EQ(f_write(NULL, "AUTO1.TXT", "hello\n", 6u), SB_IO_OK);
    CHECK_EQ(sb_list(&k_io, e, &n), SB_OK);
    CHECK_EQ(e[0].err, SF_ERR_NOT_SETTINGS);
    CHECK_EQ(sb_newest(e, n), 2);
    /* A file that does not read. */
    g_fake.fail_read = true;
    CHECK_EQ(sb_list(&k_io, e, &n), SB_OK);
    CHECK_EQ(n, 3);
    CHECK_EQ(e[1].err, SF_ERR_ARG);
    CHECK_EQ(sb_newest(e, n), -1);
    CHECK_EQ(sb_newest(NULL, 3), -1);
    /* An empty folder. */
    fake_fresh();
    CHECK_EQ(sb_list(&k_io, e, &n), SB_OK);
    CHECK_EQ(n, 0);
    CHECK_EQ(sb_newest(e, n), -1);
    CHECK_EQ(sb_list(&k_io, NULL, &n), SB_ARG);
}

TEST_CASE(a_read_takes_one_of_the_folder_s_files_whole)
{
    fake_fresh();
    static char buf[SF_FILE_MAX + 1u];
    size_t len = 0u;
    const size_t n = text("x", "s1");
    CHECK_EQ(sb_write_saved(&k_io, g_t, n), SB_OK);
    CHECK_EQ(sb_read(&k_io, SB_SAVED, buf, sizeof(buf), &len), SB_OK);
    CHECK_EQ(len, n);
    CHECK_STR_EQ(buf, g_t);
    CHECK_EQ(sb_read(&k_io, "AUTO1.TXT", buf, sizeof(buf), &len), SB_MISSING);
    /* Only the folder's names, and room for SF_FILE_MAX. */
    CHECK_EQ(sb_read(&k_io, "../X.TXT", buf, sizeof(buf), &len), SB_ARG);
    CHECK_EQ(sb_read(&k_io, SB_AUTO_TMP, buf, sizeof(buf), &len), SB_ARG);
    CHECK_EQ(sb_read(&k_io, SB_SAVED, buf, SF_FILE_MAX, &len), SB_ARG);
    CHECK_EQ(sb_read(&k_io, NULL, buf, sizeof(buf), &len), SB_ARG);
    g_fake.fail_read = true;
    CHECK_EQ(sb_read(&k_io, SB_SAVED, buf, sizeof(buf), &len), SB_READ);
}

/* A size of SF_FILE_MAX reads, one byte more is too large. */
static int32_t big_size;

static int32_t f_big_size(void *ctx, const char *name)
{
    (void)ctx;
    (void)name;
    return big_size;
}

static int32_t f_big_read(void *ctx, const char *name, char *buf, size_t cap)
{
    (void)ctx;
    (void)name;
    const size_t n = ((size_t)big_size < cap) ? (size_t)big_size : cap;
    memset(buf, 'a', n);
    return (int32_t)n;
}

TEST_CASE(a_file_of_sf_file_max_reads_and_one_byte_more_does_not)
{
    fake_fresh();
    sb_io_t io = k_io;
    io.size = f_big_size;
    io.read = f_big_read;
    static char buf[SF_FILE_MAX + 1u];
    size_t len = 0u;
    big_size = (int32_t)SF_FILE_MAX;
    CHECK_EQ(sb_read(&io, SB_SAVED, buf, sizeof(buf), &len), SB_OK);
    CHECK_EQ(len, (size_t)SF_FILE_MAX);
    CHECK_EQ(buf[SF_FILE_MAX], '\0');
    big_size = (int32_t)SF_FILE_MAX + 1;
    CHECK_EQ(sb_read(&io, SB_SAVED, buf, sizeof(buf), &len), SB_LARGE);
}

TEST_CASE(a_request_runs_and_every_request_gets_an_answer)
{
    fake_fresh();
    static char buf[SF_FILE_MAX + 1u];
    sb_req_t q;
    sb_done_t d;
    memset(&q, 0, sizeof(q));
    const size_t n = text("x", "r1");
    memcpy(buf, g_t, n + 1u);
    q.op = SB_REQ_AUTO;
    q.len = n;
    sb_run(&k_io, &q, buf, sizeof(buf), &d);
    CHECK_EQ(d.op, SB_REQ_AUTO);
    CHECK_EQ(d.result, SB_OK);
    q.op = SB_REQ_SAVE;
    sb_run(&k_io, &q, buf, sizeof(buf), &d);
    CHECK_EQ(d.result, SB_OK);
    q.op = SB_REQ_LIST;
    sb_run(&k_io, &q, buf, sizeof(buf), &d);
    CHECK_EQ(d.result, SB_OK);
    CHECK_EQ(d.count, 2);
    q.op = SB_REQ_READ;
    snprintf(q.name, sizeof(q.name), "%s", "AUTO1.TXT");
    sb_run(&k_io, &q, buf, sizeof(buf), &d);
    CHECK_EQ(d.result, SB_OK);
    CHECK_EQ(d.len, n);
    CHECK_STR_EQ(d.name, "AUTO1.TXT");
    q.op = SB_REQ_SAVE;
    q.len = sizeof(buf);
    sb_run(&k_io, &q, buf, sizeof(buf), &d);
    CHECK_EQ(d.result, SB_ARG);
    q.op = SB_REQ_NONE;
    sb_run(&k_io, &q, buf, sizeof(buf), &d);
    CHECK_EQ(d.result, SB_ARG);
    sb_run(&k_io, NULL, buf, sizeof(buf), &d);
    CHECK_EQ(d.result, SB_ARG);
    sb_run(&k_io, &q, buf, sizeof(buf), NULL);
}

TEST_CASE(a_request_waits_while_the_card_job_lock_is_held)
{
    fake_fresh();
    static char buf[SF_FILE_MAX + 1u];
    CHECK_EQ(sb_write_auto(&k_io, g_t, text("x", "old")), SB_OK);
    sb_req_t q;
    memset(&q, 0, sizeof(q));
    q.len = text("x", "new");
    memcpy(buf, g_t, q.len + 1u);
    q.op = SB_REQ_AUTO;
    sb_done_t d;
    memset(&d, 0xA5, sizeof(d));
    g_fake.held = true;
    const unsigned writes = g_fake.writes;
    CHECK(!sb_serve(&k_io, &q, buf, sizeof(buf), &d));
    /* Nothing ran and the answer is untouched. */
    CHECK_EQ(g_fake.writes, writes);
    CHECK(holds("AUTO1.TXT", "old"));
    CHECK(find("AUTO2.TXT") == NULL);
    CHECK_EQ(d.op, 0xA5);
    CHECK_EQ(g_fake.unlocks, 0u);
    /* Given back: the next pass runs it, under the lock, and gives it back. */
    g_fake.held = false;
    CHECK(sb_serve(&k_io, &q, buf, sizeof(buf), &d));
    CHECK_EQ(d.op, SB_REQ_AUTO);
    CHECK_EQ(d.result, SB_OK);
    CHECK(holds("AUTO1.TXT", "new"));
    CHECK(holds("AUTO2.TXT", "old"));
    CHECK_EQ(g_fake.locks, 1u);
    CHECK_EQ(g_fake.unlocks, 1u);
    /* Without a lock in the table the request runs at once. */
    sb_io_t bare = k_io;
    bare.lock = NULL;
    bare.unlock = NULL;
    g_fake.held = true;
    q.op = SB_REQ_LIST;
    CHECK(sb_serve(&bare, &q, buf, sizeof(buf), &d));
    CHECK_EQ(d.result, SB_OK);
    CHECK_EQ(d.count, 2);
}

/*
 * An editor's save of AUTO1.TXT, as FILES' and the Wi-Fi page's replace
 * runs it under the card's job lock: the file's size checked against the
 * one it read, the file removed, its text written.  An automatic copy
 * asked between the check and the write must not run there: it would put
 * the new copy at AUTO1.TXT, of the same size, and the editor's remove
 * would take it.
 */
TEST_CASE(a_copy_asked_during_an_editor_save_keeps_the_newest_copy)
{
    fake_fresh();
    static char buf[SF_FILE_MAX + 1u];
    CHECK_EQ(sb_write_auto(&k_io, g_t, text("x", "AA")), SB_OK);
    const int32_t read_size = f_size(NULL, "AUTO1.TXT");
    /* The editor takes the lock and checks the size. */
    g_fake.held = true;
    CHECK_EQ(f_size(NULL, "AUTO1.TXT"), read_size);
    /* The copy is asked now, of the same size as the file the editor
     * read. */
    sb_req_t q;
    memset(&q, 0, sizeof(q));
    q.op = SB_REQ_AUTO;
    q.len = text("x", "BB");
    memcpy(buf, g_t, q.len + 1u);
    CHECK_EQ((int32_t)q.len, read_size);
    sb_done_t d;
    const bool ran_first = sb_serve(&k_io, &q, buf, sizeof(buf), &d);
    /* The editor's replace: the size checked once more, remove, write. */
    CHECK_EQ(f_size(NULL, "AUTO1.TXT"), read_size);
    CHECK_EQ(f_remove(NULL, "AUTO1.TXT"), SB_IO_OK);
    const size_t e = text("x", "EE");
    CHECK_EQ(f_write(NULL, "AUTO1.TXT", g_t, e), SB_IO_OK);
    g_fake.held = false;
    /* The caller asks again at its next pass while no answer came. */
    if (!ran_first) {
        CHECK(sb_serve(&k_io, &q, buf, sizeof(buf), &d));
    }
    CHECK_EQ(d.result, SB_OK);
    /* The copy is the newest, the editor's text the one before it. */
    CHECK(holds("AUTO1.TXT", "BB"));
    CHECK(holds("AUTO2.TXT", "EE"));
}

int main(void)
{
    RUN(save_settings_replaces_its_one_file);
    RUN(the_automatic_copies_rotate_and_keep_the_last_three);
    RUN(a_full_card_or_a_failed_write_leaves_the_copies_as_they_were);
    RUN(no_card_writes_nothing_and_lists_nothing);
    RUN(the_list_reads_each_file_s_first_lines_and_names_the_newest);
    RUN(a_read_takes_one_of_the_folder_s_files_whole);
    RUN(a_file_of_sf_file_max_reads_and_one_byte_more_does_not);
    RUN(a_request_runs_and_every_request_gets_an_answer);
    RUN(a_request_waits_while_the_card_job_lock_is_held);
    RUN(a_copy_asked_during_an_editor_save_keeps_the_newest_copy);
    return test_summary("settings_backup");
}
