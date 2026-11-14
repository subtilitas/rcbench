/**
 * @file
 * @brief BLE (Bluetooth Low Energy) central for a peer with a byte stream:
 *        the scan list, the bound address, connect, GATT (generic attribute
 *        profile) discovery of the peer's service, subscribe, the write path
 *        and reconnect, as events in and actions out.  No radio code and no
 *        pin driver: the caller runs the host stack.
 *
 * A profile (bleu_profile_t) names the service, its characteristics with
 * their roles and the connection parameters; the default is the
 * ISSC/Microchip Transparent UART service (bleu_profile_issc).  Each
 * machine has an owner, 0 to 7, in the top 3 bits of its tokens, so a
 * caller that runs several machines on one host stack routes an answer to
 * its machine by the token alone (bleuart_hub.h).
 *
 * The default profile: service 49535343-FE7D-4AE5-8FA9-9FAFD205E455.
 * Characteristics: 1E4D "Transfer TX" (NOTIFY, the sensor's UART bytes to
 * the central), 8841
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
#define BLEU_BACKOFF_MAX_MS 30000u /**< the longest gap after connects
                                         that went unanswered: each one
                                         doubles the next gap up to it */
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
#define BLEU_PARAM_MAX   20u    /**< bytes of the PARAM's value kept: 6DAA's
                                     8, a 20-byte value whole */
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
/** @brief The status a scan or a connect gets while another machine on the
 *         same host stack holds its one scan or connect (bleuart_hub.h): a
 *         connect answered so is tried again after BLEU_RETRY_MS, and the
 *         error of the last attempt stays. */
#define BLEU_STATUS_BUSY    0xFFFDu

/** @brief Tokens: the owner in the top 3 bits, a count in the rest.  Each
 *         owner's count, owner 0's too, wraps from 8,191 to 1. @{ */
#define BLEU_OWNER_SHIFT 13u     /**< the owner's first bit */
#define BLEU_OWNER_MAX   8u      /**< owners 0 to 7 */
#define BLEU_TOKEN_SEQ   0x1FFFu /**< the count's bits; a count is never 0 */
/** @} */

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

/** @brief A characteristic's role in a profile. */
typedef enum {
    BLEU_ROLE_NONE = 0, /**< named only: never written, read or subscribed */
    BLEU_ROLE_STREAM,   /**< NOTIFY, subscribed: its notifications are the
                             stream bytes; every profile has one */
    BLEU_ROLE_CMD,      /**< WRITE or WRITE NO RESPONSE: bleu_send() */
    BLEU_ROLE_PARAM,    /**< read once after each subscribe when it has
                             READ; a peer without it is taken */
} bleu_role_t;

/** @brief Characteristics a profile names at most. */
#define BLEU_CHR_MAX 4u

/** @brief bleu_chr_spec_t.flags: a BLEU_ROLE_CMD the peer may lack; the
 *         link then comes up and bleu_send() refuses. */
#define BLEU_CHR_OPTIONAL 0x01u

/** @brief One characteristic of a profile. */
typedef struct {
    const uint8_t *uuid;  /**< 128-bit UUID, least significant byte first */
    uint8_t        role;  /**< bleu_role_t */
    uint8_t        flags; /**< BLEU_CHR_OPTIONAL */
} bleu_chr_spec_t;

/** @brief The parameters a connect asks for.  Interval in 1.25ms units,
 *         supervision timeout in 10ms units, the scan while connecting in
 *         0.625ms units. */
typedef struct {
    uint16_t itvl_min;    /**< interval, least: 6 (7.5ms) or more */
    uint16_t itvl_max;    /**< interval, most: itvl_min to 3,200 (4s) */
    uint16_t latency;     /**< peripheral latency, events */
    uint16_t supervision; /**< 10 (100ms) to 3,200 (32s), longer than twice
                               (1 + latency) x itvl_max */
    uint16_t scan_itvl;   /**< 4 (2.5ms) to 0x4000 */
    uint16_t scan_window; /**< 4 to scan_itvl */
} bleu_conn_params_t;

/** @brief A peer's service as the central finds and uses it.  Two
 *         entries may name one UUID: that characteristic takes both
 *         roles, such as a STREAM that is also the CMD. */
