/*
 * A settings file against an ESC (protocols/escwire/ew_file.c): a load, each
 * refusal, and every byte but the editable ones staying the ESC's; a restore,
 * each refusal, and the whole way from file to page against the simulated ESC;
 * the names a file gets from the store.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_file_rig.h"
#include "servo_store.h"

static ew_file_load_t g_load;

/* ------------------------------------------------------------------ a load */

TEST_CASE(a_load_takes_editable_fields_only)
{
    up_am(4u, 2u);
    /* The file changes every kind of byte and is signed. */
    g_f.page[0x18] = 48u;      /* editable */
    g_f.page[0x05] = 200u;     /* editable */
    g_f.page[0x02] = 9u;       /* the bootloader's */
    g_f.page[0x03] = 9u;       /* shown */
    g_f.page[0x0E] = 9u;       /* reserved */
    g_f.page[0x40] = 0u;       /* tune */
    g_f.page[0xB3] = 0u;       /* CAN */
    g_len = ew_file_format(&g_f, g_text, sizeof(g_text));
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 2u);
    CHECK_EQ(g_load.skipped, 5u);
    CHECK_EQ(g_load.named, 40u);
    CHECK_EQ(g_load.fields, 40u);
    CHECK_EQ(g_load.len, 192u);
    CHECK_EQ(g_load.target[0x18], 48u);
    CHECK_EQ(g_load.target[0x05], 200u);
    for (size_t i = 0u; i < 192u; ++i) {
        if (i != 0x18u && i != 0x05u) {
            CHECK_EQ(g_load.target[i], s.orig[i]);
        }
    }
    /* The guard takes the draft and the ESC holds it after. */
    CHECK_EQ(ew_session_write(&s, g_load.target, g_load.len), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.am[0x18], 48u);
    CHECK_EQ(esc.am[0x03], 2u);
}

static void loaded_write_of_an_efm8(void)
{
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 1u);
    /* A marker and a tag stay the ESC's. */
    CHECK_EQ(g_load.skipped, 2u);
    CHECK_EQ(g_load.target[0x0D], 0x55u);
    CHECK_EQ(g_load.target[0x60], s.orig[0x60]);
    CHECK_EQ(ew_session_write(&s, g_load.target, g_load.len), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

TEST_CASE(a_load_on_an_efm8)
{
    up_s();
    g_f.page[0x15] = 5u;
    g_f.page[0x0D] = 0u;
    g_f.page[0x60] = 'x';
    g_len = ew_file_format(&g_f, g_text, sizeof(g_text));
    const int at = esc.starts;

    loaded_write_of_an_efm8();
    CHECK_EQ(esc.flash[esc.base + 0x15], 5u);
    /* The draft takes the session's whole procedure: its re-read, then erase. */
    CHECK(sim_sent(&esc, at + 3, "02 01 C0 A0"));
    up_bj();
    g_f.page[0x15] = 5u;
    g_f.page[0x0D] = 0u;
    g_f.page[0x70] = 0u;
    g_len = ew_file_format(&g_f, g_text, sizeof(g_text));
    loaded_write_of_an_efm8();
    CHECK_EQ(esc.flash[esc.base + 0x15], 5u);
}

TEST_CASE(a_load_of_an_edited_file_keeps_what_it_does_not_name)
{
    unsigned line = 0u;

    up_am(4u, 2u);
    CHECK_EQ(edited_body("PWM FREQUENCY = 30kHz\n"
                         "MIN DUTY = 1\n"
                         "REVISION = 3 [not loaded]\n", &line), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.named, 2u);
    CHECK_EQ(g_load.fields, 40u);
    CHECK_EQ(g_load.changed, 1u);
    CHECK_EQ(g_load.skipped, 1u);
    CHECK_EQ(g_load.target[0x18], 30u);
    CHECK_EQ(g_load.target[1], 4u);
    for (size_t i = 0u; i < 192u; ++i) {
        if (i != 0x18u) {
            CHECK_EQ(g_load.target[i], s.orig[i]);
        }
    }
    /* A file with the ESC's own values changes nothing. */
    CHECK_EQ(edited_body("PWM FREQUENCY = 24\n", &line), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 0u);
    CHECK(memcmp(g_load.target, s.orig, 192u) == 0);
    /* A raw value out of a field: taken if it is what the ESC holds. */
    s.orig[0x18] = 200u;
    CHECK_EQ(edited_body("PWM FREQUENCY = raw 200\n", &line), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 0u);
    s.orig[0x18] = 24u;
    /* And refused, with its byte, if it would change it. */
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_VALUE);
    CHECK_EQ(g_load.bad_off, 0x18u);
    CHECK_EQ(g_load.target[0x18], 24u);
}

