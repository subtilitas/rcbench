/*
 * Servo configuration files on the card for a programmer named by a stem
 * (shared/logfile/servo_store.c), with the Hitec programmer's stem HITEC:
 * names, the proposed name, saving without replacing, the sorted list and
 * loading, over a directory held in memory.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "servo_store.h"

/** @brief The stem every test uses. */
#define STEM "HITEC"

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
    servo_store_io_result_t create_result;
    int32_t read_result;       /* -2: as the file is */
    unsigned creates;
} g_card;

static bool card_mounted(void *ctx)
{
    (void)ctx;
    return g_card.mounted;
}

static bool card_walk(void *ctx, servo_store_visit_fn visit, void *visit_ctx)
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

static servo_store_io_result_t card_create(void *ctx, const char *name,
                                         const char *data, size_t len)
{
    char text[CARD_DATA];

    (void)ctx;
    g_card.creates++;
    if (g_card.create_result != SERVO_STORE_IO_OK) {
        return g_card.create_result;
    }
    memcpy(text, data, len);
    text[len] = '\0';
    card_add(name, false, text);
    return SERVO_STORE_IO_OK;
}

static const servo_store_io_t g_io = {
    NULL, STEM, card_mounted, card_walk, card_size, card_read, card_create,
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

    snprintf(name, sizeof(name), "HITEC%03d%s", number, ext);
    card_add(name, false, "x");
}

/* --- names -------------------------------------------------------------------- */

TEST_CASE(a_base_name_is_held_to_what_the_card_and_a_desktop_take)
{
    CHECK_EQ(servo_store_name_check(STEM, "HITEC001"), SERVO_STORE_NAME_OK);
    CHECK_EQ(servo_store_name_check(STEM, "left wing, v2 (final)"), SERVO_STORE_NAME_OK);
    CHECK_EQ(servo_store_name_check(STEM, "a"), SERVO_STORE_NAME_OK);
    CHECK_EQ(servo_store_name_check(STEM, "a.b"), SERVO_STORE_NAME_OK);
    CHECK_EQ(servo_store_name_check(STEM, "~!#$%&'()+,-;=@[]^_`{}"), SERVO_STORE_NAME_OK);

    CHECK_EQ(servo_store_name_check(STEM, NULL), SERVO_STORE_NAME_EMPTY);
    CHECK_EQ(servo_store_name_check(STEM, ""), SERVO_STORE_NAME_EMPTY);

    /* 32 characters pass, 33 do not. */
    CHECK_EQ(servo_store_name_check(STEM, "12345678901234567890123456789012"),
             SERVO_STORE_NAME_OK);
    CHECK_EQ(servo_store_name_check(STEM, "123456789012345678901234567890123"),
             SERVO_STORE_NAME_TOO_LONG);
    /* Too long is told before the characters are looked at. */
    CHECK_EQ(servo_store_name_check(STEM, "12345678901234567890123456789012*"),
             SERVO_STORE_NAME_TOO_LONG);
}

TEST_CASE(each_character_a_file_system_refuses_is_refused)
{
    static const char refused[] = "\"*/:<>?\\|";
    char name[4] = "a?b";

    for (size_t i = 0; i < sizeof(refused) - 1u; ++i) {
        name[1] = refused[i];
        CHECK_EQ(servo_store_name_check(STEM, name), SERVO_STORE_NAME_BAD_CHAR);
    }
    for (unsigned c = 1; c < 256u; ++c) {
        const bool plain = c >= 0x20u && c <= 0x7Eu &&
                           strchr(refused, (int)c) == NULL;

        name[1] = (char)c;
        CHECK_EQ(servo_store_name_check(STEM, name),
                 plain ? SERVO_STORE_NAME_OK : SERVO_STORE_NAME_BAD_CHAR);
    }
    /* A character is refused before an edge is. */
    CHECK_EQ(servo_store_name_check(STEM, " a?"), SERVO_STORE_NAME_BAD_CHAR);
}

TEST_CASE(a_full_stop_or_a_space_at_either_end_is_refused)
{
    CHECK_EQ(servo_store_name_check(STEM, ".a"), SERVO_STORE_NAME_EDGE);
    CHECK_EQ(servo_store_name_check(STEM, "a."), SERVO_STORE_NAME_EDGE);
    CHECK_EQ(servo_store_name_check(STEM, " a"), SERVO_STORE_NAME_EDGE);
    CHECK_EQ(servo_store_name_check(STEM, "a "), SERVO_STORE_NAME_EDGE);
    CHECK_EQ(servo_store_name_check(STEM, "."), SERVO_STORE_NAME_EDGE);
    CHECK_EQ(servo_store_name_check(STEM, " "), SERVO_STORE_NAME_EDGE);
    CHECK_EQ(servo_store_name_check(STEM, ".."), SERVO_STORE_NAME_EDGE);
    CHECK_EQ(servo_store_name_check(STEM, "a b.c d"), SERVO_STORE_NAME_OK);
}

