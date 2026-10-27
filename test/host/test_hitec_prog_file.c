/*
 * The Hitec programmer's files and backups (shared/bench/hitec_prog_file.c)
 * on the whole loopback of hitec_prog_rig.h:
 *
 *  - the backup file before the first write that sends a step: saved
 *    first, once a session; a card that is full, write protected, failing,
 *    gone or silent for 5000ms ends the write with nothing sent; ABORT and
 *    STOP while it waits; the wait across the millisecond clock's wrap;
 *  - no card: the write goes, the backup stays in RAM; a card put in later
 *    gets the backup, not the servo as it stands;
 *  - a save by hand: the backup file before the first write, the servo as
 *    read and never the draft; its names and refusals; one the card ends
 *    after 5000ms;
 *  - FILES: pages of 5, the page past the last, no files, a request out, a
 *    list given up after 5000ms and its late answer, the request id's wrap,
 *    a write held back while a request is out, the gates of a stopped
 *    port and of a STOP;
 *  - a load: the draft only, with the write switch off as on; the other
 *    series, another layout, another model, a soft rule, a file with the
 *    servo's values, values outside the editable ones counted, a raw value,
 *    a hand-edited file (a wrong check, a cut text, an extra line), a file
 *    of no servo; the gates (not in programming mode, a read running, no
 *    read, a write that needs a read, a STOP, ARMED refused while held, a
 *    servo gone before the answer, an answer after LEAVE and CONNECT);
 *  - RESTORE of the session's backup, raw values included, after a write
 *    and after a lost link; another servo on the port;
 *  - FACTORY DEFAULT of a classic servo; none for the D series.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

#include "hitec_card.h"
#include "link_crc.h"

/* ---------------------------------------------------------------- helpers */

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

/* An HS-5645MG read, the switch on, a card mounted. */
static void c_ready(void)
{
    rig();
    servo_hs5645();
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
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

/* The answer to a request: 3 frames. */
static void answered(void)
{
    prun(50u);
}

static unsigned msg_arg(void)
{
    return hitec_prog_message(model())->arg;
}

static unsigned msg_reg(void)
{
    return hitec_prog_message(model())->reg;
}

/* A D485HW file with parameter @p id at @p v. */
static void d_file(const char *name, uint8_t id, int32_t v)
{
    hitec_image_t img;

    d485(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, id, v), HITEC_SET_OK);
    files_put_image(name, &img);
}

/* The text of a servo file of @p body with its check line. */
static size_t with_check(const char *body, char *out)
{
    static const char k_hex[] = "0123456789ABCDEF";
    const size_t n = strlen(body);
    const uint16_t crc = link_crc(LINK_CRC_INIT, body, n);

    memcpy(out, body, n);
    memcpy(out + n, "check=", 6u);
    for (unsigned i = 0; i < 4u; ++i) {
        out[n + 6u + i] = k_hex[(crc >> (12u - 4u * i)) & 0xFu];
    }
    out[n + 10u] = '\n';
    return n + 11u;
}

/* Load @p name and let the card answer. */
static void load(const char *name)
{
    CHECK_EQ(hitec_prog_load(model(), name), HITEC_PROG_MSG_NONE);
    answered();
}

/* ------------------------------------------------------ the backup file */

