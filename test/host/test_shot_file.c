/**
 * @file
 * @brief A screenshot on the card: the folder made, the next number, the name
 *        rules and every failure leaving no file of the real name.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#define REQUIRE(c)                                                            \
    do {                                                                      \
        if (!(c)) {                                                           \
            T_FAIL("required %s", #c);                                        \
            return;                                                           \
        }                                                                     \
    } while (0)

#include "fm_fake.h"
#include "fm_name.h"
#include "fm_ops.h"
#include "shot_file.h"

/** @brief The card. */
static fk_card_t g_card;
static fm_io_t   g_io;
static png_enc_t g_enc;
static uint8_t   g_chunk[FM_CHUNK];
static uint16_t  g_px[32 * 16];
static int       g_stop_at;
static int       g_rows;

/** @brief A card with nothing on it, the counters zero. */
static void fresh(void)
{
    fk_free(&g_card);
    fk_init(&g_card);
    g_io = fk_io(&g_card);
    g_stop_at = -1;
    g_rows = 0;
    for (size_t i = 0u; i < sizeof(g_px) / sizeof(g_px[0]); ++i) {
        g_px[i] = (uint16_t)(i * 97u);
    }
}

/**
 * @brief shot_save_t's cancelled().
 * @param ctx  unused
 * @return true from row g_stop_at on
 */
static bool stop_at(void *ctx)
{
    (void)ctx;
    return g_stop_at >= 0 && g_rows >= g_stop_at;
}

/**
 * @brief shot_save_t's progress().
 * @param done   rows done
 * @param total  rows in all
 * @param ctx    unused
 */
static void rows_done(int done, int total, void *ctx)
{
    (void)ctx;
    (void)total;
    g_rows = done;
}

/**
 * @brief A save of the test's picture.
 * @param base  the name
 * @return what it came to
 */
static fm_err_t save(const char *base)
{
    const shot_save_t s = {
        .io = &g_io, .pixels = g_px, .w = 32, .h = 16, .base = base,
        .enc = &g_enc, .chunk = g_chunk, .chunk_size = sizeof(g_chunk),
        .cancelled = stop_at, .progress = rows_done, .ctx = NULL,
    };
    return shot_file_save(&s, NULL, 0u);
}

TEST_CASE(the_folder_is_made_and_the_file_has_its_extension)
{
    fresh();
    CHECK_EQ(save("SHOT0001"), FM_OK);
    const fk_node_t *d = fk_get(&g_card, "screenshots");
    REQUIRE(d != NULL);
    CHECK(d->dir);
    const fk_node_t *f = fk_get(&g_card, "screenshots/SHOT0001.PNG");
    REQUIRE(f != NULL);
    CHECK(f->size > 57u);
    CHECK(memcmp(f->data, "\x89PNG", 4) == 0);
    CHECK(fk_get(&g_card, "screenshots/SHOT0001.PNG.NEW") == NULL);
    CHECK_EQ(g_rows, 16);
}

TEST_CASE(a_taken_name_is_refused_and_keeps_its_file)
{
    fresh();
    REQUIRE(save("A") == FM_OK);
    const fk_node_t *f = fk_get(&g_card, "screenshots/A.PNG");
    REQUIRE(f != NULL);
    const size_t len = f->size;
    g_px[0] ^= 0xFFFFu;
    g_rows = 0;
    CHECK_EQ(save("a"), FM_ERR_EXISTS);     /* FAT folds case */
    CHECK_EQ(g_rows, 0);                    /* and nothing was encoded */
    CHECK_EQ(fk_get(&g_card, "screenshots/A.PNG")->size, len);
    CHECK(fk_get(&g_card, "screenshots/a.PNG.NEW") == NULL);
}

TEST_CASE(names_follow_the_file_rules_with_the_extension_counted)
{
    char name[FM_NAME_BUF + 2];
    memset(name, 'A', sizeof(name));
    name[SHOT_BASE_MAX] = '\0';
    CHECK_EQ(shot_name_check(name), FM_NAME_OK);
    name[SHOT_BASE_MAX] = 'A';
    name[SHOT_BASE_MAX + 1u] = '\0';
    CHECK_EQ(shot_name_check(name), FM_NAME_LONG);
    CHECK_EQ(shot_name_check(""), FM_NAME_EMPTY);
    CHECK_EQ(shot_name_check("A/B"), FM_NAME_CHAR);
    CHECK_EQ(shot_name_check(" A"), FM_NAME_EDGE);
    fresh();
    CHECK_EQ(save("A:B"), FM_ERR_NAME);
    CHECK(fk_get(&g_card, "screenshots") == NULL);
}

