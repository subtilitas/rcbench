/**
 * @file test_servo_id.c
 * @brief A labelled servo's id (servo_id.h): the serial at the edges of each
 *        base-36 digit and at ZZZZZ, the next id, the prefix at 1, 17 and 18
 *        characters and at each of its rules, an id split at its last "-",
 *        a serial of 4 and of 6 characters, the folder, and the count of a
 *        walk of Servos with its 6 highest ids.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "servo_id.h"
#include "settings.h"

_Static_assert(SERVO_ID_PREFIX_MAX == SETTINGS_PREFIX_MAX,
               "the ID PREFIX setting holds a whole prefix");
_Static_assert(SERVO_ID_SIZE == SETTINGS_TEXT_MAX,
               "an id fills a string setting");

/* ------------------------------------------------------------ the serial */

/** @brief Serial @p v as text, and back. */
static void serial_both_ways(uint32_t v, const char *text)
{
    char out[6] = "x";
    CHECK(servo_id_serial_text(v, out));
    CHECK_STR_EQ(out, text);
    CHECK_EQ(servo_id_serial_parse(text), v);
}

TEST_CASE(a_serial_counts_in_base_36_from_00000_to_zzzzz)
{
    serial_both_ways(0u, "00000");
    serial_both_ways(9u, "00009");
    serial_both_ways(10u, "0000A");
    serial_both_ways(35u, "0000Z");
    serial_both_ways(36u, "00010");
    serial_both_ways(36u * 36u, "00100");
    serial_both_ways(SERVO_ID_SERIALS - 2u, "ZZZZY");
    serial_both_ways(SERVO_ID_SERIALS - 1u, "ZZZZZ");
    CHECK_EQ(SERVO_ID_SERIALS - 1u, 60466175u);

    /* Past ZZZZZ there is no text. */
    char out[6] = "x";
    CHECK(!servo_id_serial_text(SERVO_ID_SERIALS, out));
    CHECK_STR_EQ(out, "");
    CHECK(!servo_id_serial_text(SERVO_ID_NO_SERIAL, out));
    CHECK(!servo_id_serial_text(0u, NULL));

    /* Lower case reads as upper case; anything else is no serial. */
    CHECK_EQ(servo_id_serial_parse("0000a"), 10u);
    CHECK_EQ(servo_id_serial_parse("zzzzz"), SERVO_ID_SERIALS - 1u);
    CHECK_EQ(servo_id_serial_parse("0000"), SERVO_ID_NO_SERIAL);
    CHECK_EQ(servo_id_serial_parse("000000"), SERVO_ID_NO_SERIAL);
    CHECK_EQ(servo_id_serial_parse("0000-"), SERVO_ID_NO_SERIAL);
    CHECK_EQ(servo_id_serial_parse("000 0"), SERVO_ID_NO_SERIAL);
    CHECK_EQ(servo_id_serial_parse("0000_"), SERVO_ID_NO_SERIAL);
    CHECK_EQ(servo_id_serial_parse(""), SERVO_ID_NO_SERIAL);
    CHECK_EQ(servo_id_serial_parse(NULL), SERVO_ID_NO_SERIAL);
}

TEST_CASE(the_next_serial_follows_the_highest_and_stops_at_zzzzz)
{
    uint32_t next = 77u;
    CHECK(servo_id_next(SERVO_ID_NO_SERIAL, &next));
    CHECK_EQ(next, 0u);                       /* a fresh card: 00000 */
    CHECK(servo_id_next(0u, &next));
    CHECK_EQ(next, 1u);
    CHECK(servo_id_next(35u, &next));
    CHECK_EQ(next, 36u);
    CHECK(servo_id_next(servo_id_serial_parse("ZZZZX"), &next));
    CHECK_EQ(next, servo_id_serial_parse("ZZZZY"));
    CHECK(servo_id_next(servo_id_serial_parse("ZZZZY"), &next));
    CHECK_EQ(next, servo_id_serial_parse("ZZZZZ"));
    /* At ZZZZZ: no wrap, next unchanged. */
    next = 77u;
    CHECK(!servo_id_next(servo_id_serial_parse("ZZZZZ"), &next));
    CHECK_EQ(next, 77u);
    CHECK(!servo_id_next(SERVO_ID_SERIALS, &next));
    CHECK(servo_id_next(5u, NULL));
}

