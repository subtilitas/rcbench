/**
 * @file
 * @brief The WeAct PD Power Mini V1 Buck on its UART: the codec and a driver
 *        that talks to it one transaction at a time.
 *
 * Implements the UART protocol of the WeAct PD Power Mini V1 Buck module.
 * WeAct is a name or trademark of its owner; it is used here only to say
 * which module this driver works with (NOTICE, "Trademarks").
 *
 * The module's protocol, from the vendor's sheet uart_protocol_v1.1: a
 * request is a command byte, its arguments and a CRC8
 * (polynomial 0x31, initial 0xFF); a read is answered with the command byte,
 * its data and a CRC8.  WHO_AM_I's reply ends in 0x0A by the sheet, and the
 * driver takes either that or a CRC.  A write is answered with nothing at
 * all, so the driver confirms each one with the matching read: the output
 * with READ_OUTPUT_STATE 250ms later -- and reads no state in between, so
 * an early read of a module still settling is not taken for the answer --
 * a set point with READ_OUTPUT_DATA.
 *
 * An OFF goes first, before anything else waiting: an ON or a set point
 * not yet sent is dropped -- an ON also when its set points change -- a
 * read under way other than the state's is left, and an OFF that does not
 * take is written again without pause.  An OFF asked for while an ON is
 * being confirmed is written at once when a read-back has shown which
 * argument means off; before that, the state is read back to back for
 * 1000ms from the ON and the output switched off the moment it reads on.
 * An ON that comes later than that is an output on while OFF is asked,
 * switched off at the next state read: about 500ms on with a module
 * answering at once, up to 1.8s with one answering up to 390ms late, and
 * up to 2.1s while set points change faster than they are written, see
 * below.
 *
 * The readings come round on their periods: the display every 100ms, the
 * state and the input every 500ms, the slot every 1000ms, with a module
 * answering within 12ms.  A module answering later stretches them, each
 * transaction taking its reply time: at rest the input is read 850ms
 * apart at 100ms late, 1.3s at 200ms and 2.5s at 390ms, the state up
 * to 1.5s apart (host model).  A reading PDMINI_POLL_LATE_MS past its due
 * goes before the next set point, one at a time with a set-point job or a
 * read for an ON between two, so set points changed faster than a write
 * and its read-back take -- a slider dragged -- hold no reading back for
 * long.  The state read, and the input read before a write, may follow
 * another reading straight away.  The input is read before a set point
 * write or an ON once it is PDMINI_STATE_MS old, and not between the reads
 * for an ON.  Set points and an ON reach a module answering up to 390ms
 * late (host model, 40 start phases): one set point within 2.5s; an ON
 * within 3.6s at set points already written, up to 7.5s right after a
 * set point change.  With replies 279ms or more late and a set point
 * changed every 50ms, the display read stands 3.7s to 5.0s, past the
 * 3s the panel allows (see the README); from 263ms it stands over 2.9s,
 * which the panel's page read every 100ms can take past 3s.  At rest it
 * stands up to 2.5s at 390ms.  The real module's reply time is not
 * measured.
 *
 * An ON waits for the set points: it is written only once the active slot
 * reads back what is asked, read again straight before the ON together
 * with which slot is active, so the output never comes on at the voltage
 * it held before.  A slot chosen or a set point turned on the module's own
 * buttons in the 15ms between those reads and the ON is seen by the next
 * slot read, at most 1000ms on, and put back.
 *
 * Three state reads in a row that fail take the module for gone, however
 * the other readings are answered: its output state is what matters.
 *
 * A set point is kept PDMINI_CAP_MV under the input the module reports.
 * A live output whose input reads under its set point and
 * PDMINI_HEADROOM_MV on PDMINI_SAG_READS input reads in a row is switched
 * off and held off (st.sagged) until an OFF is asked: a buck fed less than
 * it is set to put out may show ERR and need a power cycle.  The 200mV
 * between the two lets the input drop under load without a cut.  An ON
 * asked while the input reads under 1500mV, with no room for the lowest
 * set point, is not written and is held off the same way.  A rule chosen
 * without a bench measurement -- whether a live module goes to ERR on a
 * sagging input is not measured -- so it may cut a run that would have
 * survived.  An input read that fails neither counts nor clears the run of
 * low readings.  While the output is on, the set point is not lowered to
 * follow a falling input; the rule switches it off instead.  From a sag to
 * the cut takes about 1.0s with a module answering at once, 1.7s at
 * 100ms late, 1.9s at 200ms and up to 4.7s up to 390ms, the input
 * reads stretching as above (host model).
 *
 * A state read can be asked for (pdmini_check_off()): one sent after the
 * asking, whose answer says whether the output is off now.  The SUPPLY
 * page takes a wiring change from a module that has answered only on such
 * a read: the module can switch itself on between two state reads.
 *
 * A module that stops answering while its output is on may still be
 * listening: while an OFF is asked for, it is sent blind straight after
 * each WHO_AM_I that is answered by not one byte, once a second, until a state read
 * shows the output off -- only with an argument a read-back has shown to
 * mean on, so the blind OFF cannot switch on an output that was off.  A
 * module that answers WHO_AM_I is read, never written blind, as it may be
 * another one.  Not closed: a module put in place of the quiet one while a
 * WHO_AM_I is on its way, up to 410ms before the blind OFF, is sent that
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
 * rp2350/pd_uart.c supplies the PIO UART.
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

/** @brief The module's range, from the vendor's page; not measured here. */
#define PDMINI_V_MIN_MV   1000u
#define PDMINI_V_MAX_MV  20000u
#define PDMINI_I_MIN_MA     50u
#define PDMINI_I_MAX_MA   3000u
/**
 * @brief The margin between the input the module reports and a live output's
 *        set point that the buck is taken to need: a buck cannot put out more
 *        than it is fed, and a module asked to shows ERR and needs a power
 *        cycle (seen on the bench, 2026-10-06, 5.88V asked from a 4.88V
 *        input).
 *
 * A live output whose input reads under its set point and this is switched off
 * (PDMINI_SAG_READS).  The margin the module needs is not measured; 4.00V
 * from 4.85V worked.
 */
