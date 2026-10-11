/*
 * The card's side of the servo programmer's files (shared/bench/kst_card.c)
 * over a directory in memory: every answer a save, a list and a load give,
 * with the text of kst_file.h and the names of kst_store.h under it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "kst_card.h"
#include "kst_fake_card.h"
#include "kst_file.h"
#include "kst_limits.h"
#include "kst_reg.h"
#include "kst_sim.h"
#include "kst_store.h"

static fake_card_t g_c;
static kst_prog_card_done_t g_d;
static uint32_t g_id;

static char g_text[4][KST_FILE_MAX + 64u];

static void fresh(void)
{
    fake_card_init(&g_c);
    memset(&g_d, 0xA5, sizeof(g_d));
    memset(g_text, 0, sizeof(g_text));
}

static kst_image_t other_type(void)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_set(&img, KST_F_20K_SEL,
                        1u - kst_field_get(&img, KST_F_20K_SEL)));
    return img;
}

/* The raw value a line with the displayed value @p shown gives. */
static uint16_t raw_of(kst_field_id_t id, int32_t shown)
{
    uint16_t raw = 0;

    CHECK(kst_field_raw_from_display(id, shown, &raw));
    return raw;
}

static kst_image_t with(kst_field_id_t id, uint16_t raw)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_edit(&img, id, raw));
    return img;
}

/* One request; returns the result. */
static unsigned run(uint8_t op, const char *name, bool preset, unsigned first,
                    const kst_image_t *img)
{
    kst_prog_card_req_t q;

    memset(&q, 0, sizeof(q));
    q.op = op;
    q.id = ++g_id;
    q.preset = preset;
    q.first = (uint16_t)first;
    q.image = img != NULL ? *img : sim_bench_image();
    if (name != NULL) {
        snprintf(q.name, sizeof(q.name), "%s", name);
    }
    g_c.calls = 0;
    g_c.walks = 0;
    kst_card_run(&g_c.io, &q, &g_d);
    CHECK_EQ(g_d.id, g_id);
    return g_d.result;
}

static unsigned save(const char *name, const kst_image_t *img)
{
    return run(KST_PROG_CARD_SAVE, name, false, 0, img);
}

static unsigned preset(const kst_image_t *img)
{
    return run(KST_PROG_CARD_SAVE, "", true, 0, img);
}

static unsigned list(unsigned first)
{
    return run(KST_PROG_CARD_LIST, NULL, false, first, NULL);
}

static unsigned load(const char *name)
{
    return run(KST_PROG_CARD_LOAD, name, false, 0, NULL);
}

/* The text of an edited file of @p img in buffer @p slot: typed, or with
 * no type line and no register block. */
static char *edited(unsigned slot, const kst_image_t *img, bool typed)
{
    fake_file_t f;
    size_t n;

    memset(&f, 0, sizeof(f));
    f.img = *img;
    f.kind = typed ? KST_PROG_FILE_EDITED : KST_PROG_FILE_UNTYPED;
    f.type = typed ? (uint8_t)kst_card_type(img) : 0u;
    n = fake_card_text(&f, g_text[slot], KST_FILE_MAX);
    g_text[slot][n] = '\0';
    return g_text[slot];
}

/* Give the line of field @p id in @p text the value @p value; returns the
 * line's number, counted from 1. */
static unsigned set_line(char *text, kst_field_id_t id, const char *value)
{
    char head[80], tail[KST_FILE_MAX];
    char *at, *end;
    unsigned line = 1;

    snprintf(head, sizeof(head), "%s = ", kst_field(id)->name);
    at = strstr(text, head);
    CHECK(at != NULL);
    if (at == NULL) {
        return 0;
    }
    for (const char *p = text; p < at; ++p) {
        line += *p == '\n';
    }
    end = strchr(at, '\n');
    snprintf(tail, sizeof(tail), "%s", end);
    snprintf(at + strlen(head), KST_FILE_MAX - (size_t)(at - text), "%s%s",
             value, tail);
    return line;
}

/* --- cases ------------------------------------------------------------------ */

TEST_CASE(the_type_of_an_image_is_6_or_8)
{
    const kst_image_t sv = sim_bench_image(), ot = other_type();

    CHECK_EQ(kst_card_type(&sv) + kst_card_type(&ot), 14);
    CHECK(kst_card_type(&sv) == 6u || kst_card_type(&sv) == 8u);
    CHECK_EQ(kst_card_type(NULL), 0);
}

