/**
 * @file node_store.c
 * @brief The node's own settings and their store (node_store.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_store.h"

#include <string.h>

/**
 * @brief Whether a shunt is one the node keeps.
 * @param uohm The shunt, µΩ.
 * @return true for RCNODE_SHUNT_MIN_UOHM to RCNODE_SHUNT_MAX_UOHM.
 */
static bool shunt_ok(uint32_t uohm)
{
    return uohm >= RCNODE_SHUNT_MIN_UOHM && uohm <= RCNODE_SHUNT_MAX_UOHM;
}

/**
 * @brief Read a value from the store.
 * @param io The store, or NULL.  @param k The key.
 * @param buf Receives it.  @param n Its size.
 * @return true when one was read.
 */
static bool load(const nst_io_t *io, nst_key_t k, void *buf, size_t n)
{
    return io != NULL && io->load != NULL && io->load(io->ctx, k, buf, n);
}

/**
 * @brief Store a value; a failed write is counted.
 * @param s The settings.  @param io The store.  @param k The key.
 * @param buf The value.  @param n Its size.
 */
static void save(nst_t *s, const nst_io_t *io, nst_key_t k, const void *buf,
                 size_t n)
{
    if (io == NULL || io->save == NULL || !io->save(io->ctx, k, buf, n)) {
        ++s->failed;
    }
}

void nst_load(nst_t *s, const nst_io_t *io, bool power)
{
    memset(s, 0, sizeof *s);
    s->power = power;
    s->v.i228 = RCNODE_I228_AUTO;
    s->v.shunt = RCNODE_SHUNT_DEF_UOHM;
    s->lang = RCNODE_LANG_EN;
    char name[RCNODE_UNAME_MAX + 1u];
    memset(name, 0, sizeof name);
    if (load(io, NST_K_NAME, name, sizeof name)) {
        name[RCNODE_UNAME_MAX] = '\0';
        if (rcnode_uname_ok(name)) {
            memcpy(s->v.name, name, sizeof s->v.name);
        }
    }
    uint8_t b = 0u;
    if (load(io, NST_K_LANG, &b, sizeof b) && b == RCNODE_LANG_DE) {
        s->lang = RCNODE_LANG_DE;
    }
    if (!power) {
        s->agreed = s->v.shunt;
        return;
    }
    if (load(io, NST_K_I228, &b, sizeof b) && rcnode_i228_ok(b)) {
        s->v.i228 = b;
    }
    uint16_t u = 0u;
    if (load(io, NST_K_SHUNT, &u, sizeof u) && shunt_ok(u)) {
        s->v.shunt = u;
    }
    s->agreed = s->v.shunt;
    if (load(io, NST_K_AGREED, &u, sizeof u) && shunt_ok(u)) {
        s->agreed = u;
    }
}

/**
 * @brief A new name, stored when it differs.
 * @param s The settings.  @param io The store.  @param name The name, one
 *        rcnode_uname_ok() takes.
 * @return NST_CH_NAME when it changed, else 0.
 */
static unsigned set_name(nst_t *s, const nst_io_t *io, const char *name)
{
    if (strncmp(s->v.name, name, sizeof s->v.name) == 0) {
        return 0u;
    }
    memset(s->v.name, 0, sizeof s->v.name);
    memcpy(s->v.name, name, strnlen(name, RCNODE_UNAME_MAX));
    save(s, io, NST_K_NAME, s->v.name, strlen(s->v.name) + 1u);
    return NST_CH_NAME;
}

/**
 * @brief A new address setting, stored when it differs.
 * @param s The settings.  @param io The store.  @param a The setting.
 * @return NST_CH_I228 when it changed, else 0.
 */
static unsigned set_i228(nst_t *s, const nst_io_t *io, uint8_t a)
{
    if (s->v.i228 == a) {
        return 0u;
    }
    s->v.i228 = a;
    save(s, io, NST_K_I228, &s->v.i228, sizeof s->v.i228);
    return NST_CH_I228;
}

