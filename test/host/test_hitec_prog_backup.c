/*
 * The Hitec programmer's load of a backup file (shared/bench/
 * hitec_prog_file.c) on the whole loopback of hitec_prog_rig.h:
 *
 *  - the station case of 2026-10-27: FACTORY DEFAULT of an HS-7955TG,
 *    LEAVE, CONNECT, and the backup file HITEC001 brings every cell back,
 *    RESOLUTION's cell 0x22 included, after CHECK THIS FILE;
 *  - a backup file of another servo (another serial, date or version)
 *    loads as any other file; a file saved by hand refuses 0x22; a file
 *    without the backup line is a backup by a proposed name only;
 *  - a cell no parameter names, 0x1D of an HS-5645MG: WRITE n VALUES counts
 *    it, sends it, and the other writes wait for it;
 *  - the D series: a backup file loads its settings only;
 *  - the backup line the bench writes: the backup file and a save by hand
 *    before the first write carry backup=1, a save after a write backup=0;
 *  - the gates: CANCEL, DISCARD EDITS, the write switch, a load answered
 *    after LEAVE and CONNECT.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

#include "hitec_card.h"
#include "link_crc.h"

/* ---------------------------------------------------------------- helpers */

/* The HS-7955TG of the station, its entry answered. */
static void servo_hs7955(void)
{
    hitec_image_t img;

    hs7955(&img);
    for (unsigned c = 0; c < 256u; ++c) {
        g_drv.eeprom[c] = hitec_image_cell(&img, (uint8_t)c);
    }
    g_drv.c_entry = true;
    g_drv.boot_byte = HITEC_C_ACK;
    g_drv.boot_ns = 59000000u;
    g_drv.version = 0x26u;
}

/* A classic servo read by @p servo, the switch on, a card mounted. */
static void c_ready(void (*servo)(void))
{
    rig();
    servo();
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    g_card = true;
    prun(20u);
}

/* A D485HW read, the switch on, a card mounted. */
static void d_ready(void)
{
    rig();
    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    g_card = true;
    prun(20u);
}

