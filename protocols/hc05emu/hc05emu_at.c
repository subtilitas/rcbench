/**
 * @file
 * @brief The HC-05 emulator's AT commands: one line in, its answer lines
 *        and actions queued.
 *
 * A line is `AT`, or `AT+NAME` followed by nothing, `?` or `=` and the
 * arguments.  `AT` and the name are read in either letter case, the
 * arguments as given.  A line the table below does not take answers
 * ERROR:(0); an argument out of range answers the datasheet's code for it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "hc05emu.h"
#include "hc05emu_int.h"

/** @brief The most letters of a command name read: VERSION's 7. */
#define NAME_LEN 7u

/** @brief The bauds AT+UART takes, from the datasheet. */
static const uint32_t k_bauds[] = { 4800u, 9600u, 19200u, 38400u, 57600u,
                                    115200u, 230400u, 460800u, 921600u,
                                    1382400u };

bool hc05emu_baud_ok(uint32_t baud)
{
    for (size_t i = 0; i < sizeof k_bauds / sizeof k_bauds[0]; ++i) {
        if (k_bauds[i] == baud) {
            return true;
        }
    }
    return false;
}

/** @brief One command: its name and its handler. */
typedef struct {
    const char *name;  /**< the name after `AT+`, upper case */
    void (*run)(hc05emu_t *e, const char *p, uint32_t now_ms); /**< the
                          handler, given the text after the name */
} cmd_t;

/**
 * @brief Read 1 to 10 decimal digits from *@p p, which is moved past them.
 * @param p The text.
 * @param v Receives the value.
 * @return false for no digit or a value above UINT32_MAX.
 */
static bool read_num(const char **p, uint32_t *v)
{
    uint32_t acc = 0;
    unsigned n = 0;
    while (**p >= '0' && **p <= '9') {
        const uint32_t d = (uint32_t)(**p - '0');
        if (acc > (UINT32_MAX - d) / 10u) {
            return false;
        }
        acc = acc * 10u + d;
        ++*p;
        ++n;
    }
    *v = acc;
    return n > 0u;
}

/**
 * @brief Step past @p c when *@p p starts with it.
 * @param p The text; moved by one byte on true.
 * @param c The character.
 * @return true when it was there.
 */
static bool take(const char **p, char c)
{
    if (**p != c) {
        return false;
    }
    ++*p;
    return true;
}

/**
 * @brief Read `=<n>` up to @p max and the line's end.
 * @param p   The text after the name.
 * @param max The highest value taken.
 * @param v   Receives the value.
 * @return 0 for a value in range; 1 for one above @p max; 2 for another
 *         form.
 */
static int read_set(const char *p, uint32_t max, uint32_t *v)
{
    if (!take(&p, '=') || !read_num(&p, v) || *p != '\0') {
        return 2;
    }
    return *v > max ? 1 : 0;
}

/**
 * @brief Whether @p p is the query form, `?` and the line's end.
 * @param p The text after the name.
 * @return true for a query.
 */
static bool is_query(const char *p)
{
    return p[0] == '?' && p[1] == '\0';
}

/** @brief An answer line being built, held to HC05EMU_OUT_MAX - 2 bytes
 *         so CR LF fit. */
typedef struct {
    char   t[HC05EMU_OUT_MAX - 1u]; /**< the text, NUL-terminated */
    size_t n;                       /**< its length */
} line_t;

/**
 * @brief Append @p s to @p l, cut where the line is full.
 * @param l The line.
 * @param s The text.
 */
static void put(line_t *l, const char *s)
{
    while (*s != '\0' && l->n + 1u < sizeof l->t) {
        l->t[l->n++] = *s++;
    }
    l->t[l->n] = '\0';
}

/**
 * @brief Append the decimal of @p v to @p l.
 * @param l The line.
 * @param v The number.
 */
static void put_u32(line_t *l, uint32_t v)
{
    char digits[10];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u);
    while (n > 0u && l->n + 1u < sizeof l->t) {
        l->t[l->n++] = digits[--n];
    }
    l->t[l->n] = '\0';
}

/**
 * @brief Answer the line @p a then @p b, then OK.
 * @param e The machine.
 * @param a The prefix.
 * @param b The value.
 */
static void say_two(hc05emu_t *e, const char *a, const char *b)
{
    line_t l = { .n = 0 };
    put(&l, a);
    put(&l, b);
    hc05emu_say(e, l.t);
    hc05emu_say(e, "OK");
}

