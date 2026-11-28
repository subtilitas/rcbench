/*
 * The display node's image beside the C6 power node's (shared/devupd/
 * devimg.c): the release asset rcbench-node-display-<version>.bin and its
 * card path, the longest name at its bound, the image the power node's row
 * picks for the board its INFO names (C6-Zero, ES3C28P, none yet, a board
 * no image is for), the last check before sending (the board takes the
 * image, the head names its chip and kind: a C6 image for the display node,
 * a display image for the C6 node, a Dial or balance image for either, an
 * image number or board out of range), the checks a downloaded display
 * image passes (chip, role, version: older and newer refused as for the C6
 * node's), the signature path of its card file, a release whose
 * SHA256SUMS has no line for the file, and every asset name of a release at
 * the widest version inside SHA256SUMS' bound.
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
#include "fw_release.h"
#include "rcnode.h"

static uint8_t *g_img;

static const fw_version_t k_rel = { 0u, 23u, 18u };

/* An application for @p chip and @p project at @p version with the role
 * @p kind in its head; the checksum and the appended SHA-256 written again
 * over it. */
static uint32_t make(uint16_t chip, const char *project, const char *version,
                     uint8_t kind)
{
    fw_fix_t f = fw_fix_default();
    f.chip = chip;
    f.project = project;
    f.version = version;
    f.body = 4000u;
    const uint32_t len = fw_fix_make(&f, g_img);
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

/* The display node's application at @p version. */
static uint32_t display(const char *version)
{
    return make(DEVUPD_CHIP_ESP32S3, DEVUPD_PROJECT_NODE, version,
                BLEOTA_KIND_NODE_POWER_S3);
}

/* The C6 power node's application at @p version. */
static uint32_t c6_power(const char *version)
{
    return make(FW_CHIP_ESP32C6, DEVUPD_PROJECT_NODE, version,
                BLEOTA_KIND_NODE_POWER);
}

/* ---------------------------------------------------------- the names */

TEST_CASE(the_display_image_has_its_release_asset_name_and_card_path)
{
    char n[DEVIMG_NAME_MAX];
    CHECK(devimg_name(DEVIMG_PNODE_S3, k_rel, n, sizeof(n)));
    CHECK_STR_EQ(n, "rcbench-node-display-0.23.18.bin");
    char p[DEVIMG_PATH_MAX];
    CHECK(devimg_card_path(DEVIMG_PNODE_S3, k_rel, p, sizeof(p)));
    CHECK_STR_EQ(p, "BENCH/UPDATES/rcbench-node-display-0.23.18.bin");
    CHECK_EQ(devimg_slot(DEVIMG_PNODE_S3), DEVUPD_SLOT_NODE);
    /* The C6 power node keeps its own name. */
    CHECK(devimg_name(DEVIMG_PNODE, k_rel, n, sizeof(n)));
    CHECK_STR_EQ(n, "rcbench-node-pwr-0.23.18.bin");
}

TEST_CASE(the_longest_display_name_fits_DEVIMG_NAME_MAX_and_a_byte_less_fails)
{
    const fw_version_t wide = { 65535u, 65535u, 65535u };
    char n[DEVIMG_NAME_MAX];
    CHECK(devimg_name(DEVIMG_PNODE_S3, wide, n, sizeof(n)));
    CHECK_STR_EQ(n, "rcbench-node-display-65535.65535.65535.bin");
    CHECK_EQ(strlen(n), 42u);
    CHECK(devimg_name(DEVIMG_PNODE_S3, wide, n, 43u));
    CHECK(!devimg_name(DEVIMG_PNODE_S3, wide, n, 42u));
    char p[DEVIMG_PATH_MAX];
    CHECK(devimg_card_path(DEVIMG_PNODE_S3, wide, p, sizeof(p)));
    CHECK(!devimg_card_path(DEVIMG_PNODE_S3, wide, p, strlen(p)));
}

/* Every asset a release publishes, at the widest version: SHA256SUMS
 * holds a line for each within FW_REL_SUMS_MAX, and the display node's
 * application is found there. */
TEST_CASE(a_widest_releases_sums_hold_every_asset_and_find_the_display_image)
{
    static const char *const k_stem[] = {
        "rcbench-panel-p4-v%s.zip", "rcbench-panel-p4-v%s.bin",
        "rcbench-iomcu-v%s.zip", "rcbench-iomcu-rbmod-v%s.zip",
        "rcbench-c6-%s.bin", "rcbench-c6-merged-%s.bin",
        "rcbench-dial-%s.bin", "rcbench-dial-merged-%s.bin",
        "rcbench-node-pwr-%s.bin", "rcbench-node-pwr-merged-%s.bin",
        "rcbench-node-bal-%s.bin", "rcbench-node-bal-merged-%s.bin",
        "rcbench-node-display-%s.bin", "rcbench-node-display-merged-%s.bin",
    };
    static char text[FW_REL_SUMS_MAX + 1u];
    size_t at = 0u;
    for (unsigned i = 0; i < sizeof(k_stem) / sizeof(k_stem[0]); ++i) {
        char name[64];
        snprintf(name, sizeof(name), k_stem[i], "65535.65535.65535");
        at += (size_t)snprintf(text + at, sizeof(text) - at,
                               "%064x  ./%s\n", i, name);
        CHECK(at < sizeof(text));
    }
    CHECK(at <= FW_REL_SUMS_MAX);
    char want[DEVIMG_NAME_MAX];
    CHECK(devimg_name(DEVIMG_PNODE_S3,
                      (fw_version_t){ 65535u, 65535u, 65535u }, want,
                      sizeof(want)));
    uint8_t sha[FW_SHA256_BYTES];
    CHECK_EQ(fw_release_parse_sums(text, at, want, sha), FW_REL_OK);
    CHECK_EQ(sha[31], 12u);               /* its own line, the 13th */
}

/* A release from before the display node's application was published:
 * SHA256SUMS has no line for it, and the download ends there with the
 * existing text (UPD_REL_SUMS_NONE). */
TEST_CASE(a_release_without_the_display_image_has_no_sums_line_for_it)
{
    const char text[] =
        "0000000000000000000000000000000000000000000000000000000000000001"
        "  ./rcbench-node-pwr-0.23.17.bin\n"
        "0000000000000000000000000000000000000000000000000000000000000002"
        "  ./rcbench-node-display-merged-0.23.17.bin\n";
    char name[DEVIMG_NAME_MAX];
    CHECK(devimg_name(DEVIMG_PNODE_S3, (fw_version_t){ 0u, 23u, 17u }, name,
                      sizeof(name)));
    uint8_t sha[FW_SHA256_BYTES];
    CHECK_EQ(fw_release_parse_sums(text, sizeof(text) - 1u, name, sha),
             FW_REL_SUMS_NONE);
    CHECK(devimg_name(DEVIMG_PNODE, (fw_version_t){ 0u, 23u, 17u }, name,
                      sizeof(name)));
    CHECK_EQ(fw_release_parse_sums(text, sizeof(text) - 1u, name, sha),
             FW_REL_OK);
}

/* ------------------------------------------------------ the selection */

TEST_CASE(the_power_node_row_picks_the_image_of_its_boards_info)
{
    CHECK_EQ(devimg_pnode_image(RCNODE_BOARD_C6_ZERO), DEVIMG_PNODE);
    CHECK_EQ(devimg_pnode_image(RCNODE_BOARD_ES3C28P), DEVIMG_PNODE_S3);
    /* No INFO yet: the C6-Zero's, as before INFO named a board. */
    CHECK_EQ(devimg_pnode_image(0u), DEVIMG_PNODE);
    /* A board no image is for: none, the row reads NO FIRMWARE. */
    CHECK_EQ(devimg_pnode_image(RCNODE_BOARD_ES3C28P + 1u), DEVIMG_COUNT);
    CHECK_EQ(devimg_pnode_image(0xFFu), DEVIMG_COUNT);
}

TEST_CASE(each_image_names_the_kind_its_device_takes)
{
    CHECK_EQ(devimg_kind(DEVIMG_C6), 0u);
    CHECK_EQ(devimg_kind(DEVIMG_DIAL), BLEOTA_KIND_DIAL);
    CHECK_EQ(devimg_kind(DEVIMG_PNODE), BLEOTA_KIND_NODE_POWER);
    CHECK_EQ(devimg_kind(DEVIMG_BAL), BLEOTA_KIND_NODE_BAL);
    CHECK_EQ(devimg_kind(DEVIMG_PNODE_S3), BLEOTA_KIND_NODE_POWER_S3);
    CHECK_EQ(devimg_kind(DEVIMG_DIAL_K518), BLEOTA_KIND_DIAL_K518);
    CHECK_EQ(devimg_kind(DEVIMG_COUNT), 0u);
}

/* ---------------------------------------------- the check before sending */

TEST_CASE(each_board_takes_its_own_image_before_sending)
{
    uint32_t len = display("0.23.18");
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_ES3C28P, g_img, len),
             DEVIMG_OK);
    /* The display image for a C6 node: the board takes another image. */
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_C6_ZERO, g_img, len),
             DEVIMG_E_ID);
    len = c6_power("0.23.18");
    CHECK_EQ(devimg_fits(DEVIMG_PNODE, RCNODE_BOARD_C6_ZERO, g_img, len),
             DEVIMG_OK);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE, RCNODE_BOARD_ES3C28P, g_img, len),
             DEVIMG_E_ID);
}

