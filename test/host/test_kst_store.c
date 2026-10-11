/*
 * Servo configuration files on the card: names, the proposed name, saving
 * without replacing, the sorted list and loading, over a directory held in
 * memory.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "kst_store.h"

/* --- a card in memory --------------------------------------------------------- */

#define CARD_FILES 1100
#define CARD_DATA  64

typedef struct {
    char name[80];
    bool is_dir;
    char data[CARD_DATA];
    size_t len;
} card_file_t;

static struct {
    card_file_t file[CARD_FILES];
    size_t count;
    bool mounted;
    bool walk_fails;
    kst_store_io_result_t create_result;
    int32_t read_result;       /* -2: as the file is */
    unsigned creates;
} g_card;

static bool card_mounted(void *ctx)
{
    (void)ctx;
    return g_card.mounted;
}

static bool card_walk(void *ctx, kst_store_visit_fn visit, void *visit_ctx)
{
    (void)ctx;
    for (size_t i = 0; i < g_card.count; ++i) {
        /* A copy: the name does not outlive the call. */
        char name[80];

        strcpy(name, g_card.file[i].name);
        visit(name, g_card.file[i].is_dir, visit_ctx);
        memset(name, '!', sizeof(name) - 1u);
    }
    return !g_card.walk_fails;
}

/* Exact name, as a file system that compares with case would. */
static card_file_t *card_find(const char *name)
{
    for (size_t i = 0; i < g_card.count; ++i) {
        if (strcmp(g_card.file[i].name, name) == 0 && !g_card.file[i].is_dir) {
            return &g_card.file[i];
        }
    }
    return NULL;
}

static int32_t card_size(void *ctx, const char *name)
{
    const card_file_t *f = card_find(name);

    (void)ctx;
    return f == NULL ? -1 : (int32_t)f->len;
}

static int32_t card_read(void *ctx, const char *name, char *buf, size_t cap)
{
    const card_file_t *f = card_find(name);
    const size_t n = f->len < cap ? f->len : cap;

    (void)ctx;
    if (g_card.read_result != -2) {
        return g_card.read_result;
    }
    memcpy(buf, f->data, n);
    return (int32_t)n;
}

static void card_add(const char *name, bool is_dir, const char *data)
{
    card_file_t *f = &g_card.file[g_card.count++];

    strcpy(f->name, name);
    f->is_dir = is_dir;
    f->len = strlen(data);
    memcpy(f->data, data, f->len);
}

static kst_store_io_result_t card_create(void *ctx, const char *name,
                                         const char *data, size_t len)
{
    char text[CARD_DATA];

    (void)ctx;
    g_card.creates++;
    if (g_card.create_result != KST_STORE_IO_OK) {
        return g_card.create_result;
    }
    memcpy(text, data, len);
    text[len] = '\0';
    card_add(name, false, text);
    return KST_STORE_IO_OK;
}

static const kst_store_io_t g_io = {
    NULL, card_mounted, card_walk, card_size, card_read, card_create,
};

static void card_reset(void)
{
    memset(&g_card, 0, sizeof(g_card));
    g_card.mounted = true;
    g_card.read_result = -2;
}

static void add_number(int number, const char *ext)
{
    char name[16];

    snprintf(name, sizeof(name), "KST%03d%s", number, ext);
    card_add(name, false, "x");
}

/* --- names -------------------------------------------------------------------- */

TEST_CASE(a_base_name_is_held_to_what_the_card_and_a_desktop_take)
{
    CHECK_EQ(kst_store_name_check("KST001"), KST_STORE_NAME_OK);
    CHECK_EQ(kst_store_name_check("left wing, v2 (final)"), KST_STORE_NAME_OK);
    CHECK_EQ(kst_store_name_check("a"), KST_STORE_NAME_OK);
    CHECK_EQ(kst_store_name_check("a.b"), KST_STORE_NAME_OK);
    CHECK_EQ(kst_store_name_check("~!#$%&'()+,-;=@[]^_`{}"), KST_STORE_NAME_OK);

    CHECK_EQ(kst_store_name_check(NULL), KST_STORE_NAME_EMPTY);
    CHECK_EQ(kst_store_name_check(""), KST_STORE_NAME_EMPTY);

    /* 32 characters pass, 33 do not. */
    CHECK_EQ(kst_store_name_check("12345678901234567890123456789012"),
             KST_STORE_NAME_OK);
    CHECK_EQ(kst_store_name_check("123456789012345678901234567890123"),
             KST_STORE_NAME_TOO_LONG);
    /* Too long is told before the characters are looked at. */
    CHECK_EQ(kst_store_name_check("12345678901234567890123456789012*"),
             KST_STORE_NAME_TOO_LONG);
}