unsigned nst_setup_save(nst_t *s, const nst_io_t *io, const nst_vals_t *v,
                        uint32_t now_ms)
{
    if (s == NULL || v == NULL
        || strnlen(v->name, sizeof v->name) > RCNODE_UNAME_MAX
        || !rcnode_uname_ok(v->name)
        || (s->power && (!rcnode_i228_ok(v->i228) || !shunt_ok(v->shunt)))) {
        return 0u;
    }
    unsigned ch = set_name(s, io, v->name);
    if (!s->power) {
        return ch;
    }
    ch |= set_i228(s, io, v->i228);
    if (v->shunt != s->v.shunt) {
        s->v.shunt = v->shunt;
        save(s, io, NST_K_SHUNT, &s->v.shunt, sizeof s->v.shunt);
        /* The agreed value is stored with it: one read back from the
         * shunt alone (a store written before AGREED) would take the new
         * shunt for the agreed one. */
        save(s, io, NST_K_AGREED, &s->agreed, sizeof s->agreed);
        s->aged = true;
        s->changed_ms = now_ms;
        ch |= NST_CH_SHUNT;
    }
    return ch;
}

unsigned nst_panel_op(nst_t *s, const nst_io_t *io, const rcnode_ctl_t *c)
{
    if (s == NULL || c == NULL) {
        return 0u;
    }
    if (c->op == RCNODE_OP_NAME) {
        return (strnlen(c->name, sizeof c->name) <= RCNODE_UNAME_MAX
                && rcnode_uname_ok(c->name)) ? set_name(s, io, c->name) : 0u;
    }
    if (!s->power) {
        return 0u;
    }
    if (c->op == RCNODE_OP_I228) {
        return rcnode_i228_ok(c->i228) ? set_i228(s, io, c->i228) : 0u;
    }
    if (c->op != RCNODE_OP_SHUNT || !shunt_ok(c->shunt_uohm)) {
        return 0u;
    }
    unsigned ch = 0u;
    if (c->shunt_uohm != s->v.shunt) {
        s->v.shunt = c->shunt_uohm;
        save(s, io, NST_K_SHUNT, &s->v.shunt, sizeof s->v.shunt);
        ch |= NST_CH_SHUNT;
    }
    if (c->shunt_uohm != s->agreed) {
        s->agreed = c->shunt_uohm;
        save(s, io, NST_K_AGREED, &s->agreed, sizeof s->agreed);
        ch |= NST_CH_AGREE;
    }
    s->aged = false;
    return ch;
}

bool nst_lang(nst_t *s, const nst_io_t *io, uint8_t lang)
{
    const uint8_t l = lang == RCNODE_LANG_DE ? RCNODE_LANG_DE : RCNODE_LANG_EN;
    if (s == NULL || s->lang == l) {
        return false;
    }
    s->lang = l;
    save(s, io, NST_K_LANG, &s->lang, sizeof s->lang);
    return true;
}

void nst_ask(nst_t *s, bool on)
{
    if (s != NULL) {
        s->asked = on;
    }
}

bool nst_sends_config(const nst_t *s)
{
    return s != NULL && s->asked;
}

void nst_config(nst_t *s, uint32_t now_ms, rcnode_config_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (s == NULL) {
        out->shunt_age_s = RCNODE_AGE_NONE;
        return;
    }
    /* CONFIG's own seq, as STATUS has: a STREAM seq would read as a lost
     * packet in the central's count of the meter's packets. */
    out->hdr.seq = s->cfg_seq;
    s->cfg_seq = (uint8_t)(s->cfg_seq + 1u);
    memcpy(out->name, s->v.name, sizeof out->name);
    out->shunt_age_s = RCNODE_AGE_NONE;
    if (!s->power) {
        return;
    }
    out->flags = RCNODE_CF_SHUNT;
    out->i228_set = s->v.i228;
    out->shunt_uohm = s->v.shunt;
    out->shunt_agreed = s->agreed;
    if (s->aged) {
        const uint32_t age = (now_ms - s->changed_ms) / 1000u;
        out->shunt_age_s = (uint16_t)(age < RCNODE_AGE_NONE ? age
                                                            : RCNODE_AGE_NONE - 1u);
    }
}
