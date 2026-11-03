/**
 * @file
 * @brief The bench console's `esc` command; see esc_console.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_console.h"

#include <stddef.h>
#include <string.h>

#include "link_pages.h"
#include "page_console.h"

/** @brief An operation with no argument. */
typedef struct {
    const char *word; /**< as typed      */
    uint8_t op;       /**< a link_esc_op_t */
} plain_op_t;

/** @brief The operations with no argument. */
static const plain_op_t k_plain[] = {
    { "done", LINK_ESC_OP_DONE },   { "read", LINK_ESC_OP_READ },
    { "leave", LINK_ESC_OP_LEAVE }, { "stop", LINK_ESC_OP_STOP },
    { "abort", LINK_ESC_OP_ABORT }, { "clear", LINK_ESC_OP_CLEAR },
    { "close", LINK_ESC_OP_CLOSE },
};

/**
 * @brief The page, in one line.
 * @param io the page and the output
 */
static void report(const esc_console_io_t *io)
{
    uint16_t r[LINK_EW_COUNT];

    io->read(0u, LINK_EW_COUNT, r);
    io->say("esc state=%u op=%u result=%u ref=%u flags=0x%04x line=%u gpio=%u "
            "ses=%u fw=%u layout=%u why=%u t=%ums\n",
            (unsigned)r[LINK_EW_STATE], (unsigned)r[LINK_EW_OP],
            (unsigned)(r[LINK_EW_RESULT] & 0xFFu),
            (unsigned)(r[LINK_EW_RESULT] >> 8), (unsigned)r[LINK_EW_FLAGS],
            (unsigned)(r[LINK_EW_LINE] & 0xFFu),
            (unsigned)(r[LINK_EW_LINE] >> 8),
            (unsigned)(r[LINK_EW_SES] & 0xFFu), (unsigned)(r[LINK_EW_ID] & 0xFFu),
            (unsigned)(r[LINK_EW_ID] >> 8), (unsigned)(r[LINK_EW_WHY] & 0xFFu),
            (unsigned)r[LINK_EW_T_MS]);
}

/**
 * @brief The command frame, with the next SEQ and the staged registers' CRC.
 * @param io     the page and the output
 * @param op     the operation
 * @param staged the registers, as just written; NULL for an operation with none
 */
static void command(const esc_console_io_t *io, uint8_t op, const uint16_t *staged)
{
    uint16_t seq = 0u;
    uint16_t f[LINK_EW_W_CMD_FRAME];

    io->read(LINK_EW_SEQ, 1u, &seq);
    f[LINK_EW_W_CMD] = op;
    f[LINK_EW_W_SEQ] = (uint16_t)(seq + 1u);
    f[LINK_EW_W_KEY] = 0u;
    f[LINK_EW_W_CRC] = staged != NULL ? link_kst_crc(staged, LINK_EW_W_STAGED) : 0u;
    const uint8_t nack = io->write(0u, LINK_EW_W_CMD_FRAME, f);

    if (nack != 0u) {
        io->say("nack %u\n", (unsigned)nack);
    }
    report(io);
}

/**
 * @brief Stage the registers and send the command.
 * @param io     the page and the output
 * @param op     the operation
 * @param staged the registers
 */
static void staged_command(const esc_console_io_t *io, uint8_t op,
                           const uint16_t *staged)
{
    const uint8_t nack = io->write(LINK_EW_W_TARGET, LINK_EW_W_STAGED, staged);

    if (nack != 0u) {
        io->say("nack %u\n", (unsigned)nack);
        return;
    }
    command(io, op, staged);
}

/**
 * @brief `esc open slot|port <n>`.
 * @param io  the page and the output
 * @param tok the tokens
 * @param n   how many
 */
