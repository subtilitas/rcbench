/*
 * A simulated ESC (electronic speed controller) on a simulated wire, for the
 * suites that test protocols/escwire.
 *
 * It answers protocol A byte for byte from its flash, as a behavioural
 * specification states an ESC does: an EFM8 part with an erase command and a
 * bootloader area, or an AM32 part that erases its page itself on a program,
 * forgets its address with every read and sets byte 2 of a written page.  The
 * wire side is an ew_driver_t on a clock the test advances; a fault script
 * bends single transactions.  On AM32 a gap ends a message: data less than
 * 234us behind their announcement are lost and no answer comes.  It counts what a host must never do: a
 * transaction outside its limits, a line() during a transaction, a first byte
 * while the pad is parked, a frame before the hold ended.  A third kind answers
 * the text interface (ew_sim_text.h).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_SIM_H
#define EW_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ew_session.h"

#pragma GCC diagnostic ignored "-Wunused-function"

#define SIM_LOG_MAX 96u
#define SIM_FLASH 0x10000u
#define SIM_AM_PAGE 1024u
/* AM32: idle line after a last stop bit that ends a message, us. */
#define SIM_AM_IDLE_US 234u
/* AM32: that idle, then one bit time, rounded up: an answer's start, us. */
#define SIM_AM_ANSWER_US 350u

typedef enum { SIM_EFM8 = 0, SIM_AM, SIM_E32 } sim_kind_t;

typedef enum {
    F_NONE = 0,
    F_SILENT,       /* the ESC hears nothing and says nothing */
    F_HEARD_SILENT, /* the ESC acts on it and its answer is lost */
    F_BAD_CRC,      /* a read's CRC is flipped */
    F_BYTE,         /* the answer is fail_byte */
    F_FRAMING,      /* a stop bit of the answer is low */
    F_OVERFLOW,     /* the capture is marked as not whole */
    F_ABORTED,      /* the capture is marked as cut */
    F_START_FALSE,  /* start() refuses */
    F_STUCK,        /* poll() never ends */
    F_DEAD,         /* from here on the ESC is gone */
    F_ERASE_DEAD,   /* AM32: a program erases, then the ESC is gone */
    F_NO_TAKE,      /* a program answers 0x30 and changes nothing */
    F_CORRUPT,      /* a program stores a wrong byte at offset fail_byte */
    F_BAD_BUFFER,   /* a data buffer is answered 0xC2, whatever else goes on */
    F_TEXT_ERROR,   /* text: the ESC does nothing and answers ERROR */
    F_TEXT_CUT,     /* text: the last 3 bytes of the reply are lost */
    F_TEXT_BYTE,    /* text: the reply's first byte is 0x01 */
    F_TEXT_FLOOD,   /* text: a reply with no end, longer than any buffer */
    F_TEXT_HEARD,   /* text: the ESC acts, its reply is swap_reply */
} sim_fault_t;

typedef struct {
    uint16_t n;
    uint8_t  b[EW_TX_MAX];
} sim_log_t;

