/*
 * The session's branch for the text interface
 * (protocols/escwire/ew_session_text.c), against the simulated ESC of
 * ew_sim_text.h.
 *
 * Under test: the entry through a silent first contact, its hold, a reply of
 * another firmware; revisions read-only; a read with faults on each try, a
 * `show` lacking a key or holding others; the specification's exchange byte for
 * byte; a clip, an `ERROR`, a `set` applied and answered badly, a `save` silent,
 * refused or answered with more than its `OK`, a read-back that differs, RAM
 * equal to a page while the flash is not proven; a failure and an abort at every transaction of a write; a stop before
 * and during a `save`; a driver that fails; the guard stopping a line; a leave;
 * a run across the clock's wrap.  The ESC's count of forbidden commands stays 0.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "ew_sim.h"

static sim_t esc;
static ew_session_t s;
static uint8_t g_draft[EW_PAGE_MAX];

#define BANNER "ESCape32 rev17.3 [BOARD_F051]"

static size_t slot_of(const char *key)
{
    size_t slot = 0u;

    (void)ew_key_find(key, strlen(key), &slot);
    return slot;
}

static void up(const char *banner)
{
    sim_init_e32(&esc, banner);
    const ew_driver_t d = sim_driver(&esc);

    (void)ew_session_init(&s, &d);
}

static void read_up(const char *banner)
{
    up(banner);
    (void)sim_enter(&s, &esc);
    (void)ew_session_read(&s);
    (void)sim_run(&s, &esc);
    memcpy(g_draft, s.orig, EW_MAP_LEN_E32);
}

static void edit(const char *key, int v)
{
    ew_e32_put(g_draft, slot_of(key), (int16_t)v);
}

static ew_ses_t write_draft(void)
{
    const ew_ses_t r = ew_session_write(&s, g_draft, EW_MAP_LEN_E32);

    return r == EW_SES_BUSY ? sim_run(&s, &esc) : r;
}

/* Step until transaction @p n has started, 60s at most. */
static void run_to_start(int n)
{
    for (uint32_t i = 0; i < 120000u && esc.starts < n; ++i) {
        esc.now += 500u;
        (void)ew_session_step(&s);
    }
}

/* Step until the running transaction has ended, 60s at most. */
static void run_past_transaction(void)
{
    for (uint32_t i = 0; i < 120000u && esc.running; ++i) {
        esc.now += 500u;
        (void)ew_session_step(&s);
    }
}

TEST_CASE(the_entry_through_a_silent_first_contact)
{
    up(BANNER);
    CHECK_EQ(ew_session_hold(&s), EW_SES_OK);
    CHECK_EQ(ew_session_enter(&s, 3000u), EW_SES_BUSY);
    /* The three contacts run; the hold starts when the third has ended. */
    run_to_start(3);
    run_past_transaction();
    esc.now += 500u;
    CHECK_EQ(ew_session_step(&s), EW_SES_BUSY);
    CHECK_EQ(esc.line, EW_LINE_HIGH);
    CHECK_EQ(esc.starts, 3);
    CHECK_EQ(esc.resets, 3);
    /* At the hold's end, not before. */
    const uint32_t t0 = esc.now;

    esc.now = t0 + EW_SES_TEXT_HOLD_MS * 1000u - 1000u;
    CHECK_EQ(ew_session_step(&s), EW_SES_BUSY);
    CHECK_EQ(esc.starts, 3);
    esc.now = t0 + EW_SES_TEXT_HOLD_MS * 1000u;
    CHECK_EQ(ew_session_step(&s), EW_SES_BUSY);
    CHECK_EQ(esc.starts, 4);
    CHECK(sim_sent_line(&esc, 3, "info\n"));
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.id.fw, EW_FW_E32);
    CHECK_EQ(s.id.layout, 17u);
    CHECK_EQ(s.id.len, EW_MAP_LEN_E32);
    CHECK(s.id.writable);
    CHECK_EQ(s.id.why, EW_WHY_NONE);
    CHECK_STR_EQ(s.id.name, "BOARD_F051");
    CHECK_EQ(s.info.sub, 3u);
    CHECK_STR_EQ(s.info.temp, "25C");
    CHECK_STR_EQ(s.info.temp_ext, "31C");
    CHECK_STR_EQ(s.info.volt, "16.1V");
    CHECK(s.in_mode);
    CHECK_EQ(s.contact.cpu, EW_CPU_TEXT);
    CHECK_EQ(s.guard.cpu, EW_CPU_TEXT);
    CHECK_EQ(esc.invalid, 0);
    CHECK_EQ(esc.parked_tx, 0);
    CHECK_EQ(esc.line_in_xfer, 0);
}

TEST_CASE(the_hold_for_the_text_interface_is_long_enough)
{
    /* Over the 2s the ESC needs after a reset. */
    CHECK(EW_SES_TEXT_HOLD_MS >= 2000u);
    up(BANNER);
    /* A shorter hold before the contact changes nothing. */
    (void)ew_session_hold(&s);
    (void)ew_session_enter(&s, EW_HOLD_MIN_MS);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.id.fw, EW_FW_E32);
}