TEST_CASE(a_device_name_is_refused_alone_and_in_front_of_a_full_stop)
{
    static const char *const refused[] = {
        "CON", "con", "Prn", "AUX", "nul", "COM1", "com9", "LPT1", "lpt9",
        "CON.a", "nul.tar.gz", "COM1.x", "lpt5.old",
    };
    static const char *const taken[] = {
        "CONS", "CO", "COM", "COM0", "COM10", "LPT0", "LPT", "aCON", "CON-1",
        "a.CON", "NULL", "AUX1", "COM1a", "HITEC1", "LPTX", "COMA",
    };

    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        CHECK_EQ(servo_store_name_check(STEM, refused[i]), SERVO_STORE_NAME_RESERVED);
    }
    for (size_t i = 0; i < sizeof(taken) / sizeof(taken[0]); ++i) {
        CHECK_EQ(servo_store_name_check(STEM, taken[i]), SERVO_STORE_NAME_OK);
    }
}

TEST_CASE(the_file_name_is_the_base_name_and_the_extension)
{
    char name[SERVO_STORE_NAME_MAX + 1];

    memset(name, 0x55, sizeof(name));
    CHECK(servo_store_file_name(STEM, "Wing", name, SERVO_STORE_NAME_MAX));
    CHECK_STR_EQ(name, "Wing.TXT");
    CHECK(servo_store_file_name(STEM, "12345678901234567890123456789012", name,
                              SERVO_STORE_NAME_MAX));
    CHECK_STR_EQ(name, "12345678901234567890123456789012.TXT");
    CHECK_EQ(name[SERVO_STORE_NAME_MAX], 0x55);

    CHECK(!servo_store_file_name(STEM, "a*", name, SERVO_STORE_NAME_MAX));
    CHECK_STR_EQ(name, "");
    name[0] = 'x';
    CHECK(!servo_store_file_name(STEM, "Wing", name, SERVO_STORE_NAME_MAX - 1));
    CHECK_STR_EQ(name, "");
    name[0] = 'x';
    CHECK(!servo_store_file_name(STEM, "Wing", name, 0));
    CHECK_EQ(name[0], 'x');
    CHECK(!servo_store_file_name(STEM, "Wing", NULL, SERVO_STORE_NAME_MAX));
    CHECK(!servo_store_file_name(STEM, NULL, name, SERVO_STORE_NAME_MAX));
}

/* --- the proposed name -------------------------------------------------------- */

static const char *preset(servo_store_t *result)
{
    static char base[SERVO_STORE_PRESET_MAX + 1];

    memset(base, 0x55, sizeof(base));
    base[SERVO_STORE_PRESET_MAX] = '\0';
    *result = servo_store_preset(&g_io, base, SERVO_STORE_PRESET_MAX);
    return base;
}

TEST_CASE(an_empty_directory_proposes_hitec001)
{
    servo_store_t result;

    card_reset();
    CHECK_STR_EQ(preset(&result), "HITEC001");
    CHECK_EQ(result, SERVO_STORE_OK);
}

TEST_CASE(the_proposal_is_one_above_the_highest_number_and_fills_no_gap)
{
    servo_store_t result;

    card_reset();
    add_number(1, ".TXT");
    add_number(2, ".TXT");
    add_number(7, ".TXT");
    CHECK_STR_EQ(preset(&result), "HITEC008");
    add_number(120, ".txt");
    CHECK_STR_EQ(preset(&result), "HITEC121");
    add_number(998, ".Txt");
    CHECK_STR_EQ(preset(&result), "HITEC999");
    CHECK_EQ(result, SERVO_STORE_OK);
}

TEST_CASE(a_name_a_person_chose_counts_for_nothing_in_the_numbering)
{
    servo_store_t result;

    card_reset();
    add_number(4, ".TXT");
    card_add("Wing.TXT", false, "x");
    card_add("HITEC900", false, "x");          /* no extension */
    card_add("HITEC900.LOG", false, "x");
    card_add("HITEC0900.TXT", false, "x");     /* 4 digits */
    card_add("HITEC90.TXT", false, "x");       /* 2 digits */
    card_add("HITEC9a0.TXT", false, "x");
    card_add("HITEC90a.TXT", false, "x");
    card_add("HITECa00.TXT", false, "x");
    card_add("HITEC 90.TXT", false, "x");
    card_add("XST900.TXT", false, "x");
    card_add("KXT900.TXT", false, "x");
    card_add("KSX900.TXT", false, "x");
    card_add("HITEC900.TXT.TXT", false, "x");
    card_add("HITEC000.TXT", false, "x");      /* below the first number */
    card_add(".TXT", false, "x");
    card_add("TXT", false, "x");
    CHECK_STR_EQ(preset(&result), "HITEC005");
    CHECK_EQ(result, SERVO_STORE_OK);
}

TEST_CASE(a_number_counts_in_any_case_and_on_a_directory)
{
    servo_store_t result;

    card_reset();
    card_add("hitec010.txt", false, "x");
    CHECK_STR_EQ(preset(&result), "HITEC011");
    card_add("Hitec020.TxT", true, "");
    CHECK_STR_EQ(preset(&result), "HITEC021");
}

