/**
 * @file
 * @brief The image stream: head lines packed as the file says, pages,
 *        chunk frames, row frames and the speed table (prn_job.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "prn_job.h"

/** @brief The page checksum reads back the byte before each 256B step. */
#define SVPAGE_STRIDE 256u

void prn_line_pack(const prn_spec_t *s, const uint8_t *in, uint8_t *out)
{
    const unsigned nb = s->line_bytes;
    memset(out, s->line_black0 ? 0xFF : 0x00, nb);
    for (unsigned d = 0; d < s->head_dots && d / 8u < nb; ++d) {
        if (((in[d / 8u] >> (d % 8u)) & 1u) == 0u) {
            continue; /* white: the byte already holds it */
        }
        const unsigned bit = s->line_msb ? (7u - d % 8u) : (d % 8u);
        out[d / 8u] ^= (uint8_t)(1u << bit);
    }
    if (s->line_reverse) {
        for (unsigned i = 0; i < nb / 2u; ++i) {
            const uint8_t t = out[i];
            out[i] = out[nb - 1u - i];
            out[nb - 1u - i] = t;
        }
    }
}

uint32_t prn_page_count(const prn_spec_t *s, uint32_t lines)
{
    if (s->page_size == 0u || s->page_lines == 0u) {
        return 0u;
    }
    return (lines + s->page_lines - 1u) / s->page_lines;
}

/**
 * @brief The offset of the page head's svpage token.
 * @param s   the spec
 * @param ctx the values (no data in a page head)
 * @param at  set to the offset
 * @return false when the head has none
 */
static bool svpage_at(const prn_spec_t *s, const prn_ctx_t *ctx, size_t *at)
{
    size_t o = 0u;
    for (unsigned i = 0; i < s->page_head.n; ++i) {
        const prn_tok_t *t = &s->tok[s->page_head.first + i];
        if (t->type == PRN_T_CK && t->x == PRN_CK_SVPAGE) {
            *at = o;
            return true;
        }
        switch (t->type) {
        case PRN_T_BYTE:
            o += 1u;
            break;
        case PRN_T_CK:
            o += prn_ck_width(t->x);
            break;
        case PRN_T_DATA:
            o += (size_t)ctx->data_len + ctx->data_pad;
            break;
        default:
            o += prn_enc_width(t->x);
            break;
        }
    }
    return false;
}

bool prn_page_fill(const prn_spec_t *s, prn_ctx_t *ctx, const uint8_t *bits,
                   uint32_t lines, uint32_t index, uint8_t *out)
{
    const uint32_t count = prn_page_count(s, lines);
    if (bits == NULL || out == NULL || index >= count) {
        return false;
    }
    const uint32_t first = index * s->page_lines;
    uint32_t n = lines - first;
    if (n > s->page_lines) {
        n = s->page_lines;
    }
    uint8_t flags = 0u;
    if (index == 0u) {
        flags |= s->page_flags[0];
    }
    if (index + 1u == count) {
        flags |= s->page_flags[1];
    }
    ctx->var[PRN_V_LINES] = n;
    ctx->var[PRN_V_PFLAGS] = flags;
    ctx->data = NULL;
    ctx->data_len = 0u;
    ctx->data_pad = 0u;
    size_t head = 0u;
    if (!prn_tmpl_render(s, s->page_head, ctx, out, s->page_size, &head)) {
        return false;
    }
    const size_t data = (size_t)n * s->line_bytes;
    if (head + (size_t)s->page_lines * s->line_bytes > s->page_size) {
        return false;
    }
    memcpy(out + head, bits + (size_t)first * s->line_bytes, data);
    memset(out + head + data, 0, s->page_size - head - data);
    size_t at;
    if (svpage_at(s, ctx, &at)) {
        uint16_t sum = 0u;
        for (size_t i = 0; i < head; ++i) {
            sum = (uint16_t)(sum + out[i]);
        }
        for (size_t i = SVPAGE_STRIDE; i <= head + data; i += SVPAGE_STRIDE) {
            sum = (uint16_t)(sum + out[i - 1u]);
        }
        out[at] = (uint8_t)sum;
        out[at + 1u] = (uint8_t)(sum >> 8);
    }
    return true;
}

uint32_t prn_frame_count(const prn_spec_t *s, uint32_t len)
{
    if (len == 0u) {
        return 0u;
    }
    if (s->chunk_size != 0u) {
        const uint32_t c = (len + s->chunk_size - 1u) / s->chunk_size;
        return (c > s->chunk_max) ? 0u : c;
    }
    if (s->line_bytes == 0u || len % s->line_bytes != 0u) {
        return 0u;
    }
    return len / s->line_bytes;
}

bool prn_frame_build(const prn_spec_t *s, prn_ctx_t *ctx,
                     const uint8_t *stream, uint32_t len, uint32_t index,
                     uint8_t *out, size_t *n)
{
    const uint32_t count = prn_frame_count(s, len);
    if (stream == NULL || index >= count) {
        return false;
    }
    if (s->chunk_size != 0u) {
        const uint32_t from = index * s->chunk_size;
        uint32_t k = len - from;
        if (k > s->chunk_size) {
            k = s->chunk_size;
        }
        ctx->data = stream + from;
        ctx->data_len = (uint16_t)k;
        ctx->data_pad = s->chunk_pad ? (uint16_t)(s->chunk_size - k) : 0u;
        ctx->pad_byte = s->chunk_pad_byte;
        ctx->var[PRN_V_IDX] = index;
        ctx->var[PRN_V_TOTAL] = count;
    } else {
        ctx->data = stream + (size_t)index * s->line_bytes;
        ctx->data_len = s->line_bytes;
        ctx->data_pad = 0u;
        ctx->var[PRN_V_LINE_NO] = index;
    }
    return prn_tmpl_render(s, s->frame, ctx, out, PRN_FRAME_MAX, n);
}

uint16_t prn_speed(const prn_spec_t *s, uint32_t input)
{
    if (s->nspeed == 0u) {
        return 0u;
    }
    for (unsigned i = 0; i < s->nspeed && i < PRN_SPEED_PAIRS; ++i) {
        if (input > s->speed_above[i]) {
            return s->speed_value[i];
        }
    }
    return s->speed_else;
}