typedef struct {
    uint8_t  kind;
    uint8_t  answer[9];
    uint16_t base, page, boot;
    uint8_t  flash[SIM_FLASH];        /* EFM8 */
    uint8_t  am[SIM_AM_PAGE];         /* AM32's settings page */
    uint8_t  am_name[32];
    uint8_t  am_record[27];
    uint8_t  am_boot_version;
    bool     contacted, dead;
    bool     addr_set;
    uint32_t addr;
    uint16_t announced;
    uint8_t  held[256];
    bool     have_data;
    bool     swallowed;    /* AM32: data ran into the announcement's message */
    /* What AM32 does after answering 0xC2 to a cut block is not stated: a test
     * chooses.  Set, it says nothing more until a power cycle. */
    bool     mute_after_cut;
    bool     mute_next;    /* mute_after_cut's turn has come */
    int      collisions;   /* a 0xC2 lay under the host's own data */
    bool     leave_sent;   /* a leave frame went */
    uint32_t leave_end;    /* its last stop bit's end */
    int      leave_unsettled; /* the line went low too soon after it */
    /* The wire. */
    uint32_t now;
    bool     running, stuck, overflow, aborted;
    uint32_t done_at;
    uint8_t *rx;
    uint16_t n_rx, bad_stop;
    uint8_t  reply[EW_RX_TEXT_MAX + 512u];
    uint8_t  line;
    /* The script: from transaction fail_at on, fail_n times. */
    uint8_t  fail;
    int      fail_at, fail_n;
    uint8_t  fail_byte;
    /* What happened. */
    int      starts, invalid, line_in_xfer, parked_tx;
    int      erases, programs, aborts, line_calls;
    sim_log_t log[SIM_LOG_MAX];
    uint8_t  line_log[16];
    int      n_line_log;
    uint32_t first_tx_at;
    /* The text interface. */
    int16_t  ram[EW_E32_KEYS];
    int16_t  saved[EW_E32_KEYS];
    char     music[EW_E32_MUSIC_MAX + 1u];
    char     banner[80];
    bool     low;
    uint32_t open_at;
    int      resets, forbidden, sets, saves, clip_slot;
    int16_t  clip_to;
    const char *show_extra;  /* one more line in `show` */
    int      show_drop;      /* a key `show` leaves out, -1 for none */
    const char *mute_word;   /* a line starting so gets no reply */
    bool     mute_heard;     /* and the ESC acts on it */
    int      drift_slot;     /* a key each `save` moves in RAM, -1 for none */
    const char *swap_reply;  /* see F_TEXT_HEARD */
    const char *save_plan;   /* per `save`: O as usual, L its reply lost, E ERROR,
                                U nothing done and a line before its OK */
} sim_t;

#include "ew_sim_text.h"

static uint32_t sim_now(void *ctx)
{
    return ((sim_t *)ctx)->now;
}

static void sim_line(void *ctx, ew_line_t level)
{
    sim_t *e = (sim_t *)ctx;

    if (e->kind == SIM_E32) {
        sim_text_line_state(e, level);
    }
    e->line = (uint8_t)level;
    e->line_calls++;
    if (e->running) {
        e->line_in_xfer++;
    }
    if (e->n_line_log < 16) {
        e->line_log[e->n_line_log++] = (uint8_t)level;
    }
    /* AM32 takes a message as ended only after more than 234us of idle line. */
    if (level == EW_LINE_LOW && e->leave_sent
        && e->now - e->leave_end <= SIM_AM_IDLE_US) {
        e->leave_unsettled++;
    }
}

static void sim_abort(void *ctx)
{
    sim_t *e = (sim_t *)ctx;

    e->running = false;
    e->aborts++;
    e->line = EW_LINE_UP;
}

/* Put the answer on the wire: reply bytes, 12 bit times each. */
static void sim_say(sim_t *e, const uint8_t *p, size_t n)
{
    memcpy(e->reply + e->n_rx, p, n);
    e->n_rx = (uint16_t)(e->n_rx + n);
}

static void sim_say1(sim_t *e, uint8_t b)
{
    sim_say(e, &b, 1u);
}

/* The bytes at the address: AM32's special addresses, or plain flash. */
static const uint8_t *sim_read_at(sim_t *e)
{
    if (e->kind == SIM_AM) {
        switch (e->addr) {
        case 0x0020u: return e->am;
        case 0x0021u: return e->am_name;
        case 0x0023u: return e->am_record;
        default:      return e->am; /* no test reads elsewhere */
        }
    }
    return e->flash + e->addr;
}

static void sim_program(sim_t *e)
{
    const uint8_t fault = e->fail;

    if (e->kind == SIM_AM) {
        if (e->addr != 0x0020u) {
            sim_say1(e, EW_A_REF_UNKNOWN);
            return;
        }
        if (fault == F_NO_TAKE) {
            sim_say1(e, EW_A_ACK);
            return;
        }
        memset(e->am, 0xFF, sizeof(e->am));
        e->programs++;
        memcpy(e->am, e->held, e->announced);
        e->am[2] = e->am_boot_version;
    } else {
        if (e->addr >= e->boot) {
            sim_say1(e, EW_A_REF_PROGRAM);
            return;
        }
        e->programs++;
        if (fault == F_NO_TAKE) {
            sim_say1(e, EW_A_ACK);
            return;
        }
        for (uint16_t i = 0; i < e->announced; ++i) {
            e->flash[e->addr + i] &= e->held[i];
        }
    }
    if (fault == F_CORRUPT) {
        if (e->kind == SIM_AM) {
            e->am[e->fail_byte] ^= 0x01u;
        } else {
            e->flash[e->addr + e->fail_byte] &= 0xFEu;
        }
    }
    sim_say1(e, EW_A_ACK);
}

