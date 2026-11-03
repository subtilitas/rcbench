/*
 * The ESC_WIRE page's reads and writes of an ESC, shared/outputs/esc_port.c: the
 * blocks of ESC_DATA, the read copy's name on a write, the key, the draft's CRC,
 * what a session refuses, the write hold on the supply, a restore, the text line.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_port_rig.h"

static uint8_t g_page[EW_PAGE_MAX];

TEST_CASE(a_read_and_a_write_of_am32)
{
    uint8_t b[LINK_ED_BLOCK_BYTES];

    up_am_rig();
    ready_on(2u);
    CHECK_EQ(reg(LINK_EW_READS), 0u);
    read_now();
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    CHECK_EQ(reg(LINK_EW_READS), 1u);
    CHECK(flag(LINK_EW_F_HAVE_READ));
    CHECK_EQ(reg(LINK_EW_READ_CRC), link_crc(LINK_CRC_INIT, esc.am, EW_MAP_LEN_AM));
    /* Every block of the read copy, and of the last read. */
    for (unsigned k = 0u; k < 3u; ++k) {
        block_get(LINK_ED_READ + k, b);
        CHECK(memcmp(b, esc.am + k * LINK_ED_BLOCK_BYTES, sizeof(b)) == 0);
        block_get(LINK_ED_PAGE + k, b);
        CHECK(memcmp(b, esc.am + k * LINK_ED_BLOCK_BYTES, sizeof(b)) == 0);
    }
    /* A draft that changes one byte goes to the ESC. */
    memcpy(g_page, esc.am, EW_MAP_LEN_AM);
    g_page[0x18] = 48u;
    CHECK_EQ(write_page(g_page), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NONE);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_RUN);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_WRITE);
    /* And no block changes while it runs. */
    const uint16_t which = LINK_ED_DRAFT;
    uint16_t w = 0u;

    (void)esc_port_write(&g_p, LINK_EW_W_BLOCK, 1u, &which, 0u);
    CHECK_EQ(esc_port_data_write(&g_p, 0u, 1u, &w), LINK_NACK_BUSY);
    run_while(LINK_ESC_RUN, 20000u);
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    CHECK_EQ(esc.am[0x18], 48u);
    CHECK_EQ(esc.programs, 1);
    /* The write's read-back is the read copy: its name moved, and a draft of the
     * page before it is stale. */
    CHECK_EQ(reg(LINK_EW_READS), 2u);
    CHECK_EQ(reg(LINK_EW_READ_CRC), link_crc(LINK_CRC_INIT, esc.am, EW_MAP_LEN_AM));
    (void)stage();
    CHECK_EQ(cmd_full(LINK_ESC_OP_WRITE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_STALE);
    /* A draft equal to the page: no frame, and the name stays. */
    read_now();
    const uint16_t reads = reg(LINK_EW_READS);
    const int starts = esc.starts;

    memcpy(g_page, esc.am, EW_MAP_LEN_AM);
    CHECK_EQ(write_page(g_page), 0u);
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    CHECK_EQ(esc.starts, starts);
    CHECK_EQ(reg(LINK_EW_READS), reads);
}

