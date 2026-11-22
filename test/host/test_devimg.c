/*
 * The device images the panel fetches when an update asks for one
 * (shared/devupd/devimg.c): the release's file names and the card's paths,
 * each image's slot as the size limit at one byte under, at and over it,
 * every check an image passes before a sender gets it (the SHA-256 of
 * SHA256SUMS, the application checks, a node role sent to the other node,
 * another version), where each hold takes its image from (GitHub, the card
 * after a failed download, GitHub again after a failed card read, the card
 * without a home network, memory once it arrived), the failure codes, and
 * the update owed to the hold: started once its gate opens, kept while the
 * radio goes off up to DEVIMG_OWED_MS and not a millisecond more, across
 * the 32-bit millisecond wrap, and dropped for any other gate.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "bleota.h"
#include "devimg.h"
#include "devupd.h"
#include "fw_c6.h"
#include "fw_fixture.h"

static uint8_t *g_img;

static const fw_version_t k_hosted = { 2u, 13u, 0u };
static const fw_version_t k_panel = { 0u, 23u, 16u };

/* An application for @p chip and @p project at @p version; with @p kind
 * not 0 also the role a Dial or a node reads in its head, the checksum and
 * the appended SHA-256 written again over it. */
static uint32_t make(uint16_t chip, const char *project, const char *version,
                     uint8_t kind)
{
    fw_fix_t f = fw_fix_default();
    f.chip = chip;
    f.project = project;
    f.version = version;
    f.body = 4000u;
    const uint32_t len = fw_fix_make(&f, g_img);
    if (kind == 0u) {
        return len;
    }
    uint8_t role[BLEOTA_ROLE_LEN];
    memset(role, 0, sizeof(role));
    fw_put32(role, BLEOTA_ROLE_MAGIC);
    role[4] = kind;
    role[5] = BLEOTA_PROTO;
    uint8_t x = 0u;
    for (unsigned i = 0; i < BLEOTA_ROLE_LEN; ++i) {
        x ^= (uint8_t)(g_img[BLEOTA_HEAD_ROLE + i] ^ role[i]);
        g_img[BLEOTA_HEAD_ROLE + i] = role[i];
    }
    g_img[len - 33u] ^= x;               /* the checksum byte */
    fw_sha256(g_img, len - 32u, g_img + len - 32u);
    return len;
}

static uint32_t dial(const char *version)
{
    return make(DEVUPD_CHIP_ESP32S3, DEVUPD_PROJECT_DIAL, version,
                BLEOTA_KIND_DIAL);
}

/* ---------------------------------------------------------- the names */

TEST_CASE(each_image_has_the_release_asset_name)
{
    char n[DEVIMG_NAME_MAX];
    CHECK(devimg_name(DEVIMG_C6, k_hosted, n, sizeof(n)));
    CHECK_STR_EQ(n, "rcbench-c6-2.13.0.bin");
    CHECK(devimg_name(DEVIMG_DIAL, k_panel, n, sizeof(n)));
    CHECK_STR_EQ(n, "rcbench-dial-0.23.16.bin");
    CHECK(devimg_name(DEVIMG_PNODE, k_panel, n, sizeof(n)));
    CHECK_STR_EQ(n, "rcbench-node-pwr-0.23.16.bin");
    CHECK(devimg_name(DEVIMG_BAL, k_panel, n, sizeof(n)));
    CHECK_STR_EQ(n, "rcbench-node-bal-0.23.16.bin");
    CHECK(!devimg_name(DEVIMG_COUNT, k_panel, n, sizeof(n)));
}

TEST_CASE(the_widest_name_and_path_fit_their_bounds_and_one_byte_less_fails)
{
    const fw_version_t wide = { 65535u, 65535u, 65535u };
    char n[DEVIMG_NAME_MAX];
    CHECK(devimg_name(DEVIMG_PNODE, wide, n, sizeof(n)));
    CHECK_EQ(strlen(n), 38u);
    CHECK(!devimg_name(DEVIMG_PNODE, wide, n, 38u));
    CHECK(devimg_name(DEVIMG_PNODE, wide, n, 39u));
    char p[DEVIMG_PATH_MAX];
    CHECK(devimg_card_path(DEVIMG_PNODE, wide, p, sizeof(p)));
    CHECK_STR_EQ(p, "BENCH/UPDATES/rcbench-node-pwr-65535.65535.65535.bin");
    CHECK(!devimg_card_path(DEVIMG_PNODE, wide, p, strlen(p)));
    CHECK(devimg_card_path(DEVIMG_DIAL, k_panel, p, sizeof(p)));
    CHECK_STR_EQ(p, "BENCH/UPDATES/rcbench-dial-0.23.16.bin");
}