/* ------------------------------------------------------------ the prefix */

TEST_CASE(a_prefix_is_1_to_17_characters_of_the_name_keyboard)
{
    char out[SERVO_ID_PREFIX_MAX + 1u] = "x";
    CHECK_EQ(servo_id_prefix_check("K", out, sizeof(out)), SERVO_ID_OK);
    CHECK_STR_EQ(out, "K");
    CHECK_EQ(servo_id_prefix_check("KST X10 LEFT WING", out, sizeof(out)),
             SERVO_ID_OK);
    CHECK_EQ(strlen(out), 17u);
    CHECK_STR_EQ(out, "KST X10 LEFT WING");
    strcpy(out, "kept");
    CHECK_EQ(servo_id_prefix_check("KST X10 LEFT WINGS", out, sizeof(out)),
             SERVO_ID_LONG);
    CHECK_STR_EQ(out, "kept");                /* left as it was */
    CHECK_EQ(servo_id_prefix_check("", out, sizeof(out)), SERVO_ID_EMPTY);
    CHECK_EQ(servo_id_prefix_check(NULL, out, sizeof(out)), SERVO_ID_EMPTY);

    /* Lower case is folded; "-", "_" and "." inside are kept. */
    CHECK_EQ(servo_id_prefix_check("kst_x10.a-b", out, sizeof(out)),
             SERVO_ID_OK);
    CHECK_STR_EQ(out, "KST_X10.A-B");

    /* A character outside the keyboard's. */
    CHECK_EQ(servo_id_prefix_check("KST/X10", NULL, 0u), SERVO_ID_CHAR);
    CHECK_EQ(servo_id_prefix_check("KST;X10", NULL, 0u), SERVO_ID_CHAR);
    CHECK_EQ(servo_id_prefix_check("K\xc3\x84", NULL, 0u), SERVO_ID_CHAR);
    /* The edges: a space at either end, a "." at the start. */
    CHECK_EQ(servo_id_prefix_check(" KST", NULL, 0u), SERVO_ID_EDGE);
    CHECK_EQ(servo_id_prefix_check("KST ", NULL, 0u), SERVO_ID_EDGE);
    CHECK_EQ(servo_id_prefix_check(".KST", NULL, 0u), SERVO_ID_EDGE);
    CHECK_EQ(servo_id_prefix_check("KST.", NULL, 0u), SERVO_ID_OK);
    CHECK_EQ(servo_id_prefix_check("K T", NULL, 0u), SERVO_ID_OK);
    /* An out too small for it. */
    char small[4];
    CHECK_EQ(servo_id_prefix_check("KST", small, sizeof(small)), SERVO_ID_OK);
    CHECK_EQ(servo_id_prefix_check("KSTX", small, sizeof(small)),
             SERVO_ID_LONG);
}

/* ---------------------------------------------------------------- the id */

TEST_CASE(an_id_is_built_from_a_prefix_and_a_serial)
{
    char id[SERVO_ID_SIZE] = "x";
    CHECK(servo_id_build(id, sizeof(id), "KST_X10", 10u));
    CHECK_STR_EQ(id, "KST_X10-0000A");
    CHECK(servo_id_build(id, sizeof(id), "kst x10 left wing", 0u));
    CHECK_STR_EQ(id, "KST X10 LEFT WING-00000");
    CHECK_EQ(strlen(id), SERVO_ID_MAX);
    CHECK(!servo_id_build(id, sizeof(id), "KST", SERVO_ID_SERIALS));
    CHECK_STR_EQ(id, "");
    CHECK(!servo_id_build(id, sizeof(id), ".KST", 1u));
    CHECK(!servo_id_build(id, sizeof(id), "", 1u));
    /* Room for the id but not its terminator. */
    char tight[9];
    CHECK(servo_id_build(tight, sizeof(tight), "KS", 1u));
    CHECK_STR_EQ(tight, "KS-00001");
    CHECK(!servo_id_build(tight, sizeof(tight), "KST", 1u));
    CHECK_STR_EQ(tight, "");
    CHECK(!servo_id_build(NULL, 8u, "KST", 1u));
    CHECK(!servo_id_build(tight, 0u, "KST", 1u));
}

