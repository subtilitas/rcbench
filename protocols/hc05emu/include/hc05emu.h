/**
 * @file
 * @brief An HC-05 seen from its UART: the AT command set of an HC-05 in
 *        the SPP (serial port profile) master role, answered from a state
 *        machine that a Bluetooth stack serves with radio results.  No pin
 *        driver and no radio code.
 *
 * KEY high is AT mode, KEY low data mode.  KEY high at the start (power-up
 * or AT+RESET) is full AT mode at 38400 baud; KEY raised later is AT mode
 * at the data baud.  In data mode the machine asks the radio to connect to
 * the bound address (CMODE 0) or to any slave that takes the PIN (CMODE 1)
 * and again after a loss; the caller passes the bytes through while
 * hc05emu_forwarding() holds.  The settings an HC-05 keeps in its flash
 * (role, CMODE, bound address, PIN, UART, inquiry mode) are hc05emu_cfg_t,
 * handed to the caller to store on every change (HC05EMU_ACT_SAVE).
 *
 * Pure C: no clock read, no I/O.  Times are unsigned differences of a
 * free-running 32-bit ms clock.  HC-05 is a product name used only to say
 * which module this answers like (README.md, "Trademarks").
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HC05EMU_H
#define HC05EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sizes. @{ */
#define HC05EMU_LINE_MAX    80u  /**< bytes of one command line held */
#define HC05EMU_OUT_MAX     64u  /**< bytes of one answer line, CR LF included */
#define HC05EMU_Q_MAX       12u  /**< actions waiting for hc05emu_step() */
#define HC05EMU_PIN_MAX     17u  /**< a PIN of up to 16 bytes + NUL */
#define HC05EMU_NAME_MAX    32u  /**< a remote name, cut to 31 bytes, + NUL */
#define HC05EMU_VERSION_MAX 24u  /**< the AT+VERSION? text + NUL */
#define HC05EMU_FOUND_MAX   16u  /**< devices one inquiry keeps */
#define HC05EMU_ADDR_LEN    6u   /**< bytes of a Bluetooth device address */
/** @} */

/** @brief Times, in ms. @{ */
#define HC05EMU_INQ_UNIT_MS 1280u  /**< one AT+INQM time unit */
#define HC05EMU_NAME_MS     2500u  /**< AT+RNAME? to FAIL without a name */
#define HC05EMU_RESTART_MS  300u   /**< AT+RESET's OK to the restart */
#define HC05EMU_CONNECT_MS  15000u /**< a data-mode connect to its end */
#define HC05EMU_RETRY_MS    1000u  /**< a failed or lost link to the next try */
/** @} */

/** @brief The inquiry of CMODE 1's search for a slave. @{ */
#define HC05EMU_ANY_UNITS   8u     /**< 8 units of 1.28s: 10.24s */
#define HC05EMU_ANY_MAX     9u     /**< devices it asks for */
/** @} */

/** @brief Bauds. @{ */
#define HC05EMU_BAUD_FULL_AT 38400u  /**< full AT mode, KEY high at the start */
#define HC05EMU_BAUD_DEFAULT 115200u /**< the data baud of a fresh setting */
/** @} */

/** @brief The settings an HC-05 keeps across a restart.  Flags are
 *         bytes, so a record read from storage is checked without reading
 *         a bool that holds another value (hc05emu_cfg_valid()). */
typedef struct {
    uint8_t  role;       /**< 0 slave, 1 master, 2 slave-loop */
    uint8_t  cmode;      /**< 0 the bound address, 1 any, 2 slave-loop */
    uint8_t  bound;      /**< 1: bind holds an address */
    uint8_t  bind[HC05EMU_ADDR_LEN]; /**< NAP (2 bytes), UAP, LAP (3 bytes) */
    char     pin[HC05EMU_PIN_MAX];   /**< the PIN, NUL-terminated */
    uint32_t baud;       /**< the data baud */
    uint8_t  stop;       /**< 0 one stop bit, 1 two */
    uint8_t  parity;     /**< 0 none, 1 odd, 2 even */
    uint8_t  inq_mode;   /**< 0 standard, 1 RSSI, 2 RSSI as well */
    uint8_t  inq_max;    /**< devices an inquiry asks for, 1 to 255 */
    uint8_t  inq_units;  /**< inquiry time in units of 1.28s, 1 to 48 */
} hc05emu_cfg_t;