TEST_CASE(an_answer_to_the_contact_sends_no_line)
{
    /* Not silent: another firmware's garbage, so no text goes to it. */
    sim_init_efm8(&esc, 0xB2u, false);
    const ew_driver_t d = sim_driver(&esc);

    (void)ew_session_init(&s, &d);
    esc.fail_byte = 0x55u;
    sim_script(&esc, F_BYTE, 0, 3, 0x55u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_CONTACT);
    CHECK_EQ(esc.starts, 3);
}

TEST_CASE(info_in_its_faults)
{
    /* 3 contacts silent, so `info` is transaction 3 on. */
    static const uint8_t k_faults[] = { F_SILENT, F_FRAMING, F_OVERFLOW,
                                        F_ABORTED, F_TEXT_CUT, F_TEXT_BYTE,
                                        F_TEXT_FLOOD, F_TEXT_ERROR };

    for (size_t i = 0; i < sizeof(k_faults); ++i) {
        up(BANNER);
        sim_script(&esc, k_faults[i], 3, 2, 0u);
        CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
        CHECK_EQ(esc.starts, 6);
        CHECK_EQ(s.id.fw, EW_FW_E32);

        up(BANNER);
        sim_script(&esc, k_faults[i], 3, 3, 0u);
        CHECK_EQ(sim_enter(&s, &esc), k_faults[i] == F_SILENT
                                          ? EW_SES_ERR_SILENT
                                          : EW_SES_ERR_CONTACT);
        CHECK_EQ(esc.starts, 6);
        CHECK(!s.in_mode);
        CHECK_EQ(s.id.fw, EW_FW_NONE);
    }
    /* A reply that is whole and is no other firmware's name. */
    up("Other 1.0");
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_CONTACT);
    CHECK_EQ(esc.starts, 6);
    CHECK(!s.in_mode);
}

TEST_CASE(a_revision_not_known_is_read_only)
{
    static const char *const k_banners[] = {
        "ESCape32 rev16.9 [B]", "ESCape32 rev18.0 [B]",
        "ESCape32 rev273.0 [B]", "ESCape32 rev17 [B]", "ESCape32 rev [B]",
    };

    for (size_t i = 0; i < sizeof(k_banners) / sizeof(k_banners[0]); ++i) {
        up(k_banners[i]);
        CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
        CHECK_EQ(s.id.fw, EW_FW_E32);
        CHECK(!s.id.writable);
        CHECK_EQ(s.id.why, EW_WHY_TEXT_REVISION);
        CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
        CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
        CHECK(!s.id.writable);
        CHECK_EQ(s.id.why, EW_WHY_TEXT_REVISION);
        CHECK_EQ(ew_e32_get(s.orig, slot_of("timing")), 16);
        memcpy(g_draft, s.orig, EW_MAP_LEN_E32);
        edit("timing", 20);
        const int at = esc.starts;

        CHECK_EQ(write_draft(), EW_SES_ERR_NOT_WRITABLE);
        CHECK_EQ(esc.starts, at);
        CHECK_EQ(esc.sets + esc.saves, 0);
    }
    /* Another revision's keys: read by key, what lacks reads as no value, and
     * the first reply is taken: there is no second try to wait for. */
    up("ESCape32 rev18.0 [B]");
    esc.show_drop = 3;
    esc.show_extra = "newkey: 5\n";
    (void)sim_enter(&s, &esc);
    const int at = esc.starts;

    (void)ew_session_read(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.starts, at + 1);
    CHECK_EQ(ew_e32_get(s.orig, 3u), EW_KEY_NONE);
    CHECK_STR_EQ(s.music, "dfa#");
}

TEST_CASE(a_read_on_its_first_and_third_try)
{
    up(BANNER);
    (void)sim_enter(&s, &esc);
    CHECK(!s.have_read);
    int at = esc.starts;

    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.starts, at + 1);
    CHECK(sim_sent_line(&esc, at, "show\n"));
    CHECK(s.have_read);
    CHECK_EQ(ew_e32_get(s.orig, slot_of("timing")), 16);
    CHECK_EQ(ew_e32_get(s.orig, slot_of("throt_max")), 2000);
    CHECK_STR_EQ(s.music, "dfa#");
    /* The third. */
    static const uint8_t k_faults[] = { F_SILENT, F_FRAMING, F_OVERFLOW,
                                        F_ABORTED, F_TEXT_CUT, F_TEXT_BYTE,
                                        F_TEXT_FLOOD, F_TEXT_ERROR };

    for (size_t i = 0; i < sizeof(k_faults); ++i) {
        at = esc.starts;
        sim_script(&esc, k_faults[i], 0, 2, 0u);
        CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
        CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
        CHECK_EQ(esc.starts, at + 3);
        /* None on 3: nothing changes in what was read. */
        at = esc.starts;
        sim_script(&esc, k_faults[i], 0, 3, 0u);
        CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
        CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_READ);
        CHECK_EQ(esc.starts, at + 3);
        CHECK(s.in_mode);
        CHECK_EQ(ew_e32_get(s.orig, slot_of("timing")), 16);
    }
}