TEST_CASE(a_preset_save_takes_the_next_number_and_reads_back)
{
    const kst_image_t sv = sim_bench_image();
    const kst_image_t low = with(KST_F_DUTY, 200);

    fresh();
    CHECK_EQ(preset(&sv), KST_PROG_CARD_OK);
    CHECK_STR_EQ(g_d.name, "KST001");
    CHECK_EQ(g_c.n, 1);
    CHECK_STR_EQ(g_c.file[0].name, "KST001");
    CHECK(memcmp(g_c.file[0].img.r, sv.r, sizeof(sv.r)) == 0);
    CHECK_EQ(preset(&low), KST_PROG_CARD_OK);
    CHECK_STR_EQ(g_d.name, "KST002");
    CHECK_EQ(g_c.file[1].img.r[KST_REG_DUTY], 200);
    /* One above the highest number; a gap below it stays. */
    (void)fake_card_put(&g_c, "KST040", &sv);
    CHECK_EQ(preset(&sv), KST_PROG_CARD_OK);
    CHECK_STR_EQ(g_d.name, "KST041");
    /* With the last number taken: the lowest free one. */
    (void)fake_card_put(&g_c, "KST999", &sv);
    CHECK_EQ(preset(&sv), KST_PROG_CARD_OK);
    CHECK_STR_EQ(g_d.name, "KST003");
    CHECK_EQ(g_c.n, 6);
}

TEST_CASE(a_named_save_replaces_no_file_and_takes_no_bad_name)
{
    static const char *const bad[] = {
        "", "A:B", "A/B", "A*", "CON", "con", "LPT1", "NUL.X", "KST", "kst",
        ".A", "A.", " A", "A ",
    };
    const kst_image_t sv = sim_bench_image();

    fresh();
    CHECK_EQ(save("WING-L", &sv), KST_PROG_CARD_OK);
    CHECK_STR_EQ(g_d.name, "WING-L");
    CHECK_EQ(save("WING-L", &sv), KST_PROG_CARD_EXISTS);
    CHECK_EQ(save("wing-l", &sv), KST_PROG_CARD_EXISTS);
    CHECK_EQ(g_c.n, 1);
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK_EQ(save(bad[i], &sv), KST_PROG_CARD_NAME);
        CHECK_EQ(g_c.n, 1);
    }
    /* The longest name, and one with a full stop inside. */
    CHECK_EQ(save("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", &sv), KST_PROG_CARD_OK);
    CHECK_STR_EQ(g_d.name, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345");
    CHECK_EQ(save("A.B", &sv), KST_PROG_CARD_OK);
    CHECK_EQ(g_c.n, 3);
}

TEST_CASE(a_card_that_takes_no_file_says_which_way)
{
    const kst_image_t sv = sim_bench_image();

    fresh();
    g_c.full = true;
    CHECK_EQ(save("A", &sv), KST_PROG_CARD_FULL);
    CHECK_EQ(preset(&sv), KST_PROG_CARD_FULL);
    g_c.full = false;
    g_c.write_fails = true;
    CHECK_EQ(save("A", &sv), KST_PROG_CARD_WRITE_ERR);
    CHECK_EQ(preset(&sv), KST_PROG_CARD_WRITE_ERR);
    g_c.write_fails = false;
    /* A preset needs the directory: one that does not read gives no name. */
    g_c.walk_fails = true;
    CHECK_EQ(preset(&sv), KST_PROG_CARD_WRITE_ERR);
    g_c.walk_fails = false;
    g_c.no_card = true;
    CHECK_EQ(save("A", &sv), KST_PROG_CARD_NO_CARD);
    CHECK_EQ(preset(&sv), KST_PROG_CARD_NO_CARD);
    CHECK_EQ(list(0), KST_PROG_CARD_NO_CARD);
    CHECK_EQ(load("A"), KST_PROG_CARD_NO_CARD);
    CHECK_EQ(g_c.n, 0);
}

