/**
 * @file
 * @brief Wi-Fi access: when the panel's radio runs, under which name, and
 *        who the page answers.
 *
 * The panel offers a browser page for two jobs: a panel firmware upload and
 * the card's files for download.  It reaches the browser one of two ways:
 *
 *   ACCESS POINT  its own network, 2.4GHz, WPA2 (Wi-Fi Protected Access 2),
 *                 named "rcbench-" and 4 hex digits of the MAC (media access
 *                 control) address, with a password of WA_SECRET_LEN
 *                 characters made fresh at every start; one client at a time
 *   HOME NETWORK  the network whose name and password the operator typed on
 *                 the panel and the panel keeps
 *
 * The radio runs only while the Wi-Fi screen asks for it, or while the
 * panel asks GitHub for a release (WA_MODE_FETCH, wa_fetch.h).  It is off
 * at every start of the panel, and it goes off on its own: on ARM, on STOP,
 * on the supply coming on, on a log run opening, after WA_IDLE_MS without
 * a request, and after WA_GUESS_MAX wrong access codes.  A joined session
 * switched off by the operator or by WA_IDLE_MS may leave the radio up for
 * the platform's last request on the network (wa_lingers()).
 *
 * The access code: WA_SECRET_LEN characters shown on the panel, made fresh
 * at every start, which the page asks for before it lists a file or takes an
 * upload.  A switch on the screen turns it off; it is on by default.
 *
 * What is kept: the home network's name and password and the code switch
 * (wa_conf_t), in a store the caller passes (NVS, non-volatile storage, on
 * the panel).  Nothing else about Wi-Fi is kept.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework):
 * the platform reports what its radio did and asks what it should do.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_SESSION_H
#define RCBENCH_WA_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Limits and times
 * @{
 */
/** Characters of the access point's password and of the access code. */
#define WA_SECRET_LEN   8u
/** What a secret is made of: no 0, 1, i, l or o, which read alike. */
#define WA_SECRET_CHARS "abcdefghjkmnpqrstuvwxyz23456789"
/** "rcbench-1A2B" and its terminator. */
#define WA_NAME_MAX     13u

/** @brief A home network's name, in bytes. */
#define WA_SSID_MAX     32u
/** @brief A WPA2/WPA3 passphrase's fewest characters. */
#define WA_PASS_MIN     8u
/** @brief A WPA2/WPA3 passphrase's most characters. */
#define WA_PASS_MAX     63u

/** The radio goes off after this long without a request. */
#define WA_IDLE_MS      (10u * 60u * 1000u)
/** A join that has not reached the network after this long failed. */
#define WA_JOIN_MS      20000u
/**
 * @brief Auth-class drops in a row that end a join as WA_JOIN_PASSWORD.
 *
 * The radio reports a refused key and a key handshake that ran out of time
 * with the same reasons, and a correct key times out when the panel or the
 * router answers late; one drop says nothing about the password.
 */
#define WA_AUTH_TRIES   3u
/** A join that reached the network and has no address after this long. */
#define WA_ADDRESS_MS   15000u
/** A start of the access point, or a scan, that has not come up. */
#define WA_START_MS     10000u
/**
 * @brief Scans a scan session starts at most: the first, and one more after
 *        each scan the radio reports failed, all within WA_START_MS.
 */
#define WA_SCAN_TRIES   3u
/** Wrong access codes before the radio goes off. */
#define WA_GUESS_MAX    16u
/** @} */

/* ------------------------------------------------------------- names */

/**
 * @brief A source of random 32-bit words.
 * @param ctx The caller's context.
 * @return The next word.
 */
typedef uint32_t (*wa_rand_fn)(void *ctx);

/**
 * @brief WA_SECRET_LEN characters of WA_SECRET_CHARS and a terminator, each
 *        drawn evenly from the alphabet (words that would favour some
 *        characters are drawn again).
 * @param out  Receives the secret.
 * @param rand The source of random words.
 * @param ctx  Passed to @p rand.
 */
void wa_secret_make(char out[WA_SECRET_LEN + 1u], wa_rand_fn rand, void *ctx);

/**
 * @brief "rcbench-" and the last two bytes of @p mac in capital hex.
 * @param mac The radio's MAC address.
 * @param out Receives the name, terminated.
 */
void wa_name(const uint8_t mac[6], char out[WA_NAME_MAX]);

/* ------------------------------------------------------- what is kept */

