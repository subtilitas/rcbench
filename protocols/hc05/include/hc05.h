/**
 * @file
 * @brief HC-05 AT set-up as an SPP master: the exchange state machine
 *        fed bytes and a clock, telling its caller what to send, which
 *        baud to use and where to hold KEY.  No pin driver.
 *
 * Operations: PROBE (AT at 115200, 38400 and 9600 baud, then AT+VERSION?),
 * SCAN (AT+ROLE=1, AT+INIT, ERROR:(17) ignored; AT+INQM=1,HC05_INQ_MAX,n;
 * AT+INQ, each +INQ: line a device; AT+RNAME? per address) and BIND
 * (AT+ROLE=1, AT+PSWD=\<pin\>, on ERROR a second try quoted for firmware
 * 3.0, AT+PAIR=\<addr\>,20, AT+BIND=\<addr\>, AT+CMODE=0, AT+UART=115200,0,0,
 * AT+RESET), and ANY (AT+ROLE=1, AT+PSWD, AT+CMODE=1, AT+UART, AT+RESET:
 * connect to any slave, the fallback when INQ answers ERROR:(1F)).
 * Addresses: +INQ and +BIND? print `3014:10:171179` with leading zeros
 * dropped; a set command takes `3014,10,171179`.  Lines end in CR LF; an
 * answer is OK, ERROR:(hh), FAIL or a +KEY:value line.  KEY stays high
 * from the first byte of an operation to its end: INQ and RNAME? answer
 * only while it is.  Timeouts: HC05_AT_MS per answer, HC05_INQ_MS(n) for
 * INQ, HC05_RNAME_MS, HC05_PAIR_MS, HC05_RESET_MS.
 *
 * Pure C: no clock read, no I/O.  Times are unsigned differences of a
 * free-running 32-bit ms clock.  HC-05 is a product name used only to say
 * which module this talks to (README.md, "Trademarks").
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HC05_H
#define HC05_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HC05_ADDR_MAX    16u   /**< "3014:10:171179" + NUL */
#define HC05_NAME_MAX    32u   /**< the remote name, truncated, + NUL */
#define HC05_VERSION_MAX 24u   /**< "3.0-20170601" + NUL */
#define HC05_PIN_MAX     17u   /**< up to 16 digits + NUL */
#define HC05_FOUND_MAX   8u    /**< devices a scan keeps */
#define HC05_LINE_MAX    96u   /**< bytes of one answer line held */
#define HC05_OUT_MAX     64u   /**< bytes of one command */
#define HC05_INQ_MAX     9u    /**< devices INQ reports at most */
#define HC05_INQ_UNITS   9u    /**< INQ time units of 1.28s: 11.5s */

/** @brief The timeouts and waits, in ms. @{ */
#define HC05_AT_MS       1000u   /**< one command's answer */
#define HC05_INQ_MS(n)   ((n) * 1280u + 2000u) /**< AT+INQ of n units */
#define HC05_RNAME_MS    3000u   /**< AT+RNAME? */
#define HC05_PAIR_MS     22000u  /**< AT+PAIR, 20s pairing plus 2s */
#define HC05_RESET_MS    1500u   /**< AT+RESET */
#define HC05_KEY_SETTLE_MS 100u  /**< KEY raised to the first byte */
/** @} */

/** @brief The data bauds PROBE tries, in order. @{ */
#define HC05_BAUD_DATA   115200u /**< the sensor's; BIND and ANY set it */
#define HC05_BAUD_ALT    38400u  /**< full AT mode's */
#define HC05_BAUD_OLD    9600u   /**< the factory data baud */
/** @} */

/** @brief What the caller does next. */
typedef enum {
    HC05_ACT_NONE = 0,   /**< nothing: idle, or waiting for bytes or time */
    HC05_ACT_KEY,        /**< drive KEY to key, then wait HC05_KEY_SETTLE_MS */
    HC05_ACT_BAUD,       /**< set the UART to baud */
    HC05_ACT_SEND,       /**< send data, len bytes */
} hc05_act_kind_t;

/** @brief One action for the caller. */
typedef struct {
    hc05_act_kind_t kind;   /**< what to do */
    bool     key;           /**< KEY: the level, true high */
    uint32_t baud;          /**< BAUD: the baud */
    uint8_t  data[HC05_OUT_MAX]; /**< SEND: the bytes, CR LF included */
    size_t   len;           /**< SEND: their count */
} hc05_act_t;

/** @brief The operations. */
typedef enum {
    HC05_OP_NONE = 0, HC05_OP_PROBE, HC05_OP_SCAN, HC05_OP_BIND, HC05_OP_ANY,
} hc05_op_t;

/** @brief Where an operation stands. */
typedef enum {
    HC05_ST_IDLE = 0, HC05_ST_RUNNING, HC05_ST_DONE, HC05_ST_FAILED,
} hc05_state_t;

/** @brief Why an operation failed. */
typedef enum {
    HC05_ERR_NONE = 0,     /**< none, or ended by hc05_abort() */
    HC05_ERR_NO_AT,        /**< no OK at any baud: no module, KEY not high */
    HC05_ERR_TIMEOUT,      /**< an answer did not come */
    HC05_ERR_REFUSED,      /**< ERROR:(code) or FAIL; code in hc05_t.err_code */
    HC05_ERR_INQ_REFUSED,  /**< AT+INQ answered ERROR:(1F): offer ANY */
    HC05_ERR_PAIR,         /**< AT+PAIR did not end in OK */
    HC05_ERR_NONE_FOUND,   /**< the scan ended with no device */
    HC05_ERR_ARG,          /**< a bad address or pin, or busy */
} hc05_err_t;

