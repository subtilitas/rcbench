/**
 * @file
 * @brief The HC-05 AT exchange state machine: PROBE, SCAN, BIND and ANY as
 *        scripts of stages, run by hc05_step() and hc05_feed().
 *
 * Each operation is a list of stages (k_scripts).  A stage hands the caller
 * one action: KEY, a baud or a command.  A command stage waits for its
 * answer; hc05_feed() takes the answer lines and moves the script on,
 * hc05_step() ends a wait that ran out.  hc05_t.wait_ms is 0 while a stage
 * has not yet handed out its action, the length of its wait after that.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "hc05.h"
#include "hc05_int.h"

/** @brief The stages an operation is made of. */
enum {
    S_KEY = 0, /**< KEY high, then HC05_KEY_SETTLE_MS */
    S_BAUD,    /**< the UART to the baud of this try */
    S_AT,      /**< AT: OK at this baud */
    S_VERSION, /**< AT+VERSION? */
    S_INIT,    /**< AT+INIT: OK or ERROR:(17) */
    S_INQM,    /**< AT+INQM=1,9,9 */
    S_INQ,     /**< AT+INQ: +INQ lines, then OK */
    S_RNAME,   /**< AT+RNAME? for each device found */
    S_ROLE,    /**< AT+ROLE=1: master */
    S_PSWD,    /**< AT+PSWD=, quoted on a second try */
    S_PAIR,    /**< AT+PAIR=addr,20 */
    S_BIND,    /**< AT+BIND=addr */
    S_CMODE0,  /**< AT+CMODE=0: the bound address only */
    S_CMODE1,  /**< AT+CMODE=1: any slave */
    S_UART,    /**< AT+UART=115200,0,0 */
    S_RESET,   /**< AT+RESET */
    S_KEYLOW,  /**< KEY low, then HC05_RESET_MS for the restart */
    S_DATA,    /**< the UART to the data baud */
    S_END,     /**< the operation is done */
};

/** @brief PROBE's stages. */
static const uint8_t k_probe[] = { S_KEY, S_BAUD, S_AT, S_VERSION, S_END };
/** @brief SCAN's stages. */
static const uint8_t k_scan[] = { S_KEY, S_BAUD, S_AT, S_ROLE, S_INIT,
                                  S_INQM, S_INQ, S_RNAME, S_END };
/** @brief BIND's stages. */
static const uint8_t k_bind[] = { S_KEY, S_BAUD, S_AT, S_ROLE, S_PSWD,
                                  S_PAIR, S_BIND, S_CMODE0, S_UART,
                                  S_RESET, S_KEYLOW, S_DATA, S_END };
/** @brief ANY's stages. */
static const uint8_t k_any[] = { S_KEY, S_BAUD, S_AT, S_ROLE, S_PSWD,
                                 S_CMODE1, S_UART, S_RESET, S_KEYLOW,
                                 S_DATA, S_END };

/** @brief The stages of each operation, by hc05_op_t. */
static const uint8_t *const k_scripts[] = { NULL, k_probe, k_scan, k_bind,
                                            k_any };

/** @brief The index of S_BAUD in every script: a try at the next baud
 *         goes back to it. */
#define STEP_BAUD 1u

/** @brief The bauds the AT handshake tries, in order. */
static const uint32_t k_bauds[] = { HC05_BAUD_DATA, HC05_BAUD_ALT,
                                    HC05_BAUD_OLD };
/** @brief How many bauds the handshake tries. */
#define BAUD_TRIES ((uint8_t)(sizeof k_bauds / sizeof k_bauds[0]))

/** @brief hc05_t.tries at S_RNAME: AT+STATE? went out after a name that
 *         did not come, and its +STATE: line has come.  0: AT+RNAME?. @{ */
#define FENCE_SENT  1u /**< AT+STATE? sent */
#define FENCE_STATE 2u /**< its +STATE: line came */
/** @} */

/** @brief hc05_t.tries in the fence, the one way out of an exchange that
 *         did not end on its own answer (a timeout, an abort): AT+STATE?
 *         goes out, every line up to its +STATE: line and OK is dropped,
 *         or its wait runs out, and only then does the operation end
 *         FAILED with hc05_t.err, set when the fence began.  So no late
 *         answer reaches the next operation. @{ */
