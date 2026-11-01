/*
 * ESCape32's settings file (protocols/escwire/ew_file*.c) against the simulated
 * ESC of ew_sim_text.h.
 *
 * Under test: the text and the round trip with `music` at its limit; a file cut
 * at every byte and damaged in every byte; each refusal of a file whose checksum
 * fits; the edited kind; a load, its refusals, a write of it with `music` left
 * alone; a backup of a revision that is not written.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_file_rig.h"

#define BANNER "ESCape32 rev17.3 [BOARD_F051]"
#define ED_E32 ED_HEAD "Firmware: ESCape32\nLayout: 17\n"

static ew_file_load_t g_load;

static size_t slot_of(const char *key)
{
    size_t slot = 0u;

    CHECK(ew_key_find(key, strlen(key), &slot) != NULL);
    return slot;
}

static void read_e32(const char *banner)
{
    sim_init_e32(&esc, banner);
    attach();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

static void text_of_session(void)
{
    CHECK(ew_file_capture_text(&g_f, &s.id, s.orig, s.music));
    g_len = ew_file_format(&g_f, g_text, sizeof(g_text));
    CHECK(g_len > 0u);
    g_text[g_len] = '\0';
}

static void up(void)
{
    read_e32(BANNER);
    text_of_session();
}

static ew_file_err_t edited(const char *body)
{
    const int n = snprintf(g_text, sizeof(g_text), ED_E32 "%sedited\n", body);

    return ew_file_parse(g_text, (size_t)n, &g_r, NULL);
}

TEST_CASE(the_text)
{
    up();
    CHECK(strncmp(g_text, "ESC settings backup, format 1\n"
                          "Firmware: ESCape32\nLayout: 17\nProcessor: TEXT\n"
                          "Target: BOARD_F051\narm: 1\ndamp: 1\nrevdir: 0\n",
                  106u) == 0);
    CHECK(strstr(g_text, "\ntelem_volt: 0\n") != NULL);
    CHECK(strstr(g_text, "\nled: 0\nmusic: dfa#\nchecksum ") != NULL);
    CHECK(strstr(g_text, "bytes") == NULL);
    /* The checksum covers every line before its own. */
    {
        const char *at = strstr(g_text, "checksum ");
        char want[32];

        snprintf(want, sizeof(want), "checksum %08X\n",
                 (unsigned)ew_file_crc32(g_text, (size_t)(at - g_text)));
        CHECK_STR_EQ(at, want);
    }
}

TEST_CASE(a_round_trip_with_music_at_its_limit)
{
    size_t lines = 0u;

    sim_init_e32(&esc, BANNER);
    memset(esc.music, 'c', EW_E32_MUSIC_MAX);
    esc.music[EW_E32_MUSIC_MAX] = '\0';
    esc.saved[slot_of("telem_volt")] = -80;
    esc.ram[slot_of("telem_volt")] = -80;
    attach();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(strlen(s.music), (size_t)EW_E32_MUSIC_MAX);
    text_of_session();
    CHECK(strstr(g_text, "\ntelem_volt: -80\n") != NULL);
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_EQ(g_r.kind, EW_FILE_VERIFIED);
    CHECK_EQ(g_r.fw, EW_FW_E32);
    CHECK_EQ(g_r.cpu, EW_CPU_TEXT);
    CHECK_EQ(g_r.layout, 17u);
    CHECK_EQ(g_r.len, EW_MAP_LEN_E32);
    CHECK_STR_EQ(g_r.name, "BOARD_F051");
    CHECK_STR_EQ(g_r.music, s.music);
    CHECK(memcmp(g_r.page, s.orig, EW_MAP_LEN_E32) == 0);
    /* The file fits with CR LF on every line and a byte order mark. */
    for (size_t i = 0u; i < g_len; ++i) {
        lines += g_text[i] == '\n';
    }
    CHECK(lines <= EW_FILE_LINES_MAX);
    CHECK(g_len + lines + 3u <= EW_FILE_MAX);
    CHECK_EQ(ew_file_format(&g_f, g_text, g_len - 1u), 0u);
}

