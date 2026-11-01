/*
 * The guard (protocols/escwire/ew_guard.c): the specification's section 9, one
 * case per refusal, each at and one past its limit.
 *
 * Under test: before a contact only the first contact goes; the command list;
 * a frame's length, fixed bytes and CRC; address rules; a read with no
 * address; an erase and a program in the first page, in the bootloader area,
 * outside the settings start, with no judged page; announced data of another
 * length, data other than the judged page, a program with no data; leave
 * rules and a leave of a half-written page; the judged page's rules for each
 * firmware; the restore; the text lines; the SimonK commands as commands no
 * host here sends.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ew_guard.h"
#include "ew_a.h"

static ew_guard_t g;
static uint8_t g_orig[256];
static uint8_t g_draft[256];

static ew_ident_t ident_s(void)
{
    ew_ident_t id;

    memset(&id, 0, sizeof(id));
    id.fw = EW_FW_S;
    id.layout = 33u;
    id.writable = true;
    id.len = 0x70u;
    return id;
}

static ew_ident_t ident_bj(void)
{
    ew_ident_t id = ident_s();

    id.fw = EW_FW_BJ;
    id.layout = 208u;
    id.len = 255u;
    return id;
}

static ew_ident_t ident_am(void)
{
    ew_ident_t id = ident_s();

    id.fw = EW_FW_AM;
    id.layout = 4u;
    id.len = 192u;
    return id;
}

static void efm8(void)
{
    const uint8_t a[9] = { 0x34, 0x37, 0x31, 0x64, 0xE8, 0xB2, 0x06, 0x01, 0x30 };
    ew_contact_t c;

    ew_id_contact(a, &c);
    ew_guard_target(&g, &c);
}

static void arm(void)
{
    const uint8_t a[9] = { 0x34, 0x37, 0x31, 0x14, 0x35, 0x06, 0x06, 0x03, 0x30 };
    ew_contact_t c;

    ew_id_contact(a, &c);
    ew_guard_target(&g, &c);
    memset(g_orig, 1, sizeof(g_orig));
    g_orig[1] = 4u;
    memcpy(g_draft, g_orig, sizeof(g_draft));
}

static ew_refuse_t addr(uint16_t a)
{
    uint8_t f[6];

    (void)ew_a_address(f, a);
    return ew_guard_frame(&g, f, 6u);
}

static ew_refuse_t cmd(uint8_t c, uint8_t arg)
{
    uint8_t f[4];

    (void)ew_a_short(f, c, arg);
    return ew_guard_frame(&g, f, 4u);
}

static ew_refuse_t announce(uint16_t n)
{
    uint8_t f[6];

    f[0] = EW_A_CMD_ANNOUNCE;
    f[1] = 0u;
    f[2] = (uint8_t)(n >> 8);
    f[3] = (uint8_t)(n & 0xFFu);
    const uint16_t c = ew_crc16(f, 4u);

    f[4] = (uint8_t)(c & 0xFFu);
    f[5] = (uint8_t)(c >> 8);
    return ew_guard_frame(&g, f, 6u);
}

static ew_refuse_t data(const uint8_t *p, size_t n)
{
    static uint8_t f[300];
    const size_t m = ew_a_data(f, p, n);

    return ew_guard_frame(&g, f, m);
}

TEST_CASE(before_a_contact_only_the_first_contact_goes)
{
    uint8_t f[EW_A_CONTACT_LEN];
    const size_t n = ew_a_contact(f);

    ew_guard_init(&g);
    CHECK_EQ(ew_guard_frame(&g, f, n), EW_REF_NONE);
    CHECK_EQ(ew_guard_frame(&g, f, n - 1u), EW_REF_NO_CONTACT);
    f[9] ^= 1u;
    CHECK_EQ(ew_guard_frame(&g, f, n), EW_REF_NO_CONTACT);
    CHECK_EQ(addr(0x1A00u), EW_REF_NO_CONTACT);
    CHECK_EQ(cmd(EW_A_CMD_LEAVE, 1u), EW_REF_NO_CONTACT);
    CHECK_EQ(ew_guard_frame(NULL, f, n), EW_REF_ARG);
    CHECK_EQ(ew_guard_frame(&g, NULL, n), EW_REF_ARG);
    CHECK_EQ(ew_guard_frame(&g, f, 0u), EW_REF_ARG);
}

TEST_CASE(the_list_of_commands)
{
    efm8();
    for (unsigned c = 0u; c < 256u; ++c) {
        const bool listed = c == 0xFFu || c == 0x03u || c == 0xFEu || c == 0x01u
                            || c == 0x02u || c == 0xFDu || c == 0x00u;
        uint8_t f[4] = { (uint8_t)c, 1u, 0u, 0u };
        const uint16_t crc = ew_crc16(f, 2u);

        f[2] = (uint8_t)(crc & 0xFFu);
        f[3] = (uint8_t)(crc >> 8);
        const ew_refuse_t r = ew_guard_frame(&g, f, 4u);

        if (!listed) {
            CHECK_EQ(r, EW_REF_COMMAND);
        } else {
            CHECK(r != EW_REF_COMMAND);
        }
    }
    /* The other firmware's chip erase and flash commands, and its frames. */
    {
        const uint8_t sign[7] = { 0x1B, 0x01, 0x00, 0x01, 0x0E, 0x01, 0x14 };
        const uint8_t chip[4] = { 0x12, 0x00, 0x00, 0x00 };
        const uint8_t flash[4] = { 0x13, 0x00, 0x00, 0x00 };

        CHECK_EQ(ew_guard_frame(&g, sign, sizeof(sign)), EW_REF_COMMAND);
        CHECK_EQ(ew_guard_frame(&g, chip, 4u), EW_REF_COMMAND);
        CHECK_EQ(ew_guard_frame(&g, flash, 4u), EW_REF_COMMAND);
    }
}