static void cmd_open(const esc_console_io_t *io, char **tok, unsigned n)
{
    uint16_t st[LINK_EW_W_STAGED];
    uint32_t index = 0u;

    if (n != 4u || !page_console_number(tok[3], &index) || index > 0xFFu
        || (strcmp(tok[2], "slot") != 0 && strcmp(tok[2], "port") != 0)) {
        io->say("err usage: esc open slot|port <n>\n");
        return;
    }
    memset(st, 0, sizeof(st));
    st[0] = (uint16_t)(index
                       | (strcmp(tok[2], "port") == 0 ? LINK_EW_TARGET_PORT : 0u));
    staged_command(io, LINK_ESC_OP_OPEN, st);
}

/**
 * @brief The usage line of `esc enter`.
 * @param io the output
 */
static void say_enter_usage(const esc_console_io_t *io)
{
    io->say("err usage: esc enter mv=<mV> ma=<mA> [hold=<ms>] [off=<ms>], "
            "or esc enter hand [hold=<ms>]\n");
}

/**
 * @brief `esc enter`.
 * @param io  the page and the output
 * @param tok the tokens
 * @param n   how many
 */
static void cmd_enter(const esc_console_io_t *io, char **tok, unsigned n)
{
    uint16_t st[LINK_EW_W_STAGED];
    uint32_t hold = 3000u;
    bool has_mv = false;
    bool has_ma = false;
    bool hand = false;

    memset(st, 0, sizeof(st));
    st[LINK_EW_W_OFF_MS - LINK_EW_W_TARGET] = 3000u;
    for (unsigned i = 2u; i < n; ++i) {
        uint32_t v = 0u;
        const bool has = strlen(tok[i]) > 3u && page_console_number(
            tok[i] + (strncmp(tok[i], "hold=", 5u) == 0 ? 5u
                      : strncmp(tok[i], "off=", 4u) == 0 ? 4u : 3u), &v);

        if (strcmp(tok[i], "hand") == 0) {
            st[LINK_EW_W_POWER - LINK_EW_W_TARGET] = LINK_ESC_BY_HAND;
            hand = true;
        } else if (has && v <= 0xFFFFu && strncmp(tok[i], "mv=", 3u) == 0) {
            st[LINK_EW_W_MV - LINK_EW_W_TARGET] = (uint16_t)v;
            has_mv = true;
        } else if (has && v <= 0xFFFFu && strncmp(tok[i], "ma=", 3u) == 0) {
            st[LINK_EW_W_MA - LINK_EW_W_TARGET] = (uint16_t)v;
            has_ma = true;
        } else if (has && v <= 0xFFFFu && strncmp(tok[i], "off=", 4u) == 0) {
            st[LINK_EW_W_OFF_MS - LINK_EW_W_TARGET] = (uint16_t)v;
        } else if (has && strncmp(tok[i], "hold=", 5u) == 0) {
            hold = v;
        } else {
            say_enter_usage(io);
            return;
        }
    }
    /* No voltage but the one asked for: BY HAND switches none on. */
    if (!hand && (!has_mv || !has_ma)) {
        say_enter_usage(io);
        return;
    }
    st[LINK_EW_W_HOLD_LO - LINK_EW_W_TARGET] = (uint16_t)(hold & 0xFFFFu);
    st[LINK_EW_W_HOLD_HI - LINK_EW_W_TARGET] = (uint16_t)(hold >> 16);
    /* The target stays as OPEN staged it; ENTER does not read it. */
    staged_command(io, LINK_ESC_OP_ENTER, st);
}

bool esc_console_line(const esc_console_io_t *io, char **tok, unsigned n)
{
    if (n == 1u) {
        report(io);
        return true;
    }
    if (strcmp(tok[1], "open") == 0) {
        cmd_open(io, tok, n);
        return true;
    }
    if (strcmp(tok[1], "enter") == 0) {
        cmd_enter(io, tok, n);
        return true;
    }
    for (size_t i = 0u; i < sizeof(k_plain) / sizeof(k_plain[0]); ++i) {
        if (n == 2u && strcmp(tok[1], k_plain[i].word) == 0) {
            command(io, k_plain[i].op, NULL);
            return true;
        }
    }
    io->say("err usage: esc [open|enter|done|read|leave|stop|abort|clear|close]\n");
    return true;
}
