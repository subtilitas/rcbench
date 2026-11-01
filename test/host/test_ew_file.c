/*
 * The settings file (protocols/escwire/ew_file.c) and its name on the card.
 *
 * Under test: the round trip of each firmware and of a page no firmware was
 * found for; the limits of a line and of a file; a file cut at every byte and
 * damaged in every byte; a file edited with its checksum kept, and each refusal
 * of a file whose checksum fits; the edited kind, each of its refusals, and
 * every value of every field read back from its text.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_file_rig.h"

/* ---------------------------------------------------------------- written */

TEST_CASE(crc32_is_the_one_of_ieee_802_3)
{
    CHECK_EQ(ew_file_crc32("123456789", 9u), 0xCBF43926u);
    CHECK_EQ(ew_file_crc32(NULL, 0u), 0u);
}

TEST_CASE(the_text_of_an_am32)
{
    static const char k_head[] =
        "ESC settings backup, format 1\n"
        "Firmware: AM32\n"
        "Layout: 4\n"
        "Processor: ARM\n"
        "Pin code: 14\n"
        "Flash code: 35\n"
        "Bootloader protocol: 2\n"
        "Bootloader version: 2\n"
        "Name: BOARD_F421\n"
        "START MARKER = 1 [not loaded]\n"
        "REVISION = 4 [not loaded]\n"
        "BOOTLOADER = 2 [not loaded]\n";

    up_am(4u, 2u);
    g_text[g_len] = '\0';
    CHECK(strncmp(g_text, k_head, sizeof(k_head) - 1u) == 0);
    CHECK(strstr(g_text, "\nPWM FREQUENCY = 24kHz\n") != NULL);
    CHECK(strstr(g_text, "\nTEMPERATURE LIMIT = 90C\n") != NULL);
    CHECK(strstr(g_text, "\nbytes 192\n00: 01 04 02 02 15 01") != NULL);
    /* The checksum covers every line before its own. */
    {
        const char *at = strstr(g_text, "checksum ");
        char want[32];

        snprintf(want, sizeof(want), "checksum %08X\n",
                 (unsigned)ew_file_crc32(g_text, (size_t)(at - g_text)));
        CHECK_STR_EQ(at, want);
    }
}

TEST_CASE(the_text_of_an_efm8)
{
    up_s();
    g_text[g_len] = '\0';
    CHECK(strstr(g_text, "Firmware: BLHeli_S\nLayout: 33\nProcessor: EFM8\n"
                         "Signature: E8B2\n") != NULL);
    CHECK(strstr(g_text, "Pin code") == NULL);
    CHECK(strstr(g_text, "\nbytes 112\n") != NULL);
    up_bj();
    g_text[g_len] = '\0';
    CHECK(strstr(g_text, "Firmware: Bluejay\nLayout: 208\nProcessor: EFM8\n"
                         "Signature: E8B5\n") != NULL);
    CHECK(strstr(g_text, "\nbytes 255\n") != NULL);
    /* The last line of the block is short: 255 is 15 x 16 + 15. */
    CHECK(strstr(g_text, "\nF0: ") != NULL);
}

static void round_trip(void)
{
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_EQ(g_r.kind, EW_FILE_VERIFIED);
    CHECK_EQ(g_r.fw, g_f.fw);
    CHECK_EQ(g_r.layout, g_f.layout);
    CHECK_EQ(g_r.cpu, g_f.cpu);
    CHECK_EQ(g_r.len, g_f.len);
    CHECK_EQ(g_r.pin_code, g_f.pin_code);
    CHECK_EQ(g_r.flash_code, g_f.flash_code);
    CHECK_EQ(g_r.protocol, g_f.protocol);
    CHECK_EQ(g_r.signature, g_f.signature);
    CHECK_STR_EQ(g_r.name, g_f.name);
    CHECK(memcmp(g_r.page, g_f.page, EW_PAGE_MAX) == 0);
    CHECK_EQ(g_r.named, 0u);
}

TEST_CASE(a_backup_holds_the_page_as_read_whole)
{
    up_s();
    round_trip();
    CHECK(memcmp(g_r.page, s.orig, 0x70u) == 0);
    up_bj();
    round_trip();
    CHECK(memcmp(g_r.page, s.orig, 255u) == 0);
    /* Melody and tune bytes travel. */
    CHECK_EQ(g_r.page[0x70], s.orig[0x70]);
    CHECK_EQ(g_r.page[0xFB], s.orig[0xFB]);
    up_am(4u, 3u);
    round_trip();
    CHECK(memcmp(g_r.page, s.orig, 192u) == 0);
    CHECK_EQ(g_r.page[0x30], 0x30u ^ 0x5Au);
    up_am(3u, 2u);
    round_trip();
    CHECK(memcmp(g_r.page, s.orig, 192u) == 0);
    CHECK(strstr(g_text, "ZERO THROTTLE BRAKE") == NULL);
    /* A revision with no table has lines for no field and all its bytes. */
    up_am(2u, 2u);
    round_trip();
    CHECK(strstr(g_text, "POWER RAMP") == NULL);
    CHECK_EQ(g_r.layout, 2u);
}

TEST_CASE(a_stored_value_outside_a_field_is_kept_and_reads_raw)
{
    up_am(4u, 2u);
    esc.am[0x18] = 200u;
    sim_power_cycle(&esc);
    attach();
    entered_and_read();
    written();
    g_text[g_len] = '\0';
    CHECK(strstr(g_text, "\nPWM FREQUENCY = raw 200\n") != NULL);
    round_trip();
    CHECK_EQ(g_r.page[0x18], 200u);
}