TEST_CASE(the_first_write_saves_the_backup_file_first_and_once)
{
    hitec_image_t img;

    d_ready();
    CHECK(hitec_prog_backup_due(model()));
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_BACKUP);
    CHECK(hitec_prog_card_busy(model()));
    /* A file request waits while the backup file is on its way. */
    CHECK_EQ(hitec_prog_files_gate(model()), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(g_drv.d_writes, 0u);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_files_saves, 1u);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK(!hitec_prog_backup_due(model()));
    CHECK(!hitec_prog_backup_ram_only(model()));
    CHECK(files_image("HITEC001.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 4095u);
    CHECK_EQ(g_drv.dreg[0x54], 10u);
    /* The second write of the session saves no file. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 30),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_files_saves, 1u);
}

TEST_CASE(without_a_card_the_backup_stays_in_ram_until_a_card_comes)
{
    hitec_image_t img;

    d_ready();
    g_card = false;
    prun(20u);
    CHECK(!hitec_prog_backup_due(model()));
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    CHECK(pstate() != HITEC_PROG_BACKUP);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK(hitec_prog_backup_ram_only(model()));
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK_EQ(g_files_saves, 0u);
    /* A card comes: the next write saves the backup first. */
    g_card = true;
    prun(20u);
    CHECK(hitec_prog_backup_due(model()));
    /* A save by hand after a write holds the written values: no backup. */
    CHECK_EQ(hitec_prog_save(model(), "AFTER"), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(msg(), HITEC_PROG_MSG_SAVED);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(files_image("AFTER.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 10u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 30),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_BACKUP);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK(!hitec_prog_backup_ram_only(model()));
    /* The file holds the servo as the session found it. */
    CHECK(files_image("HITEC001.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 4095u);
}

TEST_CASE(a_card_that_refuses_the_backup_file_keeps_the_write_from_going)
{
    static const struct {
        uint8_t  create;
        unsigned m;
    } k_case[] = {
        { SERVO_STORE_IO_FULL, HITEC_PROG_MSG_CARD_FULL },
        { SERVO_STORE_IO_DENIED, HITEC_PROG_MSG_CARD_DENIED },
        { SERVO_STORE_IO_ERROR, HITEC_PROG_MSG_CARD_WRITE },
    };

    for (size_t i = 0; i < sizeof(k_case) / sizeof(k_case[0]); ++i) {
        d_ready();
        g_files_create = k_case[i].create;
        CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
                 HITEC_PROG_EDIT_OK);
        CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
        prun(200u);
        CHECK_EQ(pstate(), HITEC_PROG_HELD);
        CHECK_EQ(msg(), k_case[i].m);
        CHECK_EQ(g_drv.d_writes, 0u);
        CHECK(!hitec_prog_record(model()));
        CHECK_EQ(g_card_writes, 0u);
        CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
        /* The card takes it: the write goes. */
        g_files_create = (uint8_t)SERVO_STORE_IO_OK;
        CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
        written();
        CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
        CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    }
    /* The card leaves between the request and its answer. */
    d_ready();
    g_files_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    prun(50u);
    CHECK_EQ(pstate(), HITEC_PROG_BACKUP);
    g_card = false;
    prun(20u);
    files_answer();
    prun(200u);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_NONE);
    CHECK_EQ(msg_arg(), 0u);
    CHECK_EQ(g_drv.d_writes, 0u);
}

TEST_CASE(a_backup_file_not_answered_in_5000ms_ends_the_write_unsent)
{
    d_ready();
    g_files_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    prun(HITEC_PROG_FILES_MS - 50u);
    CHECK_EQ(pstate(), HITEC_PROG_BACKUP);
    prun(100u);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_WRITE);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK(!hitec_prog_card_busy(model()));
    /* The late answer: the file holds the backup, and nothing is sent. */
    files_answer();
    prun(200u);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_WRITE);
    CHECK_EQ(g_drv.d_writes, 0u);
    /* The next write saves no second file. */
    g_files_slow = false;
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_files_saves, 1u);
}

TEST_CASE(abort_while_the_backup_file_is_saved_sends_nothing)
{
    d_ready();
    g_files_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    prun(50u);
    CHECK_EQ(pstate(), HITEC_PROG_BACKUP);
    /* FORGET waits for the write as well. */
    CHECK(hitec_prog_forget(model()) != HITEC_PROG_MSG_FORGOTTEN);
    hitec_prog_abort(model());
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    files_answer();
    prun(500u);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK(!hitec_prog_record(model()));
    CHECK_EQ(hitec_prog_changes(model()), 1u);
}

TEST_CASE(a_stop_while_the_backup_file_is_saved_sends_nothing)
{
    d_ready();
    g_files_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    prun(50u);
    CHECK_EQ(pstate(), HITEC_PROG_BACKUP);
    press_stop();
    prun(100u);
    files_answer();
    prun(2000u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK(pstate() != HITEC_PROG_WRITING);
    CHECK(pstate() != HITEC_PROG_RECORDING);
    CHECK(!hitec_prog_record(model()));
}

TEST_CASE(the_backup_file_wait_runs_across_the_clocks_wrap)
{
    rig_from(0xFFFFFFFFu - 2600u, SUPPLY_ON, false);
    servo_d485();
    connected();
    CHECK(g_ms > 0x80000000u);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    g_card = true;
    g_files_slow = true;
    prun(20u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    prun(HITEC_PROG_FILES_MS - 50u);
    CHECK(g_ms < 0x10000000u);
    CHECK_EQ(pstate(), HITEC_PROG_BACKUP);
    prun(100u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_WRITE);
    /* Once more, answered inside the wait: the write goes. */
    g_files_slow = false;
    files_reset();
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0x54], 10u);
}

TEST_CASE(the_session_backup_stays_for_one_servo_and_goes_with_another)
{
    c_ready();
    card_in();
    /* READ ALL of the same servo keeps it. */
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "PRE");
    /* Another serial number on the port: that read is the backup. */
    g_drv.eeprom[0x80] = (uint8_t)'9';
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(hitec_prog_backup_due(model()));
    CHECK(!hitec_prog_backup_differs(model()));
    /* LEAVE ends the session and its backup. */
    CHECK_EQ(hitec_prog_save(model(), "SECOND"), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "SECOND");
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(!hitec_prog_backup_due(model()));
    connected();
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(hitec_prog_backup_due(model()));
}

/* ------------------------------------------------------------- saves */

