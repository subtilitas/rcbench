/**
 * @file
 * @brief BLE (Bluetooth Low Energy) central for a transparent-UART sensor:
 *        the scan list, the bound address, connect, GATT (generic attribute
 *        profile) discovery of the ISSC/Microchip Transparent UART service,
 *        subscribe, the write path and reconnect, as events in and actions
 *        out.  No radio code and no pin driver: the caller runs the host
 *        stack.
 *
 * Service 49535343-FE7D-4AE5-8FA9-9FAFD205E455.  Characteristics: 1E4D
 * "Transfer TX" (NOTIFY, the sensor's UART bytes to the central), 8841
 * "Transfer RX" (WRITE or WRITE NO RESPONSE, bytes to the sensor's UART),
 * ACA3 "Air Patch" (module commands: never written, never subscribed) and
 * 6DAA "Connection Parameter" (read once after each subscribe, never
 * written).  A connect runs: connect, find the service, its
 * characteristics, 1E4D's CCCD (client characteristic configuration
 * descriptor), MTU (maximum transmission unit) exchange, write 0x0001 to
 * the CCCD, then the link is up and 6DAA is read.
 *
 * Pure C: no clock read, no I/O, no heap.  Times are unsigned differences
 * of a free-running 32-bit ms clock.  Microchip and ISSC are names of
 * their owners, used only to say which service this talks to (README.md,
 * "Trademarks").
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BLEUART_H
#define BLEUART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The timeouts and waits, in ms. @{ */
#define BLEU_SCAN_MS     10000u /**< one active scan */
#define BLEU_CONNECT_MS  5000u  /**< one connect attempt */
#define BLEU_RETRY_MS    1000u  /**< a timed-out try or a drop to the next */
#define BLEU_RETRY_CONNECT_MS 7000u /**< a connect the host stack ends with a
                                         failure, among them HCI 0x3E (failed
                                         to be established), to the next */
#define BLEU_GATT_MS     5000u  /**< one discovery, MTU, CCCD or read step */
#define BLEU_WRITE_MS    1000u  /**< one command write's answer */
#define BLEU_SETTLE_MS   2000u  /**< a cancel or a disconnect to its event */
/** @} */

/** @brief The connection parameters a connect asks for, in one place.
 *         Chosen as a phone's central asks them, not measured against
 *         other values.  Interval in 1.25ms units, supervision timeout in
 *         10ms units, the scan while connecting in 0.625ms units. @{ */
#define BLEU_ITVL_MIN    24u    /**< 30ms */
#define BLEU_ITVL_MAX    40u    /**< 50ms */
#define BLEU_LATENCY     0u     /**< peripheral latency, events */
#define BLEU_SUPERVISION 400u   /**< 4,000ms */
#define BLEU_CONN_SCAN_ITVL   0x60u /**< 60ms */
#define BLEU_CONN_SCAN_WINDOW 0x30u /**< 30ms */
#define BLEU_MTU         247u   /**< the ATT (attribute protocol) MTU asked */
/** @} */

/** @brief Sizes. @{ */
#define BLEU_FOUND_MAX   8u     /**< devices a scan lists */
#define BLEU_SEEN_MAX    32u    /**< other addresses a scan remembers */
#define BLEU_NAME_MAX    24u    /**< a name, cut to 23 bytes, + NUL */
#define BLEU_ADDR_TEXT_MAX 20u  /**< "00:0C:BF:08:19:29 P" + NUL */
#define BLEU_EVT_DATA    40u    /**< data bytes one event carries */
#define BLEU_WRITE_MAX   20u    /**< bytes of one command write */
#define BLEU_ACT_QUEUE   6u     /**< actions waiting for bleu_step() */
#define BLEU_PARAM_MAX   8u     /**< bytes of 6DAA's value kept */
/** @} */

/** @brief The 16-bit service UUID (universally unique identifier) the
 *         sensor advertises; no Bluetooth SIG assignment. */
#define BLEU_UUID16_ADV  0x18F0u
/** @brief The CCCD's 16-bit UUID. */
#define BLEU_UUID16_CCCD 0x2902u
/** @brief The status a timeout gives a write or a scan. */
#define BLEU_STATUS_TIMEOUT 0xFFFFu
/** @brief The status a dropped link gives a write in flight. */
#define BLEU_STATUS_LOST    0xFFFEu

/** @brief Characteristic properties, as the GATT declaration carries
 *         them. @{ */
#define BLEU_PROP_READ     0x02u /**< read */
#define BLEU_PROP_WRITE_NR 0x04u /**< write without response */
#define BLEU_PROP_WRITE    0x08u /**< write with response */
#define BLEU_PROP_NOTIFY   0x10u /**< notify */
/** @} */