#define FENCE_OUT 3u /**< AT+STATE? to send or sent */
#define FENCE_IN  4u /**< its +STATE: line came */
/** @} */

/** @brief hc05_t.tries at S_KEYLOW: why the hold ends the operation
 *         FAILED.  0: it goes on to the data baud. @{ */
#define ABORTED    1u /**< hc05_abort() came after AT+RESET went out */
#define RESET_LOST 2u /**< AT+RESET's OK has not come: HC05_ERR_TIMEOUT
                            unless it comes in the hold */
/** @} */

/** @brief The error code AT+INIT answers when the SPP (serial port
 *         profile) library is up already. */
#define CODE_INIT_AGAIN 0x17u
/** @brief The error code firmware 3.0 is reported to answer AT+INQ with. */
#define CODE_INQ_REFUSED 0x1Fu

/** @brief The pairing time AT+PAIR is given, in s: HC05_PAIR_MS is it plus
 *         2s. */
#define PAIR_S 20u

_Static_assert(HC05_PAIR_MS == PAIR_S * 1000u + 2000u,
               "HC05_PAIR_MS is the pairing time plus 2s");
_Static_assert(HC05_INQ_MAX <= 9u && HC05_INQ_UNITS <= 48u,
               "AT+INQM takes 1 to 9 devices and 1 to 48 units");

/**
 * @brief The stage the script of @p h stands at.
 * @param h The machine, running.
 * @return The stage.
 */
static uint8_t stage(const hc05_t *h)
{
    return k_scripts[h->op][h->step];
}

/**
 * @brief The baud of handshake try @p i: the module's last baud first when
 *        it is one of k_bauds, then the others in their order.
 * @param h The machine.
 * @param i The try, 0 to BAUD_TRIES - 1.
 * @return The baud.
 */
static uint32_t baud_at(const hc05_t *h, uint8_t i)
{
    bool known = false;
    for (uint8_t j = 0; j < BAUD_TRIES; ++j) {
        known = known || k_bauds[j] == h->baud;
    }
    if (known) {
        if (i == 0u) {
            return h->baud;
        }
        --i;
    }
    for (uint8_t j = 0; j < BAUD_TRIES; ++j) {
        if (known && k_bauds[j] == h->baud) {
            continue;
        }
        if (i == 0u) {
            return k_bauds[j];
        }
        --i;
    }
    return k_bauds[0];
}

/**
 * @brief End the operation.
 * @param h     The machine.
 * @param state HC05_ST_DONE or HC05_ST_FAILED.
 * @param err   Why it failed; HC05_ERR_NONE for DONE.
 * @param code  The hh of an ERROR:(hh), else 0.
 */
static void finish(hc05_t *h, hc05_state_t state, hc05_err_t err,
                   uint8_t code)
{
    h->state = state;
    h->err = err;
    h->err_code = code;
    h->wait_ms = 0;
}

/**
 * @brief Move to the next stage; the last one ends the operation DONE.
 * @param h The machine.
 */
static void advance(hc05_t *h)
{
    ++h->step;
    h->wait_ms = 0;
    h->tries = 0;
    if (stage(h) == S_END) {
        finish(h, HC05_ST_DONE, HC05_ERR_NONE, 0);
    }
}

/**
 * @brief Append the text @p s to the SEND action being built.
 * @param h The machine.
 * @param s The text; cut at HC05_OUT_MAX bytes.
 */
static void put(hc05_t *h, const char *s)
{
    while (*s != '\0' && h->act.len < HC05_OUT_MAX) {
        h->act.data[h->act.len++] = (uint8_t)*s++;
    }
}

/**
 * @brief Append @p v in decimal to the SEND action being built.
 * @param h The machine.
 * @param v The number.
 */
static void put_u32(hc05_t *h, uint32_t v)
{
    char buf[11];
    size_t n = sizeof buf - 1u;
    buf[n] = '\0';
    do {
        buf[--n] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u);
    put(h, &buf[n]);
}

/**
 * @brief Append an address in the set commands' form, with commas.
 * @param h    The machine.
 * @param addr The address with colons, checked when it was taken.
 */
static void put_addr(hc05_t *h, const char *addr)
{
    char cmd[HC05_ADDR_MAX];
    (void)hc05_addr_cmd(addr, cmd, sizeof cmd);
    put(h, cmd);
}

