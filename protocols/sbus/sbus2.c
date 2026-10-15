/*
 * SPDX-License-Identifier: MIT
 */

#include "sbus2.h"

#include <string.h>

/* A byte on the wire: 12 bits at 10 us. */
#define BYTE_US 120u

/* Slots each sensor takes, by sbus2_sensor_t. */
static const uint8_t k_sensor_slots[SBUS2_SENSOR_COUNT] = {
    0u, /* none */
    1u, /* receiver */
    1u, /* temperature */
    1u, /* rpm */
    3u, /* altitude */
    2u, /* voltage */
    8u, /* gps */
    6u, /* servo */
    3u, /* current */
};

uint8_t sbus2_footer(unsigned group)
{
    return (uint8_t)(SBUS2_FOOTER_0 + 0x10u * (group % SBUS2_GROUPS));
}

int sbus2_footer_group(uint8_t footer)
{
    if ((footer & 0x0Fu) != SBUS2_FOOTER_0 || footer > SBUS2_FOOTER_3) {
        return -1;
    }
    return (int)(footer >> 4);
}

/* The 5-bit number backwards: bit 0 to bit 4, bit 4 to bit 0. */
static unsigned rev5(unsigned v)
{
    unsigned r = 0;

    for (unsigned i = 0; i < 5u; ++i) {
        r = (r << 1) | ((v >> i) & 1u);
    }
    return r;
}

uint8_t sbus2_slot_id(unsigned slot)
{
    return (uint8_t)(rev5(slot % SBUS2_SLOTS) * 8u + 3u);
}

int sbus2_id_slot(uint8_t id)
{
    if ((id & 0x07u) != 0x03u) {
        return -1;
    }
    return (int)rev5((unsigned)id >> 3);
}

void sbus2_timing_default(sbus2_timing_t *t)
{
    if (t == NULL) {
        return;
    }
    t->slot0_us = SBUS2_SLOT0_US;
    t->slot_us = SBUS2_SLOT_US;
    t->early_us = SBUS2_EARLY_US;
    t->late_us = SBUS2_LATE_US;
    t->byte_gap_us = SBUS2_BYTE_GAP_US;
}

bool sbus2_timing_ok(const sbus2_timing_t *t)
{
    if (t == NULL || t->slot_us == 0u) {
        return false;
    }
    /* Windows that overlap would give one record two slots; a slot 0 window
     * that starts before the footer ends would listen to the sender. */
    if ((uint32_t)t->early_us + t->late_us > t->slot_us
        || t->early_us > t->slot0_us) {
        return false;
    }
    /* The gap must keep the bytes of one record together and the records of
     * two neighbouring slots apart. */
    if (t->byte_gap_us <= BYTE_US
        || (uint32_t)t->byte_gap_us + 2u * BYTE_US >= t->slot_us) {
        return false;
    }
    /* The last record of a group has to end before the listening does. */
    return (uint32_t)t->slot0_us + 7u * t->slot_us + t->late_us
               + SBUS2_RECORD_BYTES * BYTE_US
           <= SBUS2_WINDOW_US;
}

/* Judge one record and, when it passes, take it into its slot. */
static void take(const sbus2_timing_t *t, const sbus2_capture_t *cap,
                 unsigned first, unsigned n, sbus2_window_t *out)
{
    if (n < SBUS2_RECORD_BYTES) {
        ++out->short_rec;
        return;
    }
    for (unsigned i = first; i < first + n; ++i) {
        if (!cap->byte[i].parity_ok || !cap->byte[i].stop_ok) {
            ++out->bad_byte;
            return;
        }
    }
    const int slot = sbus2_id_slot(cap->byte[first].data);

    if (slot < 0 || (unsigned)slot / SBUS2_GROUP_SLOTS != out->group) {
        ++out->wrong_id;
        return;
    }
    const unsigned k = (unsigned)slot % SBUS2_GROUP_SLOTS;
    const uint32_t start_ns =
        ((uint32_t)t->slot0_us + k * (uint32_t)t->slot_us) * 1000u;
    const uint32_t at = cap->byte[first].t_ns;

    if (at + (uint32_t)t->early_us * 1000u < start_ns) {
        ++out->early;
        return;
    }
    if (at >= start_ns + (uint32_t)t->late_us * 1000u) {
        ++out->late;
        return;
    }
    if (out->slot[k].taken) {
        ++out->duplicate;
        return;
    }
    out->slot[k].taken = true;
    out->slot[k].data[0] = cap->byte[first + 1u].data;
    out->slot[k].data[1] = cap->byte[first + 2u].data;
    out->slot[k].t_us = (uint16_t)((at + 500u) / 1000u);
    ++out->taken;
}