TEST_CASE(with_hitec999_taken_the_lowest_free_number_is_proposed)
{
    servo_store_t result;

    card_reset();
    add_number(999, ".TXT");
    CHECK_STR_EQ(preset(&result), "HITEC001");
    add_number(1, ".TXT");
    add_number(2, ".TXT");
    add_number(4, ".TXT");
    CHECK_STR_EQ(preset(&result), "HITEC003");
    CHECK_EQ(result, SERVO_STORE_OK);
}

TEST_CASE(with_every_number_taken_no_name_is_proposed)
{
    servo_store_t result;

    card_reset();
    for (int n = 1; n <= 999; ++n) {
        if (n != 500) {
            add_number(n, ".TXT");
        }
    }
    CHECK_STR_EQ(preset(&result), "HITEC500");
    add_number(500, ".TXT");
    CHECK_STR_EQ(preset(&result), "");
    CHECK_EQ(result, SERVO_STORE_NO_NUMBER);
}

TEST_CASE(a_proposal_needs_a_card_a_readable_directory_and_room_for_the_name)
{
    servo_store_t result;
    char base[SERVO_STORE_PRESET_MAX] = "x";

    card_reset();
    CHECK_EQ(servo_store_preset(NULL, base, sizeof(base)), SERVO_STORE_ARG);
    CHECK_EQ(servo_store_preset(&g_io, NULL, sizeof(base)), SERVO_STORE_ARG);
    CHECK_EQ(servo_store_preset(&g_io, base, SERVO_STORE_PRESET_MAX - 1),
             SERVO_STORE_ARG);
    CHECK_STR_EQ(base, "x");

    g_card.mounted = false;
    CHECK_STR_EQ(preset(&result), "");
    CHECK_EQ(result, SERVO_STORE_NO_CARD);
    g_card.mounted = true;
    g_card.walk_fails = true;
    add_number(3, ".TXT");
    CHECK_STR_EQ(preset(&result), "");
    CHECK_EQ(result, SERVO_STORE_LIST_ERROR);
}

/* --- saving ------------------------------------------------------------------- */

TEST_CASE(a_save_writes_the_bytes_under_the_name_with_the_extension)
{
    servo_store_name_t why = SERVO_STORE_NAME_EDGE;

    card_reset();
    CHECK_EQ(servo_store_save(&g_io, "Wing", "abcdef", 4, &why), SERVO_STORE_OK);
    CHECK_EQ(why, SERVO_STORE_NAME_OK);
    CHECK_EQ(g_card.count, 1);
    CHECK_STR_EQ(g_card.file[0].name, "Wing.TXT");
    CHECK_EQ(g_card.file[0].len, 4);
    CHECK(memcmp(g_card.file[0].data, "abcd", 4) == 0);
    CHECK_EQ(servo_store_save(&g_io, "Tail", "", 0, NULL), SERVO_STORE_OK);
    CHECK_EQ(g_card.count, 2);
}

TEST_CASE(a_save_never_replaces_a_file_whatever_the_case_of_its_name)
{
    card_reset();
    card_add("Wing.TXT", false, "old");
    card_add("box.txt", true, "");
    CHECK_EQ(servo_store_save(&g_io, "Wing", "new", 3, NULL), SERVO_STORE_EXISTS);
    CHECK_EQ(servo_store_save(&g_io, "WING", "new", 3, NULL), SERVO_STORE_EXISTS);
    CHECK_EQ(servo_store_save(&g_io, "wing", "new", 3, NULL), SERVO_STORE_EXISTS);
    /* A directory holds the name too. */
    CHECK_EQ(servo_store_save(&g_io, "Box", "new", 3, NULL), SERVO_STORE_EXISTS);
    CHECK_EQ(g_card.creates, 0);
    CHECK(memcmp(g_card.file[0].data, "old", 3) == 0);
    /* A name that starts the same is another file. */
    CHECK_EQ(servo_store_save(&g_io, "Win", "new", 3, NULL), SERVO_STORE_OK);
    CHECK_EQ(servo_store_save(&g_io, "Wings", "new", 3, NULL), SERVO_STORE_OK);
    CHECK_EQ(g_card.creates, 2);
}