TEST_CASE(a_page_no_firmware_was_found_for_goes_to_the_card)
{
    static const ew_ident_t k_id = { .why = EW_WHY_NOT_KNOWN, .len = 0x70u };
    ew_contact_t c;
    uint8_t page[EW_PAGE_MAX];

    memset(&c, 0, sizeof(c));
    c.cpu = EW_CPU_EFM8;
    c.signature = 0xE8B1u;
    memset(page, 0x42, sizeof(page));
    CHECK(ew_file_capture(&g_f, &c, &k_id, page));
    g_len = ew_file_format(&g_f, g_text, sizeof(g_text));
    CHECK(g_len > 0u);
    g_text[g_len] = '\0';
    CHECK(strstr(g_text, "Firmware: none\nLayout: 0\n") != NULL);
    round_trip();
}

TEST_CASE(an_erased_page_is_written_with_layout_0)
{
    up_am(4u, 2u);
    memset(esc.am, 0xFF, sizeof(esc.am));
    sim_power_cycle(&esc);
    attach();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK(s.id.erased);
    written();
    g_text[g_len] = '\0';
    CHECK(strstr(g_text, "Layout: 0\n") != NULL);
    CHECK(strstr(g_text, "Bootloader version: 255\n") != NULL);
    round_trip();
}

TEST_CASE(a_capture_refuses_what_does_not_fit)
{
    ew_contact_t c = { .cpu = EW_CPU_ARM };
    ew_ident_t id = { .fw = EW_FW_AM, .layout = 4u, .len = 192u };
    uint8_t page[EW_PAGE_MAX];

    memset(page, 0, sizeof(page));
    page[0] = 1u;
    page[1] = 4u;
    memset(&g_f, 0x77, sizeof(g_f));
    CHECK(!ew_file_capture(NULL, &c, &id, page));
    CHECK(!ew_file_capture(&g_f, NULL, &id, page));
    CHECK(!ew_file_capture(&g_f, &c, NULL, page));
    CHECK(!ew_file_capture(&g_f, &c, &id, NULL));
    id.len = 0u;
    CHECK(!ew_file_capture(&g_f, &c, &id, page));
    id.len = EW_PAGE_MAX + 1u;
    CHECK(!ew_file_capture(&g_f, &c, &id, page));
    /* The length of another firmware. */
    id.len = 191u;
    CHECK(!ew_file_capture(&g_f, &c, &id, page));
    id.len = 192u;
    /* A layout the page does not hold; a firmware on the other processor. */
    id.layout = 3u;
    CHECK(!ew_file_capture(&g_f, &c, &id, page));
    id.layout = 4u;
    c.cpu = EW_CPU_EFM8;
    CHECK(!ew_file_capture(&g_f, &c, &id, page));
    c.cpu = EW_CPU_NONE;
    CHECK(!ew_file_capture(&g_f, &c, &id, page));
    c.cpu = EW_CPU_ARM;
    /* ESCape32's page, by whichever processor. */
    {
        static const ew_ident_t k_e32 = { .fw = EW_FW_E32, .layout = 17u,
                                          .len = EW_MAP_LEN_E32 };

        CHECK(!ew_file_capture(&g_f, &c, &k_e32, page));
    }
    /* A name the format cannot hold. */
    memset(id.name, 'x', EW_ID_NAME_MAX);
    id.name[EW_ID_NAME_MAX] = '\0';
    id.name[3] = '\n';
    CHECK(!ew_file_capture(&g_f, &c, &id, page));
    id.name[3] = (char)0x7F;
    CHECK(!ew_file_capture(&g_f, &c, &id, page));
    id.name[3] = 0x1F;
    CHECK(!ew_file_capture(&g_f, &c, &id, page));
    /* Nothing of that reached the file. */
    CHECK_EQ(g_f.kind, 0x77u);
    /* A name at its limit passes. */
    id.name[3] = 'x';
    CHECK(ew_file_capture(&g_f, &c, &id, page));
    g_len = ew_file_format(&g_f, g_text, sizeof(g_text));
    round_trip();
    CHECK_EQ(strlen(g_r.name), (size_t)EW_ID_NAME_MAX);
}

TEST_CASE(a_buffer_too_small_writes_nothing_usable)
{
    size_t n;

    up_am(4u, 2u);
    n = g_len;
    CHECK_EQ(ew_file_format(NULL, g_text, sizeof(g_text)), 0u);
    CHECK_EQ(ew_file_format(&g_f, NULL, sizeof(g_text)), 0u);
    /* The block's end and the checksum line's end are both limits. */
    for (size_t cap = 0u; cap < n; ++cap) {
        CHECK_EQ(ew_file_format(&g_f, g_text, cap), 0u);
    }
    CHECK_EQ(ew_file_format(&g_f, g_text, n), n);
    /* Nothing is written from an edited file. */
    g_f.kind = EW_FILE_EDITED;
    CHECK_EQ(ew_file_format(&g_f, g_text, sizeof(g_text)), 0u);
}

/* Lines and bytes of the current text, with the worst of each. */
static void within_limits(unsigned *lines)
{
    size_t start = 0u;

    *lines = 0u;
    for (size_t i = 0u; i < g_len; ++i) {
        CHECK(g_text[i] == '\n' || (g_text[i] >= 0x20 && g_text[i] <= 0x7E));
        if (g_text[i] == '\n') {
            CHECK(i - start <= EW_FILE_LINE_MAX);
            start = i + 1u;
            (*lines)++;
        }
    }
    CHECK_EQ(g_text[g_len - 1u], '\n');
    CHECK(*lines <= EW_FILE_LINES_MAX);
    /* With CR LF on every line and a byte order mark it still reads. */
    CHECK(g_len + *lines + 3u <= EW_FILE_MAX);
}