TEST_CASE(each_character_a_file_system_refuses_is_refused)
{
    static const char refused[] = "\"*/:<>?\\|";
    char name[4] = "a?b";

    for (size_t i = 0; i < sizeof(refused) - 1u; ++i) {
        name[1] = refused[i];
        CHECK_EQ(kst_store_name_check(name), KST_STORE_NAME_BAD_CHAR);
    }
    for (unsigned c = 1; c < 256u; ++c) {
        const bool plain = c >= 0x20u && c <= 0x7Eu &&
                           strchr(refused, (int)c) == NULL;

        name[1] = (char)c;
        CHECK_EQ(kst_store_name_check(name),
                 plain ? KST_STORE_NAME_OK : KST_STORE_NAME_BAD_CHAR);
    }
    /* A character is refused before an edge is. */
    CHECK_EQ(kst_store_name_check(" a?"), KST_STORE_NAME_BAD_CHAR);
}

TEST_CASE(a_full_stop_or_a_space_at_either_end_is_refused)
{
    CHECK_EQ(kst_store_name_check(".a"), KST_STORE_NAME_EDGE);
    CHECK_EQ(kst_store_name_check("a."), KST_STORE_NAME_EDGE);
    CHECK_EQ(kst_store_name_check(" a"), KST_STORE_NAME_EDGE);
    CHECK_EQ(kst_store_name_check("a "), KST_STORE_NAME_EDGE);
    CHECK_EQ(kst_store_name_check("."), KST_STORE_NAME_EDGE);
    CHECK_EQ(kst_store_name_check(" "), KST_STORE_NAME_EDGE);
    CHECK_EQ(kst_store_name_check(".."), KST_STORE_NAME_EDGE);
    CHECK_EQ(kst_store_name_check("a b.c d"), KST_STORE_NAME_OK);
}

TEST_CASE(a_device_name_is_refused_alone_and_in_front_of_a_full_stop)
{
    static const char *const refused[] = {
        "CON", "con", "Prn", "AUX", "nul", "COM1", "com9", "LPT1", "lpt9",
        "CON.a", "nul.tar.gz", "COM1.x", "lpt5.old",
    };
    static const char *const taken[] = {
        "CONS", "CO", "COM", "COM0", "COM10", "LPT0", "LPT", "aCON", "CON-1",
        "a.CON", "NULL", "AUX1", "COM1a", "KST1", "LPTX", "COMA",
    };

    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        CHECK_EQ(kst_store_name_check(refused[i]), KST_STORE_NAME_RESERVED);
    }
    for (size_t i = 0; i < sizeof(taken) / sizeof(taken[0]); ++i) {
        CHECK_EQ(kst_store_name_check(taken[i]), KST_STORE_NAME_OK);
    }
}

TEST_CASE(the_file_name_is_the_base_name_and_the_extension)
{
    char name[KST_STORE_NAME_MAX + 1];

    memset(name, 0x55, sizeof(name));
    CHECK(kst_store_file_name("Wing", name, KST_STORE_NAME_MAX));
    CHECK_STR_EQ(name, "Wing.TXT");
    CHECK(kst_store_file_name("12345678901234567890123456789012", name,
                              KST_STORE_NAME_MAX));
    CHECK_STR_EQ(name, "12345678901234567890123456789012.TXT");
    CHECK_EQ(name[KST_STORE_NAME_MAX], 0x55);

    CHECK(!kst_store_file_name("a*", name, KST_STORE_NAME_MAX));
    CHECK_STR_EQ(name, "");
    name[0] = 'x';
    CHECK(!kst_store_file_name("Wing", name, KST_STORE_NAME_MAX - 1));
    CHECK_STR_EQ(name, "");
    name[0] = 'x';
    CHECK(!kst_store_file_name("Wing", name, 0));
    CHECK_EQ(name[0], 'x');
    CHECK(!kst_store_file_name("Wing", NULL, KST_STORE_NAME_MAX));
    CHECK(!kst_store_file_name(NULL, name, KST_STORE_NAME_MAX));
}

/* --- the proposed name -------------------------------------------------------- */

