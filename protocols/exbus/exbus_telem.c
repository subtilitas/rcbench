/*
 * EX telemetry as an EX Bus reply carries it.  See exbus_telem.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "exbus_telem.h"

#include <string.h>

/* The items of the longest block fit the value array: 2 bytes is the
 * shortest item. */
_Static_assert(EXBUS_TM_MAX_VALUES * 2u >= EXBUS_TM_BLOCK_MAX - EXBUS_TM_FRAME,
               "a block holds more items than the array");

/* The offset of the first item: mark, kind and length, 4 serial bytes,
 * reserved byte. */
#define ITEMS_AT 7u

uint8_t exbus_crc8(const uint8_t *data, size_t n)
{
    uint8_t crc = 0;

    for (size_t i = 0; data != NULL && i < n; ++i) {
        crc = (uint8_t)(crc ^ data[i]);
        for (unsigned b = 0; b < 8u; ++b) {
            crc = (uint8_t)(((unsigned)crc << 1)
                            ^ ((crc & 0x80u) != 0u ? 0x07u : 0u));
        }
    }
    return crc;
}

/* The value bytes of data type @p type. */
static uint8_t type_size(uint8_t type)
{
    return type == 0u ? 1u : (uint8_t)((type >> 2) + 2u);
}

/* Decode the @p v->size bytes in v->raw by v->type. */
static void decode_value(exbus_tm_value_t *v)
{
    const uint8_t top = v->raw[v->size - 1u];
    const uint8_t point = (uint8_t)((top >> 5) & 3u);
    uint32_t bits = (uint32_t)(top & 0x1Fu);

    for (unsigned i = v->size - 1u; i > 0u; --i) {
        bits = (bits << 8) | v->raw[i - 1u];
    }
    switch (v->type) {
    case 0u:
    case 1u:
    case 4u:
    case 8u:
        v->form = EXBUS_TM_NUMBER;
        v->decimals = point;
        v->negative = (uint8_t)(top >> 7);
        v->value = v->negative != 0u ? -(int32_t)bits : (int32_t)bits;
        break;
    case 5u:
        v->form = (point & 1u) != 0u ? EXBUS_TM_DATE : EXBUS_TM_TIME;
        v->part[0] = (uint8_t)(top & 0x1Fu);
        v->part[1] = v->raw[1];
        v->part[2] = v->raw[0];
        break;
    case 9u:
        v->form = (point & 1u) != 0u ? EXBUS_TM_LONGITUDE : EXBUS_TM_LATITUDE;
        v->negative = (uint8_t)(point >> 1);
        v->value = (int32_t)bits;
        break;
    default:
        v->form = EXBUS_TM_RAW;
        break;
    }
}