TEST_CASE(a_head_of_another_chip_or_kind_is_refused_before_sending)
{
    /* The C6 image in the display node's place: wrong chip and kind. */
    uint32_t len = c6_power("0.23.18");
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_ES3C28P, g_img, len),
             DEVIMG_E_ROLE);
    /* The ESP32-S3 with the C6 node's kind: wrong kind only. */
    len = make(DEVUPD_CHIP_ESP32S3, DEVUPD_PROJECT_NODE, "0.23.18",
               BLEOTA_KIND_NODE_POWER);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_ES3C28P, g_img, len),
             DEVIMG_E_ROLE);
    /* The C6 with the display node's kind: wrong chip only. */
    len = make(FW_CHIP_ESP32C6, DEVUPD_PROJECT_NODE, "0.23.18",
               BLEOTA_KIND_NODE_POWER_S3);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_ES3C28P, g_img, len),
             DEVIMG_E_ROLE);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE, RCNODE_BOARD_C6_ZERO, g_img, len),
             DEVIMG_E_ROLE);
    /* The Dial's and the balance node's images. */
    len = make(DEVUPD_CHIP_ESP32S3, DEVUPD_PROJECT_DIAL, "0.23.18",
               BLEOTA_KIND_DIAL);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_ES3C28P, g_img, len),
             DEVIMG_E_ROLE);
    len = make(FW_CHIP_ESP32C6, DEVUPD_PROJECT_NODE, "0.23.18",
               BLEOTA_KIND_NODE_BAL);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE, RCNODE_BOARD_C6_ZERO, g_img, len),
             DEVIMG_E_ROLE);
}