TEST_CASE(a_refused_name_gives_its_reason_and_touches_no_card)
{
    servo_store_name_t why = SERVO_STORE_NAME_OK;

    card_reset();
    g_card.mounted = false;     /* the name is judged first */
    CHECK_EQ(servo_store_save(&g_io, "", "x", 1, &why), SERVO_STORE_BAD_NAME);
    CHECK_EQ(why, SERVO_STORE_NAME_EMPTY);
    CHECK_EQ(servo_store_save(&g_io, NULL, "x", 1, &why), SERVO_STORE_BAD_NAME);
    CHECK_EQ(why, SERVO_STORE_NAME_EMPTY);
    CHECK_EQ(servo_store_save(&g_io, "a/b", "x", 1, &why), SERVO_STORE_BAD_NAME);
    CHECK_EQ(why, SERVO_STORE_NAME_BAD_CHAR);
    CHECK_EQ(servo_store_save(&g_io, "NUL", "x", 1, &why), SERVO_STORE_BAD_NAME);
    CHECK_EQ(why, SERVO_STORE_NAME_RESERVED);
    CHECK_EQ(servo_store_save(&g_io, "a.", "x", 1, &why), SERVO_STORE_BAD_NAME);
    CHECK_EQ(why, SERVO_STORE_NAME_EDGE);
    CHECK_EQ(servo_store_save(&g_io, "123456789012345678901234567890123", "x", 1,
                            &why), SERVO_STORE_BAD_NAME);
    CHECK_EQ(why, SERVO_STORE_NAME_TOO_LONG);
    CHECK_EQ(servo_store_save(&g_io, "a.", "x", 1, NULL), SERVO_STORE_BAD_NAME);
    CHECK_EQ(g_card.creates, 0);
}

TEST_CASE(each_failure_of_a_save_has_its_own_result)
{
    servo_store_name_t why = SERVO_STORE_NAME_EDGE;

    card_reset();
    CHECK_EQ(servo_store_save(NULL, "Wing", "x", 1, &why), SERVO_STORE_ARG);
    CHECK_EQ(why, SERVO_STORE_NAME_OK);
    CHECK_EQ(servo_store_save(&g_io, "Wing", NULL, 0, NULL), SERVO_STORE_ARG);

    g_card.mounted = false;
    CHECK_EQ(servo_store_save(&g_io, "Wing", "x", 1, NULL), SERVO_STORE_NO_CARD);
    g_card.mounted = true;

    /* A directory that does not read to its end may hold the name. */
    g_card.walk_fails = true;
    CHECK_EQ(servo_store_save(&g_io, "Wing", "x", 1, NULL), SERVO_STORE_LIST_ERROR);
    CHECK_EQ(g_card.creates, 0);
    g_card.walk_fails = false;

    /* The name appears between the look and the create. */
    g_card.create_result = SERVO_STORE_IO_EXISTS;
    CHECK_EQ(servo_store_save(&g_io, "Wing", "x", 1, NULL), SERVO_STORE_EXISTS);
    g_card.create_result = SERVO_STORE_IO_FULL;
    CHECK_EQ(servo_store_save(&g_io, "Wing", "x", 1, NULL), SERVO_STORE_CARD_FULL);
    g_card.create_result = SERVO_STORE_IO_ERROR;
    CHECK_EQ(servo_store_save(&g_io, "Wing", "x", 1, NULL),
             SERVO_STORE_WRITE_ERROR);
    CHECK_EQ(g_card.creates, 3);
    CHECK_EQ(g_card.count, 0);
}

TEST_CASE(a_card_that_takes_no_file_is_told_from_a_write_that_fails)
{
    char base[SERVO_STORE_PRESET_MAX];

    /* Write protected, or a file named as the directory. */
    card_reset();
    g_card.create_result = SERVO_STORE_IO_DENIED;
    CHECK_EQ(servo_store_save(&g_io, "Wing", "x", 1, NULL), SERVO_STORE_DENIED);
    CHECK_EQ(SERVO_STORE_DENIED, 12);
    CHECK_EQ(g_card.creates, 1);
    CHECK_EQ(g_card.count, 0);
    /* The card still reads: a name is proposed and the list is empty. */
    CHECK_EQ(servo_store_preset(&g_io, base, sizeof(base)), SERVO_STORE_OK);
    CHECK_STR_EQ(base, "HITEC001");
}

/* --- the list ----------------------------------------------------------------- */

TEST_CASE(the_list_holds_every_file_with_the_extension_sorted_without_case)
{
    servo_store_entry_t e[8];
    size_t count = 99;
    size_t total = 99;
    size_t skipped = 99;

    card_reset();
    card_add("wing.TXT", false, "x");
    card_add("HITEC002.TXT", false, "x");
    card_add("Tail.txt", false, "x");
    card_add("HITEC001.TXT", false, "x");
    card_add("notes.md", false, "x");        /* another extension */
    card_add("TXT", false, "x");
    card_add(".TXT", false, "x");            /* no base name */
    card_add("old.TXT", true, "");           /* a directory */
    card_add("abc.Txt", false, "x");
    card_add("ABC2.TXT", false, "x");
    memset(e, 0x55, sizeof(e));
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 8, &count, &total, &skipped),
             SERVO_STORE_OK);
    CHECK_EQ(count, 6);
    CHECK_EQ(total, 6);
    CHECK_EQ(skipped, 0);
    CHECK_STR_EQ(e[0].name, "abc.Txt");
    CHECK_STR_EQ(e[1].name, "ABC2.TXT");
    CHECK_STR_EQ(e[2].name, "HITEC002.TXT");
    CHECK_STR_EQ(e[3].name, "HITEC001.TXT");
    CHECK_STR_EQ(e[4].name, "Tail.txt");
    CHECK_STR_EQ(e[5].name, "wing.TXT");
    CHECK_EQ((unsigned char)e[6].name[0], 0x55);
}

