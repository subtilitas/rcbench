/*
 * The file manager's names and paths: every rule of a name a person
 * gives, with its reason, at the length limits; the ".NEW" name a save
 * writes; folder paths, joining, going up and the last name.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "fm_name.h"
#include "storage.h"
#include "wa_walk.h"

/* A name of @p n copies of @p c. */
static const char *repeat(char c, size_t n)
{
    static char buf[256];
    memset(buf, c, n);
    buf[n] = '\0';
    return buf;
}

/* A path of @p depth folders "a". */
static const char *deep(size_t depth)
{
    static char buf[256];
    size_t at = 0u;
    for (size_t k = 0u; k < depth; ++k) {
        if (k > 0u) {
            buf[at++] = '/';
        }
        buf[at++] = 'a';
    }
    buf[at] = '\0';
    return buf;
}

/* --- the limits against the card's walk ------------------------------ */

/* A name and its ".NEW" are listed; a folder path fits the walk's
 * "<drive>/<path>" buffer with the drive "0:"; the depth is the Wi-Fi
 * page's. */
TEST_CASE(the_limits_are_the_walks)
{
    CHECK_EQ(FM_LIST_NAME_MAX, STORAGE_NAME_MAX - 1);
    CHECK_EQ(FM_NAME_MAX + FM_NEW_SUFFIX_LEN, FM_LIST_NAME_MAX);
    CHECK_EQ(FM_NEW_SUFFIX_LEN, strlen(FM_NEW_SUFFIX));
    CHECK(strlen("0:/") + FM_PATH_MAX + 1u <= STORAGE_NAME_MAX * 2u);
    CHECK_EQ(FM_DEPTH_MAX, WA_WALK_DEPTH);
    CHECK(FM_PATH_MAX <= WA_PATH_MAX);
}

/* --- names ------------------------------------------------------------- */

TEST_CASE(a_plain_name_is_taken)
{
    CHECK_EQ(fm_name_check("LIMIT001.TXT"), FM_NAME_OK);
    CHECK_EQ(fm_name_check("a"), FM_NAME_OK);
    CHECK_EQ(fm_name_check("my notes 2.txt"), FM_NAME_OK);
    CHECK_EQ(fm_name_check("a.b.c"), FM_NAME_OK);
    CHECK_EQ(fm_name_check("x~!@#$%^&()_+-=[]{};',"), FM_NAME_OK);
}

TEST_CASE(an_empty_name_is_refused)
{
    CHECK_EQ(fm_name_check(NULL), FM_NAME_EMPTY);
    CHECK_EQ(fm_name_check(""), FM_NAME_EMPTY);
}

/* 59 bytes are taken, 60 are not; a German letter is 2 bytes. */
TEST_CASE(fifty_nine_bytes_are_the_most)
{
    CHECK_EQ(fm_name_check(repeat('a', FM_NAME_MAX)), FM_NAME_OK);
    CHECK_EQ(fm_name_check(repeat('a', FM_NAME_MAX + 1u)), FM_NAME_LONG);
    CHECK_EQ(fm_name_check(repeat('a', 200u)), FM_NAME_LONG);
    char name[128] = "";
    for (int k = 0; k < 29; ++k) {
        strcat(name, "\xC3\xA4");                       /* ä */
    }
    strcat(name, "a");
    CHECK_EQ(strlen(name), 59u);
    CHECK_EQ(fm_name_check(name), FM_NAME_OK);
    strcat(name, "a");
    CHECK_EQ(fm_name_check(name), FM_NAME_LONG);
}

/* The 7 German letters are taken, the rest of UTF-8 and every control is
 * not: the faces draw what the rule takes. */
TEST_CASE(the_german_letters_are_taken_and_nothing_else_outside_ascii)
{
    CHECK_EQ(fm_name_check("\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC"
                           "\xC3\x9F.txt"), FM_NAME_OK);
    CHECK_EQ(fm_name_check("Pr\xC3\xBC" "fung"), FM_NAME_OK);
    CHECK_EQ(fm_name_check("caf\xC3\xA9"), FM_NAME_CHAR);      /* é */
    CHECK_EQ(fm_name_check("a\xC3"), FM_NAME_CHAR);            /* cut */
    CHECK_EQ(fm_name_check("a\xE2\x82\xAC"), FM_NAME_CHAR);    /* € */
    CHECK_EQ(fm_name_check("a\x7F"), FM_NAME_CHAR);
    CHECK_EQ(fm_name_check("a\tb"), FM_NAME_CHAR);
    CHECK_EQ(fm_name_check("a\x1F"), FM_NAME_CHAR);
    CHECK_EQ(fm_name_check("\xFF"), FM_NAME_CHAR);
}