/** @brief What is kept: the home network and the code switch. */
typedef struct {
    bool have_net;                    /**< a home network is kept       */
    char ssid[WA_SSID_MAX + 1u];      /**< its name, terminated         */
    char pass[WA_PASS_MAX + 1u];      /**< its passphrase, terminated   */
    bool code_on;                     /**< the page asks for the code   */
} wa_conf_t;

/** @brief Why wa_conf_set_net() refused a home network. */
typedef enum {
    WA_CONF_OK = 0,        /**< taken                                    */
    WA_CONF_SSID_EMPTY,    /**< no name                                  */
    WA_CONF_SSID_LONG,     /**< over WA_SSID_MAX bytes                  */
    WA_CONF_SSID_CHAR,     /**< a NUL byte in it                         */
    WA_CONF_PASS_SHORT,    /**< under WA_PASS_MIN characters             */
    WA_CONF_PASS_LONG,     /**< over WA_PASS_MAX characters              */
    WA_CONF_PASS_CHAR,     /**< a character outside printable ASCII      */
} wa_conf_err_t;

/**
 * @brief No home network, the code on: a fresh store, and what FORGET
 *        leaves.
 * @param c Receives the defaults.
 */
void wa_conf_defaults(wa_conf_t *c);

/**
 * @brief Take a home network.
 *
 * @p ssid of @p ssid_len bytes, 1 to WA_SSID_MAX, no NUL; @p pass of
 * @p pass_len characters, WA_PASS_MIN to WA_PASS_MAX, each printable ASCII
 * (American Standard Code for Information Interchange, 0x20 to 0x7E).
 *
 * @param c        The settings; unchanged unless WA_CONF_OK.
 * @param ssid     The network's name, not terminated.
 * @param ssid_len Its bytes.
 * @param pass     The passphrase, not terminated.
 * @param pass_len Its characters.
 * @return WA_CONF_OK, or what is wrong with the pair.
 */
wa_conf_err_t wa_conf_set_net(wa_conf_t *c, const char *ssid, size_t ssid_len,
                              const char *pass, size_t pass_len);

/** @brief The store a wa_conf_t is kept in. */
typedef struct {
    /** Fill @p out; false when nothing is kept. */
    bool (*load)(void *ctx, wa_conf_t *out);
    /** @brief Write @p c; false when the write failed. */
    bool (*save)(void *ctx, const wa_conf_t *c);
    /** Remove everything kept. */
    bool (*erase)(void *ctx);
    void *ctx;   /**< passed to each */
} wa_store_t;

/**
 * @brief What the store holds, or the defaults when it holds nothing.
 *
 * A kept network that wa_conf_set_net() would refuse is dropped; the
 * switch stays.
 *
 * @param s   The store.
 * @param out Receives the settings.
 */
void wa_conf_load(const wa_store_t *s, wa_conf_t *out);

/**
 * @brief Write @p c into the store.
 * @param s The store.
 * @param c The settings.
 * @return Whether the write worked.
 */
bool wa_conf_save(const wa_store_t *s, const wa_conf_t *c);

/**
 * @brief FORGET: erase the store and leave @p c at the defaults.
 * @param s The store.
 * @param c The settings.
 * @return Whether the erase worked.
 */
bool wa_conf_forget(const wa_store_t *s, wa_conf_t *c);

/**
 * @name The writes owed to the store
 *
 * A change lands in memory at once and in the store on a frame that may
 * write flash (wa_pending_due()).  A flag is cleared only by a write that
 * succeeded and that no later change overtook.  A failed write is shown
 * (failed) and tried again WA_PENDING_RETRIES times, WA_PENDING_RETRY_MS
 * apart; after that it waits for the next change, which tries at once and
 * starts the count again.
 * @{
 */
/** @brief Automatic tries after a failed write. */
#define WA_PENDING_RETRIES   3u
/** @brief The time between them, ms. */
#define WA_PENDING_RETRY_MS  10000u
/** @} */

/** @brief The writes owed to the store. */
typedef struct {
    bool     erase;     /**< FORGET: erase the store                    */
    bool     save;      /**< write the settings in memory               */
    bool     failed;    /**< the last write failed; show it             */
    uint8_t  left;      /**< automatic tries left after a failure       */
    uint32_t next_ms;   /**< earliest automatic try, with failed        */
    uint32_t gen;       /**< changes made                               */
} wa_pending_t;

/**
 * @brief Nothing owed.
 * @param p The writes owed.
 */