TEST_CASE(a_frames_length_fixed_bytes_and_crc)
{
    uint8_t f[8];

    efm8();
    (void)ew_a_address(f, 0x1A00u);
    CHECK_EQ(ew_guard_frame(&g, f, 5u), EW_REF_FORM);
    CHECK_EQ(ew_guard_frame(&g, f, 7u), EW_REF_FORM);
    f[5] ^= 1u;
    CHECK_EQ(ew_guard_frame(&g, f, 6u), EW_REF_CRC);
    /* An address whose second byte is not 0, with a CRC that fits. */
    f[0] = 0xFFu; f[1] = 1u; f[2] = 0x1Au; f[3] = 0u;
    {
        const uint16_t c = ew_crc16(f, 4u);

        f[4] = (uint8_t)(c & 0xFFu);
        f[5] = (uint8_t)(c >> 8);
    }
    CHECK_EQ(ew_guard_frame(&g, f, 6u), EW_REF_FORM);
    /* The announcement the same way. */
    f[0] = 0xFEu;
    {
        const uint16_t c = ew_crc16(f, 4u);

        f[4] = (uint8_t)(c & 0xFFu);
        f[5] = (uint8_t)(c >> 8);
    }
    CHECK_EQ(ew_guard_frame(&g, f, 6u), EW_REF_FORM);
    (void)ew_a_short(f, EW_A_CMD_READ, 0x70u);
    CHECK_EQ(ew_guard_frame(&g, f, 3u), EW_REF_FORM);
    f[3] ^= 1u;
    CHECK_EQ(ew_guard_frame(&g, f, 4u), EW_REF_CRC);
    CHECK_EQ(cmd(EW_A_CMD_KEEP, 0u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_KEEP, 1u), EW_REF_FORM);
    CHECK_EQ(addr(0x1A00u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_ERASE, 0u), EW_REF_FORM);
    CHECK_EQ(cmd(EW_A_CMD_PROGRAM, 0u), EW_REF_FORM);
}

TEST_CASE(a_read_needs_an_address_and_goes_anywhere)
{
    efm8();
    CHECK_EQ(cmd(EW_A_CMD_READ, 0x70u), EW_REF_NO_ADDRESS);
    /* The bootloader's address, the one a program may not reach. */
    CHECK_EQ(addr(0x1C00u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_READ, 0x70u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_READ, 0x70u), EW_REF_NO_ADDRESS);
    arm();
    CHECK_EQ(addr(0x0020u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_READ, 0xC0u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_READ, 0xC0u), EW_REF_NO_ADDRESS);
}

TEST_CASE(am32_addresses)
{
    arm();
    CHECK_EQ(addr(0x001Fu), EW_REF_AM_ADDRESS);
    CHECK_EQ(addr(0x0020u), EW_REF_NONE);
    CHECK_EQ(addr(0x0021u), EW_REF_NONE);
    CHECK_EQ(addr(0x0022u), EW_REF_NONE);
    CHECK_EQ(addr(0x0023u), EW_REF_NONE);
    CHECK_EQ(addr(0x0024u), EW_REF_AM_ADDRESS);
    CHECK_EQ(addr(0x03FFu), EW_REF_AM_ADDRESS);
    CHECK_EQ(addr(0x0000u), EW_REF_AM_ADDRESS);
    CHECK_EQ(addr(0x0400u), EW_REF_NONE);
}