TEST_CASE(a_card_pulled_at_any_card_call_ends_the_request_without_a_file)
{
    const kst_image_t sv = sim_bench_image();
    const kst_image_t low = with(KST_F_DUTY, 200);

    for (unsigned at = 1; at <= 12u; ++at) {
        fresh();
        (void)fake_card_put(&g_c, "KST001", &low);
        (void)fake_card_put(&g_c, "WING", &low);

        g_c.pull_at = at;
        if (preset(&sv) == KST_PROG_CARD_OK) {
            /* Done before the card went. */
            CHECK(g_c.calls < at);
            CHECK_EQ(g_c.n, 3);
            g_c.n = 2;
        } else {
            CHECK(g_d.result == KST_PROG_CARD_NO_CARD
                  || g_d.result == KST_PROG_CARD_WRITE_ERR);
            CHECK_EQ(g_c.n, 2);
            CHECK_STR_EQ(g_d.name, "");
        }
        if (save("NEW", &sv) == KST_PROG_CARD_OK) {
            CHECK(g_c.calls < at);
            g_c.n = 2;
        } else {
            CHECK(g_d.result == KST_PROG_CARD_NO_CARD
                  || g_d.result == KST_PROG_CARD_WRITE_ERR);
            CHECK_EQ(g_c.n, 2);
        }
        if (list(0) == KST_PROG_CARD_OK) {
            /* The directory read whole; a file that does not read any more
             * has no type, and no preset name is given without a card. */
            CHECK_EQ(g_d.count, 2);
            if (g_c.calls >= at) {
                CHECK(g_d.types[1] == 0u || g_d.name[0] == '\0');
            }
        } else {
            CHECK(g_d.result == KST_PROG_CARD_NO_CARD
                  || g_d.result == KST_PROG_CARD_READ_ERR);
            CHECK_EQ(g_d.count, 0);
        }
        if (load("WING") == KST_PROG_CARD_OK) {
            CHECK(g_c.calls < at);
            CHECK_EQ(g_d.image.r[KST_REG_DUTY], 200);
        } else {
            CHECK(g_d.result == KST_PROG_CARD_NO_CARD
                  || g_d.result == KST_PROG_CARD_READ_ERR);
            /* Nothing of a file is in the answer. */
            CHECK_EQ(g_d.image.r[KST_REG_DUTY], 0);
        }
    }
}

TEST_CASE(a_list_is_sorted_typed_and_leaves_out_what_is_no_servo_file)
{
    const kst_image_t sv = sim_bench_image(), ot = other_type();
    fake_file_t *f;

    fresh();
    /* No directory: no entry, and the first preset name. */
    CHECK_EQ(list(0), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 0);
    CHECK_EQ(g_d.total, 0);
    CHECK_STR_EQ(g_d.name, "KST001");

    (void)fake_card_put(&g_c, "WING", &ot);
    (void)fake_card_put(&g_c, "KST001", &sv);
    f = fake_card_put(&g_c, "HAND", &sv);
    f->type = 0;
    (void)fake_card_put_text(&g_c, "JUNK", "not a servo file\n");
    fake_card_put(&g_c, "SUB", &sv)->dir = true;
    fake_card_put(&g_c, "NOTES", &sv)->as = "NOTES.DOC";
    /* The journal of a write lies in the directory and is no servo file. */
    (void)fake_card_put_text(&g_c, "KST", "enter\n");
    CHECK_EQ(list(0), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 4);
    CHECK_EQ(g_d.total, 4);
    CHECK_STR_EQ(g_d.names[0], "HAND");
    CHECK_STR_EQ(g_d.names[1], "JUNK");
    CHECK_STR_EQ(g_d.names[2], "KST001");
    CHECK_STR_EQ(g_d.names[3], "WING");
    CHECK_EQ(g_d.types[0], 0);
    CHECK_EQ(g_d.types[1], 0);
    CHECK_EQ(g_d.types[2], kst_card_type(&sv));
    CHECK_EQ(g_d.types[3], kst_card_type(&ot));
    CHECK_STR_EQ(g_d.name, "KST002");
    /* A file that does not read is listed without a type. */
    g_c.read_fails = true;
    CHECK_EQ(list(0), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 4);
    CHECK_EQ(g_d.types[2], 0);
    g_c.read_fails = false;
    /* From a file on, and past the last one. */
    CHECK_EQ(list(3), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 1);
    CHECK_EQ(g_d.total, 4);
    CHECK_STR_EQ(g_d.names[0], "WING");
    CHECK_EQ(list(4), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 0);
    CHECK_EQ(list(0xFFFFu), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 0);
    CHECK_EQ(g_d.total, 4);
    /* A directory that does not read to its end is no list. */
    g_c.walk_fails = true;
    CHECK_EQ(list(0), KST_PROG_CARD_READ_ERR);
    CHECK_EQ(g_d.count, 0);
}

