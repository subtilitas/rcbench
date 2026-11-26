/**
 * @file
 * @brief The printer file reader, image part: the page, frame, write,
 *        speed table, answer joiner and time keys (prn_spec.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "prn_job.h"
#include "prn_parse_int.h"

/* The page keys: size, lines, head, flags. */
uint8_t pp_page(pp_t *p)
{
    prn_spec_t *s = p->s;
    uint8_t e;
    uint32_t v = 0u;
    if (pp_is(p, 1u, "size")) {
        e = pp_once_n(p, K_PSIZE, 3u, false);
        if (e != 0u) {
            return e;
        }
        e = pp_num(p, 2u, 64u, 8192u, &v);
        s->page_size = (uint16_t)v;
        return e;
    }
    if (pp_is(p, 1u, "lines")) {
        e = pp_once_n(p, K_PLINES, 3u, false);
        if (e != 0u) {
            return e;
        }
        if (!pp_has(p, K_PSIZE) || !pp_has(p, K_LINE)) {
            return 9u;
        }
        e = pp_num(p, 2u, 1u, 1024u, &v);
        s->page_lines = (uint16_t)v;
        return e;
    }
    if (pp_is(p, 1u, "head")) {
        e = pp_once_n(p, K_PHEAD, 3u, true);
        if (e != 0u) {
            return e;
        }
        if (!pp_has(p, K_PLINES)) {
            return 9u;
        }
        e = pp_tokens(p, 2u, PP_C_PAGE, 32u, &s->page_head);
        if (e != 0u) {
            return e;
        }
        const size_t len = pp_fixed_len(s, s->page_head);
        if (!pp_ranges_ok(s, s->page_head, len, len)) {
            return 12u;
        }
        if (len + (size_t)s->page_lines * s->line_bytes > s->page_size) {
            return 7u;
        }
        return 0u;
    }
    if (pp_is(p, 1u, "flags")) {
        e = pp_once_n(p, K_PFLAGS, 4u, false);
        if (e != 0u) {
            return e;
        }
        if (pp_hex(PP_T(p, 2), p->len[2], &s->page_flags[0], 1u) != 1u
            || pp_hex(PP_T(p, 3), p->len[3], &s->page_flags[1], 1u) != 1u) {
            return 7u;
        }
        return 0u;
    }
    return 6u;
}

/* The data frame keys: compress, chunk, rows. */
uint8_t pp_frames(pp_t *p)
{
    prn_spec_t *s = p->s;
    uint8_t e;
    uint32_t v = 0u;
    if (pp_is(p, 0, "compress")) {
        e = pp_once_n(p, K_COMPRESS, 2u, true);
        if (e != 0u) {
            return e;
        }
        if (pp_is(p, 1u, "none") && p->nt == 2u) {
            return 0u;
        }
        if (!pp_is(p, 1u, "lzma_lit") || p->nt != 3u
            || pp_num(p, 2u, 4096u, 65536u, &v) != 0u) {
            return 7u;
        }
        if (pp_has(p, K_ROWS)) {
            return 23u;
        }
        s->lzma = true;
        s->lzma_dict = v;
        return 0u;
    }
    if (pp_is(p, 0, "rows")) {
        e = pp_once_n(p, K_ROWS, 2u, true);
        if (e != 0u) {
            return e;
        }
        if (pp_has(p, K_CSIZE) || s->lzma) {
            return 23u;
        }
        if (!pp_has(p, K_LINE)) {
            return 9u;
        }
        e = pp_tokens(p, 1u, PP_C_ROWS, PRN_TMPL_TOKENS, &s->frame);
        if (e != 0u) {
            return e;
        }
        const size_t len = pp_fixed_len(s, s->frame) + s->line_bytes;
        return pp_ranges_ok(s, s->frame, len, len) ? 0u : 12u;
    }
    if (!pp_is(p, 0, "chunk")) {
        return 255u;
    }
    if (pp_is(p, 1u, "size")) {
        e = pp_once_n(p, K_CSIZE, 3u, false);
        if (e != 0u) {
            return e;
        }
        if (pp_has(p, K_ROWS)) {
            return 23u;
        }
        e = pp_num(p, 2u, 1u, PRN_CHUNK_MAX, &v);
        s->chunk_size = (uint16_t)v;
        return e;
    }
    if (pp_is(p, 1u, "pad")) {
        e = pp_once_n(p, K_CPAD, 3u, false);
        if (e != 0u) {
            return e;
        }
        if (pp_is(p, 2u, "no")) {
            return 0u;
        }
        s->chunk_pad = true;
        return (pp_hex(PP_T(p, 2), p->len[2], &s->chunk_pad_byte, 1u) == 1u)
                   ? 0u : 7u;
    }
    if (pp_is(p, 1u, "max")) {
        e = pp_once_n(p, K_CMAX, 3u, false);
        if (e != 0u) {
            return e;
        }
        e = pp_num(p, 2u, 1u, 255u, &v);
        s->chunk_max = (uint8_t)v;
        return e;
    }
    if (pp_is(p, 1u, "frame")) {
        e = pp_once_n(p, K_CFRAME, 3u, true);
        if (e != 0u) {
            return e;
        }
        if (!pp_has(p, K_CSIZE) || !pp_has(p, K_CPAD)) {
            return 9u;
        }
        e = pp_tokens(p, 2u, PP_C_CHUNK, PRN_TMPL_TOKENS, &s->frame);
        if (e != 0u) {
            return e;
        }
        const size_t fixed = pp_fixed_len(s, s->frame);
        const size_t lo = fixed + (s->chunk_pad ? s->chunk_size : 1u);
        return pp_ranges_ok(s, s->frame, lo, fixed + s->chunk_size) ? 0u
                                                                    : 12u;
    }
    return 6u;
}