static const char *preset(kst_store_t *result)
{
    static char base[KST_STORE_PRESET_MAX + 1];

    memset(base, 0x55, sizeof(base));
    base[KST_STORE_PRESET_MAX] = '\0';
    *result = kst_store_preset(&g_io, base, KST_STORE_PRESET_MAX);
    return base;
}

TEST_CASE(an_empty_directory_proposes_kst001)
{
    kst_store_t result;

    card_reset();
    CHECK_STR_EQ(preset(&result), "KST001");
    CHECK_EQ(result, KST_STORE_OK);
}

TEST_CASE(the_proposal_is_one_above_the_highest_number_and_fills_no_gap)
{
    kst_store_t result;

    card_reset();
    add_number(1, ".TXT");
    add_number(2, ".TXT");
    add_number(7, ".TXT");
    CHECK_STR_EQ(preset(&result), "KST008");
    add_number(120, ".txt");
    CHECK_STR_EQ(preset(&result), "KST121");
    add_number(998, ".Txt");
    CHECK_STR_EQ(preset(&result), "KST999");
    CHECK_EQ(result, KST_STORE_OK);
}

TEST_CASE(a_name_a_person_chose_counts_for_nothing_in_the_numbering)
{
    kst_store_t result;

    card_reset();
    add_number(4, ".TXT");
    card_add("Wing.TXT", false, "x");
    card_add("KST900", false, "x");          /* no extension */
    card_add("KST900.LOG", false, "x");
    card_add("KST0900.TXT", false, "x");     /* 4 digits */
    card_add("KST90.TXT", false, "x");       /* 2 digits */
    card_add("KST9a0.TXT", false, "x");
    card_add("KST90a.TXT", false, "x");
    card_add("KSTa00.TXT", false, "x");
    card_add("KST 90.TXT", false, "x");
    card_add("XST900.TXT", false, "x");
    card_add("KXT900.TXT", false, "x");
    card_add("KSX900.TXT", false, "x");
    card_add("KST900.TXT.TXT", false, "x");
    card_add("KST000.TXT", false, "x");      /* below the first number */
    card_add(".TXT", false, "x");
    card_add("TXT", false, "x");
    CHECK_STR_EQ(preset(&result), "KST005");
    CHECK_EQ(result, KST_STORE_OK);
}

TEST_CASE(a_number_counts_in_any_case_and_on_a_directory)
{
    kst_store_t result;

    card_reset();
    card_add("kst010.txt", false, "x");
    CHECK_STR_EQ(preset(&result), "KST011");
    card_add("Kst020.TxT", true, "");
    CHECK_STR_EQ(preset(&result), "KST021");
}

TEST_CASE(with_kst999_taken_the_lowest_free_number_is_proposed)
{
    kst_store_t result;

    card_reset();
    add_number(999, ".TXT");
    CHECK_STR_EQ(preset(&result), "KST001");
    add_number(1, ".TXT");
    add_number(2, ".TXT");
    add_number(4, ".TXT");
    CHECK_STR_EQ(preset(&result), "KST003");
    CHECK_EQ(result, KST_STORE_OK);
}

TEST_CASE(with_every_number_taken_no_name_is_proposed)
{
    kst_store_t result;

    card_reset();
    for (int n = 1; n <= 999; ++n) {
        if (n != 500) {
            add_number(n, ".TXT");
        }
    }
    CHECK_STR_EQ(preset(&result), "KST500");
    add_number(500, ".TXT");
    CHECK_STR_EQ(preset(&result), "");
    CHECK_EQ(result, KST_STORE_NO_NUMBER);
}

TEST_CASE(a_proposal_needs_a_card_a_readable_directory_and_room_for_the_name)
{
    kst_store_t result;
    char base[KST_STORE_PRESET_MAX] = "x";

    card_reset();
    CHECK_EQ(kst_store_preset(NULL, base, sizeof(base)), KST_STORE_ARG);
    CHECK_EQ(kst_store_preset(&g_io, NULL, sizeof(base)), KST_STORE_ARG);
    CHECK_EQ(kst_store_preset(&g_io, base, KST_STORE_PRESET_MAX - 1),
             KST_STORE_ARG);
    CHECK_STR_EQ(base, "x");

    g_card.mounted = false;
    CHECK_STR_EQ(preset(&result), "");
    CHECK_EQ(result, KST_STORE_NO_CARD);
    g_card.mounted = true;
    g_card.walk_fails = true;
    add_number(3, ".TXT");
    CHECK_STR_EQ(preset(&result), "");
    CHECK_EQ(result, KST_STORE_LIST_ERROR);
}