TEST_CASE(each_image_is_held_to_its_devices_slot)
{
    CHECK_EQ(devimg_slot(DEVIMG_C6), FW_C6_SLOT_BYTES);
    CHECK_EQ(devimg_slot(DEVIMG_DIAL), DEVUPD_SLOT_DIAL);
    CHECK_EQ(devimg_slot(DEVIMG_PNODE), DEVUPD_SLOT_NODE);
    CHECK_EQ(devimg_slot(DEVIMG_BAL), DEVUPD_SLOT_NODE);
    CHECK_EQ(devimg_slot(DEVIMG_COUNT), 0u);
}

/* --------------------------------------------------------- the checks */

TEST_CASE(a_c6_image_of_the_wanted_version_passes_with_its_sha)
{
    const uint32_t len = make(FW_CHIP_ESP32C6, FW_C6_PROJECT, "2.13.0", 0u);
    uint8_t sha[FW_SHA256_BYTES], want[FW_SHA256_BYTES];
    fw_sha256(g_img, len, want);
    fw_image_info_t info;
    CHECK_EQ(devimg_verify(DEVIMG_C6, g_img, len, k_hosted, want, &info, sha,
                           NULL), DEVIMG_OK);
    CHECK_EQ(memcmp(sha, want, sizeof(sha)), 0);
    CHECK_EQ(info.version.minor, 13u);
    /* No SHA-256 to hold it to: the card without SHA256SUMS. */
    CHECK_EQ(devimg_verify(DEVIMG_C6, g_img, len, k_hosted, NULL, NULL, NULL,
                           NULL), DEVIMG_OK);
}

TEST_CASE(a_sha_other_than_the_releases_is_refused)
{
    const uint32_t len = make(FW_CHIP_ESP32C6, FW_C6_PROJECT, "2.13.0", 0u);
    uint8_t want[FW_SHA256_BYTES];
    fw_sha256(g_img, len, want);
    want[31] ^= 1u;
    CHECK_EQ(devimg_verify(DEVIMG_C6, g_img, len, k_hosted, want, NULL, NULL,
                           NULL), DEVIMG_E_HASH);
}

TEST_CASE(another_version_than_the_panels_release_is_refused)
{
    uint32_t len = make(FW_CHIP_ESP32C6, FW_C6_PROJECT, "2.12.9", 0u);
    CHECK_EQ(devimg_verify(DEVIMG_C6, g_img, len, k_hosted, NULL, NULL, NULL,
                           NULL), DEVIMG_E_VERSION);
    len = dial("0.23.15");
    CHECK_EQ(devimg_verify(DEVIMG_DIAL, g_img, len, k_panel, NULL, NULL, NULL,
                           NULL), DEVIMG_E_VERSION);
}

TEST_CASE(a_dial_image_passes_and_a_node_role_or_chip_is_refused)
{
    uint32_t len = dial("0.23.16");
    CHECK_EQ(devimg_verify(DEVIMG_DIAL, g_img, len, k_panel, NULL, NULL, NULL,
                           NULL), DEVIMG_OK);
    /* The power node's image offered as the balance node's: same chip,
     * same project, another role. */
    len = make(FW_CHIP_ESP32C6, DEVUPD_PROJECT_NODE, "0.23.16",
               BLEOTA_KIND_NODE_POWER);
    CHECK_EQ(devimg_verify(DEVIMG_PNODE, g_img, len, k_panel, NULL, NULL,
                           NULL, NULL), DEVIMG_OK);
    CHECK_EQ(devimg_verify(DEVIMG_BAL, g_img, len, k_panel, NULL, NULL, NULL,
                           NULL), DEVIMG_E_ROLE);
    /* A Dial image offered to a node: another chip. */
    len = dial("0.23.16");
    fw_err_t why = FW_OK;
    CHECK_EQ(devimg_verify(DEVIMG_PNODE, g_img, len, k_panel, NULL, NULL,
                           NULL, &why), DEVIMG_E_CHECK);
    CHECK(why != FW_OK);
}

