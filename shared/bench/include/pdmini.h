/*
 * The WeAct PD Power Mini V1 Buck on its UART: the codec and a driver that
 * talks to it one transaction at a time.
 *
 * The module's protocol, from the vendor's sheet uart_protocol_v1.1: a
 * request is a command byte, its arguments and a CRC8
 * (polynomial 0x31, initial 0xFF); a read is answered with the command byte,
 * its data and a CRC8.  WHO_AM_I's reply ends in 0x0A by the sheet, and the
 * driver takes either that or a CRC.  A write is answered with nothing at
 * all, so the driver confirms each one with the matching read: the output
 * with READ_OUTPUT_STATE 250 ms later -- and reads no state in between, so
 * an early read of a module still settling is not taken for the answer --
 * a set point with READ_OUTPUT_DATA.
 *
 * An OFF goes first, before anything else waiting: an ON or a set point
 * not yet sent is dropped -- an ON also when its set points change -- a read under way other than the state's is
 * left, and an OFF that does not take is written again without pause.  An OFF asked for while
 * an ON is being confirmed is written at once when a read-back has shown
 * which argument means off; before that, the state is read back to back
 * for 1000 ms from the ON and the output switched off the moment it reads
 * on.  An ON that comes later than that is an output on while OFF is
 * asked, switched off at the next state read, at most 500 ms on.
 *
 * An ON waits for the set points: it is written only once the active slot
 * reads back what is asked, read again straight before the ON together
 * with which slot is active, so the output never comes on at the voltage
 * it held before.  A slot chosen or a set point turned on the module's own
 * buttons in the 15 ms between those reads and the ON is seen by the next
 * slot read, at most 1000 ms on, and put back.
 *
 * Three state reads in a row that fail take the module for gone, however
 * the other readings are answered: its output state is what matters.
 *
 * A module that stops answering while its output is on may still be
 * listening: while an OFF is asked for, it is sent blind straight after
 * each WHO_AM_I that is answered by not one byte, once a second, until a state read
 * shows the output off -- only with an argument a read-back has shown to
 * mean on, so the blind OFF cannot switch on an output that was off.  A
 * module that answers WHO_AM_I is read, never written blind, as it may be
 * another one.  Not closed: a module put in place of the quiet one while a
 * WHO_AM_I is on its way, up to 410 ms before the blind OFF, is sent that
 * OFF, and one that reads OUTPUT_EN the other way round comes on until it
 * has answered, been read and been switched off.
 *
 * Four writes of OUTPUT_EN that do not take put the argument in doubt again,
 * and both are tried, in case the module was swapped between two reads.
 *
 * Which OUTPUT_EN argument means on is learnt from the module rather than
 * taken from the sheet, which has it backwards: 1 first, the bench's and the
 * vendor's Python's, then 0.  It is kept across a module going quiet and
 * answering again.  It is learnt only from an output seen to come
 * on after a write towards on -- relied on once two ONs with it have taken,
 * as the module's button or AUTO OUT can switch it on in the same moment --
 * never from one seen to go off, which the module's overcurrent protection
 * does by itself.  The output is written only when it reads otherwise than
 * asked, so an OFF goes only to an output that is on and the
 * learning cannot switch on one that was off.
 *
 * The UART is attached to the pins for one transaction and the pins are
 * pulled-down inputs between them, because a TX idling high back-feeds an
 * unpowered module through its RX and stops it starting.  The io callbacks
 * do that; the driver says when.
 *
 * Non-blocking: the caller feeds received bytes to pdmini_rx() and calls
 * pdmini_step() every pass.  Pure C, host-tested against a modelled module;
 * the coprocessor supplies the PIO UART.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The module's range, from the vendor's page; not measured here. */
#define PDMINI_V_MIN_MV   1000u
#define PDMINI_V_MAX_MV  20000u
#define PDMINI_I_MIN_MA     50u
#define PDMINI_I_MAX_MA   3000u
/** A voltage set point is kept this far under the input the module reports:
 *  a buck cannot put out more than it is fed, and a module asked to shows
 *  ERR and needs a power cycle (seen on the bench, 2026-10-06, 5.88 V asked
 *  from a 4.88 V input).  The margin it needs is not measured; 4.00 V from
 *  4.85 V worked. */
#define PDMINI_HEADROOM_MV  500u

/** What WHO_AM_I's reply holds from this module: "WeAct Studio PD Power
 *  Mini V1 BUCK" in the bench station's notes of its 2026-09-11 run.  A
 *  reply without it is another device, and nothing is written to it. */