/* A card of @p n saved files, KST001 to KST<n>, put on it in an order that
 * is not the list's. */
static void card_of(unsigned n)
{
    const kst_image_t sv = sim_bench_image();
    char name[KST_PROG_NAME_MAX];

    fresh();
    for (unsigned i = 0; i < n; ++i) {
        snprintf(name, sizeof(name), "KST%03u", (i * 37u) % n + 1u);
        (void)fake_card_put(&g_c, name, &sv);
    }
}

/* Every page of the list of a card of @p n files, the latest first. */
static void pages_reach_all(unsigned n)
{
    char name[KST_PROG_NAME_MAX];
    unsigned seen = 0;

    for (unsigned first = 0; first <= n; first += KST_PROG_FILES_MAX) {
        const unsigned left = n - first;

        CHECK_EQ(list(first), KST_PROG_CARD_OK);
        CHECK_EQ(g_d.total, n);
        CHECK_EQ(g_d.count, left < KST_PROG_FILES_MAX ? left
                                                      : KST_PROG_FILES_MAX);
        for (unsigned i = 0; i < g_d.count; ++i) {
            snprintf(name, sizeof(name), "KST%03u", n - first - i);
            CHECK_STR_EQ(g_d.names[i], name);
            CHECK(g_d.types[i] != 0u);
            seen++;
        }
    }
    CHECK_EQ(seen, n);
    snprintf(name, sizeof(name), "KST%03u", n + 1u);
    CHECK_STR_EQ(g_d.name, name);
}

TEST_CASE(a_list_of_64_65_and_more_files_reaches_each_one_latest_first)
{
    static const unsigned files[] = {
        1u, 5u, 6u, 63u, 64u, 65u, 69u, 70u, 128u, 129u, 200u,
    };

    _Static_assert(KST_CARD_LIST_MAX == 64u, "the cases stand at 64 files");
    _Static_assert(FAKE_CARD_FILES >= 200u, "the fake card holds the cases");
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        card_of(files[i]);
        pages_reach_all(files[i]);
    }
    /* 65 files: the page at the top holds the file saved last, and the one
     * file past 64 is listed and counted. */
    card_of(65);
    CHECK_EQ(list(0), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.total, 65);
    CHECK_STR_EQ(g_d.names[0], "KST065");
    CHECK_EQ(list(64), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 1);
    CHECK_STR_EQ(g_d.names[0], "KST001");
    /* Any first file, not only the start of a page. */
    card_of(200);
    for (unsigned first = 0; first < 203u; ++first) {
        char name[KST_PROG_NAME_MAX];

        CHECK_EQ(list(first), KST_PROG_CARD_OK);
        CHECK_EQ(g_d.total, 200);
        if (first >= 200u) {
            CHECK_EQ(g_d.count, 0);
            continue;
        }
        snprintf(name, sizeof(name), "KST%03u", 200u - first);
        CHECK_STR_EQ(g_d.names[0], name);
    }
    CHECK_EQ(list(0xFFFFu), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 0);
    CHECK_EQ(g_d.total, 200);
}

TEST_CASE(a_list_past_64_files_walks_the_directory_once_more_per_64)
{
    card_of(200);
    /* A page inside the first 64 names: the list and the preset name. */
    CHECK_EQ(list(0), KST_PROG_CARD_OK);
    CHECK_EQ(g_c.walks, 2);
    CHECK_EQ(list(59), KST_PROG_CARD_OK);
    CHECK_EQ(g_c.walks, 2);
    CHECK_EQ(list(60), KST_PROG_CARD_OK);
    CHECK_EQ(g_c.walks, 3);
    CHECK_EQ(list(195), KST_PROG_CARD_OK);
    CHECK_EQ(g_c.walks, 5);
    /* A directory that stops reading on a later walk is no list. */
    g_c.walk_fails_at = 2;
    CHECK_EQ(list(100), KST_PROG_CARD_READ_ERR);
    CHECK_EQ(g_d.count, 0);
    CHECK_EQ(g_d.total, 0);
    g_c.walk_fails_at = 0;
    /* The card pulled between two walks. */
    g_c.pull_at = 3;
    CHECK(list(100) != KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 0);
}