/* One frame as the ESC hears it.  A frame with a bad CRC answers 0xC2. */
static void sim_frame(sim_t *e, const uint8_t *f, size_t n)
{
    if (!e->contacted) {
        uint8_t c[EW_A_CONTACT_LEN];

        (void)ew_a_contact(c);
        if (n == sizeof(c) && memcmp(f, c, n) == 0) {
            e->contacted = true;
            sim_say(e, e->answer, sizeof(e->answer));
        }
        return;
    }
    if (e->announced != 0u && !e->have_data) {
        if (ew_crc16(f, n) != 0u) {
            sim_say1(e, EW_A_REF_CRC);
            return;
        }
        memcpy(e->held, f, n - 2u);
        e->have_data = true;
        sim_say1(e, EW_A_ACK);
        return;
    }
    if (ew_crc16(f, n) != 0u) {
        sim_say1(e, EW_A_REF_CRC);
        return;
    }
    switch (f[0]) {
    case EW_A_CMD_ADDRESS: {
        const uint32_t a = ((uint32_t)f[2] << 8) | f[3];

        if (e->kind == SIM_AM && a < 0x0400u && !(a >= 0x20u && a <= 0x23u)) {
            sim_say1(e, EW_A_REF_UNKNOWN);
            return;
        }
        e->addr = a;
        e->addr_set = true;
        sim_say1(e, EW_A_ACK);
        return;
    }
    case EW_A_CMD_READ: {
        const uint16_t c = f[1] == 0u ? 256u : f[1];

        if (e->kind == SIM_AM && !e->addr_set) {
            sim_say1(e, EW_A_REF_UNKNOWN);
            return;
        }
        const uint8_t *p = sim_read_at(e);
        const uint16_t crc = ew_crc16(p, c);

        sim_say(e, p, c);
        sim_say1(e, (uint8_t)(crc & 0xFFu));
        sim_say1(e, (uint8_t)(crc >> 8));
        sim_say1(e, EW_A_ACK);
        if (e->kind == SIM_AM) {
            e->addr_set = false;
        }
        return;
    }
    case EW_A_CMD_ERASE:
        if (e->kind == SIM_EFM8) {
            memset(e->flash + (e->addr & ~(uint32_t)(e->page - 1u)), 0xFF,
                   e->page);
            e->erases++;
        }
        sim_say1(e, EW_A_ACK);
        return;
    case EW_A_CMD_ANNOUNCE:
        e->announced = (uint16_t)(((uint16_t)f[2] << 8) | f[3]);
        e->have_data = false;
        return;
    case EW_A_CMD_PROGRAM:
        sim_program(e);
        e->announced = 0u;
        e->have_data = false;
        return;
    case EW_A_CMD_LEAVE:
        e->contacted = false;
        e->dead = true;
        return;
    case EW_A_CMD_KEEP:
    default:
        sim_say1(e, EW_A_REF_UNKNOWN);
        return;
    }
}