TEST_CASE(a_capture_refuses_what_does_not_fit)
{
    char music[EW_E32_MUSIC_MAX + 2u];
    ew_ident_t id;

    read_e32(BANNER);
    id = s.id;
    memset(&g_f, 0x77, sizeof(g_f));
    CHECK(!ew_file_capture_text(NULL, &id, s.orig, s.music));
    CHECK(!ew_file_capture_text(&g_f, NULL, s.orig, s.music));
    CHECK(!ew_file_capture_text(&g_f, &id, NULL, s.music));
    CHECK(!ew_file_capture_text(&g_f, &id, s.orig, NULL));
    /* One character over; a control character. */
    memset(music, 'c', EW_E32_MUSIC_MAX + 1u);
    music[EW_E32_MUSIC_MAX + 1u] = '\0';
    CHECK(!ew_file_capture_text(&g_f, &id, s.orig, music));
    CHECK(!ew_file_capture_text(&g_f, &id, s.orig, "a\nb"));
    /* Another firmware, or a target the format cannot hold. */
    id.fw = EW_FW_AM;
    CHECK(!ew_file_capture_text(&g_f, &id, s.orig, s.music));
    id = s.id;
    id.len = 91u;
    CHECK(!ew_file_capture_text(&g_f, &id, s.orig, s.music));
    id = s.id;
    id.name[0] = 0x01;
    CHECK(!ew_file_capture_text(&g_f, &id, s.orig, s.music));
    CHECK_EQ(g_f.kind, 0x77u);
    /* No music is music. */
    CHECK(ew_file_capture_text(&g_f, &s.id, s.orig, ""));
    g_len = ew_file_format(&g_f, g_text, sizeof(g_text));
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_STR_EQ(g_r.music, "");
}

TEST_CASE(a_file_cut_or_damaged_anywhere_is_refused)
{
    up();
    for (size_t n = 0u; n + 1u < g_len; ++n) {
        CHECK(parse_len(n) != EW_FILE_OK);
    }
    CHECK_EQ(parse_len(g_len - 1u), EW_FILE_OK);
    for (size_t i = 0u; i < g_len; ++i) {
        const char keep = g_text[i];

        g_text[i] = (char)(keep ^ 0x01);
        CHECK(parse_len(g_len) != EW_FILE_OK);
        g_text[i] = keep;
    }
    /* A hand edit with the checksum kept. */
    swap_text("\narm: 1\n", "\narm: 0\n");
    CHECK_EQ(parse_len(g_len), EW_FILE_ERR_CHECKSUM);
}

static void signed_refusal(const char *from, const char *to, ew_file_err_t want)
{
    up();
    swap_text(from, to);
    resign();
    CHECK_EQ(parse_len(g_len), want);
}

TEST_CASE(a_file_with_a_fitting_checksum_is_still_read_whole)
{
    signed_refusal("Processor: TEXT", "Processor: ARMS", EW_FILE_ERR_MALFORMED);
    signed_refusal("Target: BOARD_F051", "Name: BOARD_F051",
                   EW_FILE_ERR_MALFORMED);
    signed_refusal("Target: BOARD_F051",
                   "Target: 123456789012345678901234567890",
                   EW_FILE_ERR_NAME_LONG);
    signed_refusal("\narm: 1\n", "\n", EW_FILE_ERR_MALFORMED);
    signed_refusal("\narm: 1\n", "\narm: 1x\n", EW_FILE_ERR_MALFORMED);
    signed_refusal("\narm: 1\n", "\narm: 01\n", EW_FILE_ERR_MALFORMED);
    signed_refusal("\narm: 1\n", "\narm: 32768\n", EW_FILE_ERR_MALFORMED);
    signed_refusal("\ndamp: 1\nrevdir: 0\n", "\nrevdir: 0\ndamp: 1\n",
                   EW_FILE_ERR_MALFORMED);
    signed_refusal("\nmusic: dfa#\n", "\n", EW_FILE_ERR_MALFORMED);
    signed_refusal("\nmusic: dfa#\n", "\nmusic: dfa#\nled: 0\n",
                   EW_FILE_ERR_CHECKSUM);
    /* The processor of another firmware. */
    signed_refusal("Processor: TEXT\nTarget: BOARD_F051\n",
                   "Processor: EFM8\nSignature: E8B2\n", EW_FILE_ERR_MALFORMED);
    signed_refusal("Firmware: ESCape32", "Firmware: AM32",
                   EW_FILE_ERR_MISMATCH);
    signed_refusal("Firmware: ESCape32", "Firmware: none",
                   EW_FILE_ERR_MISMATCH);
    /* A music line over the limit, by one. */
    {
        static char over[EW_E32_MUSIC_MAX + 16u];

        strcpy(over, "\nmusic: ");
        memset(over + 8, 'c', EW_E32_MUSIC_MAX + 1u);
        strcpy(over + 8 + EW_E32_MUSIC_MAX + 1u, "\n");
        signed_refusal("\nmusic: dfa#\n", over, EW_FILE_ERR_MALFORMED);
    }
    /* The long line is for music: no other line takes it. */
    {
        static char over[128];

        strcpy(over, "\narm: ");
        memset(over + 6, '1', 64u);
        strcpy(over + 70, "\n");
        signed_refusal("\narm: 1\n", over, EW_FILE_ERR_MALFORMED);
    }
}