void wa_pending_init(wa_pending_t *p);

/**
 * @brief A change of the settings in memory: owe a save.
 * @param p The writes owed.
 */
void wa_pending_change(wa_pending_t *p);

/**
 * @brief FORGET: owe an erase; a save owed before it is void.
 * @param p The writes owed.
 */
void wa_pending_forget(wa_pending_t *p);

/**
 * @brief Whether a write is owed and due now.
 * @param p         The writes owed.
 * @param may_write This frame may write flash.
 * @param now_ms    Now.
 * @return true when the caller writes now.
 */
bool wa_pending_due(const wa_pending_t *p, bool may_write, uint32_t now_ms);

/**
 * @brief Write what @p snap owes -- the erase, then @p c -- into @p st.
 *
 * Runs without the caller's lock; @p snap and @p c are copies taken with
 * wa_pending_due() under it.
 *
 * @param st       The store.
 * @param snap     The writes owed, a copy.
 * @param c        The settings, a copy.
 * @param erase_ok Receives the erase's result; true for one not owed.
 * @param save_ok  Receives the save's result; true for one not owed.
 */
void wa_pending_write(const wa_store_t *st, const wa_pending_t *snap,
                      const wa_conf_t *c, bool *erase_ok, bool *save_ok);

/**
 * @brief Record the results of a write that started from @p snap.
 * @param p        The writes owed.
 * @param snap     The copy the write started from.
 * @param erase_ok The erase's result.
 * @param save_ok  The save's result.
 * @param now_ms   Now.
 */
void wa_pending_done(wa_pending_t *p, const wa_pending_t *snap,
                     bool erase_ok, bool save_ok, uint32_t now_ms);

/**
 * @name The ways the screen offers
 * @{
 */
/** @brief The access point, always. */
#define WA_CAN_AP    0x1u
/** @brief The home network, when one is kept. */
#define WA_CAN_JOIN  0x2u
/** @} */

/**
 * @brief Which ways the screen offers.
 * @param c The settings; NULL offers the access point only.
 * @return WA_CAN_AP, with WA_CAN_JOIN when a home network is kept.
 */
unsigned wa_modes(const wa_conf_t *c);

/* ------------------------------------------------------- the scan list */

/** @brief Networks the scan list holds. */
#define WA_SCAN_MAX 16u

/** @brief One network of the scan list. */
typedef struct {
    char   ssid[WA_SSID_MAX + 1u];   /**< its name, terminated */
    int8_t rssi;                     /**< dBm */
} wa_net_t;

/** @brief The scan list, strongest first. */
typedef struct {
    wa_net_t net[WA_SCAN_MAX];   /**< the networks listed  */
    unsigned n;                  /**< how many             */
    unsigned dropped;            /**< networks seen and not listed */
} wa_scan_t;

/**
 * @brief Empty the scan list.
 * @param s The list.
 */
void wa_scan_clear(wa_scan_t *s);

/**
 * @brief One network a scan saw.
 *
 * Kept when @p usable (WPA2 or WPA3 personal) and named; a name already
 * listed keeps the stronger signal.  The list is held strongest first, and
 * a full list keeps the WA_SCAN_MAX strongest.
 *
 * @param s      The list.
 * @param ssid   The name, @p len bytes, not terminated.
 * @param len    Its bytes.
 * @param rssi   Its signal, dBm.
 * @param usable WPA2 or WPA3 personal.
 */
void wa_scan_add(wa_scan_t *s, const uint8_t *ssid, size_t len, int8_t rssi,
                 bool usable);

/**
 * @brief The name at row @p i.
 * @param s The list; NULL reads as empty.
 * @param i The row.
 * @return The name, or NULL past the end.
 */
const char *wa_scan_pick(const wa_scan_t *s, unsigned i);

/* ------------------------------------------------------- the session */

/** @brief What a session is for. */
typedef enum {
    WA_MODE_AP = 0,    /**< the access point and the page             */
    WA_MODE_JOIN,      /**< the home network and the page             */
    WA_MODE_SCAN,      /**< a scan for the home network list, no page */
    /** The home network for the panel's own requests to GitHub
     *  (wa_fetch.h): no page, no name, no access code asked. */
    WA_MODE_FETCH,
} wa_mode_t;