TEST_CASE(a_list_of_named_and_saved_files_pages_through_both)
{
    const kst_image_t sv = sim_bench_image();
    char name[KST_PROG_NAME_MAX];

    card_of(70);
    for (unsigned i = 0; i < 10u; ++i) {
        snprintf(name, sizeof(name), "%s%u", i < 5u ? "A" : "W", i);
        (void)fake_card_put(&g_c, name, &sv);
    }
    CHECK_EQ(list(0), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.total, 80);
    CHECK_STR_EQ(g_d.names[0], "A0");
    CHECK_STR_EQ(g_d.names[4], "A4");
    CHECK_EQ(list(5), KST_PROG_CARD_OK);
    CHECK_STR_EQ(g_d.names[0], "KST070");
    CHECK_EQ(list(73), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 5);
    CHECK_STR_EQ(g_d.names[0], "KST002");
    CHECK_STR_EQ(g_d.names[1], "KST001");
    CHECK_STR_EQ(g_d.names[2], "W5");
    CHECK_STR_EQ(g_d.names[4], "W7");
    CHECK_EQ(list(78), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.count, 2);
    CHECK_STR_EQ(g_d.names[1], "W9");
}

TEST_CASE(a_file_as_written_loads_its_editable_fields_only)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t img = with(KST_F_DUTY, 200);

    fresh();
    /* A locked field differs as well: it keeps the servo's value. */
    img.r[KST_REG_NODE_ADDR] ^= 0x01u;
    (void)fake_card_put(&g_c, "A", &img);
    CHECK_EQ(load("A"), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.kind, KST_PROG_FILE_VERIFIED);
    CHECK_EQ(g_d.type, kst_card_type(&sv));
    CHECK_EQ(g_d.image.r[KST_REG_DUTY], 200);
    CHECK_EQ(g_d.image.r[KST_REG_DUTY_COPY], 200);
    CHECK_EQ(g_d.image.r[KST_REG_NODE_ADDR], sv.r[KST_REG_NODE_ADDR]);
    CHECK(g_d.skipped != 0u);
    CHECK_EQ(g_d.field, 0xFF);
    CHECK_EQ(g_d.line, 0);
    /* The name in the other case is the same file. */
    CHECK_EQ(load("a"), KST_PROG_CARD_OK);
    /* A file equal to the servo. */
    (void)fake_card_put(&g_c, "SAME", &sv);
    CHECK_EQ(load("SAME"), KST_PROG_CARD_OK);
    CHECK(memcmp(g_d.image.r, sv.r, sizeof(sv.r)) == 0);
    CHECK_EQ(g_d.skipped, 0);
}

TEST_CASE(an_edited_file_loads_by_its_lines_with_or_without_a_type)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t ot = other_type();
    char *text;

    fresh();
    text = edited(0, &sv, true);
    CHECK(set_line(text, KST_F_DUTY, "50 %") != 0u);
    (void)fake_card_put_text(&g_c, "ED", text);
    CHECK_EQ(load("ED"), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.kind, KST_PROG_FILE_EDITED);
    CHECK_EQ(g_d.type, kst_card_type(&sv));
    CHECK_EQ(g_d.image.r[KST_REG_DUTY], raw_of(KST_F_DUTY, 50));
    CHECK_EQ(g_d.image.r[KST_REG_DUTY_COPY], raw_of(KST_F_DUTY, 50));
    CHECK_EQ(g_d.skipped, 0);
    /* No type, the lines of the other type: its 20k value is locked and
     * stays the servo's. */
    text = edited(1, &ot, false);
    CHECK(set_line(text, KST_F_DUTY, "50") != 0u);
    (void)fake_card_put_text(&g_c, "UN", text);
    CHECK_EQ(load("UN"), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.kind, KST_PROG_FILE_UNTYPED);
    CHECK_EQ(g_d.type, 0);
    CHECK_EQ(g_d.image.r[KST_REG_DUTY], raw_of(KST_F_DUTY, 50));
    CHECK_EQ(g_d.skipped, KST_FIELD_BIT(KST_F_20K_SEL));
    CHECK_EQ(kst_field_get(&g_d.image, KST_F_20K_SEL),
             kst_field_get(&sv, KST_F_20K_SEL));
    /* One line is a file. */
    snprintf(g_text[2], sizeof(g_text[2]),
             "KST servo configuration, format 1\n%s = 40\n",
             kst_field(KST_F_DUTY)->name);
    (void)fake_card_put_text(&g_c, "ONE", g_text[2]);
    CHECK_EQ(load("ONE"), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.kind, KST_PROG_FILE_UNTYPED);
    CHECK_EQ(g_d.image.r[KST_REG_DUTY], raw_of(KST_F_DUTY, 40));
}

