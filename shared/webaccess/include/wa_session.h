/*
 * Wi-Fi access: when the panel's radio runs, under which name, and who the
 * page answers.
 *
 * The panel offers a browser page for two jobs: a panel firmware upload and
 * the card's files for download.  It reaches the browser one of two ways:
 *
 *   ACCESS POINT  its own network, 2.4 GHz, WPA2 (Wi-Fi Protected Access 2),
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
 * a request, and after WA_GUESS_MAX wrong access codes.
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

/** Characters of the access point's password and of the access code. */
#define WA_SECRET_LEN   8u
/** What a secret is made of: no 0, 1, i, l or o, which read alike. */
#define WA_SECRET_CHARS "abcdefghjkmnpqrstuvwxyz23456789"
/** "rcbench-1A2B" and its terminator. */
#define WA_NAME_MAX     13u

/** A home network's name, in bytes, and its WPA2/WPA3 passphrase. */
#define WA_SSID_MAX     32u
#define WA_PASS_MIN     8u
#define WA_PASS_MAX     63u

/** The radio goes off after this long without a request. */
#define WA_IDLE_MS      (10u * 60u * 1000u)
/** A join that has not reached the network after this long failed. */
#define WA_JOIN_MS      20000u
/** A join that reached the network and has no address after this long. */
#define WA_ADDRESS_MS   15000u
/** A start of the access point, or a scan, that has not come up. */
#define WA_START_MS     10000u
/** Wrong access codes before the radio goes off. */
#define WA_GUESS_MAX    16u

/* ------------------------------------------------------------- names */

/** A source of random 32-bit words. */
typedef uint32_t (*wa_rand_fn)(void *ctx);

/**
 * WA_SECRET_LEN characters of WA_SECRET_CHARS and a terminator, each drawn
 * evenly from the alphabet (words that would favour some characters are
 * drawn again).
 */
void wa_secret_make(char out[WA_SECRET_LEN + 1u], wa_rand_fn rand, void *ctx);

/** "rcbench-" and the last two bytes of @p mac in capital hex. */
void wa_name(const uint8_t mac[6], char out[WA_NAME_MAX]);

/* ------------------------------------------------------- what is kept */

typedef struct {
    bool have_net;                    /**< a home network is kept       */
    char ssid[WA_SSID_MAX + 1u];
    char pass[WA_PASS_MAX + 1u];
    bool code_on;                     /**< the page asks for the code   */
} wa_conf_t;

typedef enum {
    WA_CONF_OK = 0,
    WA_CONF_SSID_EMPTY,
    WA_CONF_SSID_LONG,     /**< over WA_SSID_MAX bytes                  */
    WA_CONF_SSID_CHAR,     /**< a NUL byte in it                         */
    WA_CONF_PASS_SHORT,    /**< under WA_PASS_MIN characters             */
    WA_CONF_PASS_LONG,     /**< over WA_PASS_MAX characters              */
    WA_CONF_PASS_CHAR,     /**< a character outside printable ASCII      */
} wa_conf_err_t;

/** No home network, the code on: a fresh store, and what FORGET leaves. */
void wa_conf_defaults(wa_conf_t *c);

/**
 * Take a home network: @p ssid of @p ssid_len bytes, 1 to WA_SSID_MAX, no
 * NUL; @p pass of @p pass_len characters, WA_PASS_MIN to WA_PASS_MAX, each
 * printable ASCII (American Standard Code for Information Interchange,
 * 0x20 to 0x7E).  @p c is unchanged unless WA_CONF_OK.
 */
wa_conf_err_t wa_conf_set_net(wa_conf_t *c, const char *ssid, size_t ssid_len,
                              const char *pass, size_t pass_len);

/** The store a wa_conf_t is kept in. */
typedef struct {
    /** Fill @p out; false when nothing is kept. */
    bool (*load)(void *ctx, wa_conf_t *out);
    bool (*save)(void *ctx, const wa_conf_t *c);
    /** Remove everything kept. */
    bool (*erase)(void *ctx);
    void *ctx;
} wa_store_t;

/**
 * What the store holds, or the defaults when it holds nothing.  A kept
 * network that wa_conf_set_net() would refuse is dropped; the switch stays.
 */
void wa_conf_load(const wa_store_t *s, wa_conf_t *out);
bool wa_conf_save(const wa_store_t *s, const wa_conf_t *c);

/** FORGET: erase the store and leave @p c at the defaults. */
bool wa_conf_forget(const wa_store_t *s, wa_conf_t *c);