TEST_CASE(names_equal_without_case_keep_one_order)
{
    servo_store_entry_t e[4];
    size_t count = 0;

    /* A card written by a file system that compares with case. */
    card_reset();
    card_add("b.TXT", false, "x");
    card_add("a.TXT", false, "x");
    card_add("B.TXT", false, "x");
    card_add("A.TXT", false, "x");
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 4, &count, NULL, NULL), SERVO_STORE_OK);
    CHECK_EQ(count, 4);
    CHECK_STR_EQ(e[0].name, "A.TXT");
    CHECK_STR_EQ(e[1].name, "a.TXT");
    CHECK_STR_EQ(e[2].name, "B.TXT");
    CHECK_STR_EQ(e[3].name, "b.TXT");
}

TEST_CASE(a_list_shorter_than_the_directory_holds_the_first_names)
{
    servo_store_entry_t e[4];
    size_t count = 0;
    size_t total = 0;

    card_reset();
    for (int n = 9; n >= 1; --n) {
        add_number(n * 7 % 10, ".TXT");      /* 3 6 9 2 5 8 1 4 7 */
    }
    memset(e, 0x55, sizeof(e));
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 3, &count, &total, NULL), SERVO_STORE_OK);
    CHECK_EQ(count, 3);
    CHECK_EQ(total, 9);
    CHECK_STR_EQ(e[0].name, "HITEC009.TXT");
    CHECK_STR_EQ(e[1].name, "HITEC008.TXT");
    CHECK_STR_EQ(e[2].name, "HITEC007.TXT");
    CHECK_EQ((unsigned char)e[3].name[0], 0x55);

    /* No room at all: the counts alone. */
    count = 99;
    CHECK_EQ(servo_store_list_after(&g_io, NULL, NULL, 0, &count, &total, NULL),
             SERVO_STORE_OK);
    CHECK_EQ(count, 0);
    CHECK_EQ(total, 9);
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 0, NULL, NULL, NULL), SERVO_STORE_OK);
    CHECK_STR_EQ(e[0].name, "HITEC009.TXT");
}

TEST_CASE(the_proposed_names_stand_in_one_run_with_the_latest_first)
{
    /* The order a list has, whatever the order on the card. */
    static const char *const want[] = {
        "ABC.TXT", "HAND.TXT", "HITE.TXT", "HITEC000.TXT",
        "HITEC999.TXT", "hitec120.txt", "HITEC100.TXT", "HITEC099.TXT", "HITEC002.TXT",
        "HITEC0001.TXT", "HITEC003X.TXT", "HITEC05.TXT", "HITEC1000.TXT",
        "HITEC12A.TXT", "HITECA.TXT", "HITED.TXT", "WING.TXT",
    };
    enum { N = sizeof(want) / sizeof(want[0]) };
    servo_store_entry_t e[N + 1];
    size_t count = 0;
    size_t total = 0;

    for (unsigned turn = 0; turn < 3u; ++turn) {
        card_reset();
        for (unsigned i = 0; i < (unsigned)N; ++i) {
            /* Forwards, backwards, and in steps of 5. */
            const unsigned at = turn == 0u ? i
                              : turn == 1u ? (unsigned)N - 1u - i
                                           : i * 5u % (unsigned)N;

            card_add(want[at], false, "x");
        }
        CHECK_EQ(servo_store_list_after(&g_io, NULL, e, N + 1, &count, &total, NULL),
                 SERVO_STORE_OK);
        CHECK_EQ(count, N);
        CHECK_EQ(total, N);
        for (unsigned i = 0; i < (unsigned)N; ++i) {
            CHECK_STR_EQ(e[i].name, want[i]);
        }
    }
}

