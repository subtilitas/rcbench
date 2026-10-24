/*
 * Listing the card for the Wi-Fi page.
 *
 * The failures under test: a file the page never lists because a fixed
 * table was full, a listing in another order on the second walk (the ZIP
 * walks three times and needs the same entries each time), a walk that
 * holds more than a folder handle a level or reads a folder's entries more
 * than once a pass, a name the page could not link listed anyway, a pulled
 * card that hangs the walk instead of ending it, and a walk that goes on
 * after the server is asked to stop.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "wa_fakefs.h"
#include "wa_walk.h"

/* The walk written out as one line an event: "D path" or "F path size". */
static char g_log[1 << 16];

static wa_walk_ev_t walk_log(fake_fs_t *fs, unsigned long *left_out)
{
    const wa_fs_t w = fake_wa(fs);
    wa_walk_t wk;
    wa_walk_begin(&wk, &w);
    size_t at = 0u;
    g_log[0] = '\0';
    for (;;) {
        const wa_walk_ev_t ev = wa_walk_next(&wk);
        if (ev == WA_WALK_DIR) {
            at += (size_t)snprintf(g_log + at, sizeof(g_log) - at, "D %s\n",
                                   wk.path);
        } else if (ev == WA_WALK_FILE) {
            at += (size_t)snprintf(g_log + at, sizeof(g_log) - at,
                                   "F %s %llu\n", wk.path,
                                   (unsigned long long)wk.ent.size);
        } else {
            if (left_out != NULL) {
                *left_out = wk.left_out;
            }
            CHECK_EQ(wa_walk_next(&wk), ev);   /* stays over */
            wa_walk_end(&wk);
            CHECK_EQ(fs->dirs_open, 0);
            return ev;
        }
    }
}

TEST_CASE(an_empty_card_is_the_root_alone)
{
    fake_fs_t fs;
    fake_init(&fs);
    unsigned long left = 9u;
    CHECK_EQ(walk_log(&fs, &left), WA_WALK_END);
    CHECK_STR_EQ(g_log, "D \n");
    CHECK_EQ(left, 0u);
    fake_free(&fs);
}

TEST_CASE(no_card_is_said_as_such)
{
    fake_fs_t fs;
    fake_init(&fs);
    fs.pulled = true;
    CHECK_EQ(walk_log(&fs, NULL), WA_WALK_NO_CARD);
    CHECK_STR_EQ(g_log, "");
    wa_walk_t wk;
    wa_walk_begin(&wk, NULL);
    CHECK_EQ(wa_walk_next(&wk), WA_WALK_NO_CARD);
    fake_free(&fs);
}

TEST_CASE(each_folder_lists_its_files_then_its_folders)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "logs", true, 0u, 0);
    fake_add(&fs, "README.TXT", false, 12u, 0);
    fake_add(&fs, "logs/2026", true, 0u, 0);
    fake_add(&fs, "logs/run 1.csv", false, 100u, 0);
    fake_add(&fs, "logs/2026/m\xc3\xbcller run.csv", false, 0u, 0);
    fake_add(&fs, "logs/2026/deep", true, 0u, 0);
    fake_add(&fs, "logs/2026/deep/x", false, 1u, 0);
    fake_add(&fs, "art", true, 0u, 0);
    fake_add(&fs, "art/a.bin", false, 5u, 0);
    fake_add(&fs, "zz.txt", false, 3u, 0);
    CHECK_EQ(walk_log(&fs, NULL), WA_WALK_END);
    CHECK_STR_EQ(g_log,
                 "D \n"
                 "F README.TXT 12\n"
                 "F zz.txt 3\n"
                 "D logs\n"
                 "F logs/run 1.csv 100\n"
                 "D logs/2026\n"
                 "F logs/2026/m\xc3\xbcller run.csv 0\n"
                 "D logs/2026/deep\n"
                 "F logs/2026/deep/x 1\n"
                 "D art\n"
                 "F art/a.bin 5\n");
    CHECK_EQ(fs.dirs_open_max, 4);     /* the root, logs, 2026, deep */
    /* A second walk of the same card is the same walk. */
    char first[sizeof(g_log)];
    strcpy(first, g_log);
    CHECK_EQ(walk_log(&fs, NULL), WA_WALK_END);
    CHECK_STR_EQ(g_log, first);
    fake_free(&fs);
}

