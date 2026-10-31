/*
 * Identification (protocols/escwire/ew_id.c).
 *
 * Under test, each branch of the specification's section 8 with its
 * boundaries: the signature pairs, ARM's byte 4 at 0x01 and 0x8F and one past,
 * every listed pin code and one that is not, bootloader protocols 1 to 4; an
 * EFM8 page of every layout, a reset page, the refused variant; an ARM page
 * erased, with another marker, of revisions 2 to 5, its name cut at the first
 * zero, an erased name region, a CAN build.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ew_id.h"

static ew_contact_t contact_of(uint8_t b3, uint8_t b4, uint8_t b5, uint8_t b7)
{
    const uint8_t a[9] = { 0x34, 0x37, 0x31, b3, b4, b5, 0x06, b7, 0x30 };
    ew_contact_t c;

    ew_id_contact(a, &c);
    return c;
}

TEST_CASE(the_signature_pairs_of_an_efm8)
{
    ew_contact_t c = contact_of(0x64, 0xE8, 0xB1, 1);

    CHECK_EQ(c.cpu, EW_CPU_EFM8);
    CHECK_EQ(c.why, EW_WHY_NONE);
    CHECK_EQ(c.signature, 0xE8B1u);
    CHECK_EQ(c.base, 0x1A00u);
    CHECK_EQ(c.page, 512u);
    CHECK_EQ(c.boot, 0x1C00u);
    c = contact_of(0x64, 0xE8, 0xB2, 1);
    CHECK_EQ(c.base, 0x1A00u);
    c = contact_of(0x64, 0xE8, 0xB5, 1);
    CHECK_EQ(c.base, 0x3000u);
    CHECK_EQ(c.page, 2048u);
    CHECK_EQ(c.boot, 0xF000u);
    c = contact_of(0x64, 0xE8, 0xB3, 1);
    CHECK_EQ(c.cpu, EW_CPU_NONE);
    CHECK_EQ(c.why, EW_WHY_SIGNATURE);
}

TEST_CASE(an_arm_part_by_its_bytes)
{
    ew_contact_t c = contact_of(0x14, 0x01, 0x06, 2);

    CHECK_EQ(c.cpu, EW_CPU_ARM);
    CHECK_EQ(c.why, EW_WHY_NONE);
    CHECK_EQ(c.base, 0x0020u);
    CHECK_EQ(contact_of(0x14, 0x8F, 0x06, 3).cpu, EW_CPU_ARM);
    CHECK_EQ(contact_of(0x14, 0x00, 0x06, 3).why, EW_WHY_SIGNATURE);
    CHECK_EQ(contact_of(0x14, 0x90, 0x06, 3).why, EW_WHY_SIGNATURE);
    CHECK_EQ(contact_of(0x14, 0x35, 0x07, 3).why, EW_WHY_SIGNATURE);
    CHECK_EQ(contact_of(0x14, 0xE8, 0x06, 3).why, EW_WHY_SIGNATURE);
}

TEST_CASE(arm_protocols_and_pin_codes)
{
    static const uint8_t k_pins[] = { 0x02, 0x14, 0x06, 0x0F, 0x00, 0x12, 0x26 };

    for (size_t i = 0; i < sizeof(k_pins); ++i) {
        CHECK_EQ(contact_of(k_pins[i], 0x1F, 0x06, 3).why, EW_WHY_NONE);
    }
    CHECK_EQ(contact_of(0x03, 0x1F, 0x06, 3).why, EW_WHY_PIN_CODE);
    CHECK_EQ(contact_of(0x03, 0x1F, 0x06, 3).cpu, EW_CPU_ARM);
    CHECK_EQ(contact_of(0x14, 0x1F, 0x06, 1).why, EW_WHY_PROTOCOL);
    CHECK_EQ(contact_of(0x14, 0x1F, 0x06, 2).why, EW_WHY_NONE);
    CHECK_EQ(contact_of(0x14, 0x1F, 0x06, 3).why, EW_WHY_NONE);
    CHECK_EQ(contact_of(0x14, 0x1F, 0x06, 4).why, EW_WHY_PROTOCOL);
}

/* A page for ew_id_efm8(): bytes 0, 1, 2, the marker, the tag. */
static uint8_t g_page[256];

static void page_of(uint8_t b0, uint8_t b1, uint8_t layout, const char *tag)
{
    memset(g_page, 0xFF, sizeof(g_page));
    g_page[0] = b0;
    g_page[1] = b1;
    g_page[2] = layout;
    g_page[0x0D] = 0x55u;
    g_page[0x0E] = 0xAAu;
    if (tag != NULL) {
        memset(g_page + 0x60, 0x20, 16u);
        memcpy(g_page + 0x60, tag, strlen(tag));
    }
}

