/**
 * @file
 * @brief The HC-05 emulator's machine: the settings, the action queue, the
 *        start and the modes KEY selects, the command lines, the waits and
 *        the status LED.  The radio's results and the data-mode link are in
 *        hc05emu_radio.c, the commands in hc05emu_at.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "hc05emu.h"
#include "hc05emu_int.h"

/** @brief The AT+VERSION? text without one from the caller. */
#define VERSION_DEFAULT "btbridge"

/** @brief The LED's half period in AT mode, ms. */
#define LED_AT_MS   1000u
/** @brief The LED's half period in data mode without a link, ms. */
#define LED_DATA_MS 100u

/**
 * @brief Whether @p wait ms have passed since @p from, across the clock's
 *        wrap.
 * @param now  The clock.
 * @param from The start.
 * @param wait The wait.
 * @return true at @p from + @p wait and after.
 */
static bool due(uint32_t now, uint32_t from, uint32_t wait)
{
    return (uint32_t)(now - from) >= wait;
}

void hc05emu_cfg_default(hc05emu_cfg_t *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof *c);
    c->role = 1u;
    c->cmode = 0u;
    memcpy(c->pin, "1234", 5u);
    c->baud = HC05EMU_BAUD_DEFAULT;
    c->inq_mode = 1u;
    c->inq_max = 1u;
    c->inq_units = 48u;
}

bool hc05emu_cfg_valid(const hc05emu_cfg_t *c)
{
    if (c == NULL || c->role > 2u || c->cmode > 2u || c->bound > 1u
        || !hc05emu_baud_ok(c->baud) || c->stop > 1u || c->parity > 2u
        || c->inq_mode > 2u || c->inq_max == 0u || c->inq_units == 0u
        || c->inq_units > 48u) {
        return false;
    }
    size_t n = 0;
    while (n < HC05EMU_PIN_MAX && c->pin[n] != '\0') {
        const char ch = c->pin[n++];
        if (ch < 0x20 || ch > 0x7E || ch == '"' || ch == ',') {
            return false;
        }
    }
    return n > 0u && n < HC05EMU_PIN_MAX;
}

hc05emu_act_t *hc05emu_q(hc05emu_t *e, hc05emu_act_kind_t kind)
{
    if (e->q_n >= HC05EMU_Q_MAX) {
        ++e->q_lost;
        return NULL;
    }
    hc05emu_act_t *a = &e->q[(e->q_head + e->q_n) % HC05EMU_Q_MAX];
    ++e->q_n;
    memset(a, 0, sizeof *a);
    a->kind = kind;
    return a;
}

void hc05emu_say(hc05emu_t *e, const char *text)
{
    hc05emu_act_t *a = hc05emu_q(e, HC05EMU_ACT_SEND);
    if (a == NULL) {
        return;
    }
    while (*text != '\0' && a->len + 2u < HC05EMU_OUT_MAX) {
        a->data[a->len++] = (uint8_t)*text++;
    }
    a->data[a->len++] = '\r';
    a->data[a->len++] = '\n';
}

void hc05emu_error(hc05emu_t *e, uint8_t code)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[] = "ERROR:(00)";
    size_t n = 7u;
    if (code >= 16u) {
        line[n++] = hex[code >> 4];
    }
    line[n++] = hex[code & 15u];
    line[n++] = ')';
    line[n] = '\0';
    hc05emu_say(e, line);
}

void hc05emu_saved(hc05emu_t *e)
{
    (void)hc05emu_q(e, HC05EMU_ACT_SAVE);
    hc05emu_say(e, "OK");
}

/**
 * @brief Queue the UART's baud now and the stored stop bits and parity;
 *        full AT mode is 38400 baud 8N1.
 * @param e The machine.
 */
static void queue_baud(hc05emu_t *e)
{
    hc05emu_act_t *a = hc05emu_q(e, HC05EMU_ACT_BAUD);
    if (a != NULL) {
        a->baud = e->baud_now;
        a->stop = e->full_at ? 0u : e->cfg.stop;
        a->parity = e->full_at ? 0u : e->cfg.parity;
    }
}