/* The write and speed table keys. */
uint8_t pp_writes(pp_t *p)
{
    prn_spec_t *s = p->s;
    uint8_t e;
    uint32_t v[2] = { 0u, 0u };
    if (pp_is(p, 0, "write")) {
        const bool cmd = pp_is(p, 1u, "cmd");
        if (!cmd && !pp_is(p, 1u, "data")) {
            return 6u;
        }
        e = pp_once_n(p, cmd ? K_WCMD : K_WDATA, cmd ? 3u : 5u, false);
        if (e != 0u) {
            return e;
        }
        if (!pp_is(p, 2u, "req") && !pp_is(p, 2u, "nr")) {
            return 7u;
        }
        if (cmd) {
            s->cmd_nr = pp_is(p, 2u, "nr");
            return 0u;
        }
        if (!pp_has(p, K_MTU)) {
            return 9u;
        }
        s->data_nr = pp_is(p, 2u, "nr");
        if (pp_num(p, 3u, 16u, 244u, &v[0]) != 0u
            || pp_num(p, 4u, 0u, 500u, &v[1]) != 0u) {
            return 7u;
        }
        if (v[0] + 3u > s->mtu_min) {
            return 18u;
        }
        s->frag = (uint8_t)v[0];
        s->gap_ms = (uint16_t)v[1];
        return 0u;
    }
    if (!pp_is(p, 0, "table")) {
        return 255u;
    }
    if (!pp_is(p, 1u, "speed")) {
        return 6u;
    }
    e = pp_once_n(p, K_SPEED, 6u, true);
    if (e != 0u) {
        return e;
    }
    if (!pp_is(p, 2u, "mean_page") && !pp_is(p, 2u, "stream_len")) {
        return 7u;
    }
    s->speed_stream = pp_is(p, 2u, "stream_len");
    const unsigned pairs = p->nt - 5u;
    if (!pp_is(p, p->nt - 2u, "else")) {
        return 7u;
    }
    if (pairs > PRN_SPEED_PAIRS) {
        return 30u;
    }
    for (unsigned i = 0; i < pairs; ++i) {
        const char *t = PP_T(p, 3u + i);
        const size_t n = p->len[3u + i];
        const size_t colon = pp_find(t, n, ':');
        if (colon == n || pp_num_text(t, colon, 0u, 0xFFFFFFFFu, &v[0]) != 0u
            || pp_num_text(t + colon + 1u, n - colon - 1u, 0u, 65535u, &v[1])
                   != 0u) {
            return 7u;
        }
        if (i > 0u && v[0] >= s->speed_above[i - 1u]) {
            return 19u;
        }
        s->speed_above[i] = v[0];
        s->speed_value[i] = (uint16_t)v[1];
    }
    if (pp_num(p, p->nt - 1u, 0u, 65535u, &v[0]) != 0u) {
        return 7u;
    }
    s->speed_else = (uint16_t)v[0];
    s->nspeed = (uint8_t)pairs;
    return 0u;
}