TEST_CASE(every_written_file_stays_inside_the_limits)
{
    unsigned lines = 0u;

    up_s();
    within_limits(&lines);
    up_bj();
    within_limits(&lines);
    up_am(4u, 3u);
    within_limits(&lines);
    /* The longest value of every field of every layout, raw included. */
    for (unsigned fw = EW_FW_S; fw < EW_FWS; ++fw) {
        for (unsigned layout = 0u; layout <= 255u; ++layout) {
            for (size_t i = 0u;; ++i) {
                const ew_field_t *fd = ew_map_nth((uint8_t)fw, (uint8_t)layout,
                                                  i);
                char text[64];

                if (fd == NULL) {
                    break;
                }
                CHECK(fd->off < ew_map_len((uint8_t)fw));
                for (unsigned raw = 0u; raw <= 255u; ++raw) {
                    const size_t n = ew_field_text(fd, (uint8_t)raw, text,
                                                   sizeof(text));

                    CHECK(n < 32u);
                    CHECK(strlen(fd->name) + 3u + n + 13u <= EW_FILE_LINE_MAX);
                }
            }
        }
    }
    /* The longest name and the last row. */
    CHECK(strlen("Name: ") + EW_ID_NAME_MAX <= EW_FILE_LINE_MAX);
    CHECK(3u + 3u * 16u <= EW_FILE_LINE_MAX);
}

/* ---------------------------------------------------------- cut or damaged */

TEST_CASE(a_file_cut_at_any_byte_is_refused)
{
    up_am(4u, 2u);
    for (size_t n = 0u; n + 1u < g_len; ++n) {
        CHECK(parse_len(n) != EW_FILE_OK);
    }
    /* Without its last line end only: the checksum line shows its own end. */
    CHECK_EQ(parse_len(g_len - 1u), EW_FILE_OK);
    /* A cut between two lines is no shorter edited file. */
    swap_text("POWER RAMP", "POWER RAMP");
    {
        const char *at = strstr(g_text, "MIN DUTY");

        CHECK_EQ(parse_len((size_t)(at - g_text)), EW_FILE_ERR_TRUNCATED);
    }
    up_s();
    for (size_t n = 0u; n + 1u < g_len; ++n) {
        CHECK(parse_len(n) != EW_FILE_OK);
    }
}

TEST_CASE(a_cut_edited_file_is_refused)
{
    up_am(4u, 2u);
    as_edited();
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    for (size_t n = 0u; n + 7u < g_len; ++n) {
        CHECK(parse_len(n) != EW_FILE_OK);
    }
    /* Its last line needs no line end: the word shows its own end. */
    CHECK_EQ(parse_len(g_len - 1u), EW_FILE_OK);
}

TEST_CASE(a_file_damaged_in_any_byte_is_refused)
{
    up_am(4u, 2u);
    for (size_t i = 0u; i < g_len; ++i) {
        const char keep = g_text[i];

        g_text[i] = (char)(keep ^ 0x01);
        CHECK(parse_len(g_len) != EW_FILE_OK);
        g_text[i] = (char)(keep ^ 0x80);
        CHECK(parse_len(g_len) != EW_FILE_OK);
        g_text[i] = keep;
    }
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
}

TEST_CASE(a_value_edited_with_the_checksum_kept_is_refused)
{
    unsigned line = 0u;

    up_am(4u, 2u);
    swap_text("PWM FREQUENCY = 24kHz", "PWM FREQUENCY = 48kHz");
    CHECK_EQ(ew_file_parse(g_text, g_len, &g_r, &line), EW_FILE_ERR_CHECKSUM);
    CHECK(line > 40u);
    up_am(4u, 2u);
    swap_text("00: 01 04", "00: 01 03");
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_CHECKSUM);
    /* A lower-case digit of the checksum. */
    up_am(4u, 2u);
    {
        char *at = strstr(g_text, "checksum ");
        const uint32_t crc = ew_file_crc32(g_text, (size_t)(at - g_text));

        sprintf(at, "checksum %08x\n", (unsigned)(crc | 0xA0000000u));
        CHECK_EQ(parse_len(strlen(g_text)), EW_FILE_ERR_CHECKSUM);
    }
    /* Removing the checksum line alone does not make it an edited file. */
    up_am(4u, 2u);
    *strstr(g_text, "checksum ") = '\0';
    CHECK_EQ(parse_len(strlen(g_text)), EW_FILE_ERR_TRUNCATED);
}

TEST_CASE(what_stands_behind_the_checksum)
{
    up_am(4u, 2u);
    g_text[g_len] = '\0';
    strcat(g_text, "\n  \t\n");
    CHECK_EQ(parse(), EW_FILE_OK);
    up_am(4u, 2u);
    g_text[g_len] = '\0';
    strcat(g_text, "more\nchecksum 00000000\n");
    CHECK_EQ(parse(), EW_FILE_ERR_TRAILING);
    /* A blank line with the other line end. */
    up_am(4u, 2u);
    g_text[g_len] = '\0';
    strcat(g_text, "\r\n");
    CHECK_EQ(parse(), EW_FILE_OK);
    /* A checksum and a line that is neither behind it. */
    up_am(4u, 2u);
    g_text[g_len] = '\0';
    strcat(g_text, "more\n");
    CHECK_EQ(parse(), EW_FILE_ERR_TRAILING);
    /* The word for an edited file behind a checksum line. */
    up_am(4u, 2u);
    g_text[g_len] = '\0';
    strcat(g_text, "edited\n");
    CHECK_EQ(parse(), EW_FILE_ERR_TRAILING);
    /* Two checksum lines, both good for what is in front. */
    up_am(4u, 2u);
    g_text[g_len] = '\0';
    {
        char twice[64];

        memcpy(twice, strstr(g_text, "checksum "), 18u);
        twice[18] = '\0';
        strcat(g_text, twice);
    }
    CHECK_EQ(parse(), EW_FILE_ERR_TRAILING);
}