TEST_CASE(a_file_of_the_other_type_is_refused_with_both_types)
{
    const kst_image_t sv = sim_bench_image(), ot = other_type();

    fresh();
    (void)fake_card_put(&g_c, "OT", &ot);
    CHECK_EQ(load("OT"), KST_PROG_CARD_TYPE);
    CHECK_EQ(g_d.type, kst_card_type(&ot));
    CHECK_EQ(g_d.servo_type, kst_card_type(&sv));
    (void)fake_card_put_text(&g_c, "ED", edited(0, &ot, true));
    CHECK_EQ(load("ED"), KST_PROG_CARD_TYPE);
    CHECK_EQ(g_d.kind, KST_PROG_FILE_EDITED);
    CHECK_EQ(g_d.type + g_d.servo_type, 14);
}

TEST_CASE(a_value_past_a_hard_limit_refuses_the_file_with_its_line_or_rule)
{
    const kst_image_t sv = sim_bench_image();
    const kst_image_t hard = with(KST_F_DUTY, 0xFFu);
    unsigned line;
    char *text;

    fresh();
    /* An edited file: the field and the line a person changes. */
    text = edited(0, &sv, true);
    line = set_line(text, KST_F_DUTY, "100 %");
    CHECK(line > 2u);
    (void)fake_card_put_text(&g_c, "ED", text);
    CHECK_EQ(load("ED"), KST_PROG_CARD_VALUE);
    CHECK_EQ(g_d.field, KST_F_DUTY);
    CHECK_EQ(g_d.line, line);
    CHECK_EQ(g_d.kind, KST_PROG_FILE_EDITED);
    /* At the limit the file loads. */
    text = edited(1, &sv, true);
    (void)set_line(text, KST_F_DUTY, "98 %");
    (void)fake_card_put_text(&g_c, "AT", text);
    CHECK_EQ(load("AT"), KST_PROG_CARD_OK);
    CHECK_EQ(g_d.image.r[KST_REG_DUTY], raw_of(KST_F_DUTY, 98));
    text = edited(2, &sv, true);
    (void)set_line(text, KST_F_DUTY, "9 %");
    (void)fake_card_put_text(&g_c, "LOW", text);
    CHECK_EQ(load("LOW"), KST_PROG_CARD_VALUE);
    CHECK_EQ(g_d.field, KST_F_DUTY);
    /* A file as written has no line a person wrote: the rule. */
    (void)fake_card_put(&g_c, "WR", &hard);
    CHECK_EQ(load("WR"), KST_PROG_CARD_LIMIT);
    CHECK_EQ(g_d.rule, KST_H_DUTY_HIGH);
    CHECK_EQ(g_d.line, 0);
    CHECK_EQ(g_d.field, 0xFF);
    CHECK_EQ(g_d.kind, KST_PROG_FILE_VERIFIED);
}