TEST_CASE(names_the_page_could_not_serve_are_counted_not_listed)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "good.csv", false, 1u, 0);
    fake_add(&fs, "bad\xff.csv", false, 1u, 0);         /* not UTF-8 */
    fake_add(&fs, "tab\there", false, 1u, 0);           /* control */
    fake_add(&fs, "back\\slash", false, 1u, 0);
    char long_name[300];
    memset(long_name, 'L', 256u);
    long_name[256] = '\0';
    fake_add(&fs, long_name, false, 1u, 0);             /* 256 units */
    long_name[255] = '\0';
    fake_add(&fs, long_name, false, 1u, 0);             /* 255: fine */
    unsigned long left = 0u;
    CHECK_EQ(walk_log(&fs, &left), WA_WALK_END);
    CHECK_EQ(left, 4u);
    CHECK(strstr(g_log, "F good.csv 1\n") != NULL);
    CHECK(strstr(g_log, long_name) != NULL);
    fake_free(&fs);
}

TEST_CASE(paths_over_384_bytes_and_folders_past_16_deep_are_counted)
{
    fake_fs_t fs;
    fake_init(&fs);
    /* Two 191-byte folder names: "a/b" is 383 bytes, a file "x" in it 385. */
    char a[200], b[200];
    memset(a, 'a', 191u);
    a[191] = '\0';
    memset(b, 'b', 191u);
    b[191] = '\0';
    const int na = fake_add_in(&fs, 0, a, true, 0u, 0);
    const int nb = fake_add_in(&fs, na, b, true, 0u, 0);
    fake_add_in(&fs, nb, "x", false, 1u, 0);
    fake_add_in(&fs, na, "y", false, 1u, 0);           /* 193 bytes: fine */
    /* Folders 17 deep, a file in each. */
    int p = 0;
    for (int d = 1; d <= 17; ++d) {
        p = fake_add_in(&fs, p, "d", true, 0u, 0);
        fake_add_in(&fs, p, "f", false, 1u, 0);
    }
    unsigned long left = 0u;
    CHECK_EQ(walk_log(&fs, &left), WA_WALK_END);
    CHECK_EQ(left, 2u);                 /* "a/b/x" and the 17th "d" */
    CHECK(strstr(g_log, "/y 1\n") != NULL);
    /* The 16th folder and its file are listed, nothing below. */
    CHECK(strstr(g_log, "F d/d/d/d/d/d/d/d/d/d/d/d/d/d/d/d/f 1\n") != NULL);
    CHECK(strstr(g_log, "d/d/d/d/d/d/d/d/d/d/d/d/d/d/d/d/d") == NULL);
    CHECK_EQ(fs.dirs_open_max, (int)WA_WALK_DEPTH + 1);
    fake_free(&fs);
}

TEST_CASE(many_files_are_all_listed_in_the_same_memory)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int dir = fake_add_in(&fs, 0, "many", true, 0u, 0);
    char name[16];
    for (int i = 0; i < 70000; ++i) {
        snprintf(name, sizeof(name), "F%05d.CSV", i);
        fake_add_in(&fs, dir, name, false, (uint64_t)i, 0);
    }
    for (int i = 0; i < 300; ++i) {
        snprintf(name, sizeof(name), "d%03d", i);
        const int d = fake_add_in(&fs, 0, name, true, 0u, 0);
        fake_add_in(&fs, d, "one", false, 1u, 0);
    }
    const wa_fs_t w = fake_wa(&fs);
    wa_walk_t wk;
    wa_walk_begin(&wk, &w);
    unsigned long files = 0u, dirs = 0u;
    uint64_t sum = 0u;
    wa_walk_ev_t ev;
    while ((ev = wa_walk_next(&wk)) == WA_WALK_DIR || ev == WA_WALK_FILE) {
        if (ev == WA_WALK_FILE) {
            ++files;
            sum += wk.ent.size;
        } else {
            ++dirs;
        }
    }
    CHECK_EQ(ev, WA_WALK_END);
    CHECK_EQ(files, 70300u);
    CHECK_EQ(dirs, 302u);
    CHECK_EQ(sum, 69999ull * 70000ull / 2ull + 300ull);
    CHECK_EQ(wk.left_out, 0u);
    CHECK_EQ(fs.dirs_open_max, 2);
    CHECK_EQ(fs.dirs_open, 0);
    fake_free(&fs);
}