TEST_CASE(a_save_by_hand_holds_the_servo_as_read_and_its_refusals)
{
    char longest[HITEC_CARD_NAME_MAX + 1u];
    hitec_image_t img;

    d_ready();
    /* An edit stays in the draft: the file holds the servo. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_save(model(), "WING"), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(msg(), HITEC_PROG_MSG_SAVED);
    CHECK_STR_EQ(hitec_prog_card_name(model()), "WING");
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "WING");
    CHECK(files_image("WING.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 4095u);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    /* The card's refusals. */
    CHECK_EQ(hitec_prog_save(model(), "wing"), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(msg(), HITEC_PROG_MSG_NAME_EXISTS);
    CHECK_EQ(hitec_prog_save(model(), "HITEC"), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(msg(), HITEC_PROG_MSG_NAME_BAD);
    g_files_create = (uint8_t)SERVO_STORE_IO_FULL;
    CHECK_EQ(hitec_prog_save(model(), "FULL"), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_FULL);
    g_files_create = (uint8_t)SERVO_STORE_IO_OK;
    /* The model's: no name, 33 characters, NULL. */
    CHECK_EQ(hitec_prog_save(model(), ""), HITEC_PROG_MSG_NAME_BAD);
    memset(longest, 'A', HITEC_CARD_NAME_MAX);
    longest[HITEC_CARD_NAME_MAX] = '\0';
    CHECK_EQ(hitec_prog_save(model(), longest), HITEC_PROG_MSG_NAME_BAD);
    CHECK_EQ(hitec_prog_save(model(), NULL), HITEC_PROG_MSG_NAME_BAD);
    /* 32 characters is the longest name. */
    longest[HITEC_CARD_NAME_MAX - 1u] = '\0';
    CHECK_EQ(hitec_prog_save(model(), longest), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(msg(), HITEC_PROG_MSG_SAVED);
    /* One request at a time. */
    g_files_slow = true;
    CHECK_EQ(hitec_prog_save(model(), "SLOW"), HITEC_PROG_MSG_NONE);
    prun(20u);
    CHECK_EQ(hitec_prog_save(model(), "OTHER"), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_load(model(), "WING"), HITEC_PROG_MSG_BUSY);
    /* Given up after 5000ms; the file made later says nothing. */
    prun(HITEC_PROG_FILES_MS);
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_WRITE);
    files_answer();
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_WRITE);
    CHECK(files_image("SLOW.TXT", &img));
    /* No card: refused with the slot's state. */
    g_card = false;
    prun(20u);
    CHECK_EQ(hitec_prog_save(model(), "NONE"), HITEC_PROG_MSG_CARD_NONE);
    CHECK_EQ(msg_arg(), 0u);
}

/* ------------------------------------------------------------- lists */

TEST_CASE(files_come_in_pages_of_5_with_their_models)
{
    char name[16];
    const char *m = NULL;
    unsigned total = 0, first = 0;
    uint32_t seq;
    hitec_image_t c;

    d_ready();
    hs5645(&c);
    for (unsigned i = 1; i <= 11u; ++i) {
        snprintf(name, sizeof(name), "F%02u.TXT", i);
        files_put_image(name, &(hitec_image_t){ 0 });
    }
    /* Images of no series make no text: put real ones over them. */
    g_files_n = 0;
    for (unsigned i = 1; i <= 11u; ++i) {
        hitec_image_t d;

        d485(&d);
        snprintf(name, sizeof(name), "F%02u.TXT", i);
        files_put_image(name, i == 3u ? &c : &d);
    }
    files_put("HITEC007.TXT", "x", 1u);
    seq = hitec_prog_files_seq(model());
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(hitec_prog_files_seq(model()), seq + 1u);
    CHECK_EQ(hitec_prog_file_count(model(), &total, &first), 5u);
    CHECK_EQ(total, 12u);
    CHECK_EQ(first, 0u);
    CHECK_STR_EQ(hitec_prog_file_name(model(), 0u, &m), "F01");
    CHECK_STR_EQ(m, "D485HW");
    CHECK_STR_EQ(hitec_prog_file_name(model(), 2u, &m), "F03");
    CHECK_STR_EQ(m, "HS-5645MG");
    CHECK_STR_EQ(hitec_prog_file_name(model(), 5u, &m), "");
    CHECK_STR_EQ(m, "");
    CHECK_STR_EQ(hitec_prog_preset_name(model()), "HITEC008");
    /* The proposed names stand at the place of HITEC000, so after F11. */
    CHECK_EQ(hitec_prog_files(model(), 10u), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(hitec_prog_file_count(model(), &total, &first), 2u);
    CHECK_EQ(first, 10u);
    CHECK_STR_EQ(hitec_prog_file_name(model(), 0u, NULL), "F11");
    CHECK_STR_EQ(hitec_prog_file_name(model(), 1u, &m), "HITEC007");
    CHECK_STR_EQ(m, "");
    /* Past the last: none; a page number above 65535 is held at it. */
    CHECK_EQ(hitec_prog_files(model(), 12u), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(hitec_prog_file_count(model(), NULL, NULL), 0u);
    CHECK_EQ(hitec_prog_files(model(), 70000u), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(hitec_prog_file_count(model(), NULL, &first), 0u);
    CHECK_EQ(first, 65535u);
}

TEST_CASE(a_request_waits_for_the_card_and_its_id_wraps)
{
    hitec_card_done_t d;

    d_ready();
    /* The id after 0xFFFFFFFF is 1: 0 is no request. */
    g_p.card_id = 0xFFFFFFFFu;
    g_files_slow = true;
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_NONE);
    prun(20u);
    CHECK_EQ(g_creq.id, 1u);
    /* A write waits while a request of the card is out. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_BUSY);
    /* An answer of no operation ends the request and says nothing. */
    memset(&d, 0, sizeof(d));
    d.op = HITEC_CARD_IDLE;
    d.id = 1u;
    hitec_prog_card_done(model(), &d);
    CHECK(!hitec_prog_card_busy(model()));
    CHECK_EQ(msg(), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    g_files_slow = false;
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
}

TEST_CASE(files_need_the_port_in_programming_mode_and_a_servo_read)
{
    d_ready();
    /* SAFE at the bench: the port stops, the servo leaves programming
     * mode. */
    disarm();
    prun(100u);
    CHECK(hitec_prog_stopped(model()));
    CHECK(hitec_prog_files(model(), 0u) != HITEC_PROG_MSG_NONE);
    CHECK(hitec_prog_save(model(), "S") != HITEC_PROG_MSG_NONE);
    /* A STOP: the servo read is gone. */
    d_ready();
    press_stop();
    prun(200u);
    CHECK(hitec_prog_files(model(), 0u) != HITEC_PROG_MSG_NONE);
    CHECK(hitec_prog_save(model(), "S") != HITEC_PROG_MSG_NONE);
    CHECK_EQ(g_files_saves + g_files_lists, 0u);
    /* Held, the entry not done; the entry done, no read. */
    d_ready();
    g_p.ses.in_mode = 0u;
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_NOT_IN_MODE);
    g_p.ses.in_mode = 1u;
    g_p.have_servo = false;
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_NOT_READ);
    CHECK_EQ(hitec_prog_files_gate(model()), HITEC_PROG_MSG_NOT_READ);
}

TEST_CASE(a_list_the_card_cannot_give)
{
    uint32_t seq;

    d_ready();
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(msg(), HITEC_PROG_MSG_NO_FILES);
    CHECK_STR_EQ(hitec_prog_preset_name(model()), "HITEC001");
    g_files_walk_fails = true;
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_READ);
    g_files_walk_fails = false;
    /* Not answered in 5000ms: given up, the late list not taken. */
    files_put("A.TXT", "x", 1u);
    g_files_slow = true;
    seq = hitec_prog_files_seq(model());
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_NONE);
    prun(HITEC_PROG_FILES_MS - 50u);
    CHECK(hitec_prog_card_busy(model()));
    prun(100u);
    CHECK(!hitec_prog_card_busy(model()));
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_READ);
    files_answer();
    CHECK_EQ(hitec_prog_files_seq(model()), seq);
    /* An answer to no request out is not taken either. */
    g_cdone.id = 999u;
    hitec_prog_card_done(model(), &g_cdone);
    hitec_prog_card_done(model(), NULL);
    CHECK_EQ(hitec_prog_files_seq(model()), seq);
    /* No card. */
    g_files_slow = false;
    g_card = false;
    prun(20u);
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_CARD_NONE);
}