/**
 * @brief The start, at power-up and after AT+RESET: KEY high full AT mode
 *        at 38400 baud, KEY low data mode at the stored baud and the link
 *        asked for.
 * @param e      The machine.
 * @param now_ms The clock.
 */
static void boot(hc05emu_t *e, uint32_t now_ms)
{
    e->mode = e->key ? HC05EMU_MODE_AT : HC05EMU_MODE_DATA;
    e->full_at = e->key;
    e->baud_now = e->key ? HC05EMU_BAUD_FULL_AT : e->cfg.baud;
    /* No command waits for the radio and none is held: AT+RESET runs only
     * when none does. */
    e->inited = false;
    e->line_n = 0;
    e->line_over = false;
    e->found_n = 0;
    e->link = HC05EMU_LINK_IDLE;
    queue_baud(e);
    hc05emu_link_start(e, now_ms);
}

void hc05emu_init(hc05emu_t *e, const hc05emu_cfg_t *stored,
                  const char *version, bool key, uint32_t now_ms)
{
    if (e == NULL) {
        return;
    }
    memset(e, 0, sizeof *e);
    if (hc05emu_cfg_valid(stored)) {
        e->cfg = *stored;
    } else {
        hc05emu_cfg_default(&e->cfg);
    }
    const char *v = version != NULL ? version : VERSION_DEFAULT;
    size_t n = 0;
    for (const char *q = v; *q != '\0' && n + 1u < HC05EMU_VERSION_MAX; ++q) {
        e->version[n++] = *q;
    }
    e->version[n] = '\0';
    e->key = key;
    boot(e, now_ms);
}

void hc05emu_key(hc05emu_t *e, bool high, uint32_t now_ms)
{
    if (e == NULL || high == e->key) {
        return;
    }
    e->key = high;
    if (e->mode == HC05EMU_MODE_RESTART) {
        return; /* the restart reads KEY when it ends */
    }
    e->line_n = 0;
    e->line_over = false;
    if (high && e->mode == HC05EMU_MODE_DATA) {
        e->mode = HC05EMU_MODE_AT;
        if (e->link != HC05EMU_LINK_UP) {
            /* No try runs in AT mode: the radio is the commands'.  An open
             * link stays, its bytes held back. */
            hc05emu_link_stop(e);
        }
    } else if (!high && e->mode == HC05EMU_MODE_AT) {
        if (e->op != HC05EMU_OP_NONE) {
            /* No answer: the UART carries data now. */
            (void)hc05emu_q(e, HC05EMU_ACT_STOP);
            e->op = HC05EMU_OP_NONE;
        }
        e->held_due = false;
        e->mode = HC05EMU_MODE_DATA;
        if (e->full_at) {
            e->full_at = false;
            e->baud_now = e->cfg.baud;
            queue_baud(e);
        }
        if (e->link != HC05EMU_LINK_UP) {
            hc05emu_link_start(e, now_ms);
        }
    }
}

/**
 * @brief A command line has ended: run it, or hold it while a command
 *        waits for the radio.  A second line in that time is dropped.
 * @param e      The machine, AT mode.
 * @param now_ms The clock.
 */
static void take_line(hc05emu_t *e, uint32_t now_ms)
{
    e->line[e->line_n] = '\0';
    if (e->op == HC05EMU_OP_NONE) {
        hc05emu_command(e, e->line, e->line_over, now_ms);
    } else if (!e->held_due) {
        memcpy(e->held, e->line, sizeof e->held);
        e->held_over = e->line_over;
        e->held_due = true;
    }
    e->line_n = 0;
    e->line_over = false;
}

void hc05emu_feed(hc05emu_t *e, const uint8_t *b, size_t n, uint32_t now_ms)
{
    if (e == NULL || b == NULL) {
        return;
    }
    for (size_t i = 0; i < n && e->mode == HC05EMU_MODE_AT; ++i) {
        const uint8_t c = b[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            take_line(e, now_ms);
        } else if (e->line_n + 1u < HC05EMU_LINE_MAX) {
            /* A NUL becomes a byte no command holds. */
            e->line[e->line_n++] = c == 0u ? (char)0xFF : (char)c;
        } else {
            e->line_over = true;
        }
    }
}