TEST_CASE(every_mark_fat_refuses_is_refused)
{
    const char *marks = "\"*/:<>?\\|";
    for (const char *m = marks; *m != '\0'; ++m) {
        const char name[8] = {'a', *m, 'b', '\0'};
        if (fm_name_check(name) != FM_NAME_CHAR) {
            T_FAIL("mark %c is taken", *m);
        }
    }
}

/* A full stop or a space at either end; a name of spaces. */
TEST_CASE(a_dot_or_a_space_at_an_end_is_refused)
{
    CHECK_EQ(fm_name_check(".hidden"), FM_NAME_EDGE);
    CHECK_EQ(fm_name_check("name."), FM_NAME_EDGE);
    CHECK_EQ(fm_name_check(" name"), FM_NAME_EDGE);
    CHECK_EQ(fm_name_check("name "), FM_NAME_EDGE);
    CHECK_EQ(fm_name_check("   "), FM_NAME_EDGE);
    CHECK_EQ(fm_name_check(" "), FM_NAME_EDGE);
    CHECK_EQ(fm_name_check("."), FM_NAME_EDGE);
    CHECK_EQ(fm_name_check(".."), FM_NAME_EDGE);
    /* The first rule broken is the answer: a long name of dots is long. */
    CHECK_EQ(fm_name_check(repeat('.', FM_NAME_MAX + 1u)), FM_NAME_LONG);
    CHECK_EQ(fm_name_check(". *"), FM_NAME_CHAR);
}

/* A device name alone and in front of a full stop, in any case; a longer
 * stem and COM0 are names. */
TEST_CASE(a_device_name_is_refused_alone_and_before_a_dot)
{
    static const char *const k_dev[] = {
        "CON", "prn", "Aux", "nul", "COM1", "com9", "LPT1", "lpt9",
        "CON.TXT", "nul.tar.gz", "Com5.x",
    };
    for (size_t k = 0u; k < sizeof(k_dev) / sizeof(k_dev[0]); ++k) {
        if (fm_name_check(k_dev[k]) != FM_NAME_RESERVED) {
            T_FAIL("%s is taken", k_dev[k]);
        }
    }
    static const char *const k_ok[] = {
        "CONX", "CO", "COM0", "COM10", "LPT", "XCON", "CON2", "console.txt",
        "a.CON",
    };
    for (size_t k = 0u; k < sizeof(k_ok) / sizeof(k_ok[0]); ++k) {
        if (fm_name_check(k_ok[k]) != FM_NAME_OK) {
            T_FAIL("%s is refused", k_ok[k]);
        }
    }
    /* "NUL." ends with a full stop, which is found first. */
    CHECK_EQ(fm_name_check("NUL."), FM_NAME_EDGE);
}

/* --- the save's name --------------------------------------------------- */

TEST_CASE(the_new_name_fits_the_listing)
{
    char out[FM_LIST_NAME_MAX + 1u];
    CHECK(fm_name_new("LIMIT001.TXT", out, sizeof(out)));
    CHECK_STR_EQ(out, "LIMIT001.TXT.NEW");
    CHECK(fm_name_new(repeat('a', FM_NAME_MAX), out, sizeof(out)));
    CHECK_EQ(strlen(out), FM_LIST_NAME_MAX);
    /* A computer's name of 60 to 63 bytes is listed, its .NEW would not
     * be: a save refuses it, out left as it was. */
    strcpy(out, "keep");
    CHECK(!fm_name_new(repeat('a', FM_NAME_MAX + 1u), out, sizeof(out)));
    CHECK_STR_EQ(out, "keep");
    CHECK(!fm_name_new("", out, sizeof(out)));
    CHECK(!fm_name_new(NULL, out, sizeof(out)));
    CHECK(!fm_name_new("a", NULL, 8u));
    /* Room for "a.NEW" and its terminator: 6 bytes, not 5. */
    char small[6];
    CHECK(!fm_name_new("a", small, 5u));
    CHECK(fm_name_new("a", small, 6u));
    CHECK_STR_EQ(small, "a.NEW");
    /* In place. */
    char self[16] = "x.TXT";
    CHECK(fm_name_new(self, self, sizeof(self)));
    CHECK_STR_EQ(self, "x.TXT.NEW");
}

/* --- paths ------------------------------------------------------------- */