TEST_CASE(a_key_a_read_did_not_bring)
{
    read_e32(BANNER);
    ew_e32_put(s.orig, slot_of("led"), EW_KEY_NONE);
    text_of_session();
    CHECK(strstr(g_text, "\nled: none\n") != NULL);
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_EQ(ew_e32_get(g_r.page, slot_of("led")), EW_KEY_NONE);
    /* A load leaves it. */
    read_e32(BANNER);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 0u);
    CHECK_EQ(g_load.named, 45u);
    CHECK_EQ(g_load.fields, 46u);
    CHECK(memcmp(g_load.target, s.orig, EW_MAP_LEN_E32) == 0);
}

TEST_CASE(an_edited_file)
{
    unsigned line = 0u;

    CHECK_EQ(edited("arm: 0\n"
                    "  timing = 20  # by hand\n"
                    "TELEM_VOLT: -5\n"
                    "music: anything at all\n"
                    "music: and again\n"
                    "Target: x\nProcessor: TEXT\n"), EW_FILE_OK);
    CHECK_EQ(g_r.kind, EW_FILE_EDITED);
    CHECK_EQ(g_r.fw, EW_FW_E32);
    CHECK_EQ(g_r.len, EW_MAP_LEN_E32);
    CHECK_EQ(g_r.named, 3u);
    CHECK_EQ(ew_e32_get(g_r.page, slot_of("timing")), 20);
    CHECK_EQ(ew_e32_get(g_r.page, slot_of("telem_volt")), -5);
    CHECK_STR_EQ(g_r.music, "");
    CHECK_EQ(edited("arm: 1\narm = 0\n"), EW_FILE_ERR_DUPLICATE);
    CHECK_EQ(edited("nokey: 1\n"), EW_FILE_ERR_FIELD);
    CHECK_EQ(edited("nokey = 1\n"), EW_FILE_ERR_FIELD);
    CHECK_EQ(edited("arm: x\n"), EW_FILE_ERR_VALUE);
    CHECK_EQ(edited("arm: none\n"), EW_FILE_ERR_VALUE);
    CHECK_EQ(edited("arm: \n"), EW_FILE_ERR_VALUE);
    /* A key before the file says whose it is. */
    {
        static const char k_early[] = ED_HEAD "Firmware: ESCape32\narm: 1\n"
                                      "Layout: 17\nedited\n";

        CHECK_EQ(ew_file_parse(k_early, sizeof(k_early) - 1u, &g_r, &line),
                 EW_FILE_ERR_NO_TARGET);
        CHECK_EQ(line, 3u);
    }
    /* A key of ESCape32 in another firmware's file. */
    {
        static const char k_am[] = ED_HEAD "Firmware: AM32\nLayout: 4\n"
                                   "arm: 1\nedited\n";

        CHECK_EQ(ew_file_parse(k_am, sizeof(k_am) - 1u, &g_r, NULL),
                 EW_FILE_ERR_FIELD);
    }
}