TEST_CASE(a_damaged_image_fails_its_own_appended_sha)
{
    const uint32_t len = dial("0.23.16");
    g_img[1000] ^= 0x40u;
    fw_err_t why = FW_OK;
    CHECK_EQ(devimg_verify(DEVIMG_DIAL, g_img, len, k_panel, NULL, NULL, NULL,
                           &why), DEVIMG_E_CHECK);
    CHECK(why != FW_OK);
}

TEST_CASE(the_size_limit_is_the_slot_at_one_byte_under_at_and_over)
{
    const uint32_t slot = devimg_slot(DEVIMG_C6);
    uint8_t *big = calloc(slot + 1u, 1u);
    REQUIRE_ALLOC(big);
    /* At and under the slot the size passes, and the checks that follow
     * refuse the zeros. */
    CHECK_EQ(devimg_verify(DEVIMG_C6, big, slot, k_hosted, NULL, NULL, NULL,
                           NULL), DEVIMG_E_CHECK);
    CHECK_EQ(devimg_verify(DEVIMG_C6, big, slot - 1u, k_hosted, NULL, NULL,
                           NULL, NULL), DEVIMG_E_CHECK);
    CHECK_EQ(devimg_verify(DEVIMG_C6, big, slot + 1u, k_hosted, NULL, NULL,
                           NULL, NULL), DEVIMG_E_SIZE);
    CHECK_EQ(devimg_verify(DEVIMG_C6, big, 0u, k_hosted, NULL, NULL, NULL,
                           NULL), DEVIMG_E_SIZE);
    CHECK_EQ(devimg_verify(DEVIMG_C6, NULL, 100u, k_hosted, NULL, NULL, NULL,
                           NULL), DEVIMG_E_SIZE);
    CHECK_EQ(devimg_verify(DEVIMG_COUNT, big, 100u, k_hosted, NULL, NULL,
                           NULL, NULL), DEVIMG_E_ID);
    free(big);
}

/* --------------------------------------------- where the hold takes it */

TEST_CASE(a_hold_downloads_and_without_a_network_reads_the_card)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK_EQ(devimg_src(&s, true), DEVIMG_SRC_NET);
    CHECK_EQ(devimg_src(&s, false), DEVIMG_SRC_CARD);
    CHECK(!devimg_busy(&s));
    CHECK(devimg_begin(&s, DEVIMG_SRC_NET));
    CHECK_EQ(s.st, DEVIMG_FETCH);
    CHECK(devimg_busy(&s));
    CHECK(s.owed);
    /* A second hold while one runs changes nothing. */
    CHECK(!devimg_begin(&s, DEVIMG_SRC_CARD));
    CHECK_EQ(s.st, DEVIMG_FETCH);
}

TEST_CASE(a_failed_download_sends_the_next_hold_to_the_card_and_back)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK(devimg_begin(&s, devimg_src(&s, true)));
    devimg_failed(&s, 'D', 9u);
    CHECK_EQ(s.st, DEVIMG_IDLE);
    CHECK(!s.owed);
    CHECK_STR_EQ(s.code, "D09");
    CHECK_EQ(devimg_src(&s, true), DEVIMG_SRC_CARD);
    CHECK(devimg_begin(&s, devimg_src(&s, true)));
    CHECK_EQ(s.st, DEVIMG_CARD);
    CHECK_STR_EQ(s.code, "");
    devimg_failed(&s, 'S', 2u);
    CHECK_STR_EQ(s.code, "S02");
    CHECK_EQ(devimg_src(&s, true), DEVIMG_SRC_NET);
    /* Without a network it stays on the card. */
    CHECK_EQ(devimg_src(&s, false), DEVIMG_SRC_CARD);
}