/**
 * @brief The answer's timeout of a command stage.
 * @param s The stage.
 * @return The timeout, in ms.
 */
static uint32_t stage_wait(uint8_t s)
{
    switch (s) {
    case S_INQ:
        return HC05_INQ_MS(HC05_INQ_UNITS);
    case S_RNAME:
        return HC05_RNAME_MS;
    case S_PAIR:
        return HC05_PAIR_MS;
    case S_RESET:
        /* KEY stays high this long only: the module restarts on the
         * command, its OK or not, and KEY low then means data mode. */
        return HC05_KEY_SETTLE_MS;
    default:
        return HC05_AT_MS;
    }
}

/**
 * @brief Build the command of @p s into the action, emptied by queue().
 * @param h The machine.
 * @param s A command stage.
 * @return The answer's timeout, in ms.
 */
static uint32_t command(hc05_t *h, uint8_t s)
{
    h->act.kind = HC05_ACT_SEND;
    put(h, "AT");
    if (h->tries >= FENCE_OUT) {
        /* The aborted command's answers come first: its own time, then
         * the fence's. */
        put(h, "+STATE?\r\n");
        return stage_wait(s) + HC05_AT_MS;
    }
    switch (s) {
    case S_AT:
        break;
    case S_VERSION:
        put(h, "+VERSION?");
        break;
    case S_INIT:
        put(h, "+INIT");
        break;
    case S_INQM:
        put(h, "+INQM=1,");
        put_u32(h, HC05_INQ_MAX);
        put(h, ",");
        put_u32(h, HC05_INQ_UNITS);
        break;
    case S_INQ:
        put(h, "+INQ");
        break;
    case S_RNAME:
        if (h->tries != 0u) {
            put(h, "+STATE?");
        } else {
            put(h, "+RNAME?");
            put_addr(h, h->found[h->name_i].addr);
        }
        break;
    case S_ROLE:
        put(h, "+ROLE=1");
        break;
    case S_PSWD:
        put(h, h->pswd_quoted ? "+PSWD=\"" : "+PSWD=");
        put(h, h->pin);
        put(h, h->pswd_quoted ? "\"" : "");
        break;
    case S_PAIR:
        put(h, "+PAIR=");
        put_addr(h, h->target);
        put(h, ",");
        put_u32(h, PAIR_S);
        break;
    case S_BIND:
        put(h, "+BIND=");
        put_addr(h, h->target);
        break;
    case S_CMODE0:
        put(h, "+CMODE=0");
        break;
    case S_CMODE1:
        put(h, "+CMODE=1");
        break;
    case S_UART:
        put(h, "+UART=");
        put_u32(h, HC05_BAUD_DATA);
        put(h, ",0,0");
        break;
    default: /* S_RESET */
        put(h, "+RESET");
        break;
    }
    put(h, "\r\n");
    return stage_wait(s);
}

/**
 * @brief Queue the action of stage @p s, which has not handed one out yet.
 * @param h The machine, running.
 * @param s The stage.
 */
static void queue(hc05_t *h, uint8_t s)
{
    h->act_due = true;
    h->act.len = 0;
    switch (s) {
    case S_KEY:
        h->act.kind = HC05_ACT_KEY;
        h->act.key = true;
        h->wait_ms = HC05_KEY_SETTLE_MS;
        break;
    case S_BAUD:
        h->act.kind = HC05_ACT_BAUD;
        h->act.baud = baud_at(h, h->baud_try);
        h->line_n = 0;
        advance(h);
        break;
    case S_KEYLOW:
        h->act.kind = HC05_ACT_KEY;
        h->act.key = false;
        h->wait_ms = HC05_RESET_MS;
        break;
    case S_DATA:
        h->act.kind = HC05_ACT_BAUD;
        h->act.baud = HC05_BAUD_DATA;
        h->baud = HC05_BAUD_DATA;
        advance(h);
        break;
    default:
        h->wait_ms = command(h, s);
        h->line_n = 0;
        break;
    }
}

/**
 * @brief Move to the next device's name, or end the scan after the last.
 * @param h The machine, at S_RNAME.
 */
static void next_name(hc05_t *h)
{
    h->wait_ms = 0;
    if (++h->name_i >= h->found_n) {
        advance(h);
    }
}