/* --- saving ------------------------------------------------------------------- */

TEST_CASE(a_save_writes_the_bytes_under_the_name_with_the_extension)
{
    kst_store_name_t why = KST_STORE_NAME_EDGE;

    card_reset();
    CHECK_EQ(kst_store_save(&g_io, "Wing", "abcdef", 4, &why), KST_STORE_OK);
    CHECK_EQ(why, KST_STORE_NAME_OK);
    CHECK_EQ(g_card.count, 1);
    CHECK_STR_EQ(g_card.file[0].name, "Wing.TXT");
    CHECK_EQ(g_card.file[0].len, 4);
    CHECK(memcmp(g_card.file[0].data, "abcd", 4) == 0);
    CHECK_EQ(kst_store_save(&g_io, "Tail", "", 0, NULL), KST_STORE_OK);
    CHECK_EQ(g_card.count, 2);
}

TEST_CASE(a_save_never_replaces_a_file_whatever_the_case_of_its_name)
{
    card_reset();
    card_add("Wing.TXT", false, "old");
    card_add("box.txt", true, "");
    CHECK_EQ(kst_store_save(&g_io, "Wing", "new", 3, NULL), KST_STORE_EXISTS);
    CHECK_EQ(kst_store_save(&g_io, "WING", "new", 3, NULL), KST_STORE_EXISTS);
    CHECK_EQ(kst_store_save(&g_io, "wing", "new", 3, NULL), KST_STORE_EXISTS);
    /* A directory holds the name too. */
    CHECK_EQ(kst_store_save(&g_io, "Box", "new", 3, NULL), KST_STORE_EXISTS);
    CHECK_EQ(g_card.creates, 0);
    CHECK(memcmp(g_card.file[0].data, "old", 3) == 0);
    /* A name that starts the same is another file. */
    CHECK_EQ(kst_store_save(&g_io, "Win", "new", 3, NULL), KST_STORE_OK);
    CHECK_EQ(kst_store_save(&g_io, "Wings", "new", 3, NULL), KST_STORE_OK);
    CHECK_EQ(g_card.creates, 2);
}

TEST_CASE(a_refused_name_gives_its_reason_and_touches_no_card)
{
    kst_store_name_t why = KST_STORE_NAME_OK;

    card_reset();
    g_card.mounted = false;     /* the name is judged first */
    CHECK_EQ(kst_store_save(&g_io, "", "x", 1, &why), KST_STORE_BAD_NAME);
    CHECK_EQ(why, KST_STORE_NAME_EMPTY);
    CHECK_EQ(kst_store_save(&g_io, NULL, "x", 1, &why), KST_STORE_BAD_NAME);
    CHECK_EQ(why, KST_STORE_NAME_EMPTY);
    CHECK_EQ(kst_store_save(&g_io, "a/b", "x", 1, &why), KST_STORE_BAD_NAME);
    CHECK_EQ(why, KST_STORE_NAME_BAD_CHAR);
    CHECK_EQ(kst_store_save(&g_io, "NUL", "x", 1, &why), KST_STORE_BAD_NAME);
    CHECK_EQ(why, KST_STORE_NAME_RESERVED);
    CHECK_EQ(kst_store_save(&g_io, "a.", "x", 1, &why), KST_STORE_BAD_NAME);
    CHECK_EQ(why, KST_STORE_NAME_EDGE);
    CHECK_EQ(kst_store_save(&g_io, "123456789012345678901234567890123", "x", 1,
                            &why), KST_STORE_BAD_NAME);
    CHECK_EQ(why, KST_STORE_NAME_TOO_LONG);
    CHECK_EQ(kst_store_save(&g_io, "a.", "x", 1, NULL), KST_STORE_BAD_NAME);
    CHECK_EQ(g_card.creates, 0);
}