/* ------------------------------------------------------------- loads */

TEST_CASE(a_load_changes_the_draft_only_with_the_switch_off_as_on)
{
    hitec_image_t img;

    d_ready();
    CHECK_EQ(hitec_prog_write_enable(model(), false), HITEC_PROG_MSG_NONE);
    d485(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50),
             HITEC_SET_OK);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DEAD_BAND, 3),
             HITEC_SET_OK);
    files_put_image("TWO.TXT", &img);
    load("TWO");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOADED);
    CHECK_EQ(msg_arg(), 2u);
    CHECK_EQ(msg_reg(), 0u);
    CHECK_STR_EQ(hitec_prog_card_name(model()), "TWO");
    CHECK_EQ(hitec_prog_changes(model()), 2u);
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0x54), 10u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0x54], 10u);
    /* The file again: nothing to write. */
    load("TWO");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 0u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    /* No name, a name too long, a file not on the card. */
    CHECK_EQ(hitec_prog_load(model(), ""), HITEC_PROG_MSG_CARD_READ);
    CHECK_EQ(hitec_prog_load(model(), NULL), HITEC_PROG_MSG_CARD_READ);
    load("GONE");
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_READ);
    g_files_read_fails = true;
    load("TWO");
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_READ);
}

TEST_CASE(a_load_refuses_the_other_series_and_another_layout)
{
    hitec_image_t img;

    d_ready();
    hs5645(&img);
    files_put_image("C.TXT", &img);
    load("C");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SERIES);
    CHECK_EQ(msg_arg(), HITEC_CLASSIC);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    c_ready();
    d485(&img);
    files_put_image("D.TXT", &img);
    load("D");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SERIES);
    CHECK_EQ(msg_arg(), HITEC_D);
    hs7955(&img);
    files_put_image("HS7.TXT", &img);
    load("HS7");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_LAYOUT);
    CHECK_EQ(msg_arg(), HITEC_LAYOUT_HS7);
    CHECK_EQ(msg_reg(), HITEC_LAYOUT_HS5);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    /* A classic name of no model known: the HS-5xxx layout, another
     * model.  Its name alone is no value to load: 16 cells not loaded. */
    hs5645(&img);
    for (unsigned c = 0; c < HITEC_C_NAME_LEN; ++c) {
        CHECK(hitec_image_put_cell(&img, (uint8_t)(HITEC_C_NAME + c),
                                   (uint8_t)'Z'));
    }
    files_put_image("ZZZ.TXT", &img);
    load("ZZZ");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 16u);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_SPEED, 50),
             HITEC_SET_OK);
    files_put_image("ZZZ2.TXT", &img);
    load("ZZZ2");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_CHECK);
    {
        bool other = false;

        CHECK_EQ(hitec_prog_load_pending(model(), &other), 0u);
        CHECK(other);
    }
}