/* A judged page for an EFM8 at address 0x1A00. */
static void judged_s(void)
{
    const ew_ident_t id = ident_s();

    efm8();
    memset(g_orig, 0xFF, sizeof(g_orig));
    memcpy(g_draft, g_orig, sizeof(g_draft));
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 0x70u, NULL),
             EW_REF_NONE);
}

TEST_CASE(an_erase_and_a_program_stay_on_the_settings_start)
{
    static const struct {
        uint16_t a;
        ew_refuse_t r;
    } k_rows[] = {
        { 0x0000u, EW_REF_FIRST_PAGE },   { 0x01FFu, EW_REF_FIRST_PAGE },
        { 0x0200u, EW_REF_NOT_SETTINGS }, { 0x19FFu, EW_REF_NOT_SETTINGS },
        { 0x1A00u, EW_REF_NONE },         { 0x1A01u, EW_REF_NOT_SETTINGS },
        { 0x1BFFu, EW_REF_NOT_SETTINGS }, { 0x1C00u, EW_REF_BOOTLOADER },
        { 0xFFFFu, EW_REF_BOOTLOADER },
    };

    for (size_t i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]); ++i) {
        judged_s();
        CHECK_EQ(addr(k_rows[i].a), EW_REF_NONE);
        CHECK_EQ(cmd(EW_A_CMD_ERASE, 1u), k_rows[i].r);
        judged_s();
        CHECK_EQ(addr(k_rows[i].a), EW_REF_NONE);
        if (k_rows[i].r != EW_REF_NONE) {
            /* A program with data sent before: the address alone decides. */
            g.data_sent = true;
            CHECK_EQ(cmd(EW_A_CMD_PROGRAM, 1u), k_rows[i].r);
        }
        judged_s();
        CHECK_EQ(addr(k_rows[i].a), EW_REF_NONE);
        CHECK_EQ(announce(0x70u), k_rows[i].r);
    }
    /* The EFM8BB5x has its own limits. */
    {
        const uint8_t a[9] = { 0x34, 0x37, 0x31, 0x64, 0xE8, 0xB5, 0x06, 0x01,
                               0x30 };
        ew_contact_t c;
        const ew_ident_t id = ident_s();

        ew_id_contact(a, &c);
        ew_guard_target(&g, &c);
        CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 0x70u, NULL),
                 EW_REF_NONE);
        CHECK_EQ(addr(0x07FFu), EW_REF_NONE);
        CHECK_EQ(cmd(EW_A_CMD_ERASE, 1u), EW_REF_FIRST_PAGE);
        CHECK_EQ(addr(0x3000u), EW_REF_NONE);
        CHECK_EQ(cmd(EW_A_CMD_ERASE, 1u), EW_REF_NONE);
        CHECK_EQ(addr(0xF000u), EW_REF_NONE);
        CHECK_EQ(cmd(EW_A_CMD_ERASE, 1u), EW_REF_BOOTLOADER);
    }
}

TEST_CASE(an_erase_program_and_data_need_their_order)
{
    const uint8_t page[0x70] = { 0 };

    efm8();
    /* No address. */
    CHECK_EQ(cmd(EW_A_CMD_ERASE, 1u), EW_REF_NO_ADDRESS);
    CHECK_EQ(cmd(EW_A_CMD_PROGRAM, 1u), EW_REF_NO_ADDRESS);
    /* No page judged. */
    CHECK_EQ(addr(0x1A00u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_ERASE, 1u), EW_REF_NOT_APPROVED);
    CHECK_EQ(announce(0x70u), EW_REF_NOT_APPROVED);
    CHECK_EQ(cmd(EW_A_CMD_PROGRAM, 1u), EW_REF_NO_DATA);

    judged_s();
    CHECK_EQ(addr(0x1A00u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_ERASE, 1u), EW_REF_NONE);
    /* An erase uses its address up. */
    CHECK_EQ(cmd(EW_A_CMD_ERASE, 1u), EW_REF_NO_ADDRESS);
    CHECK_EQ(addr(0x1A00u), EW_REF_NONE);
    CHECK_EQ(announce(0x6Fu), EW_REF_COUNT);
    CHECK_EQ(announce(0x71u), EW_REF_COUNT);
    CHECK_EQ(cmd(EW_A_CMD_PROGRAM, 1u), EW_REF_NO_DATA);
    CHECK_EQ(announce(0x70u), EW_REF_NONE);
    /* After an announcement only the judged page goes. */
    CHECK_EQ(addr(0x1A00u), EW_REF_DATA);
    CHECK_EQ(data(page, 0x70u), EW_REF_DATA);
    CHECK_EQ(data(g_draft, 0x6Fu), EW_REF_DATA);
    {
        uint8_t f[0x72];

        (void)ew_a_data(f, g_draft, 0x70u);
        f[0x71] ^= 1u;
        CHECK_EQ(ew_guard_frame(&g, f, 0x72u), EW_REF_CRC);
    }
    CHECK_EQ(data(g_draft, 0x70u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_PROGRAM, 1u), EW_REF_NONE);
    /* A program uses its data and its address up. */
    CHECK_EQ(cmd(EW_A_CMD_PROGRAM, 1u), EW_REF_NO_ADDRESS);
    CHECK_EQ(addr(0x1A00u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_PROGRAM, 1u), EW_REF_NO_DATA);
    ew_guard_done(&g);
    CHECK_EQ(announce(0x70u), EW_REF_NOT_APPROVED);
}