TEST_CASE(each_failure_of_a_save_has_its_own_result)
{
    kst_store_name_t why = KST_STORE_NAME_EDGE;

    card_reset();
    CHECK_EQ(kst_store_save(NULL, "Wing", "x", 1, &why), KST_STORE_ARG);
    CHECK_EQ(why, KST_STORE_NAME_OK);
    CHECK_EQ(kst_store_save(&g_io, "Wing", NULL, 0, NULL), KST_STORE_ARG);

    g_card.mounted = false;
    CHECK_EQ(kst_store_save(&g_io, "Wing", "x", 1, NULL), KST_STORE_NO_CARD);
    g_card.mounted = true;

    /* A directory that does not read to its end may hold the name. */
    g_card.walk_fails = true;
    CHECK_EQ(kst_store_save(&g_io, "Wing", "x", 1, NULL), KST_STORE_LIST_ERROR);
    CHECK_EQ(g_card.creates, 0);
    g_card.walk_fails = false;

    /* The name appears between the look and the create. */
    g_card.create_result = KST_STORE_IO_EXISTS;
    CHECK_EQ(kst_store_save(&g_io, "Wing", "x", 1, NULL), KST_STORE_EXISTS);
    g_card.create_result = KST_STORE_IO_FULL;
    CHECK_EQ(kst_store_save(&g_io, "Wing", "x", 1, NULL), KST_STORE_CARD_FULL);
    g_card.create_result = KST_STORE_IO_ERROR;
    CHECK_EQ(kst_store_save(&g_io, "Wing", "x", 1, NULL),
             KST_STORE_WRITE_ERROR);
    CHECK_EQ(g_card.creates, 3);
    CHECK_EQ(g_card.count, 0);
}

/* --- the list ----------------------------------------------------------------- */

TEST_CASE(the_list_holds_every_file_with_the_extension_sorted_without_case)
{
    kst_store_entry_t e[8];
    size_t count = 99;
    size_t total = 99;
    size_t skipped = 99;

    card_reset();
    card_add("wing.TXT", false, "x");
    card_add("KST002.TXT", false, "x");
    card_add("Tail.txt", false, "x");
    card_add("KST001.TXT", false, "x");
    card_add("notes.md", false, "x");        /* another extension */
    card_add("TXT", false, "x");
    card_add(".TXT", false, "x");            /* no base name */
    card_add("old.TXT", true, "");           /* a directory */
    card_add("abc.Txt", false, "x");
    card_add("ABC2.TXT", false, "x");
    memset(e, 0x55, sizeof(e));
    CHECK_EQ(kst_store_list(&g_io, e, 8, &count, &total, &skipped),
             KST_STORE_OK);
    CHECK_EQ(count, 6);
    CHECK_EQ(total, 6);
    CHECK_EQ(skipped, 0);
    CHECK_STR_EQ(e[0].name, "abc.Txt");
    CHECK_STR_EQ(e[1].name, "ABC2.TXT");
    CHECK_STR_EQ(e[2].name, "KST001.TXT");
    CHECK_STR_EQ(e[3].name, "KST002.TXT");
    CHECK_STR_EQ(e[4].name, "Tail.txt");
    CHECK_STR_EQ(e[5].name, "wing.TXT");
    CHECK_EQ((unsigned char)e[6].name[0], 0x55);
}

TEST_CASE(names_equal_without_case_keep_one_order)
{
    kst_store_entry_t e[4];
    size_t count = 0;

    /* A card written by a file system that compares with case. */
    card_reset();
    card_add("b.TXT", false, "x");
    card_add("a.TXT", false, "x");
    card_add("B.TXT", false, "x");
    card_add("A.TXT", false, "x");
    CHECK_EQ(kst_store_list(&g_io, e, 4, &count, NULL, NULL), KST_STORE_OK);
    CHECK_EQ(count, 4);
    CHECK_STR_EQ(e[0].name, "A.TXT");
    CHECK_STR_EQ(e[1].name, "a.TXT");
    CHECK_STR_EQ(e[2].name, "B.TXT");
    CHECK_STR_EQ(e[3].name, "b.TXT");
}

TEST_CASE(a_list_shorter_than_the_directory_holds_the_first_names)
{
    kst_store_entry_t e[4];
    size_t count = 0;
    size_t total = 0;

    card_reset();
    for (int n = 9; n >= 1; --n) {
        add_number(n * 7 % 10, ".TXT");      /* 3 6 9 2 5 8 1 4 7 */
    }
    memset(e, 0x55, sizeof(e));
    CHECK_EQ(kst_store_list(&g_io, e, 3, &count, &total, NULL), KST_STORE_OK);
    CHECK_EQ(count, 3);
    CHECK_EQ(total, 9);
    CHECK_STR_EQ(e[0].name, "KST001.TXT");
    CHECK_STR_EQ(e[1].name, "KST002.TXT");
    CHECK_STR_EQ(e[2].name, "KST003.TXT");
    CHECK_EQ((unsigned char)e[3].name[0], 0x55);

    /* No room at all: the counts alone. */
    count = 99;
    CHECK_EQ(kst_store_list(&g_io, NULL, 0, &count, &total, NULL),
             KST_STORE_OK);
    CHECK_EQ(count, 0);
    CHECK_EQ(total, 9);
    CHECK_EQ(kst_store_list(&g_io, e, 0, NULL, NULL, NULL), KST_STORE_OK);
    CHECK_STR_EQ(e[0].name, "KST001.TXT");
}