/** @brief A device a scan found. */
typedef struct {
    char addr[HC05_ADDR_MAX];   /**< as +INQ printed it, colons */
    char name[HC05_NAME_MAX];   /**< from +RNAME:, "" when it did not answer */
    uint16_t dev_class;         /**< the +INQ class word */
    int16_t  rssi;              /**< the +INQ RSSI word, signed */
} hc05_dev_t;

/** @brief One HC-05's machine. */
typedef struct {
    hc05_op_t    op;            /**< the last operation started */
    hc05_state_t state;         /**< where it stands */
    hc05_err_t   err;           /**< why it failed */
    uint8_t      err_code;      /**< the hh of ERROR:(hh) */
    uint8_t      step;          /**< private */
    uint8_t      baud_try;      /**< private */
    uint8_t      tries;         /**< private */
    uint32_t     baud;          /**< the baud the module answered at */
    uint32_t     sent_ms;       /**< when the last command went out */
    uint32_t     wait_ms;       /**< its timeout */
    bool         key;           /**< KEY as asked */
    bool         pswd_quoted;   /**< firmware 3.0: the PIN went quoted */
    char         version[HC05_VERSION_MAX]; /**< PROBE's +VERSION: text */
    char         pin[HC05_PIN_MAX]; /**< BIND's and ANY's PIN */
    char         target[HC05_ADDR_MAX];   /**< BIND's address, colons */
    hc05_dev_t   found[HC05_FOUND_MAX]; /**< SCAN's devices */
    uint8_t      found_n;       /**< how many */
    uint8_t      name_i;        /**< RNAME? under way for found[name_i] */
    char         line[HC05_LINE_MAX]; /**< private: the line being read */
    uint8_t      line_n;        /**< private: its length */
    bool         act_due;       /**< an action waits for hc05_step() */
    hc05_act_t   act;           /**< private: that action */
} hc05_t;

/** @brief Clear to idle. @param h The machine; NULL does nothing. */
void hc05_init(hc05_t *h);

/** @brief Start PROBE. @param h The machine. @param now_ms The clock.
 *  @return false when busy or NULL. */
bool hc05_probe(hc05_t *h, uint32_t now_ms);
/** @brief Start SCAN. @param h The machine. @param now_ms The clock.
 *  @return false when busy or NULL. */
bool hc05_scan(hc05_t *h, uint32_t now_ms);
/**
 * @brief Start BIND to @p addr (colons or commas) with @p pin (1 to 16
 *        digits).
 * @param h      The machine.
 * @param addr   The sensor's address.
 * @param pin    Its PIN.
 * @param now_ms The clock.
 * @return false when busy, NULL, or an address or pin the forms above do
 *         not take (hc05_t.err HC05_ERR_ARG).
 */
bool hc05_bind(hc05_t *h, const char *addr, const char *pin, uint32_t now_ms);
/** @brief Start ANY: connect to any slave with @p pin. @param h The
 *  machine. @param pin The PIN. @param now_ms The clock.
 *  @return as hc05_bind(). */
bool hc05_any(hc05_t *h, const char *pin, uint32_t now_ms);
/** @brief End the running operation as FAILED with HC05_ERR_NONE; KEY is
 *         asked low on the next step.  @param h The machine; NULL does
 *         nothing. */
void hc05_abort(hc05_t *h);

/** @brief Feed bytes from the module. @param h The machine. @param b Bytes.
 *         @param n Count. */
void hc05_feed(hc05_t *h, const uint8_t *b, size_t n);

/**
 * @brief Advance to @p now_ms and take the next action.
 * @param h      The machine.
 * @param now_ms The clock.
 * @param out    Receives the action; kind NONE when there is none.
 * @return true when @p out holds an action other than NONE.
 */
bool hc05_step(hc05_t *h, uint32_t now_ms, hc05_act_t *out);

/** @brief Whether an operation runs. @param h The machine.
 *  @return true while RUNNING; false for NULL. */
bool hc05_busy(const hc05_t *h);
/** @brief Convert `3014:10:171179` to `3014,10,171179` into @p out (cap
 *         HC05_ADDR_MAX). @param addr The address. @param out Receives
 *         it. @param cap The size of @p out. @return false for a form that is not
 *         hex,hex,hex with 1 to 4, 1 to 2 and 1 to 6 digits. */
bool hc05_addr_cmd(const char *addr, char *out, size_t cap);
/** @brief The reverse: commas to colons. @param addr The address.
 *  @param out Receives it. @param cap The size of @p out.
 *  @return as hc05_addr_cmd(). */
bool hc05_addr_show(const char *addr, char *out, size_t cap);
/** @brief The kinds of answer line hc05_parse_line() tells apart. */
typedef enum {
    HC05_LINE_OTHER = 0, HC05_LINE_OK, HC05_LINE_ERROR, HC05_LINE_FAIL,
    HC05_LINE_INQ, HC05_LINE_RNAME, HC05_LINE_VERSION, HC05_LINE_STATE,
    HC05_LINE_BIND,
} hc05_line_t;
/** @brief Parse one answer line (without CR LF) for the tests and the
 *         console: OK, ERROR:(hh), FAIL, +INQ:, +RNAME:, +VERSION:,
 *         +STATE:, +BIND:.  @param line The line. @param value Receives
 *         the text after a +KEY: prefix, "" otherwise. @param cap The size
 *         of @p value. @param code Receives the hh of ERROR:(hh), else 0.
 *         @return the kind as hc05_line_t. */
hc05_line_t hc05_parse_line(const char *line, char *value, size_t cap,
                            uint8_t *code);

#ifdef __cplusplus
}
#endif
#endif