TEST_CASE(a_leave_of_each_firmware_and_of_a_half_written_page)
{
    efm8();
    CHECK_EQ(cmd(EW_A_CMD_LEAVE, 0u), EW_REF_LEAVE_ARG);
    CHECK_EQ(cmd(EW_A_CMD_LEAVE, 2u), EW_REF_LEAVE_ARG);
    CHECK_EQ(cmd(EW_A_CMD_LEAVE, 1u), EW_REF_NONE);
    arm();
    CHECK_EQ(cmd(EW_A_CMD_LEAVE, 1u), EW_REF_LEAVE_ARG);
    CHECK_EQ(cmd(EW_A_CMD_LEAVE, 0u), EW_REF_NONE);
    judged_s();
    CHECK_EQ(cmd(EW_A_CMD_LEAVE, 1u), EW_REF_HALF_WRITTEN);
    ew_guard_done(&g);
    CHECK_EQ(cmd(EW_A_CMD_LEAVE, 1u), EW_REF_NONE);
}

TEST_CASE(a_judged_page_on_an_efm8)
{
    ew_ident_t id = ident_s();
    uint16_t off = 0xFFFFu;

    efm8();
    memset(g_orig, 0xFF, sizeof(g_orig));
    g_orig[0x15] = 3u;
    memcpy(g_draft, g_orig, sizeof(g_draft));
    CHECK_EQ(ew_guard_approve(NULL, &id, g_orig, g_draft, 0x70u, &off), EW_REF_ARG);
    CHECK_EQ(ew_guard_approve(&g, NULL, g_orig, g_draft, 0x70u, &off), EW_REF_ARG);
    CHECK_EQ(ew_guard_approve(&g, &id, NULL, g_draft, 0x70u, &off), EW_REF_ARG);
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, NULL, 0x70u, &off), EW_REF_ARG);
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 0x6Fu, &off), EW_REF_COUNT);
    /* The layout: shown layouts, a layout not known. */
    id.layout = 32u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 0x70u, &off),
             EW_REF_NOT_WRITABLE);
    id = ident_s();
    id.writable = false;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 0x70u, &off),
             EW_REF_NOT_WRITABLE);
    id = ident_s();
    /* The name tag, the markers, unused bytes. */
    static const uint8_t k_kept[] = { 0x00, 0x02, 0x03, 0x0A, 0x0D, 0x0E, 0x29,
                                      0x2A, 0x40, 0x50, 0x60, 0x6F };
    for (size_t i = 0; i < sizeof(k_kept); ++i) {
        memcpy(g_draft, g_orig, sizeof(g_draft));
        g_draft[k_kept[i]] ^= 0x01u;
        CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 0x70u, &off),
                 EW_REF_CHANGED_BYTE);
        CHECK_EQ(off, k_kept[i]);
    }
    /* A throttle byte is shown. */
    memcpy(g_draft, g_orig, sizeof(g_draft));
    g_draft[0x19] = 40u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 0x70u, &off),
             EW_REF_CHANGED_BYTE);
    /* A value out of range, and at its limit. */
    memcpy(g_draft, g_orig, sizeof(g_draft));
    g_draft[0x15] = 6u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 0x70u, &off), EW_REF_VALUE);
    CHECK_EQ(off, 0x15u);
    g_draft[0x15] = 5u;
    CHECK(g.approved == NULL);
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 0x70u, &off), EW_REF_NONE);
    CHECK(g.approved == g_draft);
    /* Bluejay: 255 bytes, its melody kept. */
    id = ident_bj();
    memcpy(g_draft, g_orig, sizeof(g_draft));
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 255u, &off), EW_REF_NONE);
    g_draft[0x70] ^= 1u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 255u, &off),
             EW_REF_CHANGED_BYTE);
    CHECK_EQ(off, 0x70u);
}