TEST_CASE(a_show_with_a_key_missing_or_another)
{
    /* Revision 17 that keeps doing so: 3 tries, then shown and not written. */
    up(BANNER);
    esc.show_drop = 7;
    (void)sim_enter(&s, &esc);
    int at = esc.starts;

    (void)ew_session_read(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.starts, at + 3);
    CHECK(!s.id.writable);
    CHECK_EQ(s.id.why, EW_WHY_SHOW_KEYS);
    CHECK_EQ(ew_e32_get(s.orig, 7u), EW_KEY_NONE);
    memcpy(g_draft, s.orig, EW_MAP_LEN_E32);
    edit("timing", 20);
    CHECK_EQ(write_draft(), EW_SES_ERR_NOT_WRITABLE);
    /* A clean read gives the write back. */
    esc.show_drop = -1;
    (void)ew_session_read(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(s.id.writable);
    CHECK_EQ(s.id.why, EW_WHY_NONE);
    /* A key on top: the same. */
    up(BANNER);
    esc.show_extra = "newkey: 5\n";
    (void)sim_enter(&s, &esc);
    (void)ew_session_read(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(!s.id.writable);
    /* Once, then whole. */
    up(BANNER);
    (void)sim_enter(&s, &esc);
    esc.show_drop = 7;
    at = esc.starts;
    (void)ew_session_read(&s);
    esc.show_drop = -1;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.starts, at + 2);
    CHECK(s.id.writable);
}

TEST_CASE(the_write_byte_for_byte)
{
    read_up(BANNER);
    edit("timing", 20);
    edit("telem_volt", -80);
    edit("led", 15);
    const int at = esc.starts;

    CHECK_EQ(ew_session_write(&s, g_draft, EW_MAP_LEN_E32), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(sim_sent_line(&esc, at + 0, "info\n"));
    CHECK(sim_sent_line(&esc, at + 1, "show\n"));
    CHECK(sim_sent_line(&esc, at + 2, "set timing 20\n"));
    CHECK(sim_sent_line(&esc, at + 3, "set telem_volt -80\n"));
    CHECK(sim_sent_line(&esc, at + 4, "set led 15\n"));
    CHECK(sim_sent_line(&esc, at + 5, "save\n"));
    CHECK(sim_sent_line(&esc, at + 6, "show\n"));
    CHECK_EQ(esc.starts, at + 7);
    CHECK_EQ(esc.saves, 1);
    CHECK_EQ(esc.saved[slot_of("timing")], 20);
    CHECK_EQ(esc.saved[slot_of("telem_volt")], -80);
    CHECK_EQ(esc.saved[slot_of("led")], 15);
    CHECK(!s.dirty);
    CHECK(!s.writing);
    CHECK(!s.save_open);
    CHECK(s.guard.approved == NULL);
    CHECK_EQ(memcmp(s.orig, g_draft, EW_MAP_LEN_E32), 0);
    CHECK_EQ(esc.forbidden, 0);
    CHECK_EQ(esc.invalid, 0);
    /* A draft equal to the page: no line. */
    CHECK_EQ(write_draft(), EW_SES_OK);
    CHECK_EQ(esc.starts, at + 7);
}

TEST_CASE(the_refusals_before_a_line_goes)
{
    up(BANNER);
    CHECK_EQ(ew_session_write(&s, g_draft, EW_MAP_LEN_E32), EW_SES_ERR_STATE);
    (void)sim_enter(&s, &esc);
    /* No read yet. */
    CHECK_EQ(ew_session_write(&s, g_draft, EW_MAP_LEN_E32), EW_SES_ERR_NO_READ);
    (void)ew_session_read(&s);
    (void)sim_run(&s, &esc);
    memcpy(g_draft, s.orig, EW_MAP_LEN_E32);
    const int at = esc.starts;

    CHECK_EQ(ew_session_write(&s, g_draft, EW_MAP_LEN_E32 - 1u), EW_SES_ERR_ARG);
    edit("timing", 32);
    CHECK_EQ(write_draft(), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_VALUE);
    CHECK_EQ(s.bad_off, slot_of("timing") * 2u);
    edit("timing", 16);
    edit("freq_max", 23);
    CHECK_EQ(write_draft(), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.bad_off, slot_of("freq_max") * 2u);
    CHECK_EQ(esc.starts, at);
    /* A protocol A action is refused too: a restore needs an erased AM32. */
    ew_backup_id_t id;

    ew_session_backup_id(&s, &id);
    CHECK_EQ(ew_session_restore(&s, &id, g_draft, EW_MAP_LEN_E32),
             EW_SES_ERR_STATE);
}

TEST_CASE(a_page_that_moved_or_a_revision_that_did)
{
    read_up(BANNER);
    edit("timing", 20);
    esc.ram[slot_of("led")] = 3;
    CHECK_EQ(write_draft(), EW_SES_ERR_CHANGED);
    CHECK_EQ(esc.sets + esc.saves, 0);
    CHECK(!s.dirty);
    CHECK(s.guard.approved == NULL);

    read_up(BANNER);
    edit("timing", 20);
    (void)snprintf(esc.banner, sizeof(esc.banner), "ESCape32 rev18.0 [B]");
    CHECK_EQ(write_draft(), EW_SES_ERR_CHANGED);
    CHECK_EQ(esc.sets + esc.saves, 0);

    read_up(BANNER);
    edit("timing", 20);
    (void)snprintf(esc.banner, sizeof(esc.banner), "Other");
    CHECK_EQ(write_draft(), EW_SES_ERR_READ);
    CHECK_EQ(esc.sets + esc.saves, 0);
    /* The first look fails: nothing moved in the ESC. */
    read_up(BANNER);
    edit("timing", 20);
    sim_script(&esc, F_SILENT, 1, 3, 0u);
    CHECK_EQ(write_draft(), EW_SES_ERR_READ);
    CHECK_EQ(esc.sets + esc.saves, 0);
    CHECK(s.guard.approved == NULL);
    /* The look before the write lacks a key each time. */
    read_up(BANNER);
    edit("timing", 20);
    esc.show_drop = 7;
    CHECK_EQ(write_draft(), EW_SES_ERR_READ);
    CHECK_EQ(esc.sets + esc.saves, 0);
}

TEST_CASE(a_clip_ends_with_no_save)
{
    read_up(BANNER);
    edit("timing", 20);
    edit("bec", 4);
    edit("led", 15);
    esc.clip_slot = (int)slot_of("bec");
    esc.clip_to = 1;
    CHECK_EQ(write_draft(), EW_SES_ERR_CLIPPED);
    CHECK_EQ(s.clipped, 1);
    CHECK_EQ(s.bad_off, slot_of("bec") * 2u);
    CHECK_EQ(esc.saves, 0);
    /* The key after it did not go. */
    CHECK_EQ(esc.ram[slot_of("led")], 0);
    /* The flash is as read, RAM is not, and the page says so. */
    CHECK_EQ(esc.saved[slot_of("timing")], 16);
    CHECK(s.dirty);
    CHECK(!s.save_open);
    CHECK(s.guard.approved == NULL);
    /* A read comes first; it keeps the page read. */
    CHECK_EQ(write_draft(), EW_SES_ERR_MUST_READ);
    (void)ew_session_read(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_e32_get(s.orig, slot_of("timing")), 16);
    CHECK_EQ(ew_e32_get(s.page, slot_of("timing")), 20);
    /* Then only the two pages go: another draft does not. */
    uint8_t other[EW_MAP_LEN_E32];

    memcpy(other, g_draft, sizeof(other));
    ew_e32_put(other, slot_of("volume"), 7);
    CHECK_EQ(ew_session_write(&s, other, sizeof(other)), EW_SES_ERR_DIRTY);
    /* Back to the page read: RAM and flash agree again. */
    memcpy(other, s.orig, sizeof(other));
    CHECK_EQ(ew_session_write(&s, other, sizeof(other)), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(!s.dirty);
    CHECK_EQ(esc.ram[slot_of("timing")], 16);
    CHECK_EQ(esc.ram[slot_of("bec")], 0);
    /* A leave stays open through all of it: the flash was never in doubt. */
    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
}

TEST_CASE(a_leave_after_a_clip_keeps_the_flash)
{
    read_up(BANNER);
    edit("bec", 4);
    esc.clip_slot = (int)slot_of("bec");
    esc.clip_to = 1;
    CHECK_EQ(write_draft(), EW_SES_ERR_CLIPPED);
    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK_EQ(esc.saves, 0);
    CHECK_EQ(esc.ram[slot_of("bec")], 0);
}

TEST_CASE(an_error_to_a_set_or_a_save)
{
    /* `info`, `show`, then the first `set`. */
    read_up(BANNER);
    edit("timing", 20);
    edit("led", 15);
    sim_script(&esc, F_TEXT_ERROR, 3, 1, 0u);
    CHECK_EQ(write_draft(), EW_SES_ERR_ESC_ERROR);
    CHECK_EQ(s.bad_off, slot_of("led") * 2u);
    CHECK_EQ(esc.saves, 0);
    CHECK_EQ(s.last_ans, EW_ANS_TEXT_ERROR);
    CHECK(s.dirty);
    CHECK(s.guard.approved == NULL);
    /* The `save`: the flash stays, and there is no second one. */
    read_up(BANNER);
    edit("timing", 20);
    int at = esc.starts;

    sim_script(&esc, F_TEXT_ERROR, 3, 1, 0u);
    CHECK_EQ(write_draft(), EW_SES_ERR_ESC_ERROR);
    CHECK_EQ(esc.starts, at + 4);
    CHECK_EQ(esc.saves, 0);
    CHECK_EQ(esc.saved[slot_of("timing")], 16);
    CHECK(!s.save_open);
    CHECK(!s.writing);
    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
}

TEST_CASE(a_set_on_its_third_try_and_none)
{
    static const uint8_t k_faults[] = { F_SILENT, F_HEARD_SILENT, F_FRAMING,
                                        F_TEXT_CUT, F_TEXT_BYTE };

    for (size_t i = 0; i < sizeof(k_faults); ++i) {
        read_up(BANNER);
        edit("timing", 20);
        sim_script(&esc, k_faults[i], 2, 2, 0u);
        CHECK_EQ(write_draft(), EW_SES_OK);
        CHECK_EQ(esc.saved[slot_of("timing")], 20);
        CHECK(!s.dirty);
    }
    /* None on 3 and a read that says RAM is as read: the procedure runs again. */
    read_up(BANNER);
    edit("timing", 20);
    const int at = esc.starts;

    sim_script(&esc, F_SILENT, 2, 3, 0u);
    CHECK_EQ(write_draft(), EW_SES_OK);
    CHECK_EQ(esc.saves, 1);
    /* info, show, 3 of set, show, set, save, show. */
    CHECK_EQ(esc.starts, at + 9);
    /* Unheard on all 3 procedures: the flash and RAM are as read. */
    read_up(BANNER);
    edit("timing", 20);
    esc.mute_word = "set";
    CHECK_EQ(write_draft(), EW_SES_ERR_WRITE_FAILED);
    CHECK_EQ(s.attempts, EW_SES_ATTEMPTS);
    CHECK_EQ(esc.saves, 0);
    CHECK(!s.dirty);
    CHECK(s.guard.approved == NULL);
    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
}

TEST_CASE(a_set_heard_with_its_replies_lost_still_gets_its_save)
{
    /* RAM reaches the draft with no reply: only a `save` puts it in flash. */
    read_up(BANNER);
    edit("timing", 20);
    const int at = esc.starts;

    sim_script(&esc, F_HEARD_SILENT, 2, 3, 0u);
    CHECK_EQ(write_draft(), EW_SES_OK);
    CHECK_EQ(esc.saves, 1);
    CHECK_EQ(esc.saved[slot_of("timing")], 20);
    /* info, show, 3 of set, show, save, show. */
    CHECK_EQ(esc.starts, at + 8);
    CHECK(sim_sent_line(&esc, at + 6, "save\n"));
    CHECK(!s.dirty);
}

TEST_CASE(a_set_applied_and_answered_badly)
{
    /* Each reply wrong in its own way, the ESC acting on all 3. */
    static const char *const k_replies[] = {
        "xx\nOK\n", "timing: 20\n", "led: 20\nOK\n", "timing: x\nOK\n",
        "timing: 20\ndamp: 1\nOK\n", "OK\n",
    };

    for (size_t i = 0; i < sizeof(k_replies) / sizeof(k_replies[0]); ++i) {
        read_up(BANNER);
        edit("timing", 20);
        const int at = esc.starts;

        esc.swap_reply = k_replies[i];
        sim_script(&esc, F_TEXT_HEARD, 2, 3, 0u);
        CHECK_EQ(write_draft(), EW_SES_OK);
        CHECK_EQ(esc.sets, 3);
        CHECK_EQ(esc.saves, 1);
        CHECK_EQ(esc.saved[slot_of("timing")], 20);
        /* info, show, 3 of set, show, save, show. */
        CHECK_EQ(esc.starts, at + 8);
    }
}

TEST_CASE(a_save_answered_with_more_than_its_ok)
{
    /* A line before the OK: not this `save`'s reply, and the flash not moved. */
    read_up(BANNER);
    edit("timing", 20);
    const int at = esc.starts;

    esc.save_plan = "U";
    CHECK_EQ(write_draft(), EW_SES_OK);
    CHECK_EQ(esc.saves, 1);
    CHECK_EQ(esc.saved[slot_of("timing")], 20);
    /* info, show, set, save, show, save, show. */
    CHECK_EQ(esc.starts, at + 7);
    CHECK(sim_sent_line(&esc, at + 5, "save\n"));
    /* On every `save`: never taken as written. */
    read_up(BANNER);
    edit("timing", 20);
    esc.save_plan = "UUU";
    CHECK_EQ(write_draft(), EW_SES_ERR_TORN);
    CHECK_EQ(esc.saves, 0);
    CHECK(s.save_open);
}

TEST_CASE(ram_as_read_after_a_save_proves_nothing_about_the_flash)
{
    /* Each `save` stores 15, then RAM drifts back to the 16 read. */
    read_up(BANNER);
    edit("timing", 15);
    esc.drift_slot = (int)slot_of("timing");
    CHECK_EQ(write_draft(), EW_SES_ERR_TORN);
    CHECK_EQ(esc.saved[slot_of("timing")], 15);
    CHECK_EQ(esc.ram[slot_of("timing")], 16);
    CHECK(s.dirty);
    CHECK(s.save_open);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_DIRTY);
    /* The page read goes through a `save`, and that repairs the flash. */
    esc.drift_slot = -1;
    uint8_t back[EW_MAP_LEN_E32];

    memcpy(back, s.orig, sizeof(back));
    const int saves = esc.saves;

    CHECK_EQ(ew_session_write(&s, back, sizeof(back)), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.saves, saves + 1);
    CHECK_EQ(esc.saved[slot_of("timing")], 16);
    CHECK(!s.dirty);
    CHECK(!s.save_open);
    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
}

TEST_CASE(an_error_keeps_what_an_earlier_save_left_open)
{
    /* The first `save` reply lost, the second answered ERROR. */
    read_up(BANNER);
    edit("timing", 20);
    esc.save_plan = "LE";
    CHECK_EQ(write_draft(), EW_SES_ERR_ESC_ERROR);
    CHECK_EQ(esc.saves, 1);
    CHECK(s.save_open);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_DIRTY);
    /* A later write's ERROR keeps it too: only a proof clears it. */
    (void)ew_session_read(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    esc.save_plan = "E";
    CHECK_EQ(write_draft(), EW_SES_ERR_ESC_ERROR);
    CHECK(s.save_open);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_DIRTY);
    (void)ew_session_read(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(write_draft(), EW_SES_OK);
    CHECK(!s.save_open);
    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
}

TEST_CASE(any_answer_to_a_contact_sends_no_line)
{
    /* Bytes on the first try and silence on the other two. */
    up(BANNER);
    sim_script(&esc, F_BYTE, 0, 1, 0x55u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_CONTACT);
    CHECK_EQ(esc.starts, 3);
    /* On the second. */
    up(BANNER);
    sim_script(&esc, F_BYTE, 1, 1, 0x55u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_CONTACT);
    CHECK_EQ(esc.starts, 3);
}

TEST_CASE(a_rejected_show_leaves_the_music)
{
    read_up(BANNER);
    CHECK_STR_EQ(s.music, "dfa#");
    /* A new tune in a reply that lacks a key, then silence. */
    (void)snprintf(esc.music, sizeof(esc.music), "abc");
    esc.show_drop = 7;
    sim_script(&esc, F_SILENT, 1, 2, 0u);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_READ);
    CHECK_STR_EQ(s.music, "dfa#");
    /* The reply taken on the third try brings its tune along. */
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(!s.id.writable);
    CHECK_STR_EQ(s.music, "abc");
}

TEST_CASE(a_set_heard_with_its_reply_lost_for_good)
{
    /* RAM holds the new value and the ESC goes silent: the flash is as read. */
    read_up(BANNER);
    edit("timing", 20);
    sim_script(&esc, F_HEARD_SILENT, 2, 60, 0u);
    CHECK_EQ(write_draft(), EW_SES_ERR_WRITE_FAILED);
    CHECK_EQ(esc.saved[slot_of("timing")], 16);
    CHECK_EQ(esc.saves, 0);
    CHECK(s.dirty);
    CHECK(s.guard.approved == NULL);
    /* A read comes first. */
    CHECK_EQ(write_draft(), EW_SES_ERR_MUST_READ);
    /* A leave goes: the flash was never touched. */
    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
}

TEST_CASE(a_set_answered_for_another_key)
{
    read_up(BANNER);
    edit("timing", 20);
    const int at = esc.starts;

    (void)ew_session_write(&s, g_draft, EW_MAP_LEN_E32);
    /* Run up to the `set`, swap its reply, and let it go. */
    run_to_start(at + 3);
    memcpy(esc.reply, "led: 20\nOK\n", 11u);
    esc.n_rx = 11u;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    /* info, show, set with the wrong key, set, save, show. */
    CHECK_EQ(esc.starts, at + 6);
    CHECK_EQ(esc.saved[slot_of("timing")], 20);
}

TEST_CASE(a_save_with_no_answer)
{
    /* Heard, its OK lost: the read says RAM fits and the `save` goes again. */
    read_up(BANNER);
    edit("timing", 20);
    int at = esc.starts;

    sim_script(&esc, F_HEARD_SILENT, 3, 1, 0u);
    CHECK_EQ(write_draft(), EW_SES_OK);
    CHECK_EQ(esc.saves, 2);
    /* info, show, set, save, show, save, show. */
    CHECK_EQ(esc.starts, at + 7);
    CHECK(sim_sent_line(&esc, at + 5, "save\n"));
    CHECK(!s.dirty);
    /* Never answered: the flash stays in doubt, and no leave goes. */
    read_up(BANNER);
    edit("timing", 20);
    esc.mute_word = "save";
    esc.mute_heard = true;
    CHECK_EQ(write_draft(), EW_SES_ERR_TORN);
    CHECK_EQ(s.attempts, EW_SES_ATTEMPTS);
    CHECK_EQ(esc.saves, EW_SES_ATTEMPTS);
    CHECK(s.save_open);
    CHECK(s.dirty);
    CHECK(s.reread);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_DIRTY);
    /* The page's draft goes again, and clears it. */
    esc.mute_word = NULL;
    CHECK_EQ(write_draft(), EW_SES_OK);
    CHECK(!s.save_open);
    CHECK(!s.dirty);
    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
}