#define PDMINI_HEADROOM_MV  500u
/**
 * @brief A voltage set point is kept this far under the input the module
 *        reports, rounded down to 10mV, and no lower than PDMINI_V_MIN_MV.
 *        200mV above  PDMINI_HEADROOM_MV, so a set point at the cap is not on
 *        the line its output is cut at: the input is read with the output off,
 *        and under load it drops by the current through the source and the
 *        cable.  200mV is  3000mA, PDMINI_I_MAX_MA, through 1m of 20 AWG
 *        copper out and back,  2 x 33mOhm.
 *
 * The drop on a real source and cable is not measured.
 */
#define PDMINI_CAP_MV       700u

#ifdef __cplusplus
static_assert(PDMINI_CAP_MV > PDMINI_HEADROOM_MV,
              "a set point at the cap lies above the sag cut line");
#else
_Static_assert(PDMINI_CAP_MV > PDMINI_HEADROOM_MV,
               "a set point at the cap lies above the sag cut line");
#endif

/**
 * @brief What WHO_AM_I's reply holds from this module: "WeAct Studio PD Power
 *        Mini V1 BUCK" in the bench station's notes of its 2026-09-11 run.
 *
 * A  reply without it is another device, and nothing is written to it.
 */
#define PDMINI_WHO "PD Power Mini"

/** @brief Commands this driver sends. */
enum {
    PDMINI_OUTPUT_EN    = 0x02,  /**< write the output on or off            */
    PDMINI_OUTPUT_DATA  = 0x04,  /**< write the set points                  */
    PDMINI_SYSTEM_RESET = 0x40,  /**< restart the module                    */
    PDMINI_WHO_AM_I     = 0x81,  /**< identify the module                   */
    PDMINI_READ_STATE   = 0x82,  /**< read the output state                 */
    PDMINI_READ_ID      = 0x83,  /**< read the module ID                    */
    PDMINI_READ_DATA    = 0x84,  /**< read back the set points              */
    PDMINI_READ_DISPLAY = 0x85,  /**< read the terminal voltage and current */
    PDMINI_READ_INPUT   = 0x8A,  /**< read the input state                  */
};

/** @brief READ_OUTPUT_STATE's bits 2..1. */
enum { PDMINI_MODE_NORMAL = 0, PDMINI_MODE_CC = 1, PDMINI_MODE_OC = 2 };

/** @brief Timing, from the vendor's working driver; none of it measured. */
#define PDMINI_ATTACH_MS     5u    /**< pins handed over before a request  */
#define PDMINI_REPLY_MS    400u    /**< the window for a reply             */
#define PDMINI_BYTE_MS      60u    /**< and longer by this on every byte   */
#define PDMINI_TXN_MS      600u    /**< but never past this from the request;
                                        a 37-byte reply is 20ms at 19200   */
#define PDMINI_CONFIRM_MS  250u    /**< OUTPUT_EN to the read that confirms */
#define PDMINI_WATCH_MS   1000u    /**< an ON cancelled before its argument
                                        is known, watched this long         */