/* Copy @p n bytes as a NUL-terminated string. */
static void take_text(char *dst, const uint8_t *src, size_t n)
{
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static exbus_tm_err_t parse_data(const uint8_t *p, size_t n,
                                 exbus_tm_packet_t *out)
{
    size_t i = 0;

    /* The count bound never ends the loop (the assertion above); it keeps
     * the index visibly inside the array. */
    while (i < n && out->n_values < EXBUS_TM_MAX_VALUES) {
        exbus_tm_value_t *v = &out->values[out->n_values];
        const uint8_t head = p[i++];

        v->id = (uint8_t)(head >> 4);
        v->type = (uint8_t)(head & 0x0Fu);
        if (v->id == 0u) {
            if (i >= n) {
                return EXBUS_TM_ERR_ITEM;
            }
            v->id = p[i++];
        }
        v->size = type_size(v->type);
        if (n - i < v->size) {
            return EXBUS_TM_ERR_ITEM;
        }
        memcpy(v->raw, p + i, v->size);
        i += v->size;
        decode_value(v);
        out->n_values++;
    }
    return i == n ? EXBUS_TM_OK : EXBUS_TM_ERR_ITEM;
}

static exbus_tm_err_t parse_text(const uint8_t *p, size_t n,
                                 exbus_tm_packet_t *out)
{
    if (n < 2u) {
        return EXBUS_TM_ERR_ITEM;
    }
    const uint8_t label = (uint8_t)(p[1] >> 3);
    const uint8_t unit = (uint8_t)(p[1] & 7u);

    if (2u + (size_t)label + unit != n) {
        return EXBUS_TM_ERR_ITEM;
    }
    out->text_id = p[0];
    out->label_len = label;
    out->unit_len = unit;
    take_text(out->label, p + 2, label);
    take_text(out->unit, p + 2 + label, unit);
    return EXBUS_TM_OK;
}

static exbus_tm_err_t parse_message(const uint8_t *p, size_t n,
                                    exbus_tm_packet_t *out)
{
    if (n < 2u) {
        return EXBUS_TM_ERR_ITEM;
    }
    const uint8_t len = (uint8_t)(p[1] & 0x1Fu);

    if (2u + (size_t)len != n) {
        return EXBUS_TM_ERR_ITEM;
    }
    out->msg_type = p[0];
    out->msg_class = (uint8_t)(p[1] >> 5);
    out->msg_len = len;
    take_text(out->message, p + 2, len);
    return EXBUS_TM_OK;
}

exbus_tm_err_t exbus_tm_parse(const uint8_t *block, size_t n,
                              exbus_tm_packet_t *out)
{
    exbus_tm_err_t err;

    if (block == NULL || out == NULL) {
        return EXBUS_TM_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (n < EXBUS_TM_FRAME) {
        return EXBUS_TM_ERR_SHORT;
    }
    if (n > EXBUS_TM_BLOCK_MAX) {
        return EXBUS_TM_ERR_TOO_LONG;
    }
    if ((block[0] & 0x0Fu) != 0x0Fu) {
        return EXBUS_TM_ERR_MARK;
    }
    if ((size_t)(block[1] & 0x3Fu) + 2u != n) {
        return EXBUS_TM_ERR_LENGTH;
    }
    if (exbus_crc8(block + 1, n - 2u) != block[n - 1u]) {
        return EXBUS_TM_ERR_CRC;
    }
    out->kind = (uint8_t)(block[1] >> 6);
    out->manufacturer = (uint16_t)(block[2] | ((uint16_t)block[3] << 8));
    out->device = (uint16_t)(block[4] | ((uint16_t)block[5] << 8));
    out->reserved = block[6];

    const uint8_t *items = block + ITEMS_AT;
    const size_t n_items = n - ITEMS_AT - 1u;

    switch (out->kind) {
    case EXBUS_TM_DATA:
        err = parse_data(items, n_items, out);
        break;
    case EXBUS_TM_TEXT:
        err = parse_text(items, n_items, out);
        break;
    case EXBUS_TM_MESSAGE:
        err = parse_message(items, n_items, out);
        break;
    default:
        err = EXBUS_TM_ERR_KIND;
        break;
    }
    if (err != EXBUS_TM_OK) {
        memset(out, 0, sizeof(*out));
    }
    return err;
}

void exbus_tm_table_init(exbus_tm_table_t *t)
{
    if (t != NULL) {
        memset(t, 0, sizeof(*t));
    }
}

/* The sensor with @p id, made when there is room; NULL when there is
 * none. */
static exbus_tm_sensor_t *sensor_for(exbus_tm_table_t *t, uint8_t id)
{
    for (unsigned i = 0; i < t->n; ++i) {
        if (t->sensors[i].id == id) {
            return &t->sensors[i];
        }
    }
    if (t->n >= EXBUS_TM_MAX_SENSORS) {
        t->full++;
        return NULL;
    }
    exbus_tm_sensor_t *s = &t->sensors[t->n++];

    memset(s, 0, sizeof(*s));
    s->id = id;
    return s;
}

void exbus_tm_table_update(exbus_tm_table_t *t, const exbus_tm_packet_t *p,
                           uint32_t now_us)
{
    if (t == NULL || p == NULL) {
        return;
    }
    if (t->has_device != 0u
        && (t->manufacturer != p->manufacturer || t->device != p->device)) {
        const uint32_t changes = t->device_changes + 1u;
        const uint32_t full = t->full;

        memset(t, 0, sizeof(*t));
        t->device_changes = changes;
        t->full = full;
    }
    t->has_device = 1;
    t->manufacturer = p->manufacturer;
    t->device = p->device;
    t->packets++;

    if (p->kind == EXBUS_TM_TEXT) {
        exbus_tm_sensor_t *s = sensor_for(t, p->text_id);

        if (s != NULL) {
            s->has_label = 1;
            s->label_len = p->label_len;
            s->unit_len = p->unit_len;
            memcpy(s->label, p->label, sizeof(s->label));
            memcpy(s->unit, p->unit, sizeof(s->unit));
        }
    } else if (p->kind == EXBUS_TM_DATA) {
        for (unsigned i = 0; i < p->n_values && i < EXBUS_TM_MAX_VALUES; ++i) {
            exbus_tm_sensor_t *s = sensor_for(t, p->values[i].id);

            if (s != NULL) {
                s->has_value = 1;
                s->value = p->values[i];
                s->value_us = now_us;
                s->updates++;
            }
        }
    } else if (p->kind == EXBUS_TM_MESSAGE) {
        t->has_message = 1;
        t->msg_type = p->msg_type;
        t->msg_class = p->msg_class;
        t->msg_len = p->msg_len;
        memcpy(t->message, p->message, sizeof(t->message));
        t->message_us = now_us;
    }
}

const exbus_tm_sensor_t *exbus_tm_table_find(const exbus_tm_table_t *t,
                                             uint8_t id)
{
    for (unsigned i = 0; t != NULL && i < t->n; ++i) {
        if (t->sensors[i].id == id) {
            return &t->sensors[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------ formatting */

typedef struct {
    char *buf;
    size_t size;
    size_t len;     /* characters of the whole text */
} text_t;

static void put_char(text_t *t, char c)
{
    if (t->size != 0u && t->len < t->size - 1u) {
        t->buf[t->len] = c;
        t->buf[t->len + 1u] = '\0';
    }
    t->len++;
}

static void put_str(text_t *t, const char *s)
{
    while (*s != '\0') {
        put_char(t, *s++);
    }
}

/* @p v in decimal, at least @p width digits. */
static void put_uint(text_t *t, uint32_t v, unsigned width)
{
    char digits[10];
    unsigned n = 0;

    do {
        digits[n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0u);
    while (n < width && n < sizeof(digits)) {
        digits[n++] = '0';
    }
    while (n > 0u) {
        put_char(t, digits[--n]);
    }
}

static void put_hex(text_t *t, uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";

    put_char(t, hex[b >> 4]);
    put_char(t, hex[b & 0x0Fu]);
}

static void put_number(text_t *t, const exbus_tm_value_t *v)
{
    static const uint32_t scale[4] = { 1u, 10u, 100u, 1000u };
    const uint32_t mag = v->value < 0 ? 0u - (uint32_t)v->value
                                      : (uint32_t)v->value;
    const unsigned d = v->decimals & 3u;

    if (v->negative != 0u) {
        put_char(t, '-');
    }
    put_uint(t, mag / scale[d], 1);
    if (d > 0u) {
        put_char(t, '.');
        put_uint(t, mag % scale[d], d);
    }
}

size_t exbus_tm_format(const exbus_tm_value_t *v, char *buf, size_t size)
{
    text_t t;

    t.buf = buf;
    t.size = buf == NULL ? 0u : size;
    t.len = 0;
    if (t.size != 0u) {
        buf[0] = '\0';
    }
    if (v == NULL) {
        return 0;
    }
    switch (v->form) {
    case EXBUS_TM_NUMBER:
        put_number(&t, v);
        break;
    case EXBUS_TM_TIME:
        put_uint(&t, v->part[0], 2);
        put_char(&t, ':');
        put_uint(&t, v->part[1], 2);
        put_char(&t, ':');
        put_uint(&t, v->part[2], 2);
        break;
    case EXBUS_TM_DATE:
        put_uint(&t, 2000u + v->part[0], 4);
        put_char(&t, '-');
        put_uint(&t, v->part[1], 2);
        put_char(&t, '-');
        put_uint(&t, v->part[2], 2);
        break;
    case EXBUS_TM_LATITUDE:
    case EXBUS_TM_LONGITUDE:
        if (v->form == EXBUS_TM_LATITUDE) {
            put_char(&t, v->negative != 0u ? 'S' : 'N');
        } else {
            put_char(&t, v->negative != 0u ? 'W' : 'E');
        }
        put_str(&t, " raw ");
        put_uint(&t, (uint32_t)v->value, 1);
        break;
    default:
        put_str(&t, "raw");
        for (unsigned i = 0; i < v->size && i < EXBUS_TM_VALUE_MAX; ++i) {
            put_char(&t, ' ');
            put_hex(&t, v->raw[i]);
        }
        break;
    }
    return t.len;
}