/** @brief Where a session is. */
typedef enum {
    WA_ST_OFF = 0,     /**< the radio is off                              */
    WA_ST_STARTING,    /**< the access point or the scan is coming up    */
    WA_ST_AP,          /**< the access point is up                        */
    WA_ST_SCANNING,    /**< a scan session, its list not read yet         */
    WA_ST_JOINING,     /**< looking for the home network                  */
    WA_ST_ADDRESS,     /**< on the home network, waiting for an address   */
    WA_ST_JOINED,      /**< on the home network with an address           */
} wa_state_t;

/** @brief Why the radio is off. */
typedef enum {
    WA_OFF_NONE = 0,   /**< never started, or a scan finished            */
    WA_OFF_ARMED,      /**< ARM, or an arm waiting                        */
    WA_OFF_SUPPLY,     /**< the supply's output on or queued on           */
    WA_OFF_RUN,        /**< a log run is open                             */
    WA_OFF_STOP,       /**< STOP                                          */
    WA_OFF_IDLE,       /**< WA_IDLE_MS without a request                  */
    WA_OFF_GUESSES,    /**< WA_GUESS_MAX wrong access codes               */
    WA_OFF_ASKED,      /**< the operator switched it off                  */
    WA_OFF_INSTALL,    /**< an install is about to write the flash        */
    WA_OFF_FAILED,     /**< the radio did not come up                     */
    WA_OFF_JOIN,       /**< a join failed: see wa_t.join_err              */
} wa_off_t;

/** @brief Why a join failed. */
typedef enum {
    WA_JOIN_OK = 0,      /**< no join failed                             */
    WA_JOIN_PASSWORD,    /**< the network refused the password           */
    WA_JOIN_NOT_FOUND,   /**< no such network in reach                   */
    WA_JOIN_NO_ADDRESS,  /**< no address within WA_ADDRESS_MS            */
    WA_JOIN_LOST,        /**< joined, then the network went away         */
} wa_join_err_t;

/** @brief A session: what the radio should do, and where it is. */
typedef struct {
    wa_state_t    state;      /**< where the session is                   */
    wa_mode_t     mode;       /**< what it is for                         */
    wa_off_t      off;        /**< why it went off, with WA_ST_OFF        */
    wa_join_err_t join_err;   /**< with WA_OFF_JOIN                       */
    uint32_t      since_ms;   /**< when the state was entered             */
    uint32_t      last_ms;    /**< the last request's start or end        */
    unsigned      busy;       /**< requests under way                     */
    unsigned      guesses;    /**< wrong codes in this session            */
    unsigned      auth_drops; /**< auth-class drops in a row in this join */
    uint32_t      ip;         /**< the address, network byte order       */
    unsigned      scan_runs;  /**< scans started in this scan session     */
    uint32_t      scan_tok;   /**< the scan under way, 0 for none         */
    uint32_t      scan_seq;   /**< the last scan token given; kept by
                                   wa_start(), so no two scans share one  */
    bool          linger;     /**< switched off while joined, and the
                                   radio may stay up for the platform's
                                   last request (wa_lingers())            */
    char          pass[WA_SECRET_LEN + 1u];   /**< the access point's    */
    char          code[WA_SECRET_LEN + 1u];   /**< the page's access code */
} wa_t;

/**
 * @brief Off, nothing kept of any session.
 * @param w The session.
 */
void wa_init(wa_t *w);

/**
 * @brief Why the radio may not run now.
 * @param armed  As flash_gate_open() takes it.
 * @param supply Likewise.
 * @param run    A log run is open.
 * @return The reason, or WA_OFF_NONE.
 */
wa_off_t wa_gate(bool armed, bool supply, bool run);

/**
 * @brief Start in @p mode when @p gate is WA_OFF_NONE, with a fresh
 *        password and code.
 *
 * A start while the radio runs ends that session first.
 *
 * @param w      The session.
 * @param mode   What the session is for.
 * @param gate   wa_gate()'s answer.
 * @param now_ms Now.
 * @param rand   The source of the secrets.
 * @param ctx    Passed to @p rand.
 * @return WA_OFF_NONE, and the platform brings the radio up; otherwise the
 *         reason, and the radio stays off.
 */
wa_off_t wa_start(wa_t *w, wa_mode_t mode, wa_off_t gate, uint32_t now_ms,
                  wa_rand_fn rand, void *ctx);

/**
 * @brief Switch off for @p why.
 *
 * A joined session switched off by the operator (WA_OFF_ASKED, which is
 * also a release check's own end) or by WA_IDLE_MS lingers (wa_lingers()).
 * Already off: only a reason that does not linger acts, and it ends the
 * linger.
 *
 * @param w   The session.
 * @param why The reason.
 */