/** @brief What the caller does next. */
typedef enum {
    HC05EMU_ACT_NONE = 0,   /**< nothing */
    HC05EMU_ACT_SEND,       /**< write data, len bytes, to the UART */
    HC05EMU_ACT_BAUD,       /**< set the UART to baud, stop, parity */
    HC05EMU_ACT_INQ,        /**< start an inquiry: units, max */
    HC05EMU_ACT_NAME,       /**< read the remote name of addr */
    HC05EMU_ACT_PAIR,       /**< pair with addr, pin, within timeout_ms */
    HC05EMU_ACT_CONNECT,    /**< open an SPP link to addr with pin */
    HC05EMU_ACT_STOP,       /**< end every radio procedure, close the link */
    HC05EMU_ACT_SAVE,       /**< store hc05emu_t.cfg */
} hc05emu_act_kind_t;

/** @brief One action for the caller. */
typedef struct {
    hc05emu_act_kind_t kind;         /**< what to do */
    uint8_t  data[HC05EMU_OUT_MAX];  /**< SEND: the bytes */
    size_t   len;                    /**< SEND: their count */
    uint32_t baud;                   /**< BAUD: the baud */
    uint8_t  stop;                   /**< BAUD: 0 one stop bit, 1 two */
    uint8_t  parity;                 /**< BAUD: 0 none, 1 odd, 2 even */
    uint8_t  addr[HC05EMU_ADDR_LEN]; /**< NAME, PAIR, CONNECT: the device */
    char     pin[HC05EMU_PIN_MAX];   /**< PAIR, CONNECT: the PIN */
    uint8_t  units;                  /**< INQ: units of 1.28s */
    uint8_t  max;                    /**< INQ: devices asked for */
    uint32_t timeout_ms;             /**< PAIR, CONNECT: the machine's limit */
} hc05emu_act_t;

/** @brief Where the module stands. */
typedef enum {
    HC05EMU_MODE_RESTART = 0, /**< AT+RESET's restart: deaf */
    HC05EMU_MODE_AT,          /**< KEY high: UART bytes are commands */
    HC05EMU_MODE_DATA,        /**< KEY low: UART bytes go over the link */
} hc05emu_mode_t;

/** @brief The AT command that waits for the radio. */
typedef enum {
    HC05EMU_OP_NONE = 0, HC05EMU_OP_INQ, HC05EMU_OP_NAME, HC05EMU_OP_PAIR,
} hc05emu_op_t;

/** @brief The data-mode link. */
typedef enum {
    HC05EMU_LINK_IDLE = 0, /**< no link and none wanted */
    HC05EMU_LINK_SEARCH,   /**< CMODE 1: the inquiry for a slave runs */
    HC05EMU_LINK_CONNECTING, /**< a CONNECT is out */
    HC05EMU_LINK_UP,       /**< the link is open */
    HC05EMU_LINK_WAIT,     /**< the next try waits HC05EMU_RETRY_MS */
} hc05emu_link_t;

/** @brief A device an inquiry reported. */
typedef struct {
    uint8_t  addr[HC05EMU_ADDR_LEN]; /**< its address */
    uint32_t cod;                    /**< its class of device, 24 bits */
    int16_t  rssi;                   /**< dBm; INT16_MAX unknown */
    bool     named;                  /**< name holds its remote name */
    char     name[HC05EMU_NAME_MAX]; /**< its name, printable ASCII */
} hc05emu_dev_t;

/** @brief One emulated module. */
typedef struct {
    hc05emu_cfg_t  cfg;           /**< the stored settings */
    char           version[HC05EMU_VERSION_MAX]; /**< AT+VERSION?'s text */
    hc05emu_mode_t mode;          /**< where it stands */
    bool           key;           /**< KEY as last given */
    bool           full_at;       /**< started with KEY high: 38400 baud */
    bool           inited;        /**< AT+INIT came, or data mode ran */
    uint32_t       baud_now;      /**< the UART's baud now */
    uint32_t       restart_ms;    /**< MODE_RESTART: when it began */
    hc05emu_op_t   op;            /**< the command waiting for the radio */
    uint32_t       op_ms;         /**< when it began */
    uint32_t       op_wait;       /**< its limit, ms */
    uint8_t        target[HC05EMU_ADDR_LEN]; /**< NAME, PAIR, CONNECT's device */
    uint8_t        inq_told;      /**< +INQ lines of this inquiry */
    hc05emu_dev_t  found[HC05EMU_FOUND_MAX]; /**< the last inquiry's devices */
    uint8_t        found_n;       /**< how many */
    hc05emu_link_t link;          /**< the data-mode link */
    uint32_t       link_ms;       /**< when the link state began */
    uint8_t        any_i;         /**< CMODE 1: the device tried */
    char           line[HC05EMU_LINE_MAX]; /**< the command being read */
    uint8_t        line_n;        /**< its length */
    bool           line_over;     /**< it was longer than the buffer */
    char           held[HC05EMU_LINE_MAX]; /**< a command that came while op ran */
    bool           held_over;     /**< held was longer than the buffer */
    bool           held_due;      /**< held waits for op's end */
    hc05emu_act_t  q[HC05EMU_Q_MAX]; /**< actions waiting */
    uint8_t        q_head;        /**< the oldest */
    uint8_t        q_n;           /**< how many */
    uint32_t       q_lost;        /**< actions dropped on a full queue */
} hc05emu_t;