typedef struct {
    const uint8_t     *svc;  /**< the service's 128-bit UUID, least
                                  significant byte first */
    bleu_chr_spec_t    chr[BLEU_CHR_MAX]; /**< its characteristics: one
                                  STREAM, at most one CMD, at most one
                                  PARAM, any NONE */
    uint8_t            chr_n; /**< how many of chr[] are used */
    bleu_conn_params_t conn; /**< the connect's parameters */
    const uint8_t     *adv_svc; /**< the 128-bit UUID a scan lists a device
                                     for when an AD (advertising data)
                                     128-bit UUID list holds it; NULL is
                                     svc */
    uint16_t           adv_uuid16; /**< a 16-bit UUID a scan lists a device
                                        for as well, 0 none: 0x18F0 for
                                        the ISSC profile */
} bleu_profile_t;

/** @brief The ISSC Transparent UART profile, every machine's default:
 *         1E4D STREAM, 8841 CMD, ACA3 NONE, 6DAA PARAM, the BLEU_ITVL_*
 *         set, and a scan that lists 0x18F0 or the ISSC service. */
extern const bleu_profile_t bleu_profile_issc;

/** @brief A device address. */
typedef struct {
    uint8_t val[6]; /**< least significant byte first, as HCI has it */
    uint8_t type;   /**< BLEU_ADDR_PUBLIC or BLEU_ADDR_RANDOM */
} bleu_addr_t;

/** @brief bleu_dev_t.flags. @{ */
#define BLEU_DEV_18F0      0x01u /**< a report listed the profile's 16-bit
                                      UUID, 0x18F0 for the ISSC profile */
#define BLEU_DEV_ISSC      0x02u /**< a report listed the profile's 128-bit
                                      service, the ISSC one by default */
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
    bool    has_18f0;    /**< 0x18F0, or the profile's 16-bit UUID, in a
                              16-bit UUID list */
    bool    has_issc;    /**< the ISSC service, or the profile's 128-bit
                              UUID, in a 128-bit UUID list */
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
    BLEU_ACT_CONNECT,    /**< connect to addr with cp, ms */
    BLEU_ACT_CANCEL,     /**< cancel the pending connect */
    BLEU_ACT_DISCONNECT, /**< terminate conn */
    BLEU_ACT_DISC_SVC,   /**< find the service whose 128-bit UUID is
                              data[0..15], len 16 */
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
    bleu_conn_params_t cp; /**< CONNECT: the profile's parameters */
    uint8_t     data[BLEU_WRITE_MAX]; /**< SUBSCRIBE, WRITE, WRITE_NR; the
                                           service's UUID for DISC_SVC */
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
    BLEU_ST_NOT_PEER,  /**< the bound device lacks the profile's service or
                            a characteristic it needs: no retry until a new
                            bind or OFF and on */
} bleu_state_t;

/** @brief BLEU_ST_NOT_PEER under the name the IMU's code uses. */
#define BLEU_ST_NOT_WIT BLEU_ST_NOT_PEER

/** @brief Why the last attempt or link ended. */
typedef enum {
    BLEU_ERR_NONE = 0,  /**< none */
    BLEU_ERR_CONNECT,   /**< a connect failed or timed out */
    BLEU_ERR_NOT_PEER,  /**< no service, no STREAM with NOTIFY, no CMD
                             with WRITE (unless optional), or no CCCD */
    BLEU_ERR_GATT,      /**< a GATT step failed or timed out */
    BLEU_ERR_LOST,      /**< the link dropped; reason in bleu_t.reason */
} bleu_err_t;

/** @brief BLEU_ERR_NOT_PEER under the name the IMU's code uses. */
#define BLEU_ERR_NOT_WIT BLEU_ERR_NOT_PEER

/** @brief Where the last command write stands. */
typedef enum {
    BLEU_WR_IDLE = 0,   /**< none since the link came up */
    BLEU_WR_BUSY,       /**< in flight */
    BLEU_WR_OK,         /**< taken */
    BLEU_WR_FAILED,     /**< refused, timed out or lost: wr_status */
} bleu_wr_t;

/** @brief One central's machine.  tx_* is the profile's STREAM, rx_* its
 *         CMD, param_* its PARAM. */
typedef struct {
    const bleu_profile_t *prof; /**< private: the profile; NULL is
                                     bleu_profile_issc */
    uint8_t      owner;     /**< private: the tokens' top 3 bits */
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
    uint16_t     tx_val;    /**< the STREAM's (1E4D's) value handle */
    uint16_t     tx_end;    /**< its last handle */
    uint16_t     cccd;      /**< its CCCD */
    uint16_t     rx_val;    /**< the CMD's (8841's) value handle, 0 none */
    uint8_t      rx_props;  /**< its properties */
    uint8_t      tx_props;  /**< the STREAM's properties */
    uint16_t     param_val; /**< the PARAM's (6DAA's) value handle, 0
                                 without READ */
    uint16_t     chr_val[BLEU_CHR_MAX]; /**< each profile entry's value
                                 handle, 0 not found: a NONE entry's too */
    uint8_t      chr_props[BLEU_CHR_MAX]; /**< each entry's properties */
    uint8_t      param[BLEU_PARAM_MAX]; /**< its value as read */
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
    uint32_t     backoff_ms;/**< private: the gap after the next connect
                                 that goes unanswered, 0 for BLEU_RETRY_MS */
    bleu_act_t   q[BLEU_ACT_QUEUE]; /**< private: actions due */
    uint8_t      q_head;    /**< private */
    uint8_t      q_n;       /**< private */
} bleu_t;