TEST_CASE(a_downloaded_file_that_fails_its_checks_sends_the_next_to_the_card)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_NET));
    devimg_fetched(&s);
    CHECK_EQ(s.st, DEVIMG_VERIFY);
    CHECK(devimg_busy(&s));
    devimg_failed(&s, 'V', (unsigned)DEVIMG_E_VERSION);
    CHECK_STR_EQ(s.code, "V05");
    CHECK_EQ(devimg_src(&s, true), DEVIMG_SRC_CARD);
}

TEST_CASE(an_image_that_arrived_stays_in_memory_for_every_later_hold)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_CARD));
    devimg_ready(&s, 500u);
    CHECK_EQ(s.st, DEVIMG_READY);
    CHECK_EQ(devimg_src(&s, true), DEVIMG_SRC_RAM);
    CHECK_EQ(devimg_src(&s, false), DEVIMG_SRC_RAM);
    CHECK(!devimg_begin(&s, DEVIMG_SRC_NET));
    CHECK(!devimg_begin(&s, DEVIMG_SRC_RAM));
    /* A failure or a fetch end outside its state changes nothing. */
    devimg_failed(&s, 'D', 3u);
    devimg_fetched(&s);
    CHECK_EQ(s.st, DEVIMG_READY);
    CHECK_STR_EQ(s.code, "");
}

TEST_CASE(a_code_past_99_reads_99)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_NET));
    devimg_failed(&s, 'D', 100u);
    CHECK_STR_EQ(s.code, "D99");
    CHECK(devimg_begin(&s, DEVIMG_SRC_CARD));
    devimg_failed(&s, 'S', 0u);
    CHECK_STR_EQ(s.code, "S00");
}

/* ------------------------------------------------------ the owed start */

TEST_CASE(the_owed_update_starts_once_its_gate_opens)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK_EQ(devimg_owed_step(&s, true, false, 0u), DEVIMG_OWED_NONE);
    CHECK(devimg_begin(&s, DEVIMG_SRC_NET));
    CHECK_EQ(devimg_owed_step(&s, true, false, 0u), DEVIMG_OWED_WAIT);
    devimg_fetched(&s);
    CHECK_EQ(devimg_owed_step(&s, true, false, 0u), DEVIMG_OWED_WAIT);
    devimg_ready(&s, 1000u);
    CHECK_EQ(devimg_owed_step(&s, false, true, 1000u), DEVIMG_OWED_WAIT);
    CHECK_EQ(devimg_owed_step(&s, true, false, 1500u), DEVIMG_OWED_FIRE);
    CHECK(!s.owed);
    CHECK_EQ(devimg_owed_step(&s, true, false, 1600u), DEVIMG_OWED_NONE);
}

TEST_CASE(the_radios_wait_ends_at_DEVIMG_OWED_MS_not_a_millisecond_before)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_NET));
    devimg_fetched(&s);
    devimg_ready(&s, 1000u);
    CHECK_EQ(devimg_owed_step(&s, false, true, 1000u + DEVIMG_OWED_MS - 1u),
             DEVIMG_OWED_WAIT);
    CHECK_EQ(devimg_owed_step(&s, false, true, 1000u + DEVIMG_OWED_MS),
             DEVIMG_OWED_DROP);
    CHECK(!s.owed);
    /* The image stays: the next hold takes it from memory. */
    CHECK_EQ(devimg_src(&s, true), DEVIMG_SRC_RAM);
}

TEST_CASE(the_wait_holds_across_the_millisecond_wrap)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_CARD));
    const uint32_t t0 = 0xFFFFFFFFu - 5000u;
    devimg_ready(&s, t0);
    CHECK_EQ(devimg_owed_step(&s, false, true, t0 + DEVIMG_OWED_MS - 1u),
             DEVIMG_OWED_WAIT);
    CHECK_EQ(devimg_owed_step(&s, false, true, t0 + DEVIMG_OWED_MS),
             DEVIMG_OWED_DROP);
}

TEST_CASE(any_other_closed_gate_drops_the_owed_update_at_once)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_NET));
    devimg_fetched(&s);
    devimg_ready(&s, 0u);
    /* ARMED, the supply, a log run: not a wait. */
    CHECK_EQ(devimg_owed_step(&s, false, false, 1u), DEVIMG_OWED_DROP);
    CHECK_EQ(devimg_owed_step(&s, true, false, 2u), DEVIMG_OWED_NONE);
}