/* The write runs to its end: held, nothing running. */
static void written(void)
{
    for (unsigned i = 0; i < 60000u
                         && (pstate() == HITEC_PROG_BACKUP
                             || pstate() == HITEC_PROG_RECORDING
                             || pstate() == HITEC_PROG_WRITING);
         ++i) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

/* LEAVE, then CONNECT again: a new session, the old backup gone. */
static void new_session(void)
{
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    connected();
    CHECK(!hitec_prog_backup_differs(model()));
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
}

static unsigned msg_arg(void)
{
    return hitec_prog_message(model())->arg;
}

static unsigned msg_reg(void)
{
    return hitec_prog_message(model())->reg;
}

/* Load @p name and let the card answer. */
static void load(const char *name)
{
    CHECK_EQ(hitec_prog_load(model(), name), HITEC_PROG_MSG_NONE);
    prun(50u);
}

/* A servo file of @p img under @p name, with backup=1. */
static void put_backup(const char *name, const hitec_image_t *img)
{
    char text[HITEC_CARD_FILE_MAX];
    const size_t len = hitec_card_file_text(img, true, text, sizeof(text));

    CHECK(len > 0u);
    files_put(name, text, len);
}

/* A servo file of @p img under @p name without the backup line. */
static void put_unmarked(const char *name, const hitec_image_t *img)
{
    static const char k_hex[] = "0123456789ABCDEF";
    char text[HITEC_CARD_FILE_MAX + 16u];
    const char *line;
    size_t len;
    uint16_t crc;

    CHECK(hitec_card_file_text(img, false, text, sizeof(text)) > 0u);
    line = strstr(text, "backup=0\n");
    if (line == NULL) {
        T_FAIL("no backup line");
        return;
    }
    len = (size_t)(line - text);
    crc = link_crc(LINK_CRC_INIT, text, len);
    memcpy(text + len, "check=", 6u);
    for (unsigned i = 0; i < 4u; ++i) {
        text[len + 6u + i] = k_hex[(crc >> (12u - 4u * i)) & 0xFu];
    }
    text[len + 10u] = '\n';
    files_put(name, text, len + 11u);
}

/* Whether the file @p name holds the backup line @p line. */
static bool file_says(const char *name, const char *line)
{
    const rig_file_t *rf = files_find(name);

    return rf != NULL && strstr(rf->text, line) != NULL;
}

/* The HS-7955TG as read, cell 0x22 at @p res. */
static void hs7955_res(hitec_image_t *img, uint8_t res)
{
    hs7955(img);
    CHECK(hitec_image_put_cell(img, 0x22, res));
}

/* ------------------------------------------------------- the station case */

TEST_CASE(a_backup_file_undoes_factory_default_with_cell_0x22)
{
    const uint8_t *set = fix_hs7955();
    hitec_image_t img;

    c_ready(servo_hs7955);
    CHECK_EQ(hitec_prog_layout(model()), HITEC_LAYOUT_HS7);
    /* FACTORY DEFAULT: the backup file goes first, then the copy. */
    CHECK_EQ(hitec_prog_factory(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_FACTORY_DONE);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK(file_says("HITEC001.TXT", "\nbackup=1\ncheck="));
    CHECK_EQ(g_drv.eeprom[0x08], 0x70u);
    CHECK_EQ(g_drv.eeprom[0x13], 0xDCu);
    CHECK_EQ(g_drv.eeprom[0x22], 0x0Bu);
    /* LEAVE and CONNECT: the session's backup is the factory values now,
     * and RESTORE has nothing to give back. */
    new_session();
    /* The backup file of this very servo: every cell a write sends, 0x22
     * one of them, and CHECK THIS FILE first. */
    load("HITEC001");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_CHECK);
    CHECK(hitec_prog_load_waits(model()));
    CHECK_EQ(model()->load_raw, 1u);
    CHECK_EQ(hitec_image_cell(hitec_prog_load_image(model()), 0x22), 0x00u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(hitec_prog_load_confirm(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOADED_RAW);
    CHECK_EQ(msg_reg(), 1u);
    /* The values of cells 0x08, 0x13, 0x14, 0x1B and 0x22. */
    CHECK_EQ(msg_arg(), hitec_prog_changes(model()));
    CHECK_EQ(hitec_prog_changes(model()), 5u);
    img = *hitec_prog_draft(model());
    CHECK_EQ(hitec_image_cell(&img, 0x22), 0x00u);
    CHECK(!hitec_prog_sum_bad(model()));
    CHECK_EQ(g_drv.eeprom[0x22], 0x0Bu);
    /* WRITE n VALUES sends it, 0x22 included, through the plan. */
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    for (unsigned c = 0; c < 0x24u; ++c) {
        if (g_drv.eeprom[c] != set[c]) {
            T_FAIL("cell 0x%02X: 0x%02X, want 0x%02X", c, g_drv.eeprom[c],
                   set[c]);
        }
    }
    /* The file again: nothing left to write. */
    load("HITEC001");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 0u);
}

/* ------------------------------------------------------- not this servo */

TEST_CASE(a_backup_file_of_another_servo_loads_as_any_file)
{
    hitec_image_t img;

    c_ready(servo_hs7955);
    /* Another serial digit. */
    hs7955_res(&img, 0x0B);
    CHECK(hitec_image_put_cell(&img, HITEC_C_SERIAL + 6u, (uint8_t)'9'));
    put_backup("SERIAL.TXT", &img);
    load("SERIAL");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 2u);
    CHECK_EQ(model()->load_raw, 0u);
    CHECK_EQ(hitec_image_cell(hitec_prog_draft(model()), 0x22), 0x00u);
    /* Another date. */
    hs7955_res(&img, 0x0B);
    CHECK(hitec_image_put_cell(&img, HITEC_C_DATE, 0x1B));
    put_backup("DATE.TXT", &img);
    load("DATE");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 2u);
    /* Another version. */
    hs7955_res(&img, 0x0B);
    CHECK(hitec_image_put_version(&img, 0x27));
    put_backup("VERSION.TXT", &img);
    load("VERSION");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 1u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    /* The same servo: 0x22 comes. */
    hs7955_res(&img, 0x0B);
    put_backup("SAME.TXT", &img);
    load("SAME");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_CHECK);
    CHECK_EQ(model()->load_raw, 1u);
}

TEST_CASE(a_file_saved_by_hand_refuses_cell_0x22)
{
    hitec_image_t img;

    c_ready(servo_hs7955);
    hs7955_res(&img, 0x0B);
    /* backup=0, even under a proposed name. */
    files_put_image("HAND.TXT", &img);
    files_put_image("HITEC004.TXT", &img);
    load("HAND");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 1u);
    CHECK_EQ(model()->load_raw, 0u);
    load("HITEC004");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 1u);
    CHECK_EQ(hitec_image_cell(hitec_prog_draft(model()), 0x22), 0x00u);
    /* No backup line: a backup under a proposed name only. */
    put_unmarked("OLD.TXT", &img);
    load("OLD");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 1u);
    put_unmarked("HITEC007.TXT", &img);
    load("HITEC007");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_CHECK);
    CHECK_EQ(model()->load_raw, 1u);
}

/* ----------------------------------------------- a cell no parameter names */

