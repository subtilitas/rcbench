/*
 * Sending one card file to the browser a piece at a time.
 *
 * The failures under test: a download that holds the whole file, one that
 * sends a different number of bytes than its Content-Length announced, and
 * a read error or a pulled card that a browser would take as a complete
 * file.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "wa_fakefs.h"
#include "wa_send.h"

static uint8_t g_buf[WA_SEND_CHUNK];

/* Send node @p node announced as @p size; returns the bytes or -1. */
static int64_t send_all(fake_fs_t *fs, int node, uint64_t size, int *pieces)
{
    const wa_fs_t w = fake_wa(fs);
    wa_send_t s;
    if (!wa_send_begin(&s, &w, fs->n[node].path, size)) {
        return -2;
    }
    int64_t total = 0;
    *pieces = 0;
    for (;;) {
        const int32_t n = wa_send_next(&s, g_buf, sizeof(g_buf));
        if (n < 0) {
            CHECK(s.failed);
            CHECK_EQ(fs->files_open, 0);
            return -1;
        }
        if (n == 0) {
            CHECK_EQ(fs->files_open, 0);
            CHECK_EQ(wa_send_next(&s, g_buf, sizeof(g_buf)), 0);
            return total;
        }
        CHECK(n <= (int32_t)WA_SEND_CHUNK);
        for (int32_t k = 0; k < n; ++k) {
            if (g_buf[k] != fake_byte(node, (uint64_t)total + (uint64_t)k)) {
                CHECK(!"a byte differs");
                return -3;
            }
        }
        total += n;
        ++*pieces;
    }
}

TEST_CASE(an_empty_file_is_sent_as_no_bytes)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "empty.csv", false, 0u, 0);
    int pieces = -1;
    CHECK_EQ(send_all(&fs, f, 0u, &pieces), 0);
    CHECK_EQ(pieces, 0);
    fake_free(&fs);
}

TEST_CASE(a_file_goes_in_pieces_of_4096_bytes)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int a = fake_add(&fs, "a", false, 3u * WA_SEND_CHUNK, 0);
    const int b = fake_add(&fs, "b", false, 3u * WA_SEND_CHUNK + 1u, 0);
    const int c = fake_add(&fs, "c", false, 1u, 0);
    const int d = fake_add(&fs, "d", false, WA_SEND_CHUNK - 1u, 0);
    int pieces = 0;
    CHECK_EQ(send_all(&fs, a, 3u * WA_SEND_CHUNK, &pieces),
             3 * (int64_t)WA_SEND_CHUNK);
    CHECK_EQ(pieces, 3);
    CHECK_EQ(send_all(&fs, b, 3u * WA_SEND_CHUNK + 1u, &pieces),
             3 * (int64_t)WA_SEND_CHUNK + 1);
    CHECK_EQ(pieces, 4);
    CHECK_EQ(send_all(&fs, c, 1u, &pieces), 1);
    CHECK_EQ(send_all(&fs, d, WA_SEND_CHUNK - 1u, &pieces),
             (int64_t)WA_SEND_CHUNK - 1);
    CHECK_EQ(pieces, 1);
    fake_free(&fs);
}

TEST_CASE(a_read_error_part_way_fails_the_download)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 20000u, 0);
    /* At the start, inside a piece, at a piece's edge, at the last byte. */
    const int64_t at[] = { 0, 5000, 8192, 19999 };
    for (int i = 0; i < 4; ++i) {
        fs.n[f].fail_at = at[i];
        int pieces = 0;
        CHECK_EQ(send_all(&fs, f, 20000u, &pieces), -1);
    }
    fs.n[f].fail_at = -1;
    fake_free(&fs);
}

TEST_CASE(a_file_shorter_or_longer_than_announced_fails)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 10000u, 0);
    int pieces = 0;
    /* It ends early: a card pulled, or the file cut since the listing. */
    fs.n[f].end_at = 6000;
    CHECK_EQ(send_all(&fs, f, 10000u, &pieces), -1);
    fs.n[f].end_at = 8192;
    CHECK_EQ(send_all(&fs, f, 10000u, &pieces), -1);
    fs.n[f].end_at = -1;
    /* It runs long: grown since the listing. */
    CHECK_EQ(send_all(&fs, f, 8192u, &pieces), -1);
    CHECK_EQ(send_all(&fs, f, 0u, &pieces), -1);
    CHECK_EQ(send_all(&fs, f, 10000u, &pieces), 10000);
    fake_free(&fs);
}

TEST_CASE(a_card_pulled_between_pieces_fails_and_closes)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 10000u, 0);
    const wa_fs_t w = fake_wa(&fs);
    wa_send_t s;
    CHECK(wa_send_begin(&s, &w, "f", 10000u));
    CHECK_EQ(wa_send_next(&s, g_buf, sizeof(g_buf)), (int32_t)WA_SEND_CHUNK);
    fs.pulled = true;
    CHECK_EQ(wa_send_next(&s, g_buf, sizeof(g_buf)), -1);
    CHECK_EQ(wa_send_next(&s, g_buf, sizeof(g_buf)), -1);
    CHECK_EQ(fs.files_open, 0);
    /* Gone before it opened. */
    CHECK(!wa_send_begin(&s, &w, "f", 10000u));
    fs.pulled = false;
    CHECK(!wa_send_begin(&s, &w, "missing", 1u));
    CHECK(!wa_send_begin(&s, NULL, "f", 1u));
    /* Given up part way by the server: closed. */
    CHECK(wa_send_begin(&s, &w, "f", 10000u));
    CHECK_EQ(wa_send_next(&s, g_buf, 100u), 100);
    CHECK_EQ(fs.files_open, 1);
    wa_send_end(&s);
    CHECK_EQ(fs.files_open, 0);
    (void)f;
    fake_free(&fs);
}

int main(void)
{
    RUN(an_empty_file_is_sent_as_no_bytes);
    RUN(a_file_goes_in_pieces_of_4096_bytes);
    RUN(a_read_error_part_way_fails_the_download);
    RUN(a_file_shorter_or_longer_than_announced_fails);
    RUN(a_card_pulled_between_pieces_fails_and_closes);
    return test_summary("wa_send");
}