TEST_CASE(a_line_that_reads_as_the_esc_changes_nothing)
{
    char text[32];
    char file[128];
    const ew_field_t *fd;
    int n;

    up_bj();
    /* BRAKING: 3 and 4 read as one percentage. */
    fd = ew_map_find(EW_FW_BJ, 208u, 0x10u);
    CHECK(fd != NULL);
    s.orig[0x10] = 4u;
    (void)ew_field_text(fd, 4u, text, sizeof(text));
    n = snprintf(file, sizeof(file),
                 ED_HEAD "Firmware: Bluejay\nLayout: 208\nBRAKING = %s\n"
                 "edited\n", text);
    CHECK_EQ(ew_file_parse(file, (size_t)n, &g_r, NULL), EW_FILE_OK);
    CHECK_EQ(g_r.page[0x10], 3u);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 0u);
    CHECK_EQ(g_load.target[0x10], 4u);
    /* A line of `raw` names its byte, whatever it reads as. */
    n = snprintf(file, sizeof(file),
                 ED_HEAD "Firmware: Bluejay\nLayout: 208\nBRAKING = raw 3\n"
                 "edited\n");
    CHECK_EQ(ew_file_parse(file, (size_t)n, &g_r, NULL), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 1u);
    CHECK_EQ(g_load.target[0x10], 3u);
    /* A verified file gives the byte, whatever it reads as. */
    g_f.page[0x10] = 3u;
    g_r = g_f;
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_OK);
    CHECK_EQ(g_load.changed, 1u);
    CHECK_EQ(g_load.target[0x10], 3u);
}

TEST_CASE(a_load_of_a_verified_file_with_a_value_the_field_lacks)
{
    up_am(4u, 2u);
    g_f.page[0x05] = 200u;
    g_f.page[0x0C] = 6u;       /* ACTIVE BRAKE: 1 to 5 */
    g_len = ew_file_format(&g_f, g_text, sizeof(g_text));
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_VALUE);
    CHECK_EQ(g_load.bad_off, 0x0Cu);
    /* Nothing of the file is in the target. */
    CHECK(memcmp(g_load.target, s.orig, 192u) == 0);
}

TEST_CASE(a_load_refuses_another_esc)
{
    static ew_file_t am4;
    static ew_ident_t id;
    static uint8_t page[EW_PAGE_MAX];

    up_am(4u, 2u);
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    am4 = g_r;
    /* Arguments, which leave the result alone. */
    memset(&g_load, 0x55, sizeof(g_load));
    CHECK_EQ(ew_file_load(NULL, &s.id, s.orig, &g_load), EW_LOAD_ARG);
    CHECK_EQ(ew_file_load(&am4, NULL, s.orig, &g_load), EW_LOAD_ARG);
    CHECK_EQ(ew_file_load(&am4, &s.id, NULL, &g_load), EW_LOAD_ARG);
    CHECK_EQ(ew_file_load(&am4, &s.id, s.orig, NULL), EW_LOAD_ARG);
    id = s.id;
    id.len = EW_PAGE_MAX + 1u;
    CHECK_EQ(ew_file_load(&am4, &id, s.orig, &g_load), EW_LOAD_ARG);
    CHECK_EQ(g_load.len, 0x5555u);
    /* Another firmware; the target stays the ESC's. */
    up_bj();
    CHECK_EQ(ew_file_load(&am4, &s.id, s.orig, &g_load), EW_LOAD_FIRMWARE);
    CHECK(memcmp(g_load.target, s.orig, 255u) == 0);
    up_s();
    CHECK_EQ(ew_file_load(&am4, &s.id, s.orig, &g_load), EW_LOAD_FIRMWARE);
    /* A file of another firmware into an AM32. */
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    {
        static ew_file_t s33;

        s33 = g_r;
        up_am(4u, 2u);
        CHECK_EQ(ew_file_load(&s33, &s.id, s.orig, &g_load), EW_LOAD_FIRMWARE);
    }
    /* Another revision, either way round. */
    up_am(3u, 2u);
    CHECK_EQ(ew_file_load(&am4, &s.id, s.orig, &g_load), EW_LOAD_LAYOUT);
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    {
        static ew_file_t am3;

        am3 = g_r;
        up_am(4u, 2u);
        CHECK_EQ(ew_file_load(&am3, &s.id, s.orig, &g_load), EW_LOAD_LAYOUT);
        /* Into a revision shown and not written. */
        up_am(5u, 2u);
        CHECK_EQ(ew_file_load(&am3, &s.id, s.orig, &g_load), EW_LOAD_LAYOUT);
    }
    /* A file of that revision into that ESC: nothing is written there. */
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    CHECK_EQ(ew_file_load(&g_r, &s.id, s.orig, &g_load), EW_LOAD_READ_ONLY);
    /* An AM32 built for CAN (Controller Area Network) boards. */
    up_am(4u, 2u);
    id = s.id;
    id.writable = false;
    id.can_build = true;
    CHECK_EQ(ew_file_load(&am4, &id, s.orig, &g_load), EW_LOAD_READ_ONLY);
    /* Erased settings take a restore and no load. */
    id = s.id;
    id.erased = true;
    id.writable = false;
    memset(page, 0xFF, sizeof(page));
    CHECK_EQ(ew_file_load(&am4, &id, page, &g_load), EW_LOAD_ERASED);
    CHECK(memcmp(g_load.target, page, 192u) == 0);
    /* A page reset by the ESC, its tags 0xFF, is shown only. */
    up_s();
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    id = s.id;
    id.writable = false;
    id.why = EW_WHY_RESET_PAGE;
    CHECK_EQ(ew_file_load(&g_r, &id, s.orig, &g_load), EW_LOAD_READ_ONLY);
}

