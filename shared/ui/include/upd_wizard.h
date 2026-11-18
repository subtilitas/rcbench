/**
 * @file upd_wizard.h
 * @brief SETUP > UPDATES' step model: six steps in a fixed order, PANEL,
 *        IOMCU, C6 RADIO, DIAL, POWER NODE and BALANCE NODE, each step's
 *        state, its list tag and the one control of the current step.
 *
 * The panel image carries every other image, so it comes first.  The C6
 * comes before the wireless devices: their transfers run on the C6's
 * Bluetooth LE (Low Energy) controller, and a C6 update restarts the panel.
 *
 * upd_wizard_eval() is a pure function of what UPDATES is handed once a
 * frame: the four views (upd_view_t, upd_cu_view_t, upd_c6_view_t and one
 * upd_dev_view_t per device), whether an update run stands after a restart,
 * the steps the operator skipped since the panel's start, and each step's
 * last failed attempt.  No step position is stored: a restart, a lost link
 * or a failed step gives the right step from the facts alone.
 *
 * The current step is the step whose update runs, whatever its number;
 * with no update running, PANEL while its check runs; else the
 * lowest-numbered step that blocks.  An update outranks PANEL's check: a
 * device's ask and CANCEL UPDATE stay on the card while a check runs
 * beside its transfer.  RUNNING, OPEN,
 * HELD, PENDING, WAITING and FAILED block; DONE, ABSENT and SKIPPED pass.  A
 * newer panel release found while a later step is current therefore makes
 * PANEL current again: its image replaces the images the later steps send.
 *
 * ABSENT becomes WAITING, and blocks, only in a run and only for a target
 * that comes up by itself after a start: the IOmcu without a link
 * (CU_SEC_NO_LINK) and a bound device that is not connected.  A device
 * that is not bound, a bound device whose switch on WIRELESS is OFF (the
 * panel runs no link to it) and a step without an image never wait.
 *
 * The step's state is its own; its tag is the word its row shows.  A step
 * after the current one that is OPEN, HELD or FAILED reads LATER.  While
 * PANEL is current with a newer release, IOMCU and every connected device
 * that runs the carried version read LATER too: the release carries newer
 * images for them.  The C6's image follows the esp_hosted version, not the
 * release, so its row keeps its own tag.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework),
 * no drawing, no router.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_UPD_WIZARD_H
#define RCBENCH_UPD_WIZARD_H

#include <stdbool.h>
#include <stddef.h>

#include "upd_c6.h"
#include "upd_dev.h"
#include "upd_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The steps, in their order. */
typedef enum {
    UPD_WIZ_PANEL = 0, /**< 1: the panel's own image, from GitHub          */
    UPD_WIZ_IOMCU,     /**< 2: the IOmcu, over CAN                          */
    UPD_WIZ_C6,        /**< 3: the board's ESP32-C6 radio                   */
    UPD_WIZ_DIAL,      /**< 4: the M5Stack Dial, over Bluetooth LE          */
    UPD_WIZ_PNODE,     /**< 5: the power node, over Bluetooth LE            */
    UPD_WIZ_BAL,       /**< 6: the balance node, over Bluetooth LE          */
    UPD_WIZ_STEPS,     /**< the count of steps                              */
} upd_wiz_id_t;

/** @brief No step is current: every step is DONE, ABSENT or SKIPPED. */
#define UPD_WIZ_NONE UPD_WIZ_STEPS

/** @brief A step's bit in upd_wiz_in_t.skip. */
#define UPD_WIZ_BIT(id) (1u << (unsigned)(id))

/** @brief Every step's bit. */
#define UPD_WIZ_ALL ((1u << (unsigned)UPD_WIZ_STEPS) - 1u)

/** @brief Bytes of a step's version texts, the terminator included. */
#define UPD_WIZ_VER_MAX (WIFI_VER_MAX + 1)