TEST_CASE(a_write_refused_before_any_frame)
{
    up_am_rig();
    ready_on(2u);
    read_now();
    memcpy(g_page, esc.am, EW_MAP_LEN_AM);
    g_page[0x18] = 48u;
    (void)write_page(g_page);
    (void)cmd(LINK_ESC_OP_ABORT);
    /* Back up, read. */
    up_am_rig();
    ready_on(2u);
    /* No read yet: the session says so, and nothing goes off. */
    memcpy(g_page, esc.am, EW_MAP_LEN_AM);
    g_page[0x18] = 48u;
    (void)write_page(g_page);
    CHECK_EQ(refusal(), LINK_ESC_REF_SESSION);
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_NO_READ);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    CHECK(g.v.asked_on);
    CHECK(!flag(LINK_EW_F_FAULT));
    read_now();
    const int starts = esc.starts;

    /* No key. */
    CHECK_EQ(cmd_full(LINK_ESC_OP_WRITE, 0u, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NO_KEY);
    /* The draft's CRC, the staged CRC, the read copy's counter and CRC. */
    (void)write_page(g_page);
    (void)cmd(LINK_ESC_OP_ABORT);
    up_am_rig();
    ready_on(2u);
    read_now();
    draft_put(g_page, EW_MAP_LEN_AM);
    ST_REG(LINK_EW_W_READS) = reg(LINK_EW_READS);
    ST_REG(LINK_EW_W_READ_CRC) = reg(LINK_EW_READ_CRC);
    ST_REG(LINK_EW_W_DRAFT_CRC) = (uint16_t)(link_crc(LINK_CRC_INIT, g_page, EW_MAP_LEN_AM) ^ 1u);
    (void)stage();
    CHECK_EQ(cmd_full(LINK_ESC_OP_WRITE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_DRAFT);
    ST_REG(LINK_EW_W_DRAFT_CRC) = link_crc(LINK_CRC_INIT, g_page, EW_MAP_LEN_AM);
    (void)stage();
    CHECK_EQ(cmd_full(LINK_ESC_OP_WRITE, LINK_ESC_WRITE_KEY, true), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_STAGED);
    ST_REG(LINK_EW_W_READS) = (uint16_t)(reg(LINK_EW_READS) + 1u);
    (void)stage();
    CHECK_EQ(cmd_full(LINK_ESC_OP_WRITE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_STALE);
    ST_REG(LINK_EW_W_READS) = reg(LINK_EW_READS);
    ST_REG(LINK_EW_W_READ_CRC) = (uint16_t)(reg(LINK_EW_READ_CRC) ^ 1u);
    (void)stage();
    CHECK_EQ(cmd_full(LINK_ESC_OP_WRITE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_STALE);
    /* Unsafe, and a second one while one runs. */
    ST_REG(LINK_EW_W_READ_CRC) = reg(LINK_EW_READ_CRC);
    (void)stage();
    g_p.safe = false;
    CHECK_EQ(cmd_full(LINK_ESC_OP_WRITE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_UNSAFE);
    pass();
    CHECK(esc.starts >= starts);
    (void)cmd(LINK_ESC_OP_READ);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_RUN);
    (void)cmd_full(LINK_ESC_OP_READ, LINK_ESC_WRITE_KEY, false);
    CHECK_EQ(refusal(), LINK_ESC_REF_BUSY);
    /* A write on an open page with a stop standing. */
    (void)cmd(LINK_ESC_OP_STOP);
    (void)cmd_full(LINK_ESC_OP_READ, 0u, true);
    CHECK_EQ(refusal(), LINK_ESC_REF_STOPPED);
}

TEST_CASE(a_draft_the_guard_refuses_is_a_fault)
{
    up_am_rig();
    ready_on(2u);
    read_now();
    memcpy(g_page, esc.am, EW_MAP_LEN_AM);
    /* A byte no field of the map marks editable. */
    g_page[1] = (uint8_t)(g_page[1] + 1u);
    CHECK_EQ(write_page(g_page), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_SESSION);
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_REFUSED);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK_EQ(esc.programs, 0);
}

TEST_CASE(the_supply_holds_for_a_write_and_its_tail)
{
    up_s_rig();
    ready_on(2u);
    read_now();
    memcpy(g_page, g_p.ses.orig, sizeof(g_page));
    g_page[0x15] = 2u;
    CHECK(!esc_port_writing(&g_p));
    CHECK_EQ(write_page(g_page), 0u);
    /* The re-read writes nothing; the erase does. */
    while (esc.erases == 0) {
        pass();
    }
    CHECK(esc_port_writing(&g_p));
    CHECK(flag(LINK_EW_F_WRITING));
    (void)cmd(LINK_ESC_OP_CLOSE);
    CHECK_EQ(refusal(), LINK_ESC_REF_WRITING);
    run_while(LINK_ESC_RUN, 20000u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    /* 150ms after its last frame. */
    const uint32_t t0 = esc.now;

    CHECK(esc_port_writing(&g_p));
    (void)cmd(LINK_ESC_OP_LEAVE);
    CHECK_EQ(refusal(), LINK_ESC_REF_WRITING);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    while (esc_port_writing(&g_p) && esc.now - t0 < 1000000u) {
        pass();
    }
    CHECK(esc.now - t0 >= (ESC_PORT_WRITE_TAIL_MS - 1u) * 1000u);
    CHECK(esc.now - t0 <= (ESC_PORT_WRITE_TAIL_MS + 1u) * 1000u);
    /* LEAVE waits for the same tail; here it has run out. */
    CHECK_EQ(cmd_full(LINK_ESC_OP_CLOSE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NONE);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    /* The supply goes off with the session, also for a CLOSE from READY. */
    CHECK(!g.v.asked_on);
}

TEST_CASE(the_blocks_of_the_data_page)
{
    uint16_t v[LINK_ED_COUNT] = { 0u };
    uint8_t b[LINK_ED_BLOCK_BYTES];

    up_am_rig();
    /* Read-only ones, past the page, and a NULL. */
    for (unsigned i = 0u; i < (unsigned)LINK_ED_BLOCKS; ++i) {
        const uint16_t k = (uint16_t)i;
        const bool can = (k >= LINK_ED_DRAFT && k < LINK_ED_PAGE)
                         || k == LINK_ED_BACKUP;

        (void)esc_port_write(&g_p, LINK_EW_W_BLOCK, 1u, &k, 0u);
        CHECK_EQ(esc_port_data_write(&g_p, 0u, 1u, v),
                 can ? 0u : (unsigned)LINK_NACK_READ_ONLY);
    }
    CHECK_EQ(esc_port_data_write(&g_p, LINK_ED_COUNT - 1u, 2u, v),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(esc_port_data_write(&g_p, 0u, 0u, v), LINK_NACK_BAD_RANGE);
    CHECK_EQ(esc_port_data_write(&g_p, 0u, 1u, NULL), LINK_NACK_BAD_RANGE);
    v[0] = 0x1234u;
    esc_port_data_read(&g_p, LINK_ED_COUNT - 1u, 2u, v);
    esc_port_data_read(NULL, 0u, 1u, v);
    esc_port_data_read(&g_p, 0u, 1u, NULL);
    CHECK_EQ(v[0], 0x1234u);
    /* The draft's last block and the backup's, read back. */
    for (unsigned k = 0u; k < LINK_ED_COUNT; ++k) {
        v[k] = (uint16_t)(0x0101u * k);
    }
    const uint16_t last = LINK_ED_DRAFT + 3u;
    const uint16_t bk = LINK_ED_BACKUP;

    (void)esc_port_write(&g_p, LINK_EW_W_BLOCK, 1u, &last, 0u);
    CHECK_EQ(esc_port_data_write(&g_p, 0u, LINK_ED_COUNT, v), 0u);
    block_get(last, b);
    CHECK_EQ(b[62], LINK_ED_COUNT - 1u);
    CHECK_EQ(g_p.draft[EW_PAGE_MAX - 1u], LINK_ED_COUNT - 1u);
    (void)esc_port_write(&g_p, LINK_EW_W_BLOCK, 1u, &bk, 0u);
    CHECK_EQ(esc_port_data_write(&g_p, 1u, 1u, v + 1u), 0u);
    block_get(bk, b);
    CHECK_EQ(b[2], 1u);
    /* A name that ends in its block; registers past it read 0. */
    ready_on(2u);
    strcpy(g_p.ses.id.name, "ABCD");
    block_get(LINK_ED_NAME, b);
    CHECK(memcmp(b, "ABCD", 5u) == 0);
    CHECK_EQ(b[62], 0u);
}

TEST_CASE(a_restore_of_erased_am32_settings)
{
    up_am_rig();
    uint8_t saved[EW_MAP_LEN_AM];
    uint16_t id[LINK_ED_COUNT];

    memcpy(saved, esc.am, sizeof(saved));
    memset(esc.am, 0xFF, sizeof(esc.am));
    ready_on(2u);
    CHECK(flag(LINK_EW_F_ERASED));
    CHECK(!flag(LINK_EW_F_WRITABLE));
    /* LEAVE refused, nothing off. */
    (void)cmd(LINK_ESC_OP_LEAVE);
    CHECK_EQ(refusal(), LINK_ESC_REF_SESSION);
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_ERASED);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    /* A backup with another name is refused and nothing goes. */
    draft_put(saved, EW_MAP_LEN_AM);
    ST_REG(LINK_EW_W_DRAFT_CRC) = link_crc(LINK_CRC_INIT, saved, EW_MAP_LEN_AM);
    (void)stage();
    CHECK_EQ(cmd_full(LINK_ESC_OP_RESTORE, 0u, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NO_KEY);
    const uint16_t bk = LINK_ED_BACKUP;
    ew_backup_id_t me;

    (void)esc_port_write(&g_p, LINK_EW_W_BLOCK, 1u, &bk, 0u);
    memset(id, 0, sizeof(id));
    CHECK_EQ(esc_port_data_write(&g_p, 0u, LINK_ED_COUNT, id), 0u);
    CHECK_EQ(cmd_full(LINK_ESC_OP_RESTORE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_SESSION);
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_BACKUP);
    /* Its own identity goes through. */
    ew_session_backup_id(&g_p.ses, &me);
    id[0] = (uint16_t)(me.pin_code | ((uint16_t)me.flash_code << 8));
    id[1] = me.protocol;
    for (size_t i = 0u; i <= strlen(me.name); i += 2u) {
        id[2u + i / 2u] = (uint16_t)((uint8_t)me.name[i]
                                     | ((uint16_t)(uint8_t)me.name[i + 1u] << 8));
    }
    CHECK_EQ(esc_port_data_write(&g_p, 0u, LINK_ED_COUNT, id), 0u);
    /* The last frame is the frame last taken; a new SEQ makes it a new one. */
    CHECK_EQ(cmd_full(LINK_ESC_OP_RESTORE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NONE);
    run_while(LINK_ESC_RUN, 20000u);
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
    CHECK(memcmp(esc.am, saved, 2u) == 0);
}

/* The line register after a capture: a cut or overflowing one parks the pad. */
TEST_CASE(the_line_register_follows_the_capture)
{
    static const uint8_t k_faults[] = { F_ABORTED, F_OVERFLOW };
    static const uint8_t k_tx[2] = { 0xFDu, 0x00u };
    static uint8_t rx[1] = { 0u };
    ew_capture_t c;
    const ew_xfer_t x = { .rate = EW_RATE_A, .n_tx = 2u, .max_rx = 1u,
                          .first_us = 1000u, .gap_us = 1000u, .tx = k_tx, .rx = rx };

    for (size_t i = 0u; i < 2u; ++i) {
        up_am_rig();
        ready_on(2u);
        sim_script(&esc, k_faults[i], 0, 1, 0u);
        CHECK(g_p.ses.d.start(g_p.ses.d.ctx, &x));
        CHECK_EQ(line_word(), LINK_ESC_LINE_SEND);
        for (int k = 0; k < 100000 && !g_p.ses.d.poll(g_p.ses.d.ctx, &c); ++k) {
            esc.now += 100u;
        }
        CHECK_EQ(line_word(), LINK_ESC_LINE_PARK);
    }
}

TEST_CASE(the_text_line)
{
    uint8_t b[LINK_ED_BLOCK_BYTES];

    sim_init_e32(&esc, "ESCape32 rev17.3 [BOARD_F051]");
    rig_up();
    ready_on(2u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    CHECK_EQ(reg(LINK_EW_ID) & 0xFFu, EW_FW_E32);
    read_now();
    /* The values, as 16-bit integers in the map's order. */
    block_get(LINK_ED_READ, b);
    CHECK_EQ((int16_t)(b[0] | (b[1] << 8)), esc.ram[0]);
    block_get(LINK_ED_MUSIC, b);
    CHECK(memcmp(b, esc.music, 8u) == 0);
    block_get(LINK_ED_INFO, b);
    CHECK_EQ(b[1], EW_E32_REVISION);
    /* A leave sends nothing, and the supply goes off in that frame. */
    const int starts = esc.starts;

    (void)cmd(LINK_ESC_OP_LEAVE);
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
    CHECK_EQ(esc.starts, starts);
    CHECK_ALL_OFF();
    CHECK(!flag(LINK_EW_F_FAULT));
}

int main(void)
{
    RUN(a_read_and_a_write_of_am32);
    RUN(a_write_refused_before_any_frame);
    RUN(a_draft_the_guard_refuses_is_a_fault);
    RUN(the_supply_holds_for_a_write_and_its_tail);
    RUN(the_blocks_of_the_data_page);
    RUN(a_restore_of_erased_am32_settings);
    RUN(the_line_register_follows_the_capture);
    RUN(the_text_line);
    return test_summary("esc_port_data");
}
