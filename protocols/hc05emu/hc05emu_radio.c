/**
 * @file
 * @brief The HC-05 emulator's side of the radio: the addresses, the
 *        inquiry and its +INQ: lines, the results of a name read and a
 *        pairing, and the data-mode link with its search, connects and
 *        retries.
 *
 * An address is NAP:UAP:LAP (the non-significant, upper and lower address
 * parts): 6 bytes, NAP the first 2, UAP the third, LAP the last 3.  +INQ:
 * prints it in upper-case hex with colons and without leading zeros,
 * `98D3:31:FD5C81`; a set command takes it with commas.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "hc05emu.h"
#include "hc05emu_int.h"

/** @brief The hex digits, upper case. */
static const char k_hex[] = "0123456789ABCDEF";

/**
 * @brief Write @p v in hex without leading zeros to @p out.
 * @param out The text; moved past what was written.
 * @param v   The number.
 */
static void put_hex(char **out, uint32_t v)
{
    char buf[8];
    size_t n = 0;
    do {
        buf[n++] = k_hex[v & 15u];
        v >>= 4;
    } while (v != 0u);
    while (n > 0u) {
        *(*out)++ = buf[--n];
    }
}

void hc05emu_addr_text(const uint8_t *addr, char *out)
{
    put_hex(&out, ((uint32_t)addr[0] << 8) | addr[1]);
    *out++ = ':';
    put_hex(&out, addr[2]);
    *out++ = ':';
    put_hex(&out, ((uint32_t)addr[3] << 16) | ((uint32_t)addr[4] << 8)
                      | addr[5]);
    *out = '\0';
}

/**
 * @brief The value of a hex digit.
 * @param c The character.
 * @return 0 to 15, or -1 when @p c is no hex digit.
 */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

uint8_t hc05emu_addr_read(const char **p, uint8_t *addr)
{
    static const unsigned max[3] = { 4u, 2u, 6u };
    static const uint8_t code[3] = { EMU_E_NAP, EMU_E_UAP, EMU_E_LAP };
    uint32_t part[3] = { 0 };
    char sep = '\0';
    const char *s = *p;
    if (hex_digit(*s) < 0) {
        return *s == '\0' ? EMU_E_NO_ADDR : EMU_E_CMD;
    }
    for (unsigned i = 0; i < 3u; ++i) {
        unsigned n = 0;
        if (i > 0u) {
            if ((*s != ',' && *s != ':') || (sep != '\0' && *s != sep)) {
                return EMU_E_CMD;
            }
            sep = *s++;
        }
        for (int d = hex_digit(*s); d >= 0; d = hex_digit(*++s)) {
            part[i] = (part[i] << 4) | (uint32_t)d;
            ++n;
        }
        if (n == 0u) {
            return EMU_E_CMD;
        }
        if (n > max[i]) {
            return code[i];
        }
    }
    addr[0] = (uint8_t)(part[0] >> 8);
    addr[1] = (uint8_t)part[0];
    addr[2] = (uint8_t)part[1];
    addr[3] = (uint8_t)(part[2] >> 16);
    addr[4] = (uint8_t)(part[2] >> 8);
    addr[5] = (uint8_t)part[2];
    *p = s;
    return 0u;
}