/** @brief Address types, as HCI (host controller interface) reports
 *         them. @{ */
#define BLEU_ADDR_PUBLIC 0u /**< public device address */
#define BLEU_ADDR_RANDOM 1u /**< random device address */
/** @} */

/** @brief The 128-bit UUIDs, least significant byte first as on the air:
 *         the service, 1E4D, 8841, ACA3 and 6DAA. @{ */
extern const uint8_t bleu_uuid_svc[16];
extern const uint8_t bleu_uuid_tx[16];
extern const uint8_t bleu_uuid_rx[16];
extern const uint8_t bleu_uuid_air[16];
extern const uint8_t bleu_uuid_param[16];
/** @} */

/** @brief A device address. */
typedef struct {
    uint8_t val[6]; /**< least significant byte first, as HCI has it */
    uint8_t type;   /**< BLEU_ADDR_PUBLIC or BLEU_ADDR_RANDOM */
} bleu_addr_t;

/** @brief bleu_dev_t.flags. @{ */
#define BLEU_DEV_18F0      0x01u /**< a report listed 0x18F0 */
#define BLEU_DEV_ISSC      0x02u /**< a report listed the ISSC service */
#define BLEU_DEV_NAME_FULL 0x04u /**< name is a complete local name */
/** @} */

/** @brief A device a scan lists. */
typedef struct {
    bleu_addr_t addr;            /**< its address */
    int8_t      rssi;            /**< dBm of its last report; 127 unknown */
    uint8_t     flags;           /**< BLEU_DEV_* */
    char        name[BLEU_NAME_MAX]; /**< "" when no report named it */
} bleu_dev_t;

/** @brief An address a scan saw and does not list. */
typedef struct {
    bleu_addr_t addr;    /**< the address */
    bool        matched; /**< it matched and the list was full */
} bleu_seen_t;

/** @brief A scan's list. */
typedef struct {
    bleu_dev_t  found[BLEU_FOUND_MAX]; /**< strongest first */
    uint8_t     found_n;               /**< how many */
    bleu_seen_t seen[BLEU_SEEN_MAX];   /**< private */
    uint8_t     seen_n;                /**< private */
    uint16_t    hidden;                /**< other devices, not listed */
    uint16_t    reports;               /**< reports taken */
} bleu_list_t;

/** @brief What one advertising report says. */
typedef struct {
    char    name[BLEU_NAME_MAX]; /**< AD 0x09, else 0x08, else "";
                                      up to its first NUL */
    uint8_t name_kind;   /**< 0 none, 0x08 shortened, 0x09 complete */
    uint8_t flags;       /**< AD 0x01's byte, 0 without */
    bool    has_18f0;    /**< 0x18F0 in a 16-bit UUID list */
    bool    has_issc;    /**< the service in a 128-bit UUID list */
    bool    cut;         /**< a length ran past the end: the rest dropped */
} bleu_adv_t;

/** @brief Events the caller hands in, one per host stack callback. */
typedef enum {
    BLEU_EV_NONE = 0,
    BLEU_EV_ADV,         /**< a report: addr, rssi, data = AD bytes */
    BLEU_EV_SCAN_END,    /**< the scan stopped or did not start: status */
    BLEU_EV_CONNECT,     /**< a connect ended: status, conn; h1 h2 h3 =
                              interval, latency, supervision timeout */
    BLEU_EV_DISCONNECT,  /**< conn ended: status = reason */
    BLEU_EV_CONN_UPDATE, /**< conn's parameters: h1 h2 h3 as CONNECT */
    BLEU_EV_SVC,         /**< the service: h1 start, h2 end */
    BLEU_EV_SVC_DONE,    /**< service discovery ended: status 0 or error */
    BLEU_EV_CHR,         /**< a characteristic: h1 declaration, h2 value
                              handle, props, data = UUID (2 or 16 bytes) */
    BLEU_EV_CHR_DONE,    /**< characteristic discovery ended */
    BLEU_EV_DSC,         /**< a descriptor: h1 handle, data = UUID */
    BLEU_EV_DSC_DONE,    /**< descriptor discovery ended */
    BLEU_EV_MTU,         /**< the MTU exchange ended: status, h1 = MTU */
    BLEU_EV_WRITE_DONE,  /**< a SUBSCRIBE, WRITE or WRITE_NR ended: status */
    BLEU_EV_READ,        /**< a READ ended: status, data = the value */
    BLEU_EV_NOTIFY,      /**< a notification: conn, h1 value handle, data;
                              a longer one comes as several, in order */
} bleu_ev_kind_t;

