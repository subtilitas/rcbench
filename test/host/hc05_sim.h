/*
 * A scripted HC-05 for the suites that test protocols/hc05.
 *
 * The module answers AT commands from a table per firmware version, as the
 * datasheet and reports state them: 2.0 (VERSION 2.0-20100601) takes an
 * unquoted PIN and scans; 3.0 (VERSION 3.0-20170601) takes the PIN quoted
 * only and answers AT+INQ with ERROR:(1F) unless `inq_on_3` is set.  It
 * answers only at its own baud and only while KEY is high; at another baud
 * a command leaves junk in its line buffer when `junk` is set, and the next
 * command at the right baud answers ERROR:(0).  AT+INQ reports each device
 * at its own time and ends with OK after INQM's units of 1.28s or at the
 * ninth device; AT+RESET restarts the module 300ms after its OK, at the
 * AT+UART baud with KEY low and at 38400 (full AT mode) with KEY high.
 * It starts in the slave role and runs AT+INQ only after AT+ROLE=1.
 *
 * The driver runs the machine 1ms at a time: the answers due are fed, then
 * every action of that ms is taken.  The sim counts what a host must never
 * do: a byte while KEY is low, a byte within HC05_KEY_SETTLE_MS of KEY's
 * rise.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HC05_SIM_H
#define HC05_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hc05.h"

#pragma GCC diagnostic ignored "-Wunused-function"

#define SIM_DEV_MAX 12u
#define SIM_OUT_MAX 40u
#define SIM_LOG_MAX 64u
#define SIM_TEXT_MAX 72u
/* The module's answer latency, ms. */
#define SIM_LAT_MS 10u
/* AT+RESET's OK to the restart, ms. */
#define SIM_RESTART_MS 300u

typedef struct {
    const char *addr;  /* colons, as +INQ prints it */
    uint32_t cls;      /* the class word printed */
    uint16_t rssi;     /* the RSSI word printed */
    const char *name;  /* NULL: AT+RNAME? answers FAIL */
    bool name_silent;  /* AT+RNAME? answers nothing */
    uint32_t name_late_ms; /* not 0: AT+RNAME? answers this late */
    uint32_t inq_ms;   /* its +INQ line after AT+INQ, ms */
} sim_dev_t;

typedef struct {
    uint32_t at;
    char text[SIM_TEXT_MAX];
    size_t n;
} sim_out_t;

typedef struct {
    uint32_t t;
    hc05_act_t act;
} sim_log_t;

typedef struct {
    int fw;              /* 2 or 3 */
    uint32_t baud;       /* the module's UART baud */
    bool key;            /* KEY as the host drives it */
    uint32_t uart;       /* the host's UART baud */
    bool junk;           /* a wrong-baud command leaves junk behind */
    bool dirty;          /* junk sits in the line buffer */
    bool garble;         /* a wrong-baud command answers garbage bytes */
    bool silent;         /* no module */
    bool inq_on_3;       /* a 3.0 module that scans */
    bool inq_no_ok;      /* AT+INQ never ends */
    bool pswd_any;       /* AT+PSWD answers ERROR:(0) in either form */
    bool init_error;     /* AT+INIT answers ERROR:(4) */
    bool reset_silent;   /* AT+RESET answers nothing */
    bool pair_error;     /* AT+PAIR answers ERROR:(15) */
    const char *inq_answer; /* AT+INQ answers this line alone */
    uint32_t inq_units;  /* the units AT+INQM set */
    uint32_t inq_max;    /* the devices AT+INQM set */
    bool inited;
    int role;
    int cmode;
    char pin[24];
    char bind[24];
    uint32_t uart_set;   /* AT+UART's baud, taken at the restart */
    uint32_t restart_at; /* 0: none due */
    bool restart_due;
    bool restarted_key;  /* KEY at the last restart */
    int restarts;
    const char *dev_pin; /* the slaves' PIN */
    uint32_t pair_ms;    /* AT+PAIR to its OK */
    sim_dev_t devs[SIM_DEV_MAX];
    unsigned ndev;
    const char *inq_raw[4]; /* lines AT+INQ prints 100ms on, as they are */
    unsigned nraw;
    sim_out_t out[SIM_OUT_MAX];
    unsigned nout;
    uint32_t busy_until; /* the last answer queued: answers keep order */
    char cmds[SIM_LOG_MAX][SIM_TEXT_MAX]; /* commands at the right baud */
    unsigned ncmd;
    sim_log_t log[SIM_LOG_MAX * 2u];      /* every action */
    unsigned nlog;
    uint32_t key_rise;
    unsigned bytes_key_low;  /* SEND while KEY was low */
    unsigned bytes_early;    /* SEND within the settle time */
    unsigned sends;
} sim_t;