/* The answer joiner keys: rx join, tail, check, key. */
uint8_t pp_rx(pp_t *p)
{
    static const char *const k_lenc[] = { "u8", "le16", "be16", NULL };
    prn_spec_t *s = p->s;
    uint8_t e;
    uint32_t v[4] = { 0u, 0u, 0u, 0u };
    if (pp_is(p, 1u, "join")) {
        e = pp_once(p, K_RXJOIN);
        if (e != 0u) {
            return e;
        }
        if (pp_is(p, 2u, "notify") && p->nt == 5u) {
            if (pp_num(p, 3u, 1u, 40u, &v[0]) != 0u
                || pp_num(p, 4u, v[0], 40u, &v[1]) != 0u) {
                return 7u;
            }
            s->join = PRN_J_NOTIFY;
            s->rx_min = (uint8_t)v[0];
            s->rx_max = (uint8_t)v[1];
            return 0u;
        }
        if (!pp_is(p, 2u, "magic_len") || p->nt != 9u) {
            return 7u;
        }
        s->magic_len = (uint8_t)pp_hex(PP_T(p, 3), p->len[3], s->magic, 4u);
        const int enc = pp_tok_word(p, 5u, k_lenc);
        if (s->magic_len == 0u || enc < 0
            || pp_num(p, 4u, 0u, 8u, &v[0]) != 0u
            || pp_num(p, 6u, 0u, 16u, &v[1]) != 0u
            || pp_num(p, 7u, 4u, PRN_RX_MAX, &v[2]) != 0u
            || pp_num(p, 8u, v[2], PRN_RX_MAX, &v[3]) != 0u) {
            return 7u;
        }
        const uint32_t need = v[0] + ((enc == 0) ? 1u : 2u);
        if (v[2] < s->magic_len || v[2] < need) {
            return 7u;
        }
        s->join = PRN_J_MAGIC_LEN;
        s->len_off = (uint8_t)v[0];
        s->len_enc = (uint8_t)enc;
        s->len_add = (uint8_t)v[1];
        s->rx_min = (uint8_t)v[2];
        s->rx_max = (uint8_t)v[3];
        return 0u;
    }
    if (pp_is(p, 1u, "tail")) {
        e = pp_once_n(p, K_RXTAIL, 3u, false);
        if (e != 0u) {
            return e;
        }
        s->tail_len = (uint8_t)pp_hex(PP_T(p, 2), p->len[2], s->tail, 4u);
        return (s->tail_len != 0u) ? 0u : 7u;
    }
    if (pp_is(p, 1u, "check")) {
        e = pp_once_n(p, K_RXCHECK, 3u, false);
        if (e != 0u) {
            return e;
        }
        if (!pp_has(p, K_RXJOIN)) {
            return 9u;
        }
        prn_tseq_t seq;
        e = pp_tokens(p, 2u, PP_C_CHECK, 1u, &seq);
        if (e != 0u) {
            return e;
        }
        if (!pp_ranges_ok(s, seq, s->rx_min, s->rx_max)) {
            return 12u;
        }
        const prn_tok_t *ck = &s->tok[seq.first];
        for (unsigned k = 0; k < 2u; ++k) {
            /* the checksum sits right after its range */
            const long len = k ? s->rx_max : s->rx_min;
            const long to = (ck->b < 0) ? len + ck->b : ck->b;
            if (to + (long)prn_ck_width(ck->x) >= len) {
                return 12u;
            }
        }
        s->rx_check = *ck;
        s->has_rx_check = true;
        s->ntok = seq.first; /* the token lives in rx_check, not the pool */
        return 0u;
    }
    if (pp_is(p, 1u, "key")) {
        e = pp_once_n(p, K_RXKEY, 3u, false);
        if (e != 0u) {
            return e;
        }
        if (!pp_has(p, K_RXJOIN)) {
            return 9u;
        }
        if (pp_num(p, 2u, 0u, 15u, &v[0]) != 0u || v[0] >= s->rx_min) {
            return 7u;
        }
        s->key_off = (uint8_t)v[0];
        return 0u;
    }
    return 6u;
}

/* One top-level key that takes a number: relink_ms, answer_ms. */
uint8_t pp_times(pp_t *p)
{
    prn_spec_t *s = p->s;
    const bool relink = pp_is(p, 0, "relink_ms");
    if (!relink && !pp_is(p, 0, "answer_ms")) {
        return 255u;
    }
    uint8_t e = pp_once_n(p, relink ? K_RELINKMS : K_ANSWERMS, 2u, false);
    if (e != 0u) {
        return e;
    }
    uint32_t v = 0u;
    e = relink ? pp_num(p, 1u, 1000u, 30000u, &v)
               : pp_num(p, 1u, 100u, 60000u, &v);
    if (relink) {
        s->relink_ms = (uint16_t)v;
    } else {
        s->answer_ms = (uint16_t)v;
    }
    return e;
}