/** @brief One event: 64 bytes, the element of the caller's queue.  status
 *         0 is success; a nonzero code is the host stack's own. */
typedef struct {
    uint8_t     kind;    /**< bleu_ev_kind_t */
    uint8_t     len;     /**< bytes in data, at most BLEU_EVT_DATA */
    uint16_t    conn;    /**< connection handle */
    uint16_t    status;  /**< 0 or the error */
    uint16_t    token;   /**< the token of the action it answers;
                              DISCONNECT: the connect's when the
                              connection failed to be established */
    uint16_t    h1;      /**< a handle or a value, by kind */
    uint16_t    h2;      /**< a handle or a value, by kind */
    uint16_t    h3;      /**< a value, by kind */
    uint8_t     props;   /**< CHR: BLEU_PROP_* */
    int8_t      rssi;    /**< ADV: dBm, 127 unknown */
    bleu_addr_t addr;    /**< ADV: the advertiser */
    uint8_t     data[BLEU_EVT_DATA]; /**< by kind */
} bleu_evt_t;

/** @brief What the caller does next.  An action with a token is answered
 *         by an event with that token. */
typedef enum {
    BLEU_ACT_NONE = 0,
    BLEU_ACT_SCAN,       /**< active scan, duplicates filtered, ms long */
    BLEU_ACT_SCAN_STOP,  /**< stop the scan */
    BLEU_ACT_CONNECT,    /**< connect to addr with the BLEU_ITVL_* set, ms */
    BLEU_ACT_CANCEL,     /**< cancel the pending connect */
    BLEU_ACT_DISCONNECT, /**< terminate conn */
    BLEU_ACT_DISC_SVC,   /**< find the service by bleu_uuid_svc */
    BLEU_ACT_DISC_CHRS,  /**< every characteristic in h1..h2 */
    BLEU_ACT_DISC_DSCS,  /**< every descriptor after value handle h1 to h2 */
    BLEU_ACT_MTU,        /**< exchange the MTU, BLEU_MTU asked */
    BLEU_ACT_SUBSCRIBE,  /**< write data (0x01 0x00) to the CCCD h1 */
    BLEU_ACT_READ,       /**< read h1 */
    BLEU_ACT_WRITE,      /**< write data to h1, with response */
    BLEU_ACT_WRITE_NR,   /**< write data to h1 without response; answer
                              with WRITE_DONE and the call's own status */
} bleu_act_kind_t;

/** @brief One action for the caller. */
typedef struct {
    uint8_t     kind;    /**< bleu_act_kind_t */
    uint8_t     len;     /**< bytes in data */
    uint16_t    token;   /**< for the answering event; 0: none comes */
    uint16_t    conn;    /**< connection handle */
    uint16_t    h1;      /**< a handle, by kind */
    uint16_t    h2;      /**< a handle, by kind */
    uint16_t    ms;      /**< SCAN, CONNECT: the duration */
    bleu_addr_t addr;    /**< CONNECT: the address */
    uint8_t     data[BLEU_WRITE_MAX]; /**< SUBSCRIBE, WRITE, WRITE_NR */
} bleu_act_t;

/** @brief Where the link stands. */
typedef enum {
    BLEU_ST_OFF = 0,   /**< not enabled */
    BLEU_ST_IDLE,      /**< enabled, no address bound */
    BLEU_ST_WAIT,      /**< bound: the connect is due after the retry gap */
    BLEU_ST_CONNECTING,/**< a connect runs */
    BLEU_ST_DISC_SVC,  /**< connected: finding the service */
    BLEU_ST_DISC_CHR,  /**< its characteristics */
    BLEU_ST_DISC_DSC,  /**< 1E4D's descriptors */
    BLEU_ST_MTU,       /**< the MTU exchange */
    BLEU_ST_SUBSCRIBE, /**< the CCCD write */
    BLEU_ST_UP,        /**< subscribed: the stream flows */
    BLEU_ST_NOT_WIT,   /**< the bound device lacks the service: no retry
                            until a new bind or OFF and on */
} bleu_state_t;

/** @brief Why the last attempt or link ended. */
typedef enum {
    BLEU_ERR_NONE = 0,  /**< none */
    BLEU_ERR_CONNECT,   /**< a connect failed or timed out */
    BLEU_ERR_NOT_WIT,   /**< no service, no 1E4D NOTIFY, no 8841 WRITE, or
                             no CCCD */
    BLEU_ERR_GATT,      /**< a GATT step failed or timed out */
    BLEU_ERR_LOST,      /**< the link dropped; reason in bleu_t.reason */
} bleu_err_t;