/**
 * @brief Start the fence: the operation stays busy and ends FAILED with
 *        @p err when the fence ends.
 * @param h   The machine, at a command stage.
 * @param err Why the operation fails.
 */
static void fence(hc05_t *h, hc05_err_t err)
{
    h->err = err;
    h->tries = FENCE_OUT;
    h->wait_ms = 0;
}

/**
 * @brief A stage's wait ran out.
 * @param h The machine, running.
 * @param s The stage.
 */
static void expired(hc05_t *h, uint8_t s)
{
    h->wait_ms = 0;
    if (h->tries >= FENCE_OUT && s != S_KEYLOW) {
        finish(h, HC05_ST_FAILED, h->err, 0);
    } else if (s == S_KEYLOW && h->tries != 0u) {
        finish(h, HC05_ST_FAILED,
               h->tries == ABORTED ? HC05_ERR_NONE : HC05_ERR_TIMEOUT, 0);
    } else if (s == S_RESET) {
        /* No OK yet: KEY low and the hold all the same; an OK in the hold
         * still counts. */
        ++h->step;
        h->tries = RESET_LOST;
    } else if (s == S_AT) {
        h->tries = 0;
        if (++h->baud_try >= BAUD_TRIES) {
            h->baud = 0;
            fence(h, HC05_ERR_NO_AT);
        } else {
            /* A late answer at this baud is noise at the next one. */
            h->step = STEP_BAUD;
        }
    } else if (s == S_RNAME && h->tries == 0u) {
        /* A late answer would read as the next device's: AT+STATE? first,
         * and the next name after its +STATE: line and OK. */
        h->tries = FENCE_SENT;
    } else if (s == S_KEY || s == S_KEYLOW || s == S_RNAME) {
        /* At S_RNAME: no answer to AT+STATE? either, no more names. */
        advance(h);
    } else {
        fence(h, HC05_ERR_TIMEOUT);
    }
}

/**
 * @brief Keep the device of an +INQ line: the first report of an address,
 *        up to HC05_FOUND_MAX devices.
 * @param h     The machine, at S_INQ.
 * @param value The text after `+INQ:`.
 */
static void add_found(hc05_t *h, const char *value)
{
    hc05_dev_t d;
    if (!hc05_inq_fields(value, &d)) {
        return;
    }
    for (uint8_t i = 0; i < h->found_n; ++i) {
        if (strcmp(h->found[i].addr, d.addr) == 0) {
            return;
        }
    }
    if (h->found_n < HC05_FOUND_MAX) {
        h->found[h->found_n++] = d;
    }
}

/**
 * @brief Take one answer line for the command under way.
 * @param h    The machine, running, its command sent.
 * @param line The line, without CR LF.
 */
static void take_line(hc05_t *h, const char *line)
{
    char value[HC05_LINE_MAX];
    uint8_t code = 0;
    const hc05_line_t kind = hc05_parse_line(line, value, sizeof value,
                                             &code);
    const uint8_t s = stage(h);

    if (h->tries >= FENCE_OUT && s != S_KEYLOW) {
        if (kind == HC05_LINE_STATE) {
            h->tries = FENCE_IN;
        } else if (kind == HC05_LINE_OK && h->tries == FENCE_IN) {
            finish(h, HC05_ST_FAILED, h->err, 0);
        }
        return;
    }
    if (s == S_KEYLOW && kind == HC05_LINE_OK && h->tries == RESET_LOST) {
        h->tries = 0; /* AT+RESET's OK, late */
    }
    if (s == S_KEY || s == S_KEYLOW) {
        return;
    }
    if (s == S_RNAME && h->tries != 0u) {
        /* The fence: every line up to +STATE: and its OK is dropped. */
        if (kind == HC05_LINE_STATE) {
            h->tries = FENCE_STATE;
        } else if (kind == HC05_LINE_OK && h->tries == FENCE_STATE) {
            h->tries = 0;
            next_name(h);
        }
        return;
    }
    if (kind == HC05_LINE_OK) {
        if (s == S_AT) {
            h->baud = baud_at(h, h->baud_try);
        } else if (s == S_INQ && h->found_n == 0u) {
            finish(h, HC05_ST_FAILED, HC05_ERR_NONE_FOUND, 0);
            return;
        } else if (s == S_RNAME) {
            next_name(h);
            return;
        }
        advance(h);
    } else if (kind == HC05_LINE_ERROR || kind == HC05_LINE_FAIL) {
        const bool error = kind == HC05_LINE_ERROR;
        if (s == S_RNAME) {
            h->found[h->name_i].name[0] = '\0';
            next_name(h);
        } else if (s == S_INIT && error && code == CODE_INIT_AGAIN) {
            advance(h);
        } else if (s == S_AT && error && h->tries == 0u) {
            h->tries = 1; /* a line the module held before AT: once more */
            h->wait_ms = 0;
        } else if (s == S_PSWD && error && !h->pswd_quoted) {
            h->pswd_quoted = true;
            h->wait_ms = 0;
        } else if (s == S_INQ && error && code == CODE_INQ_REFUSED) {
            finish(h, HC05_ST_FAILED, HC05_ERR_INQ_REFUSED, code);
        } else if (s == S_PAIR) {
            finish(h, HC05_ST_FAILED, HC05_ERR_PAIR, code);
        } else {
            finish(h, HC05_ST_FAILED, HC05_ERR_REFUSED, code);
        }
    } else if (kind == HC05_LINE_VERSION && s == S_VERSION) {
        hc05_copy_text(h->version, sizeof h->version, value);
    } else if (kind == HC05_LINE_INQ && s == S_INQ) {
        add_found(h, value);
    } else if (kind == HC05_LINE_RNAME && s == S_RNAME) {
        hc05_dev_t *d = &h->found[h->name_i];
        hc05_copy_text(d->name, sizeof d->name, value);
    }
}

