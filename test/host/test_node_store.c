/*
 * The node's own settings and their store
 * (firmware/node/main/node_store.c), over a store in memory.
 *
 * The failures this is about: a stored name or address out of range taken
 * as it is, a shunt changed on the node's SETUP that a later link reads as
 * the agreed one (so a panel's older value would overwrite it), a panel's
 * SHUNT that leaves the node's change pending, a value stored on a
 * balance node that has no shunt, and an age that wraps.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "node_store.h"
#include "rcnode.h"

/** @brief The fake store: one value per key and the writes counted. */
typedef struct {
    uint8_t  val[NST_K_COUNT][RCNODE_UNAME_MAX + 1u];
    size_t   len[NST_K_COUNT];
    bool     have[NST_K_COUNT];
    unsigned writes;
    bool     fail;
} fake_t;

/** @brief nst_io_t's load over the fake. */
static bool f_load(void *ctx, nst_key_t k, void *buf, size_t n)
{
    const fake_t *f = ctx;
    if (!f->have[k] || f->len[k] > n) {
        return false;
    }
    memcpy(buf, f->val[k], f->len[k]);
    return true;
}

/** @brief nst_io_t's save over the fake. */
static bool f_save(void *ctx, nst_key_t k, const void *buf, size_t n)
{
    fake_t *f = ctx;
    ++f->writes;
    if (f->fail || n > sizeof f->val[k]) {
        return false;
    }
    memcpy(f->val[k], buf, n);
    f->len[k] = n;
    f->have[k] = true;
    return true;
}

/** @brief Put a value into the fake directly. */
static void put(fake_t *f, nst_key_t k, const void *v, size_t n)
{
    memcpy(f->val[k], v, n);
    f->len[k] = n;
    f->have[k] = true;
}

TEST_CASE(an_empty_store_reads_as_every_default)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    const nst_io_t io = { f_load, f_save, &f };
    nst_t s;
    nst_load(&s, &io, true);
    CHECK_STR_EQ(s.v.name, "");
    CHECK_EQ(s.v.i228, RCNODE_I228_AUTO);
    CHECK_EQ(s.v.shunt, RCNODE_SHUNT_DEF_UOHM);
    CHECK_EQ(s.agreed, RCNODE_SHUNT_DEF_UOHM);
    CHECK_EQ(s.lang, RCNODE_LANG_EN);
    nst_load(&s, NULL, true);
    CHECK_EQ(s.v.shunt, RCNODE_SHUNT_DEF_UOHM);
}

TEST_CASE(a_bad_stored_name_or_address_reads_as_the_default)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    const nst_io_t io = { f_load, f_save, &f };
    put(&f, NST_K_NAME, "lower", 6);
    const uint8_t a = 0x38u;
    put(&f, NST_K_I228, &a, 1);
    const uint16_t sh = 20000u;
    put(&f, NST_K_SHUNT, &sh, 2);
    const uint8_t lang = 7u;
    put(&f, NST_K_LANG, &lang, 1);
    nst_t s;
    nst_load(&s, &io, true);
    CHECK_STR_EQ(s.v.name, "");
    CHECK_EQ(s.v.i228, RCNODE_I228_AUTO);
    CHECK_EQ(s.v.shunt, RCNODE_SHUNT_DEF_UOHM);
    CHECK_EQ(s.lang, RCNODE_LANG_EN);
    /* A name of 17 bytes without its NUL is cut and refused as too long
     * only when it breaks the rule; 16 good characters stand. */
    put(&f, NST_K_NAME, "0123456789ABCDEF", 17);
    const uint8_t b = 0x4Fu;
    put(&f, NST_K_I228, &b, 1);
    nst_load(&s, &io, true);
    CHECK_STR_EQ(s.v.name, "0123456789ABCDEF");
    CHECK_EQ(s.v.i228, 0x4Fu);
}

TEST_CASE(a_stored_shunt_from_before_agreed_counts_as_agreed)
{
    /* A node updated from the release with SHUNT alone has no agreed
     * value: its shunt is the agreed one, as the panel last wrote it. */
    fake_t f;
    memset(&f, 0, sizeof f);
    const nst_io_t io = { f_load, f_save, &f };
    const uint16_t sh = 500u;
    put(&f, NST_K_SHUNT, &sh, 2);
    nst_t s;
    nst_load(&s, &io, true);
    CHECK_EQ(s.v.shunt, 500u);
    CHECK_EQ(s.agreed, 500u);
    rcnode_config_t c;
    nst_config(&s, 1000u, &c);
    CHECK_EQ(c.shunt_age_s, RCNODE_AGE_NONE);
    CHECK_EQ(rcnode_shunt_sync(500u, c.shunt_uohm, c.shunt_agreed,
                               c.shunt_age_s, 0u), RCNODE_SYNC_KEEP);
}