/** @brief Where the last command write stands. */
typedef enum {
    BLEU_WR_IDLE = 0,   /**< none since the link came up */
    BLEU_WR_BUSY,       /**< in flight */
    BLEU_WR_OK,         /**< taken */
    BLEU_WR_FAILED,     /**< refused, timed out or lost: wr_status */
} bleu_wr_t;

/** @brief One central's machine. */
typedef struct {
    bleu_state_t st;        /**< where the link stands */
    bleu_err_t   err;       /**< why the last attempt or link ended */
    uint16_t     err_status;/**< the status that ended it, 0 for a timeout */
    uint16_t     reason;    /**< the last disconnect's reason */
    bool         bound;     /**< an address is bound */
    bleu_addr_t  addr;      /**< the bound address */
    bool         scanning;  /**< a scan runs */
    uint16_t     scan_status; /**< the last scan's end: 0, or its error */
    bleu_list_t  list;      /**< the last scan's devices */
    uint16_t     conn;      /**< the connection handle while connected */
    uint16_t     mtu;       /**< the MTU answered, 0 before or on error */
    uint16_t     itvl;      /**< connection interval, 1.25ms units */
    uint16_t     latency;   /**< peripheral latency */
    uint16_t     supervision; /**< supervision timeout, 10ms units */
    uint16_t     svc_start; /**< the service's handles */
    uint16_t     svc_end;   /**< its last handle */
    uint16_t     tx_val;    /**< 1E4D's value handle */
    uint16_t     tx_end;    /**< 1E4D's last handle */
    uint16_t     cccd;      /**< 1E4D's CCCD */
    uint16_t     rx_val;    /**< 8841's value handle */
    uint8_t      rx_props;  /**< 8841's properties */
    uint8_t      tx_props;  /**< 1E4D's properties */
    uint16_t     param_val; /**< 6DAA's value handle, 0 without READ */
    uint8_t      param[BLEU_PARAM_MAX]; /**< 6DAA's value as read */
    uint8_t      param_len; /**< its length; 0 before the read */
    uint16_t     param_status; /**< the read's status */
    bool         param_due; /**< private: the read waits for the slot */
    bleu_wr_t    wr;        /**< the last command write */
    uint16_t     wr_status; /**< its status */
    uint32_t     t_connect; /**< ms from CONNECT to the connect event */
    uint32_t     t_setup;   /**< ms from the connect event to UP */
    uint32_t     notifies;  /**< notifications passed on as stream bytes */
    uint32_t     stray;     /**< events ignored: late, stale or another
                                 handle */
    uint32_t     attempts;  /**< connects started */
    uint32_t     act_drops; /**< actions the full queue dropped */
    uint16_t     token;     /**< private: the last token handed out */
    uint16_t     tok_link;  /**< private: the link step's token */
    uint16_t     tok_scan;  /**< private: the scan's token */
    uint16_t     tok_wr;    /**< private: the slot's token */
    uint16_t     tok_cancel;/**< private: the cancelled connect's token */
    uint16_t     close_conn;/**< private: the connection being closed */
    uint8_t      settle;    /**< private: 0, 1 cancel, 2 close */
    bool         rd_busy;   /**< private: the 6DAA read holds the slot */
    uint32_t     t_link;    /**< private: when the link step began */
    uint32_t     t_scan;    /**< private: when the scan began */
    uint32_t     t_wr;      /**< private: when the slot was taken */
    uint32_t     t_settle;  /**< private: when the settle began */
    uint32_t     t_gap;     /**< private: when the retry gap began */
    uint32_t     t_conn_at; /**< private: when the connect event came */
    uint32_t     gap_ms;    /**< private: its length */
    bleu_act_t   q[BLEU_ACT_QUEUE]; /**< private: actions due */
    uint8_t      q_head;    /**< private */
    uint8_t      q_n;       /**< private */
} bleu_t;

/** @brief Clear to OFF, nothing bound. @param b The machine; NULL does
 *         nothing. */
void bleu_init(bleu_t *b);

/**
 * @brief Switch the central on or off.  On: a bound address is connected
 *        at once.  Off: a scan stops, a connect is cancelled, a link is
 *        disconnected; the bound address and the scan list stay.
 * @param b      The machine; NULL does nothing.
 * @param on     true to switch on.
 * @param now_ms The clock.
 */
void bleu_enable(bleu_t *b, bool on, uint32_t now_ms);

/**
 * @brief Bind @p addr, or unbind with NULL.  A link to another address is
 *        ended and, when on, the new one connected at once; the same
 *        address while connecting or connected changes nothing.
 * @param b      The machine; NULL does nothing.
 * @param addr   The address, or NULL.
 * @param now_ms The clock.
 */