/*
 * The writes owed to the store.  A change lands in memory at once and in
 * the store on a frame that may write flash (wa_pending_due()).  A flag is
 * cleared only by a write that succeeded and that no later change
 * overtook.  A failed write is shown (failed) and tried again
 * WA_PENDING_RETRIES times, WA_PENDING_RETRY_MS apart; after that it waits
 * for the next change, which tries at once and starts the count again.
 */
#define WA_PENDING_RETRIES   3u
#define WA_PENDING_RETRY_MS  10000u

typedef struct {
    bool     erase;     /**< FORGET: erase the store                    */
    bool     save;      /**< write the settings in memory               */
    bool     failed;    /**< the last write failed; show it             */
    uint8_t  left;      /**< automatic tries left after a failure       */
    uint32_t next_ms;   /**< earliest automatic try, with failed        */
    uint32_t gen;       /**< changes made                               */
} wa_pending_t;

/** Nothing owed. */
void wa_pending_init(wa_pending_t *p);

/** A change of the settings in memory: owe a save. */
void wa_pending_change(wa_pending_t *p);

/** FORGET: owe an erase; a save owed before it is void. */
void wa_pending_forget(wa_pending_t *p);

/** Whether a write is owed and due now, on a frame that @p may_write. */
bool wa_pending_due(const wa_pending_t *p, bool may_write, uint32_t now_ms);

/**
 * Write what @p snap owes -- the erase, then @p c -- into @p st.  Runs
 * without the caller's lock; @p snap and @p c are copies taken with
 * wa_pending_due() under it.  Each result in @p erase_ok and @p save_ok;
 * true for a write not owed.
 */
void wa_pending_write(const wa_store_t *st, const wa_pending_t *snap,
                      const wa_conf_t *c, bool *erase_ok, bool *save_ok);

/** Record the results of a write that started from @p snap. */
void wa_pending_done(wa_pending_t *p, const wa_pending_t *snap,
                     bool erase_ok, bool save_ok, uint32_t now_ms);

/** Which ways the screen offers: always the access point, and the home
 *  network when one is kept. */
#define WA_CAN_AP    0x1u
#define WA_CAN_JOIN  0x2u
unsigned wa_modes(const wa_conf_t *c);

/* ------------------------------------------------------- the scan list */

#define WA_SCAN_MAX 16u

typedef struct {
    char   ssid[WA_SSID_MAX + 1u];
    int8_t rssi;   /**< dBm */
} wa_net_t;

typedef struct {
    wa_net_t net[WA_SCAN_MAX];
    unsigned n;
    unsigned dropped;   /**< networks seen and not listed */
} wa_scan_t;

void wa_scan_clear(wa_scan_t *s);

/**
 * One network a scan saw.  Kept when @p usable (WPA2 or WPA3 personal) and
 * named; a name already listed keeps the stronger signal.  The list is held
 * strongest first, and a full list keeps the WA_SCAN_MAX strongest.
 * @p ssid is @p len bytes, not terminated.
 */
void wa_scan_add(wa_scan_t *s, const uint8_t *ssid, size_t len, int8_t rssi,
                 bool usable);

/** The name at row @p i, or NULL past the end. */
const char *wa_scan_pick(const wa_scan_t *s, unsigned i);

/* ------------------------------------------------------- the session */

typedef enum {
    WA_MODE_AP = 0,
    WA_MODE_JOIN,
    WA_MODE_SCAN,      /**< a scan for the home network list, no page */
    /** The home network for the panel's own requests to GitHub
     *  (wa_fetch.h): no page, no name, no access code asked. */
    WA_MODE_FETCH,
} wa_mode_t;

typedef enum {
    WA_ST_OFF = 0,
    WA_ST_STARTING,    /**< the access point or the scan is coming up    */
    WA_ST_AP,          /**< the access point is up                        */
    WA_ST_SCANNING,
    WA_ST_JOINING,     /**< looking for the home network                  */
    WA_ST_ADDRESS,     /**< on the home network, waiting for an address   */
    WA_ST_JOINED,      /**< on the home network with an address           */
} wa_state_t;