TEST_CASE(a_save_and_then_silence)
{
    /* No answer to the `save` and none after: not known. */
    read_up(BANNER);
    edit("timing", 20);
    sim_script(&esc, F_DEAD, 3, 1, 0u);
    CHECK_EQ(write_draft(), EW_SES_ERR_LOST);
    CHECK(s.save_open);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_DIRTY);
    /* Its OK came and the read-back did not: not proven, so no leave. */
    read_up(BANNER);
    edit("timing", 20);
    sim_script(&esc, F_DEAD, 4, 1, 0u);
    CHECK_EQ(write_draft(), EW_SES_ERR_LOST);
    CHECK(s.save_open);
    CHECK_EQ(esc.saved[slot_of("timing")], 20);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_DIRTY);
}

TEST_CASE(a_read_back_that_differs)
{
    /* Once: the procedure runs again and repairs RAM. */
    read_up(BANNER);
    edit("timing", 20);
    int at = esc.starts;

    sim_script(&esc, F_CORRUPT, 3, 1, (uint8_t)slot_of("led"));
    CHECK_EQ(write_draft(), EW_SES_OK);
    CHECK_EQ(esc.saves, 2);
    /* info, show, set, save, show, set led, save, show. */
    CHECK_EQ(esc.starts, at + 8);
    CHECK(sim_sent_line(&esc, at + 5, "set led 0\n"));
    CHECK_EQ(esc.saved[slot_of("led")], 0);
    /* On every procedure: neither page. */
    read_up(BANNER);
    edit("timing", 20);
    esc.drift_slot = (int)slot_of("led");
    CHECK_EQ(write_draft(), EW_SES_ERR_TORN);
    CHECK_EQ(s.attempts, EW_SES_ATTEMPTS);
    CHECK(s.dirty);
    CHECK(s.reread);
    CHECK(s.guard.approved != NULL);
}