static void sim_init(sim_t *s, int fw, uint32_t baud)
{
    memset(s, 0, sizeof *s);
    s->fw = fw;
    s->baud = baud;
    s->uart = 0;
    s->role = 0;
    s->cmode = 1;
    s->uart_set = baud;
    s->dev_pin = "1234";
    s->pair_ms = 4000u;
}

static void sim_add_dev(sim_t *s, const char *addr, uint32_t cls,
                        uint16_t rssi, const char *name, uint32_t inq_ms)
{
    sim_dev_t *d = &s->devs[s->ndev++];
    d->addr = addr;
    d->cls = cls;
    d->rssi = rssi;
    d->name = name;
    d->name_silent = false;
    d->inq_ms = inq_ms;
}

static void sim_say_n(sim_t *s, uint32_t at, const char *text, size_t n)
{
    if (s->nout >= SIM_OUT_MAX) {
        return;
    }
    /* One command at a time: no answer before the one queued last. */
    if (s->nout > 0u && (int32_t)(at - s->busy_until) <= 0) {
        at = s->busy_until + 1u;
    }
    s->busy_until = at;
    sim_out_t *o = &s->out[s->nout++];
    o->at = at;
    memcpy(o->text, text, n);
    o->n = n;
}

static void sim_say(sim_t *s, uint32_t at, const char *line)
{
    char buf[SIM_TEXT_MAX];
    int n = snprintf(buf, sizeof buf, "%s\r\n", line);
    sim_say_n(s, at, buf, (size_t)n);
}

/* A device by the address of a set command (commas). */
static sim_dev_t *sim_dev_by_cmd(sim_t *s, const char *cmd_addr)
{
    for (unsigned i = 0; i < s->ndev; ++i) {
        char c[HC05_ADDR_MAX];
        if (hc05_addr_cmd(s->devs[i].addr, c, sizeof c)
            && strcmp(c, cmd_addr) == 0) {
            return &s->devs[i];
        }
    }
    return NULL;
}

static void sim_inq(sim_t *s, uint32_t now)
{
    uint32_t end = now + s->inq_units * 1280u;
    unsigned told = 0;
    if (s->inq_answer != NULL) {
        sim_say(s, now + SIM_LAT_MS, s->inq_answer);
        return;
    }
    if (!s->inited) {
        sim_say(s, now + SIM_LAT_MS, "ERROR:(16)");
        return;
    }
    if (s->role != 1) {
        /* A slave runs no inquiry; the code is this sim's choice. */
        sim_say(s, now + SIM_LAT_MS, "ERROR:(0)");
        return;
    }
    if (s->fw == 3 && !s->inq_on_3) {
        sim_say(s, now + SIM_LAT_MS, "ERROR:(1F)");
        return;
    }
    for (unsigned i = 0; i < s->nraw; ++i) {
        sim_say(s, now + 100u + i, s->inq_raw[i]);
    }
    for (unsigned i = 0; i < s->ndev; ++i) {
        const sim_dev_t *d = &s->devs[i];
        char line[SIM_TEXT_MAX];
        if (d->inq_ms >= s->inq_units * 1280u || told >= s->inq_max) {
            continue;
        }
        snprintf(line, sizeof line, "+INQ:%s,%lX,%X", d->addr,
                 (unsigned long)d->cls, (unsigned)d->rssi);
        sim_say(s, now + d->inq_ms, line);
        if (++told == s->inq_max) {
            end = now + d->inq_ms + 1u;
        }
    }
    if (!s->inq_no_ok) {
        sim_say(s, end, "OK");
    }
}