void wa_close(wa_t *w, wa_off_t why);

/**
 * @brief Whether the platform's radio should be running.
 * @param w The session.
 * @return true for any state but WA_ST_OFF.
 */
bool wa_radio(const wa_t *w);

/**
 * @brief Whether the radio may stay up past the session's end, for a
 *        request of the platform's own that is under way (the clock's NTP
 *        request, wall_ntp_holds()).
 *
 * Only a joined session switched off for WA_OFF_ASKED or WA_OFF_IDLE
 * lingers.  The linger ends at once with a gate (ARM, the supply, a log
 * run), STOP, any other wa_close() reason (an install, a failed radio),
 * a drop of the station and any wa_start(); a new session's start takes
 * the radio down first.
 *
 * @param w The session.
 * @return true while the session is off and lingers.
 */
bool wa_lingers(const wa_t *w);

/**
 * @brief The platform needs the radio off now: a linger ends; a session
 *        that runs goes on.
 * @param w The session.
 */
void wa_unlinger(wa_t *w);

/**
 * @brief Whether the page is being served.
 * @param w The session.
 * @return true with the access point up or joined, not for WA_MODE_FETCH.
 */
bool wa_serving(const wa_t *w);

/**
 * @brief The access point came up.
 * @param w      The session.
 * @param now_ms Now.
 */
void wa_on_ap_up(wa_t *w, uint32_t now_ms);

/**
 * @brief A scan session's list was read: the session ends, off
 *        WA_OFF_NONE.  wa_on_scan_end() does it with the scan's result.
 * @param w The session.
 */
void wa_on_scan_done(wa_t *w);

/**
 * @brief The station reached the home network.
 * @param w      The session.
 * @param now_ms Now.
 */
void wa_on_associated(wa_t *w, uint32_t now_ms);

/**
 * @brief The station has an address.
 * @param w      The session.
 * @param ip     The address, network byte order.
 * @param now_ms Now.
 */
void wa_on_address(wa_t *w, uint32_t ip, uint32_t now_ms);

/**
 * @brief A join failed or a joined network went away.
 * @param w   The session.
 * @param why As the platform reads its reason code.
 */
void wa_on_join_failed(wa_t *w, wa_join_err_t why);

/**
 * @brief The radio dropped the station.
 *
 * Joining: an auth-class drop counts, any other drop clears the count, and
 * the WA_AUTH_TRIES-th auth-class drop in a row ends the join as
 * WA_JOIN_PASSWORD.  On the network, with or without an address:
 * WA_JOIN_LOST.  A join that reaches WA_JOIN_MS ends as WA_JOIN_PASSWORD
 * when its last drop was auth-class, and as WA_JOIN_NOT_FOUND after any
 * other drop or none (wa_tick()).
 *
 * @param w    The session.
 * @param auth The platform reads the reason as auth-class (the key
 *             refused, a MIC (message integrity code) failure, the key
 *             handshake out of time).
 * @return Whether the platform asks the radio to connect again: true while
 *         the join goes on.
 */
bool wa_on_drop(wa_t *w, bool auth);

/**
 * @brief The radio could not be started: off WA_OFF_FAILED.
 *        A refused scan is wa_scan_refused().
 * @param w The session.
 */
void wa_on_failed(wa_t *w);

/* ------------------------------------------------------- the scan */

/**
 * @brief What the platform does with a scan the radio reports done
 *        (wa_on_scan_end()).
 */
typedef enum {
    WA_SCAN_IGNORE = 0, /**< not the scan under way: nothing changed       */
    WA_SCAN_TAKE,       /**< keep the list read; the session ended, off
                             WA_OFF_NONE                                    */
    WA_SCAN_AGAIN,      /**< start the next scan of the session now         */
    WA_SCAN_FAILED,     /**< the session ended, off WA_OFF_FAILED           */
} wa_scan_step_t;

/**
 * @brief The radio's station started in a scan session: the token of the
 *        scan the platform starts now, or 0 for none.
 *
 * One scan at a time: a second report of the station's start while a scan
 * runs starts none, as a second esp_wifi_scan_start() would replace the
 * first and report it done with nothing found.  Each scan has its own
 * token, never 0, which the platform carries through the calls that run
 * outside its lock: a refusal (wa_scan_refused()), a done report
 * (wa_on_scan_end()) and a retry (wa_scan_is()) act only on the scan under
 * way, never on a session started after it.
 *
 * @param w The session.
 * @return The scan's token, once per scan session, with the session
 *         scanning and no scan under way; 0 otherwise.
 */