bool sbus2_window_decode(const sbus2_timing_t *t, uint8_t footer,
                         const sbus2_capture_t *cap, sbus2_window_t *out)
{
    sbus2_timing_t def;

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (t == NULL) {
        sbus2_timing_default(&def);
        t = &def;
    }
    const int group = sbus2_footer_group(footer);

    if (cap == NULL || group < 0 || !sbus2_timing_ok(t)) {
        return false;
    }
    out->group = (uint8_t)group;
    out->overflow = cap->overflow;

    const unsigned n = cap->n > SBUS2_CAPTURE_MAX ? SBUS2_CAPTURE_MAX : cap->n;
    const uint32_t gap_ns = (uint32_t)t->byte_gap_us * 1000u;
    unsigned first = 0;

    /* A record ends at its third byte or at a gap; the wire has no other
     * marker.  The difference is unsigned, so a byte stamped before the
     * one ahead of it also starts a record. */
    for (unsigned i = 1; i <= n; ++i) {
        if (i == n || i - first == SBUS2_RECORD_BYTES
            || cap->byte[i].t_ns - cap->byte[i - 1u].t_ns > gap_ns) {
            if (i > first) {
                take(t, cap, first, i - first, out);
            }
            first = i;
        }
    }
    return true;
}

void sbus2_table_reset(sbus2_table_t *tab)
{
    if (tab != NULL) {
        memset(tab, 0, sizeof(*tab));
    }
}

void sbus2_table_update(sbus2_table_t *tab, const sbus2_window_t *win)
{
    if (tab == NULL || win == NULL || win->group >= SBUS2_GROUPS) {
        return;
    }
    for (unsigned k = 0; k < SBUS2_GROUP_SLOTS; ++k) {
        sbus2_slot_t *s = &tab->slot[win->group * SBUS2_GROUP_SLOTS + k];
        const sbus2_slot_rec_t *r = &win->slot[k];

        if (r->taken) {
            s->data[0] = r->data[0];
            s->data[1] = r->data[1];
            s->t_us = r->t_us;
            s->age = 0;
            s->valid = true;
            ++s->records;
        } else if (s->valid) {
            if (s->age < 255u) {
                ++s->age;
            }
            ++s->missed;
        }
    }
    ++tab->windows;
    tab->taken += win->taken;
    tab->duplicate += win->duplicate;
    tab->wrong_id += win->wrong_id;
    tab->early += win->early;
    tab->late += win->late;
    tab->short_rec += win->short_rec;
    tab->bad_byte += win->bad_byte;
    tab->overflow += win->overflow ? 1u : 0u;
}

bool sbus2_slot_live(const sbus2_table_t *tab, unsigned slot)
{
    return tab != NULL && slot < SBUS2_SLOTS && tab->slot[slot].valid
           && tab->slot[slot].age < SBUS2_LIVE_WINDOWS;
}

unsigned sbus2_sensor_slots(unsigned sensor)
{
    return sensor < SBUS2_SENSOR_COUNT ? k_sensor_slots[sensor] : 0u;
}

/* A sensor at its default start slot, all its slots in one encoding. */
static void place(sbus2_map_t *map, unsigned start, unsigned sensor,
                  unsigned decode)
{
    for (unsigned p = 0; p < k_sensor_slots[sensor]; ++p) {
        map->slot[start + p].sensor = (uint8_t)sensor;
        map->slot[start + p].part = (uint8_t)p;
        map->slot[start + p].decode = (uint8_t)decode;
    }
}