static void sim_command(sim_t *s, const char *cmd, uint32_t now)
{
    const uint32_t at = now + SIM_LAT_MS;
    const char *pswd = s->fw == 3 ? "AT+PSWD=\"" : "AT+PSWD=";
    unsigned long m = 0;
    unsigned long u = 0;

    if (s->ncmd < SIM_LOG_MAX) {
        snprintf(s->cmds[s->ncmd++], SIM_TEXT_MAX, "%s", cmd);
    }
    if (s->dirty) {
        s->dirty = false;
        sim_say(s, at, "ERROR:(0)");
        return;
    }
    if (strcmp(cmd, "AT") == 0) {
        sim_say(s, at, "OK");
    } else if (strcmp(cmd, "AT+VERSION?") == 0) {
        sim_say(s, at, s->fw == 3 ? "+VERSION:3.0-20170601"
                                  : "+VERSION:2.0-20100601");
        sim_say(s, at + 1u, "OK");
    } else if (strcmp(cmd, "AT+INIT") == 0) {
        if (s->init_error) {
            sim_say(s, at, "ERROR:(4)");
        } else {
            sim_say(s, at, s->inited ? "ERROR:(17)" : "OK");
            s->inited = true;
        }
    } else if (sscanf(cmd, "AT+INQM=1,%lu,%lu", &m, &u) == 2) {
        s->inq_max = (uint32_t)m;
        s->inq_units = (uint32_t)u;
        sim_say(s, at, "OK");
    } else if (strcmp(cmd, "AT+INQ") == 0) {
        sim_inq(s, now);
    } else if (strncmp(cmd, "AT+RNAME?", 9) == 0) {
        const sim_dev_t *d = sim_dev_by_cmd(s, cmd + 9);
        if (d != NULL && d->name_silent) {
            return;
        }
        if (d == NULL || d->name == NULL) {
            sim_say(s, now + 2000u, "FAIL");
            return;
        }
        char buf[SIM_TEXT_MAX];
        const uint32_t late = d->name_late_ms != 0u ? d->name_late_ms : 800u;
        snprintf(buf, sizeof buf, "+RNAME:%s", d->name);
        sim_say(s, now + late, buf);
        sim_say(s, now + late + 1u, "OK");
    } else if (strcmp(cmd, "AT+STATE?") == 0) {
        sim_say(s, at, "+STATE:PAIRABLE");
        sim_say(s, at + 1u, "OK");
    } else if (strcmp(cmd, "AT+ROLE=1") == 0) {
        s->role = 1;
        sim_say(s, at, "OK");
    } else if (strncmp(cmd, pswd, strlen(pswd)) == 0 && !s->pswd_any) {
        snprintf(s->pin, sizeof s->pin, "%s", cmd + strlen(pswd));
        if (s->fw == 3) {
            s->pin[strcspn(s->pin, "\"")] = '\0';
        }
        sim_say(s, at, "OK");
    } else if (strncmp(cmd, "AT+PAIR=", 8) == 0) {
        char addr[24];
        snprintf(addr, sizeof addr, "%s", cmd + 8);
        addr[strcspn(addr, ";")] = '\0';
        char *comma = strrchr(addr, ',');
        if (comma != NULL) {
            *comma = '\0';
        }
        if (s->pair_error) {
            sim_say(s, at, "ERROR:(15)");
        } else if (sim_dev_by_cmd(s, addr) != NULL
                   && strcmp(s->pin, s->dev_pin) == 0) {
            sim_say(s, now + s->pair_ms, "OK");
        } else {
            sim_say(s, now + 20000u, "FAIL");
        }
    } else if (strncmp(cmd, "AT+BIND=", 8) == 0) {
        snprintf(s->bind, sizeof s->bind, "%s", cmd + 8);
        sim_say(s, at, "OK");
    } else if (strcmp(cmd, "AT+CMODE=0") == 0 || strcmp(cmd, "AT+CMODE=1") == 0) {
        s->cmode = cmd[9] - '0';
        sim_say(s, at, "OK");
    } else if (strncmp(cmd, "AT+UART=", 8) == 0) {
        unsigned long b = 0;
        if (sscanf(cmd + 8, "%lu,0,0", &b) == 1) {
            s->uart_set = (uint32_t)b;
        }
        sim_say(s, at, "OK");
    } else if (strcmp(cmd, "AT+RESET") == 0) {
        if (s->reset_silent) {
            return;
        }
        sim_say(s, at, "OK");
        s->restart_at = at + SIM_RESTART_MS;
        s->restart_due = true;
    } else {
        sim_say(s, at, "ERROR:(0)");
    }
}