TEST_CASE(a_hand_edited_file_is_refused_whole)
{
    char text[HITEC_CARD_FILE_MAX + 16u];
    char body[HITEC_CARD_FILE_MAX];
    hitec_image_t img;
    size_t len;
    const char *chk;

    d_ready();
    d485(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50),
             HITEC_SET_OK);
    len = hitec_card_file_text(&img, text, sizeof(text));
    /* A changed digit. */
    text[50] = text[50] == '0' ? '1' : '0';
    files_put("DIGIT.TXT", text, len);
    load("DIGIT");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_DAMAGED);
    CHECK_EQ(msg_arg(), HITEC_CARD_ERR_CHECK);
    /* Cut before the check line. */
    CHECK(hitec_card_file_text(&img, text, sizeof(text)) > 0u);
    chk = strstr(text, "check=");
    if (chk == NULL) {
        T_FAIL("no check line");
        return;
    }
    files_put("CUT.TXT", text, (size_t)(chk - text));
    load("CUT");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_DAMAGED);
    CHECK_EQ(msg_arg(), HITEC_CARD_ERR_TRUNCATED);
    /* An extra line, the check made over it: line 5 is not due. */
    memcpy(body, text, (size_t)(chk - text));
    memcpy(body + (chk - text), "note=1\n", 8u);
    len = with_check(body, text);
    files_put("EXTRA.TXT", text, len);
    load("EXTRA");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_LINE);
    CHECK_EQ(msg_arg(), 5u);
    /* No servo file at all. */
    files_put("NOTE.TXT", "a note\n", 7u);
    load("NOTE");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_BAD);
    /* Larger than any servo file. */
    memset(text, 'x', sizeof(text));
    files_put("BIG.TXT", text, sizeof(text));
    load("BIG");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_BAD);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

