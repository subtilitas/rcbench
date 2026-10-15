/*
 * Receiving a panel image over the Wi-Fi page into memory.
 *
 * The failure under test is an image that waits for the operator and
 * should not: one larger than the slot, one that arrived in part because
 * the connection dropped, or a second one that replaced the image the
 * question on the panel names.  The source here is the test, handing over
 * pieces as an HTTP (hypertext transfer protocol) handler would.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fw_fixture.h"
#include "fw_upload.h"

#define SLOT 0x3E0000u

static uint8_t *g_img;
static uint8_t *g_buf;
static uint32_t g_len;
static fw_upload_t g_up;

/* Hand over @p n bytes of the image in pieces of @p piece. */
static fw_err_t send(uint32_t n, uint32_t piece)
{
    for (uint32_t at = 0; at < n; at += piece) {
        const uint32_t k = (n - at < piece) ? n - at : piece;
        const fw_err_t e = fw_upload_add(&g_up, g_img + at, k);
        if (e != FW_OK) {
            return e;
        }
    }
    return FW_OK;
}

static void fresh(void)
{
    fw_fix_t f = fw_fix_default();
    g_len = fw_fix_make(&f, g_img);
    fw_upload_init(&g_up, g_buf, SLOT, SLOT);
}

TEST_CASE(a_whole_image_arrives_staged_and_checked)
{
    fresh();
    CHECK_EQ(fw_upload_begin(&g_up, g_len), FW_OK);
    CHECK_EQ(g_up.state, FW_UP_RECEIVING);
    CHECK_EQ(send(g_len, 1460u), FW_OK);     /* one TCP segment a piece */
    CHECK_EQ(fw_upload_percent(&g_up), 100u);
    CHECK_EQ(fw_upload_end(&g_up, true), FW_OK);
    CHECK_EQ(g_up.state, FW_UP_STAGED);
    CHECK_STR_EQ(g_up.info.version_text, "0.19.0");
    CHECK_EQ(memcmp(g_buf, g_img, g_len), 0);
}

TEST_CASE(nothing_and_more_than_the_slot_are_refused_before_a_byte)
{
    fresh();
    CHECK_EQ(fw_upload_begin(&g_up, 0u), FW_ERR_EMPTY);
    CHECK_EQ(g_up.state, FW_UP_IDLE);
    CHECK_EQ(fw_upload_begin(&g_up, SLOT + 1u), FW_ERR_TOO_BIG);
    CHECK_EQ(g_up.state, FW_UP_IDLE);
    /* The slot's size itself is taken. */
    CHECK_EQ(fw_upload_begin(&g_up, SLOT), FW_OK);
    fw_upload_discard(&g_up);
    /* A buffer smaller than the slot refuses what it cannot hold. */
    fw_upload_init(&g_up, g_buf, 1000u, SLOT);
    CHECK_EQ(fw_upload_begin(&g_up, 1001u), FW_ERR_MEMORY);
    fw_upload_init(&g_up, NULL, SLOT, SLOT);
    CHECK_EQ(fw_upload_begin(&g_up, 100u), FW_ERR_MEMORY);
}

TEST_CASE(a_connection_dropped_part_way_leaves_nothing_staged)
{
    fresh();
    CHECK_EQ(fw_upload_begin(&g_up, g_len), FW_OK);
    CHECK_EQ(send(g_len / 2u, 1460u), FW_OK);
    CHECK_EQ(fw_upload_percent(&g_up),
             (unsigned)((uint64_t)(g_len / 2u) * 100u / g_len));
    CHECK_EQ(fw_upload_end(&g_up, false), FW_ERR_READ);
    CHECK_EQ(g_up.state, FW_UP_FAILED);
    /* Ended as whole but short of what it announced: the same. */
    CHECK_EQ(fw_upload_begin(&g_up, g_len), FW_OK);
    CHECK_EQ(send(g_len - 1u, 4096u), FW_OK);
    CHECK_EQ(fw_upload_end(&g_up, true), FW_ERR_READ);
    CHECK_EQ(g_up.state, FW_UP_FAILED);
    /* And the next upload starts afresh. */
    CHECK_EQ(fw_upload_begin(&g_up, g_len), FW_OK);
    CHECK_EQ(send(g_len, 4096u), FW_OK);
    CHECK_EQ(fw_upload_end(&g_up, true), FW_OK);
}