TEST_CASE(an_id_splits_at_its_last_dash)
{
    servo_id_t p;
    CHECK(servo_id_parse("KST_X10-0000A", &p));
    CHECK_STR_EQ(p.text, "KST_X10-0000A");
    CHECK_STR_EQ(p.prefix, "KST_X10");
    CHECK_EQ(p.serial, 10u);

    CHECK(servo_id_parse("A-B-00001", &p));
    CHECK_STR_EQ(p.prefix, "A-B");
    CHECK_EQ(p.serial, 1u);

    /* A space inside, lower case, the longest. */
    CHECK(servo_id_parse("kst x10 left wing-zzzzz", &p));
    CHECK_STR_EQ(p.text, "KST X10 LEFT WING-ZZZZZ");
    CHECK_STR_EQ(p.prefix, "KST X10 LEFT WING");
    CHECK_EQ(p.serial, SERVO_ID_SERIALS - 1u);
    CHECK(servo_id_parse("K-00000", NULL));   /* 7 characters, the shortest */
}

TEST_CASE(an_id_names_the_first_rule_it_breaks)
{
    servo_id_t p;
    memset(&p, 0x5A, sizeof(p));
    CHECK(!servo_id_parse("KST-0001", &p));         /* 4 after the dash */
    CHECK_EQ(p.text[0], 0x5A);                     /* left as it was */
    CHECK_EQ(servo_id_why("KST-0001"), SERVO_ID_SERIAL);
    CHECK_EQ(servo_id_why("KST-000001"), SERVO_ID_SERIAL);
    CHECK_EQ(servo_id_why("KST00001"), SERVO_ID_SERIAL);
    CHECK_EQ(servo_id_why("KST-0000_"), SERVO_ID_SERIAL);
    CHECK_EQ(servo_id_why("KST-00001 "), SERVO_ID_SERIAL);
    CHECK_EQ(servo_id_why("https://x.example/KST-00001"), SERVO_ID_LONG);
    CHECK_EQ(servo_id_why("KST/X-00001"), SERVO_ID_CHAR);
    CHECK_EQ(servo_id_why("-00001"), SERVO_ID_EMPTY);
    CHECK_EQ(servo_id_why(""), SERVO_ID_EMPTY);
    CHECK_EQ(servo_id_why(NULL), SERVO_ID_EMPTY);
    CHECK_EQ(servo_id_why(".KST-00001"), SERVO_ID_EDGE);
    CHECK_EQ(servo_id_why(" KST-00001"), SERVO_ID_EDGE);
    CHECK_EQ(servo_id_why("KST -00001"), SERVO_ID_EDGE);
    /* 23 characters are an id, 24 are long; an 18-character prefix with a
     * serial is 24. */
    CHECK_EQ(servo_id_why("KST X10 LEFT WING-00001"), SERVO_ID_OK);
    CHECK_EQ(servo_id_why("KST X10 LEFT WINGS-00001"), SERVO_ID_LONG);
    CHECK(!servo_id_parse("KST X10 LEFT WINGS-00001", NULL));
}

TEST_CASE(a_servos_folder_is_servos_and_the_id_in_upper_case)
{
    char dir[CARD_RUN_DIR_MAX] = "x";
    CHECK(servo_id_folder(dir, sizeof(dir), "kst_x10-0000a"));
    CHECK_STR_EQ(dir, "Servos/KST_X10-0000A");
    CHECK(servo_id_folder(dir, sizeof(dir), "KST X10 LEFT WING-ZZZZZ"));
    CHECK_STR_EQ(dir, "Servos/KST X10 LEFT WING-ZZZZZ");
    CHECK_EQ(strlen(dir), 30u);
    CHECK(!servo_id_folder(dir, sizeof(dir), "KST-0001"));
    CHECK_STR_EQ(dir, "");
    char small[30];
    CHECK(!servo_id_folder(small, sizeof(small), "KST X10 LEFT WING-ZZZZZ"));
    CHECK_STR_EQ(small, "");
    CHECK(!servo_id_folder(NULL, 8u, "K-00000"));
    CHECK(!servo_id_folder(small, 0u, "K-00000"));
}

/* ---------------------------------------------------- the card's count */