TEST_CASE(no_board_no_image_or_a_short_head_is_refused_before_sending)
{
    const uint32_t len = display("0.23.18");
    /* No INFO on the link that reported the version. */
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, 0u, g_img, len), DEVIMG_E_ID);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE, 0u, g_img, len), DEVIMG_E_ID);
    /* A board no image is for, an image number out of range, the other
     * rows' images. */
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, 0xFFu, g_img, len), DEVIMG_E_ID);
    CHECK_EQ(devimg_fits(DEVIMG_COUNT, RCNODE_BOARD_ES3C28P, g_img, len),
             DEVIMG_E_ID);
    CHECK_EQ(devimg_fits(DEVIMG_DIAL, RCNODE_BOARD_ES3C28P, g_img, len),
             DEVIMG_E_ID);
    CHECK_EQ(devimg_fits(DEVIMG_BAL, RCNODE_BOARD_C6_ZERO, g_img, len),
             DEVIMG_E_ID);
    /* No bytes, and a head one byte short of the role. */
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_ES3C28P, NULL, len),
             DEVIMG_E_ROLE);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_ES3C28P, g_img, 0u),
             DEVIMG_E_ROLE);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_ES3C28P, g_img,
                         BLEOTA_HEAD_LEN - 1u), DEVIMG_E_ROLE);
    CHECK_EQ(devimg_fits(DEVIMG_PNODE_S3, RCNODE_BOARD_ES3C28P, g_img,
                         BLEOTA_HEAD_LEN), DEVIMG_OK);
}

/* ------------------------------------------------- the download's checks */