TEST_CASE(more_bytes_than_announced_fail_the_upload)
{
    fresh();
    CHECK_EQ(fw_upload_begin(&g_up, g_len - 10u), FW_OK);
    CHECK_EQ(send(g_len, 4096u), FW_ERR_LENGTH);
    CHECK_EQ(g_up.state, FW_UP_FAILED);
    CHECK_EQ(g_up.err, FW_ERR_LENGTH);
}

TEST_CASE(a_second_upload_waits_for_the_first_to_be_answered)
{
    fresh();
    CHECK_EQ(fw_upload_begin(&g_up, g_len), FW_OK);
    /* While one is arriving. */
    CHECK_EQ(fw_upload_begin(&g_up, g_len), FW_ERR_BUSY);
    CHECK_EQ(send(g_len, 8192u), FW_OK);
    CHECK_EQ(fw_upload_end(&g_up, true), FW_OK);
    /* While one is staged: the question names this one, and a second
     * would change what INSTALL writes under it. */
    uint8_t first[64];
    memcpy(first, g_buf, sizeof(first));
    CHECK_EQ(fw_upload_begin(&g_up, 100u), FW_ERR_BUSY);
    CHECK_EQ(fw_upload_add(&g_up, g_img, 10u), FW_ERR_BUSY);
    CHECK_EQ(g_up.state, FW_UP_STAGED);
    CHECK_EQ(memcmp(first, g_buf, sizeof(first)), 0);
    /* CANCEL on the panel drops it, and the next is taken. */
    fw_upload_discard(&g_up);
    CHECK_EQ(g_up.state, FW_UP_IDLE);
    CHECK_EQ(fw_upload_begin(&g_up, g_len), FW_OK);
}

TEST_CASE(a_damaged_image_arrives_refused_with_its_reason)
{
    fresh();
    g_img[g_len / 2u] ^= 0x80u;
    CHECK_EQ(fw_upload_begin(&g_up, g_len), FW_OK);
    CHECK_EQ(send(g_len, 4096u), FW_OK);
    CHECK_EQ(fw_upload_end(&g_up, true), FW_ERR_HASH);
    CHECK_EQ(g_up.state, FW_UP_FAILED);
    CHECK_EQ(g_up.err, FW_ERR_HASH);
    /* A refused one does not hold the next back. */
    CHECK_EQ(fw_upload_begin(&g_up, g_len), FW_OK);
}

TEST_CASE(an_end_without_a_begin_changes_nothing)
{
    fresh();
    CHECK_EQ(fw_upload_end(&g_up, true), FW_ERR_BUSY);
    CHECK_EQ(g_up.state, FW_UP_IDLE);
    CHECK_EQ(fw_upload_percent(&g_up), 0u);
}

int main(void)
{
    g_img = malloc(SLOT + 64u);
    g_buf = malloc(SLOT);
    REQUIRE_ALLOC(g_img);
    REQUIRE_ALLOC(g_buf);
    RUN(a_whole_image_arrives_staged_and_checked);
    RUN(nothing_and_more_than_the_slot_are_refused_before_a_byte);
    RUN(a_connection_dropped_part_way_leaves_nothing_staged);
    RUN(more_bytes_than_announced_fail_the_upload);
    RUN(a_second_upload_waits_for_the_first_to_be_answered);
    RUN(a_damaged_image_arrives_refused_with_its_reason);
    RUN(an_end_without_a_begin_changes_nothing);
    free(g_img);
    free(g_buf);
    return test_summary("fw_upload");
}