#define PDMINI_GAP_MS       10u    /**< pins at rest between transactions  */
#define PDMINI_DISPLAY_MS  100u    /**< how often the output is read       */
#define PDMINI_POLL_LATE_MS 400u  /**< a reading this far past its due
                                        goes before the next set-point
                                        write: set points changed faster
                                        than a write and its read-back take
                                        would otherwise hold the readings
                                        back                               */
#define PDMINI_STATE_MS    500u    /**< the output state and the input     */
#define PDMINI_IDENTIFY_MS 1000u   /**< WHO_AM_I again while nothing answers */
#define PDMINI_SLOT_MS     1000u   /**< the active slot read again          */
#define PDMINI_RETRY_MS    2000u   /**< a write that did not take, again    */
#define PDMINI_FAILS          3u   /**< failed transactions in a row: gone */
#define PDMINI_ABSENT_TRIES  10u   /**< WHO_AM_I unanswered by a byte this
                                        many times, about 10s: a restored
                                        module is taken not to be there   */
#define PDMINI_INPUT_MISSES   3u   /**< READ_INPUT_STATE unanswered this many
                                        times is not asked again until the
                                        module is identified again: the
                                        vendor's client has it from firmware
                                        v1.0.2.0 on                        */
#define PDMINI_INPUT_RETRY_MS 5000u /**< and asked again this often after */
#define PDMINI_SAG_READS      2u   /**< input reads in a row under a live
                                        output's set point and
                                        PDMINI_HEADROOM_MV that switch it
                                        off; chosen, not measured        */

/**
 * @brief CRC8, polynomial 0x31, initial 0xFF, over @p n bytes.
 *
 * @param p  Bytes to check.
 * @param n  Number of bytes in @p p.
 * @return   The CRC8 of the bytes.
 */
uint8_t pdmini_crc8(const uint8_t *p, size_t n);

/**
 * @brief Whether @p buf (@p n bytes received) holds a whole reply to @p cmd.
 */
typedef enum {
    PDMINI_MORE = 0,   /**< not yet                                     */
    PDMINI_DONE,       /**< a whole reply, its check passed             */
    PDMINI_BAD,        /**< a whole reply that fails its check, or junk */
} pdmini_frame_t;

/**
 * @brief Checks the bytes received so far against a reply to @p cmd.
 *
 * @param cmd  Command byte the reply answers.
 * @param buf  Bytes received so far.
 * @param n    Number of bytes in @p buf.
 * @return     PDMINI_MORE, PDMINI_DONE or PDMINI_BAD.
 */
pdmini_frame_t pdmini_reply_frame(uint8_t cmd, const uint8_t *buf, size_t n);

/** @brief What the driver needs from the hardware. */
typedef struct {
    void (*attach)(void *ctx);                       /**< UART onto the pins */
    void (*detach)(void *ctx);                       /**< pins pulled down   */
    void (*send)(void *ctx, const uint8_t *p, size_t n);  /**< sends n bytes at p */
    void *ctx;                          /**< the context passed to each function */
} pdmini_io_t;

/** @brief What the module has said, read back rather than assumed. */
typedef struct {
    bool     online;      /**< it answered WHO_AM_I and has since        */
    bool     output;      /**< READ_OUTPUT_STATE says the output is on   */
    uint8_t  mode;        /**< PDMINI_MODE_*                             */
    uint16_t v_mv,                          /**< voltage at the terminals, READ_OUTPUT_DISPLAY, mV */
             i_ma;                          /**< current at the terminals, READ_OUTPUT_DISPLAY, mA */
    uint16_t set_mv;      /**< the active slot's set points, read back   */
    uint16_t set_ma;                    /**< the active slot's current limit, mA */
    uint8_t  in_state;    /**< READ_INPUT_STATE: 5 PD, 4 QC, 6 DC, ...   */
    uint16_t vin_mv;                    /**< the module's input voltage, mV */
    bool     stuck;       /**< the output would not reach what was asked */
    bool     set_stuck;   /**< the set points would not take             */
    bool     tripped;     /**< the module switched it off; held off      */
    bool     sagged;      /**< switched off here: the input sagged under
                               the set point and the headroom; held off */
    uint32_t samples;     /**< readings of the output taken              */
    uint32_t errors;      /**< transactions that failed                  */
} pdmini_status_t;

/** @brief Driver state: what is asked, what is known, and the transaction
 *         under way. */