TEST_CASE(a_file_of_another_model_or_past_a_soft_rule_waits_for_a_check)
{
    hitec_image_t img;
    bool other = true;

    d_ready();
    /* LEFT END 3351 -> 3300 breaks the end rule. */
    d_file("END.TXT", HITEC_P_D_LEFT_END, 3300);
    load("END");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_CHECK);
    CHECK(hitec_prog_load_waits(model()));
    CHECK_EQ(hitec_prog_load_pending(model(), &other),
             HITEC_RULE_BIT(HITEC_RULE_END));
    CHECK(!other);
    CHECK_EQ(hitec_image_reg(hitec_prog_load_image(model()), 0xB2), 3300u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    /* CANCEL: the draft as it was. */
    hitec_prog_load_cancel(model());
    CHECK(!hitec_prog_load_waits(model()));
    CHECK(hitec_prog_load_image(model()) == NULL);
    CHECK_EQ(hitec_prog_load_pending(model(), &other), 0u);
    CHECK_EQ(hitec_prog_load_confirm(model()), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    /* HOLD 2s TO LOAD: the draft, the rule confirmed for its value. */
    load("END");
    CHECK_EQ(hitec_prog_load_confirm(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOADED);
    CHECK_EQ(msg_arg(), 1u);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_LEFT_END, 3300),
             HITEC_PROG_EDIT_OK);
    hitec_prog_discard(model());
    /* Another model number of the D series: asks, no rule. */
    d485(&img);
    CHECK(hitec_image_put_reg(&img, HITEC_D_REG_MODEL, 34999u));
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50),
             HITEC_SET_OK);
    files_put_image("OTHER.TXT", &img);
    load("OTHER");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_CHECK);
    CHECK_EQ(hitec_prog_load_pending(model(), &other), 0u);
    CHECK(other);
    CHECK_EQ(hitec_prog_load_confirm(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    /* A load that waits when the port stops is dropped at the hold. */
    hitec_prog_discard(model());
    load("END");
    CHECK(hitec_prog_load_waits(model()));
    disarm();
    prun(100u);
    CHECK(hitec_prog_stopped(model()));
    CHECK(hitec_prog_load_confirm(model()) != HITEC_PROG_MSG_NONE);
    CHECK(!hitec_prog_load_waits(model()));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

TEST_CASE(values_of_a_file_outside_the_editable_ones_are_counted)
{
    hitec_image_t img;

    d_ready();
    /* The position moves: not counted.  Register 0x64 and 0x04: 2. */
    d485(&img);
    CHECK(hitec_image_put_reg(&img, 0x0C, 1234u));
    files_put_image("POS.TXT", &img);
    load("POS");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 0u);
    CHECK(hitec_image_put_reg(&img, 0x64, 7u));
    CHECK(hitec_image_put_reg(&img, 0x04, 7u));
    files_put_image("RO.TXT", &img);
    load("RO");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(msg_arg(), 2u);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50),
             HITEC_SET_OK);
    files_put_image("RO2.TXT", &img);
    load("RO2");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOADED);
    CHECK_EQ(msg_arg(), 1u);
    CHECK_EQ(msg_reg(), 2u);
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0x64),
             hitec_image_reg(hitec_prog_servo(model()), 0x64));
    /* Classic: a serial digit and a factory cell, not the balance. */
    c_ready();
    hs5645(&img);
    CHECK(hitec_image_put_cell(&img, 0x80, (uint8_t)'7'));
    CHECK(hitec_image_put_cell(&img, 0x30, 0x51));
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_SPEED, 50),
             HITEC_SET_OK);
    files_put_image("CSER.TXT", &img);
    load("CSER");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOADED);
    CHECK_EQ(msg_reg(), 2u);
    CHECK_EQ(hitec_image_cell(hitec_prog_draft(model()), 0x80),
             hitec_image_cell(hitec_prog_servo(model()), 0x80));
    /* The draft's balance is its own. */
    CHECK(!hitec_prog_sum_bad(model()));
}

TEST_CASE(a_raw_value_comes_back_by_a_load)
{
    d_ready();
    g_drv.dreg[0x54] = 7u;   /* fits no speed */
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_save(model(), "RAW"), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(msg(), HITEC_PROG_MSG_SAVED);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(g_drv.dreg[0x54], 10u);
    load("RAW");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOADED);
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0x54), 7u);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0x54], 7u);
}

TEST_CASE(a_load_needs_a_servo_read_in_programming_mode)
{
    d_ready();
    d_file("F.TXT", HITEC_P_D_SPEED, 50);
    /* A read runs. */
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_load(model(), "F"), HITEC_PROG_MSG_BUSY);
    until(HITEC_PROG_HELD);
    /* ARMED is refused while the port is held: no load goes ARMED. */
    CHECK(!arm());
    CHECK_EQ(g_arm_refused, 1u);
    /* A write that needs a read after it. */
    g_drv.d_drop = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_save(model(), "PRE"), HITEC_PROG_MSG_NONE);
    answered();
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_load(model(), "F"), HITEC_PROG_MSG_BUSY);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_VERIFY_FAIL);
    CHECK_EQ(hitec_prog_load(model(), "F"), HITEC_PROG_MSG_MUST_READ);
    CHECK_EQ(hitec_prog_save(model(), "X"), HITEC_PROG_MSG_MUST_READ);
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_MUST_READ);
    g_drv.d_drop = false;
    /* A STOP: the port leaves programming mode. */
    d_ready();
    d_file("F.TXT", HITEC_P_D_SPEED, 50);
    press_stop();
    prun(200u);
    CHECK(hitec_prog_load(model(), "F") != HITEC_PROG_MSG_NONE);
    prun(200u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    /* No servo read: before CONNECT. */
    rig();
    servo_d485();
    g_card = true;
    prun(20u);
    CHECK_EQ(hitec_prog_load(model(), "F"), HITEC_PROG_MSG_NOT_IN_MODE);
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_NOT_IN_MODE);
}