TEST_CASE(a_judged_page_on_am32)
{
    ew_ident_t id = ident_am();
    uint16_t off = 0u;

    arm();
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, &off), EW_REF_NONE);
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 191u, &off),
             EW_REF_AM_LENGTH);
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 193u, &off),
             EW_REF_AM_LENGTH);
    g_draft[0] = 0u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, &off),
             EW_REF_AM_BYTE0);
    g_draft[0] = 0xFFu;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, &off),
             EW_REF_AM_BYTE0);
    g_draft[0] = 1u;
    g_draft[1] = 2u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, &off),
             EW_REF_AM_REVISION);
    g_draft[1] = 5u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, &off),
             EW_REF_AM_REVISION);
    /* Revision 3 is a revision, and changing it is still a change. */
    g_draft[1] = 3u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, &off),
             EW_REF_CHANGED_BYTE);
    CHECK_EQ(off, 1u);
    g_draft[1] = 4u;
    /* Byte 2 stays as read, the tune too. */
    g_draft[2] ^= 1u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, &off),
             EW_REF_CHANGED_BYTE);
    g_draft[2] ^= 1u;
    g_draft[0x30] ^= 1u;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, &off),
             EW_REF_CHANGED_BYTE);
    g_draft[0x30] ^= 1u;
    id.writable = false;
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, &off),
             EW_REF_NOT_WRITABLE);
}

TEST_CASE(am32_data_erase_and_addresses_of_a_write)
{
    const ew_ident_t id = ident_am();

    arm();
    CHECK_EQ(ew_guard_approve(&g, &id, g_orig, g_draft, 192u, NULL), EW_REF_NONE);
    CHECK_EQ(addr(0x0020u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_ERASE, 1u), EW_REF_ERASE_AM);
    CHECK_EQ(announce(191u), EW_REF_AM_LENGTH);
    CHECK_EQ(announce(193u), EW_REF_AM_LENGTH);
    /* No special address but the settings, and no plain flash address. */
    CHECK_EQ(addr(0x0021u), EW_REF_NONE);
    CHECK_EQ(announce(192u), EW_REF_NOT_SETTINGS);
    CHECK_EQ(addr(0x0023u), EW_REF_NONE);
    CHECK_EQ(announce(192u), EW_REF_NOT_SETTINGS);
    CHECK_EQ(addr(0x7C00u), EW_REF_NONE);
    CHECK_EQ(announce(192u), EW_REF_NOT_SETTINGS);
    CHECK_EQ(addr(0x0020u), EW_REF_NONE);
    CHECK_EQ(announce(192u), EW_REF_NONE);
    CHECK_EQ(data(g_draft, 192u), EW_REF_NONE);
    CHECK_EQ(cmd(EW_A_CMD_PROGRAM, 1u), EW_REF_NONE);
}

TEST_CASE(a_restore_is_am32_on_an_erased_page)
{
    ew_ident_t id = ident_am();

    arm();
    id.erased = true;
    id.writable = false;
    memcpy(id.name, "BOARD", 6u);
    memset(g_orig, 0xFF, sizeof(g_orig));
    CHECK_EQ(ew_guard_approve_restore(NULL, &id, g_orig, g_draft, 192u), EW_REF_ARG);
    CHECK_EQ(ew_guard_approve_restore(&g, NULL, g_orig, g_draft, 192u), EW_REF_ARG);
    CHECK_EQ(ew_guard_approve_restore(&g, &id, NULL, g_draft, 192u), EW_REF_ARG);
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, NULL, 192u), EW_REF_ARG);
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, g_draft, 192u), EW_REF_NONE);
    CHECK(g.approved == g_draft);
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, g_draft, 191u),
             EW_REF_AM_LENGTH);
    g_draft[0] = 0xFFu;
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, g_draft, 192u),
             EW_REF_AM_BYTE0);
    g_draft[0] = 1u;
    g_draft[1] = 5u;
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, g_draft, 192u),
             EW_REF_AM_REVISION);
    g_draft[1] = 4u;
    /* An ESC with no name: nothing says the backup is its own. */
    id.name[0] = '\0';
    g.approved = NULL;
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, g_draft, 192u),
             EW_REF_RESTORE);
    CHECK(g.approved == NULL);
    id.name[0] = 'B';
    /* A build for CAN boards takes no write at all. */
    id.can_build = true;
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, g_draft, 192u),
             EW_REF_RESTORE);
    id.can_build = false;
    /* A page that is not wholly erased. */
    g_orig[191] = 0u;
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, g_draft, 192u),
             EW_REF_RESTORE);
    g_orig[191] = 0xFFu;
    id.erased = false;
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, g_draft, 192u),
             EW_REF_RESTORE);
    id = ident_s();
    id.erased = true;
    CHECK_EQ(ew_guard_approve_restore(&g, &id, g_orig, g_draft, 192u),
             EW_REF_RESTORE);
}