TEST_CASE(a_card_pulled_part_way_ends_the_walk_with_an_error)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "a", true, 0u, 0);
    fake_add(&fs, "a/1", false, 1u, 0);
    fake_add(&fs, "b", true, 0u, 0);
    fake_add(&fs, "b/2", false, 1u, 0);
    const wa_fs_t w = fake_wa(&fs);
    for (int cut = 1; cut <= 5; ++cut) {   /* 5 events before the end */
        wa_walk_t wk;
        wa_walk_begin(&wk, &w);
        fs.pulled = false;
        wa_walk_ev_t ev = WA_WALK_DIR;
        for (int k = 0; k < cut; ++k) {
            ev = wa_walk_next(&wk);
        }
        CHECK(ev == WA_WALK_DIR || ev == WA_WALK_FILE);
        /* cppcheck-suppress redundantAssignment
         * the walk reads through its own pointer to fs */
        fs.pulled = true;
        ev = wa_walk_next(&wk);
        CHECK_EQ(ev, WA_WALK_ERROR);
        CHECK_EQ(wa_walk_next(&wk), WA_WALK_ERROR);
        CHECK_EQ(fs.dirs_open, 0);
    }
    /* Given up part way: the handles are closed. */
    /* cppcheck-suppress redundantAssignment
     * the walk reads through its own pointer to fs */
    fs.pulled = false;
    wa_walk_t wk;
    wa_walk_begin(&wk, &w);
    CHECK_EQ(wa_walk_next(&wk), WA_WALK_DIR);    /* the root */
    CHECK_EQ(fs.dirs_open, 1);
    CHECK_EQ(wa_walk_next(&wk), WA_WALK_DIR);    /* a, the root's open */
    CHECK_EQ(fs.dirs_open, 2);
    wa_walk_end(&wk);
    CHECK_EQ(fs.dirs_open, 0);
    fake_free(&fs);
}

TEST_CASE(each_entry_is_read_once_a_pass)
{
    /* 300 folders of one file each in the root: each folder is read twice,
     * its files' pass and its folders', each pass its entries and the end.
     * A walk that opened the root again after each folder and skipped what
     * it had done read about 45,000 entries more. */
    fake_fs_t fs;
    fake_init(&fs);
    char name[16];
    for (int i = 0; i < 300; ++i) {
        snprintf(name, sizeof(name), "d%03d", i);
        const int d = fake_add_in(&fs, 0, name, true, 0u, 0);
        fake_add_in(&fs, d, "one", false, 1u, 0);
    }
    CHECK_EQ(walk_log(&fs, NULL), WA_WALK_END);
    CHECK_EQ(fs.readdirs, 2L * (300 + 1) + 300L * 2L * (1 + 1));
    CHECK_EQ(fs.dirs_open_max, 2);
    fake_free(&fs);
}