TEST_CASE(a_failed_attempt_owes_no_update)
{
    devimg_slot_t s;
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_NET));
    devimg_failed(&s, 'D', 6u);
    CHECK_EQ(devimg_owed_step(&s, true, false, 0u), DEVIMG_OWED_NONE);
}

/* CANCEL UPDATE in each phase of the image's way: the card read, the
 * download, the downloaded file's checks, and READY while the radio's
 * wait still owes the start.  The image still lands for the next hold. */
TEST_CASE(cancel_drops_the_owed_update_in_every_phase_and_keeps_the_image)
{
    devimg_slot_t s;
    /* the card read and its checks */
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_CARD));
    CHECK(devimg_cancel(&s));
    CHECK(devimg_busy(&s));
    devimg_ready(&s, 1000u);
    CHECK_EQ(s.st, DEVIMG_READY);
    CHECK_EQ(devimg_owed_step(&s, true, false, 1000u), DEVIMG_OWED_NONE);
    CHECK_EQ(devimg_src(&s, true), DEVIMG_SRC_RAM);
    /* the download, then its checks */
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_NET));
    CHECK(devimg_cancel(&s));
    devimg_fetched(&s);
    devimg_ready(&s, 0u);
    CHECK_EQ(devimg_owed_step(&s, true, false, 0u), DEVIMG_OWED_NONE);
    /* the downloaded file's checks alone */
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_NET));
    devimg_fetched(&s);
    CHECK_EQ(s.st, DEVIMG_VERIFY);
    CHECK(devimg_cancel(&s));
    devimg_ready(&s, 0u);
    CHECK_EQ(devimg_owed_step(&s, true, false, 0u), DEVIMG_OWED_NONE);
    /* READY, one millisecond before the radio's wait ends */
    devimg_init(&s);
    CHECK(devimg_begin(&s, DEVIMG_SRC_CARD));
    devimg_ready(&s, 0u);
    CHECK_EQ(devimg_owed_step(&s, false, true, DEVIMG_OWED_MS - 1u),
             DEVIMG_OWED_WAIT);
    CHECK(devimg_cancel(&s));
    CHECK_EQ(devimg_owed_step(&s, true, false, DEVIMG_OWED_MS - 1u),
             DEVIMG_OWED_NONE);
    /* nothing owed: the cancel is not this image's */
    CHECK(!devimg_cancel(&s));
    devimg_init(&s);
    CHECK(!devimg_cancel(&s));
}

int main(void)
{
    g_img = malloc(DEVUPD_SLOT_DIAL);
    REQUIRE_ALLOC(g_img);
    RUN(each_image_has_the_release_asset_name);
    RUN(the_widest_name_and_path_fit_their_bounds_and_one_byte_less_fails);
    RUN(each_image_is_held_to_its_devices_slot);
    RUN(a_c6_image_of_the_wanted_version_passes_with_its_sha);
    RUN(a_sha_other_than_the_releases_is_refused);
    RUN(another_version_than_the_panels_release_is_refused);
    RUN(a_dial_image_passes_and_a_node_role_or_chip_is_refused);
    RUN(a_damaged_image_fails_its_own_appended_sha);
    RUN(the_size_limit_is_the_slot_at_one_byte_under_at_and_over);
    RUN(a_hold_downloads_and_without_a_network_reads_the_card);
    RUN(a_failed_download_sends_the_next_hold_to_the_card_and_back);
    RUN(a_downloaded_file_that_fails_its_checks_sends_the_next_to_the_card);
    RUN(an_image_that_arrived_stays_in_memory_for_every_later_hold);
    RUN(a_code_past_99_reads_99);
    RUN(the_owed_update_starts_once_its_gate_opens);
    RUN(the_radios_wait_ends_at_DEVIMG_OWED_MS_not_a_millisecond_before);
    RUN(the_wait_holds_across_the_millisecond_wrap);
    RUN(any_other_closed_gate_drops_the_owed_update_at_once);
    RUN(a_failed_attempt_owes_no_update);
    RUN(cancel_drops_the_owed_update_in_every_phase_and_keeps_the_image);
    free(g_img);
    return test_summary("devimg");
}