TEST_CASE(a_failure_and_an_abort_at_every_transaction)
{
    /* info, show, 2 of set, save, show. */
    for (int at = 0; at < 6; ++at) {
        /* The ESC goes at each transaction: the write ends with a result, and
         * never with a line a settings host must not send. */
        read_up(BANNER);
        edit("timing", 20);
        edit("led", 15);
        sim_script(&esc, F_DEAD, at, 1, 0u);
        const ew_ses_t r = write_draft();

        CHECK(r == EW_SES_ERR_READ || r == EW_SES_ERR_WRITE_FAILED
              || r == EW_SES_ERR_LOST);
        CHECK_EQ(esc.forbidden, 0);
        CHECK_EQ(esc.invalid, 0);
        CHECK(!s.writing);

        /* An abort. */
        read_up(BANNER);
        edit("timing", 20);
        edit("led", 15);
        const int from = esc.starts;

        (void)ew_session_write(&s, g_draft, EW_MAP_LEN_E32);
        run_to_start(from + at + 1);
        ew_session_abort(&s);
        CHECK_EQ(esc.line, EW_LINE_PARK);
        CHECK(!s.in_mode);
        CHECK(!s.writing);
        const int n = esc.starts;

        CHECK_EQ(ew_session_write(&s, g_draft, EW_MAP_LEN_E32), EW_SES_ERR_STATE);
        CHECK_EQ(ew_session_read(&s), EW_SES_ERR_STATE);
        CHECK_EQ(ew_session_step(&s), s.result);
        CHECK_EQ(esc.starts, n);
    }
}