/* --------------------------------------------------------------- a restore */

/* An AM32's file in g_r, the ESC then erased and entered again. */
static void backup_then_erased(uint8_t protocol)
{
    uint8_t draft[EW_PAGE_MAX];

    up_am(4u, protocol);
    CHECK_EQ(parse_len(g_len), EW_FILE_OK);
    memcpy(draft, s.orig, sizeof(draft));
    draft[0x18] = 48u;
    sim_script(&esc, F_ERASE_DEAD, 0, 100, 0u);
    CHECK_EQ(ew_session_write(&s, draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_LOST);
    sim_power_cycle(&esc);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK(s.id.erased);
}

TEST_CASE(a_file_restores_its_own_erased_esc)
{
    ew_backup_id_t here;
    ew_backup_id_t from;

    backup_then_erased(2u);
    ew_session_backup_id(&s, &here);
    CHECK_EQ(ew_file_restore_check(&g_r, &s.id, &here, s.orig, &from),
             EW_RESTORE_OK);
    CHECK_EQ(from.pin_code, 0x14u);
    CHECK_EQ(from.flash_code, 0x35u);
    CHECK_EQ(from.protocol, 2u);
    CHECK_STR_EQ(from.name, "BOARD_F421");
    CHECK_EQ(ew_session_restore(&s, &from, g_r.page, g_r.len), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(memcmp(esc.am + 3, g_r.page + 3, 189u) == 0);
    CHECK(s.id.writable);
    /* The name is compared on all its characters; the protocol stands for
     * the bootloader's version, which an erased page does not show. */
    CHECK_EQ(esc.am[2], g_r.page[2]);
}

TEST_CASE(a_restore_refuses_every_other_file)
{
    static ew_file_t good;
    static ew_file_t f;
    ew_backup_id_t here;
    ew_backup_id_t from;
    ew_ident_t id;

    backup_then_erased(3u);
    good = g_r;
    ew_session_backup_id(&s, &here);
    id = s.id;
    memset(&from, 0x5A, sizeof(from));
    CHECK_EQ(ew_file_restore_check(NULL, &id, &here, s.orig, &from),
             EW_RESTORE_ARG);
    CHECK_EQ(ew_file_restore_check(&good, NULL, &here, s.orig, &from),
             EW_RESTORE_ARG);
    CHECK_EQ(ew_file_restore_check(&good, &id, NULL, s.orig, &from),
             EW_RESTORE_ARG);
    CHECK_EQ(ew_file_restore_check(&good, &id, &here, NULL, &from),
             EW_RESTORE_ARG);
    CHECK_EQ(ew_file_restore_check(&good, &id, &here, s.orig, NULL),
             EW_RESTORE_ARG);
    /* An edited file holds no CRC-32. */
    f = good;
    f.kind = EW_FILE_EDITED;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_NOT_VERIFIED);
    /* Family S and Bluejay have no restore. */
    f = good;
    f.fw = EW_FW_S;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_FIRMWARE);
    f.fw = EW_FW_BJ;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_FIRMWARE);
    id.fw = EW_FW_BJ;
    CHECK_EQ(ew_file_restore_check(&good, &id, &here, s.orig, &from),
             EW_RESTORE_FIRMWARE);
    id = s.id;
    /* An ESC that is not erased, wherever it shows. */
    id.erased = false;
    CHECK_EQ(ew_file_restore_check(&good, &id, &here, s.orig, &from),
             EW_RESTORE_NOT_ERASED);
    id = s.id;
    s.orig[191] = 0xFEu;
    CHECK_EQ(ew_file_restore_check(&good, &id, &here, s.orig, &from),
             EW_RESTORE_NOT_ERASED);
    s.orig[191] = 0xFFu;
    /* A build for CAN boards. */
    id.can_build = true;
    CHECK_EQ(ew_file_restore_check(&good, &id, &here, s.orig, &from),
             EW_RESTORE_CAN_BUILD);
    id = s.id;
    /* A backup of an erased page, or of a revision not written. */
    f = good;
    f.page[0] = 0xFFu;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_BAD_PAGE);
    f = good;
    f.page[1] = 2u;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_BAD_PAGE);
    f.page[1] = 5u;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_BAD_PAGE);
    /* Each piece of the identity. */
    f = good;
    f.pin_code = 0x02u;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_PIN_CODE);
    f = good;
    f.flash_code = 0x1Fu;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_FLASH_CODE);
    f = good;
    f.protocol = 2u;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_PROTOCOL);
    f = good;
    f.name[9] = '2';
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_NAME);
    f.name[0] = '\0';
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_NAME);
    /* Refusals leave the identity alone. */
    CHECK_EQ(from.pin_code, 0x5Au);
    /* What this file takes, the session takes; revision 3 too. */
    f = good;
    f.page[1] = 3u;
    CHECK_EQ(ew_file_restore_check(&f, &id, &here, s.orig, &from),
             EW_RESTORE_OK);
    CHECK_EQ(ew_session_restore(&s, &from, f.page, f.len), EW_SES_BUSY);
}