static void sim_rx(sim_t *s, const hc05_act_t *a, uint32_t now)
{
    char cmd[SIM_TEXT_MAX];
    size_t n = a->len;

    ++s->sends;
    if (!s->key) {
        ++s->bytes_key_low;
    } else if ((uint32_t)(now - s->key_rise) < HC05_KEY_SETTLE_MS) {
        ++s->bytes_early;
    }
    if (s->silent || !s->key || s->restart_due) {
        return;
    }
    if (s->uart != s->baud) {
        s->dirty = s->dirty || s->junk;
        if (s->garble) {
            sim_say_n(s, now + SIM_LAT_MS, "\xF8\x80\r\n", 4u);
        }
        return;
    }
    if (n >= 2u && a->data[n - 2u] == '\r' && a->data[n - 1u] == '\n') {
        n -= 2u;
    }
    if (n >= sizeof cmd) {
        n = sizeof cmd - 1u;
    }
    memcpy(cmd, a->data, n);
    cmd[n] = '\0';
    sim_command(s, cmd, now);
}

static void sim_take(sim_t *s, const hc05_act_t *a, uint32_t now)
{
    if (s->nlog < sizeof s->log / sizeof s->log[0]) {
        s->log[s->nlog].t = now;
        s->log[s->nlog].act = *a;
        ++s->nlog;
    }
    if (a->kind == HC05_ACT_KEY) {
        if (a->key && !s->key) {
            s->key_rise = now;
        }
        s->key = a->key;
    } else if (a->kind == HC05_ACT_BAUD) {
        s->uart = a->baud;
    } else if (a->kind == HC05_ACT_SEND) {
        sim_rx(s, a, now);
    }
}

/* Feed the answers due at @p now, oldest first. */
static void sim_deliver(sim_t *s, hc05_t *h, uint32_t now)
{
    if (s->restart_due && (int32_t)(now - s->restart_at) >= 0) {
        s->restart_due = false;
        s->restarted_key = s->key;
        s->baud = s->key ? HC05_BAUD_ALT : s->uart_set;
        s->inited = false;
        ++s->restarts;
    }
    unsigned keep = 0;
    for (unsigned i = 0; i < s->nout; ++i) {
        const sim_out_t *o = &s->out[i];
        if ((int32_t)(now - o->at) >= 0) {
            /* An answer reaches the host only at its baud. */
            if (s->uart == s->baud || o->text[0] == '\xF8') {
                hc05_feed(h, (const uint8_t *)o->text, o->n);
            }
        } else {
            s->out[keep++] = *o;
        }
    }
    s->nout = keep;
}

/* One ms: the answers due, then every action the machine has. */
static void sim_tick(sim_t *s, hc05_t *h, uint32_t now)
{
    hc05_act_t a;
    sim_deliver(s, h, now);
    for (int i = 0; i < 8 && hc05_step(h, now, &a); ++i) {
        sim_take(s, &a, now);
    }
}

/* Run from @p t0 until the operation ended and KEY is low, at most @p max
 * ms.  @return the ms the run ended at, as an offset from t0. */
static uint32_t sim_run(sim_t *s, hc05_t *h, uint32_t t0, uint32_t max)
{
    uint32_t k = 0;
    for (; k <= max; ++k) {
        sim_tick(s, h, t0 + k);
        if (!hc05_busy(h) && !h->key) {
            break;
        }
    }
    return k;
}

/* The index of the first command @p cmd in the sim's log, or -1. */
static int sim_cmd_index(const sim_t *s, const char *cmd)
{
    for (unsigned i = 0; i < s->ncmd; ++i) {
        if (strcmp(s->cmds[i], cmd) == 0) {
            return (int)i;
        }
    }
    return -1;
}

#endif