int hc05emu_find(const hc05emu_t *e, const uint8_t *addr)
{
    for (uint8_t i = 0; i < e->found_n; ++i) {
        if (memcmp(e->found[i].addr, addr, HC05EMU_ADDR_LEN) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void hc05emu_copy_name(char *dst, const char *src)
{
    size_t n = 0;
    for (; src[n] != '\0' && n + 1u < HC05EMU_NAME_MAX; ++n) {
        const char c = src[n];
        dst[n] = (c < 0x20 || c > 0x7E) ? '?' : c;
    }
    dst[n] = '\0';
}

const char *hc05emu_state_word(const hc05emu_t *e)
{
    /* AT mode runs no connect, and a command that came while the radio
     * worked is answered after it: CONNECTING and INQUIRING never show. */
    if (e->link == HC05EMU_LINK_UP) {
        return "CONNECTED";
    }
    return e->inited ? "PAIRABLE" : "INITIALIZED";
}

void hc05emu_link_stop(hc05emu_t *e)
{
    if (e->link == HC05EMU_LINK_SEARCH || e->link == HC05EMU_LINK_CONNECTING
        || e->link == HC05EMU_LINK_UP) {
        (void)hc05emu_q(e, HC05EMU_ACT_STOP);
    }
    e->link = HC05EMU_LINK_IDLE;
}

/**
 * @brief Ask the radio to open the link to @p addr.
 * @param e      The machine.
 * @param addr   The device.
 * @param now_ms The clock.
 */
static void connect(hc05emu_t *e, const uint8_t *addr, uint32_t now_ms)
{
    hc05emu_act_t *a = hc05emu_q(e, HC05EMU_ACT_CONNECT);
    if (a != NULL) {
        memcpy(a->addr, addr, sizeof a->addr);
        memcpy(a->pin, e->cfg.pin, sizeof a->pin);
        a->timeout_ms = HC05EMU_CONNECT_MS;
    }
    memmove(e->target, addr, sizeof e->target);
    e->link = HC05EMU_LINK_CONNECTING;
    e->link_ms = now_ms;
}

void hc05emu_link_start(hc05emu_t *e, uint32_t now_ms)
{
    e->link = HC05EMU_LINK_IDLE;
    if (e->mode != HC05EMU_MODE_DATA) {
        return;
    }
    e->inited = true;
    if (e->cfg.role != 1u) {
        return; /* a slave: this bridge takes no connection */
    }
    if (e->cfg.cmode == 0u && e->cfg.bound != 0u) {
        connect(e, e->cfg.bind, now_ms);
    } else if (e->cfg.cmode == 1u) {
        hc05emu_act_t *a = hc05emu_q(e, HC05EMU_ACT_INQ);
        if (a != NULL) {
            a->units = HC05EMU_ANY_UNITS;
            a->max = HC05EMU_ANY_MAX;
        }
        e->found_n = 0;
        e->link = HC05EMU_LINK_SEARCH;
        e->link_ms = now_ms;
    }
}

/**
 * @brief Try CMODE 1's next device, or wait for the retry after the last.
 * @param e      The machine.
 * @param now_ms The clock.
 */
static void any_next(hc05emu_t *e, uint32_t now_ms)
{
    if (e->any_i < e->found_n) {
        connect(e, e->found[e->any_i++].addr, now_ms);
        return;
    }
    e->link = HC05EMU_LINK_WAIT;
    e->link_ms = now_ms;
}

void hc05emu_link_failed(hc05emu_t *e, uint32_t now_ms)
{
    const bool was_try = e->link == HC05EMU_LINK_CONNECTING;
    if (e->mode != HC05EMU_MODE_DATA) {
        e->link = HC05EMU_LINK_IDLE;
    } else if (was_try && e->cfg.cmode == 1u) {
        any_next(e, now_ms);
    } else {
        e->link = HC05EMU_LINK_WAIT;
        e->link_ms = now_ms;
    }
}

/**
 * @brief The RSSI as an order: an unknown one below every measured one.
 * @param d The device.
 * @return The key, larger for a stronger signal.
 */
static int32_t rssi_key(const hc05emu_dev_t *d)
{
    return d->rssi == INT16_MAX ? INT16_MIN - 1 : (int32_t)d->rssi;
}

void hc05emu_search_end(hc05emu_t *e, uint32_t now_ms)
{
    /* Insertion sort: at most HC05EMU_FOUND_MAX devices; equal ones keep
     * the order they were found in. */
    for (uint8_t i = 1; i < e->found_n; ++i) {
        const hc05emu_dev_t d = e->found[i];
        uint8_t j = i;
        while (j > 0u && rssi_key(&e->found[j - 1u]) < rssi_key(&d)) {
            e->found[j] = e->found[j - 1u];
            --j;
        }
        e->found[j] = d;
    }
    e->any_i = 0;
    any_next(e, now_ms);
}

void hc05emu_inq_start(hc05emu_t *e, uint32_t now_ms)
{
    hc05emu_link_stop(e);
    e->found_n = 0;
    e->inq_told = 0;
    hc05emu_act_t *a = hc05emu_q(e, HC05EMU_ACT_INQ);
    if (a != NULL) {
        a->units = e->cfg.inq_units;
        a->max = e->cfg.inq_max;
    }
    e->op = HC05EMU_OP_INQ;
    e->op_ms = now_ms;
    e->op_wait = (uint32_t)e->cfg.inq_units * HC05EMU_INQ_UNIT_MS;
}

/**
 * @brief Keep a device the inquiry reported, once per address.
 * @param e    The machine.
 * @param addr Its address.
 * @param cod  Its class of device.
 * @param rssi Its RSSI.
 * @param name Its name, or NULL.
 * @return The device when it is new; NULL for one seen before (its name
 *         taken when it had none) and when the list is full.
 */
static hc05emu_dev_t *keep(hc05emu_t *e, const uint8_t *addr, uint32_t cod,
                           int16_t rssi, const char *name)
{
    const int i = hc05emu_find(e, addr);
    hc05emu_dev_t *d = i >= 0 ? &e->found[i] : NULL;
    if (d == NULL && e->found_n < HC05EMU_FOUND_MAX) {
        d = &e->found[e->found_n++];
        memcpy(d->addr, addr, sizeof d->addr);
        d->cod = cod & 0xFFFFFFu;
        d->rssi = rssi;
        d->named = false;
        d->name[0] = '\0';
    } else if (d != NULL) {
        if (name != NULL && !d->named) {
            hc05emu_copy_name(d->name, name);
            d->named = true;
        }
        return NULL;
    }
    if (d != NULL && name != NULL) {
        hc05emu_copy_name(d->name, name);
        d->named = true;
    }
    return d;
}

void hc05emu_found(hc05emu_t *e, const uint8_t *addr, uint32_t cod,
                   int16_t rssi, const char *name, uint32_t now_ms)
{
    if (e == NULL || addr == NULL) {
        return;
    }
    if (e->mode == HC05EMU_MODE_DATA && e->link == HC05EMU_LINK_SEARCH) {
        (void)keep(e, addr, cod, rssi, name);
        return;
    }
    if (e->mode != HC05EMU_MODE_AT || e->op != HC05EMU_OP_INQ) {
        return;
    }
    const hc05emu_dev_t *d = keep(e, addr, cod, rssi, name);
    if (d == NULL) {
        return;
    }
    char line[40] = "+INQ:";
    char *p = line + 5;
    hc05emu_addr_text(d->addr, p);
    p += strlen(p);
    *p++ = ',';
    put_hex(&p, d->cod);
    if (e->cfg.inq_mode != 0u) {
        *p++ = ',';
        put_hex(&p, d->rssi == INT16_MAX ? 0x7FFFu
                                         : (uint32_t)(uint16_t)d->rssi);
    }
    *p = '\0';
    hc05emu_say(e, line);
    if (++e->inq_told >= e->cfg.inq_max) {
        hc05emu_say(e, "OK");
        (void)hc05emu_q(e, HC05EMU_ACT_STOP);
        e->op = HC05EMU_OP_NONE;
    }
    (void)now_ms;
}

void hc05emu_inq_done(hc05emu_t *e, uint32_t now_ms)
{
    if (e == NULL) {
        return;
    }
    if (e->mode == HC05EMU_MODE_DATA && e->link == HC05EMU_LINK_SEARCH) {
        hc05emu_search_end(e, now_ms);
    } else if (e->mode == HC05EMU_MODE_AT && e->op == HC05EMU_OP_INQ) {
        hc05emu_say(e, "OK");
        e->op = HC05EMU_OP_NONE;
    }
}

/**
 * @brief Whether the command waiting is @p op for @p addr.
 * @param e    The machine.
 * @param op   The command.
 * @param addr The device of a result; NULL is none.
 * @return true when the result is that command's.
 */
static bool waits_for(const hc05emu_t *e, hc05emu_op_t op,
                      const uint8_t *addr)
{
    return e->mode == HC05EMU_MODE_AT && e->op == op && addr != NULL
           && memcmp(e->target, addr, HC05EMU_ADDR_LEN) == 0;
}

void hc05emu_name_done(hc05emu_t *e, const uint8_t *addr, const char *name,
                       uint32_t now_ms)
{
    (void)now_ms;
    if (e == NULL || !waits_for(e, HC05EMU_OP_NAME, addr)) {
        return;
    }
    e->op = HC05EMU_OP_NONE;
    if (name == NULL) {
        hc05emu_say(e, "FAIL");
        return;
    }
    char line[7u + HC05EMU_NAME_MAX] = "+RNAME:";
    hc05emu_copy_name(line + 7, name);
    const int i = hc05emu_find(e, addr);
    if (i >= 0) {
        memcpy(e->found[i].name, line + 7, HC05EMU_NAME_MAX);
        e->found[i].named = true;
    }
    hc05emu_say(e, line);
    hc05emu_say(e, "OK");
}

void hc05emu_pair_done(hc05emu_t *e, const uint8_t *addr, bool ok,
                       uint32_t now_ms)
{
    (void)now_ms;
    if (e == NULL || !waits_for(e, HC05EMU_OP_PAIR, addr)) {
        return;
    }
    e->op = HC05EMU_OP_NONE;
    hc05emu_say(e, ok ? "OK" : "FAIL");
}

void hc05emu_link_event(hc05emu_t *e, const uint8_t *addr, bool up,
                        uint32_t now_ms)
{
    if (e == NULL || addr == NULL) {
        return;
    }
    const bool ours = memcmp(e->target, addr, HC05EMU_ADDR_LEN) == 0;
    if (up) {
        if (e->link == HC05EMU_LINK_CONNECTING && ours) {
            e->link = HC05EMU_LINK_UP;
            e->link_ms = now_ms;
        } else if (e->link != HC05EMU_LINK_UP) {
            /* A link nobody asked for, or one that came too late. */
            (void)hc05emu_q(e, HC05EMU_ACT_STOP);
        }
        return;
    }
    if (ours && (e->link == HC05EMU_LINK_UP
                 || e->link == HC05EMU_LINK_CONNECTING)) {
        hc05emu_link_failed(e, now_ms);
    }
}