TEST_CASE(a_cell_no_parameter_names_is_counted_written_and_waited_for)
{
    hitec_image_t img;

    c_ready(servo_hs5645);
    hs5645(&img);
    CHECK(hitec_image_put_cell(&img, 0x1D, (uint8_t)(g_drv.eeprom[0x1D]
                                                     ^ 0x01u)));
    put_backup("RAW.TXT", &img);
    load("RAW");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_CHECK);
    /* CANCEL leaves the draft as it was. */
    hitec_prog_load_cancel(model());
    CHECK(!hitec_prog_load_waits(model()));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NO_CHANGES);
    load("RAW");
    CHECK_EQ(hitec_prog_load_confirm(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOADED_RAW);
    CHECK_EQ(msg_arg(), 1u);
    CHECK_EQ(msg_reg(), 1u);
    /* No parameter shows it: it counts as a value of its own. */
    CHECK_EQ(hitec_prog_raw_cells(model(), hitec_prog_draft(model())), 1u);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    /* The other writes wait for it, as for an edit. */
    CHECK_EQ(hitec_prog_restore_backup(model()),
             HITEC_PROG_MSG_EDITS_PENDING);
    CHECK_EQ(hitec_prog_factory(model()), HITEC_PROG_MSG_EDITS_PENDING);
    /* DISCARD EDITS drops it. */
    hitec_prog_discard(model());
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    load("RAW");
    CHECK_EQ(hitec_prog_load_confirm(model()), HITEC_PROG_MSG_NONE);
    /* The switch. */
    CHECK_EQ(hitec_prog_write_enable(model(), false), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(msg_arg(), 1u);
    CHECK_EQ(g_drv.eeprom[0x1D], hitec_image_cell(&img, 0x1D));
    CHECK(!hitec_prog_sum_bad(model()));
}

/* --------------------------------------------------------------- D series */

TEST_CASE(a_d_series_backup_file_loads_its_settings_only)
{
    hitec_image_t img;

    d_ready();
    d485(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50),
             HITEC_SET_OK);
    CHECK(hitec_image_put_reg(&img, 0x64, 7u));
    put_backup("DBK.TXT", &img);
    load("DBK");
    /* Every register a D series write sends is a setting: nothing more
     * comes, and register 0x64 stays. */
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOADED);
    CHECK_EQ(msg_arg(), 1u);
    CHECK_EQ(msg_reg(), 1u);
    CHECK_EQ(model()->load_raw, 0u);
    CHECK_EQ(hitec_prog_raw_cells(model(), hitec_prog_draft(model())), 0u);
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0x64),
             hitec_image_reg(hitec_prog_servo(model()), 0x64));
    /* Another model number: another servo, CHECK THIS FILE as for any. */
    CHECK(hitec_image_put_reg(&img, HITEC_D_REG_MODEL, 34999u));
    put_backup("DOTHER.TXT", &img);
    load("DOTHER");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_CHECK);
    CHECK_EQ(model()->load_raw, 0u);
    {
        bool other = false;

        (void)hitec_prog_load_pending(model(), &other);
        CHECK(other);
    }
}

/* ------------------------------------------------------- the backup line */

TEST_CASE(the_bench_marks_its_backup_files)
{
    c_ready(servo_hs5645);
    /* A save by hand before the first write: the backup file. */
    CHECK_EQ(hitec_prog_save(model(), "FIRST"), HITEC_PROG_MSG_NONE);
    prun(50u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_SAVED);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "FIRST");
    CHECK(file_says("FIRST.TXT", "\nbackup=1\ncheck="));
    /* Once more before a write: the same servo as found, a backup too. */
    CHECK_EQ(hitec_prog_save(model(), "AGAIN"), HITEC_PROG_MSG_NONE);
    prun(50u);
    CHECK(file_says("AGAIN.TXT", "\nbackup=1\ncheck="));
    /* After a write: backup=0. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(hitec_prog_save(model(), "AFTER"), HITEC_PROG_MSG_NONE);
    prun(50u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_SAVED);
    CHECK(file_says("AFTER.TXT", "\nbackup=0\ncheck="));
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "FIRST");
}

TEST_CASE(a_backup_load_answered_after_leave_and_connect_changes_nothing)
{
    hitec_image_t img;

    c_ready(servo_hs7955);
    hs7955_res(&img, 0x0B);
    put_backup("LATE.TXT", &img);
    g_files_slow = true;
    CHECK_EQ(hitec_prog_load(model(), "LATE"), HITEC_PROG_MSG_NONE);
    prun(20u);
    new_session();
    g_files_slow = false;
    files_answer();
    prun(20u);
    CHECK(!hitec_prog_load_waits(model()));
    CHECK(msg() != HITEC_PROG_MSG_LOAD_CHECK);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

int main(void)
{
    RUN(a_backup_file_undoes_factory_default_with_cell_0x22);
    RUN(a_backup_file_of_another_servo_loads_as_any_file);
    RUN(a_file_saved_by_hand_refuses_cell_0x22);
    RUN(a_cell_no_parameter_names_is_counted_written_and_waited_for);
    RUN(a_d_series_backup_file_loads_its_settings_only);
    RUN(the_bench_marks_its_backup_files);
    RUN(a_backup_load_answered_after_leave_and_connect_changes_nothing);
    return test_summary("hitec_prog_backup");
}