static bool sim_start(void *ctx, const ew_xfer_t *x)
{
    sim_t *e = (sim_t *)ctx;
    const int index = e->starts++;
    bool faulted = e->fail != F_NONE && index >= e->fail_at
                   && index < e->fail_at + e->fail_n;
    const uint8_t saved = e->fail;

    if (!ew_xfer_valid(x)) {
        e->invalid++;
    }
    if (e->line == EW_LINE_PARK) {
        e->parked_tx++;
    }
    if (index < (int)SIM_LOG_MAX) {
        e->log[index].n = x->n_tx;
        memcpy(e->log[index].b, x->tx, x->n_tx);
    }
    if (index == 0) {
        e->first_tx_at = e->now;
    }
    if (faulted && saved == F_START_FALSE) {
        return false;
    }
    e->n_rx = 0u;
    e->bad_stop = 0u;
    e->overflow = false;
    e->aborted = false;
    e->stuck = false;
    e->rx = x->rx;
    if (faulted && saved == F_DEAD) {
        e->dead = true;
    }
    if (e->mute_next) {
        e->mute_next = false;
        e->dead = true;
    }
    const bool text_rate = x->rate == (uint8_t)EW_RATE_TEXT;
    const uint32_t bit_us = text_rate ? 26u : 52u;
    const uint32_t send_us = ((uint32_t)x->n_tx * 10u + x->pause_bits) * bit_us;

    if (!e->dead && !(faulted && saved == F_SILENT)) {
        /* The script's own program faults stay behind this flag. */
        const uint8_t kept = e->fail;

        e->fail = (faulted && (saved == F_NO_TAKE || saved == F_CORRUPT))
                      ? saved : (uint8_t)F_NONE;
        if (e->kind == SIM_E32) {
            sim_text_heard(e, x, faulted ? saved : (uint8_t)F_NONE);
        } else if (faulted && saved == F_ERASE_DEAD && x->tx[0] == EW_A_CMD_PROGRAM) {
            memset(e->am, 0xFF, sizeof(e->am));
            e->dead = true;
        } else if (e->swallowed) {
            /* AM32 still waits for data: a message that ends short of them
             * is a cut block, answered 0xC2.  Behind an announcement that
             * answer runs from about 0.3ms to 0.87ms into the host's pause and
             * data, so the host, sending, cannot read it. */
            e->swallowed = false;
            e->announced = 0u;
            e->mute_next = e->mute_after_cut;
            if (x->n_head != 0u) {
                e->collisions++;
            } else {
                sim_say1(e, EW_A_REF_CRC);
            }
        } else if (x->n_head != 0u && e->kind == SIM_AM
                   && (uint32_t)x->pause_bits * bit_us <= SIM_AM_IDLE_US) {
            /* The data run into the announcement's message and are lost; the
             * ESC waits for data the host has already sent. */
            sim_frame(e, x->tx, x->n_head);
            e->swallowed = true;
        } else if (x->n_head != 0u) {
            sim_frame(e, x->tx, x->n_head);
            sim_frame(e, x->tx + x->n_head, (size_t)(x->n_tx - x->n_head));
        } else {
            sim_frame(e, x->tx, x->n_tx);
        }
        e->fail = kept;
    }
    if (faulted) {
        switch (saved) {
        case F_HEARD_SILENT:
            e->n_rx = 0u;
            break;
        case F_BAD_CRC:
            if (e->n_rx >= 3u) {
                e->reply[0] ^= 0x01u;
            }
            break;
        case F_BYTE:
            e->reply[0] = e->fail_byte;
            e->n_rx = 1u;
            break;
        case F_FRAMING:
            e->bad_stop = 1u;
            break;
        case F_OVERFLOW:
            e->overflow = true;
            break;
        case F_ABORTED:
            e->aborted = true;
            break;
        case F_STUCK:
            e->stuck = true;
            break;
        case F_TEXT_HEARD:
            e->n_rx = 0u;
            sim_text_say(e, e->swap_reply);
            break;
        case F_TEXT_CUT:
            e->n_rx = e->n_rx >= 3u ? (uint16_t)(e->n_rx - 3u) : 0u;
            break;
        case F_TEXT_BYTE:
            e->reply[0] = 0x01u;
            break;
        case F_TEXT_FLOOD:
            memset(e->reply, 'x', sizeof(e->reply));
            e->n_rx = (uint16_t)sizeof(e->reply);
            break;
        case F_BAD_BUFFER:
            if (x->n_head != 0u) {
                e->reply[0] = EW_A_REF_CRC;
                e->n_rx = 1u;
                e->have_data = false;
                e->announced = 0u;
            }
            break;
        default:
            break;
        }
    }
    if (e->dead) {
        e->n_rx = 0u;
    }
    if (x->max_rx != 0u && e->n_rx > x->max_rx) {
        e->n_rx = x->max_rx;
    }
    if (x->tx[0] == EW_A_CMD_LEAVE && x->n_tx == EW_A_SHORT_FRAME
        && e->kind == SIM_AM) {
        e->leave_sent = true;
        e->leave_end = e->now + send_us;
    }
    /* An EFM8 answers after 2 bit times, 12 bit times a byte; AM32 after its
     * end of message, 10 bit times a byte. */
    const uint32_t answer_us = e->kind == SIM_AM
                                   ? SIM_AM_ANSWER_US + (uint32_t)e->n_rx * 520u
                                   : 104u + (uint32_t)e->n_rx * 624u;

    e->running = true;
    e->done_at = e->now + send_us
                 + (x->max_rx == 0u
                        ? 0u
                        : (e->n_rx == 0u ? x->first_us + 1u
                           : text_rate ? 52u + (uint32_t)e->n_rx * 260u + x->gap_us
                                       : answer_us));
    return true;
}