/** Why the radio is off. */
typedef enum {
    WA_OFF_NONE = 0,   /**< never started, or a scan finished            */
    WA_OFF_ARMED,      /**< ARM, or an arm waiting                        */
    WA_OFF_SUPPLY,     /**< the supply's output on or queued on           */
    WA_OFF_RUN,        /**< a log run is open                             */
    WA_OFF_STOP,
    WA_OFF_IDLE,       /**< WA_IDLE_MS without a request                  */
    WA_OFF_GUESSES,    /**< WA_GUESS_MAX wrong access codes               */
    WA_OFF_ASKED,      /**< the operator switched it off                  */
    WA_OFF_INSTALL,    /**< an install is about to write the flash        */
    WA_OFF_FAILED,     /**< the radio did not come up                     */
    WA_OFF_JOIN,       /**< a join failed: see wa_t.join_err              */
} wa_off_t;

/** Why a join failed. */
typedef enum {
    WA_JOIN_OK = 0,
    WA_JOIN_PASSWORD,    /**< the network refused the password           */
    WA_JOIN_NOT_FOUND,   /**< no such network in reach                   */
    WA_JOIN_NO_ADDRESS,  /**< no address within WA_ADDRESS_MS            */
    WA_JOIN_LOST,        /**< joined, then the network went away         */
} wa_join_err_t;

typedef struct {
    wa_state_t    state;
    wa_mode_t     mode;
    wa_off_t      off;
    wa_join_err_t join_err;
    uint32_t      since_ms;   /**< when the state was entered             */
    uint32_t      last_ms;    /**< the last request's start or end        */
    unsigned      busy;       /**< requests under way                     */
    unsigned      guesses;    /**< wrong codes in this session            */
    uint32_t      ip;         /**< the address, network byte order       */
    char          pass[WA_SECRET_LEN + 1u];   /**< the access point's    */
    char          code[WA_SECRET_LEN + 1u];   /**< the page's access code */
} wa_t;

void wa_init(wa_t *w);

/**
 * Why the radio may not run now, or WA_OFF_NONE: @p armed as
 * flash_gate_open() takes it, @p supply likewise, @p run a log run open.
 */
wa_off_t wa_gate(bool armed, bool supply, bool run);

/**
 * Start in @p mode at @p now_ms when @p gate is WA_OFF_NONE, with a fresh
 * password and code from @p rand: WA_OFF_NONE, and the platform brings the
 * radio up.  Otherwise the reason, and the radio stays off.  A start while
 * the radio runs ends that session first.
 */
wa_off_t wa_start(wa_t *w, wa_mode_t mode, wa_off_t gate, uint32_t now_ms,
                  wa_rand_fn rand, void *ctx);

/** Switch off for @p why.  Nothing when already off. */
void wa_close(wa_t *w, wa_off_t why);

/** Whether the platform's radio should be running. */
bool wa_radio(const wa_t *w);

/** Whether the page is being served: the access point up or joined, not
 *  for WA_MODE_FETCH. */
bool wa_serving(const wa_t *w);

/* What the platform's radio reports. */
void wa_on_ap_up(wa_t *w, uint32_t now_ms);
void wa_on_scan_done(wa_t *w);
void wa_on_associated(wa_t *w, uint32_t now_ms);
void wa_on_address(wa_t *w, uint32_t ip, uint32_t now_ms);
/** A join failed or a joined network went away; @p why as the platform
 *  reads its reason code. */
void wa_on_join_failed(wa_t *w, wa_join_err_t why);
/** The radio could not be started. */
void wa_on_failed(wa_t *w);

/**
 * Once a frame: the gate, STOP (@p stop: pressed since the last call) and
 * the time-outs.  Returns whether this call switched the session off.
 */
bool wa_tick(wa_t *w, uint32_t now_ms, wa_off_t gate, bool stop);

/** A request begins and ends; one under way holds the idle time-out off. */
void wa_request_begin(wa_t *w, uint32_t now_ms);
void wa_request_end(wa_t *w, uint32_t now_ms);

/** The ms left before the idle time-out, 0 while a request is under way. */
uint32_t wa_idle_left(const wa_t *w, uint32_t now_ms);

/** What the access code check says of a request. */
typedef enum {
    WA_CODE_OK = 0,
    WA_CODE_MISSING,   /**< none sent: the page asks for it            */
    WA_CODE_WRONG,     /**< counted toward WA_GUESS_MAX                 */
    WA_CODE_CLOSED,    /**< no session                                  */
} wa_code_t;

/**
 * Check @p given, NULL or terminated, against this session's code.  With
 * @p code_on false every request passes.  A code from an earlier session
 * is wrong.  The WA_GUESS_MAX-th wrong one switches the session off.
 */
wa_code_t wa_code_check(wa_t *w, const char *given, bool code_on);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_SESSION_H */
