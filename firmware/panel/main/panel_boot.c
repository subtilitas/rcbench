/**
 * @file panel_boot.c
 * @brief The panel's start-up: the board, the settings, the display,
 *        touch, the card, the link and the IOmcu, each reported to the
 *        splash as it happens.
 *
 * app_main (main.c) calls bring_up() once.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdio.h>

#include "esp_log.h"

#include "art_flash_esp.h"
#include "board_pins.h"
#include "busfault_screen.h"
#include "can_selftest.h"
#include "can_twai.h"
#include "display.h"
#include "fw_ota.h"
#include "log_viewer_screen.h"
#include "panel.h"
#include "panel_main.h"
#include "panel_perf.h"
#include "panel_trial.h"
#include "rcbench_version.h"
#include "selftest.h"
#include "settings.h"
#include "settings_screen.h"
#include "splash_screen.h"
#include "storage.h"
#include "touch.h"
#include "ui_card_text.h"
#include "ui_screen.h"
#include "ui_board.h"
#include "ui_text.h"
#include "wifi_access.h"

/** @brief Log tag. */
static const char *TAG = "rcbench";

/* ------------------------------------------------------------ the CAN bus */

/**
 * @brief How long the start-up echo test runs.
 *
 * A verdict needs CAN_SELFTEST_MIN_PROBES (8) probes and one probe is in
 * flight at a time, so with a far end that answers this is hundreds of
 * probes and with one that does not it is tens.  It is spent inside the
 * splash, which holds afterwards anyway, so it costs no start-up time an
 * operator waits through.
 */
#define CAN_SELFTEST_MS 1200u

/**
 * @brief The start check's bus-fault report, shown on the band after the
 *        splash
 */
static busfault_report_t s_busfault;
bool                     s_bus_ok = true;  /**< whether the CAN self-test passed */
bool                     s_touch_ok = true;  /**< whether touch_init() answered */

/* What the start check found, for the band once the splash has gone. */
char               s_boot_alert[ALERT_MAX];

/* ------------------------------------------------------------------- boot */

/**
 * @brief Whether the bench is ARMED, as the router's status says; the
 *        settings model's ARMED source (settings_set_armed()).
 * @return True while ARMED.
 */
static bool bench_armed(void)
{
    return ui_router_status()->armed;
}

/**
 * @brief Draws one frame of the splash between initialisation steps, so the
 *        splash fills in as it happens.
 */
static void pump(void)
{
    /* Draw between init steps so the splash fills in as it happens, rather
     * than appearing complete at the end. */
    gfx_canvas_t *c = panel_canvas();
    if (c != NULL) {
        ui_router_render(c, display_back_index());
        display_flip();
    }
}

/**
 * @brief How long to keep asking who is there: 3000ms is about three attempts,
 *        because a poll with no answer costs the 1000ms host timeout.
 */
#define IDENTITY_WAIT_MS 3000u


/**
 * @brief Ask the IOmcu who it is, repeatedly, for IDENTITY_WAIT_MS.
 *
 * The two boards do not finish booting at the same instant, and a first frame
 * onto a bus whose far end is not listening is retransmitted in silicon until
 * it is acknowledged, so a single attempt can miss a working IOmcu.
 * The wait is bounded: a bench with no IOmcu attached must not sit on
 * the splash for ever.
 */
static bool poll_identity(link_host_t *host, link_msg_t *reply)
{
    const uint32_t start = now_ms();
    do {
        if (poll_page(host, LINK_PAGE_IDENTITY, LINK_ID_COUNT, reply)
            && reply->op == LINK_OP_DATA) {
            return true;
        }
        pump();
    } while ((uint32_t)(now_ms() - start) < IDENTITY_WAIT_MS);
    return false;
}