void hc05_init(hc05_t *h)
{
    if (h != NULL) {
        memset(h, 0, sizeof *h);
    }
}

/**
 * @brief Start operation @p op from its first stage.
 * @param h      The machine.
 * @param op     The operation.
 * @param now_ms The clock.
 * @return false when @p h is NULL or busy.
 */
static bool start(hc05_t *h, hc05_op_t op, uint32_t now_ms)
{
    if (h == NULL || h->state == HC05_ST_RUNNING) {
        return false;
    }
    h->op = op;
    h->state = HC05_ST_RUNNING;
    h->err = HC05_ERR_NONE;
    h->err_code = 0;
    h->step = 0;
    h->baud_try = 0;
    h->tries = 0;
    h->sent_ms = now_ms;
    h->wait_ms = 0;
    h->pswd_quoted = false;
    h->line_n = 0;
    h->act_due = false;
    return true;
}

bool hc05_probe(hc05_t *h, uint32_t now_ms)
{
    if (!start(h, HC05_OP_PROBE, now_ms)) {
        return false;
    }
    h->baud = 0;
    h->version[0] = '\0';
    return true;
}

bool hc05_scan(hc05_t *h, uint32_t now_ms)
{
    if (!start(h, HC05_OP_SCAN, now_ms)) {
        return false;
    }
    memset(h->found, 0, sizeof h->found);
    h->found_n = 0;
    h->name_i = 0;
    return true;
}

/**
 * @brief Whether @p pin is 1 to 16 decimal digits.
 * @param pin The PIN (personal identification number); may be NULL.
 * @return true when the module takes it.
 */
static bool pin_ok(const char *pin)
{
    size_t n = 0;
    if (pin == NULL) {
        return false;
    }
    for (; pin[n] != '\0'; ++n) {
        if (pin[n] < '0' || pin[n] > '9' || n + 1u >= HC05_PIN_MAX) {
            return false;
        }
    }
    return n > 0u;
}

/**
 * @brief Refuse a start for a bad argument: the operation ends FAILED with
 *        HC05_ERR_ARG and nothing is sent.
 * @param h  The machine, not busy.
 * @param op The operation asked for.
 * @return false.
 */
static bool refuse(hc05_t *h, hc05_op_t op)
{
    h->op = op;
    finish(h, HC05_ST_FAILED, HC05_ERR_ARG, 0);
    h->act_due = false;
    return false;
}

bool hc05_bind(hc05_t *h, const char *addr, const char *pin, uint32_t now_ms)
{
    char target[HC05_ADDR_MAX];
    if (h == NULL || h->state == HC05_ST_RUNNING) {
        return false;
    }
    if (!hc05_addr_show(addr, target, sizeof target) || !pin_ok(pin)) {
        return refuse(h, HC05_OP_BIND);
    }
    (void)start(h, HC05_OP_BIND, now_ms);
    memcpy(h->target, target, sizeof h->target);
    memcpy(h->pin, pin, strlen(pin) + 1u);
    return true;
}