TEST_CASE(a_walk_counts_id_folders_under_any_prefix)
{
    servo_ids_t s;
    servo_ids_begin(&s);
    CHECK_EQ(s.highest, SERVO_ID_NO_SERIAL);
    CHECK_EQ(s.count, 0u);
    CHECK_EQ(s.recent_n, 0u);

    servo_ids_visit(&s, "KST-00003", true);
    servo_ids_visit(&s, "MG90S-0000A", true);
    servo_ids_visit(&s, "kst-00004", true);        /* made on a computer */
    servo_ids_visit(&s, "notes", true);            /* no id               */
    servo_ids_visit(&s, "KST-00099", false);       /* a file              */
    servo_ids_visit(&s, "KST-0001", true);         /* 4 characters        */
    servo_ids_visit(&s, NULL, true);
    servo_ids_visit(NULL, "KST-00001", true);
    CHECK_EQ(s.count, 3u);
    CHECK_EQ(s.highest, 10u);
    CHECK_EQ(s.recent_n, 3u);
    CHECK_STR_EQ(s.recent[0], "MG90S-0000A");
    CHECK_STR_EQ(s.recent[1], "KST-00004");
    CHECK_STR_EQ(s.recent[2], "KST-00003");

    uint32_t next;
    CHECK(servo_id_next(s.highest, &next));
    CHECK_EQ(next, 11u);
}

TEST_CASE(a_walk_keeps_the_6_highest_ids_highest_first)
{
    servo_ids_t s;
    servo_ids_begin(&s);
    /* 9 ids in no order; one serial under two prefixes. */
    static const char *const seen[] = {
        "A-00005", "A-00001", "A-00009", "B-00007", "A-00002",
        "A-00008", "C-00007", "A-00003", "A-00006",
    };
    for (size_t i = 0u; i < sizeof(seen) / sizeof(seen[0]); ++i) {
        servo_ids_visit(&s, seen[i], true);
    }
    CHECK_EQ(s.count, 9u);
    CHECK_EQ(s.highest, 9u);
    CHECK_EQ(s.recent_n, SERVO_IDS_RECENT);
    static const char *const want[SERVO_IDS_RECENT] = {
        "A-00009", "A-00008", "B-00007", "C-00007", "A-00006", "A-00005",
    };
    for (size_t i = 0u; i < SERVO_IDS_RECENT; ++i) {
        CHECK_STR_EQ(s.recent[i], want[i]);
    }
    /* One below the sixth changes nothing; one above the first leads. */
    servo_ids_visit(&s, "A-00004", true);
    CHECK_STR_EQ(s.recent[5], "A-00005");
    servo_ids_visit(&s, "Z-ZZZZZ", true);
    CHECK_STR_EQ(s.recent[0], "Z-ZZZZZ");
    CHECK_STR_EQ(s.recent[5], "A-00006");
    CHECK_EQ(s.highest, SERVO_ID_SERIALS - 1u);
    CHECK(!servo_id_next(s.highest, NULL));        /* NEW ID: NO FREE ID */
}

TEST_CASE(the_count_of_id_folders_holds_at_65535)
{
    servo_ids_t s;
    servo_ids_begin(&s);
    s.count = 65534u;
    servo_ids_visit(&s, "A-00001", true);
    CHECK_EQ(s.count, 65535u);
    servo_ids_visit(&s, "A-00002", true);
    CHECK_EQ(s.count, 65535u);
    servo_ids_begin(NULL);
}

int main(void)
{
    RUN(a_serial_counts_in_base_36_from_00000_to_zzzzz);
    RUN(the_next_serial_follows_the_highest_and_stops_at_zzzzz);
    RUN(a_prefix_is_1_to_17_characters_of_the_name_keyboard);
    RUN(an_id_is_built_from_a_prefix_and_a_serial);
    RUN(an_id_splits_at_its_last_dash);
    RUN(an_id_names_the_first_rule_it_breaks);
    RUN(a_servos_folder_is_servos_and_the_id_in_upper_case);
    RUN(a_walk_counts_id_folders_under_any_prefix);
    RUN(a_walk_keeps_the_6_highest_ids_highest_first);
    RUN(the_count_of_id_folders_holds_at_65535);
    return test_summary("servo_id");
}