TEST_CASE(a_text_that_is_no_whole_servo_file_says_why_and_where)
{
    const kst_image_t sv = sim_bench_image();
    fake_file_t f;
    char *text, *at;
    size_t n;

    fresh();
    (void)fake_card_put_text(&g_c, "JUNK", "not a servo file\n");
    CHECK_EQ(load("JUNK"), KST_PROG_CARD_BAD);
    CHECK_EQ(g_d.line, 1);
    (void)fake_card_put_text(&g_c, "EMPTY", "");
    CHECK_EQ(load("EMPTY"), KST_PROG_CARD_BAD);
    /* A line no field has, in an edited file. */
    text = edited(0, &sv, true);
    n = strlen(text);
    snprintf(&text[n], KST_FILE_MAX - n, "Nonsense = 1\n");
    (void)fake_card_put_text(&g_c, "NAME", text);
    CHECK_EQ(load("NAME"), KST_PROG_CARD_BAD);
    CHECK_EQ(g_d.line, 30);
    /* A value its field does not encode. */
    text = edited(1, &sv, true);
    n = set_line(text, KST_F_DUTY, "many");
    (void)fake_card_put_text(&g_c, "VALUE", text);
    CHECK_EQ(load("VALUE"), KST_PROG_CARD_BAD);
    CHECK_EQ(g_d.line, n);
    /* A file as written with one character changed. */
    memset(&f, 0, sizeof(f));
    f.img = sv;
    f.kind = KST_PROG_FILE_VERIFIED;
    f.type = (uint8_t)kst_card_type(&sv);
    n = fake_card_text(&f, g_text[2], KST_FILE_MAX);
    g_text[2][n] = '\0';
    at = strstr(g_text[2], "registers 32\n");
    CHECK(at != NULL);
    at[strlen("registers 32\n") + 4u] ^= 0x01;
    (void)fake_card_put_text(&g_c, "DAMAGED", g_text[2]);
    CHECK_EQ(load("DAMAGED"), KST_PROG_CARD_CHECKSUM);
    /* A later format. */
    text = edited(3, &sv, true);
    at = strstr(text, "format 1");
    CHECK(at != NULL);
    at[7] = '2';
    (void)fake_card_put_text(&g_c, "NEWER", text);
    CHECK_EQ(load("NEWER"), KST_PROG_CARD_NEWER);
    /* Nothing of such a file is in the answer. */
    CHECK_EQ(g_d.image.r[KST_REG_DUTY], 0);
}

TEST_CASE(a_file_that_is_gone_too_large_or_does_not_read_is_no_load)
{
    static char big[KST_FILE_MAX + 2u];
    const kst_image_t sv = sim_bench_image();

    fresh();
    (void)fake_card_put(&g_c, "A", &sv);
    CHECK_EQ(load("GONE"), KST_PROG_CARD_READ_ERR);
    CHECK_EQ(load(""), KST_PROG_CARD_READ_ERR);
    CHECK_EQ(load("A:B"), KST_PROG_CARD_READ_ERR);
    /* The journal's name is no servo file's. */
    (void)fake_card_put_text(&g_c, "KST", "enter\n");
    CHECK_EQ(load("KST"), KST_PROG_CARD_READ_ERR);
    g_c.read_fails = true;
    CHECK_EQ(load("A"), KST_PROG_CARD_READ_ERR);
    g_c.read_fails = false;
    memset(big, '#', sizeof(big) - 1u);
    (void)fake_card_put_text(&g_c, "BIG", big);
    CHECK_EQ(load("BIG"), KST_PROG_CARD_BAD);
    CHECK_EQ(g_d.line, 0);
    /* A request that is none of the three. */
    CHECK_EQ(run(KST_PROG_CARD_IDLE, "A", false, 0, NULL),
             KST_PROG_CARD_READ_ERR);
    CHECK_EQ(run(99, "A", false, 0, NULL), KST_PROG_CARD_READ_ERR);
}

int main(void)
{
    RUN(the_type_of_an_image_is_6_or_8);
    RUN(a_preset_save_takes_the_next_number_and_reads_back);
    RUN(a_named_save_replaces_no_file_and_takes_no_bad_name);
    RUN(a_card_that_takes_no_file_says_which_way);
    RUN(a_card_pulled_at_any_card_call_ends_the_request_without_a_file);
    RUN(a_list_is_sorted_typed_and_leaves_out_what_is_no_servo_file);
    RUN(a_list_of_64_65_and_more_files_reaches_each_one_latest_first);
    RUN(a_list_past_64_files_walks_the_directory_once_more_per_64);
    RUN(a_list_of_named_and_saved_files_pages_through_both);
    RUN(a_file_as_written_loads_its_editable_fields_only);
    RUN(an_edited_file_loads_by_its_lines_with_or_without_a_type);
    RUN(a_file_of_the_other_type_is_refused_with_both_types);
    RUN(a_value_past_a_hard_limit_refuses_the_file_with_its_line_or_rule);
    RUN(a_text_that_is_no_whole_servo_file_says_why_and_where);
    RUN(a_file_that_is_gone_too_large_or_does_not_read_is_no_load);
    return test_summary("kst_card");
}