/** @brief The settings of a fresh bridge: master, CMODE 0, no address,
 *         PIN 1234, 115200 baud 8N1 (8 data bits, no parity, one stop
 *         bit), INQM 1,1,48.  @param c Receives them; NULL does nothing. */
void hc05emu_cfg_default(hc05emu_cfg_t *c);
/** @brief Whether @p c is a record hc05emu_init() takes: every field in
 *         its range, the PIN 1 to 16 printable bytes without `"` or `,`
 *         and NUL-terminated.  @param c The record; NULL is not.
 *         @return true when valid. */
bool hc05emu_cfg_valid(const hc05emu_cfg_t *c);

/**
 * @brief Start the module, as at power-up.
 * @param e       The machine.
 * @param stored  The stored settings; NULL or invalid takes the defaults.
 * @param version AT+VERSION?'s text, cut to 23 bytes; NULL "btbridge".
 * @param key     KEY's level now: high starts full AT mode at 38400 baud,
 *                low data mode at the stored baud.
 * @param now_ms  The clock.
 */
void hc05emu_init(hc05emu_t *e, const hc05emu_cfg_t *stored,
                  const char *version, bool key, uint32_t now_ms);

/** @brief KEY's level.  A rise leaves data mode for AT mode at the data
 *         baud; a fall enters data mode and ends a command that waits for
 *         the radio without an answer.  @param e The machine. @param high
 *         The level. @param now_ms The clock. */
void hc05emu_key(hc05emu_t *e, bool high, uint32_t now_ms);

/** @brief Bytes the UART received in AT mode; ignored in the other modes.
 *         A line ends at LF, a CR is dropped.  @param e The machine.
 *         @param b The bytes. @param n Their count. @param now_ms The clock. */
void hc05emu_feed(hc05emu_t *e, const uint8_t *b, size_t n, uint32_t now_ms);

/**
 * @brief An inquiry result.
 * @param e      The machine.
 * @param addr   The device's address, 6 bytes.
 * @param cod    Its class of device.
 * @param rssi   Its RSSI (received signal strength indication) in dBm;
 *               INT16_MAX when the radio gave none.
 * @param name   Its name from the inquiry, NULL when it came without one.
 * @param now_ms The clock.
 */
void hc05emu_found(hc05emu_t *e, const uint8_t *addr, uint32_t cod,
                   int16_t rssi, const char *name, uint32_t now_ms);
/** @brief The radio's inquiry ended. @param e The machine.
 *         @param now_ms The clock. */
void hc05emu_inq_done(hc05emu_t *e, uint32_t now_ms);
/** @brief A remote name read ended. @param e The machine. @param addr The
 *         device. @param name Its name; NULL for a failed read.
 *         @param now_ms The clock. */
void hc05emu_name_done(hc05emu_t *e, const uint8_t *addr, const char *name,
                       uint32_t now_ms);
/** @brief A pairing ended. @param e The machine. @param addr The device.
 *         @param ok Whether it paired. @param now_ms The clock. */
void hc05emu_pair_done(hc05emu_t *e, const uint8_t *addr, bool ok,
                       uint32_t now_ms);
/** @brief The SPP link opened (@p up) or closed, or a CONNECT failed.
 *         @param e The machine. @param addr The device. @param up Open.
 *         @param now_ms The clock. */
void hc05emu_link_event(hc05emu_t *e, const uint8_t *addr, bool up,
                        uint32_t now_ms);

/**
 * @brief Advance to @p now_ms and take the next action.
 * @param e      The machine.
 * @param now_ms The clock.
 * @param out    Receives the action; kind NONE when there is none.
 * @return true when @p out holds an action other than NONE.
 */
bool hc05emu_step(hc05emu_t *e, uint32_t now_ms, hc05emu_act_t *out);

/** @brief Whether UART bytes are commands. @param e The machine.
 *         @return true in AT mode. */
bool hc05emu_at_mode(const hc05emu_t *e);
/** @brief Whether the caller passes bytes between the UART and the link.
 *         @param e The machine. @return true in data mode with the link up. */
bool hc05emu_forwarding(const hc05emu_t *e);
/** @brief The STATE pin: the link is up, in either mode. @param e The
 *         machine. @return true while connected. */
bool hc05emu_connected(const hc05emu_t *e);
/** @brief The status LED: off in the restart; 1000ms on, 1000ms off in AT
 *         mode; on with the link up in data mode; 100ms on, 100ms off
 *         otherwise.  @param e The machine. @param now_ms The clock.
 *         @return true for lit. */
bool hc05emu_led(const hc05emu_t *e, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
