/*
 * The Hitec bench harness: the command lines a USB (Universal Serial Bus)
 * serial host sends, and the lines that report a transaction.  Pure C over
 * protocols/hitec; the RP2350 build (../rp2350) runs the commands on a
 * hitec_line_t, and the host suite drives the parser and the formatter.
 *
 * One command a line, at most HH_LINE_MAX characters, LF or CRLF.  Words
 * are separated by spaces.  A number is decimal or 0x hex.  Options are
 * key=value words after the positional ones, in any order.
 *
 *   hello                         ok hello rcbench-hitec-harness proto=1
 *   help                          the command list, then ok help
 *   status                        ok status open= busy= family= pin=
 *                                 drive= mark=
 *   open <classic|d> <pin> [od|pp] [mark=<pin>]
 *                                 ok open family= pin= drive= bit_ns=
 *                                 div256= err_ppm= mark=
 *   close                         ok close
 *   c <cmd> <p1> <p2>             classic command, hitec_c_xfer()
 *   dread <addr> [id=<id>]        D series read, hitec_d_read_xfer()
 *   dwrite <addr> <value> [id=]   D series write, hitec_d_write_xfer()
 *   send <b0> [b1 .. b7]          raw SEND of 1 to 8 bytes
 *   listen <window_us> <rx> [answer=<b>] [want=<b>]
 *   level <0|1> <us>              hold the line, then release it
 *   abort                         cut the running transaction
 *
 * c, dread, dwrite and send take lead=<us>, window=<us>, gap=<bits> and
 * rx=<n>, which replace the builder's values.  A transaction answers
 *
 *   ok start kind= family= tx=<hex bytes> lead_us= gap= window_us= rx=
 *        answer= level= level_us= lead_ns= span_us=      (one line)
 *   cap n= full= answered= overflow= aborted= window_ns= t_us=
 *   byte <k> at_ns= value= framing=                      (one per byte)
 *   dec kind=<c|d|listen|none> rx=<code> value= id= n= delay_ns=
 *   done
 *
 * Errors are one line, err <code> [detail], and nothing runs.  While a
 * transaction runs only abort and status are taken; everything else gets
 * err busy.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hitec_pio.h"
#include "hitec_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HH_PROTOCOL  1
#define HH_LINE_MAX  127u   /* characters, without the LF */
#define HH_OUT_MAX   256u   /* characters of one reply line, with the NUL */
#define HH_PIN_MAX   47u    /* the highest RP2350B GPIO */
#define HH_NO_PIN    0xFFu

typedef enum {
    HH_HELLO = 0,
    HH_HELP,
    HH_STATUS,
    HH_OPEN,
    HH_CLOSE,
    HH_ABORT,
    HH_C,
    HH_DREAD,
    HH_DWRITE,
    HH_SEND,
    HH_LISTEN,
    HH_LEVEL,
} hh_verb_t;

/** How the capture of a transaction is decoded. */
typedef enum {
    HH_DEC_NONE = 0,
    HH_DEC_C,        /* hitec_c_decode(), with expect */
    HH_DEC_D,        /* hitec_d_decode(), with addr */
    HH_DEC_LISTEN,   /* hitec_listen_decode(), with want */
} hh_dec_t;

/** What the open line is, for the parser: transactions need its family. */
typedef struct {
    bool    open;
    uint8_t family;   /* hitec_family_t */
} hh_line_t;

typedef struct {
    uint8_t      verb;      /* hh_verb_t */
    /* open */
    uint8_t      family;
    uint8_t      pin;
    uint8_t      drive;     /* hitec_drive_t */
    uint8_t      mark;      /* HH_NO_PIN: none */
    /* transactions */
    hitec_xfer_t xfer;
    uint8_t      dec;       /* hh_dec_t */
    uint8_t      expect;    /* hitec_c_expect_t */
    uint8_t      addr;      /* D: the register read */
    uint8_t      want;      /* listen: the byte asked for */
} hh_cmd_t;

/**
 * Parse @p line (no LF) for a line in state @p line_state.  True and
 * @p out filled, or false and @p err holding the reply, `err <code>
 * [detail]`, at most @p err_len characters with the NUL.
 */
bool hh_parse(const char *line, const hh_line_t *line_state, hh_cmd_t *out,
              char *err, size_t err_len);

/** True for the verbs that start a transaction. */
bool hh_is_xfer(uint8_t verb);

/** The name of @p code, as hitec_rx_t names it without HITEC_RX_ERR_: OK,
 *  ARG, ..., SUM; "?" for a code outside the enum. */
const char *hh_rx_name(hitec_rx_t code);

/** Decode @p cap as @p cmd asks; HITEC_RX_OK and @p out zeroed for
 *  HH_DEC_NONE. */
hitec_rx_t hh_decode(const hh_cmd_t *cmd, const hitec_capture_t *cap,
                     hitec_reply_t *out);

/** A line of a report, with no LF. */
typedef void (*hh_emit_t)(void *ctx, const char *line);

/** The `ok start` line of @p cmd, laid out by the driver as @p run. */
void hh_report_start(const hh_cmd_t *cmd, const hitec_pio_run_t *run,
                     hh_emit_t emit, void *ctx);

/** The cap, byte, dec and done lines of a finished transaction; @p t_us
 *  is the time from start() to the poll that returned the capture. */
void hh_report_capture(const hh_cmd_t *cmd, const hitec_capture_t *cap,
                       uint32_t t_us, hh_emit_t emit, void *ctx);

/** The help lines, then `ok help`. */
void hh_report_help(hh_emit_t emit, void *ctx);

#ifdef __cplusplus
}
#endif
