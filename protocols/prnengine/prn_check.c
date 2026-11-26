/**
 * @file
 * @brief The spec's second check and the advertisement match
 *        (prn_spec.h).
 *
 * prn_spec_check() holds a spec to the ranges and references the reader
 * gave it, so a spec damaged after the parse is refused before a job runs.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "prn_job.h"

_Static_assert(sizeof(prn_spec_t) <= PRN_SPEC_MAX,
               "prn_spec_t is over its 6,144 bytes");

/**
 * @brief Whether a NUL-terminated text lies inside its field.
 * @param t   the field
 * @param cap its size
 * @return true when a NUL ends it inside
 */
static bool text_ok(const char *t, size_t cap)
{
    for (size_t i = 0; i < cap; ++i) {
        if (t[i] == '\0') {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check a token run: inside the pool, every token of a known kind.
 * @param s   the spec
 * @param seq the run
 * @return 0 or a code
 */
static uint8_t seq_ok(const prn_spec_t *s, prn_tseq_t seq)
{
    if ((unsigned)seq.first + seq.n > s->ntok || seq.n > PRN_TMPL_TOKENS) {
        return 30u;
    }
    for (unsigned i = 0; i < seq.n; ++i) {
        const prn_tok_t *t = &s->tok[seq.first + i];
        bool ok;
        switch (t->type) {
        case PRN_T_BYTE:
        case PRN_T_DATA:
            ok = true;
            break;
        case PRN_T_ARG:
            ok = prn_enc_width(t->x) != 0u && t->a >= 0
                 && (unsigned)t->a < PRN_ARGS;
            break;
        case PRN_T_VAR:
            ok = prn_enc_width(t->x) != 0u && t->a > 0
                 && t->a < (int)PRN_V_COUNT;
            break;
        case PRN_T_LEN:
            ok = prn_enc_width(t->x) != 0u;
            break;
        case PRN_T_CK:
            ok = prn_ck_width(t->x) != 0u;
            break;
        default:
            ok = false;
            break;
        }
        if (!ok) {
            return 11u;
        }
    }
    return 0u;
}

/**
 * @brief Check a send: its template and its variables.
 * @param s   the spec
 * @param snd the send
 * @return 0 or a code
 */
static uint8_t send_ok(const prn_spec_t *s, const prn_send_t *snd)
{
    if (snd->tmpl >= s->ntmpl) {
        return 14u;
    }
    for (unsigned i = 0; i < PRN_ARGS; ++i) {
        if (snd->var[i] >= PRN_V_COUNT) {
            return 14u;
        }
    }
    return 0u;
}

/**
 * @brief Check a condition.
 * @param c   the condition
 * @return 0 or a code
 */
static uint8_t cond_ok(const prn_cond_t *c)
{
    if (c->field >= PRN_F_NAMES || c->op > PRN_OP_NE_V
        || c->var >= PRN_V_COUNT || c->err >= PRN_ERR_COUNT) {
        return 14u;
    }
    if (c->op >= PRN_OP_EQ_V && c->var == PRN_V_NONE) {
        return 14u;
    }
    return 0u;
}

/**
 * @brief Check the steps and the two jobs.
 * @param s the spec
 * @return 0 or a code
 */
static uint8_t steps_ok(const prn_spec_t *s)
{
    for (unsigned i = 0; i < s->nstep; ++i) {
        const prn_step_t *st = &s->step[i];
        uint8_t e = 0u;
        if (st->kind > PRN_S_WAIT || st->nuntil > 2u
            || st->ncheck > PRN_STEP_CHECKS || st->nsave > PRN_STEP_SAVES
            || st->tries < 1u || st->tries > 5u
            || st->else_err >= PRN_ERR_COUNT) {
            return 24u;
        }
        if (st->kind == PRN_S_SEND) {
            e = send_ok(s, &st->send);
            if (st->within_ms == 0u || (st->poll_ms != 0u && !st->has_expect)) {
                return 24u;
            }
        } else if ((st->kind == PRN_S_DATA && st->within_ms == 0u)
                   || (st->kind == PRN_S_WAIT && st->wait_ms == 0u)) {
            return 24u;
        }
        for (unsigned k = 0; e == 0u && k < st->nuntil; ++k) {
            e = cond_ok(&st->until[k]);
        }
        for (unsigned k = 0; e == 0u && k < st->ncheck; ++k) {
            e = cond_ok(&st->check[k]);
        }
        for (unsigned k = 0; e == 0u && k < st->nsave; ++k) {
            if (st->save_var[k] < PRN_V_SAVED || st->save_var[k] >= PRN_V_COUNT
                || st->save_field[k] >= PRN_F_NAMES) {
                e = 14u;
            }
        }
        if (e != 0u) {
            return e;
        }
        if (st->relink != PRN_RELINK_NONE && st->relink != PRN_RELINK_VERDICT
            && st->relink >= s->nstep) {
            return 25u;
        }
        if (st->relink != PRN_RELINK_NONE && s->relink_ms == 0u) {
            return 9u;
        }
    }
    if (s->nquery == 0u || s->nquery > PRN_STEPS || s->nprint == 0u
        || s->nprint > PRN_STEPS) {
        return 15u;
    }
    unsigned data = 0u;
    for (unsigned i = 0; i < s->nprint; ++i) {
        if (s->print[i] >= s->nstep) {
            return 15u;
        }
        data += (s->step[s->print[i]].kind == PRN_S_DATA) ? 1u : 0u;
    }
    for (unsigned i = 0; i < s->nquery; ++i) {
        if (s->query[i] >= s->nstep
            || s->step[s->query[i]].kind == PRN_S_DATA) {
            return 15u;
        }
    }
    if (data != 1u) {
        return 15u;
    }
    if (s->has_abort
        && (send_ok(s, &s->abort) != 0u || s->abort_to >= s->nprint
            || s->abort_from > s->abort_to)) {
        return 26u;
    }
    return 0u;
}

/**
 * @brief Check the answers, fields, errors and the verdict.
 * @param s the spec
 * @return 0 or a code
 */
static uint8_t answers_ok(const prn_spec_t *s)
{
    if (s->rx_min == 0u || s->rx_min > s->rx_max || s->rx_max > PRN_RX_MAX
        || s->key_off >= s->rx_min || s->magic_len > 4u || s->tail_len > 4u
        || s->join > PRN_J_NOTIFY || s->len_enc > PRN_E_BE16) {
        return 7u;
    }
    if (s->join == PRN_J_MAGIC_LEN
        && (s->magic_len == 0u
            || s->rx_min < s->len_off + prn_enc_width(s->len_enc))) {
        return 7u;
    }
    for (unsigned i = 0; i < s->nanswer; ++i) {
        if (s->answer_min[i] <= s->key_off || s->answer_min[i] > s->rx_max) {
            return 7u;
        }
    }
    for (unsigned i = 0; i < s->nfield; ++i) {
        const prn_field_t *f = &s->field[i];
        static const uint8_t k_w[] = { 1u, 1u, 2u, 2u, 4u, 4u, 0u };
        if (f->name >= PRN_F_NAMES || f->kind > PRN_FK_BYTES
            || (f->kind == PRN_FK_BIT && f->n > 7u)
            || (f->kind == PRN_FK_BYTES && (f->n < 1u || f->n > 8u))) {
            return 7u;
        }
        int a = -1;
        for (unsigned k = 0; k < s->nanswer; ++k) {
            a = (s->answer_key[k] == f->key) ? (int)k : a;
        }
        if (a < 0) {
            return 14u;
        }
        const unsigned w = (f->kind == PRN_FK_BYTES) ? f->n : k_w[f->kind];
        if ((unsigned)f->off + w > s->answer_min[a]) {
            return 13u;
        }
    }
    for (unsigned i = 0; i < s->nerror; ++i) {
        if (s->error_field[i] >= PRN_F_NAMES
            || s->error_code[i] >= PRN_ERR_COUNT) {
            return 14u;
        }
    }
    if (s->verdict > PRN_VD_NONE || s->vd_field >= PRN_F_NAMES
        || s->nresult > PRN_RESULTS) {
        return 7u;
    }
    if (s->verdict == PRN_VD_COUNTER
        && (send_ok(s, &s->vd_send) != 0u || s->vd_reads == 0u
            || prn_field_find(s, s->vd_key, s->vd_field) < 0)) {
        return 14u;
    }
    return 0u;
}

uint8_t prn_spec_check(const prn_spec_t *s)
{
    if (s->nmatch > PRN_MATCHES || s->nsource > PRN_SOURCES
        || s->ntmpl > PRN_TMPLS || s->ntok > PRN_TOKENS
        || s->nfield > PRN_FIELDS || s->nerror > PRN_ERRORS
        || s->nanswer > PRN_ANSWERS || s->nstep > PRN_STEPS
        || s->nroll > PRN_ROLLS || s->nspeed > PRN_SPEED_PAIRS) {
        return 30u;
    }
    if (!text_ok(s->id, sizeof s->id) || !text_ok(s->name, sizeof s->name)
        || s->id[0] == '\0' || s->name[0] == '\0') {
        return 29u;
    }
    if (s->nmatch == 0u) {
        return 20u;
    }
    for (unsigned i = 0; i < s->nmatch; ++i) {
        if (s->match[i].kind > PRN_M_SVC || s->match[i].len == 0u
            || s->match[i].len > PRN_NAME_MAX) {
            return 7u;
        }
    }
    if (s->head_dots < 8u || s->head_dots > 1024u || s->dots_mm != 8u
        || s->line_bytes < (s->head_dots + 7u) / 8u
        || s->line_bytes > PRN_LINE_BYTES || s->mtu_min < 23u
        || s->answer_ms < 100u || s->answer_ms > 60000u) {
        return 7u;
    }
    if (s->frag < 16u || s->frag > 244u || s->frag + 3u > s->mtu_min) {
        return 18u;
    }
    if (s->feed > PRN_EDGE_RIGHT || s->dot0 > PRN_EDGE_RIGHT
        || (s->feed < 2u) == (s->dot0 < 2u)) {
        return 21u;
    }
    for (unsigned i = 1u; i < s->nspeed; ++i) {
        if (s->speed_above[i] >= s->speed_above[i - 1u]) {
            return 19u;
        }
    }
    uint8_t e = 0u;
    for (unsigned i = 0; e == 0u && i < s->ntmpl; ++i) {
        e = seq_ok(s, s->tmpl[i].seq);
    }
    if (e == 0u) {
        e = seq_ok(s, s->frame);
    }
    if (e == 0u && s->page_size != 0u) {
        e = seq_ok(s, s->page_head);
    }
    if (e != 0u) {
        return e;
    }
    if (s->frame.n == 0u) {
        return 9u;
    }
    if (s->chunk_size > PRN_CHUNK_MAX || (s->chunk_size != 0u && s->chunk_max
                                                                    == 0u)) {
        return 7u;
    }
    if (s->chunk_size == 0u && s->lzma) {
        return 23u;
    }
    if (s->page_size != 0u
        && (s->page_size > 8192u || s->page_lines == 0u
            || (size_t)s->page_lines * s->line_bytes > s->page_size)) {
        return 7u;
    }
    if (s->roll_fixed ? s->nroll == 0u : s->nroll > PRN_ROLLS) {
        return 27u;
    }
    e = answers_ok(s);
    if (e != 0u) {
        return e;
    }
    return steps_ok(s);
}

/**
 * @brief Whether an advertised name starts with a prefix.
 * @param name   the name
 * @param m      the match line
 * @param nocase compare without regard to case
 * @return true when it does
 */
static bool prefix(const char *name, const prn_match_t *m, bool nocase)
{
    if (name == NULL || strlen(name) < m->len) {
        return false;
    }
    for (unsigned i = 0; i < m->len; ++i) {
        char a = name[i];
        char b = (char)m->b[i];
        if (nocase) {
            a = (a >= 'A' && a <= 'Z') ? (char)(a + 32) : a;
            b = (b >= 'A' && b <= 'Z') ? (char)(b + 32) : b;
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether an advertisement lists a UUID.
 * @param adv the advertisement
 * @param m   the match line, 2 or 16 bytes
 * @return true when it does
 */
static bool lists(const prn_adv_t *adv, const prn_match_t *m)
{
    const uint8_t *list = (m->len == 2u) ? adv->uuid16 : adv->uuid128;
    const unsigned n = (m->len == 2u) ? adv->n16 : adv->n128;
    for (unsigned i = 0; list != NULL && i < n; ++i) {
        if (memcmp(list + (size_t)i * m->len, m->b, m->len) == 0) {
            return true;
        }
    }
    return false;
}

unsigned prn_match(const prn_spec_t *s, const prn_adv_t *adv)
{
    static const uint8_t k_score[4] = { 4u, 2u, 2u, 1u };
    bool used[4] = { false, false, false, false };
    bool hit[4] = { false, false, false, false };
    for (unsigned i = 0; i < s->nmatch && i < PRN_MATCHES; ++i) {
        const prn_match_t *m = &s->match[i];
        if (m->kind > PRN_M_SVC) {
            continue;
        }
        used[m->kind] = true;
        bool h;
        switch (m->kind) {
        case PRN_M_OUI:
            h = adv->addr_public && memcmp(adv->addr, m->b, 3u) == 0;
            break;
        case PRN_M_NAME:
        case PRN_M_INAME:
            h = prefix(adv->name, m, m->kind == PRN_M_INAME);
            break;
        default:
            h = lists(adv, m);
            break;
        }
        hit[m->kind] = hit[m->kind] || h;
    }
    unsigned score = 0u;
    bool any = false;
    /* name and iname are one kind for the AND rule */
    const bool names_used = used[PRN_M_NAME] || used[PRN_M_INAME];
    const bool names_hit = hit[PRN_M_NAME] || hit[PRN_M_INAME];
    if (used[PRN_M_OUI]) {
        if (!hit[PRN_M_OUI]) {
            return 0u;
        }
        score += k_score[PRN_M_OUI];
        any = true;
    }
    if (names_used) {
        if (!names_hit) {
            return 0u;
        }
        score += k_score[PRN_M_NAME];
        any = true;
    }
    if (used[PRN_M_SVC]) {
        if (!hit[PRN_M_SVC]) {
            return 0u;
        }
        score += k_score[PRN_M_SVC];
        any = true;
    }
    return any ? score : 0u;
}