typedef struct {
    pdmini_io_t     io;
    pdmini_status_t st;

    /* What is asked for. */
    bool     want_output;
    bool     want_set;       /**< set points named; none written before   */
    uint16_t want_mv, want_ma;

    /* What is known. */
    bool     identified;
    bool     answered;       /**< identified once since pdmini_init()      */
    bool     restored;       /**< wiring from before a restart: maybe on   */
    bool     hurry;          /**< WHO_AM_I again as soon as one has failed:
                                  pdmini_hurry()                          */
    uint8_t  who_misses;     /**< WHO_AM_I answered by not a byte, so far  */
    uint16_t who_failed;     /* WHO_AM_I without a valid answer: a count
                                to compare for a change, wrapping from
                                65535 to 0                              */
    bool     state_known;    /**< READ_OUTPUT_STATE answered since online */
    int      slot;           /**< the active slot, -1 until read           */
    bool     data_known;     /**< set_mv/set_ma read back from it          */
    uint8_t  on_value;       /**< OUTPUT_EN's argument for on; learnt      */
    uint8_t  en_value;       /**< the argument last written                */
    bool     en_for;         /**< the state it was written towards         */
    bool     on_seen;        /**< on_value seen to work once               */
    bool     on_confirmed;   /**< and twice: relied on                     */
    bool     on_sent;        /**< an ON went out, not yet settled by a read */
    bool     reset_owed;     /**< SYSTEM_RESET asked, not yet sent         */
    bool     held_off;       /* switched off for set points that would not
                                take; until an OFF is asked             */
    bool     off_owed;       /**< gone while on: an OFF is sent blind      */
    bool     blind_due;      /**< a WHO_AM_I went unanswered: OFF blind   */
    uint8_t  en_tries;       /**< OUTPUT_EN writes towards the wanted state */
    bool     en_pending;     /**< written, waiting for the confirming read */
    uint32_t en_at;
    bool     data_pending;   /**< OUTPUT_DATA written, not yet read back   */
    uint8_t  data_tries;     /**< OUTPUT_DATA writes towards the set points */
    uint32_t data_at;
    uint8_t  fails;          /**< consecutive                              */
    uint8_t  state_fails;    /**< READ_OUTPUT_STATE failed, consecutive    */
    uint8_t  input_misses;   /**< READ_INPUT_STATE unanswered, consecutive */
    bool     input_known;    /**< READ_INPUT_STATE answered, the last read */
    bool     input_seen;     /**< and this firmware has answered it at all */
    uint8_t  on_step;        /* reads before an ON: 1 data asked, 2 data
                                seen, 3 slot asked, 4 slot seen          */
    bool     polled_late;    /* the last job a reading that went before
                                the set points: an overdue one, or the
                                input read before a set point or an ON  */
    uint8_t  sag_reads;      /* input reads in a row under a live output's
                                set point and the headroom              */
    uint8_t  check;          /**< pdmini_check_t, PDMINI_CHECK_SENT between */

    /* The transaction under way. */
    enum { PD_IDLE, PD_ATTACH, PD_WAIT, PD_GAP } phase;
    uint8_t  req[8];
    size_t   req_n;
    uint8_t  cmd;
    bool     write;          /**< answered by nothing                     */
    uint8_t  rx[72];
    size_t   rx_n;
    uint32_t t;              /**< when the phase began, or the deadline   */
    uint32_t deadline;

    uint32_t last_display, last_state, last_input, last_identify, last_slot;
} pdmini_t;

/**
 * @brief Set up a driver with nothing asked and nothing known.
 *
 * @param d       Driver state to initialise.
 * @param io      Hardware callbacks; copied.
 * @param now_ms  Current time, in milliseconds.
 */
void pdmini_init(pdmini_t *d, const pdmini_io_t *io, uint32_t now_ms);

/** @brief What a state read asked for with pdmini_check_off() has shown. */
typedef enum {
    PDMINI_CHECK_NONE = 0,  /**< none asked                               */
    PDMINI_CHECK_ASKED,     /**< asked, no state read sent since          */
    PDMINI_CHECK_SENT,      /**< a state read sent since, not yet answered */
    PDMINI_CHECK_OFF,       /**< it showed the output off, and nothing
                                 that may be on (pdmini_may_be_on())      */
    PDMINI_CHECK_NOT_OFF,   /**< it showed the output on, or something
                                 that may be on, or it failed, or the
                                 module was not identified to be read      */
} pdmini_check_t;

/**
 * @brief A state read sent from now on, ahead of the other readings: the
 *        answer is in pdmini_checked().
 *
 * It waits for the transaction under way and for an OFF owed, so at most about
 * two transactions, 1210ms.
 *
 * @param d  Driver state.
 */