uint32_t wa_scan_begin(wa_t *w);

/**
 * @brief The scan of this session under way, its done report still owed.
 * @param w The session.
 * @return Its token, or 0 for none.
 */
uint32_t wa_scan_pending(const wa_t *w);

/**
 * @brief Whether the scan of @p tok is the one under way.
 * @param w   The session.
 * @param tok A token wa_scan_begin() or wa_on_scan_end() gave.
 * @return true for the scan under way; false for 0, for a scan that
 *         ended and for one of an earlier session.
 */
bool wa_scan_is(const wa_t *w, uint32_t tok);

/**
 * @brief The radio refused to start the scan of @p tok: off
 *        WA_OFF_FAILED when it is the scan under way.
 * @param w   The session.
 * @param tok The scan's token.
 * @return Whether the session ended; false for a scan no longer under way.
 */
bool wa_scan_refused(wa_t *w, uint32_t tok);

/**
 * @brief Whether a scan reported done counts as done.
 *
 * The read holds what the report counts, or fills the buffer when the
 * report counts more than fit.
 *
 * @param status   The done report's status: 0 success, other a failure.
 * @param reported The networks the done report counts.
 * @param read_ok  The list was read (esp_wifi_scan_get_ap_records()).
 * @param read     The networks read.
 * @param room     The records the read had room for.
 * @return true for a status of 0 and @p read equal to the smaller of
 *         @p reported and @p room.
 */
bool wa_scan_read_ok(uint32_t status, unsigned reported, bool read_ok,
                     unsigned read, unsigned room);

/**
 * @brief The scan of @p tok reported done; @p ok as wa_scan_read_ok()
 *        says.
 *
 * A report for a scan that is not the one under way changes nothing.  A
 * failed scan is started again while WA_SCAN_TRIES allows; the last one
 * failed ends the session WA_OFF_FAILED, never as an empty list.
 *
 * @param w    The session.
 * @param tok  The token of the scan the report was read for.
 * @param ok   The scan worked and its list was read.
 * @param next Receives the token of the scan to start with WA_SCAN_AGAIN,
 *             0 otherwise.
 * @return What the platform does next.
 */
wa_scan_step_t wa_on_scan_end(wa_t *w, uint32_t tok, bool ok,
                              uint32_t *next);

/**
 * @brief Once a frame: the gate, STOP and the time-outs.  Off, a gate or
 *        STOP ends a linger (wa_lingers()).
 * @param w      The session.
 * @param now_ms Now.
 * @param gate   wa_gate()'s answer.
 * @param stop   STOP pressed since the last call.
 * @return Whether this call switched the session off or ended its linger.
 */
bool wa_tick(wa_t *w, uint32_t now_ms, wa_off_t gate, bool stop);

/**
 * @brief A request begins; one under way holds the idle time-out off.
 * @param w      The session.
 * @param now_ms Now.
 */
void wa_request_begin(wa_t *w, uint32_t now_ms);

/**
 * @brief A request ends; the idle time counts from now.
 * @param w      The session.
 * @param now_ms Now.
 */
void wa_request_end(wa_t *w, uint32_t now_ms);

/**
 * @brief The time left before the idle time-out.
 * @param w      The session.
 * @param now_ms Now.
 * @return ms; 0 while a request is under way or no page is served.
 */
uint32_t wa_idle_left(const wa_t *w, uint32_t now_ms);

/** @brief What the access code check says of a request. */
typedef enum {
    WA_CODE_OK = 0,    /**< the right code, or the code off            */
    WA_CODE_MISSING,   /**< none sent: the page asks for it            */
    WA_CODE_WRONG,     /**< counted toward WA_GUESS_MAX                 */
    WA_CODE_CLOSED,    /**< no session                                  */
} wa_code_t;

/**
 * @brief Check @p given against this session's code.
 *
 * A code from an earlier session is wrong.  The WA_GUESS_MAX-th wrong one
 * switches the session off.
 *
 * @param w       The session.
 * @param given   The code sent, NULL or terminated.
 * @param code_on The code switch; false passes every request while the
 *                page is served.
 * @return The check's answer; WA_CODE_CLOSED when no page is served.
 */
wa_code_t wa_code_check(wa_t *w, const char *given, bool code_on);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_SESSION_H */