TEST_CASE(a_walk_asked_to_stop_ends_at_the_next_entry)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_bench_tree(&fs);
    const wa_fs_t w = fake_wa(&fs);
    /* Stopped after every count of reads the walk makes: it ends at once
     * with an error, reads nothing more, and holds no handle. */
    fs.readdirs = 0;
    CHECK_EQ(walk_log(&fs, NULL), WA_WALK_END);
    const long total = fs.readdirs;
    CHECK(total > 20);
    for (long cut = 1; cut <= total; ++cut) {
        fs.readdirs = 0;
        fs.stop_after = cut;
        wa_walk_t wk;
        wa_walk_begin(&wk, &w);
        wa_walk_ev_t ev;
        do {
            ev = wa_walk_next(&wk);
        } while (ev == WA_WALK_DIR || ev == WA_WALK_FILE);
        CHECK_EQ(ev, (cut < total) ? WA_WALK_ERROR : WA_WALK_END);
        CHECK_EQ(fs.readdirs, cut);
        CHECK_EQ(fs.dirs_open, 0);
        CHECK_EQ(wa_walk_next(&wk), ev);
        CHECK_EQ(fs.readdirs, cut);
    }
    /* Stopped before the first entry: the root's DIR, then the error. */
    fs.readdirs = 0;
    /* cppcheck-suppress redundantAssignment
     * the walk reads through its own pointer to fs */
    fs.stop_after = 0;
    wa_walk_t wk;
    wa_walk_begin(&wk, &w);
    CHECK_EQ(wa_walk_next(&wk), WA_WALK_DIR);
    fs.stop_after = 1;
    fs.readdirs = 1;
    CHECK_EQ(wa_walk_next(&wk), WA_WALK_ERROR);
    CHECK_EQ(fs.dirs_open, 0);
    /* No hook: never stopped. */
    /* cppcheck-suppress redundantAssignment
     * the walk reads through its own pointer to fs */
    fs.stop_after = 0;
    /* cppcheck-suppress redundantAssignment
     * the walk reads through its own pointer to fs */
    fs.readdirs = 0;
    wa_fs_t plain = w;
    plain.stopped = NULL;
    wa_walk_begin(&wk, &plain);
    wa_walk_ev_t ev;
    do {
        ev = wa_walk_next(&wk);
    } while (ev == WA_WALK_DIR || ev == WA_WALK_FILE);
    CHECK_EQ(ev, WA_WALK_END);
    CHECK_EQ(fs.dirs_open, 0);
    fake_free(&fs);
}

TEST_CASE(the_bench_tree_is_walked_folder_by_folder)
{
    /* The panel's folders three deep, an empty one among them, beside the
     * root files and folders of firmware 0.21.0 and older. */
    fake_fs_t fs;
    fake_init(&fs);
    fake_bench_tree(&fs);
    CHECK_EQ(walk_log(&fs, NULL), WA_WALK_END);
    CHECK_STR_EQ(g_log,
                 "D \n"
                 "F BENCH041.CSV 18422\n"
                 "F SERVO040.TXT 1890\n"
                 "F RCBENCH.LOG 2080\n"
                 "D ESC\n"
                 "F ESC/hobbywing-flyfun-8item.json 3104\n"
                 "D KST\n"
                 "F KST/KST001.TXT 1310\n"
                 "D BENCH\n"
                 "F BENCH/RCBENCH.LOG 6240\n"
                 "F BENCH/RCBENCH.OLD 1048576\n"
                 "D BENCH/LOGS\n"
                 "F BENCH/LOGS/SERVO001.CSV 4410\n"
                 "F BENCH/LOGS/MOTOR002.CSV 9120\n"
                 "F BENCH/LOGS/SUPPL003.CSV 2205\n"
                 "D BENCH/ESC\n"
                 "D BENCH/ESC/CONF\n"
                 "F BENCH/ESC/CONF/hobbywing-flyfun-8item.json 3104\n"
                 "D BENCH/KST\n"
                 "D BENCH/KST/CONF\n"
                 "F BENCH/KST/CONF/KST.TXT 396\n"
                 "F BENCH/KST/CONF/Wing left.TXT 1310\n"
                 "D BENCH/HITEC\n"
                 "D BENCH/HITEC/CONF\n"
                 "D USER\n"
                 "D USER/LOGS\n"
                 "F USER/LOGS/ATEST001.CSV 15104\n"
                 "F USER/LOGS/ATEST001.TXT 2240\n"
                 "F USER/LOGS/LIMIT002.TXT 1940\n");
    CHECK_EQ(fs.dirs_open_max, 4);     /* the root, BENCH, KST, CONF */
    fake_free(&fs);
}

int main(void)
{
    RUN(an_empty_card_is_the_root_alone);
    RUN(no_card_is_said_as_such);
    RUN(each_folder_lists_its_files_then_its_folders);
    RUN(names_the_page_could_not_serve_are_counted_not_listed);
    RUN(paths_over_384_bytes_and_folders_past_16_deep_are_counted);
    RUN(many_files_are_all_listed_in_the_same_memory);
    RUN(a_card_pulled_part_way_ends_the_walk_with_an_error);
    RUN(the_bench_tree_is_walked_folder_by_folder);
    RUN(each_entry_is_read_once_a_pass);
    RUN(a_walk_asked_to_stop_ends_at_the_next_entry);
    return test_summary("wa_walk");
}