TEST_CASE(a_load_answered_after_the_servo_went_changes_nothing)
{
    d_ready();
    d_file("F.TXT", HITEC_P_D_SPEED, 50);
    g_files_slow = true;
    CHECK_EQ(hitec_prog_load(model(), "F"), HITEC_PROG_MSG_NONE);
    prun(20u);
    disarm();
    prun(100u);
    files_answer();
    CHECK(msg() != HITEC_PROG_MSG_LOADED);
    CHECK(msg() != HITEC_PROG_MSG_LOAD_CHECK);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

/* ----------------------------------------------------------- restores */

TEST_CASE(restore_of_the_backup_writes_every_value_back_raw_included)
{
    hitec_image_t img;

    rig();
    servo_d485();
    g_drv.dreg[0x54] = 7u;   /* fits no speed */
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    card_in();
    CHECK(!hitec_prog_backup_differs(model()));
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NO_CHANGES);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_LEFT_END, 3300),
             HITEC_PROG_EDIT_CHECK);
    CHECK_EQ(hitec_prog_confirm(model()), HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0x54], 10u);
    CHECK_EQ(g_drv.dreg[0xB2], 3300u);
    CHECK(hitec_prog_backup_differs(model()));
    CHECK(hitec_prog_backup_image(model(), &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 7u);
    /* Refused with an edit in the draft and with the switch off. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 30),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_restore_backup(model()),
             HITEC_PROG_MSG_EDITS_PENDING);
    hitec_prog_discard(model());
    CHECK_EQ(hitec_prog_write_enable(model(), false), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_BACKUP_RESTORED);
    CHECK_EQ(g_drv.dreg[0x54], 7u);
    CHECK_EQ(g_drv.dreg[0xB2], 3351u);
    CHECK(!hitec_prog_record(model()));
    CHECK(!hitec_prog_backup_differs(model()));
    /* No servo read, no backup: nothing to restore. */
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK(!hitec_prog_backup_image(model(), &img));
    CHECK(hitec_prog_restore_backup(model()) != HITEC_PROG_MSG_NONE);
}