TEST_CASE(line_ends_and_the_byte_order_mark)
{
    size_t n = 0u;

    up_am(4u, 2u);
    /* CR LF on every line: the same checksum holds. */
    for (size_t i = 0u; i < g_len; ++i) {
        if (g_text[i] == '\n') {
            g_alt[n++] = '\r';
        }
        g_alt[n++] = g_text[i];
    }
    g_alt[n] = '\0';
    CHECK_EQ(ew_file_parse(g_alt, n, &g_r, NULL), EW_FILE_OK);
    CHECK(memcmp(g_r.page, g_f.page, 192u) == 0);
    /* CR LF but for the checksum line; but for one line in the middle. */
    {
        char *last = strstr(g_alt, "checksum ");

        strcpy(last + 17u, "\n");
        CHECK_EQ(ew_file_parse(g_alt, strlen(g_alt), &g_r, NULL),
                 EW_FILE_ERR_LINE_END);
        strcpy(last + 17u, "\r\n");
    }
    {
        char *mid = strstr(g_alt, "MIN DUTY");

        while (*mid != '\r') {
            mid++;
        }
        memmove(mid, mid + 1, strlen(mid));
        CHECK_EQ(ew_file_parse(g_alt, strlen(g_alt), &g_r, NULL),
                 EW_FILE_ERR_LINE_END);
    }
    /* LF but for one line: a CR there is a character the format does not have
     * when the file's line end is LF. */
    up_am(4u, 2u);
    swap_text("Layout: 4\n", "Layout: 4\r\n");
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_LINE_END);
    /* The mark is no part of the checksum. */
    up_am(4u, 2u);
    g_alt[0] = (char)0xEF;
    g_alt[1] = (char)0xBB;
    g_alt[2] = (char)0xBF;
    memcpy(g_alt + 3, g_text, g_len);
    CHECK_EQ(ew_file_parse(g_alt, g_len + 3u, &g_r, NULL), EW_FILE_OK);
    /* Two marks are not. */
    memmove(g_alt + 3, g_alt, g_len + 3u);
    CHECK_EQ(ew_file_parse(g_alt, g_len + 6u, &g_r, NULL), EW_FILE_ERR_NOT_ESC);
}

TEST_CASE(the_first_line)
{
    unsigned line = 99u;

    CHECK_EQ(ew_file_parse(NULL, 0u, &g_r, &line), EW_FILE_ERR_ARG);
    CHECK_EQ(line, 0u);
    CHECK_EQ(ew_file_parse("x", 1u, NULL, NULL), EW_FILE_ERR_ARG);
    CHECK_EQ(ew_file_parse("", 0u, &g_r, &line), EW_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 1u);
    CHECK_EQ(ew_file_parse("ESC set", 7u, &g_r, &line), EW_FILE_ERR_TRUNCATED);
    CHECK_EQ(ew_file_parse("KST servo configuration, format 2\n", 34u, &g_r,
                           &line), EW_FILE_ERR_NOT_ESC);
    CHECK_EQ(line, 1u);
#define HEAD(rest) "ESC settings backup, format " rest
    CHECK_EQ(ew_file_parse(HEAD("1"), sizeof(HEAD("1")) - 1u, &g_r, &line),
             EW_FILE_ERR_TRUNCATED);
    CHECK_EQ(ew_file_parse(HEAD("\n"), sizeof(HEAD("\n")) - 1u, &g_r, &line),
             EW_FILE_ERR_VERSION);
    CHECK_EQ(ew_file_parse(HEAD("0\n"), sizeof(HEAD("0\n")) - 1u, &g_r, &line),
             EW_FILE_ERR_VERSION);
    CHECK_EQ(ew_file_parse(HEAD("x\n"), sizeof(HEAD("x\n")) - 1u, &g_r, &line),
             EW_FILE_ERR_VERSION);
    CHECK_EQ(ew_file_parse(HEAD("1000\n"), sizeof(HEAD("1000\n")) - 1u, &g_r,
                           &line), EW_FILE_ERR_VERSION);
    CHECK_EQ(ew_file_parse(HEAD("2\n"), sizeof(HEAD("2\n")) - 1u, &g_r, &line),
             EW_FILE_ERR_NEWER);
    CHECK_EQ(line, 1u);
    /* A header and nothing with a last line. */
    CHECK_EQ(ew_file_parse(HEAD("1\n"), sizeof(HEAD("1\n")) - 1u, &g_r, &line),
             EW_FILE_ERR_TRUNCATED);
    CHECK_EQ(ew_file_parse(HEAD("1\n\n \n"), sizeof(HEAD("1\n\n \n")) - 1u,
                           &g_r, &line), EW_FILE_ERR_TRUNCATED);
}

TEST_CASE(a_file_over_the_limit_is_refused)
{
    unsigned line = 99u;

    CHECK_EQ(ew_file_parse(g_text, EW_FILE_MAX + 1u, &g_r, &line),
             EW_FILE_ERR_OVERSIZED);
    CHECK_EQ(line, 0u);
    /* At the limit: comments of a person fill it. */
    {
        size_t n = (size_t)sprintf(g_text, "ESC settings backup, format 1\n"
                                           "Firmware: AM32\nLayout: 4\n");

        while (n < EW_FILE_MAX - 7u) {
            g_text[n++] = '#';
        }
        memcpy(g_text + n, "\nedited", 7u);
        CHECK_EQ(ew_file_parse(g_text, EW_FILE_MAX, &g_r, &line), EW_FILE_OK);
    }
}

/* ------------------------------------------------- a checksum that fits */

/* Each line changed and signed again. */
static void signed_refusal(const char *from, const char *to, ew_file_err_t want)
{
    up_am(4u, 2u);
    swap_text(from, to);
    resign();
    CHECK_EQ(parse_len(g_len), want);
}