TEST_CASE(a_file_whose_name_this_layer_would_not_write_is_counted_not_listed)
{
    kst_store_entry_t e[4];
    size_t count = 0;
    size_t total = 0;
    size_t skipped = 0;

    card_reset();
    card_add("fl\xC3\xBCgel.TXT", false, "x");
    card_add("123456789012345678901234567890123.TXT", false, "x");
    card_add("NUL.TXT", false, "x");
    card_add("a .TXT", false, "x");
    card_add("ok.TXT", false, "x");
    CHECK_EQ(kst_store_list(&g_io, e, 4, &count, &total, &skipped),
             KST_STORE_OK);
    CHECK_EQ(count, 1);
    CHECK_EQ(total, 1);
    CHECK_EQ(skipped, 4);
    CHECK_STR_EQ(e[0].name, "ok.TXT");
}

TEST_CASE(a_list_needs_a_card_and_reports_a_directory_that_does_not_read)
{
    kst_store_entry_t e[2];
    size_t count = 99;

    card_reset();
    CHECK_EQ(kst_store_list(NULL, e, 2, &count, NULL, NULL), KST_STORE_ARG);
    CHECK_EQ(kst_store_list(&g_io, NULL, 2, &count, NULL, NULL), KST_STORE_ARG);
    CHECK_EQ(count, 99);

    /* An empty directory, and none: no file. */
    CHECK_EQ(kst_store_list(&g_io, e, 2, &count, NULL, NULL), KST_STORE_OK);
    CHECK_EQ(count, 0);

    g_card.mounted = false;
    count = 99;
    CHECK_EQ(kst_store_list(&g_io, e, 2, &count, NULL, NULL),
             KST_STORE_NO_CARD);
    CHECK_EQ(count, 99);
    g_card.mounted = true;

    /* What was read before the failure is in the list. */
    card_add("a.TXT", false, "x");
    g_card.walk_fails = true;
    CHECK_EQ(kst_store_list(&g_io, e, 2, &count, NULL, NULL),
             KST_STORE_LIST_ERROR);
    CHECK_EQ(count, 1);
}

/* --- loading ------------------------------------------------------------------ */

TEST_CASE(a_load_gives_the_bytes_of_the_file_and_no_terminator)
{
    char buf[8];
    size_t len = 99;

    card_reset();
    card_add("Wing.TXT", false, "abcdef");
    memset(buf, 0x55, sizeof(buf));
    CHECK_EQ(kst_store_load(&g_io, "Wing.TXT", buf, 6, &len), KST_STORE_OK);
    CHECK_EQ(len, 6);
    CHECK(memcmp(buf, "abcdef\x55\x55", 8) == 0);
}

