/*
 * Reading a board drawing. See link_drawing.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_drawing.h"

#include <stddef.h>
#include <string.h>

static uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

static void pad_of(const uint8_t *p, link_draw_pad_t *out)
{
    out->pad   = p[0];
    out->kind  = p[1];
    out->value = p[2];
    out->flags = p[3];
    out->x     = u16_at(p + 4);
    out->y     = u16_at(p + 6);
}

static void shape_of(const uint8_t *p, link_draw_shape_t *out)
{
    out->kind   = p[0];
    out->param  = p[1];
    out->colour = u16_at(p + 2);
    out->a      = u16_at(p + 4);
    out->b      = u16_at(p + 6);
    out->c      = u16_at(p + 8);
    out->d      = u16_at(p + 10);
}

/* [lo, hi] lies inside [0, limit]: no part of it past either edge.  In 32
 * bits, so lo - r below zero is a negative number and not a wrap. */
static bool within(int32_t lo, int32_t hi, uint16_t limit)
{
    return lo >= 0 && hi <= (int32_t)limit;
}

static bool pad_ok(const link_drawing_t *d, const link_draw_pad_t *p)
{
    if (p->pad == 0u || p->pad > 63u) {
        return false;
    }
    if (p->kind != (uint8_t)LINK_PAD_GROUND
        && p->kind != (uint8_t)LINK_PAD_POWER
        && p->kind != (uint8_t)LINK_PAD_OTHER
        && p->kind != (uint8_t)LINK_DRAW_PAD_GPIO) {
        return false;
    }
    if (p->kind != (uint8_t)LINK_DRAW_PAD_GPIO
        && p->kind != (uint8_t)LINK_PAD_POWER && p->value != 0u) {
        return false;      /* a ground or an OTHER pad carries no number */
    }
    if ((p->flags & (uint8_t)~LINK_DRAW_PAD_SQUARE) != 0u) {
        return false;
    }
    const int32_t r = d->ring_r;
    return within((int32_t)p->x - r, (int32_t)p->x + r, d->width)
           && within((int32_t)p->y - r, (int32_t)p->y + r, d->height);
}

static bool shape_ok(const link_drawing_t *d, const link_draw_shape_t *s)
{
    const int32_t a = s->a, b = s->b, c = s->c, e = s->d;
    switch (s->kind) {
    case LINK_DRAW_RECT:
    case LINK_DRAW_RRECT:
        if (c == 0 || e == 0) {
            return false;
        }
        if (s->kind == (uint8_t)LINK_DRAW_RECT && s->param != 0u) {
            return false;
        }
        /* A corner no larger than half the shorter side: a rounded
         * rectangle that is all corner is a disc drawn the wrong way. */
        if (2 * (int32_t)s->param > ((c < e) ? c : e)) {
            return false;
        }
        return within(a, a + c, d->width) && within(b, b + e, d->height);
    case LINK_DRAW_DISC:
        if (s->param != 0u || c == 0 || e != 0) {
            return false;
        }
        return within(a - c, a + c, d->width)
               && within(b - c, b + c, d->height);
    case LINK_DRAW_LINE: {
        if (s->param == 0u) {
            return false;
        }
        const int32_t h = ((int32_t)s->param + 1) / 2;
        const int32_t x0 = (a < c) ? a : c, x1 = (a < c) ? c : a;
        const int32_t y0 = (b < e) ? b : e, y1 = (b < e) ? e : b;
        return within(x0 - h, x1 + h, d->width)
               && within(y0 - h, y1 + h, d->height);
    }
    default:
        return false;
    }
}

static bool refuse(link_drawing_t *out, const char **why, const char *reason)
{
    memset(out, 0, sizeof(*out));
    if (why != NULL) {
        *why = reason;
    }
    return false;
}