TEST_CASE(setup_save_stores_what_changed_and_starts_the_shunt_clock)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    const nst_io_t io = { f_load, f_save, &f };
    nst_t s;
    nst_load(&s, &io, true);
    nst_vals_t v = s.v;
    strcpy(v.name, "XT90 MAIN");
    v.i228 = 0x45u;
    v.shunt = 500u;
    CHECK_EQ(nst_setup_save(&s, &io, &v, 5000u),
             NST_CH_NAME | NST_CH_I228 | NST_CH_SHUNT);
    CHECK_EQ(f.writes, 4u);
    CHECK_EQ(s.agreed, RCNODE_SHUNT_DEF_UOHM);
    rcnode_config_t c;
    nst_config(&s, 12999u, &c);
    CHECK_EQ(c.flags, RCNODE_CF_SHUNT);
    CHECK_EQ(c.shunt_uohm, 500u);
    CHECK_EQ(c.shunt_agreed, RCNODE_SHUNT_DEF_UOHM);
    CHECK_EQ(c.shunt_age_s, 7u);
    CHECK_EQ(c.i228_set, 0x45u);
    CHECK_STR_EQ(c.name, "XT90 MAIN");
    /* Saved again unchanged: nothing written. */
    CHECK_EQ(nst_setup_save(&s, &io, &v, 6000u), 0u);
    CHECK_EQ(f.writes, 4u);
    /* A restart reads the same values back. */
    nst_t r;
    nst_load(&r, &io, true);
    CHECK_STR_EQ(r.v.name, "XT90 MAIN");
    CHECK_EQ(r.v.i228, 0x45u);
    CHECK_EQ(r.v.shunt, 500u);
    CHECK_EQ(r.agreed, RCNODE_SHUNT_DEF_UOHM);
    /* After the restart the change is still the node's, of unknown age. */
    nst_config(&r, 0u, &c);
    CHECK_EQ(c.shunt_age_s, RCNODE_AGE_NONE);
    CHECK_EQ(rcnode_shunt_sync(RCNODE_SHUNT_DEF_UOHM, c.shunt_uohm,
                               c.shunt_agreed, c.shunt_age_s, 0u),
             RCNODE_SYNC_ADOPT);
}

TEST_CASE(setup_save_refuses_a_value_out_of_range)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    const nst_io_t io = { f_load, f_save, &f };
    nst_t s;
    nst_load(&s, &io, true);
    nst_vals_t v = s.v;
    v.shunt = 49u;
    CHECK_EQ(nst_setup_save(&s, &io, &v, 0u), 0u);
    v.shunt = 10001u;
    CHECK_EQ(nst_setup_save(&s, &io, &v, 0u), 0u);
    v.shunt = 200u;
    v.i228 = 0x50u;
    CHECK_EQ(nst_setup_save(&s, &io, &v, 0u), 0u);
    v.i228 = 0u;
    strcpy(v.name, " A");
    CHECK_EQ(nst_setup_save(&s, &io, &v, 0u), 0u);
    CHECK_EQ(f.writes, 0u);
}

TEST_CASE(a_panel_shunt_sets_the_agreed_value_and_stops_the_clock)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    const nst_io_t io = { f_load, f_save, &f };
    nst_t s;
    nst_load(&s, &io, true);
    nst_vals_t v = s.v;
    v.shunt = 500u;
    (void)nst_setup_save(&s, &io, &v, 0u);
    rcnode_ctl_t c;
    memset(&c, 0, sizeof c);
    c.op = RCNODE_OP_SHUNT;
    c.shunt_uohm = 500u;          /* the panel took the node's value */
    CHECK_EQ(nst_panel_op(&s, &io, &c), NST_CH_AGREE);
    rcnode_config_t g;
    nst_config(&s, 4000u, &g);
    CHECK_EQ(g.shunt_agreed, 500u);
    CHECK_EQ(g.shunt_age_s, RCNODE_AGE_NONE);
    c.shunt_uohm = 1000u;         /* the panel's own change */
    CHECK_EQ(nst_panel_op(&s, &io, &c), NST_CH_SHUNT | NST_CH_AGREE);
    CHECK_EQ(s.v.shunt, 1000u);
    CHECK_EQ(s.agreed, 1000u);
    c.shunt_uohm = 20u;
    CHECK_EQ(nst_panel_op(&s, &io, &c), 0u);
    CHECK_EQ(s.v.shunt, 1000u);
}

TEST_CASE(a_panel_name_and_address_write_and_their_refusals)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    const nst_io_t io = { f_load, f_save, &f };
    nst_t s;
    nst_load(&s, &io, true);
    rcnode_ctl_t c;
    memset(&c, 0, sizeof c);
    c.op = RCNODE_OP_NAME;
    strcpy(c.name, "BENCH 2");
    CHECK_EQ(nst_panel_op(&s, &io, &c), NST_CH_NAME);
    CHECK_STR_EQ(s.v.name, "BENCH 2");
    CHECK_EQ(nst_panel_op(&s, &io, &c), 0u);
    strcpy(c.name, "bad");
    CHECK_EQ(nst_panel_op(&s, &io, &c), 0u);
    CHECK_STR_EQ(s.v.name, "BENCH 2");
    c.op = RCNODE_OP_I228;
    c.i228 = 0x41u;
    CHECK_EQ(nst_panel_op(&s, &io, &c), NST_CH_I228);
    c.i228 = 0x77u;
    CHECK_EQ(nst_panel_op(&s, &io, &c), 0u);
    CHECK_EQ(s.v.i228, 0x41u);
    c.op = RCNODE_OP_RUN;
    CHECK_EQ(nst_panel_op(&s, &io, &c), 0u);
}