/* ------------------------------------------------------- the store's names */

static bool fake_mounted(void *ctx)
{
    (void)ctx;
    return true;
}

static bool fake_walk(void *ctx, servo_store_visit_fn visit, void *visit_ctx)
{
    const char *const *names = ctx;

    for (size_t i = 0u; names[i] != NULL; ++i) {
        visit(names[i], false, visit_ctx);
    }
    return true;
}

TEST_CASE(the_names_on_the_card)
{
    char out[SERVO_STORE_NAME_MAX];
    char base[SERVO_STORE_PRESET_MAX];
    const char *names[] = { "ESCW001.TXT", "escw041.txt", "ESCW.TXT",
                            "ESCW1000.TXT", "KST002.TXT", NULL };
    const char *full[] = { "ESCW999.TXT", NULL };
    servo_store_io_t io;

    memset(&io, 0, sizeof(io));
    io.ctx = (void *)names;
    io.stem = EW_FILE_STEM;
    io.mounted = fake_mounted;
    io.walk = fake_walk;
    CHECK_STR_EQ(EW_FILE_STEM, "ESCW");
    /* The next number above the highest. */
    CHECK_EQ(servo_store_preset(&io, base, sizeof(base)), SERVO_STORE_OK);
    CHECK_STR_EQ(base, "ESCW042");
    /* 999 taken: the lowest free one. */
    io.ctx = (void *)full;
    CHECK_EQ(servo_store_preset(&io, base, sizeof(base)), SERVO_STORE_OK);
    CHECK_STR_EQ(base, "ESCW001");
    /* The record is no file of a save. */
    CHECK_EQ(servo_store_name_check(EW_FILE_STEM, "ESCW"),
             SERVO_STORE_NAME_RESERVED);
    CHECK_EQ(servo_store_name_check(EW_FILE_STEM, "escw"),
             SERVO_STORE_NAME_RESERVED);
    CHECK_EQ(servo_store_name_check(EW_FILE_STEM, "ESCW001"),
             SERVO_STORE_NAME_OK);
    CHECK_EQ(servo_store_number(EW_FILE_STEM, "ESCW999.TXT"), 999);
    CHECK_EQ(servo_store_number(EW_FILE_STEM, "ESCW.TXT"), 0);
    /* 32 characters and one past. */
    CHECK(servo_store_file_name(EW_FILE_STEM,
                                "12345678901234567890123456789012", out,
                                sizeof(out)));
    CHECK_EQ(servo_store_name_check(EW_FILE_STEM,
                                    "123456789012345678901234567890123"),
             SERVO_STORE_NAME_TOO_LONG);
}

int main(void)
{
    RUN(a_load_takes_editable_fields_only);
    RUN(a_load_on_an_efm8);
    RUN(a_load_of_an_edited_file_keeps_what_it_does_not_name);
    RUN(a_line_that_reads_as_the_esc_changes_nothing);
    RUN(a_load_of_a_verified_file_with_a_value_the_field_lacks);
    RUN(a_load_refuses_another_esc);
    RUN(a_file_restores_its_own_erased_esc);
    RUN(a_restore_refuses_every_other_file);
    RUN(the_names_on_the_card);
    return test_summary("ew_file_load");
}