TEST_CASE(a_stop_before_and_during_a_save)
{
    /* Before the `save`: at once, and the flash stays. */
    read_up(BANNER);
    edit("timing", 20);
    int at = esc.starts;

    (void)ew_session_write(&s, g_draft, EW_MAP_LEN_E32);
    run_to_start(at + 3);
    CHECK(!ew_session_writing(&s));
    ew_session_stop(&s);
    CHECK_EQ(s.result, EW_SES_ERR_STOPPED);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK_EQ(esc.saves, 0);
    /* From the `save` on: to its read-back, then parked. */
    read_up(BANNER);
    edit("timing", 20);
    at = esc.starts;
    (void)ew_session_write(&s, g_draft, EW_MAP_LEN_E32);
    run_to_start(at + 4);
    CHECK(ew_session_writing(&s));
    CHECK(s.save_open);
    CHECK_EQ(s.x.first_us, EW_TEXT_SAVE_US);
    ew_session_stop(&s);
    CHECK(esc.line != EW_LINE_PARK);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.starts, at + 5);
    CHECK(s.stopped);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK(!ew_session_writing(&s));
    CHECK_EQ(esc.saved[slot_of("timing")], 20);
    /* A stop with no procedure left: a `save` with no answer starts no other. */
    read_up(BANNER);
    edit("timing", 20);
    at = esc.starts;
    sim_script(&esc, F_HEARD_SILENT, 3, 1, 0u);
    (void)ew_session_write(&s, g_draft, EW_MAP_LEN_E32);
    run_to_start(at + 4);
    ew_session_stop(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_TORN);
    CHECK_EQ(esc.saves, 1);
    CHECK_EQ(esc.line, EW_LINE_PARK);
}