TEST_CASE(a_balance_node_keeps_a_name_and_no_shunt)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    const nst_io_t io = { f_load, f_save, &f };
    const uint16_t sh = 500u;
    put(&f, NST_K_SHUNT, &sh, 2);
    nst_t s;
    nst_load(&s, &io, false);
    CHECK_EQ(s.v.shunt, RCNODE_SHUNT_DEF_UOHM);
    rcnode_ctl_t c;
    memset(&c, 0, sizeof c);
    c.op = RCNODE_OP_SHUNT;
    c.shunt_uohm = 700u;
    CHECK_EQ(nst_panel_op(&s, &io, &c), 0u);
    c.op = RCNODE_OP_I228;
    c.i228 = 0x40u;
    CHECK_EQ(nst_panel_op(&s, &io, &c), 0u);
    c.op = RCNODE_OP_NAME;
    strcpy(c.name, "PROP 1");
    CHECK_EQ(nst_panel_op(&s, &io, &c), NST_CH_NAME);
    rcnode_config_t g;
    nst_config(&s, 0u, &g);
    CHECK_EQ(g.flags, 0u);
    CHECK_EQ(g.shunt_uohm, 0u);
    CHECK_EQ(g.i228_set, 0u);
    CHECK_STR_EQ(g.name, "PROP 1");
}

TEST_CASE(the_age_saturates_and_survives_the_clock_wrap)
{
    nst_t s;
    nst_load(&s, NULL, true);
    nst_vals_t v = s.v;
    v.shunt = 300u;
    (void)nst_setup_save(&s, NULL, &v, 0xFFFFF000u);
    rcnode_config_t g;
    nst_config(&s, 0x00001000u, &g);   /* 8192ms later, past the wrap */
    CHECK_EQ(g.shunt_age_s, 8u);
    nst_config(&s, 0xFFFFF000u + 70000000u, &g);
    CHECK_EQ(g.shunt_age_s, RCNODE_AGE_NONE - 1u);
    CHECK(s.failed > 0u);              /* no store: each write counted */
}

TEST_CASE(the_last_panel_language_is_kept)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    const nst_io_t io = { f_load, f_save, &f };
    nst_t s;
    nst_load(&s, &io, true);
    CHECK(nst_lang(&s, &io, RCNODE_LANG_DE));
    CHECK(!nst_lang(&s, &io, RCNODE_LANG_DE));
    nst_t r;
    nst_load(&r, &io, true);
    CHECK_EQ(r.lang, RCNODE_LANG_DE);
    CHECK(nst_lang(&r, &io, 9u));
    CHECK_EQ(r.lang, RCNODE_LANG_EN);
}

TEST_CASE(config_goes_out_only_after_the_central_asked_on_the_link)
{
    /* An older panel writes SHUNT and never CONFIG: no packet it does not
     * know reaches it.  A newer one asks; the link's end stops it. */
    nst_t s;
    nst_load(&s, NULL, true);
    CHECK(!nst_sends_config(&s));
    rcnode_ctl_t c;
    memset(&c, 0, sizeof c);
    c.op = RCNODE_OP_SHUNT;
    c.shunt_uohm = 500u;
    CHECK(nst_panel_op(&s, NULL, &c) != 0u);
    CHECK(!nst_sends_config(&s));
    nst_ask(&s, true);
    CHECK(nst_sends_config(&s));
    nst_ask(&s, false);
    CHECK(!nst_sends_config(&s));
    /* INFO keeps its 20 bytes: an older panel reads it as before. */
    CHECK_EQ(RCNODE_INFO_LEN, 20u);
}

int main(void)
{
    RUN(an_empty_store_reads_as_every_default);
    RUN(a_bad_stored_name_or_address_reads_as_the_default);
    RUN(a_stored_shunt_from_before_agreed_counts_as_agreed);
    RUN(setup_save_stores_what_changed_and_starts_the_shunt_clock);
    RUN(setup_save_refuses_a_value_out_of_range);
    RUN(a_panel_shunt_sets_the_agreed_value_and_stops_the_clock);
    RUN(a_panel_name_and_address_write_and_their_refusals);
    RUN(a_balance_node_keeps_a_name_and_no_shunt);
    RUN(the_age_saturates_and_survives_the_clock_wrap);
    RUN(the_last_panel_language_is_kept);
    RUN(config_goes_out_only_after_the_central_asked_on_the_link);
    return test_summary("node_store");
}