#define PDMINI_WHO "PD Power Mini"

/** Commands this driver sends. */
enum {
    PDMINI_OUTPUT_EN    = 0x02,
    PDMINI_OUTPUT_DATA  = 0x04,
    PDMINI_WHO_AM_I     = 0x81,
    PDMINI_READ_STATE   = 0x82,
    PDMINI_READ_ID      = 0x83,
    PDMINI_READ_DATA    = 0x84,
    PDMINI_READ_DISPLAY = 0x85,
    PDMINI_READ_INPUT   = 0x8A,
};

/** READ_OUTPUT_STATE's bits 2..1. */
enum { PDMINI_MODE_NORMAL = 0, PDMINI_MODE_CC = 1, PDMINI_MODE_OC = 2 };

/** Timing, from the vendor's working driver; none of it measured. */
#define PDMINI_ATTACH_MS     5u    /**< pins handed over before a request  */
#define PDMINI_REPLY_MS    400u    /**< the window for a reply             */
#define PDMINI_BYTE_MS      60u    /**< and longer by this on every byte   */
#define PDMINI_TXN_MS      600u    /**< but never past this from the request;
                                        a 37-byte reply is 20 ms at 19200  */
#define PDMINI_CONFIRM_MS  250u    /**< OUTPUT_EN to the read that confirms */
#define PDMINI_WATCH_MS   1000u    /**< an ON cancelled before its argument
                                        is known, watched this long         */
#define PDMINI_GAP_MS       10u    /**< pins at rest between transactions  */
#define PDMINI_DISPLAY_MS  100u    /**< how often the output is read       */
#define PDMINI_STATE_MS    500u    /**< the output state and the input     */
#define PDMINI_IDENTIFY_MS 1000u   /**< WHO_AM_I again while nothing answers */
#define PDMINI_SLOT_MS     1000u   /**< the active slot read again          */
#define PDMINI_RETRY_MS    2000u   /**< a write that did not take, again    */
#define PDMINI_FAILS          3u   /**< failed transactions in a row: gone */
#define PDMINI_ABSENT_TRIES  10u   /**< WHO_AM_I unanswered by a byte this
                                        many times, about 10 s: a restored
                                        module is taken not to be there   */
#define PDMINI_INPUT_MISSES   3u   /**< READ_INPUT_STATE unanswered this many
                                        times is not asked again until the
                                        module is identified again: the
                                        vendor's client has it from firmware
                                        v1.0.2.0 on                        */
#define PDMINI_INPUT_RETRY_MS 5000u /**< and asked again this often after */

/** CRC8, polynomial 0x31, initial 0xFF, over @p n bytes. */
uint8_t pdmini_crc8(const uint8_t *p, size_t n);

/** Whether @p buf (@p n bytes received) holds a whole reply to @p cmd. */
typedef enum {
    PDMINI_MORE = 0,   /**< not yet                                     */
    PDMINI_DONE,       /**< a whole reply, its check passed             */
    PDMINI_BAD,        /**< a whole reply that fails its check, or junk */
} pdmini_frame_t;

pdmini_frame_t pdmini_reply_frame(uint8_t cmd, const uint8_t *buf, size_t n);

/** What the driver needs from the hardware. */
typedef struct {
    void (*attach)(void *ctx);                       /**< UART onto the pins */
    void (*detach)(void *ctx);                       /**< pins pulled down   */
    void (*send)(void *ctx, const uint8_t *p, size_t n);
    void *ctx;
} pdmini_io_t;

/** What the module has said, read back rather than assumed. */
typedef struct {
    bool     online;      /**< it answered WHO_AM_I and has since        */
    bool     output;      /**< READ_OUTPUT_STATE says the output is on   */
    uint8_t  mode;        /**< PDMINI_MODE_*                             */
    uint16_t v_mv, i_ma;  /**< at the terminals, READ_OUTPUT_DISPLAY     */
    uint16_t set_mv;      /**< the active slot's set points, read back   */
    uint16_t set_ma;
    uint8_t  in_state;    /**< READ_INPUT_STATE: 5 PD, 4 QC, 6 DC, ...   */
    uint16_t vin_mv;
    bool     stuck;       /**< the output would not reach what was asked */
    bool     set_stuck;   /**< the set points would not take             */
    bool     tripped;     /**< the module switched it off; held off      */
    uint32_t samples;     /**< readings of the output taken              */
    uint32_t errors;      /**< transactions that failed                  */
} pdmini_status_t;