TEST_CASE(a_load_writes_keys_and_leaves_music)
{
    static const char k_text[] = ED_E32 "timing: 20\nmusic: abc\nedited\n";
    const size_t t = slot_of("timing");

    read_e32(BANNER);
    CHECK_EQ(ew_file_parse(k_text, sizeof(k_text) - 1u, &g_r, NULL), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 1u);
    CHECK_EQ(g_load.named, 1u);
    CHECK_EQ(g_load.fields, 46u);
    CHECK_EQ(g_load.len, EW_MAP_LEN_E32);
    CHECK_EQ(ew_e32_get(g_load.target, t), 20);
    {
        const int at = esc.starts;

        CHECK_EQ(ew_session_write(&s, g_load.target, g_load.len), EW_SES_BUSY);
        CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
        CHECK(sim_sent_line(&esc, at + 2, "set timing 20\n"));
        CHECK(sim_sent_line(&esc, at + 3, "save\n"));
    }
    CHECK_EQ(esc.saved[t], 20);
    CHECK_STR_EQ(esc.music, "dfa#");
    /* A key to 0. */
    read_e32(BANNER);
    CHECK_EQ(edited("arm: 0\n"), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 1u);
    CHECK_EQ(ew_e32_get(g_load.target, slot_of("arm")), 0);
    /* A verified file's music differs and stays the ESC's as well. */
    read_e32(BANNER);
    strcpy(esc.music, "dfa#");
    text_of_session();
    swap_text("music: dfa#", "music: abc");
    swap_text("\ntiming: 16\n", "\ntiming: 21\n");
    resign();
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_STR_EQ(g_r.music, "abc");
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 1u);
    CHECK_EQ(ew_session_write(&s, g_load.target, g_load.len), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.saved[t], 21);
    CHECK_STR_EQ(esc.music, "dfa#");
}

TEST_CASE(a_load_refuses)
{
    static ew_file_t e17;

    up();
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    e17 = g_r;
    /* A value the key lacks, with the byte of its slot. */
    CHECK_EQ(edited("timing: 32\n"), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_VALUE);
    CHECK_EQ(g_load.bad_off, (uint16_t)(slot_of("timing") * 2u));
    CHECK(memcmp(g_load.target, s.orig, EW_MAP_LEN_E32) == 0);
    /* A key below the one it may not be below. */
    CHECK_EQ(edited("freq_min: 48\nfreq_max: 47\n"), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_VALUE);
    CHECK_EQ(g_load.bad_off, (uint16_t)(slot_of("freq_max") * 2u));
    /* Another revision, either way round; a revision shown and not written. */
    read_e32("ESCape32 rev16.1 [BOARD_F051]");
    CHECK(!s.id.writable);
    CHECK_EQ(ew_file_load(&e17, &s.id, s.orig, &g_load), EW_LOAD_LAYOUT);
    text_of_session();
    CHECK(strstr(g_text, "Layout: 16\n") != NULL);
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_EQ(g_r.layout, 16u);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_READ_ONLY);
    /* Another firmware, either way round. */
    read_e32(BANNER);
    {
        static const char k_am[] = ED_HEAD "Firmware: AM32\nLayout: 4\n"
                                   "edited\n";

        CHECK_EQ(ew_file_parse(k_am, sizeof(k_am) - 1u, &g_r, NULL), EW_FILE_OK);
        CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_FIRMWARE);
    }
    up_am(4u, 2u);
    CHECK_EQ(ew_file_load(&e17, &s.id, s.orig, &g_load), EW_LOAD_FIRMWARE);
    /* And no restore. */
    {
        ew_ident_t id = s.id;
        ew_backup_id_t here;
        ew_backup_id_t from;
        uint8_t blank[EW_PAGE_MAX];

        memset(blank, 0xFF, sizeof(blank));
        memset(&here, 0, sizeof(here));
        id.erased = true;
        CHECK_EQ(ew_file_restore_check(&e17, &id, &here, blank, &from),
                 EW_RESTORE_FIRMWARE);
    }
}

int main(void)
{
    RUN(the_text);
    RUN(a_round_trip_with_music_at_its_limit);
    RUN(a_capture_refuses_what_does_not_fit);
    RUN(a_file_cut_or_damaged_anywhere_is_refused);
    RUN(a_file_with_a_fitting_checksum_is_still_read_whole);
    RUN(a_key_a_read_did_not_bring);
    RUN(an_edited_file);
    RUN(a_load_writes_keys_and_leaves_music);
    RUN(a_load_refuses);
    return test_summary("ew_file_e32");
}