TEST_CASE(a_file_with_a_fitting_checksum_is_still_read_whole)
{
    signed_refusal("Firmware: AM32", "Firmware: VESC", EW_FILE_ERR_FIRMWARE);
    signed_refusal("Firmware: AM32", "Firm: AM32", EW_FILE_ERR_MALFORMED);
    signed_refusal("Firmware: AM32", "Firmware:AM32", EW_FILE_ERR_MALFORMED);
    signed_refusal("Firmware: AM32", "Firmware", EW_FILE_ERR_MALFORMED);
    signed_refusal("Layout: 4", "Layout: 256", EW_FILE_ERR_MALFORMED);
    signed_refusal("Layout: 4", "Layout: x", EW_FILE_ERR_MALFORMED);
    signed_refusal("Layout: 4", "Layout: ", EW_FILE_ERR_MALFORMED);
    signed_refusal("Layout: 4", "Layout", EW_FILE_ERR_MALFORMED);
    signed_refusal("Processor: ARM", "Processor: Z80", EW_FILE_ERR_MALFORMED);
    signed_refusal("Processor: ARM", "Processor: ARMS", EW_FILE_ERR_MALFORMED);
    signed_refusal("Processor: ARM", "Proc: ARM", EW_FILE_ERR_MALFORMED);
    signed_refusal("Pin code: 14", "Pin code: 1", EW_FILE_ERR_MALFORMED);
    signed_refusal("Pin code: 14", "Pin code: 1a", EW_FILE_ERR_MALFORMED);
    signed_refusal("Pin code: 14", "Pin: 14", EW_FILE_ERR_MALFORMED);
    signed_refusal("Flash code: 35", "Flash code: G5", EW_FILE_ERR_MALFORMED);
    signed_refusal("Bootloader protocol: 2", "Bootloader protocol: 256",
                   EW_FILE_ERR_MALFORMED);
    signed_refusal("Bootloader version: 2", "Bootloader version: a",
                   EW_FILE_ERR_MALFORMED);
    signed_refusal("Name: BOARD_F421", "Nom: BOARD_F421",
                   EW_FILE_ERR_MALFORMED);
    /* A line over the format's length. */
    signed_refusal("MIN DUTY = 1",
                   "MIN DUTY = 1                                                "
                   "      ", EW_FILE_ERR_MALFORMED);
    /* A character the format does not have. */
    signed_refusal("MIN DUTY = 1", "MIN DUTY = \t1", EW_FILE_ERR_MALFORMED);
    /* A checksum line among the fields. */
    signed_refusal("MIN DUTY = 1", "checksum 00000000", EW_FILE_ERR_MALFORMED);
    /* No count of bytes: the checksum line takes its place. */
    signed_refusal("bytes 192\n", "", EW_FILE_ERR_MALFORMED);
    /* Rows. */
    signed_refusal("00: 01 04", "00: 01 0G", EW_FILE_ERR_MALFORMED);
    signed_refusal("00: 01 04", "00: 01 0a", EW_FILE_ERR_MALFORMED);
    signed_refusal("00: 01 04", "00: 01  4", EW_FILE_ERR_MALFORMED);
    signed_refusal("00: 01 04", "00: 01_04", EW_FILE_ERR_MALFORMED);
    signed_refusal("00: 01 04", "01: 01 04", EW_FILE_ERR_MALFORMED);
    signed_refusal("00: 01 04", "00; 01 04", EW_FILE_ERR_MALFORMED);
    signed_refusal("00: 01 04", "00: 01 04 ", EW_FILE_ERR_MALFORMED);
    /* A row less, a row more: the checksum line or a row in the wrong place. */
    signed_refusal("\nB0: ", "\nC0: ", EW_FILE_ERR_MALFORMED);
    signed_refusal("checksum ", "C0: 00\nchecksum ", EW_FILE_ERR_CHECKSUM);
}

TEST_CASE(the_count_of_bytes)
{
    signed_refusal("bytes 192", "bytes 191", EW_FILE_ERR_LENGTH);
    signed_refusal("bytes 192", "bytes 193", EW_FILE_ERR_LENGTH);
    signed_refusal("bytes 192", "bytes 0", EW_FILE_ERR_LENGTH);
    signed_refusal("bytes 192", "bytes 257", EW_FILE_ERR_LENGTH);
    signed_refusal("bytes 192", "bytes x", EW_FILE_ERR_LENGTH);
    /* Oversize for a firmware's file: 256B of an AM32. */
    signed_refusal("bytes 192", "bytes 256", EW_FILE_ERR_LENGTH);
}

TEST_CASE(lines_that_say_another_thing_than_the_page)
{
    unsigned line = 0u;

    signed_refusal("Layout: 4", "Layout: 3", EW_FILE_ERR_MISMATCH);
    signed_refusal("Bootloader version: 2", "Bootloader version: 3",
                   EW_FILE_ERR_MISMATCH);
    /* A firmware of the other processor, and the same bytes count. */
    up_s();
    swap_text("Firmware: BLHeli_S", "Firmware: none");
    resign();
    CHECK_EQ(ew_file_parse(g_text, g_len, &g_r, &line), EW_FILE_ERR_MISMATCH);
    CHECK_EQ(line, 2u);
    up_am(4u, 2u);
    swap_text("Firmware: AM32", "Firmware: none");
    resign();
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_MISMATCH);
    /* Bytes of an ARM part's file named as the processor of an EFM8. */
    up_am(4u, 2u);
    swap_text("Processor: ARM\nPin code: 14\nFlash code: 35\n"
              "Bootloader protocol: 2\nBootloader version: 2\n"
              "Name: BOARD_F421\n", "Processor: EFM8\nSignature: E8B2\n");
    resign();
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_MISMATCH);
    up_s();
    swap_text("Processor: EFM8\nSignature: E8B2\n",
              "Processor: EFM8\nSignature: E8G2\n");
    resign();
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_MALFORMED);
}