/** @brief A step's state. */
typedef enum {
    UPD_WIZ_DONE = 0, /**< runs the carried or newest version; PANEL also
                           when nothing newer is known                    */
    UPD_WIZ_OPEN,     /**< another version; the hold is live              */
    UPD_WIZ_HELD,     /**< another version; a gate holds it: see why      */
    UPD_WIZ_RUNNING,  /**< its update, or PANEL's check, runs             */
    UPD_WIZ_PENDING,  /**< a short wait: a build or image check, Bluetooth
                           LE's start, another update's end               */
    UPD_WIZ_ABSENT,   /**< nothing to act on: see the tag                 */
    UPD_WIZ_WAITING,  /**< ABSENT in a run, for a target that comes up by
                           itself                                          */
    UPD_WIZ_FAILED,   /**< OPEN, and its last attempt since the panel's
                           start ended without an install                 */
    UPD_WIZ_SKIPPED,  /**< SKIP THIS STEP was tapped since the panel's
                           start                                           */
} upd_wiz_state_t;

/** @brief The word a step's row shows. */
typedef enum {
    UPD_WIZ_TAG_NOT_CHECKED = 0, /**< PANEL: no check result to go by     */
    UPD_WIZ_TAG_CURRENT,         /**< DONE                                 */
    UPD_WIZ_TAG_NEXT,            /**< OPEN, HELD                           */
    UPD_WIZ_TAG_UPDATING,        /**< RUNNING: an update                   */
    UPD_WIZ_TAG_CHECKING,        /**< PENDING; PANEL's check running       */
    UPD_WIZ_TAG_FAILED,          /**< FAILED; PANEL's failed download      */
    UPD_WIZ_TAG_SKIPPED,         /**< SKIPPED                              */
    UPD_WIZ_TAG_LATER,           /**< after the current step, see the file
                                      comment                              */
    UPD_WIZ_TAG_NO_IMAGE,        /**< this panel carries no image for it   */
    UPD_WIZ_TAG_BAD_IMAGE,       /**< the carried image fails its check    */
    UPD_WIZ_TAG_NO_LINK,         /**< no IOmcu answers                     */
    UPD_WIZ_TAG_CHECK_FAILED,    /**< the IOmcu's build check gave no answer */
    UPD_WIZ_TAG_USB_ONLY,        /**< the IOmcu takes no update over CAN   */
    UPD_WIZ_TAG_LINK_FAILED,     /**< the C6's link failed since the start */
    UPD_WIZ_TAG_NOT_REPORTED,    /**< the C6 named no version              */
    UPD_WIZ_TAG_NOT_CONNECTED,   /**< a bound device is not connected      */
    UPD_WIZ_TAG_NOT_BOUND,       /**< no device of the kind is bound       */
    UPD_WIZ_TAG_OFF,             /**< a bound device is OFF on WIRELESS    */
} upd_wiz_tag_t;

/** @brief The last card's heading, while no step is current. */
typedef enum {
    UPD_WIZ_END_CURRENT = 0, /**< every step runs its newest version: PANEL
                                  checked and CURRENT, each other step
                                  CURRENT or without a bound device       */
    UPD_WIZ_END_NO_CHECK,    /**< PANEL is NOT CHECKED or FAILED; each other
                                  step as for CURRENT                     */
    UPD_WIZ_END_NONE_OPEN,   /**< a step is SKIPPED, or ABSENT with a device
                                  or a target the panel knows of          */
} upd_wiz_end_t;

/** @brief Why a step is HELD or PENDING. */
typedef enum {
    UPD_WIZ_WHY_NONE = 0,
    UPD_WIZ_WHY_ARMED,   /**< ARMED, or an arm waiting                     */
    UPD_WIZ_WHY_SUPPLY,  /**< the supply's output on or coming             */
    UPD_WIZ_WHY_UNSAFE,  /**< the IOmcu's word for either of the two above */
    UPD_WIZ_WHY_RUN,     /**< a log run is open                            */
    UPD_WIZ_WHY_RADIO,   /**< Wi-Fi is on, or a session asks for it        */
    UPD_WIZ_WHY_STOP,    /**< STOP is latched                              */
    UPD_WIZ_WHY_DRAWING, /**< the board drawing is fetched or kept         */
    UPD_WIZ_WHY_INSTALL, /**< a panel download or install runs             */
    UPD_WIZ_WHY_NETWORK, /**< no home network is kept                      */
    UPD_WIZ_WHY_BLE,     /**< Bluetooth LE's start runs                    */
    UPD_WIZ_WHY_BUSY,    /**< another update runs or ends                  */
} upd_wiz_why_t;