TEST_CASE(a_stop_or_abort_in_the_entrys_hold)
{
    up(BANNER);
    (void)ew_session_hold(&s);
    (void)ew_session_enter(&s, EW_HOLD_MIN_MS);
    run_to_start(3);
    run_past_transaction();
    esc.now += 500u;
    (void)ew_session_step(&s);
    ew_session_abort(&s);
    CHECK_EQ(s.result, EW_SES_ERR_ABORTED);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    const int n = esc.starts;

    esc.now += 5000000u;
    CHECK_EQ(ew_session_step(&s), EW_SES_ERR_ABORTED);
    CHECK_EQ(esc.starts, n);
}

TEST_CASE(a_driver_that_refuses_or_does_not_end)
{
    read_up(BANNER);
    edit("timing", 20);
    sim_script(&esc, F_START_FALSE, 0, 1, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, EW_MAP_LEN_E32), EW_SES_ERR_DRIVER);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK(!s.in_mode);

    read_up(BANNER);
    sim_script(&esc, F_STUCK, 0, 1, 0u);
    (void)ew_session_read(&s);
    const uint32_t longest = s.wait_us;

    /* 1536 bytes, 20ms each at most, plus the first wait: bounded. */
    CHECK(longest < 32000000u);
    CHECK(longest > 30000000u);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_DRIVER);
    CHECK_EQ(esc.aborts, 1);
    CHECK_EQ(esc.line, EW_LINE_PARK);
}