TEST_CASE(a_family_s_page)
{
    const ew_contact_t c = contact_of(0x64, 0xE8, 0xB2, 1);
    ew_ident_t id;

    page_of(16, 7, 33, "");
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.fw, EW_FW_S);
    CHECK_EQ(id.layout, 33u);
    CHECK(id.writable);
    CHECK_EQ(id.len, 0x70u);
    /* Layout 32 is shown, not written. */
    page_of(16, 7, 32, "");
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.fw, EW_FW_S);
    CHECK(!id.writable);
    CHECK_EQ(id.why, EW_WHY_OLD_LAYOUT);
    /* A wrong marker: the ESC resets at its next start. */
    page_of(16, 7, 33, "");
    g_page[0x0E] = 0x00u;
    ew_id_efm8(&c, g_page, &id);
    CHECK(!id.writable);
    CHECK_EQ(id.why, EW_WHY_MARKER);
    page_of(16, 7, 33, "");
    g_page[0x0D] = 0x00u;
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_MARKER);
    /* The refused variant. */
    page_of(16, 8, 33, "");
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.fw, EW_FW_NONE);
    CHECK_EQ(id.why, EW_WHY_VARIANT);
    CHECK_EQ(id.len, 0x70u);
    page_of(16, 9, 32, "");
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_VARIANT);
    /* Anything else. */
    page_of(15, 7, 33, "");
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_NOT_KNOWN);
    page_of(16, 7, 31, "");
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_NOT_KNOWN);
    page_of(16, 7, 34, "");
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_NOT_KNOWN);
    page_of(16, 7, 33, "OTHER");
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_NOT_KNOWN);
}

TEST_CASE(a_reset_page_is_read_only)
{
    const ew_contact_t c = contact_of(0x64, 0xE8, 0xB2, 1);
    const ew_contact_t bb1 = contact_of(0x64, 0xE8, 0xB1, 1);
    ew_ident_t id;

    page_of(16, 7, 33, NULL);
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.fw, EW_FW_S);
    CHECK(!id.writable);
    CHECK_EQ(id.why, EW_WHY_RESET_PAGE);
    page_of(0, 21, 208, NULL);
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.fw, EW_FW_BJ);
    CHECK_EQ(id.len, 255u);
    CHECK(!id.writable);
    CHECK_EQ(id.why, EW_WHY_RESET_PAGE);
    ew_id_efm8(&bb1, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_BJ_ON_BB1);
    /* Layout 32 is not what a reset writes. */
    page_of(16, 7, 32, NULL);
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_NOT_KNOWN);
    page_of(0, 21, 207, NULL);
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_NOT_KNOWN);
}

TEST_CASE(a_bluejay_page_of_every_layout)
{
    const ew_contact_t c = contact_of(0x64, 0xE8, 0xB2, 1);
    const ew_contact_t bb1 = contact_of(0x64, 0xE8, 0xB1, 1);
    ew_ident_t id;

    for (unsigned layout = 0u; layout < 256u; ++layout) {
        page_of(0, 21, (uint8_t)layout, "Bluejay (.1 RC1)");
        ew_id_efm8(&c, g_page, &id);
        const bool known = layout == 33u
                           || (layout >= 200u && layout <= 208u
                               && layout != 202u && layout != 205u);
        CHECK_EQ(id.fw, known ? EW_FW_BJ : EW_FW_NONE);
        CHECK_EQ(id.writable, layout == 208u);
        if (!known) {
            CHECK_EQ(id.why, EW_WHY_LAYOUT);
        } else if (layout != 208u) {
            CHECK_EQ(id.why, EW_WHY_OLD_LAYOUT);
        }
    }
    page_of(0, 21, 208, "Bluejay");
    g_page[0x0D] = 0u;
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_MARKER);
    page_of(0, 21, 208, "Bluejay");
    ew_id_efm8(&bb1, g_page, &id);
    CHECK_EQ(id.fw, EW_FW_NONE);
    CHECK_EQ(id.why, EW_WHY_BJ_ON_BB1);
    /* Not the whole word. */
    page_of(16, 7, 33, "Bluejax");
    ew_id_efm8(&c, g_page, &id);
    CHECK_EQ(id.why, EW_WHY_NOT_KNOWN);
}

static uint8_t g_am[192];
static uint8_t g_region[EW_ID_NAME_REGION];
static uint8_t g_record[EW_ID_RECORD_LEN];