TEST_CASE(the_name_at_its_limit)
{
    /* 29 characters read. */
    up_am(4u, 2u);
    swap_text("Name: BOARD_F421", "Name: 12345678901234567890123456789");
    resign();
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_STR_EQ(g_r.name, "12345678901234567890123456789");
    /* 30 do not. */
    up_am(4u, 2u);
    swap_text("Name: BOARD_F421", "Name: 123456789012345678901234567890");
    resign();
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_NAME_LONG);
    /* No name, spaces kept, characters the keys use. */
    up_am(4u, 2u);
    swap_text("Name: BOARD_F421", "Name: ");
    resign();
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_STR_EQ(g_r.name, "");
    up_am(4u, 2u);
    swap_text("Name: BOARD_F421", "Name:  a=b: #c ");
    resign();
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_STR_EQ(g_r.name, " a=b: #c ");
}

/* ------------------------------------------------------------ edited files */

TEST_CASE(an_edited_file_gives_its_lines)
{
    unsigned line = 99u;

    CHECK_EQ(edited_body("# a comment \xC3\xA4\n"
                         "\n"
                         "  pwm   frequency  =  30khz   # by hand \xFF\n"
                         "Stick Cal Off: \n"
                         "\tMIN DUTY = 7\t\n", &line), EW_FILE_ERR_FIELD);
    CHECK_EQ(line, 7u);
    CHECK_EQ(edited_body("# a comment \xC3\xA4\n"
                         "\n"
                         "  pwm   frequency  =  30khz   # by hand \xFF\n"
                         "Stick Cal Off = on\r\n"
                         "\tMIN DUTY = 7\t\n"
                         "CURRENT LIMIT = off\n"
                         "TEMPERATURE LIMIT = 100 c\n"
                         "VOLTAGE CUTOFF = 5.5V\n"
                         "REVISION = 4 [not loaded]\n", &line), EW_FILE_OK);
    CHECK_EQ(line, 0u);
    CHECK_EQ(g_r.kind, EW_FILE_EDITED);
    CHECK_EQ(g_r.fw, EW_FW_AM);
    CHECK_EQ(g_r.layout, 4u);
    CHECK_EQ(g_r.len, 192u);
    CHECK_EQ(g_r.named, 7u);
    CHECK_EQ(g_r.page[0x18], 30u);
    CHECK_EQ(g_r.page[0x07], 1u);
    CHECK_EQ(g_r.page[0x06], 7u);
    CHECK_EQ(g_r.page[0x2C], 0u);
    CHECK_EQ(g_r.page[0x2B], 100u);
    CHECK_EQ(g_r.page[0x08], 11u);
    /* The identity lines no load uses may come twice; those it uses may not. */
    CHECK_EQ(edited_body("Name: a\nName: b\nProcessor: ARM\nProcessor: ARM\n",
                         &line), EW_FILE_OK);
    /* The unit may go. */
    CHECK_EQ(edited_body("PWM FREQUENCY = 30\n", &line), EW_FILE_OK);
    CHECK_EQ(g_r.page[0x18], 30u);
    /* A value outside the field, by its own text. */
    CHECK_EQ(edited_body("PWM FREQUENCY = raw 200\n", &line), EW_FILE_OK);
    CHECK_EQ(g_r.page[0x18], 200u);
    /* No field at all. */
    CHECK_EQ(edited_body("", &line), EW_FILE_OK);
    CHECK_EQ(g_r.named, 0u);
}

TEST_CASE(an_edited_file_refuses_a_line)
{
    unsigned line = 0u;

    CHECK_EQ(edited_body("NOT A FIELD = 1\n", &line), EW_FILE_ERR_FIELD);
    CHECK_EQ(line, 4u);
    CHECK_EQ(edited_body("Colour: red\n", &line), EW_FILE_ERR_FIELD);
    CHECK_EQ(edited_body("MIN DUTY = 1\nmin duty = 2\n", &line),
             EW_FILE_ERR_DUPLICATE);
    CHECK_EQ(line, 5u);
    CHECK_EQ(edited_body("MIN DUTY = \n", &line), EW_FILE_ERR_VALUE);
    /* A value one past the top of a label list has no text but raw. */
    CHECK_EQ(edited_body("VARIABLE PWM = NONE\n", &line), EW_FILE_ERR_VALUE);
    CHECK_EQ(edited_body("PWM FREQUENCY = 7\n", &line), EW_FILE_ERR_VALUE);
    CHECK_EQ(edited_body("PWM FREQUENCY = 145\n", &line), EW_FILE_ERR_VALUE);
    CHECK_EQ(edited_body("PWM FREQUENCY = 24.5kHz\n", &line),
             EW_FILE_ERR_VALUE);
    CHECK_EQ(edited_body("MIN DUTY = 1]\n", &line), EW_FILE_ERR_VALUE);
    CHECK_EQ(edited_body("MIN DUTY = raw 256\n", &line), EW_FILE_ERR_VALUE);
    CHECK_EQ(edited_body("MIN DUTY = raw x\n", &line), EW_FILE_ERR_VALUE);
    CHECK_EQ(edited_body("MIN DUTY = raw\n", &line), EW_FILE_ERR_VALUE);
    CHECK_EQ(edited_body("no separator here\n", &line), EW_FILE_ERR_MALFORMED);
    CHECK_EQ(edited_body("byte 192\n", &line), EW_FILE_ERR_MALFORMED);
    CHECK_EQ(edited_body("bytes\n", &line), EW_FILE_ERR_MALFORMED);
    CHECK_EQ(edited_body("MIN DUTY \xC3\xA4 = 1\n", &line),
             EW_FILE_ERR_MALFORMED);
    CHECK_EQ(edited_body("MIN\x01 DUTY = 1\n", &line), EW_FILE_ERR_MALFORMED);
    /* The word for an edited file anywhere but last. */
    CHECK_EQ(edited_body("edited\n", &line), EW_FILE_ERR_MALFORMED);
    CHECK_EQ(edited_body("Firmware: AM32\n", &line), EW_FILE_ERR_DUPLICATE);
    CHECK_EQ(edited_body("Layout: 4\n", &line), EW_FILE_ERR_DUPLICATE);
    /* A checksum line in an edited file. */
    CHECK_EQ(edited_body("checksum 00000000\n", &line), EW_FILE_ERR_TRAILING);
}