/** @brief Slots of an event queue kept for the events a link's state
 *         hangs on: a notification or an advertising report takes a slot
 *         only while more than this many are free. */
#define BLEU_EVQ_RESERVE 8u

/**
 * @brief Whether an event of @p kind may take a slot of a queue with
 *        @p free slots: a notification or an advertising report only while
 *        more than BLEU_EVQ_RESERVE are free, every other kind while one
 *        is, so a full stream never crowds out a DISCONNECT or an answer.
 * @param kind bleu_ev_kind_t.
 * @param free The queue's free slots.
 * @return true when it may
 */
bool bleu_evt_admit(uint8_t kind, uint32_t free);

/** @brief Clear to OFF, nothing bound, the ISSC profile, owner 0.
 *  @param b The machine; NULL does nothing. */
void bleu_init(bleu_t *b);

/**
 * @brief Whether @p p is a profile a machine takes: a service UUID, 1 to
 *        BLEU_CHR_MAX characteristics each with a UUID, exactly one STREAM,
 *        at most one CMD and one PARAM, and connection parameters inside
 *        the ranges bleu_conn_params_t names.
 * @param p The profile.
 * @return false for NULL or any other.
 */
bool bleu_profile_valid(const bleu_profile_t *p);

/**
 * @brief Give a machine its profile and its owner, after bleu_init() and
 *        before bleu_enable().  The profile is kept by pointer: it outlives
 *        the machine.
 * @param b     The machine.
 * @param p     The profile, valid (bleu_profile_valid()).
 * @param owner The tokens' top 3 bits, below BLEU_OWNER_MAX.
 * @return false, and nothing changes, for NULL, an invalid profile or an
 *         owner out of range.
 */
bool bleu_setup(bleu_t *b, const bleu_profile_t *p, uint8_t owner);

/**
 * @brief The owner a token carries.
 * @param token The token.
 * @return Its top 3 bits, 0 to 7.
 */
uint8_t bleu_token_owner(uint16_t token);

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
 * @brief End the link as a failed GATT step (BLEU_ERR_GATT): the client
 *        found the peer's answer unusable, a PARAM it needs that would not
 *        read for one.  Disconnect, and connect again after BLEU_RETRY_MS.
 * @param b      The machine; NULL, or no link under way, does nothing.
 * @param status The status kept in err_status.
 * @param now_ms The clock.
 */
void bleu_fail_link(bleu_t *b, uint16_t status, uint32_t now_ms);

/**
 * @brief Send @p len bytes to the peer on the CMD characteristic (8841):
 *        with response when it has WRITE, without when it has WRITE NO
 *        RESPONSE only.
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
 * @return true when @p e is a notification of the STREAM (1E4D) on the
 *         link: its data[0..len-1] are the peer's stream bytes, for the
 *         caller's parser.  false for every other event and for NULL.
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

/**
 * @brief Parse one advertising report as bleu_adv_parse() does, matched
 *        against @p p's advertising: has_issc for its 128-bit UUID
 *        (adv_svc, else svc), has_18f0 for its adv_uuid16.
 * @param p   The profile; NULL is bleu_profile_issc.
 * @param ad  The bytes; NULL with @p len 0 is an empty report.
 * @param len Their count.
 * @param out Receives what the report says.
 * @return true when the report lists either.
 */
bool bleu_adv_parse_for(const bleu_profile_t *p, const uint8_t *ad,
                        size_t len, bleu_adv_t *out);

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
 * @brief bleu_list_report() with the match of @p p's advertising
 *        (bleu_adv_parse_for()): a machine's scan lists what its own
 *        profile advertises.
 * @param l    The list.
 * @param p    The profile; NULL is bleu_profile_issc.
 * @param addr The advertiser.
 * @param rssi Its RSSI, dBm, 127 unknown.
 * @param ad   The AD bytes.
 * @param len  Their count.
 */
void bleu_list_report_for(bleu_list_t *l, const bleu_profile_t *p,
                          const bleu_addr_t *addr, int8_t rssi,
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
