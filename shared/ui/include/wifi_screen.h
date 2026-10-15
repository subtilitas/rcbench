/*
 * FILES & UPDATE: the panel's Wi-Fi, for two jobs in a browser -- every file
 * on the card to download, and a panel firmware image to upload -- and the
 * screen that writes an uploaded image (PANEL UPDATE).
 *
 * The screen opens from LOGS and runs the radio only while it is open.  It
 * offers the panel's own access point always, and the home network when one
 * is kept: chosen from a scan, or typed for a hidden one, the password typed
 * on the Wi-Fi keyboard (ui_textkey_open_wifi()).  It shows what a browser
 * needs -- network, password, address, name, access code -- and why the
 * radio went off when it did.
 *
 * An uploaded image waits on the panel as a question naming both versions.
 * The browser cannot start the write: INSTALL is a 2 s hold here (UI_HOLD_S),
 * and the hold ends unfired when the image goes away under it -- a STOP, an
 * ARM, CANCEL -- or the install gate closes.
 *
 * The screen commands nothing itself.  The application hands it what to show
 * (wifi_screen_set()) and drains what it asks for (wifi_screen_take()), as it
 * does STOP: the UI (user interface) never calls into hardware.  The enums
 * here mirror the Wi-Fi session's and the installer's, which live in other
 * components; the application maps one to the other in one place.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_SCREEN_H
#define RCBENCH_WIFI_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID_MAX   32
#define WIFI_PASS_MAX   63
#define WIFI_SCAN_ROWS  16
#define WIFI_VER_MAX    32

/** The radio, as the session has it (wa_state_t). */
typedef enum {
    WIFI_RADIO_OFF = 0,
    WIFI_RADIO_STARTING,
    WIFI_RADIO_AP,
    WIFI_RADIO_SCANNING,
    WIFI_RADIO_JOINING,
    WIFI_RADIO_ADDRESS,
    WIFI_RADIO_JOINED,
} wifi_radio_t;

/** Why the radio is off, or may not start (wa_off_t). */
typedef enum {
    WIFI_WHY_NONE = 0,
    WIFI_WHY_ARMED,
    WIFI_WHY_SUPPLY,
    WIFI_WHY_RUN,
    WIFI_WHY_STOP,
    WIFI_WHY_IDLE,
    WIFI_WHY_GUESSES,
    WIFI_WHY_ASKED,
    WIFI_WHY_INSTALL,
    WIFI_WHY_FAILED,
    WIFI_WHY_JOIN,
    /** The panel's own, with no wa_off_t twin: Wi-Fi could not be set up
     *  at start, and nothing on the screen can start it. */
    WIFI_WHY_UNAVAILABLE,
} wifi_why_t;

/** How a join failed (wa_join_err_t). */
typedef enum {
    WIFI_JOIN_OK = 0,
    WIFI_JOIN_PASSWORD,
    WIFI_JOIN_NOT_FOUND,
    WIFI_JOIN_NO_ADDRESS,
    WIFI_JOIN_LOST,
} wifi_join_t;

/** The upload, as the receiver has it (fw_upload_state_t). */
typedef enum {
    WIFI_UP_IDLE = 0,
    WIFI_UP_RECEIVING,
    WIFI_UP_STAGED,
    WIFI_UP_FAILED,
} wifi_up_t;

/** What a staged image is to the running one (fw_offer_t). */
typedef enum {
    WIFI_OFFER_NEWER = 0,
    WIFI_OFFER_OLDER,
    WIFI_OFFER_REBUILD,     /**< same version, another build             */
    WIFI_OFFER_INSTALLED,   /**< the build that runs: nothing to install */
} wifi_offer_t;

/** Whether flash may be written now (fw_gate_t). */
typedef enum {
    WIFI_GATE_OPEN = 0,
    WIFI_GATE_ARMED,
    WIFI_GATE_SUPPLY,
    WIFI_GATE_RUN,
    WIFI_GATE_DRAWING,      /**< a board drawing is fetched or kept      */
    WIFI_GATE_COPROC,       /**< the coprocessor's update runs           */
} wifi_gate_t;

