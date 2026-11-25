/*
 * A draft of programming for YGE ESCs carries the read copy it was made from
 * (shared/bench/yge_prog.c, yge_prog_file.c): a draft of an earlier read is
 * refused on the panel; one the panel has not yet seen to be stale goes with
 * its own read number and CRC and the IOmcu refuses it, with no frame to the
 * ESC; a loaded file's draft ends with the next read copy.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_prog_int.h"
#include "yge_prog_rig.h"

/* Draft, READ, WRITE: refused here as STALE, nothing sent, the ESC as it
 * was. */
TEST_CASE(a_draft_of_an_earlier_read_is_refused_on_the_panel)
{
    uint16_t d[YGE_LINK_VALUES_MAX];
    size_t len = 0u;

    rig_up();
    model_up();
    connected();
    const uint16_t reads = g_f.image.reads;
    const uint16_t crc = g_f.image.read_crc;

    memcpy(d, draft_with(21u, 13u), sizeof(d));
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_READ_DONE);
    CHECK(g_f.image.reads != reads);
    CHECK_EQ(yge_prog_write(&g_m, d, 32u, reads, crc), YGE_PROG_MSG_STALE);
    CHECK(!yge_prog_busy(&g_m));
    CHECK(!yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                              sizeof(g_tag), &g_card_id));
    model_run(500u);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 0);
    CHECK_EQ(sim.table[21], 12u);
    /* Made again from the copy held now, it goes. */
    CHECK_EQ(yge_prog_write(&g_m, d, 32u, g_f.image.reads, g_f.image.read_crc),
             YGE_PROG_MSG_NONE);
    CHECK(yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                             sizeof(g_tag), &g_card_id));
    yge_prog_card_done(&g_m, g_card_id, YGE_PROG_CARD_OK);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_WRITE_DONE);
    CHECK_EQ(sim.table[21], 13u);
}

/* The bench console reads the table again; the link's task has seen it, the
 * frame's feed not yet.  The panel's check passes on the feed, the WRITE goes
 * with the draft's own read number and CRC, and the IOmcu refuses it as STALE
 * before any frame to the ESC. */
TEST_CASE(the_iomcu_refuses_a_draft_the_panel_saw_as_current)
{
    uint16_t d[YGE_LINK_VALUES_MAX];
    size_t len = 0u;
    uint16_t detail = 0u;

    rig_up();
    model_up();
    connected();
    const uint16_t reads = g_f.image.reads;
    const uint16_t crc = g_f.image.read_crc;

    memcpy(d, draft_with(21u, 13u), sizeof(d));
    read_now();
    link_run(300u);
    CHECK(reg(LINK_YP_READS) != reads);
    CHECK_EQ(g_f.image.reads, reads);
    const unsigned stored = sim.stored;

    CHECK_EQ(yge_prog_write(&g_m, d, 32u, reads, crc), YGE_PROG_MSG_NONE);
    CHECK(yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                             sizeof(g_tag), &g_card_id));
    yge_prog_card_done(&g_m, g_card_id, YGE_PROG_CARD_OK);
    model_run(500u);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 1);
    CHECK_EQ(yge_prog_message(&g_m, &detail), YGE_PROG_MSG_REFUSED);
    CHECK_EQ(detail, LINK_YGE_REF_STALE);
    CHECK_EQ(refusal(), LINK_YGE_REF_STALE);
    CHECK_EQ(sim.stored, stored);
    CHECK_EQ(sim.table[21], 12u);
    CHECK(!yge_port_writing(&g_p));
    CHECK(!flag(LINK_YP_F_WRITING));
    /* The same through the model's own WRITE step, past every panel check. */
    yge_prog_write_go(&g_m, d, 32u, reads, crc);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, &detail), YGE_PROG_MSG_REFUSED);
    CHECK_EQ(detail, LINK_YGE_REF_STALE);
    CHECK_EQ(sim.stored, stored);
    CHECK_EQ(sim.table[21], 12u);
}

/* A loaded file's draft belongs to the read copy it was loaded against: the
 * next read ends it, and its own numbers make a WRITE of it STALE. */
TEST_CASE(a_loaded_draft_ends_with_the_next_read)
{
    uint16_t t[YGE_LINK_VALUES_MAX];
    uint16_t n = 0u;
    uint16_t reads = 0u;
    uint16_t crc = 0u;
    uint16_t d[YGE_LINK_VALUES_MAX];

    rig_up();
    model_up();
    connected();
    memcpy(t, g_f.image.value, sizeof(t));
    t[21] = 13u;
    const size_t len = oy_file_format(t, 32u, g_text, sizeof(g_text));

    CHECK_EQ(yge_prog_file_load(&g_m, g_text, len, NULL), YGE_PROG_MSG_LOADED);
    const uint16_t *got = yge_prog_file_draft(&g_m, &n, &reads, &crc);

    CHECK(got != NULL);
    CHECK_EQ(reads, g_f.image.reads);
    CHECK_EQ(crc, g_f.image.read_crc);
    memcpy(d, got, sizeof(d));
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_READ_DONE);
    CHECK(yge_prog_file_draft(&g_m, NULL, NULL, NULL) == NULL);
    CHECK_EQ(yge_prog_write(&g_m, d, n, reads, crc), YGE_PROG_MSG_STALE);
    model_run(500u);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 0);
    /* Loaded again against the new copy, it carries that copy's numbers. */
    CHECK_EQ(yge_prog_file_load(&g_m, g_text, len, NULL), YGE_PROG_MSG_LOADED);
    CHECK(yge_prog_file_draft(&g_m, NULL, &reads, NULL) != NULL);
    CHECK_EQ(reads, g_f.image.reads);
}

int main(void)
{
    RUN(a_draft_of_an_earlier_read_is_refused_on_the_panel);
    RUN(the_iomcu_refuses_a_draft_the_panel_saw_as_current);
    RUN(a_loaded_draft_ends_with_the_next_read);
    return test_summary("yge_prog_base");
}