static bool sim_poll(void *ctx, ew_capture_t *out)
{
    sim_t *e = (sim_t *)ctx;

    if (!e->running || e->stuck || (int32_t)(e->now - e->done_at) < 0) {
        return false;
    }
    e->running = false;
    e->line = EW_LINE_UP;
    memset(out, 0, sizeof(*out));
    out->n = e->n_rx;
    out->bad_stop = e->bad_stop;
    out->overflow = e->overflow;
    out->aborted = e->aborted;
    if (e->rx != NULL) {
        memcpy(e->rx, e->reply, e->n_rx);
    }
    return true;
}

static ew_driver_t sim_driver(sim_t *e)
{
    const ew_driver_t d = { e, sim_start, sim_poll, sim_abort, sim_line,
                            sim_now };

    return d;
}

/* --------------------------------------------------- pages of each firmware */

static void sim_s_page(uint8_t *p)
{
    memset(p, 0xFF, 256u);
    p[0x00] = 16u; p[0x01] = 7u; p[0x02] = 33u;
    p[0x09] = 9u; p[0x0B] = 1u; p[0x0D] = 0x55u; p[0x0E] = 0xAAu;
    p[0x0F] = 1u; p[0x15] = 3u; p[0x19] = 37u; p[0x1A] = 208u;
    p[0x1B] = 40u; p[0x1C] = 80u; p[0x1D] = 4u; p[0x1F] = 2u; p[0x21] = 122u;
    p[0x23] = 7u; p[0x24] = 1u; p[0x27] = 0u; p[0x28] = 0u;
    memcpy(p + 0x40, "#BOARD_NAME_ONE#", 16u);
    memcpy(p + 0x50, "#BLHELI$EFM8B21#", 16u);
    memset(p + 0x60, 0x20, 16u);
}

static void sim_bj_page(uint8_t *p)
{
    memset(p, 0xFF, 256u);
    p[0x00] = 0u; p[0x01] = 21u; p[0x02] = 208u;
    p[0x04] = 21u; p[0x05] = 1u; p[0x07] = 5u; p[0x09] = 9u; p[0x0A] = 48u;
    p[0x0B] = 1u; p[0x0D] = 0x55u; p[0x0E] = 0xAAu; p[0x10] = 255u;
    p[0x15] = 4u; p[0x1B] = 40u; p[0x1C] = 80u; p[0x1D] = 4u; p[0x1F] = 2u;
    p[0x23] = 0u; p[0x27] = 0u; p[0x28] = 0u; p[0x29] = 2u; p[0x2A] = 0u;
    memcpy(p + 0x40, "#BOARD_NAME_TWO#", 16u);
    memcpy(p + 0x50, "#BLHELI$EFM8B21#", 16u);
    memcpy(p + 0x60, "Bluejay (.1 RC1)", 16u);
    for (unsigned i = 0x70u; i <= 0xFBu; ++i) {
        p[i] = (uint8_t)(i * 3u);
    }
}

static void sim_am_page(uint8_t *p, uint8_t rev)
{
    memset(p, 0xFF, 192u);
    p[0] = 0x01u; p[1] = rev; p[2] = 2u; p[3] = 2u; p[4] = 21u;
    for (unsigned i = 5u; i < 0x30u; ++i) {
        p[i] = 1u;
    }
    p[0x17] = 26u; p[0x18] = 24u; p[0x19] = 100u; p[0x28] = 5u; p[0x2B] = 90u;
    for (unsigned i = 0x30u; i < 0xB0u; ++i) {
        p[i] = (uint8_t)(i ^ 0x5Au);
    }
}