bool bring_up(void)
{
    bool ok = true;

    /*
     * Schema defaults, then the values the NVS (non-volatile storage) store
     * holds on top of them.  A store that cannot be opened leaves the
     * defaults in place: a working bench with unsaved settings.
     *
     * Done before the panel starts scanning, and reported at its place in the
     * list below, so the stored theme is in force for the first frame drawn
     * rather than from the second onwards.
     */
    const settings_store_t *store = settings_nvs_store();
    settings_set_store(store);
    /* Installed before the load, so the count the store returns is owed to
     * the IOmcu the same way an edit made later is. */
    settings_set_observer(settings_changed);
    settings_set_text_observer(publish_log_folder);
    /* Screen rotation is held while ARMED, as the router's status says:
     * the status DISCARD on SETUP waits on. */
    settings_set_armed(bench_armed);
    settings_init();
    /* And once unconditionally, whatever settings_init() told the observer:
     * a bench left at the defaults still has to tell the far end what it
     * is, and each snapshot the control task reads starts zeroed.  The
     * pulse endpoints the same way: the far end keeps CHAN_CFG in flash, so
     * a range written by another panel, or before a reset to defaults,
     * would otherwise stay on the motor channels.  The current monitors'
     * set-up too: a zeroed one is no set-up at all.  Nothing is written
     * when the page already agrees. */
    publish_poles();
    publish_endpoints();
    publish_pdmini();
    publish_sense();
    publish_log_auto();
    publish_tone();
    publish_hitec_rx();
    /* The zone before the card mounts, so the first file stamped is
     * stamped in it; and what the clock holds, on the console. */
    publish_time_zone();
    log_clock_at_start();
    settings_apply_ui();
    /* The panel's own build, on the first line it draws, in the language
     * the settings just put in force.  It is the host, so it publishes no
     * identity page; without this the only version anywhere on the bench
     * would be the IOmcu's.  "panel firmware": the board's name beside it
     * is the board, not a build. */
    {
        char board[SPLASH_DETAIL_MAX];
        snprintf(board, sizeof(board), TR(SPLASH_PANEL_FW), BOARD_NAME_TEXT,
                 RCBENCH_VERSION_STRING);
        splash_screen_set(SPLASH_STEP_BOARD, SPLASH_OK, board);
    }
    /*
     * The panel's Wi-Fi reads its kept home network now, before the panel
     * scans, as the settings are read here.  The radio stays off.
     */
    {
        fw_image_info_t running;
        if (!fw_ota_running(&running)) {
            ESP_LOGW(TAG, "running version \"%s\" does not parse",
                     running.version_text);
        }
        s_wifi_ok = (wifi_access_init(fw_ota_slot_bytes(), &running)
                     == ESP_OK);
        if (!s_wifi_ok) {
            ESP_LOGE(TAG, "Wi-Fi not set up: unavailable until a restart");
        }
    }

    display_config_t dcfg = DISPLAY_CONFIG_DEFAULT();
    if (display_init(&dcfg) == ESP_OK) {
        /* Before the first frame: nothing is painted yet that the probe's
         * clears would spoil. */
        perf_probe_psram();
        splash_screen_set(SPLASH_STEP_DISPLAY, SPLASH_OK, BOARD_DISPLAY_TEXT);
    } else {
        splash_screen_set(SPLASH_STEP_DISPLAY, SPLASH_FAIL, TR(SPLASH_NO_PANEL));
        /* A build on its first start after an update goes back to the one
         * before it, here and now; any other start goes on as before. */
        panel_trial_begin(false);
        return false;   /* nothing can be reported after this */
    }
    pump();

    touch_config_t tcfg = TOUCH_CONFIG_DEFAULT();
    if (touch_init(&tcfg) == ESP_OK) {
        splash_screen_set(SPLASH_STEP_TOUCH, SPLASH_OK, "GT911 5pt");
    } else {
        /* A bench with no touch has no STOP button, so this is fatal rather
         * than degraded -- but it is reported first. */
        splash_screen_set(SPLASH_STEP_TOUCH, SPLASH_FAIL, TR(SPLASH_NO_ANSWER));
        s_touch_ok = false;
        ok = false;
    }
    pump();

    /*
     * A build's first start after an update is a trial.  Without touch --
     * no STOP -- the panel restarts into the build before it here.  With
     * the display and touch up the trial stays open until the panel works
     * as a whole: the card, the settings, the control and logger tasks and
     * a frame on the display (panel_trial_frame()).
     */
    panel_trial_begin(s_touch_ok);

    /* A card is optional: the log viewer wants one, nothing else does, so a
     * missing card is a warning the operator reads on the way past rather
     * than a boot failure. */
    (void)storage_init();
    /* And the viewer is told how to reach it.  Without this it has no way to
     * list anything and reports no card whatever is mounted. */
    log_viewer_set_io(&k_card_io);
    /* FILES reaches the card through its own task, started at its first
     * request. */
    files_screen_set_io(&k_files_io);
    /* Before anything can ask for a profile; with no card, the built-in
     * ones alone. */
    esc_profiles_load();
    /* No card, not FAT32 or unreadable, in the language showing; the
     * console line of storage_init() has the code behind it. */
    splash_screen_set(SPLASH_STEP_STORAGE,
                      storage_mounted() ? SPLASH_OK : SPLASH_WARN,
                      storage_mounted()
                          ? storage_status()
                          : ui_card_text(storage_card_state(),
                                         UI_CARD_AT_SPLASH));
    pump();

    /* Loaded above, before the panel started scanning. */
    splash_screen_set(SPLASH_STEP_SETTINGS,
                      store != NULL ? SPLASH_OK : SPLASH_WARN,
                      store != NULL ? "NVS" : TR(SPLASH_NVS_MISSING));
    pump();

    /* CAN (Controller Area Network): TWAI on PANEL_CAN_PIN_TX and _RX. */
    const bool link_open = (can_twai_start(PANEL_CAN_BITRATE) == ESP_OK);
    splash_screen_set(SPLASH_STEP_LINK,
                      link_open ? SPLASH_OK : SPLASH_WARN,
                      link_open ? "CAN 1Mbit/s" : TR(SPLASH_NOT_OPENED));
    /*
     * After the bus is up, so the echo test measures the bus rather than
     * reporting "nothing came back" regardless of the hardware, and before
     * the identity poll, so a broken bus is diagnosed as such rather than as
     * an identity that never answers.
     *
     * It runs at every start-up.  A bus that does not carry frames looks
     * exactly like an IOmcu that is not fitted, and both look like a
     * bench that shows no numbers; nothing else the panel does separates
     * them, and an operator with no console cannot.
     */
    if (link_open) {
        s_bus_ok = can_selftest_run(CAN_SELFTEST_MS, &s_busfault);
        splash_screen_set(SPLASH_STEP_LINK,
                          s_bus_ok ? SPLASH_OK : SPLASH_FAIL,
                          s_bus_ok ? "CAN 1Mbit/s"
                                   : busfault_verdict_text(
                                         s_busfault.verdict));
        if (!s_bus_ok) {
            busfault_screen_set(&s_busfault);
        }
    }
    pump();

    /*
     * Ask who is there.  The IOMCU step is set to a result on every path: a
     * splash step that is declared and never set keeps all_answered() false,
     * and the splash never hands over.
     */
    /* The partition the drawings live in, looked up once. */
    s_artflash = art_flash_esp();

    link_host_init(&s_host, now_ms());
    if (link_open) {
        link_msg_t reply;
        if (poll_identity(&s_host, &reply)) {
            /*
             * SPLASH_DETAIL_MAX (64) bytes hold five 16-bit registers plus
             * the words, the splash's own field.  A truncating snprintf is a
             * compiler warning, and warnings are errors.
             *
             * The far end's firmware version is printed as well as the
             * protocol it speaks.  Two boards can speak the same protocol
             * and be different builds, and "which one is on the bench" is
             * the question a bring-up line exists to answer.
             */
            char detail[SPLASH_DETAIL_MAX];
            snprintf(detail, sizeof(detail), TR(SPLASH_IOMCU_FW),
                     (unsigned)reply.regs[LINK_ID_PROTOCOL_MAJOR],
                     (unsigned)reply.regs[LINK_ID_PROTOCOL_MINOR],
                     (unsigned)reply.regs[LINK_ID_FIRMWARE_MAJOR],
                     (unsigned)reply.regs[LINK_ID_FIRMWARE_MINOR],
                     (unsigned)reply.regs[LINK_ID_FIRMWARE_PATCH]);
            s_bring.have_identity = true;
            /*
             * What the far end can do.  Read here, at bring-up, rather than
             * at a later poll: a menu that greys itself only after a poll
             * shows a state in which the bench claims more than it has.
             */
            atomic_store(&s_capabilities,
                         (unsigned)reply.regs[LINK_ID_CAPABILITIES]);
            /*
             * Which board answered.  Everything the outputs screen offers is
             * that board's, and a board this build does not know offers
             * nothing -- guessing a pin map is how an output ends up on the
             * safety line.
             */
            s_board               = reply.regs[LINK_ID_HARDWARE];
            ui_board_set(s_board);
            s_bring.proto_major   = reply.regs[LINK_ID_PROTOCOL_MAJOR];
            s_bring.proto_minor   = reply.regs[LINK_ID_PROTOCOL_MINOR];
            const bool speaks_ours =
                reply.regs[LINK_ID_PROTOCOL_MAJOR] == LINK_PROTOCOL_MAJOR;
            splash_screen_set(SPLASH_STEP_IOMCU,
                              speaks_ours ? SPLASH_OK : SPLASH_FAIL, detail);
            ok = ok && speaks_ours;
        } else {
            /*
             * Not a failure: the bench runs without an IOmcu.  The two
             * words name the kind of silence: nothing arriving and a reply
             * that is rejected are different faults, and the poller counts
             * which happened.
             */
            const char *why = TR(SPLASH_NO_ANSWER);
            if (s_host.nacks > 0) {
                why = TR(SPLASH_REFUSED);
            } else if (s_host.mismatches > 0) {
                why = TR(SPLASH_WRONG_REPLY);
            }
            splash_screen_set(SPLASH_STEP_IOMCU, SPLASH_WARN, why);
        }
    } else {
        splash_screen_set(SPLASH_STEP_IOMCU, SPLASH_WARN, TR(SPLASH_NO_LINK));
    }
    pump();

    return ok;
}