TEST_CASE(a_list_goes_on_behind_a_name_and_reaches_every_file)
{
    servo_store_entry_t e[4];
    servo_store_entry_t all[40];
    char behind[SERVO_STORE_NAME_MAX];
    size_t count = 0;
    size_t total = 0;
    size_t skipped = 0;
    size_t whole = 0;
    size_t seen = 0;

    card_reset();
    for (int n = 1; n <= 30; ++n) {
        add_number(n * 7 % 31, ".TXT");      /* each of 1 to 30 once */
    }
    card_add("WING.TXT", false, "x");
    card_add("ABC.TXT", false, "x");
    card_add("NUL.TXT", false, "x");         /* not listed, counted apart */
    CHECK_EQ(servo_store_list_after(&g_io, NULL, all, 40, &whole, NULL, NULL), SERVO_STORE_OK);
    CHECK_EQ(whole, 32);
    CHECK_STR_EQ(all[0].name, "ABC.TXT");
    CHECK_STR_EQ(all[1].name, "HITEC030.TXT");
    CHECK_STR_EQ(all[30].name, "HITEC001.TXT");
    CHECK_STR_EQ(all[31].name, "WING.TXT");

    /* 4 names a call, each call behind the last name of the one before. */
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 4, &count, &total, &skipped),
             SERVO_STORE_OK);
    while (count > 0u) {
        CHECK_EQ(total, 32);
        CHECK_EQ(skipped, 1);
        for (size_t i = 0; i < count && seen < whole; ++i) {
            CHECK_STR_EQ(e[i].name, all[seen].name);
            seen++;
        }
        strcpy(behind, e[count - 1u].name);
        CHECK_EQ(servo_store_list_after(&g_io, behind, e, 4, &count, &total,
                                      &skipped), SERVO_STORE_OK);
    }
    CHECK_EQ(seen, 32);

    /* The name need not be a file's, and its case does not count. */
    CHECK_EQ(servo_store_list_after(&g_io, "hitec002.txt", e, 4, &count, NULL,
                                  NULL), SERVO_STORE_OK);
    CHECK_EQ(count, 2);
    CHECK_STR_EQ(e[0].name, "HITEC001.TXT");
    CHECK_STR_EQ(e[1].name, "WING.TXT");
    CHECK_EQ(servo_store_list_after(&g_io, "HITEC500.TXT", e, 4, &count, NULL,
                                  NULL), SERVO_STORE_OK);
    CHECK_STR_EQ(e[0].name, "HITEC030.TXT");
    CHECK_EQ(servo_store_list_after(&g_io, "X.TXT", e, 4, &count, &total, NULL),
             SERVO_STORE_OK);
    CHECK_EQ(count, 0);
    CHECK_EQ(total, 32);
    /* The failures of a list. */
    CHECK_EQ(servo_store_list_after(NULL, "X.TXT", e, 4, &count, NULL, NULL),
             SERVO_STORE_ARG);
    g_card.walk_fails = true;
    CHECK_EQ(servo_store_list_after(&g_io, "ABC.TXT", e, 4, &count, NULL, NULL),
             SERVO_STORE_LIST_ERROR);
    g_card.walk_fails = false;
    g_card.mounted = false;
    CHECK_EQ(servo_store_list_after(&g_io, "ABC.TXT", e, 4, &count, NULL, NULL),
             SERVO_STORE_NO_CARD);
}

TEST_CASE(a_file_whose_name_this_layer_would_not_write_is_counted_not_listed)
{
    servo_store_entry_t e[4];
    size_t count = 0;
    size_t total = 0;
    size_t skipped = 0;

    card_reset();
    card_add("fl\xC3\xBCgel.TXT", false, "x");
    card_add("123456789012345678901234567890123.TXT", false, "x");
    card_add("NUL.TXT", false, "x");
    card_add("a .TXT", false, "x");
    card_add("ok.TXT", false, "x");
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 4, &count, &total, &skipped),
             SERVO_STORE_OK);
    CHECK_EQ(count, 1);
    CHECK_EQ(total, 1);
    CHECK_EQ(skipped, 4);
    CHECK_STR_EQ(e[0].name, "ok.TXT");
}

TEST_CASE(the_record_of_a_write_is_no_name_no_list_entry_and_no_servo_file)
{
    static const char *const base[] = {"HITEC", "hitec", "Hitec", "hItec"};
    static const char *const taken[] = {"HITEC1", "KS", "HITECX", "HITEC.old",
                                        "AHITEC", "HITEC 1", "HITEC001"};
    servo_store_entry_t e[4];
    servo_store_name_t why = SERVO_STORE_NAME_OK;
    char name[SERVO_STORE_NAME_MAX];
    char buf[8];
    size_t count = 99;
    size_t total = 99;
    size_t skipped = 99;
    size_t len = 99;

    /* The record is the stem's: another stem takes the name. */
    CHECK_EQ(servo_store_name_check("KST", "HITEC"), SERVO_STORE_NAME_OK);
    CHECK_EQ(servo_store_name_check("KST", "kst"), SERVO_STORE_NAME_RESERVED);
    for (size_t i = 0; i < sizeof(base) / sizeof(base[0]); ++i) {
        CHECK_EQ(servo_store_name_check(STEM, base[i]), SERVO_STORE_NAME_RESERVED);
        CHECK(!servo_store_file_name(STEM, base[i], name, sizeof(name)));
    }
    for (size_t i = 0; i < sizeof(taken) / sizeof(taken[0]); ++i) {
        CHECK_EQ(servo_store_name_check(STEM, taken[i]), SERVO_STORE_NAME_OK);
    }

    /* No save writes it, with the file absent and with it present. */
    card_reset();
    CHECK_EQ(servo_store_save(&g_io, "hitec", "abc", 3, &why), SERVO_STORE_BAD_NAME);
    CHECK_EQ(why, SERVO_STORE_NAME_RESERVED);
    CHECK_EQ(g_card.count, 0);

    /* In the directory it is in no number of the list, in any case. */
    card_add("HITEC.TXT", false, "record");
    card_add("Wing.TXT", false, "x");
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 4, &count, &total, &skipped),
             SERVO_STORE_OK);
    CHECK_EQ(count, 1);
    CHECK_EQ(total, 1);
    CHECK_EQ(skipped, 0);
    CHECK_STR_EQ(e[0].name, "Wing.TXT");

    card_reset();
    card_add("hitec.txt", false, "record");
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 4, &count, &total, &skipped),
             SERVO_STORE_OK);
    CHECK_EQ(count, 0);
    CHECK_EQ(total, 0);
    CHECK_EQ(skipped, 0);

    /* No load reads it, and the numbering starts at 1 beside it. */
    CHECK_EQ(servo_store_load(&g_io, "hitec.txt", buf, sizeof(buf), &len),
             SERVO_STORE_BAD_NAME);
    CHECK_EQ(servo_store_load(&g_io, "HITEC.TXT", buf, sizeof(buf), &len),
             SERVO_STORE_BAD_NAME);
    CHECK_EQ(len, 0);
    CHECK_EQ(servo_store_preset(&g_io, name, sizeof(name)), SERVO_STORE_OK);
    CHECK_STR_EQ(name, "HITEC001");
    CHECK_EQ(g_card.count, 1);
}