static ew_refuse_t line(const char *s)
{
    return ew_guard_line((const uint8_t *)s, strlen(s));
}

TEST_CASE(the_text_lines)
{
    /* The specification's session. */
    CHECK_EQ(line("info\n"), EW_REF_NONE);
    CHECK_EQ(line("set timing 20\n"), EW_REF_NONE);
    CHECK_EQ(line("save\n"), EW_REF_NONE);
    CHECK_EQ(line("show\n"), EW_REF_NONE);
    CHECK_EQ(line("get timing\n"), EW_REF_NONE);
    CHECK_EQ(line("help\n"), EW_REF_NONE);
    /* Never sent. */
    CHECK_EQ(line("reset\n"), EW_REF_LINE);
    CHECK_EQ(line("throt 100\n"), EW_REF_LINE);
    CHECK_EQ(line("throt\n"), EW_REF_LINE);
    CHECK_EQ(line("play\n"), EW_REF_LINE);
    CHECK_EQ(line("beep\n"), EW_REF_LINE);
    CHECK_EQ(line("set music dfa#\n"), EW_REF_LINE);
    CHECK_EQ(line("setx timing 20\n"), EW_REF_LINE);
    CHECK_EQ(line("set\n"), EW_REF_LINE);
    CHECK_EQ(line("informer\n"), EW_REF_LINE);
    CHECK_EQ(ew_guard_line((const uint8_t *)"\x00\xFF\n", 3u), EW_REF_LINE);
    CHECK_EQ(ew_guard_line((const uint8_t *)"\x00\xFF", 2u), EW_REF_LINE);
    /* Control characters, the end of a line, its length. */
    CHECK_EQ(line("info"), EW_REF_LINE);
    CHECK_EQ(line("in\x08" "fo\n"), EW_REF_LINE);
    CHECK_EQ(line("info\x7F\n"), EW_REF_LINE);
    CHECK_EQ(line("\n"), EW_REF_LINE);
    CHECK_EQ(ew_guard_line(NULL, 3u), EW_REF_ARG);
    CHECK_EQ(ew_guard_line((const uint8_t *)"x", 0u), EW_REF_ARG);
    {
        static uint8_t l[EW_LINE_MAX + 2u];

        memset(l, 'x', sizeof(l));
        memcpy(l, "get ", 4u);
        l[EW_LINE_MAX] = '\n';
        CHECK_EQ(ew_guard_line(l, EW_LINE_MAX + 1u), EW_REF_NONE);
        l[EW_LINE_MAX] = 'x';
        l[EW_LINE_MAX + 1u] = '\n';
        CHECK_EQ(ew_guard_line(l, EW_LINE_MAX + 2u), EW_REF_LINE);
    }
}

int main(void)
{
    RUN(before_a_contact_only_the_first_contact_goes);
    RUN(the_list_of_commands);
    RUN(a_frames_length_fixed_bytes_and_crc);
    RUN(a_read_needs_an_address_and_goes_anywhere);
    RUN(am32_addresses);
    RUN(an_erase_and_a_program_stay_on_the_settings_start);
    RUN(an_erase_program_and_data_need_their_order);
    RUN(a_leave_of_each_firmware_and_of_a_half_written_page);
    RUN(a_judged_page_on_an_efm8);
    RUN(a_judged_page_on_am32);
    RUN(am32_data_erase_and_addresses_of_a_write);
    RUN(a_restore_is_am32_on_an_erased_page);
    RUN(the_text_lines);
    return test_summary("ew_guard");
}