void sbus2_map_default(sbus2_map_t *map)
{
    if (map == NULL) {
        return;
    }
    memset(map, 0, sizeof(*map));
    /* Futaba's default slots: T16IZ SUPER manual and the sensor manuals. */
    place(map, 0u, SBUS2_SENSOR_RECEIVER, SBUS2_DEC_VOLT);
    place(map, 1u, SBUS2_SENSOR_TEMP, SBUS2_DEC_TEMP);
    place(map, 2u, SBUS2_SENSOR_RPM, SBUS2_DEC_RAW);
    place(map, 3u, SBUS2_SENSOR_ALTITUDE, SBUS2_DEC_RAW);
    place(map, 6u, SBUS2_SENSOR_VOLTAGE, SBUS2_DEC_RAW);
    place(map, 8u, SBUS2_SENSOR_GPS, SBUS2_DEC_RAW);
    place(map, 16u, SBUS2_SENSOR_SERVO, SBUS2_DEC_RAW);
    place(map, 24u, SBUS2_SENSOR_CURRENT, SBUS2_DEC_RAW);
}

bool sbus2_map_set(sbus2_map_t *map, unsigned slot, unsigned sensor,
                   unsigned part, unsigned decode)
{
    if (map == NULL || slot >= SBUS2_SLOTS || sensor >= SBUS2_SENSOR_COUNT
        || decode >= SBUS2_DEC_COUNT) {
        return false;
    }
    const unsigned parts = k_sensor_slots[sensor] == 0u ? 1u
                                                        : k_sensor_slots[sensor];

    if (part >= parts) {
        return false;
    }
    map->slot[slot].sensor = (uint8_t)sensor;
    map->slot[slot].part = (uint8_t)part;
    map->slot[slot].decode = (uint8_t)decode;
    return true;
}

void sbus2_value(unsigned decode, const uint8_t *data, sbus2_value_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (data == NULL) {
        return;
    }
    out->raw[0] = data[0];
    out->raw[1] = data[1];
    switch (decode) {
    case SBUS2_DEC_VOLT:
        out->unit = SBUS2_UNIT_DECIVOLT;
        out->value = data[1];
        break;
    case SBUS2_DEC_TEMP:
        out->unit = SBUS2_UNIT_DEGC;
        out->value = (int32_t)data[0] - 100;
        break;
    default:
        out->unit = SBUS2_UNIT_RAW;
        out->value = (int32_t)(((uint32_t)data[0] << 8) | data[1]);
        break;
    }
}

/* Decimal digits of @p v, at least @p min_digits, at @p p; returns the end. */
static char *put_uint(char *p, uint32_t v, unsigned min_digits)
{
    char tmp[10];
    unsigned n = 0;

    do {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u || n < min_digits);
    while (n > 0u) {
        *p++ = tmp[--n];
    }
    return p;
}

static char *put_hex(char *p, uint8_t b)
{
    static const char k_hex[] = "0123456789ABCDEF";

    *p++ = k_hex[b >> 4];
    *p++ = k_hex[b & 0x0Fu];
    return p;
}

size_t sbus2_value_text(const sbus2_value_t *v, char *buf, size_t len)
{
    if (buf == NULL) {
        return 0;
    }
    if (len < SBUS2_VALUE_TEXT || v == NULL) {
        if (len > 0u) {
            buf[0] = '\0';
        }
        return 0;
    }
    char *p = buf;

    if (v->unit == SBUS2_UNIT_DECIVOLT || v->unit == SBUS2_UNIT_DEGC) {
        const uint32_t mag = v->value < 0 ? 0u - (uint32_t)v->value
                                          : (uint32_t)v->value;

        if (v->value < 0) {
            *p++ = '-';
        }
        if (v->unit == SBUS2_UNIT_DECIVOLT) {
            p = put_uint(p, mag / 10u, 1u);
            *p++ = '.';
            p = put_uint(p, mag % 10u, 1u);
            *p++ = ' ';
            *p++ = 'V';
        } else {
            p = put_uint(p, mag, 1u);
            *p++ = ' ';
            *p++ = 'C';
        }
    } else {
        p = put_hex(p, v->raw[0]);
        *p++ = ' ';
        p = put_hex(p, v->raw[1]);
    }
    *p = '\0';
    return (size_t)(p - buf);
}