TEST_CASE(what_the_guard_refuses_reaches_no_driver)
{
    /* A `save` with the judged page taken away between two steps. */
    read_up(BANNER);
    edit("timing", 20);
    const int at = esc.starts;

    (void)ew_session_write(&s, g_draft, EW_MAP_LEN_E32);
    run_to_start(at + 3);
    ew_guard_done(&s.guard);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_NOT_APPROVED);
    CHECK_EQ(esc.starts, at + 3);
    CHECK_EQ(esc.saves, 0);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    /* A line of a guard that holds an ESC of protocol A. */
    read_up(BANNER);
    s.guard.cpu = EW_CPU_ARM;
    CHECK_EQ(ew_session_read(&s), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_INTERFACE);
}

TEST_CASE(a_leave_is_a_power_cycle)
{
    read_up(BANNER);
    const int at = esc.starts;

    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
    CHECK_EQ(ew_session_step(&s), EW_SES_OK);
    /* No line and no frame: the line parked at once. */
    CHECK_EQ(esc.starts, at);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK(!s.in_mode);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_STATE);
    /* A read-only ESC leaves the same way. */
    up("ESCape32 rev18.0 [B]");
    (void)sim_enter(&s, &esc);
    CHECK_EQ(ew_session_leave(&s), EW_SES_OK);
    CHECK_EQ(esc.line, EW_LINE_PARK);
}

TEST_CASE(a_whole_session_across_the_wrap)
{
    up(BANNER);
    esc.now = 0xFFFFFF00u - 3000000u;
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    (void)ew_session_read(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    memcpy(g_draft, s.orig, EW_MAP_LEN_E32);
    edit("freq_min", 16);
    edit("freq_max", 20);
    edit("telem_curr", -100);
    CHECK_EQ(write_draft(), EW_SES_OK);
    CHECK_EQ(esc.saved[slot_of("freq_max")], 20);
    CHECK_EQ(esc.saved[slot_of("telem_curr")], -100);
    CHECK_EQ(esc.forbidden, 0);
    CHECK_EQ(esc.invalid, 0);
    CHECK_EQ(esc.parked_tx, 0);
    CHECK_EQ(esc.line_in_xfer, 0);
}

TEST_CASE(the_longest_show_is_read_whole)
{
    up(BANNER);
    memset(esc.music, 'a', EW_E32_MUSIC_MAX);
    esc.music[EW_E32_MUSIC_MAX] = '\0';
    (void)sim_enter(&s, &esc);
    (void)ew_session_read(&s);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(s.id.writable);
    CHECK_EQ(strlen(s.music), EW_E32_MUSIC_MAX);
}

int main(void)
{
    RUN(the_entry_through_a_silent_first_contact);
    RUN(the_hold_for_the_text_interface_is_long_enough);
    RUN(an_answer_to_the_contact_sends_no_line);
    RUN(info_in_its_faults);
    RUN(a_revision_not_known_is_read_only);
    RUN(a_read_on_its_first_and_third_try);
    RUN(a_show_with_a_key_missing_or_another);
    RUN(the_write_byte_for_byte);
    RUN(the_refusals_before_a_line_goes);
    RUN(a_page_that_moved_or_a_revision_that_did);
    RUN(a_clip_ends_with_no_save);
    RUN(a_leave_after_a_clip_keeps_the_flash);
    RUN(an_error_to_a_set_or_a_save);
    RUN(a_set_on_its_third_try_and_none);
    RUN(a_set_heard_with_its_replies_lost_still_gets_its_save);
    RUN(a_set_applied_and_answered_badly);
    RUN(a_save_answered_with_more_than_its_ok);
    RUN(ram_as_read_after_a_save_proves_nothing_about_the_flash);
    RUN(an_error_keeps_what_an_earlier_save_left_open);
    RUN(any_answer_to_a_contact_sends_no_line);
    RUN(a_rejected_show_leaves_the_music);
    RUN(a_set_heard_with_its_reply_lost_for_good);
    RUN(a_set_answered_for_another_key);
    RUN(a_save_with_no_answer);
    RUN(a_save_and_then_silence);
    RUN(a_read_back_that_differs);
    RUN(a_failure_and_an_abort_at_every_transaction);
    RUN(a_stop_before_and_during_a_save);
    RUN(a_stop_or_abort_in_the_entrys_hold);
    RUN(a_driver_that_refuses_or_does_not_end);
    RUN(what_the_guard_refuses_reaches_no_driver);
    RUN(a_leave_is_a_power_cycle);
    RUN(a_whole_session_across_the_wrap);
    RUN(the_longest_show_is_read_whole);
    return test_summary("ew_session_text");
}