/**
 * @brief End the command that waits for the radio when its time is up:
 *        AT+INQ with OK, AT+RNAME? and AT+PAIR with FAIL.
 * @param e      The machine, AT mode.
 * @param now_ms The clock.
 */
static void op_timeout(hc05emu_t *e, uint32_t now_ms)
{
    if (e->op == HC05EMU_OP_NONE || !due(now_ms, e->op_ms, e->op_wait)) {
        return;
    }
    if (e->op == HC05EMU_OP_INQ) {
        hc05emu_say(e, "OK");
    } else {
        hc05emu_say(e, "FAIL");
    }
    if (e->op != HC05EMU_OP_NAME) {
        (void)hc05emu_q(e, HC05EMU_ACT_STOP);
    }
    e->op = HC05EMU_OP_NONE;
}

/**
 * @brief The data-mode link's waits: the search, the connect, the retry.
 * @param e      The machine, data mode.
 * @param now_ms The clock.
 */
static void link_timeout(hc05emu_t *e, uint32_t now_ms)
{
    switch (e->link) {
    case HC05EMU_LINK_SEARCH:
        if (due(now_ms, e->link_ms, HC05EMU_ANY_UNITS * HC05EMU_INQ_UNIT_MS)) {
            (void)hc05emu_q(e, HC05EMU_ACT_STOP);
            hc05emu_search_end(e, now_ms);
        }
        break;
    case HC05EMU_LINK_CONNECTING:
        if (due(now_ms, e->link_ms, HC05EMU_CONNECT_MS)) {
            (void)hc05emu_q(e, HC05EMU_ACT_STOP);
            hc05emu_link_failed(e, now_ms);
        }
        break;
    case HC05EMU_LINK_WAIT:
        if (due(now_ms, e->link_ms, HC05EMU_RETRY_MS)) {
            hc05emu_link_start(e, now_ms);
        }
        break;
    case HC05EMU_LINK_IDLE:
    case HC05EMU_LINK_UP:
    default:
        break;
    }
}

bool hc05emu_step(hc05emu_t *e, uint32_t now_ms, hc05emu_act_t *out)
{
    if (out != NULL) {
        out->kind = HC05EMU_ACT_NONE;
        out->len = 0;
    }
    if (e == NULL || out == NULL) {
        return false;
    }
    if (e->mode == HC05EMU_MODE_RESTART
        && due(now_ms, e->restart_ms, HC05EMU_RESTART_MS)) {
        boot(e, now_ms);
    }
    if (e->mode == HC05EMU_MODE_AT) {
        op_timeout(e, now_ms);
        if (e->op == HC05EMU_OP_NONE && e->held_due) {
            e->held_due = false;
            hc05emu_command(e, e->held, e->held_over, now_ms);
        }
    } else if (e->mode == HC05EMU_MODE_DATA) {
        link_timeout(e, now_ms);
    }
    if (e->q_n == 0u) {
        return false;
    }
    *out = e->q[e->q_head];
    e->q_head = (uint8_t)((e->q_head + 1u) % HC05EMU_Q_MAX);
    --e->q_n;
    return true;
}

bool hc05emu_at_mode(const hc05emu_t *e)
{
    return e != NULL && e->mode == HC05EMU_MODE_AT;
}

bool hc05emu_forwarding(const hc05emu_t *e)
{
    return e != NULL && e->mode == HC05EMU_MODE_DATA
           && e->link == HC05EMU_LINK_UP;
}

bool hc05emu_connected(const hc05emu_t *e)
{
    return e != NULL && e->link == HC05EMU_LINK_UP;
}

bool hc05emu_led(const hc05emu_t *e, uint32_t now_ms)
{
    if (e == NULL || e->mode == HC05EMU_MODE_RESTART) {
        return false;
    }
    if (e->mode == HC05EMU_MODE_AT) {
        return (now_ms / LED_AT_MS) % 2u == 0u;
    }
    if (e->link == HC05EMU_LINK_UP) {
        return true;
    }
    return (now_ms / LED_DATA_MS) % 2u == 0u;
}