TEST_CASE(a_display_image_passes_its_checks_and_the_c6_ones_do_not)
{
    uint32_t len = display("0.23.18");
    uint8_t want[FW_SHA256_BYTES], sha[FW_SHA256_BYTES];
    fw_sha256(g_img, len, want);
    fw_image_info_t info;
    CHECK_EQ(devimg_verify(DEVIMG_PNODE_S3, g_img, len, k_rel, want, &info,
                           sha, NULL), DEVIMG_OK);
    CHECK_EQ(memcmp(sha, want, sizeof(sha)), 0);
    CHECK_EQ(info.version.patch, 18u);
    /* The display image offered as the C6 node's: another chip. */
    fw_err_t why = FW_OK;
    CHECK_EQ(devimg_verify(DEVIMG_PNODE, g_img, len, k_rel, NULL, NULL, NULL,
                           &why), DEVIMG_E_CHECK);
    CHECK(why != FW_OK);
    /* The C6 node's image offered as the display node's: another chip. */
    len = c6_power("0.23.18");
    CHECK_EQ(devimg_verify(DEVIMG_PNODE_S3, g_img, len, k_rel, NULL, NULL,
                           NULL, NULL), DEVIMG_E_CHECK);
    /* The ESP32-S3 with the C6 node's kind: the role in the head. */
    len = make(DEVUPD_CHIP_ESP32S3, DEVUPD_PROJECT_NODE, "0.23.18",
               BLEOTA_KIND_NODE_POWER);
    CHECK_EQ(devimg_verify(DEVIMG_PNODE_S3, g_img, len, k_rel, NULL, NULL,
                           NULL, NULL), DEVIMG_E_ROLE);
    /* The Dial's image: same chip, another project. */
    len = make(DEVUPD_CHIP_ESP32S3, DEVUPD_PROJECT_DIAL, "0.23.18",
               BLEOTA_KIND_DIAL);
    CHECK_EQ(devimg_verify(DEVIMG_PNODE_S3, g_img, len, k_rel, NULL, NULL,
                           NULL, NULL), DEVIMG_E_CHECK);
}

/* The release's own version only, as for the C6 node: an older or a newer
 * file is refused; the device decides on a reinstall of the version it
 * runs. */
TEST_CASE(a_display_image_of_another_version_is_refused_as_the_c6_nodes)
{
    static const char *const k_other[] = { "0.23.17", "0.23.19", "0.22.18",
                                           "1.23.18" };
    for (unsigned i = 0; i < sizeof(k_other) / sizeof(k_other[0]); ++i) {
        uint32_t len = display(k_other[i]);
        CHECK_EQ(devimg_verify(DEVIMG_PNODE_S3, g_img, len, k_rel, NULL,
                               NULL, NULL, NULL), DEVIMG_E_VERSION);
        len = c6_power(k_other[i]);
        CHECK_EQ(devimg_verify(DEVIMG_PNODE, g_img, len, k_rel, NULL, NULL,
                               NULL, NULL), DEVIMG_E_VERSION);
    }
}

TEST_CASE(the_display_image_is_held_to_the_node_slot_at_one_byte_over)
{
    const uint32_t len = display("0.23.18");
    CHECK_EQ(devimg_verify(DEVIMG_PNODE_S3, g_img, DEVUPD_SLOT_NODE + 1u,
                           k_rel, NULL, NULL, NULL, NULL), DEVIMG_E_SIZE);
    CHECK_EQ(devimg_verify(DEVIMG_PNODE_S3, g_img, 0u, k_rel, NULL, NULL,
                           NULL, NULL), DEVIMG_E_SIZE);
    CHECK_EQ(devimg_verify(DEVIMG_PNODE_S3, g_img, len, k_rel, NULL, NULL,
                           NULL, NULL), DEVIMG_OK);
}

/* ------------------------------------------------------- the signature */

TEST_CASE(the_display_files_card_sig_takes_the_path_of_every_image)
{
    fw_sig_t v = FW_SIG_OK;
    CHECK(!devimg_card_sig(false, NULL, 0u, &v));
    CHECK_EQ(v, FW_SIG_NONE);
    uint8_t sig[FW_SIG_MAX];
    memset(sig, 0x30, sizeof(sig));
    CHECK(!devimg_card_sig(true, sig, FW_SIG_MIN - 1u, &v));
    CHECK_EQ(v, FW_SIG_FORM);
    CHECK(devimg_card_sig(true, sig, FW_SIG_MIN, &v));
    CHECK(devimg_card_sig(true, sig, FW_SIG_MAX, &v));
    CHECK(!devimg_card_sig(true, sig, FW_SIG_MAX + 1u, &v));
    /* Warn while not enforced, refuse while enforced. */
    CHECK_EQ(devimg_sig_policy(FW_SIG_NONE, false), DEVIMG_OK);
    CHECK_EQ(devimg_sig_policy(FW_SIG_NONE, true), DEVIMG_E_SIG);
    CHECK_EQ(devimg_sig_policy(FW_SIG_OK, true), DEVIMG_OK);
    /* The display file's signature name fits beside its longest name. */
    char n[DEVIMG_NAME_MAX];
    CHECK(devimg_name(DEVIMG_PNODE_S3,
                      (fw_version_t){ 65535u, 65535u, 65535u }, n,
                      sizeof(n)));
    char s[DEVIMG_NAME_MAX + 4u];
    CHECK_EQ((size_t)snprintf(s, sizeof(s), "%s.sig", n), strlen(n) + 4u);
    CHECK(strlen(s) < sizeof(s));
}