typedef struct {
    /* The radio's gate: why it may not start now, or WIFI_WHY_NONE. */
    wifi_why_t   block;

    /* What is kept. */
    bool         have_net;
    char         net[WIFI_SSID_MAX + 1];
    bool         code_on;
    bool         save_failed;  /**< the last write of the above failed    */

    /* The radio. */
    wifi_radio_t radio;
    wifi_why_t   off;          /**< why it last went off                 */
    wifi_join_t  join;         /**< with WIFI_WHY_JOIN                    */
    char         name[16];     /**< "rcbench-1A2B"                        */
    char         pass[12];     /**< the access point's                    */
    char         code[12];     /**< the page's access code                */
    uint8_t      ip[4];
    uint8_t      channel;
    uint32_t     idle_left_s;  /**< until the radio goes off unused       */

    /* The last scan, strongest first. */
    bool         scanned;
    int          scan_n;
    char         scan[WIFI_SCAN_ROWS][WIFI_SSID_MAX + 1];
    int8_t       rssi[WIFI_SCAN_ROWS];

    /* The upload, and the image that waits. */
    wifi_up_t    up;
    uint32_t     received;
    uint32_t     total;
    wifi_offer_t offer;
    wifi_gate_t  gate;
    char         new_ver[WIFI_VER_MAX + 1];
    char         run_ver[WIFI_VER_MAX + 1];
} wifi_view_t;

/** What the screen asks for: bits of wifi_screen_take(). */
#define WIFI_REQ_AP        0x001u   /**< start the access point          */
#define WIFI_REQ_JOIN      0x002u   /**< join the kept home network      */
#define WIFI_REQ_OFF       0x004u   /**< radio off                        */
#define WIFI_REQ_SCAN      0x008u
#define WIFI_REQ_SAVE_NET  0x010u   /**< keep wifi_screen_net()'s network */
#define WIFI_REQ_FORGET    0x020u
#define WIFI_REQ_CODE_ON   0x040u
#define WIFI_REQ_CODE_OFF  0x080u
#define WIFI_REQ_DISCARD   0x100u   /**< drop the staged image           */
#define WIFI_REQ_INSTALL   0x200u   /**< the 2 s hold completed          */

/** Hand the screen what to show. */
void wifi_screen_set(const wifi_view_t *v);

/**
 * Wi-Fi could not be set up at start: show WIFI_WHY_UNAVAILABLE with every
 * control off.  Needs no view from the caller, so it holds when the memory
 * for one is what ran out.
 */
void wifi_screen_set_unavailable(void);

/** What is showing, for the application and the tests. */
const wifi_view_t *wifi_screen_view(void);

/** The requests since the last call, as WIFI_REQ_* bits; cleared. */
unsigned wifi_screen_take(void);

/** The network WIFI_REQ_SAVE_NET keeps: its name and its password. */
void wifi_screen_net(char ssid[WIFI_SSID_MAX + 1], char pass[WIFI_PASS_MAX + 1]);

/** Repaint on the next frame. */
void wifi_screen_invalidate(void);

const ui_screen_t *wifi_screen(void);

/* ------------------------------------------------------ PANEL UPDATE */

typedef enum {
    UPDATE_WRITING = 0,
    UPDATE_RESTARTING,     /**< written and checked                       */
    UPDATE_FAILED,         /**< nothing changed: the running image stays  */
    /** SETUP > UPDATES' download from GitHub, before the write: joining,
     *  SHA256SUMS, the image, its checks.  CANCEL ends it. */
    UPDATE_FETCHING,
    UPDATE_FETCH_FAILED,   /**< nothing written: the running image stays  */
    /** The image checked out, but the release key does not vouch for it (no
     *  signature, or one that does not verify).  Nothing is written; a
     *  second 2 s hold writes it, CANCEL drops it. */
    UPDATE_UNSIGNED,
} update_state_t;

typedef struct {
    update_state_t state;
    char           version[WIFI_VER_MAX + 1];
    uint32_t       written;    /**< FETCHING: bytes received              */
    uint32_t       total;
    /** FETCHING: the step and the time left; FETCH_FAILED: why, in one
     *  line or two.  The application writes them (upd_result_text()). */
    char           what[96];
    char           why[96];
    /** UNSIGNED: a signature came with the image and does not verify; false
     *  when none came. */
    bool           sig_bad;
} update_view_t;

void update_screen_set(const update_view_t *v);

/** True once CLOSE was tapped on a failed write or download, or CANCEL on an
 *  UNSIGNED image; cleared when read. */
bool update_screen_take_close(void);

/** True once the second hold of an UNSIGNED image completed; cleared when
 *  read. */
bool update_screen_take_go(void);

/** True once CANCEL was tapped on a download; cleared when read. */
bool update_screen_take_cancel(void);

void update_screen_invalidate(void);

const ui_screen_t *update_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WIFI_SCREEN_H */