/**
 * @brief Answer `<prefix><n>` and OK.
 * @param e      The machine.
 * @param prefix The text before the number.
 * @param n      The number.
 */
static void say_num(hc05emu_t *e, const char *prefix, uint32_t n)
{
    line_t l = { .n = 0 };
    put(&l, prefix);
    put_u32(&l, n);
    hc05emu_say(e, l.t);
    hc05emu_say(e, "OK");
}

/**
 * @brief Answer a set of a small number: the field set and saved, or the
 *        error for a value out of range or another form.
 * @param e    The machine.
 * @param p    The text after the name.
 * @param max  The highest value.
 * @param code The error code for a value above @p max.
 * @param f    The field.
 */
static void set_small(hc05emu_t *e, const char *p, uint32_t max, uint8_t code,
                      uint8_t *f)
{
    uint32_t v = 0;
    const int r = read_set(p, max, &v);
    if (r == 0) {
        *f = (uint8_t)v;
        hc05emu_saved(e);
    } else {
        hc05emu_error(e, r == 1 ? code : EMU_E_CMD);
    }
}

/** @brief AT+VERSION?. @param e The machine. @param p The arguments.
 *  @param now_ms The clock. */
static void cmd_version(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    if (!is_query(p)) {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    say_two(e, "+VERSION:", e->version);
}

/** @brief AT+ROLE? and AT+ROLE=\<0..2\>. @param e The machine. @param p The
 *  arguments. @param now_ms The clock. */
static void cmd_role(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    if (is_query(p)) {
        say_num(e, "+ROLE:", e->cfg.role);
    } else {
        set_small(e, p, 2u, EMU_E_ROLE, &e->cfg.role);
    }
}

/** @brief AT+CMODE? and AT+CMODE=\<0..2\>. @param e The machine. @param p The
 *  arguments. @param now_ms The clock. */
static void cmd_cmode(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    if (is_query(p)) {
        say_num(e, "+CMODE:", e->cfg.cmode);
    } else {
        set_small(e, p, 2u, EMU_E_CMD, &e->cfg.cmode);
    }
}

/** @brief AT+INIT: OK once, ERROR:(17) after.  @param e The machine.
 *  @param p The arguments. @param now_ms The clock. */
static void cmd_init(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    if (*p != '\0') {
        hc05emu_error(e, EMU_E_CMD);
    } else if (e->inited) {
        hc05emu_error(e, EMU_E_INIT_AGAIN);
    } else {
        e->inited = true;
        hc05emu_say(e, "OK");
    }
}

/** @brief AT+INQM? and AT+INQM=\<mode\>,\<max\>,\<units\>.  @param e The machine.
 *  @param p The arguments. @param now_ms The clock. */
static void cmd_inqm(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    hc05emu_cfg_t *c = &e->cfg;
    if (is_query(p)) {
        line_t l = { .n = 0 };
        put(&l, "+INQM:");
        put_u32(&l, c->inq_mode);
        put(&l, ",");
        put_u32(&l, c->inq_max);
        put(&l, ",");
        put_u32(&l, c->inq_units);
        hc05emu_say(e, l.t);
        hc05emu_say(e, "OK");
        return;
    }
    uint32_t mode = 0;
    uint32_t max = 0;
    uint32_t units = 0;
    if (!take(&p, '=') || !read_num(&p, &mode) || !take(&p, ',')
        || !read_num(&p, &max) || !take(&p, ',') || !read_num(&p, &units)
        || *p != '\0' || max == 0u || max > 255u) {
        hc05emu_error(e, EMU_E_CMD);
    } else if (mode > 2u) {
        hc05emu_error(e, EMU_E_INQ_MODE);
    } else if (units == 0u || units > 48u) {
        hc05emu_error(e, EMU_E_INQ_TIME);
    } else {
        c->inq_mode = (uint8_t)mode;
        c->inq_max = (uint8_t)max;
        c->inq_units = (uint8_t)units;
        hc05emu_saved(e);
    }
}

/** @brief AT+INQ: the +INQ: lines and OK come from the radio's results.
 *  @param e The machine. @param p The arguments. @param now_ms The clock. */
static void cmd_inq(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    if (*p != '\0') {
        hc05emu_error(e, EMU_E_CMD);
    } else if (!e->inited) {
        hc05emu_error(e, EMU_E_NOT_INIT);
    } else if (e->cfg.role != 1u) {
        hc05emu_error(e, EMU_E_ROLE);
    } else {
        hc05emu_inq_start(e, now_ms);
    }
}

/** @brief AT+RNAME?\<addr\>: a name the inquiry gave at once, else a read.
 *  @param e The machine. @param p The arguments. @param now_ms The clock. */
static void cmd_rname(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    uint8_t addr[HC05EMU_ADDR_LEN];
    if (!take(&p, '?')) {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    const uint8_t code = hc05emu_addr_read(&p, addr);
    if (code != 0u || *p != '\0') {
        hc05emu_error(e, code != 0u ? code : EMU_E_CMD);
        return;
    }
    const int i = hc05emu_find(e, addr);
    if (i >= 0 && e->found[i].named) {
        say_two(e, "+RNAME:", e->found[i].name);
        return;
    }
    hc05emu_act_t *a = hc05emu_q(e, HC05EMU_ACT_NAME);
    if (a != NULL) {
        memcpy(a->addr, addr, sizeof a->addr);
    }
    memcpy(e->target, addr, sizeof e->target);
    e->op = HC05EMU_OP_NAME;
    e->op_ms = now_ms;
    e->op_wait = HC05EMU_NAME_MS;
}

/** @brief AT+STATE?.  @param e The machine. @param p The arguments.
 *  @param now_ms The clock. */
static void cmd_state(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    if (!is_query(p)) {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    say_two(e, "+STATE:", hc05emu_state_word(e));
}

/** @brief AT+PSWD?, AT+PSWD=\<pin\> and AT+PSWD="\<pin\>".  @param e The
 *  machine. @param p The arguments. @param now_ms The clock. */
static void cmd_pswd(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    if (is_query(p)) {
        say_two(e, "+PSWD:", e->cfg.pin);
        return;
    }
    if (!take(&p, '=')) {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    size_t n = strlen(p);
    if (*p == '"') {
        if (n < 2u || p[n - 1u] != '"') {
            hc05emu_error(e, EMU_E_CMD);
            return;
        }
        ++p;
        n -= 2u;
    }
    if (n == 0u) {
        hc05emu_error(e, EMU_E_PIN_EMPTY);
        return;
    }
    if (n + 1u > HC05EMU_PIN_MAX) {
        hc05emu_error(e, EMU_E_PIN_LONG);
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < 0x20 || p[i] > 0x7E || p[i] == '"' || p[i] == ',') {
            hc05emu_error(e, EMU_E_CMD);
            return;
        }
    }
    memcpy(e->cfg.pin, p, n);
    e->cfg.pin[n] = '\0';
    hc05emu_saved(e);
}

/** @brief AT+PAIR=\<addr\>,\<s\>: the link closed, then the radio pairs.
 *  @param e The machine. @param p The arguments. @param now_ms The clock. */
static void cmd_pair(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    uint8_t addr[HC05EMU_ADDR_LEN];
    uint32_t s = 0;
    if (!take(&p, '=')) {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    const uint8_t code = hc05emu_addr_read(&p, addr);
    if (code != 0u) {
        hc05emu_error(e, code);
        return;
    }
    if (!take(&p, ',') || !read_num(&p, &s) || *p != '\0' || s == 0u
        || s > 255u) {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    hc05emu_link_stop(e);
    hc05emu_act_t *a = hc05emu_q(e, HC05EMU_ACT_PAIR);
    if (a != NULL) {
        memcpy(a->addr, addr, sizeof a->addr);
        memcpy(a->pin, e->cfg.pin, sizeof a->pin);
        a->timeout_ms = s * 1000u;
    }
    memcpy(e->target, addr, sizeof e->target);
    e->op = HC05EMU_OP_PAIR;
    e->op_ms = now_ms;
    e->op_wait = s * 1000u;
}

/** @brief AT+BIND? and AT+BIND=\<addr\>.  @param e The machine. @param p
 *  The arguments. @param now_ms The clock. */
static void cmd_bind(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    uint8_t addr[HC05EMU_ADDR_LEN] = { 0 };
    if (is_query(p)) {
        char text[16];
        hc05emu_addr_text(e->cfg.bound != 0u ? e->cfg.bind : addr, text);
        say_two(e, "+BIND:", text);
        return;
    }
    if (!take(&p, '=')) {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    const uint8_t code = hc05emu_addr_read(&p, addr);
    if (code != 0u || *p != '\0') {
        hc05emu_error(e, code != 0u ? code : EMU_E_CMD);
        return;
    }
    memcpy(e->cfg.bind, addr, sizeof e->cfg.bind);
    e->cfg.bound = 1u;
    hc05emu_saved(e);
}

/** @brief AT+UART? and AT+UART=\<baud\>,\<stop\>,\<parity\>: stored, taken at
 *  the next start.  @param e The machine. @param p The arguments.
 *  @param now_ms The clock. */
static void cmd_uart(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    hc05emu_cfg_t *c = &e->cfg;
    if (is_query(p)) {
        line_t l = { .n = 0 };
        put(&l, "+UART:");
        put_u32(&l, c->baud);
        put(&l, ",");
        put_u32(&l, c->stop);
        put(&l, ",");
        put_u32(&l, c->parity);
        hc05emu_say(e, l.t);
        hc05emu_say(e, "OK");
        return;
    }
    uint32_t baud = 0;
    uint32_t stop = 0;
    uint32_t parity = 0;
    if (!take(&p, '=') || !read_num(&p, &baud) || !take(&p, ',')
        || !read_num(&p, &stop) || !take(&p, ',') || !read_num(&p, &parity)
        || *p != '\0') {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    if (!hc05emu_baud_ok(baud)) {
        hc05emu_error(e, EMU_E_BAUD);
    } else if (stop > 1u) {
        hc05emu_error(e, EMU_E_STOP);
    } else if (parity > 2u) {
        hc05emu_error(e, EMU_E_PARITY);
    } else {
        c->baud = baud;
        c->stop = (uint8_t)stop;
        c->parity = (uint8_t)parity;
        hc05emu_saved(e);
    }
}

/** @brief AT+RESET: OK, the link closed, the restart in
 *  HC05EMU_RESTART_MS.  @param e The machine. @param p The arguments.
 *  @param now_ms The clock. */
static void cmd_reset(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    if (*p != '\0') {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    hc05emu_say(e, "OK");
    hc05emu_link_stop(e);
    e->mode = HC05EMU_MODE_RESTART;
    e->restart_ms = now_ms;
}

/** @brief AT+ORGL: the settings of a fresh bridge, the bound address
 *  forgotten.  @param e The machine. @param p The arguments.
 *  @param now_ms The clock. */
static void cmd_orgl(hc05emu_t *e, const char *p, uint32_t now_ms)
{
    (void)now_ms;
    if (*p != '\0') {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    hc05emu_cfg_default(&e->cfg);
    hc05emu_saved(e);
}

/** @brief The commands taken. */
static const cmd_t k_cmds[] = {
    { "VERSION", cmd_version }, { "ROLE", cmd_role },   { "CMODE", cmd_cmode },
    { "INIT", cmd_init },       { "INQM", cmd_inqm },   { "INQ", cmd_inq },
    { "RNAME", cmd_rname },     { "STATE", cmd_state }, { "PSWD", cmd_pswd },
    { "PAIR", cmd_pair },       { "BIND", cmd_bind },   { "UART", cmd_uart },
    { "RESET", cmd_reset },     { "ORGL", cmd_orgl },
};

/**
 * @brief The upper case of an ASCII letter.
 * @param c The character.
 * @return @p c in upper case; any other character as it is.
 */
static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

void hc05emu_command(hc05emu_t *e, const char *line, bool over,
                     uint32_t now_ms)
{
    if (line[0] == '\0' && !over) {
        return; /* an empty line: nothing */
    }
    if (over || upper(line[0]) != 'A' || upper(line[1]) != 'T') {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    const char *p = line + 2;
    if (*p == '\0') {
        hc05emu_say(e, "OK");
        return;
    }
    if (!take(&p, '+')) {
        hc05emu_error(e, EMU_E_CMD);
        return;
    }
    char name[NAME_LEN + 1u];
    size_t n = 0;
    while (upper(*p) >= 'A' && upper(*p) <= 'Z' && n < NAME_LEN) {
        name[n++] = upper(*p++);
    }
    name[n] = '\0';
    for (size_t i = 0; i < sizeof k_cmds / sizeof k_cmds[0]; ++i) {
        if (strcmp(name, k_cmds[i].name) == 0) {
            k_cmds[i].run(e, p, now_ms);
            return;
        }
    }
    hc05emu_error(e, EMU_E_CMD);
}