typedef struct {
    pdmini_io_t     io;
    pdmini_status_t st;

    /* What is asked for. */
    bool     want_output;
    bool     want_set;       /* set points named; none written before   */
    uint16_t want_mv, want_ma;

    /* What is known. */
    bool     identified;
    bool     answered;       /* identified once since pdmini_init()      */
    bool     restored;       /* wiring from before a restart: maybe on   */
    uint8_t  who_misses;     /* WHO_AM_I answered by not a byte, so far  */
    uint16_t who_failed;     /* WHO_AM_I without a valid answer, all told */
    bool     state_known;    /* READ_OUTPUT_STATE answered since online */
    int      slot;           /* the active slot, -1 until read           */
    bool     data_known;     /* set_mv/set_ma read back from it          */
    uint8_t  on_value;       /* OUTPUT_EN's argument for on; learnt      */
    uint8_t  en_value;       /* the argument last written                */
    bool     en_for;         /* the state it was written towards         */
    bool     on_seen;        /* on_value seen to work once               */
    bool     on_confirmed;   /* and twice: relied on                     */
    bool     on_sent;        /* an ON went out, not yet settled by a read */
    bool     held_off;       /* switched off for set points that would not
                                take; until an OFF is asked             */
    bool     off_owed;       /* gone while on: an OFF is sent blind      */
    bool     blind_due;      /* a WHO_AM_I went unanswered: OFF blind   */
    uint8_t  en_tries;       /* OUTPUT_EN writes towards the wanted state */
    bool     en_pending;     /* written, waiting for the confirming read */
    uint32_t en_at;
    bool     data_pending;   /* OUTPUT_DATA written, not yet read back   */
    uint8_t  data_tries;     /* OUTPUT_DATA writes towards the set points */
    uint32_t data_at;
    uint8_t  fails;          /* consecutive                              */
    uint8_t  state_fails;    /* READ_OUTPUT_STATE failed, consecutive    */
    uint8_t  input_misses;   /* READ_INPUT_STATE unanswered, consecutive */
    bool     input_known;    /* READ_INPUT_STATE answered, the last read */
    bool     input_seen;     /* and this firmware has answered it at all */
    uint8_t  on_step;        /* reads before an ON: 1 data asked, 2 data
                                seen, 3 slot asked, 4 slot seen          */

    /* The transaction under way. */
    enum { PD_IDLE, PD_ATTACH, PD_WAIT, PD_GAP } phase;
    uint8_t  req[8];
    size_t   req_n;
    uint8_t  cmd;
    bool     write;          /* answered by nothing                     */
    uint8_t  rx[72];
    size_t   rx_n;
    uint32_t t;              /* when the phase began, or the deadline   */
    uint32_t deadline;

    uint32_t last_display, last_state, last_input, last_identify, last_slot;
} pdmini_t;

void pdmini_init(pdmini_t *d, const pdmini_io_t *io, uint32_t now_ms);

/** What the output is to be.  Set points are clamped to the module's range;
 *  switching off is the first thing done whatever else is waiting.  An
 *  output switched off because its set points would not take stays off,
 *  whatever is asked, until an OFF is asked -- and so does one the module
 *  switched off by itself while ON was asked (st.tripped). */
void pdmini_want(pdmini_t *d, bool output, uint16_t set_mv, uint16_t set_ma);

/** Attached to a module whose state is not known -- wiring restored after
 *  a restart, or newly given: it may be on.  pdmini_may_be_on() says so
 *  until a state read shows it off, or until PDMINI_ABSENT_TRIES WHO_AM_I
 *  in a row go unanswered by a byte. */
void pdmini_restored(pdmini_t *d);

/** The output off, and no set points asked: the module's are left as
 *  they are until pdmini_want() names some. */
void pdmini_want_off(pdmini_t *d);

/** A byte from the module. */
void pdmini_rx(pdmini_t *d, uint8_t byte, uint32_t now_ms);

/** Every pass. */
void pdmini_step(pdmini_t *d, uint32_t now_ms);

const pdmini_status_t *pdmini_status(const pdmini_t *d);

/** Whether the output is on or may be: it reads on, an ON waits to be sent
 *  or confirmed or went out unsettled, an OFF is owed to a module that went
 *  quiet with it on, or a module that has answered is not read off now.
 *  Its wiring is not to be taken from under it while this holds. */
bool pdmini_may_be_on(const pdmini_t *d);

#ifdef __cplusplus
}
#endif
