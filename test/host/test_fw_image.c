/*
 * The checks a panel image passes before the first erase.
 *
 * The failure under test is an image that reaches the flash and should
 * not have: one cut short, one damaged on the way, one for another chip or
 * another project, one with no release number to show the operator, or
 * one larger than the slot.  Each is made in memory with exactly one field
 * wrong and held to its own reason.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fw_fixture.h"
#include "fw_image.h"
#include "fw_sha256.h"

#define SLOT 0x3E0000u   /* the panel's ota_0 and ota_1 */

static uint8_t *g_img;
static uint32_t g_len;

static fw_err_t check_fix(const fw_fix_t *f)
{
    g_len = fw_fix_make(f, g_img);
    fw_image_info_t info;
    return fw_image_check(g_img, g_len, SLOT, &info);
}

/* -------------------------------------------------------------- SHA-256 */

static void hex(const uint8_t *d, char *out)
{
    for (int i = 0; i < 32; ++i) {
        static const char k[] = "0123456789abcdef";
        out[2 * i] = k[d[i] >> 4];
        out[2 * i + 1] = k[d[i] & 15];
    }
    out[64] = '\0';
}

TEST_CASE(sha256_gives_the_published_digests)
{
    /* FIPS 180-2 appendix B, and the empty message. */
    uint8_t d[32];
    char h[65];
    fw_sha256("", 0u, d);
    hex(d, h);
    CHECK_STR_EQ(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    fw_sha256("abc", 3u, d);
    hex(d, h);
    CHECK_STR_EQ(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    static const char two[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    fw_sha256(two, strlen(two), d);
    hex(d, h);
    CHECK_STR_EQ(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    /* A million "a", fed in uneven pieces across block edges. */
    fw_sha256_t s;
    fw_sha256_init(&s);
    static char a[1001];
    memset(a, 'a', sizeof(a));
    for (int i = 0; i < 1000; ++i) {
        fw_sha256_add(&s, a, (i % 2) ? 999u : 1001u);
    }
    fw_sha256_done(&s, d);
    hex(d, h);
    CHECK_STR_EQ(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

/* --------------------------------------------------------- whole images */

TEST_CASE(a_whole_image_for_this_panel_passes_and_says_what_it_is)
{
    fw_fix_t f = fw_fix_default();
    g_len = fw_fix_make(&f, g_img);
    fw_image_info_t info;
    CHECK_EQ(fw_image_check(g_img, g_len, SLOT, &info), FW_OK);
    CHECK_EQ(info.bytes, g_len);
    CHECK_EQ(info.version.major, 0);
    CHECK_EQ(info.version.minor, 19);
    CHECK_EQ(info.version.patch, 0);
    CHECK_STR_EQ(info.version_text, "0.19.0");
    CHECK_EQ(info.elf_sha[0], 0x5A);
    CHECK_EQ(info.elf_sha[31], 0x5A);
    /* One segment and the most there are pass alike. */
    f.segments = 1u;
    CHECK_EQ(check_fix(&f), FW_OK);
    f.segments = FW_MAX_SEGMENTS;
    CHECK_EQ(check_fix(&f), FW_OK);
}

TEST_CASE(an_empty_file_is_refused_as_empty)
{
    fw_image_info_t info;
    CHECK_EQ(fw_image_check(g_img, 0u, SLOT, &info), FW_ERR_EMPTY);
    CHECK_EQ(fw_image_check(NULL, 0u, SLOT, &info), FW_ERR_EMPTY);
}

TEST_CASE(the_slot_is_the_limit_to_the_byte)
{
    fw_fix_t f = fw_fix_default();
    g_len = fw_fix_make(&f, g_img);
    fw_image_info_t info;
    /* An image exactly the slot's size, and one byte over it. */
    CHECK_EQ(fw_image_check(g_img, g_len, g_len, &info), FW_OK);
    CHECK_EQ(fw_image_check(g_img, g_len, g_len - 1u, &info), FW_ERR_TOO_BIG);
    /* The panel's slot, filled to the last 16-byte block: the body is
     * chosen so the image comes out at SLOT exactly. */
    f.segments = 1u;
    f.body = SLOT - 24u - 8u - 256u - 32u - 16u;
    CHECK_EQ(fw_fix_len(&f), SLOT);
    CHECK_EQ(check_fix(&f), FW_OK);
    f.body += 16u;
    CHECK_EQ(fw_fix_len(&f), SLOT + 16u);
    CHECK_EQ(check_fix(&f), FW_ERR_TOO_BIG);
}

TEST_CASE(another_chips_image_is_refused)
{
    fw_fix_t f = fw_fix_default();
    f.chip = 0u;                              /* ESP32 */
    CHECK_EQ(check_fix(&f), FW_ERR_CHIP);
    f.chip = 5u;                              /* ESP32-C3 */
    CHECK_EQ(check_fix(&f), FW_ERR_CHIP);
    f.chip = 0x0109u;                         /* the high byte counts */
    CHECK_EQ(check_fix(&f), FW_ERR_CHIP);
}

TEST_CASE(what_is_not_an_application_image_is_refused)
{
    fw_fix_t f = fw_fix_default();
    f.magic = 0xE8u;
    CHECK_EQ(check_fix(&f), FW_ERR_NOT_IMAGE);
    f = fw_fix_default();
    f.segments = 0u;
    CHECK_EQ(check_fix(&f), FW_ERR_NOT_IMAGE);
    f.segments = FW_MAX_SEGMENTS + 1u;
    CHECK_EQ(check_fix(&f), FW_ERR_NOT_IMAGE);
    /* A bootloader or a partition table has no descriptor where an app
     * has one. */
    f = fw_fix_default();
    f.desc_magic = 0xABCD5433uL;
    CHECK_EQ(check_fix(&f), FW_ERR_NO_DESC);
    /* A text file named .bin. */
    static const char text[] = "timestamp,rpm,current\n0,0,0.0\n";
    memset(g_img, 0, 512);
    memcpy(g_img, text, sizeof(text));
    fw_image_info_t info;
    CHECK_EQ(fw_image_check(g_img, 512u, SLOT, &info), FW_ERR_NOT_IMAGE);
}

TEST_CASE(another_projects_image_is_refused)
{
    fw_fix_t f = fw_fix_default();
    f.project = "rcbench-iomcu";
    CHECK_EQ(check_fix(&f), FW_ERR_PROJECT);
    f.project = "rcbench-panel-x";            /* longer, same start */
    CHECK_EQ(check_fix(&f), FW_ERR_PROJECT);
    f.project = "rcbench-pane";               /* shorter */
    CHECK_EQ(check_fix(&f), FW_ERR_PROJECT);
    f.project = "RCBENCH-PANEL";
    CHECK_EQ(check_fix(&f), FW_ERR_PROJECT);
    /* A name filling the field, with no terminator in it. */
    f.project = "rcbench-panelrcbench-panelrcbenc";
    CHECK_EQ(strlen(f.project), 32u);
    CHECK_EQ(check_fix(&f), FW_ERR_PROJECT);
}

TEST_CASE(an_image_without_a_release_number_is_refused)
{
    fw_fix_t f = fw_fix_default();
    /* What 0.18.0 and earlier carry: a git hash. */
    f.version = "4184795";
    CHECK_EQ(check_fix(&f), FW_ERR_VERSION);
    f.version = "0.19.0-dirty";
    CHECK_EQ(check_fix(&f), FW_ERR_VERSION);
    f.version = "v0.19.0";
    CHECK_EQ(check_fix(&f), FW_ERR_VERSION);
    f.version = "0.19";
    CHECK_EQ(check_fix(&f), FW_ERR_VERSION);
    f.version = "";
    CHECK_EQ(check_fix(&f), FW_ERR_VERSION);
    f.version = "65535.65535.65535";
    CHECK_EQ(check_fix(&f), FW_OK);
    f.version = "65536.0.0";
    CHECK_EQ(check_fix(&f), FW_ERR_VERSION);
}

TEST_CASE(versions_parse_strictly_and_compare_by_number)
{
    fw_version_t v;
    CHECK(fw_version_parse("0.19.0", 32u, &v));
    CHECK(fw_version_parse("10.0.2", 32u, &v));
    CHECK_EQ(v.major, 10);
    CHECK_EQ(v.patch, 2);
    CHECK(!fw_version_parse("1..2", 32u, &v));
    CHECK(!fw_version_parse(".1.2", 32u, &v));
    CHECK(!fw_version_parse("1.2.", 32u, &v));
    CHECK(!fw_version_parse("1.2.3.4", 32u, &v));
    CHECK(!fw_version_parse(" 1.2.3", 32u, &v));
    CHECK(!fw_version_parse(NULL, 32u, &v));
    /* The field's end terminates as a NUL would. */
    CHECK(fw_version_parse("1.2.34", 5u, &v));
    CHECK_EQ(v.patch, 3);
    fw_version_t a = { 0, 19, 0 }, b = { 0, 9, 9 }, c = { 1, 0, 0 };
    CHECK(fw_version_cmp(a, b) > 0);          /* 19 > 9, not "19" < "9" */
    CHECK(fw_version_cmp(b, a) < 0);
    CHECK(fw_version_cmp(c, a) > 0);
    CHECK_EQ(fw_version_cmp(a, a), 0);
}

TEST_CASE(an_image_built_without_its_sha256_is_refused)
{
    fw_fix_t f = fw_fix_default();
    f.hashed = 0u;
    CHECK_EQ(check_fix(&f), FW_ERR_NO_HASH);
}

TEST_CASE(one_changed_byte_anywhere_fails_the_sha256)
{
    fw_fix_t f = fw_fix_default();
    g_len = fw_fix_make(&f, g_img);
    fw_image_info_t info;
    const uint32_t at[] = { 300u, g_len / 2u, g_len - 33u, g_len - 1u };
    for (size_t i = 0; i < sizeof(at) / sizeof(at[0]); ++i) {
        g_img[at[i]] ^= 0x01u;
        CHECK_EQ(fw_image_check(g_img, g_len, SLOT, &info), FW_ERR_HASH);
        g_img[at[i]] ^= 0x01u;
    }
    CHECK_EQ(fw_image_check(g_img, g_len, SLOT, &info), FW_OK);
}

TEST_CASE(a_file_cut_short_anywhere_is_refused)
{
    fw_fix_t f = fw_fix_default();
    g_len = fw_fix_make(&f, g_img);
    fw_image_info_t info;
    /* Inside the head, inside a segment, inside the hash, one byte short. */
    const uint32_t cut[] = { 1u, 100u, FW_HEAD_BYTES - 1u, FW_HEAD_BYTES,
                             5000u, g_len - 32u, g_len - 1u };
    for (size_t i = 0; i < sizeof(cut) / sizeof(cut[0]); ++i) {
        CHECK_EQ(fw_image_check(g_img, cut[i], SLOT, &info), FW_ERR_SHORT);
    }
}

TEST_CASE(a_file_longer_than_its_image_is_refused)
{
    fw_fix_t f = fw_fix_default();
    g_len = fw_fix_make(&f, g_img);
    g_img[g_len] = 0u;
    fw_image_info_t info;
    CHECK_EQ(fw_image_check(g_img, g_len + 1u, SLOT, &info), FW_ERR_LENGTH);
    CHECK_EQ(fw_image_check(g_img, g_len + 16u, SLOT, &info), FW_ERR_LENGTH);
}

TEST_CASE(a_segment_length_past_the_end_is_a_file_cut_short)
{
    fw_fix_t f = fw_fix_default();
    g_len = fw_fix_make(&f, g_img);
    /* The last segment's length, made huge: it runs past the file. */
    uint32_t at = 24u;
    for (int s = 0; s < 2; ++s) {
        uint32_t len = (uint32_t)g_img[at + 4] | ((uint32_t)g_img[at + 5] << 8)
                       | ((uint32_t)g_img[at + 6] << 16)
                       | ((uint32_t)g_img[at + 7] << 24);
        at += 8u + len;
    }
    fw_put32(g_img + at + 4u, 0xFFFFFFF0u);
    fw_image_info_t info;
    CHECK_EQ(fw_image_check(g_img, g_len, SLOT, &info), FW_ERR_SHORT);
}

TEST_CASE(the_head_alone_reads_what_an_image_says_of_itself)
{
    fw_fix_t f = fw_fix_default();
    g_len = fw_fix_make(&f, g_img);
    fw_image_info_t info;
    CHECK_EQ(fw_image_head(g_img, FW_HEAD_BYTES, g_len, SLOT, &info), FW_OK);
    CHECK_STR_EQ(info.version_text, "0.19.0");
    /* Fewer bytes than the head of a longer file: the source failed. */
    CHECK_EQ(fw_image_head(g_img, FW_HEAD_BYTES - 1u, g_len, SLOT, &info),
             FW_ERR_READ);
    CHECK_EQ(fw_image_head(NULL, 0u, g_len, SLOT, &info), FW_ERR_READ);
}

/* ------------------------------------------------- against the panel */

TEST_CASE(newer_older_another_build_and_the_running_one)
{
    fw_fix_t f = fw_fix_default();
    g_len = fw_fix_make(&f, g_img);
    fw_image_info_t img;
    CHECK_EQ(fw_image_check(g_img, g_len, SLOT, &img), FW_OK);

    fw_image_info_t run = fw_fix_running("0.18.9", 0x5A);
    CHECK_EQ(fw_offer(&run, &img), FW_OFFER_NEWER);
    run = fw_fix_running("0.20.0", 0x5A);
    CHECK_EQ(fw_offer(&run, &img), FW_OFFER_OLDER);
    run = fw_fix_running("0.19.0", 0x11);
    CHECK_EQ(fw_offer(&run, &img), FW_OFFER_REBUILD);
    run = fw_fix_running("0.19.0", 0x5A);
    CHECK_EQ(fw_offer(&run, &img), FW_OFFER_INSTALLED);

    CHECK(fw_offer_installable(FW_OFFER_NEWER));
    CHECK(fw_offer_installable(FW_OFFER_OLDER));
    CHECK(fw_offer_installable(FW_OFFER_REBUILD));
    CHECK(!fw_offer_installable(FW_OFFER_INSTALLED));
}

TEST_CASE(every_reason_has_a_name)
{
    for (int e = 0; e < FW_ERR_COUNT; ++e) {
        CHECK(strcmp(fw_err_str((fw_err_t)e), "?") != 0);
    }
    CHECK_STR_EQ(fw_err_str(FW_ERR_COUNT), "?");
}

int main(void)
{
    g_img = malloc(SLOT + 64u);
    REQUIRE_ALLOC(g_img);
    RUN(sha256_gives_the_published_digests);
    RUN(a_whole_image_for_this_panel_passes_and_says_what_it_is);
    RUN(an_empty_file_is_refused_as_empty);
    RUN(the_slot_is_the_limit_to_the_byte);
    RUN(another_chips_image_is_refused);
    RUN(what_is_not_an_application_image_is_refused);
    RUN(another_projects_image_is_refused);
    RUN(an_image_without_a_release_number_is_refused);
    RUN(versions_parse_strictly_and_compare_by_number);
    RUN(an_image_built_without_its_sha256_is_refused);
    RUN(one_changed_byte_anywhere_fails_the_sha256);
    RUN(a_file_cut_short_anywhere_is_refused);
    RUN(a_file_longer_than_its_image_is_refused);
    RUN(a_segment_length_past_the_end_is_a_file_cut_short);
    RUN(the_head_alone_reads_what_an_image_says_of_itself);
    RUN(newer_older_another_build_and_the_running_one);
    RUN(every_reason_has_a_name);
    free(g_img);
    return test_summary("fw_image");
}