/* ------------------------------------------------- the two Dial boards */

TEST_CASE(the_k518_image_has_its_release_asset_name_card_path_and_slot)
{
    char n[DEVIMG_NAME_MAX];
    CHECK(devimg_name(DEVIMG_DIAL_K518, k_rel, n, sizeof(n)));
    CHECK_STR_EQ(n, "rcbench-dial-k518-0.23.18.bin");
    char p[DEVIMG_PATH_MAX];
    CHECK(devimg_card_path(DEVIMG_DIAL_K518, k_rel, p, sizeof(p)));
    CHECK_STR_EQ(p, "BENCH/UPDATES/rcbench-dial-k518-0.23.18.bin");
    CHECK_EQ(devimg_slot(DEVIMG_DIAL_K518), DEVUPD_SLOT_DIAL);
    /* The widest version's name stays inside DEVIMG_NAME_MAX. */
    const fw_version_t wide = { 65535u, 65535u, 65535u };
    CHECK(devimg_name(DEVIMG_DIAL_K518, wide, n, sizeof(n)));
    CHECK_EQ(strlen(n), 39u);
    /* The M5Stack Dial keeps its own name. */
    CHECK(devimg_name(DEVIMG_DIAL, k_rel, n, sizeof(n)));
    CHECK_STR_EQ(n, "rcbench-dial-0.23.18.bin");
}

TEST_CASE(each_dial_boards_image_passes_as_its_own_and_not_as_the_other)
{
    uint32_t len = make(DEVUPD_CHIP_ESP32S3, DEVUPD_PROJECT_DIAL, "0.23.18",
                        BLEOTA_KIND_DIAL_K518);
    CHECK_EQ(devimg_verify(DEVIMG_DIAL_K518, g_img, len, k_rel, NULL, NULL,
                           NULL, NULL), DEVIMG_OK);
    CHECK_EQ(devimg_verify(DEVIMG_DIAL, g_img, len, k_rel, NULL, NULL, NULL,
                           NULL), DEVIMG_E_ROLE);
    len = make(DEVUPD_CHIP_ESP32S3, DEVUPD_PROJECT_DIAL, "0.23.18",
               BLEOTA_KIND_DIAL);
    CHECK_EQ(devimg_verify(DEVIMG_DIAL, g_img, len, k_rel, NULL, NULL, NULL,
                           NULL), DEVIMG_OK);
    CHECK_EQ(devimg_verify(DEVIMG_DIAL_K518, g_img, len, k_rel, NULL, NULL,
                           NULL, NULL), DEVIMG_E_ROLE);
}

int main(void)
{
    g_img = malloc(DEVUPD_SLOT_DIAL);
    REQUIRE_ALLOC(g_img);
    RUN(the_display_image_has_its_release_asset_name_and_card_path);
    RUN(the_longest_display_name_fits_DEVIMG_NAME_MAX_and_a_byte_less_fails);
    RUN(a_widest_releases_sums_hold_every_asset_and_find_the_display_image);
    RUN(a_release_without_the_display_image_has_no_sums_line_for_it);
    RUN(the_power_node_row_picks_the_image_of_its_boards_info);
    RUN(each_image_names_the_kind_its_device_takes);
    RUN(each_board_takes_its_own_image_before_sending);
    RUN(a_head_of_another_chip_or_kind_is_refused_before_sending);
    RUN(no_board_no_image_or_a_short_head_is_refused_before_sending);
    RUN(a_display_image_passes_its_checks_and_the_c6_ones_do_not);
    RUN(a_display_image_of_another_version_is_refused_as_the_c6_nodes);
    RUN(the_display_image_is_held_to_the_node_slot_at_one_byte_over);
    RUN(the_display_files_card_sig_takes_the_path_of_every_image);
    RUN(the_k518_image_has_its_release_asset_name_card_path_and_slot);
    RUN(each_dial_boards_image_passes_as_its_own_and_not_as_the_other);
    free(g_img);
    return test_summary("devimg_board");
}