static ew_file_err_t edited_text(const char *text)
{
    return ew_file_parse(text, strlen(text), &g_r, NULL);
}

static ew_file_err_t s33_line(const char *body)
{
    char file[128];
    const int n = snprintf(file, sizeof(file), ED_HEAD "Firmware: BLHeli_S\n"
                           "Layout: 33\n%s\nedited\n", body);

    return ew_file_parse(file, (size_t)n, &g_r, NULL);
}

TEST_CASE(a_plain_number_reads_as_the_one_label_it_starts)
{
    CHECK_EQ(s33_line("TEMPERATURE = 80"), EW_FILE_OK);
    CHECK_EQ(g_r.page[0x23], 1u);
    CHECK_EQ(s33_line("TEMPERATURE = 140"), EW_FILE_OK);
    CHECK_EQ(g_r.page[0x23], 7u);
    CHECK_EQ(s33_line("BEACON DELAY = 1"), EW_FILE_OK);
    CHECK_EQ(g_r.page[0x1D], 1u);
    CHECK_EQ(s33_line("BEACON DELAY = 10"), EW_FILE_OK);
    CHECK_EQ(g_r.page[0x1D], 4u);
    /* Part of a number is no number. */
    CHECK_EQ(s33_line("TEMPERATURE = 8"), EW_FILE_ERR_VALUE);
    CHECK_EQ(s33_line("BEACON DELAY = 3"), EW_FILE_ERR_VALUE);
    CHECK_EQ(s33_line("STARTUP POWER = 0.5"), EW_FILE_ERR_VALUE);
    /* A word is no number, and a label with no number takes none. */
    CHECK_EQ(s33_line("TIMING = MED"), EW_FILE_ERR_VALUE);
    CHECK_EQ(s33_line("TIMING = 3"), EW_FILE_ERR_VALUE);
    CHECK_EQ(s33_line("TEMPERATURE = 80x"), EW_FILE_ERR_VALUE);
    /* The exact text comes first. */
    CHECK_EQ(s33_line("STARTUP POWER = 0.50"), EW_FILE_OK);
    CHECK_EQ(g_r.page[0x09], 9u);
}

TEST_CASE(an_edited_file_names_what_it_is_for)
{
    CHECK_EQ(edited_text(ED_HEAD "edited\n"), EW_FILE_ERR_NO_TARGET);
    CHECK_EQ(edited_text(ED_HEAD "Firmware: AM32\nedited\n"),
             EW_FILE_ERR_NO_TARGET);
    CHECK_EQ(edited_text(ED_HEAD "Layout: 4\nedited\n"), EW_FILE_ERR_NO_TARGET);
    CHECK_EQ(edited_text(ED_HEAD "MIN DUTY = 1\nFirmware: AM32\nLayout: 4\n"
                                 "edited\n"), EW_FILE_ERR_NO_TARGET);
    CHECK_EQ(edited_text(ED_HEAD "Firmware: AM32\nMIN DUTY = 1\nLayout: 4\n"
                                 "edited\n"), EW_FILE_ERR_NO_TARGET);
    CHECK_EQ(edited_text(ED_HEAD "Firmware: none\nLayout: 0\nedited\n"),
             EW_FILE_ERR_FIRMWARE);
    CHECK_EQ(edited_text(ED_HEAD "Firmware: VESC\nLayout: 0\nedited\n"),
             EW_FILE_ERR_FIRMWARE);
    CHECK_EQ(edited_text(ED_HEAD "Firmware: AM32\nLayout: x\nedited\n"),
             EW_FILE_ERR_VALUE);
    CHECK_EQ(edited_text(ED_HEAD "Firmware: AM32\nLayout: 256\nedited\n"),
             EW_FILE_ERR_VALUE);
    /* ESCape32 has no file. */
    CHECK_EQ(edited_text(ED_HEAD "Firmware: ESCape32\nLayout: 17\nedited\n"),
             EW_FILE_ERR_FIRMWARE);
    /* A layout with no table has no fields to name. */
    CHECK_EQ(edited_text(ED_HEAD "Firmware: AM32\nLayout: 2\nMIN DUTY = 1\n"
                                 "edited\n"), EW_FILE_ERR_FIELD);
    /* A field of revision 4 in a file of revision 3. */
    CHECK_EQ(edited_text(ED_HEAD "Firmware: AM32\nLayout: 3\n"
                                 "ZERO THROTTLE BRAKE = 1\nedited\n"),
             EW_FILE_ERR_FIELD);
    /* In any case, either way round for the rest of the identity. */
    CHECK_EQ(edited_text(ED_HEAD "layout: 33\nfirmware: blheli_s\nName: x\n"
                                 "Processor: EFM8\nSignature: E8B2\n"
                                 "Pin code: 1\nFlash code: 2\n"
                                 "Bootloader protocol: 3\n"
                                 "Bootloader version: 4\nedited\n"),
             EW_FILE_OK);
    CHECK_EQ(g_r.fw, EW_FW_S);
    CHECK_EQ(g_r.layout, 33u);
    CHECK_EQ(g_r.len, 0x70u);
    /* Text behind the word is a blank line or nothing. */
    CHECK_EQ(edited_text(ED_HEAD "Firmware: AM32\nLayout: 4\nedited\n\n  \n"),
             EW_FILE_OK);
    CHECK_EQ(edited_text(ED_HEAD "Firmware: AM32\nLayout: 4\nEDITED"),
             EW_FILE_OK);
}