TEST_CASE(a_stem_is_one_to_five_capital_letters_or_nothing_is_done)
{
    static const char *const bad[] = {"", "hitec", "HITECS", "HI1", "HI TE"};
    servo_store_io_t io = g_io;
    servo_store_entry_t e[2];
    char name[SERVO_STORE_NAME_MAX];
    size_t count = 99;

    card_reset();
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        io.stem = bad[i];
        CHECK_EQ(servo_store_preset(&io, name, sizeof(name)),
                 SERVO_STORE_ARG);
        CHECK_EQ(servo_store_save(&io, "WING", "x", 1u, NULL),
                 SERVO_STORE_ARG);
        CHECK_EQ(servo_store_list_after(&io, NULL, e, 2u, &count, NULL,
                                        NULL),
                 SERVO_STORE_ARG);
    }
    io.stem = NULL;
    CHECK_EQ(servo_store_preset(&io, name, sizeof(name)), SERVO_STORE_ARG);
    /* No stem reserves no name. */
    CHECK_EQ(servo_store_name_check(NULL, "HITEC"), SERVO_STORE_NAME_OK);
    CHECK_EQ(g_card.creates, 0u);
    /* One letter and five: the presets follow the stem. */
    io.stem = "K";
    CHECK_EQ(servo_store_preset(&io, name, sizeof(name)), SERVO_STORE_OK);
    CHECK_STR_EQ(name, "K001");
    io.stem = "ABCDE";
    CHECK_EQ(servo_store_preset(&io, name, sizeof(name)), SERVO_STORE_OK);
    CHECK_STR_EQ(name, "ABCDE001");
    CHECK_EQ(servo_store_preset(&io, name, SERVO_STORE_PRESET_MAX - 1u),
             SERVO_STORE_ARG);
}

TEST_CASE(a_list_needs_a_card_and_reports_a_directory_that_does_not_read)
{
    servo_store_entry_t e[2];
    size_t count = 99;

    card_reset();
    CHECK_EQ(servo_store_list_after(NULL, NULL, e, 2, &count, NULL, NULL), SERVO_STORE_ARG);
    CHECK_EQ(servo_store_list_after(&g_io, NULL, NULL, 2, &count, NULL, NULL), SERVO_STORE_ARG);
    CHECK_EQ(count, 99);

    /* An empty directory, and none: no file. */
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 2, &count, NULL, NULL), SERVO_STORE_OK);
    CHECK_EQ(count, 0);

    g_card.mounted = false;
    count = 99;
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 2, &count, NULL, NULL),
             SERVO_STORE_NO_CARD);
    CHECK_EQ(count, 99);
    g_card.mounted = true;

    /* What was read before the failure is in the list. */
    card_add("a.TXT", false, "x");
    g_card.walk_fails = true;
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 2, &count, NULL, NULL),
             SERVO_STORE_LIST_ERROR);
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
    CHECK_EQ(servo_store_load(&g_io, "Wing.TXT", buf, 6, &len), SERVO_STORE_OK);
    CHECK_EQ(len, 6);
    CHECK(memcmp(buf, "abcdef\x55\x55", 8) == 0);
}