TEST_CASE(after_a_lost_link_the_backup_file_brings_the_old_values_back)
{
    hitec_image_t img;

    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    for (unsigned i = 0; i < 60000u && g_drv.d_saves == 0u; ++i) {
        pms();
    }
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    link_silent();
    prun(400u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITE_CUT);
    prun(200u);
    link_back();
    until(HITEC_PROG_OTHER);
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK(arm());
    disarm();
    prun(50u);
    connected();
    CHECK_EQ(hitec_prog_record_verdict(model()), HITEC_PROG_REC_DONE);
    /* A new session: its backup is the servo as found, written values
     * and all; the file of the old one stays on the card. */
    CHECK(!hitec_prog_backup_differs(model()));
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(files_image("HITEC001.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 4095u);
    /* The record's RESTORE puts the old value back, and the file then
     * matches the servo. */
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_restore(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_RESTORED);
    CHECK_EQ(g_drv.dreg[0x54], 4095u);
    /* The restore saved the new session's backup first. */
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC002");
    CHECK(files_image("HITEC002.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 10u);
    load("HITEC001");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    /* The session's backup back: the written value once more. */
    CHECK(hitec_prog_backup_differs(model()));
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_BACKUP_RESTORED);
    CHECK_EQ(g_drv.dreg[0x54], 10u);
}

TEST_CASE(a_file_with_the_servos_values_replaces_an_edit_in_the_draft)
{
    hitec_image_t img;

    d_ready();
    d485(&img);
    files_put_image("SAME.TXT", &img);
    /* An edit, and a value past a soft rule confirmed. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_LEFT_END, 3300),
             HITEC_PROG_EDIT_CHECK);
    CHECK_EQ(hitec_prog_confirm(model()), HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_changes(model()), 2u);
    load("SAME");
    CHECK_EQ(msg(), HITEC_PROG_MSG_LOAD_SAME);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0x54), 4095u);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NO_CHANGES);
    CHECK_EQ(g_drv.d_writes, 0u);
    /* The confirmation went with the edit. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_LEFT_END, 3300),
             HITEC_PROG_EDIT_CHECK);
}

TEST_CASE(a_late_backup_file_saves_nothing_for_the_next_session)
{
    d_ready();
    g_files_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    prun(HITEC_PROG_FILES_MS + 100u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_CARD_WRITE);
    /* LEAVE and CONNECT before the card answers. */
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    connected();
    CHECK(hitec_prog_backup_due(model()));
    files_answer();
    prun(100u);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(hitec_prog_backup_due(model()));
    /* The next write saves the new session's backup. */
    g_files_slow = false;
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_BACKUP);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC002");
    /* A save by hand still waiting across LEAVE and CONNECT is no
     * backup file of the new session. */
    d_ready();
    g_files_slow = true;
    CHECK_EQ(hitec_prog_save(model(), "OLD"), HITEC_PROG_MSG_NONE);
    prun(20u);
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    connected();
    CHECK(hitec_prog_card_busy(model()));
    files_answer();
    CHECK_EQ(msg(), HITEC_PROG_MSG_SAVED);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(hitec_prog_backup_due(model()));
}

TEST_CASE(a_late_load_changes_nothing_in_the_next_session)
{
    d_ready();
    d_file("F.TXT", HITEC_P_D_SPEED, 50);
    g_files_slow = true;
    CHECK_EQ(hitec_prog_load(model(), "F"), HITEC_PROG_MSG_NONE);
    prun(20u);
    /* LEAVE and CONNECT before the card answers. */
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    connected();
    CHECK(hitec_prog_card_busy(model()));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    files_answer();
    prun(100u);
    CHECK(!hitec_prog_card_busy(model()));
    CHECK(!hitec_prog_load_waits(model()));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK(msg() != HITEC_PROG_MSG_LOADED);
    /* The same file asked for in this session loads. */
    g_files_slow = false;
    load("F");
    CHECK_EQ(hitec_prog_changes(model()), 1u);
}

/* ---------------------------------------------------------- factory */

TEST_CASE(factory_default_writes_a_cell_no_parameter_names)
{
    rig();
    servo_hs5645();
    /* The factory block equals the settings but for cell 0x1D. */
    for (unsigned c = 0; c < 0x24u; ++c) {
        g_drv.eeprom[0x30u + c] = g_drv.eeprom[c];
    }
    g_drv.eeprom[0x30u + 0x1Du] ^= 0x01u;
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    card_in();
    CHECK_EQ(hitec_prog_write_gate(model(), HITEC_PROG_WR_FACTORY),
             HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_factory(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_FACTORY_DONE);
    CHECK_EQ(g_drv.eeprom[0x1D], g_drv.eeprom[0x4D]);
    CHECK_EQ(hitec_prog_factory(model()), HITEC_PROG_MSG_NO_CHANGES);
}

TEST_CASE(factory_default_writes_the_classic_factory_cells)
{
    hitec_image_t img;

    c_ready();
    card_in();
    CHECK(hitec_prog_factory_image(model(), &img));
    CHECK_EQ(hitec_image_cell(&img, 0x13), 0x74u);
    CHECK_EQ(hitec_image_cell(&img, 0x14), 0x74u);
    CHECK_EQ(hitec_prog_write_enable(model(), false), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_factory(model()), HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_factory(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_FACTORY_DONE);
    CHECK_EQ(g_drv.eeprom[0x13], 0x74u);
    CHECK_EQ(g_drv.eeprom[0x14], 0x74u);
    CHECK(!hitec_prog_sum_bad(model()));
    /* Done: once more is no change. */
    CHECK_EQ(hitec_prog_factory(model()), HITEC_PROG_MSG_NO_CHANGES);
    /* The backup brings the servo's own ends back. */
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_BACKUP_RESTORED);
    CHECK_EQ(g_drv.eeprom[0x13], 0xC2u);
    CHECK_EQ(g_drv.eeprom[0x14], 0xA2u);
}

TEST_CASE(no_factory_default_for_the_d_series)
{
    hitec_image_t img;

    d_ready();
    CHECK(!hitec_prog_factory_image(model(), &img));
    CHECK(hitec_prog_factory(model()) != HITEC_PROG_MSG_NONE);
    prun(200u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    /* Nor before a read. */
    rig();
    CHECK(!hitec_prog_factory_image(model(), &img));
    CHECK(hitec_prog_factory(model()) != HITEC_PROG_MSG_NONE);
}

int main(void)
{
    RUN(the_first_write_saves_the_backup_file_first_and_once);
    RUN(without_a_card_the_backup_stays_in_ram_until_a_card_comes);
    RUN(a_card_that_refuses_the_backup_file_keeps_the_write_from_going);
    RUN(a_backup_file_not_answered_in_5000ms_ends_the_write_unsent);
    RUN(abort_while_the_backup_file_is_saved_sends_nothing);
    RUN(a_stop_while_the_backup_file_is_saved_sends_nothing);
    RUN(the_backup_file_wait_runs_across_the_clocks_wrap);
    RUN(the_session_backup_stays_for_one_servo_and_goes_with_another);
    RUN(a_save_by_hand_holds_the_servo_as_read_and_its_refusals);
    RUN(files_come_in_pages_of_5_with_their_models);
    RUN(a_request_waits_for_the_card_and_its_id_wraps);
    RUN(files_need_the_port_in_programming_mode_and_a_servo_read);
    RUN(a_list_the_card_cannot_give);
    RUN(a_load_changes_the_draft_only_with_the_switch_off_as_on);
    RUN(a_load_refuses_the_other_series_and_another_layout);
    RUN(a_hand_edited_file_is_refused_whole);
    RUN(a_file_of_another_model_or_past_a_soft_rule_waits_for_a_check);
    RUN(values_of_a_file_outside_the_editable_ones_are_counted);
    RUN(a_raw_value_comes_back_by_a_load);
    RUN(a_load_needs_a_servo_read_in_programming_mode);
    RUN(a_load_answered_after_the_servo_went_changes_nothing);
    RUN(restore_of_the_backup_writes_every_value_back_raw_included);
    RUN(after_a_lost_link_the_backup_file_brings_the_old_values_back);
    RUN(a_file_with_the_servos_values_replaces_an_edit_in_the_draft);
    RUN(a_late_backup_file_saves_nothing_for_the_next_session);
    RUN(a_late_load_changes_nothing_in_the_next_session);
    RUN(factory_default_writes_the_classic_factory_cells);
    RUN(factory_default_writes_a_cell_no_parameter_names);
    RUN(no_factory_default_for_the_d_series);
    return test_summary("hitec_prog_file");
}