TEST_CASE(the_block_of_an_edited_file_gives_no_value)
{
    unsigned line = 0u;

    /* The whole file with one line changed. */
    up_am(4u, 2u);
    as_edited();
    swap_text("PWM FREQUENCY = 24kHz", "PWM FREQUENCY = 48kHz");
    /* And the block saying otherwise. */
    swap_text("00: 01 04", "00: 7F 7F");
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_EQ(g_r.kind, EW_FILE_EDITED);
    CHECK_EQ(g_r.page[0x18], 48u);
    CHECK_EQ(g_r.page[0], 1u);
    /* A block that lost a line, is not hexadecimal, or comes twice. */
    up_am(4u, 2u);
    as_edited();
    swap_text("\nB0: ", "\n#B0: ");
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_TRUNCATED);
    up_am(4u, 2u);
    as_edited();
    swap_text("00: 01 04", "00: 01 0x");
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_MALFORMED);
    CHECK_EQ(edited_body("bytes 192\n", &line), EW_FILE_ERR_TRUNCATED);
    CHECK_EQ(edited_body("bytes 191\n", &line), EW_FILE_ERR_LENGTH);
    CHECK_EQ(edited_body("bytes 0\n", &line), EW_FILE_ERR_LENGTH);
    CHECK_EQ(edited_body("bytes x\n", &line), EW_FILE_ERR_LENGTH);
    CHECK_EQ(edited_text(ED_HEAD "bytes 192\nFirmware: AM32\nLayout: 4\n"
                                 "edited\n"), EW_FILE_ERR_LENGTH);
    up_am(4u, 2u);
    as_edited();
    swap_text("edited\n", "bytes 192\nedited\n");
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_DUPLICATE);
}

TEST_CASE(every_value_of_every_field_reads_back_from_its_text)
{
    static const struct { const char *word; uint8_t fw; } k_fw[] = {
        { "BLHeli_S", EW_FW_S }, { "Bluejay", EW_FW_BJ }, { "AM32", EW_FW_AM },
    };

    for (size_t w = 0u; w < 3u; ++w) {
        for (unsigned layout = 0u; layout <= 255u; ++layout) {
            for (size_t i = 0u;; ++i) {
                const ew_field_t *fd = ew_map_nth(k_fw[w].fw, (uint8_t)layout,
                                                  i);
                if (fd == NULL) {
                    break;
                }
                /* Names do not hide each other. */
                for (size_t k = 0u; k < i; ++k) {
                    const ew_field_t *o = ew_map_nth(k_fw[w].fw,
                                                     (uint8_t)layout, k);
                    CHECK(strcmp(o->name, fd->name) != 0);
                }
                CHECK(strpbrk(fd->name, "=:#[") == NULL);
                for (unsigned raw = 0u; raw <= 255u; ++raw) {
                    char text[32];
                    char file[160];
                    ew_file_err_t e;

                    (void)ew_field_text(fd, (uint8_t)raw, text, sizeof(text));
                    CHECK(strpbrk(text, "#[]") == NULL);
                    int n = snprintf(file, sizeof(file),
                                     ED_HEAD "Firmware: %s\nLayout: %u\n"
                                     "%s = %s\nedited\n", k_fw[w].word, layout,
                                     fd->name, text);
                    e = ew_file_parse(file, (size_t)n, &g_r, NULL);
                    CHECK_EQ(e, EW_FILE_OK);
                    CHECK_EQ(g_r.named, 1u);
                    /* Several bytes of a percentage read as one text. */
                    {
                        char back[32];

                        (void)ew_field_text(fd, g_r.page[fd->off], back,
                                            sizeof(back));
                        CHECK_STR_EQ(back, text);
                    }
                    /* The byte itself, exactly. */
                    n = snprintf(file, sizeof(file),
                                 ED_HEAD "Firmware: %s\nLayout: %u\n"
                                 "%s = RAW  %u\nedited\n", k_fw[w].word, layout,
                                 fd->name, raw);
                    CHECK_EQ(ew_file_parse(file, (size_t)n, &g_r, NULL),
                             EW_FILE_OK);
                    CHECK_EQ(g_r.page[fd->off], raw);
                }
            }
        }
    }
}

int main(void)
{
    RUN(crc32_is_the_one_of_ieee_802_3);
    RUN(the_text_of_an_am32);
    RUN(the_text_of_an_efm8);
    RUN(a_backup_holds_the_page_as_read_whole);
    RUN(a_stored_value_outside_a_field_is_kept_and_reads_raw);
    RUN(a_page_no_firmware_was_found_for_goes_to_the_card);
    RUN(an_erased_page_is_written_with_layout_0);
    RUN(a_capture_refuses_what_does_not_fit);
    RUN(a_buffer_too_small_writes_nothing_usable);
    RUN(every_written_file_stays_inside_the_limits);
    RUN(a_file_cut_at_any_byte_is_refused);
    RUN(a_cut_edited_file_is_refused);
    RUN(a_file_damaged_in_any_byte_is_refused);
    RUN(a_value_edited_with_the_checksum_kept_is_refused);
    RUN(what_stands_behind_the_checksum);
    RUN(line_ends_and_the_byte_order_mark);
    RUN(the_first_line);
    RUN(a_file_over_the_limit_is_refused);
    RUN(a_file_with_a_fitting_checksum_is_still_read_whole);
    RUN(the_count_of_bytes);
    RUN(lines_that_say_another_thing_than_the_page);
    RUN(the_name_at_its_limit);
    RUN(an_edited_file_gives_its_lines);
    RUN(an_edited_file_refuses_a_line);
    RUN(a_plain_number_reads_as_the_one_label_it_starts);
    RUN(an_edited_file_names_what_it_is_for);
    RUN(the_block_of_an_edited_file_gives_no_value);
    RUN(every_value_of_every_field_reads_back_from_its_text);
    return test_summary("ew_file");
}