void pdmini_check_off(pdmini_t *d);

/**
 * @brief The answer to the last pdmini_check_off().
 *
 * @param d  Driver state.
 * @return   The state of the check.
 */
pdmini_check_t pdmini_checked(const pdmini_t *d);

/**
 * @brief What the output is to be.
 *
 * Set points are clamped to the module's range;  switching off is the first
 * thing done whatever else is waiting.  An output switched off because its set
 * points would not take stays off, whatever is asked, until an OFF is asked --
 * and so does one the module switched off by itself while ON was asked
 * (st.tripped), and one switched off for a sagging input (st.sagged).
 *
 * @param d        Driver state.
 * @param output   true to ask for the output on.
 * @param set_mv   Voltage set point, in mV.
 * @param set_ma   Current set point, in mA.
 */
void pdmini_want(pdmini_t *d, bool output, uint16_t set_mv, uint16_t set_ma);

/**
 * @brief Attached to a module whose state is not known -- wiring restored after
 *        a restart, or newly given: it may be on.  pdmini_may_be_on() says so
 *        until a state read shows it off, or until PDMINI_ABSENT_TRIES WHO_AM_I
 *        in a row go unanswered by a byte.
 *
 * @param d  Driver state.
 */
void pdmini_restored(pdmini_t *d);

/**
 * @brief WHO_AM_I again as soon as the last one has failed, rather than
 *        PDMINI_IDENTIFY_MS after it, while @p on.
 *
 * For a caller that has to reach a module that may be on as soon as it can:
 * a start-up after a reset, with the module's rate still to be found.  Each
 * WHO_AM_I that nothing answers then takes PDMINI_ATTACH_MS +
 * PDMINI_REPLY_MS + PDMINI_GAP_MS, 415ms, so seven rates are tried in
 * 2.9s rather than 7s.  It changes nothing once the module is identified.
 *
 * @param d  Driver state.
 * @param on true to hurry, false for the PDMINI_IDENTIFY_MS period.
 */
void pdmini_hurry(pdmini_t *d, bool on);

/**
 * @brief Restart the module with SYSTEM_RESET: sent once it has answered, its
 *        output reads off and nothing else waits; it is then asked who it is
 *        again, PDMINI_IDENTIFY_MS on.
 *
 * @param d  Driver state.
 */
void pdmini_reset(pdmini_t *d);

/**
 * @brief The output off, and no set points asked: the module's are left as
 *        they are until pdmini_want() names some.
 *
 * @param d  Driver state.
 */
void pdmini_want_off(pdmini_t *d);

/**
 * @brief A byte from the module.
 *
 * @param d       Driver state.
 * @param byte    Received byte.
 * @param now_ms  Current time, in milliseconds.
 */
void pdmini_rx(pdmini_t *d, uint8_t byte, uint32_t now_ms);

/**
 * @brief Run one pass of the driver.
 *
 * Call every pass.
 *
 * @param d       Driver state.
 * @param now_ms  Current time, in milliseconds.
 */
void pdmini_step(pdmini_t *d, uint32_t now_ms);

/**
 * @brief The driver's status, read back from the module.
 *
 * @param d  Driver state.
 * @return   Pointer to the status inside @p d.
 */
const pdmini_status_t *pdmini_status(const pdmini_t *d);

/**
 * @brief Whether the output is on or may be: it reads on, an ON waits to be
 *        sent or confirmed or went out unsettled, an OFF is owed to a module
 *        that went quiet with it on, or a module that has answered is not read
 *        off now.
 *
 * Its wiring is not to be taken from under it while this holds.
 *
 * @param d  Driver state.
 * @return   true while the output is, or may be, on.
 */
bool pdmini_may_be_on(const pdmini_t *d);

/**
 * @brief The highest voltage set point written for an input of vin_mv, as read.
 *
 * vin_mv - PDMINI_CAP_MV rounded down to 10mV, at least PDMINI_V_MIN_MV
 * (an input under PDMINI_V_MIN_MV + PDMINI_CAP_MV, 1700mV, gets the
 * 1000mV floor, and one under 1500mV is held off), at most
 * PDMINI_V_MAX_MV.  The caller treats an input not known as PDMINI_V_MAX_MV.
 *
 * @param vin_mv Input voltage as read, in mV.
 * @return The highest voltage set point for that input, in mV.
 */
uint16_t pdmini_cap_mv(uint32_t vin_mv);

#ifdef __cplusplus
}
#endif