static void sim_init_efm8(sim_t *e, uint8_t sig, bool bluejay)
{
    const uint8_t a[9] = { 0x34, 0x37, 0x31, 0x64, 0xE8, sig, 0x06, 0x01, 0x30 };

    memset(e, 0, sizeof(*e));
    e->kind = SIM_EFM8;
    memcpy(e->answer, a, 9u);
    e->base = sig == 0xB5u ? 0x3000u : 0x1A00u;
    e->page = sig == 0xB5u ? 2048u : 512u;
    e->boot = sig == 0xB5u ? 0xF000u : 0x1C00u;
    memset(e->flash, 0xFF, sizeof(e->flash));
    if (bluejay) {
        sim_bj_page(e->flash + e->base);
    } else {
        sim_s_page(e->flash + e->base);
    }
    e->line = EW_LINE_PARK;
}

static void sim_init_am(sim_t *e, uint8_t rev, uint8_t protocol)
{
    const uint8_t a[9] = { 0x34, 0x37, 0x31, 0x14, 0x35, 0x06, 0x06, protocol,
                           0x30 };

    memset(e, 0, sizeof(*e));
    e->kind = SIM_AM;
    memcpy(e->answer, a, 9u);
    e->base = 0x0020u;
    memset(e->am, 0xFF, sizeof(e->am));
    sim_am_page(e->am, rev);
    memset(e->am_name, 0xFF, sizeof(e->am_name));
    memcpy(e->am_name, "BOARD_F421", 11u);
    e->am_boot_version = 2u;
    memcpy(e->am_record, "\xDA\xE3\x25\x59\xD9\x63\xB8\x4E", 8u);
    e->am_record[19] = 0x00u;
    e->am_record[20] = 0x10u;
    e->line = EW_LINE_PARK;
}

/* The ESC starts again: forgets the session; a page with no marker is reset
 * as the firmware does it, tags 0xFF. */
static void sim_power_cycle(sim_t *e)
{
    e->contacted = false;
    e->dead = false;
    e->addr_set = false;
    e->announced = 0u;
    e->have_data = false;
    e->swallowed = false;
    e->mute_next = false;
    e->fail = F_NONE;
    if (e->kind == SIM_EFM8) {
        uint8_t *p = e->flash + e->base;

        if (p[0x0D] != 0x55u || p[0x0E] != 0xAAu) {
            sim_s_page(p);
            memset(p + 0x40, 0xFF, 48u);
        }
    }
}

static void sim_script(sim_t *e, uint8_t fault, int at, int n, uint8_t byte)
{
    e->fail = fault;
    e->fail_at = e->starts + at;
    e->fail_n = n;
    e->fail_byte = byte;
}

/* Run a session to the end of its operation, 500us a pass. */
static ew_ses_t sim_run(ew_session_t *s, sim_t *e)
{
    ew_ses_t r = EW_SES_BUSY;

    for (uint32_t i = 0; i < 2000000u && r == EW_SES_BUSY; ++i) {
        e->now += 500u;
        r = ew_session_step(s);
    }
    return r;
}

/* Hold, power, entry to the end. */
static ew_ses_t sim_enter(ew_session_t *s, sim_t *e)
{
    (void)ew_session_hold(s);
    (void)ew_session_enter(s, 3000u);
    return sim_run(s, e);
}

/* A transaction's bytes are this line. */
static bool sim_sent_line(const sim_t *e, int index, const char *line)
{
    const size_t n = strlen(line);

    return index < (int)SIM_LOG_MAX && e->log[index].n == n
           && memcmp(e->log[index].b, line, n) == 0;
}

/* The first bytes of a transaction are these. */
static bool sim_sent(const sim_t *e, int index, const char *hex)
{
    size_t n = 0u;

    while (hex[0] != '\0') {
        unsigned v = 0u;

        for (int k = 0; k < 2; ++k, ++hex) {
            v = v * 16u + (unsigned)(*hex <= '9' ? *hex - '0' : *hex - 'A' + 10);
        }
        if (*hex == ' ') {
            hex++;
        }
        if (index >= (int)SIM_LOG_MAX || e->log[index].b[n] != (uint8_t)v) {
            return false;
        }
        n++;
    }
    return n == e->log[index].n;
}

#endif /* EW_SIM_H */