TEST_CASE(a_folder_path_is_checked)
{
    CHECK_EQ(fm_path_check(""), FM_PATH_OK);
    CHECK_EQ(fm_path_check("USER"), FM_PATH_OK);
    CHECK_EQ(fm_path_check("USER/LOGS"), FM_PATH_OK);
    CHECK_EQ(fm_path_check("System Volume Information"), FM_PATH_OK);
    CHECK_EQ(fm_path_check(NULL), FM_PATH_ARG);
    CHECK_EQ(fm_path_check("/USER"), FM_PATH_BAD);
    CHECK_EQ(fm_path_check("USER/"), FM_PATH_BAD);
    CHECK_EQ(fm_path_check("/"), FM_PATH_BAD);
    CHECK_EQ(fm_path_check("A//B"), FM_PATH_BAD);
    CHECK_EQ(fm_path_check("A/./B"), FM_PATH_BAD);
    CHECK_EQ(fm_path_check(".."), FM_PATH_BAD);
    CHECK_EQ(fm_path_check("A/.."), FM_PATH_BAD);
    CHECK_EQ(fm_path_check("A/...b"), FM_PATH_OK);
    CHECK_EQ(fm_path_check(repeat('a', FM_PATH_MAX)), FM_PATH_OK);
    CHECK_EQ(fm_path_check(repeat('a', FM_PATH_MAX + 1u)), FM_PATH_LONG);
    CHECK_EQ(fm_path_check(deep(FM_DEPTH_MAX)), FM_PATH_OK);
    CHECK_EQ(fm_path_check(deep(FM_DEPTH_MAX + 1u)), FM_PATH_DEEP);
    CHECK_EQ(fm_path_depth(""), 0u);
    CHECK_EQ(fm_path_depth(NULL), 0u);
    CHECK_EQ(fm_path_depth("A"), 1u);
    CHECK_EQ(fm_path_depth("BENCH/KST/CONF"), 3u);
}

TEST_CASE(a_name_joins_a_folder)
{
    char out[FM_PATH_SIZE];
    CHECK_EQ(fm_path_join("", "USER", true, out, sizeof(out)), FM_PATH_OK);
    CHECK_STR_EQ(out, "USER");
    CHECK_EQ(fm_path_join("USER/LOGS", "LIMIT001.TXT", false, out,
                          sizeof(out)), FM_PATH_OK);
    CHECK_STR_EQ(out, "USER/LOGS/LIMIT001.TXT");
    /* Any name the card lists joins: the typed-name rules are not the
     * path's. */
    CHECK_EQ(fm_path_join("", "caf\xC3\xA9 *", false, out, sizeof(out)),
             FM_PATH_OK);
    CHECK_STR_EQ(out, "caf\xC3\xA9 *");

    /* In place: a pane goes down. */
    char pane[FM_PATH_SIZE] = "BENCH";
    CHECK_EQ(fm_path_join(pane, "KST", true, pane, sizeof(pane)), FM_PATH_OK);
    CHECK_STR_EQ(pane, "BENCH/KST");
    char root[FM_PATH_SIZE] = "";
    CHECK_EQ(fm_path_join(root, "USER", true, root, sizeof(root)),
             FM_PATH_OK);
    CHECK_STR_EQ(root, "USER");
}

TEST_CASE(a_join_is_refused_with_out_unchanged)
{
    char out[FM_PATH_SIZE] = "keep";
    CHECK_EQ(fm_path_join(NULL, "a", false, out, sizeof(out)), FM_PATH_ARG);
    CHECK_EQ(fm_path_join("", NULL, false, out, sizeof(out)), FM_PATH_ARG);
    CHECK_EQ(fm_path_join("", "a", false, NULL, 8u), FM_PATH_ARG);
    CHECK_EQ(fm_path_join("/x", "a", false, out, sizeof(out)), FM_PATH_BAD);
    CHECK_EQ(fm_path_join("", "", false, out, sizeof(out)), FM_PATH_BAD);
    CHECK_EQ(fm_path_join("", ".", true, out, sizeof(out)), FM_PATH_BAD);
    CHECK_EQ(fm_path_join("A", "..", true, out, sizeof(out)), FM_PATH_BAD);
    CHECK_EQ(fm_path_join("A", "b/c", false, out, sizeof(out)), FM_PATH_BAD);
    CHECK_STR_EQ(out, "keep");

    /* 120 bytes join, 121 do not: "a" * 118, "/", 1 or 2 bytes. */
    char dir[FM_PATH_SIZE];
    strcpy(dir, repeat('a', FM_PATH_MAX - 2u));
    CHECK_EQ(fm_path_join(dir, "b", false, out, sizeof(out)), FM_PATH_OK);
    CHECK_EQ(strlen(out), FM_PATH_MAX);
    strcpy(out, "keep");
    CHECK_EQ(fm_path_join(dir, "bc", false, out, sizeof(out)), FM_PATH_LONG);
    CHECK_STR_EQ(out, "keep");
    /* The out's room: a path of 3 bytes takes 4. */
    char small[4];
    CHECK_EQ(fm_path_join("a", "b", false, small, 3u), FM_PATH_ARG);
    CHECK_EQ(fm_path_join("a", "b", false, small, 4u), FM_PATH_OK);
    CHECK_STR_EQ(small, "a/b");
}