void bleu_bind(bleu_t *b, const bleu_addr_t *addr, uint32_t now_ms);

/**
 * @brief Start a scan of BLEU_SCAN_MS.  The list is cleared.  A pending
 *        connect is cancelled and resumes after the scan; a link stays.
 * @param b      The machine.
 * @param now_ms The clock.
 * @return false when NULL, OFF or a scan runs.
 */
bool bleu_scan(bleu_t *b, uint32_t now_ms);

/** @brief Stop a running scan; the list stays. @param b The machine; NULL
 *         does nothing. @param now_ms The clock. */
void bleu_scan_stop(bleu_t *b, uint32_t now_ms);

/**
 * @brief Send @p len bytes to the sensor on 8841: with response when 8841
 *        has WRITE, without when it has WRITE NO RESPONSE only.
 * @param b      The machine.
 * @param data   The bytes.
 * @param len    Their count, 1 to BLEU_WRITE_MAX.
 * @param now_ms The clock.
 * @return false, and nothing changes, unless bleu_can_send() and the
 *         length fits.
 */
bool bleu_send(bleu_t *b, const uint8_t *data, size_t len, uint32_t now_ms);

/**
 * @brief Take one event from the host stack.
 * @param b      The machine.
 * @param e      The event.
 * @param now_ms The clock.
 * @return true when @p e is a notification of 1E4D on the link: its
 *         data[0..len-1] are the sensor's UART bytes, for the caller's
 *         parser.  false for every other event and for NULL.
 */
bool bleu_event(bleu_t *b, const bleu_evt_t *e, uint32_t now_ms);

/**
 * @brief Advance to @p now_ms and take the next action.
 * @param b      The machine.
 * @param now_ms The clock.
 * @param out    Receives the action; kind NONE when there is none.
 * @return true when @p out holds an action other than NONE.
 */
bool bleu_step(bleu_t *b, uint32_t now_ms, bleu_act_t *out);

/** @brief Whether the stream flows. @param b The machine. @return true in
 *         BLEU_ST_UP; false for NULL. */
bool bleu_link_up(const bleu_t *b);
/** @brief Whether bleu_send() takes a command now. @param b The machine.
 *  @return true when up, no write in flight and the 6DAA read done. */
bool bleu_can_send(const bleu_t *b);

/**
 * @brief Parse one advertising report's AD (advertising data) structures.
 *        A length 0 ends the data; a length past the end drops the rest.
 * @param ad  The bytes; NULL with @p len 0 is an empty report.
 * @param len Their count.
 * @param out Receives what the report says.
 * @return true when the report lists 0x18F0 or the ISSC service.
 */
bool bleu_adv_parse(const uint8_t *ad, size_t len, bleu_adv_t *out);

/** @brief Clear a scan list. @param l The list; NULL does nothing. */
void bleu_list_clear(bleu_list_t *l);

/**
 * @brief Take one report into a scan list: a matching device is listed or
 *        updated, strongest first, the weakest of a full list given up for
 *        a stronger one; another device is counted in hidden once.
 * @param l    The list.
 * @param addr The advertiser.
 * @param rssi Its RSSI (received signal strength indication), dBm, 127
 *             unknown.
 * @param ad   The AD bytes.
 * @param len  Their count.
 */
void bleu_list_report(bleu_list_t *l, const bleu_addr_t *addr, int8_t rssi,
                      const uint8_t *ad, size_t len);

/**
 * @brief Write @p a as `00:0C:BF:08:19:29`, with ` P` or ` R` after it
 *        when @p with_type.
 * @param a         The address.
 * @param with_type Append the type letter.
 * @param out       Receives the text and its NUL.
 * @param cap       The size of @p out.
 * @return The text's length, 0 when it does not fit, for NULL, or for a
 *         type other than public and random with @p with_type.
 */
size_t bleu_addr_format(const bleu_addr_t *a, bool with_type, char *out,
                        size_t cap);

/**
 * @brief Read `00:0C:BF:08:19:29 P` (or ` R`), hex in either case.
 * @param s   The text.
 * @param out Receives the address; untouched on false.
 * @return false for NULL or any other form.
 */
bool bleu_addr_parse(const char *s, bleu_addr_t *out);

/** @brief Whether two addresses and their types are equal. @param a One.
 *  @param b The other. @return false when either is NULL. */
bool bleu_addr_eq(const bleu_addr_t *a, const bleu_addr_t *b);

#ifdef __cplusplus
}
#endif
#endif