/** @brief The current step's one control. */
typedef enum {
    UPD_WIZ_ACT_NONE = 0, /**< no control: a line says what happens       */
    UPD_WIZ_ACT_DOWNLOAD, /**< HOLD 2s TO DOWNLOAD & INSTALL              */
    UPD_WIZ_ACT_IOMCU,    /**< HOLD 2s TO UPDATE THE IOMCU                */
    UPD_WIZ_ACT_C6,       /**< HOLD 2s TO UPDATE THE C6 RADIO             */
    UPD_WIZ_ACT_DIAL,     /**< HOLD 2s TO UPDATE THE DIAL                 */
    UPD_WIZ_ACT_PNODE,    /**< HOLD 2s TO UPDATE THE POWER NODE           */
    UPD_WIZ_ACT_BAL,      /**< HOLD 2s TO UPDATE THE BALANCE NODE         */
} upd_wiz_act_t;

/** @brief What the model is decided from, once a frame. */
typedef struct {
    const upd_view_t     *panel; /**< UPDATES' view; NULL: ABSENT         */
    const upd_cu_view_t  *cu;    /**< the IOmcu section; NULL: ABSENT     */
    const upd_c6_view_t  *c6;    /**< the C6 row; NULL: ABSENT            */
    /** @brief Each device's row, by upd_dev_id_t; NULL: ABSENT. */
    const upd_dev_view_t *dev[UPD_DEV_COUNT];
    bool                  run;   /**< an update run stands after a restart */
    unsigned              skip;  /**< UPD_WIZ_BIT()s of the skipped steps  */
    /** @brief Each step's last attempt since the panel's start that ended
     *         without an install: the band's reason; NULL or empty for
     *         none.  The text is not copied. */
    const char           *result[UPD_WIZ_STEPS];
} upd_wiz_in_t;

/** @brief One step. */
typedef struct {
    upd_wiz_state_t state;  /**< its own state                             */
    upd_wiz_tag_t   tag;    /**< the word its row shows                    */
    upd_wiz_why_t   why;    /**< HELD, PENDING: why; else NONE             */
    bool            older;  /**< the carried image is older than the one
                                 the target runs: the warning colour      */
    const char     *result; /**< the last failed attempt's reason, or NULL */
    char ver[UPD_WIZ_VER_MAX];  /**< the version the target runs, "-" when
                                     not known                             */
    char next[UPD_WIZ_VER_MAX]; /**< the version the step installs: PANEL's
                                     release found, else the carried image's;
                                     empty when none                       */
} upd_wiz_step_t;

/** @brief The model's answer. */
typedef struct {
    upd_wiz_step_t step[UPD_WIZ_STEPS]; /**< each step, by upd_wiz_id_t   */
    unsigned       current;  /**< the current step, or UPD_WIZ_NONE       */
    upd_wiz_act_t  act;      /**< the current step's control              */
    bool           act_live; /**< the control takes a hold; else it is grey
                                  (HELD) or shows a running update's phase */
    bool           can_skip; /**< SKIP THIS STEP stands: the current step
                                  is OPEN, HELD, FAILED or WAITING        */
    bool           cancel;   /**< CANCEL UPDATE stands: the current step is
                                  a device whose transfer runs            */
    bool           show_skipped; /**< no step is current and one is
                                      SKIPPED: SHOW SKIPPED STEPS stands  */
} upd_wiz_t;

/**
 * @brief The six steps, the current one and its control, from the facts.
 * @param in  what the model is decided from; NULL is a zeroed input: six
 *            ABSENT steps
 * @param out the answer; NULL does nothing
 */
void upd_wizard_eval(const upd_wiz_in_t *in, upd_wiz_t *out);

/**
 * @brief Whether a state blocks the steps after it.
 * @param s the state
 * @return true for OPEN, HELD, RUNNING, PENDING, WAITING and FAILED
 */
bool upd_wiz_blocks(upd_wiz_state_t s);

/**
 * @brief The last card's heading from the steps.  A step counts as current
 *        when it is DONE with the tag CURRENT, or ABSENT with NOT BOUND:
 *        no device of the kind exists.
 * @param w the model's answer; NULL is UPD_WIZ_END_NONE_OPEN
 * @return which heading holds; meaningful while w->current is UPD_WIZ_NONE
 */
upd_wiz_end_t upd_wiz_end(const upd_wiz_t *w);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_UPD_WIZARD_H */