bool link_drawing_parse(link_drawing_t *out, const uint8_t *data,
                        uint32_t bytes, const char **why)
{
    if (why != NULL) {
        *why = "";
    }
    if (out == NULL) {
        return false;
    }
    if (data == NULL || bytes < LINK_DRAW_HEAD_BYTES) {
        return refuse(out, why, "shorter than a drawing header");
    }
    if (data[0] != (uint8_t)LINK_DRAW_VERSION) {
        return refuse(out, why, "a drawing version this build does not read");
    }
    link_drawing_t d;
    memset(&d, 0, sizeof(d));
    d.data   = data;
    d.bytes  = bytes;
    d.pads   = data[1];
    d.shapes = u16_at(data + 2);
    d.width  = u16_at(data + 4);
    d.height = u16_at(data + 6);
    d.colour = u16_at(data + 8);
    d.ring   = u16_at(data + 10);
    d.corner = data[12];
    d.ring_r = data[13];
    d.hole_r = data[14];

    if (data[15] != 0u) {
        return refuse(out, why, "a reserved header byte is not zero");
    }
    if (d.pads == 0u || d.pads > LINK_DRAW_MAX_PADS
        || d.shapes > LINK_DRAW_MAX_SHAPES) {
        return refuse(out, why, "more pads or shapes than a drawing holds");
    }
    const uint32_t want = LINK_DRAW_HEAD_BYTES
                          + (uint32_t)d.pads * LINK_DRAW_PAD_BYTES
                          + (uint32_t)d.shapes * LINK_DRAW_SHAPE_BYTES;
    if (bytes != want) {
        return refuse(out, why,
                      "the length is not the header, pads and shapes");
    }
    if (d.width == 0u || d.height == 0u
        || 2u * (unsigned)d.corner > (unsigned)((d.width < d.height)
                                                ? d.width : d.height)) {
        return refuse(out, why, "an outline that is not one");
    }
    if (d.ring_r == 0u || d.hole_r >= d.ring_r) {
        return refuse(out, why,
                      "a pad ring with no ring left around its hole");
    }

    uint8_t last = 0u;
    for (uint8_t i = 0; i < d.pads; ++i) {
        link_draw_pad_t p;
        pad_of(data + LINK_DRAW_HEAD_BYTES + (size_t)i * LINK_DRAW_PAD_BYTES,
               &p);
        if (!pad_ok(&d, &p)) {
            return refuse(out, why, "a pad out of place or of no known kind");
        }
        if (p.pad <= last) {
            return refuse(out, why, "the pads are not in ascending order");
        }
        last = p.pad;
    }
    const uint8_t *shapes = data + LINK_DRAW_HEAD_BYTES
                            + (size_t)d.pads * LINK_DRAW_PAD_BYTES;
    for (uint16_t i = 0; i < d.shapes; ++i) {
        link_draw_shape_t s;
        shape_of(shapes + (size_t)i * LINK_DRAW_SHAPE_BYTES, &s);
        if (!shape_ok(&d, &s)) {
            return refuse(out, why,
                          "a shape outside the outline or of no known kind");
        }
    }
    *out = d;
    return true;
}

bool link_drawing_pad_at(const link_drawing_t *d, uint8_t i,
                         link_draw_pad_t *out)
{
    if (d == NULL || out == NULL || d->data == NULL || i >= d->pads) {
        return false;
    }
    pad_of(d->data + LINK_DRAW_HEAD_BYTES + (size_t)i * LINK_DRAW_PAD_BYTES,
           out);
    return true;
}

bool link_drawing_find_pad(const link_drawing_t *d, uint8_t pad,
                           link_draw_pad_t *out)
{
    if (d == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < d->pads; ++i) {
        link_draw_pad_t p;
        if (link_drawing_pad_at(d, i, &p) && p.pad == pad) {
            if (out != NULL) {
                *out = p;
            }
            return true;
        }
    }
    return false;
}

bool link_drawing_shape_at(const link_drawing_t *d, uint16_t i,
                           link_draw_shape_t *out)
{
    if (d == NULL || out == NULL || d->data == NULL || i >= d->shapes) {
        return false;
    }
    shape_of(d->data + LINK_DRAW_HEAD_BYTES
                 + (size_t)d->pads * LINK_DRAW_PAD_BYTES
                 + (size_t)i * LINK_DRAW_SHAPE_BYTES,
             out);
    return true;
}