TEST_CASE(no_card_is_said_and_writes_nothing)
{
    fresh();
    g_card.in = false;
    CHECK_EQ(save("A"), FM_ERR_NO_CARD);
    unsigned n = 7u;
    CHECK_EQ(shot_file_next(&g_io, &n), FM_ERR_NO_CARD);
    CHECK_EQ(n, 1);
}

TEST_CASE(a_full_card_and_a_write_error_leave_no_file)
{
    fresh();
    fk_fail(&g_card, FK_WRITE, 1, FM_ERR_FULL);
    CHECK_EQ(save("A"), FM_ERR_FULL);
    CHECK(fk_get(&g_card, "screenshots/A.PNG") == NULL);
    CHECK(fk_get(&g_card, "screenshots/A.PNG.NEW") == NULL);
    fresh();
    fk_fail(&g_card, FK_SYNC, 1, FM_ERR_IO);
    CHECK_EQ(save("A"), FM_ERR_IO);
    CHECK(fk_get(&g_card, "screenshots/A.PNG") == NULL);
    CHECK(fk_get(&g_card, "screenshots/A.PNG.NEW") == NULL);
    fresh();
    fk_fail(&g_card, FK_RENAME, 1, FM_ERR_IO);
    CHECK_EQ(save("A"), FM_ERR_IO);
    CHECK(fk_get(&g_card, "screenshots/A.PNG.NEW") == NULL);
    fresh();
    fk_fail(&g_card, FK_MKDIR, 1, FM_ERR_DENIED);
    CHECK_EQ(save("A"), FM_ERR_DENIED);
}

TEST_CASE(a_file_holding_the_folders_name_is_said)
{
    fresh();
    fk_file(&g_card, "screenshots", (const uint8_t *)"x", 1u);
    CHECK_EQ(save("A"), FM_ERR_NAME);
}

TEST_CASE(cancel_stops_at_a_row_and_leaves_no_file)
{
    fresh();
    g_stop_at = 3;
    CHECK_EQ(save("A"), FM_ERR_CANCELLED);
    CHECK_EQ(g_rows, 3);
    CHECK(fk_get(&g_card, "screenshots/A.PNG") == NULL);
    CHECK(fk_get(&g_card, "screenshots/A.PNG.NEW") == NULL);
}

TEST_CASE(a_new_a_power_cut_left_goes_with_the_next_save)
{
    fresh();
    fk_dir(&g_card, "screenshots");
    fk_file(&g_card, "screenshots/A.PNG.NEW", (const uint8_t *)"old", 3u);
    CHECK_EQ(save("A"), FM_OK);
    CHECK(fk_get(&g_card, "screenshots/A.PNG.NEW") == NULL);
    CHECK(fk_get(&g_card, "screenshots/A.PNG") != NULL);
}

TEST_CASE(the_next_number_is_one_above_the_highest)
{
    fresh();
    unsigned n = 0u;
    CHECK_EQ(shot_file_next(&g_io, &n), FM_OK);     /* no folder */
    CHECK_EQ(n, 1);
    fk_dir(&g_card, "screenshots");
    fk_file(&g_card, "screenshots/SHOT0002.PNG", (const uint8_t *)"x", 1u);
    fk_file(&g_card, "screenshots/shot0041.png", (const uint8_t *)"x", 1u);
    fk_file(&g_card, "screenshots/SHOTX.PNG", (const uint8_t *)"x", 1u);
    fk_file(&g_card, "screenshots/SHOT9.TXT", (const uint8_t *)"x", 1u);
    fk_file(&g_card, "screenshots/OTHER77.PNG", (const uint8_t *)"x", 1u);
    CHECK_EQ(shot_file_next(&g_io, &n), FM_OK);
    CHECK_EQ(n, 42);
    fk_file(&g_card, "screenshots/SHOT9999.PNG", (const uint8_t *)"x", 1u);
    CHECK_EQ(shot_file_next(&g_io, &n), FM_OK);
    CHECK_EQ(n, 9999);
    fk_file(&g_card, "screenshots/SHOT123456.PNG", (const uint8_t *)"x", 1u);
    CHECK_EQ(shot_file_next(&g_io, &n), FM_OK);
    CHECK_EQ(n, 9999);
}

int main(void)
{
    RUN(the_folder_is_made_and_the_file_has_its_extension);
    RUN(a_taken_name_is_refused_and_keeps_its_file);
    RUN(names_follow_the_file_rules_with_the_extension_counted);
    RUN(no_card_is_said_and_writes_nothing);
    RUN(a_full_card_and_a_write_error_leave_no_file);
    RUN(a_file_holding_the_folders_name_is_said);
    RUN(cancel_stops_at_a_row_and_leaves_no_file);
    RUN(a_new_a_power_cut_left_goes_with_the_next_save);
    RUN(the_next_number_is_one_above_the_highest);
    fk_free(&g_card);
    return test_summary("shot_file");
}