/* A folder at depth 16 takes files, not folders. */
TEST_CASE(a_folder_below_the_deepest_is_refused_a_file_is_not)
{
    char out[FM_PATH_SIZE];
    char dir[FM_PATH_SIZE];
    strcpy(dir, deep(FM_DEPTH_MAX - 1u));
    CHECK_EQ(fm_path_join(dir, "a", true, out, sizeof(out)), FM_PATH_OK);
    CHECK_EQ(fm_path_depth(out), FM_DEPTH_MAX);
    strcpy(dir, deep(FM_DEPTH_MAX));
    CHECK_EQ(fm_path_join(dir, "a", true, out, sizeof(out)), FM_PATH_DEEP);
    CHECK_EQ(fm_path_join(dir, "f.txt", false, out, sizeof(out)),
             FM_PATH_OK);
    strcpy(dir, deep(FM_DEPTH_MAX + 1u));
    CHECK_EQ(fm_path_join(dir, "f.txt", false, out, sizeof(out)),
             FM_PATH_DEEP);
}

TEST_CASE(up_and_the_last_name)
{
    char p[FM_PATH_SIZE] = "BENCH/KST/CONF";
    CHECK_STR_EQ(fm_path_base(p), "CONF");
    CHECK(fm_path_up(p));
    CHECK_STR_EQ(p, "BENCH/KST");
    CHECK(fm_path_up(p));
    CHECK_STR_EQ(p, "BENCH");
    CHECK_STR_EQ(fm_path_base(p), "BENCH");
    CHECK(fm_path_up(p));
    CHECK_STR_EQ(p, "");
    /* At the root nothing is above. */
    CHECK(!fm_path_up(p));
    CHECK_STR_EQ(p, "");
    CHECK(!fm_path_up(NULL));
    CHECK_STR_EQ(fm_path_base(""), "");
    CHECK_STR_EQ(fm_path_base(NULL), "");
}

TEST_CASE(a_path_with_an_alias_name_is_not_plain)
{
    CHECK(fm_path_plain(""));
    CHECK(fm_path_plain("BENCH/KST/CONF/KST.TXT"));
    CHECK(fm_path_plain("USER/LOGS/N\xc3\x84 1.TXT"));
    CHECK(fm_path_plain("._A"));
    CHECK(fm_path_plain("A~B/C~.TXT"));
    static const char *const no[] = {
        "BENCH/KST/CONF./KST.TXT", "BENCH/KST/CONF /KST.TXT",
        " BENCH", "BENCH/A.", "BENCH/A ", "BENCH\\KST", "0:/BENCH",
        "A\"B", "A*B", "A<B", "A>B", "A?B", "A|B", "A\x01B", "A\x7f",
        "LOG000~1.BFL", "BENCH/KST~1", "~9",
    };
    for (size_t i = 0u; i < sizeof(no) / sizeof(no[0]); ++i) {
        CHECK(!fm_path_plain(no[i]));
    }
    CHECK(!fm_path_plain(NULL));
}

int main(void)
{
    RUN(the_limits_are_the_walks);
    RUN(a_plain_name_is_taken);
    RUN(an_empty_name_is_refused);
    RUN(fifty_nine_bytes_are_the_most);
    RUN(the_german_letters_are_taken_and_nothing_else_outside_ascii);
    RUN(every_mark_fat_refuses_is_refused);
    RUN(a_dot_or_a_space_at_an_end_is_refused);
    RUN(a_device_name_is_refused_alone_and_before_a_dot);
    RUN(the_new_name_fits_the_listing);
    RUN(a_folder_path_is_checked);
    RUN(a_name_joins_a_folder);
    RUN(a_join_is_refused_with_out_unchanged);
    RUN(a_folder_below_the_deepest_is_refused_a_file_is_not);
    RUN(up_and_the_last_name);
    RUN(a_path_with_an_alias_name_is_not_plain);
    return test_summary("fm_name");
}