static void arm_of(uint8_t b0, uint8_t rev)
{
    memset(g_am, 0xFF, sizeof(g_am));
    g_am[0] = b0;
    g_am[1] = rev;
    memset(g_region, 0xFF, sizeof(g_region));
    memcpy(g_region, "BOARD_F421", 11u);
    memcpy(g_record, "\xDA\xE3\x25\x59\xD9\x63\xB8\x4E", 8u);
    g_record[19] = 0x00u;
    g_record[20] = 0x10u;
}

TEST_CASE(an_arm_page_by_its_first_bytes)
{
    const ew_contact_t c = contact_of(0x14, 0x35, 0x06, 2);
    ew_ident_t id;

    arm_of(0x01, 4);
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK_EQ(id.fw, EW_FW_AM);
    CHECK_EQ(id.layout, 4u);
    CHECK(id.writable);
    CHECK_EQ(id.len, 192u);
    CHECK_STR_EQ(id.name, "BOARD_F421");
    arm_of(0x01, 3);
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK(id.writable);
    arm_of(0x01, 2);
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK_EQ(id.fw, EW_FW_AM);
    CHECK(!id.writable);
    CHECK_EQ(id.why, EW_WHY_OLD_REVISION);
    arm_of(0x01, 5);
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK(!id.writable);
    CHECK_EQ(id.why, EW_WHY_NEW_REVISION);
    arm_of(0xFF, 0xFF);
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK_EQ(id.fw, EW_FW_AM);
    CHECK(id.erased);
    CHECK(!id.writable);
    CHECK_EQ(id.why, EW_WHY_ERASED);
    arm_of(0x02, 4);
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK_EQ(id.fw, EW_FW_NONE);
    CHECK_EQ(id.why, EW_WHY_START_MARKER);
    arm_of(0x00, 4);
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK_EQ(id.why, EW_WHY_START_MARKER);
}

TEST_CASE(the_name_is_cut_at_its_first_zero)
{
    const ew_contact_t c = contact_of(0x14, 0x35, 0x06, 2);
    ew_ident_t id;

    arm_of(0x01, 4);
    /* An erased region is no name. */
    memset(g_region, 0xFF, sizeof(g_region));
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK_STR_EQ(id.name, "");
    /* 29 characters and the zero. */
    memset(g_region, 'N', sizeof(g_region));
    g_region[29] = 0u;
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK_EQ(strlen(id.name), 29u);
    /* A region with no zero at all stops at 29. */
    memset(g_region, 'N', sizeof(g_region));
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK_EQ(strlen(id.name), EW_ID_NAME_MAX);
    /* A zero first. */
    g_region[0] = 0u;
    ew_id_arm(&c, g_am, g_region, NULL, &id);
    CHECK_STR_EQ(id.name, "");
}

TEST_CASE(a_can_build_is_read_only)
{
    const ew_contact_t c3 = contact_of(0x14, 0x35, 0x06, 3);
    const ew_contact_t c2 = contact_of(0x14, 0x35, 0x06, 2);
    ew_ident_t id;

    arm_of(0x01, 4);
    ew_id_arm(&c3, g_am, g_region, g_record, &id);
    CHECK(id.writable);
    g_record[19] = 0x00u;
    g_record[20] = 0x40u;
    ew_id_arm(&c3, g_am, g_region, g_record, &id);
    CHECK(!id.writable);
    CHECK_EQ(id.why, EW_WHY_CAN_BUILD);
    /* The low byte and the record's start both count. */
    g_record[19] = 0x01u;
    ew_id_arm(&c3, g_am, g_region, g_record, &id);
    CHECK(id.writable);
    g_record[19] = 0x00u;
    g_record[0] = 0x00u;
    ew_id_arm(&c3, g_am, g_region, g_record, &id);
    CHECK(id.writable);
    /* A protocol with no record. */
    g_record[0] = 0xDAu;
    ew_id_arm(&c2, g_am, g_region, g_record, &id);
    CHECK(id.writable);
}

int main(void)
{
    RUN(the_signature_pairs_of_an_efm8);
    RUN(an_arm_part_by_its_bytes);
    RUN(arm_protocols_and_pin_codes);
    RUN(a_family_s_page);
    RUN(a_reset_page_is_read_only);
    RUN(a_bluejay_page_of_every_layout);
    RUN(an_arm_page_by_its_first_bytes);
    RUN(the_name_is_cut_at_its_first_zero);
    RUN(a_can_build_is_read_only);
    return test_summary("ew_id");
}