TEST_CASE(each_failure_of_a_load_has_its_own_result)
{
    char buf[8];
    size_t len = 99;

    card_reset();
    card_add("Wing.TXT", false, "abcdef");
    CHECK_EQ(kst_store_load(NULL, "Wing.TXT", buf, 8, &len), KST_STORE_ARG);
    CHECK_EQ(kst_store_load(&g_io, NULL, buf, 8, &len), KST_STORE_ARG);
    CHECK_EQ(kst_store_load(&g_io, "Wing.TXT", NULL, 8, &len), KST_STORE_ARG);
    CHECK_EQ(kst_store_load(&g_io, "Wing.TXT", buf, 8, NULL), KST_STORE_ARG);
    CHECK_EQ(len, 99);

    /* A name with a path, without the extension, without a base name. */
    CHECK_EQ(kst_store_load(&g_io, "../Wing.TXT", buf, 8, &len),
             KST_STORE_BAD_NAME);
    CHECK_EQ(len, 0);
    CHECK_EQ(kst_store_load(&g_io, "Wing", buf, 8, &len), KST_STORE_BAD_NAME);
    CHECK_EQ(kst_store_load(&g_io, ".TXT", buf, 8, &len), KST_STORE_BAD_NAME);
    CHECK_EQ(kst_store_load(&g_io, "Wing.LOG", buf, 8, &len),
             KST_STORE_BAD_NAME);

    g_card.mounted = false;
    CHECK_EQ(kst_store_load(&g_io, "Wing.TXT", buf, 8, &len),
             KST_STORE_NO_CARD);
    g_card.mounted = true;

    CHECK_EQ(kst_store_load(&g_io, "Tail.TXT", buf, 8, &len),
             KST_STORE_NOT_FOUND);
    CHECK_EQ(kst_store_load(&g_io, "Wing.TXT", buf, 5, &len),
             KST_STORE_TOO_LARGE);
    CHECK_EQ(len, 0);

    g_card.read_result = -1;
    CHECK_EQ(kst_store_load(&g_io, "Wing.TXT", buf, 8, &len),
             KST_STORE_READ_ERROR);
    g_card.read_result = 5;     /* a short read */
    CHECK_EQ(kst_store_load(&g_io, "Wing.TXT", buf, 8, &len),
             KST_STORE_READ_ERROR);
    CHECK_EQ(len, 0);
}

TEST_CASE(a_saved_file_is_listed_and_loads_back)
{
    kst_store_entry_t e[2];
    char base[KST_STORE_PRESET_MAX];
    char buf[16];
    size_t count = 0;
    size_t len = 0;

    card_reset();
    CHECK_EQ(kst_store_preset(&g_io, base, sizeof(base)), KST_STORE_OK);
    CHECK_EQ(kst_store_save(&g_io, base, "first", 5, NULL), KST_STORE_OK);
    CHECK_EQ(kst_store_preset(&g_io, base, sizeof(base)), KST_STORE_OK);
    CHECK_STR_EQ(base, "KST002");
    CHECK_EQ(kst_store_save(&g_io, base, "second", 6, NULL), KST_STORE_OK);
    CHECK_EQ(kst_store_list(&g_io, e, 2, &count, NULL, NULL), KST_STORE_OK);
    CHECK_EQ(count, 2);
    CHECK_STR_EQ(e[1].name, "KST002.TXT");
    CHECK_EQ(kst_store_load(&g_io, e[1].name, buf, sizeof(buf), &len),
             KST_STORE_OK);
    CHECK_EQ(len, 6);
    CHECK(memcmp(buf, "second", 6) == 0);
}

int main(void)
{
    RUN(a_base_name_is_held_to_what_the_card_and_a_desktop_take);
    RUN(each_character_a_file_system_refuses_is_refused);
    RUN(a_full_stop_or_a_space_at_either_end_is_refused);
    RUN(a_device_name_is_refused_alone_and_in_front_of_a_full_stop);
    RUN(the_file_name_is_the_base_name_and_the_extension);
    RUN(an_empty_directory_proposes_kst001);
    RUN(the_proposal_is_one_above_the_highest_number_and_fills_no_gap);
    RUN(a_name_a_person_chose_counts_for_nothing_in_the_numbering);
    RUN(a_number_counts_in_any_case_and_on_a_directory);
    RUN(with_kst999_taken_the_lowest_free_number_is_proposed);
    RUN(with_every_number_taken_no_name_is_proposed);
    RUN(a_proposal_needs_a_card_a_readable_directory_and_room_for_the_name);
    RUN(a_save_writes_the_bytes_under_the_name_with_the_extension);
    RUN(a_save_never_replaces_a_file_whatever_the_case_of_its_name);
    RUN(a_refused_name_gives_its_reason_and_touches_no_card);
    RUN(each_failure_of_a_save_has_its_own_result);
    RUN(the_list_holds_every_file_with_the_extension_sorted_without_case);
    RUN(names_equal_without_case_keep_one_order);
    RUN(a_list_shorter_than_the_directory_holds_the_first_names);
    RUN(a_file_whose_name_this_layer_would_not_write_is_counted_not_listed);
    RUN(a_list_needs_a_card_and_reports_a_directory_that_does_not_read);
    RUN(a_load_gives_the_bytes_of_the_file_and_no_terminator);
    RUN(each_failure_of_a_load_has_its_own_result);
    RUN(a_saved_file_is_listed_and_loads_back);
    return test_summary("kst_store");
}