bool hc05_any(hc05_t *h, const char *pin, uint32_t now_ms)
{
    if (h == NULL || h->state == HC05_ST_RUNNING) {
        return false;
    }
    if (!pin_ok(pin)) {
        return refuse(h, HC05_OP_ANY);
    }
    (void)start(h, HC05_OP_ANY, now_ms);
    h->target[0] = '\0';
    memcpy(h->pin, pin, strlen(pin) + 1u);
    return true;
}

void hc05_abort(hc05_t *h)
{
    if (h == NULL || h->state != HC05_ST_RUNNING) {
        return;
    }
    const uint8_t s = stage(h);
    if (h->tries >= FENCE_OUT && s != S_KEYLOW) {
        return; /* a fence runs or waits to go out: it stands */
    }
    if ((s == S_RESET && h->wait_ms != 0u) || s == S_KEYLOW) {
        /* AT+RESET went out: the module restarts whatever comes.  KEY low
         * and the hold run to their end, so no operation raises KEY in the
         * restart; the operation then ends FAILED, HC05_ERR_NONE. */
        if (s == S_RESET) {
            ++h->step;
            h->wait_ms = 0;
            h->act_due = false;
        }
        h->tries = ABORTED;
        return;
    }
    if (s == S_RNAME && h->tries != 0u) {
        /* The name fence is out: it serves as this one. */
        h->tries = h->tries == FENCE_SENT ? FENCE_OUT : FENCE_IN;
        h->err = HC05_ERR_NONE;
        return;
    }
    if (h->wait_ms != 0u && s != S_KEY) {
        fence(h, HC05_ERR_NONE); /* a command is out */
        return;
    }
    finish(h, HC05_ST_FAILED, HC05_ERR_NONE, 0);
    h->act_due = false;
}

void hc05_feed(hc05_t *h, const uint8_t *b, size_t n)
{
    if (h == NULL || b == NULL) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c = b[i];
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            /* The rest of a line longer than the buffer is dropped; a NUL
             * becomes a byte no answer holds. */
            if (h->line_n + 1u < HC05_LINE_MAX) {
                h->line[h->line_n++] = c == 0u ? (char)0xFF : (char)c;
            }
            continue;
        }
        h->line[h->line_n] = '\0';
        h->line_n = 0;
        if (h->state == HC05_ST_RUNNING && h->wait_ms != 0u) {
            take_line(h, h->line);
        }
    }
}

/**
 * @brief Run the script up to its next action or wait.
 * @param h      The machine, no action queued.
 * @param now_ms The clock.
 */
static void plan(hc05_t *h, uint32_t now_ms)
{
    /* At most: a wait ends, the stage after it queues its action. */
    for (int i = 0; i < 3; ++i) {
        if (h->state != HC05_ST_RUNNING) {
            if (h->key) {
                h->act.kind = HC05_ACT_KEY;
                h->act.key = false;
                h->act.len = 0;
                h->act_due = true;
            }
            return;
        }
        if (h->wait_ms == 0u) {
            queue(h, stage(h));
            return;
        }
        if ((uint32_t)(now_ms - h->sent_ms) < h->wait_ms) {
            return;
        }
        expired(h, stage(h));
    }
}

bool hc05_step(hc05_t *h, uint32_t now_ms, hc05_act_t *out)
{
    if (out != NULL) {
        out->kind = HC05_ACT_NONE;
        out->len = 0;
    }
    if (h == NULL || out == NULL) {
        return false;
    }
    if (!h->act_due) {
        plan(h, now_ms);
    }
    if (!h->act_due) {
        return false;
    }
    *out = h->act;
    h->act_due = false;
    if (out->kind == HC05_ACT_KEY) {
        h->key = out->key;
    }
    if (out->kind == HC05_ACT_SEND || out->kind == HC05_ACT_KEY) {
        h->sent_ms = now_ms;
    }
    return true;
}

bool hc05_busy(const hc05_t *h)
{
    return h != NULL && h->state == HC05_ST_RUNNING;
}