TEST_CASE(each_failure_of_a_load_has_its_own_result)
{
    char buf[8];
    size_t len = 99;

    card_reset();
    card_add("Wing.TXT", false, "abcdef");
    CHECK_EQ(servo_store_load(NULL, "Wing.TXT", buf, 8, &len), SERVO_STORE_ARG);
    CHECK_EQ(servo_store_load(&g_io, NULL, buf, 8, &len), SERVO_STORE_ARG);
    CHECK_EQ(servo_store_load(&g_io, "Wing.TXT", NULL, 8, &len), SERVO_STORE_ARG);
    CHECK_EQ(servo_store_load(&g_io, "Wing.TXT", buf, 8, NULL), SERVO_STORE_ARG);
    CHECK_EQ(len, 99);

    /* A name with a path, without the extension, without a base name. */
    CHECK_EQ(servo_store_load(&g_io, "../Wing.TXT", buf, 8, &len),
             SERVO_STORE_BAD_NAME);
    CHECK_EQ(len, 0);
    CHECK_EQ(servo_store_load(&g_io, "Wing", buf, 8, &len), SERVO_STORE_BAD_NAME);
    CHECK_EQ(servo_store_load(&g_io, ".TXT", buf, 8, &len), SERVO_STORE_BAD_NAME);
    CHECK_EQ(servo_store_load(&g_io, "Wing.LOG", buf, 8, &len),
             SERVO_STORE_BAD_NAME);

    g_card.mounted = false;
    CHECK_EQ(servo_store_load(&g_io, "Wing.TXT", buf, 8, &len),
             SERVO_STORE_NO_CARD);
    g_card.mounted = true;

    CHECK_EQ(servo_store_load(&g_io, "Tail.TXT", buf, 8, &len),
             SERVO_STORE_NOT_FOUND);
    CHECK_EQ(servo_store_load(&g_io, "Wing.TXT", buf, 5, &len),
             SERVO_STORE_TOO_LARGE);
    CHECK_EQ(len, 0);

    g_card.read_result = -1;
    CHECK_EQ(servo_store_load(&g_io, "Wing.TXT", buf, 8, &len),
             SERVO_STORE_READ_ERROR);
    g_card.read_result = 5;     /* a short read */
    CHECK_EQ(servo_store_load(&g_io, "Wing.TXT", buf, 8, &len),
             SERVO_STORE_READ_ERROR);
    CHECK_EQ(len, 0);
}

TEST_CASE(a_saved_file_is_listed_and_loads_back)
{
    servo_store_entry_t e[2];
    char base[SERVO_STORE_PRESET_MAX];
    char buf[16];
    size_t count = 0;
    size_t len = 0;

    card_reset();
    CHECK_EQ(servo_store_preset(&g_io, base, sizeof(base)), SERVO_STORE_OK);
    CHECK_EQ(servo_store_save(&g_io, base, "first", 5, NULL), SERVO_STORE_OK);
    CHECK_EQ(servo_store_preset(&g_io, base, sizeof(base)), SERVO_STORE_OK);
    CHECK_STR_EQ(base, "HITEC002");
    CHECK_EQ(servo_store_save(&g_io, base, "second", 6, NULL), SERVO_STORE_OK);
    CHECK_EQ(servo_store_list_after(&g_io, NULL, e, 2, &count, NULL, NULL), SERVO_STORE_OK);
    CHECK_EQ(count, 2);
    /* The later file stands first. */
    CHECK_STR_EQ(e[0].name, "HITEC002.TXT");
    CHECK_STR_EQ(e[1].name, "HITEC001.TXT");
    CHECK_EQ(servo_store_load(&g_io, e[0].name, buf, sizeof(buf), &len),
             SERVO_STORE_OK);
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
    RUN(an_empty_directory_proposes_hitec001);
    RUN(the_proposal_is_one_above_the_highest_number_and_fills_no_gap);
    RUN(a_name_a_person_chose_counts_for_nothing_in_the_numbering);
    RUN(a_number_counts_in_any_case_and_on_a_directory);
    RUN(with_hitec999_taken_the_lowest_free_number_is_proposed);
    RUN(with_every_number_taken_no_name_is_proposed);
    RUN(a_proposal_needs_a_card_a_readable_directory_and_room_for_the_name);
    RUN(a_save_writes_the_bytes_under_the_name_with_the_extension);
    RUN(a_save_never_replaces_a_file_whatever_the_case_of_its_name);
    RUN(a_refused_name_gives_its_reason_and_touches_no_card);
    RUN(each_failure_of_a_save_has_its_own_result);
    RUN(a_card_that_takes_no_file_is_told_from_a_write_that_fails);
    RUN(the_list_holds_every_file_with_the_extension_sorted_without_case);
    RUN(names_equal_without_case_keep_one_order);
    RUN(a_list_shorter_than_the_directory_holds_the_first_names);
    RUN(the_proposed_names_stand_in_one_run_with_the_latest_first);
    RUN(a_list_goes_on_behind_a_name_and_reaches_every_file);
    RUN(a_file_whose_name_this_layer_would_not_write_is_counted_not_listed);
    RUN(the_record_of_a_write_is_no_name_no_list_entry_and_no_servo_file);
    RUN(a_stem_is_one_to_five_capital_letters_or_nothing_is_done);
    RUN(a_list_needs_a_card_and_reports_a_directory_that_does_not_read);
    RUN(a_load_gives_the_bytes_of_the_file_and_no_terminator);
    RUN(each_failure_of_a_load_has_its_own_result);
    RUN(a_saved_file_is_listed_and_loads_back);
    return test_summary("servo_store");
}
