/**
 * @file
 * @brief The bench console's `yge` command; see yge_console.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "yge_console.h"

#include <stddef.h>
#include <string.h>

#include "link_pages.h"
#include "openyge_session.h"
#include "page_console.h"

/** @brief An operation with no argument. */
typedef struct {
    const char *word; /**< as typed        */
    uint8_t op;       /**< a link_yge_op_t */
} plain_op_t;

/** @brief The operations with no argument. */
static const plain_op_t k_plain[] = {
    { "done", LINK_YGE_OP_HAND_DONE }, { "read", LINK_YGE_OP_READ },
    { "leave", LINK_YGE_OP_LEAVE },    { "stop", LINK_YGE_OP_STOP },
    { "abort", LINK_YGE_OP_ABORT },    { "clear", LINK_YGE_OP_CLEAR },
    { "close", LINK_YGE_OP_CLOSE },
};

/**
 * @brief The page, in one line.
 * @param io the page and the output
 */
static void report(const yge_console_io_t *io)
{
    uint16_t r[LINK_YP_COUNT];

    io->read(0u, LINK_YP_COUNT, r);
    io->say("yge state=%u op=%u result=%u ref=%u flags=0x%04x line=%u gpio=%u "
            "port=%u ses=%u guard=%u entries=%u reads=%u type=%u fw=%lu t=%ums\n",
            (unsigned)r[LINK_YP_STATE], (unsigned)r[LINK_YP_OP],
            (unsigned)(r[LINK_YP_RESULT] & 0xFFu),
            (unsigned)(r[LINK_YP_RESULT] >> 8), (unsigned)r[LINK_YP_FLAGS],
            (unsigned)(r[LINK_YP_LINE] & 0xFFu), (unsigned)(r[LINK_YP_LINE] >> 8),
            (unsigned)r[LINK_YP_PORT], (unsigned)(r[LINK_YP_SES] & 0xFFu),
            (unsigned)(r[LINK_YP_SES] >> 8), (unsigned)r[LINK_YP_ENTRIES],
            (unsigned)r[LINK_YP_READS], (unsigned)r[LINK_YP_TYPE],
            (unsigned long)(r[LINK_YP_FW_LO] | ((uint32_t)r[LINK_YP_FW_HI] << 16)),
            (unsigned)r[LINK_YP_T_MS]);
}

/**
 * @brief The command frame, with the next SEQ and the staged registers' CRC.
 * @param io     the page and the output
 * @param op     the operation
 * @param staged the registers, as just written; NULL for an operation with none
 */
static void command(const yge_console_io_t *io, uint8_t op, const uint16_t *staged)
{
    uint16_t seq = 0u;
    uint16_t f[LINK_YP_W_CMD_FRAME];

    io->read(LINK_YP_SEQ, 1u, &seq);
    f[LINK_YP_W_CMD] = op;
    f[LINK_YP_W_SEQ] = (uint16_t)(seq + 1u);
    f[LINK_YP_W_KEY] = op == (uint8_t)LINK_YGE_OP_ENTER ? LINK_YGE_KEY : 0u;
    f[LINK_YP_W_CRC] = staged != NULL ? link_kst_crc(staged, LINK_YP_W_STAGED) : 0u;
    const uint8_t nack = io->write(0u, LINK_YP_W_CMD_FRAME, f);

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
static void staged_command(const yge_console_io_t *io, uint8_t op,
                           const uint16_t *staged)
{
    const uint8_t nack = io->write(LINK_YP_W_PORT, LINK_YP_W_STAGED, staged);

    if (nack != 0u) {
        io->say("nack %u\n", (unsigned)nack);
        return;
    }
    command(io, op, staged);
}

/**
 * @brief `yge open port <n>`.
 * @param io  the page and the output
 * @param tok the tokens
 * @param n   how many
 */
static void cmd_open(const yge_console_io_t *io, char **tok, unsigned n)
{
    uint16_t st[LINK_YP_W_STAGED];
    uint32_t index = 0u;

    if (n != 4u || strcmp(tok[2], "port") != 0
        || !page_console_number(tok[3], &index) || index > 0xFFu) {
        io->say("err usage: yge open port <n>\n");
        return;
    }
    memset(st, 0, sizeof(st));
    st[0] = (uint16_t)index;    /* LINK_YP_W_PORT, the first staged */
    staged_command(io, LINK_YGE_OP_OPEN, st);
}

/**
 * @brief The usage line of `yge enter`.
 * @param io the output
 */
static void say_enter_usage(const yge_console_io_t *io)
{
    io->say("err usage: yge enter mv=<mV> ma=<mA> [boot=<ms>], "
            "or yge enter hand [boot=<ms>]\n");
}

/**
 * @brief `yge enter`.
 * @param io  the page and the output
 * @param tok the tokens
 * @param n   how many
 */
static void cmd_enter(const yge_console_io_t *io, char **tok, unsigned n)
{
    uint16_t st[LINK_YP_W_STAGED];
    uint16_t port = 0u;
    bool has_mv = false;
    bool has_ma = false;
    bool hand = false;

    memset(st, 0, sizeof(st));
    /* The port stays as OPEN staged it: the CRC covers it. */
    io->read(LINK_YP_PORT, 1u, &port);
    st[0] = port;               /* LINK_YP_W_PORT */
    st[LINK_YP_W_BOOT_MS - LINK_YP_W_PORT] = (uint16_t)OY_BOOT_MS;
    for (unsigned i = 2u; i < n; ++i) {
        uint32_t v = 0u;
        const size_t skip = strncmp(tok[i], "boot=", 5u) == 0 ? 5u : 3u;
        const bool has = strlen(tok[i]) > skip
                         && page_console_number(tok[i] + skip, &v) && v <= 0xFFFFu;

        if (strcmp(tok[i], "hand") == 0) {
            st[LINK_YP_W_POWER - LINK_YP_W_PORT] = LINK_YGE_BY_HAND;
            hand = true;
        } else if (has && strncmp(tok[i], "mv=", 3u) == 0) {
            st[LINK_YP_W_MV - LINK_YP_W_PORT] = (uint16_t)v;
            has_mv = true;
        } else if (has && strncmp(tok[i], "ma=", 3u) == 0) {
            st[LINK_YP_W_MA - LINK_YP_W_PORT] = (uint16_t)v;
            has_ma = true;
        } else if (has && skip == 5u) {
            st[LINK_YP_W_BOOT_MS - LINK_YP_W_PORT] = (uint16_t)v;
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
    staged_command(io, LINK_YGE_OP_ENTER, st);
}

bool yge_console_line(const yge_console_io_t *io, char **tok, unsigned n)
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
    io->say("err usage: yge [open|enter|done|read|leave|stop|abort|clear|close]\n");
    return true;
}
