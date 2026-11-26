/**
 * @file
 * @brief The label printer on the panel: the Bluetooth LE central of
 *        protocols/bleuart with the Supvan profile (print_profile), the job
 *        of protocols/supvan, the label's layout and its encoding in steps,
 *        and the view the screens read.  Pure C: the panel's `blec` task
 *        (firmware/panel/main/panel_print.c) hands in the host stack's
 *        events, performs the actions and gives the heap block.
 *
 * Two parts with two owners:
 *
 * - The controller (print_ctl_t, print_ctl_*() with a print_ctl_t
 *   argument), the `blec` task's.
 * - The requests and the view (print_ctl_label(), print_ctl_test(),
 *   print_ctl_query(), print_ctl_abort(), print_ctl_view()), the frame
 *   task's.  The glue moves a request to the controller
 *   (print_ctl_take()) and the controller's view back (print_ctl_show()).
 *
 * The rules it holds:
 *
 * - The printer is bound by its address (SETUP's `prn_ble`); the link is
 *   made for a request and ends PRINT_LINGER_MS after the printer's last
 *   answer.  A request inside that time uses the link as it is.
 * - The connect is hurried (bleu_hurry()).  A link not up PRINT_FIND_MS
 *   after the request ends it: PRINTER NOT FOUND, BLUETOOTH NOT READY or
 *   NOT A LABEL PRINTER.
 * - A link with an ATT MTU (attribute protocol maximum transmission unit)
 *   under PRINT_MTU_MIN takes no job: a data write is 128 bytes and the
 *   3-byte ATT header.
 * - A label is a QUERY job (the roll and its counter), then the layout for
 *   that roll and one print buffer filled and encoded each
 *   print_ctl_poll(), then the PRINT job.  The heap block holds the
 *   bitmap, one buffer, the encoder and the stream; it is asked for once
 *   the roll is known and handed back at the job's end.
 * - A request while a job runs is refused and changes nothing.  A label or
 *   a test label while the bench is ARMED, or any request while a device
 *   update runs, ends at once with its error.
 * - An abort ends the job: STOP_PRINT once after START (supvan.h), PRINT
 *   STOPPED.
 * - A device update runs on the same spare link: the controller ends a
 *   lingering link while an update runs (print_ctl_gate()), and
 *   print_ctl_busy() tells the update a job or a scan runs.
 * - A scan (PRINT_REQ_SCAN) lists printers for SETUP > WIRELESS > PRINTER:
 *   devices whose public address starts with A4:93:40, as the printer
 *   advertises no service, and no device for a service it advertises, BLEU_SCAN_MS long.  It is refused while a job
 *   or a scan runs, while the bench is ARMED and while a device update
 *   runs; it waits up to PRINT_FIND_MS for Bluetooth to be ready.  A
 *   switch to ARMED stops it (scan_cut); a label, test label or query
 *   request stops it and goes on.  A link that lingers ends before the
 *   scan starts: a linked printer does not advertise.
 *
 * Every function of the controller is called from one task.  Times are
 * unsigned differences of a free-running 32-bit ms clock.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bleuart.h"
#include "imu_ble.h"
#include "label_layout.h"
#include "lzlit.h"
#include "servo_id.h"
#include "supvan.h"
#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The printer's owner on the panel's hub (bleuart_hub.h). */
#define PRINT_BLE_OWNER 5u
/** @brief From a request to a link that is up, at most, ms. */
#define PRINT_FIND_MS   10000u
/** @brief A link with no job ends this long after the last answer, ms. */
#define PRINT_LINGER_MS 30000u
/** @brief The least ATT MTU a job takes: 128 data bytes and 3. */
#define PRINT_MTU_MIN   131u
/** @brief Bytes of a DUT NAME with its NUL. */
#define PRINT_DUT_SIZE  24u
/** @brief Bytes of the printer's name or address text with its NUL. */
#define PRINT_NAME_SIZE 24u
/** @brief Bytes of the firmware version text with its NUL. */
#define PRINT_VERSION_SIZE 16u
/** @brief Printers a scan lists. */
#define PRINT_FOUND_MAX BLEU_FOUND_MAX

/** @brief The Supvan profile: the vendor service
 *         0000e0ff-3c17-d293-8e48-14fe2e4da212, FFE1 the STREAM, FFE9 the
 *         CMD, FFEA named only, each matched in 16 or 128 bits; a scan by
 *         the address prefix A4:93:40, as the printer advertises no
 *         service; the CCCD after FFE1's value, never read back. */
extern const bleu_profile_t print_profile;

/** @brief Where a request stands. */
typedef enum {
    PRINT_IDLE = 0,   /**< none since the start */
    PRINT_CONNECTING, /**< the link is being made */
    PRINT_READING,    /**< the QUERY job reads the roll and the counter */
    PRINT_BUILDING,   /**< the layout and the stream */
    PRINT_SENDING,    /**< the PRINT job up to BUF_FULL */
    PRINT_PRINTING,   /**< the printer prints */
    PRINT_CHECKING,   /**< the counter's verdict */
    PRINT_DONE,       /**< printed, or the roll read */
    PRINT_FAILED      /**< ended with err */
} print_phase_t;

/** @brief Why a request ended without a label. */
typedef enum {
    PRINT_OK = 0,           /**< none */
    PRINT_ERR_NO_PRINTER,   /**< no address bound */
    PRINT_ERR_NOT_FOUND,    /**< no link within PRINT_FIND_MS */
    PRINT_ERR_RADIO,        /**< Bluetooth not synced */
    PRINT_ERR_NOT_PRINTER,  /**< the device lacks the Supvan service */
    PRINT_ERR_MTU,          /**< the MTU is under PRINT_MTU_MIN */
    PRINT_ERR_COVER,        /**< cover open */
    PRINT_ERR_NO_LABEL,     /**< no label, or a zeroed record */
    PRINT_ERR_LABEL,        /**< label read or mode error */
    PRINT_ERR_BATTERY,      /**< printer battery low */
    PRINT_ERR_HOT,          /**< print head hot */
    PRINT_ERR_BUSY,         /**< busy for 5s before START */
    PRINT_ERR_SILENT,       /**< a step's answer missed */
    PRINT_ERR_WRITE,        /**< the link refused a write */
    PRINT_ERR_LINK,         /**< the link dropped and did not come back,
                                 or dropped again before the counter was
                                 read: no verdict */
    PRINT_ERR_LINK_NOT_PRINTED, /**< the link dropped, came back, and the
                                     counter read 3 times stayed */
    PRINT_ERR_NOT_PRINTED,  /**< the counter stayed for 3 reads */
    PRINT_ERR_ROLL,         /**< the roll changed during the job */
    PRINT_ERR_SMALL,        /**< the roll is under 15 x 20mm */
    PRINT_ERR_LARGE,        /**< the roll is over 48 x 100mm */
    PRINT_ERR_ABORTED,      /**< print_ctl_abort() */
    PRINT_ERR_UPDATE,       /**< a device update runs */
    PRINT_ERR_MEMORY,       /**< the heap block was refused */
    PRINT_ERR_ARMED,        /**< the bench is ARMED */
    PRINT_ERR_COUNT         /**< number of values, not a value */
} print_err_t;

/** @brief What a request asks. */
typedef enum {
    PRINT_REQ_NONE = 0, /**< none */
    PRINT_REQ_LABEL,    /**< a servo label: id and DUT NAME */
    PRINT_REQ_TEST,     /**< the test label */
    PRINT_REQ_QUERY,    /**< the roll and the counter only */
    PRINT_REQ_ABORT,    /**< end the job that runs */
    PRINT_REQ_SCAN      /**< list the printers in range */
} print_req_kind_t;

/** @brief A request. */
typedef struct {
    uint8_t kind;                 /**< print_req_kind_t */
    char    id[SERVO_ID_SIZE];    /**< PRINT_REQ_LABEL: the servo id */
    char    dut[PRINT_DUT_SIZE];  /**< PRINT_REQ_LABEL: DUT NAME, "" none */
} print_req_t;

/** @brief A printer a scan found. */
typedef struct {
    char   addr[BLEU_ADDR_TEXT_MAX]; /**< its address with its type */
    char   name[BLEU_NAME_MAX];      /**< its name, "" when none came */
    int8_t rssi;                     /**< dBm of its last report, 127
                                          none */
} print_dev_t;

/** @brief What the screens read. */
typedef struct {
    uint8_t  phase;    /**< print_phase_t */
    uint8_t  err;      /**< print_err_t at PRINT_FAILED */
    uint8_t  kind;     /**< print_req_kind_t of the last request */
    uint8_t  frame;    /**< data frames sent */
    uint8_t  frames;   /**< data frames of the stream */
    bool     bound;    /**< an address is bound */
    bool     linked;   /**< the link is up */
    bool     relinked; /**< the verdict came after a link drop */
    char     printer[PRINT_NAME_SIZE]; /**< the printer's name, else its
                                            address, "" none bound */
    uint8_t  w_mm;     /**< the roll's width, 0 unknown */
    uint8_t  h_mm;     /**< its height along the feed, 0 unknown */
    uint8_t  gap_mm;   /**< its gap */
    int32_t  left;     /**< labels left, -1 unknown */
    uint16_t mtu;      /**< the link's MTU, 0 none */
    char     id[SERVO_ID_SIZE]; /**< the label in work or printed last */
    char     addr[BLEU_ADDR_TEXT_MAX]; /**< the bound address, "" none */
    char     name[PRINT_NAME_SIZE];    /**< the bound name, "" none */
    bool     update;      /**< a device update runs or was asked for */
    bool     scanning;    /**< a scan is asked or runs */
    bool     scan_done;   /**< a scan ended since the start: found is its */
    bool     scan_cut;    /**< that scan was stopped by a switch to ARMED */
    uint8_t  scan_left_s; /**< the running scan's time left, s, rounded
                               up */
    uint8_t  found_n;     /**< the printers it found */
    print_dev_t found[PRINT_FOUND_MAX]; /**< strongest first */
    uint16_t scan_refused; /**< scans refused since the start */
    uint16_t jobs;     /**< requests taken since the start: a new one
                            with the same result reads as new */
    uint32_t rev;      /**< counts every change */
} print_view_t;

/** @brief The controller.  Its members are private but for the tests. */
typedef struct {
    bleu_t       b;          /**< the central */
    sv_job_t     job;        /**< the job */
    imu_radio_t  radio;      /**< the panel's Bluetooth */
    bool         armed;      /**< the bench is ARMED or an arm waits */
    bool         update;     /**< a device update runs */
    bool         bound;      /**< an address is bound */
    char         addr[BLEU_ADDR_TEXT_MAX]; /**< its text */
    char         name[PRINT_NAME_SIZE];    /**< its name, "" none */
    char         version[PRINT_VERSION_SIZE]; /**< for the test label */
    print_req_t  req;        /**< the request in work */
    uint8_t      phase;      /**< print_phase_t */
    uint8_t      err;        /**< print_err_t */
    bool         on;         /**< the central is asked to link */
    bool         was_up;     /**< the link was up at the last look */
    bool         wr_out;     /**< a job write is with the central */
    bool         job_on;     /**< the job runs */
    uint16_t     seen_frame; /**< the job's frame at the last poll */
    uint32_t     t_req;      /**< the request came */
    uint32_t     t_quiet;    /**< the printer's last answer */
    uint8_t      frag[SV_FRAGMENT]; /**< the data write's bytes */
    sv_material_t roll;      /**< the roll as read */
    bool         roll_known; /**< roll holds a record */
    int32_t      left;       /**< labels left, -1 unknown */
    bool         relinked;   /**< the last verdict came after a drop */
    bool         scan_due;   /**< a scan waits for Bluetooth */
    bool         scan_on;    /**< the central scans */
    bool         scan_done;  /**< a scan ended since the start */
    bool         scan_cut;   /**< the bench ARMED stopped it */
    uint16_t     scan_refused; /**< scans refused */
    uint16_t     jobs;       /**< requests taken */
    uint32_t     t_scan;     /**< the scan was asked */
    uint32_t     t_now;      /**< the clock at the last poll */
    /* the heap block and its parts */
    uint8_t     *blk;        /**< the block, NULL none */
    size_t       blk_want;   /**< bytes asked for, 0 none */
    uint8_t     *blk_free;   /**< a block to hand back, NULL none */
    lzlit_t     *z;          /**< the encoder */
    label_info_t *info;      /**< the layout's result */
    label_bmp_t  bmp;        /**< the bitmap */
    uint8_t     *buf;        /**< one print buffer */
    uint8_t     *stream;     /**< the stream */
    size_t       cap;        /**< its room */
    uint16_t     bufs;       /**< print buffers of the label */
    uint16_t     next;       /**< the buffer to fill next; bufs + 1 before
                                  the layout */
    uint32_t     zlen;       /**< the stream's length */
    uint32_t     rev;        /**< counts every change of the view */
    uint32_t     builds;     /**< labels built */
    uint32_t     t_build;    /**< the last build's start */
    uint32_t     build_ms;   /**< the last build's time */
} print_ctl_t;

/**
 * @brief Clear: nothing bound, Bluetooth off, no job.
 * @param c       The controller; NULL does nothing.
 * @param version The firmware version for the test label, "0.23.17".
 */
void print_ctl_init(print_ctl_t *c, const char *version);

/**
 * @brief Bind the printer by its address text, or unbind with "".  A job
 *        that runs keeps its link and ends first; the new address is taken
 *        at the next request.  A bind of the bound address, in any letter
 *        case, keeps the roll and the labels left.  Another address
 *        clears them at once, also while a job runs, whose end leaves
 *        them clear; with no job in work it also ends the link and sets
 *        the phase back to PRINT_IDLE with no request kind.
 * @param c      The controller.
 * @param addr   `A4:93:40:30:FA:71 P`; "", a random address or text
 *               bleu_addr_parse() refuses unbinds.
 * @param name   The name the scan gave, "" none.
 * @param now_ms The clock.
 */
void print_ctl_bind(print_ctl_t *c, const char *addr, const char *name,
                    uint32_t now_ms);

/**
 * @brief The panel's Bluetooth state.
 * @param c      The controller.
 * @param radio  The state.
 * @param now_ms The clock.
 */
void print_ctl_radio(print_ctl_t *c, imu_radio_t radio, uint32_t now_ms);

/**
 * @brief The bench and the device update: a label is refused while ARMED,
 *        every request while an update runs, and a link with no job ends
 *        while an update runs.
 * @param c      The controller.
 * @param armed  ARMED, or an arm waits.
 * @param update A device update runs or was asked for.
 * @param now_ms The clock.
 */
void print_ctl_gate(print_ctl_t *c, bool armed, bool update,
                    uint32_t now_ms);

/**
 * @brief Take a request.
 * @param c      The controller.
 * @param r      The request; PRINT_REQ_ABORT ends the job that runs.
 * @param now_ms The clock.
 * @return false, and nothing changes, while a job runs (an abort aside) or
 *         for a label whose id servo_id_parse() refuses; false for a scan
 *         refused (scan_refused counts it); true otherwise, a refusal by
 *         the gate included (PRINT_FAILED with its error).
 */
bool print_ctl_request(print_ctl_t *c, const print_req_t *r,
                       uint32_t now_ms);

/**
 * @brief Take one event from the host stack.
 * @param c      The controller.
 * @param e      The event, its token the controller's.
 * @param now_ms The clock.
 */
void print_ctl_event(print_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms);

/**
 * @brief The next action: the job's write when one is due, then the
 *        central's.
 * @param c      The controller.
 * @param now_ms The clock.
 * @param out    Receives the action.
 * @return true when @p out holds one.
 */
bool print_ctl_step(print_ctl_t *c, uint32_t now_ms, bleu_act_t *out);

/**
 * @brief The bytes of the controller's long write (BLEU_ACT_WRITE_EXT).
 * @param c The controller.
 * @param a The action; a->h2 bytes are asked for.
 * @return The data write's bytes, or NULL for another length.
 */
const uint8_t *print_ctl_ext(const print_ctl_t *c, const bleu_act_t *a);

/**
 * @brief Advance: the request's deadline, the build's next step, the job's
 *        timeouts, the linger.  Call every poll.
 * @param c      The controller.
 * @param now_ms The clock.
 */
void print_ctl_poll(print_ctl_t *c, uint32_t now_ms);

/**
 * @brief The heap block the build waits for.
 * @param c The controller.
 * @return Its bytes, 0 when none is asked for.
 */
size_t print_ctl_block_want(const print_ctl_t *c);

/**
 * @brief Give the block print_ctl_block_want() asked for.
 * @param c      The controller.
 * @param blk    The block, at least that many bytes, 8-byte aligned; NULL
 *               ends the request with PRINT_ERR_MEMORY.
 * @param now_ms The clock.
 */
void print_ctl_block_give(print_ctl_t *c, void *blk, uint32_t now_ms);

/**
 * @brief Take back a block the controller is done with.
 * @param c The controller.
 * @return The block to free, NULL none.
 */
void *print_ctl_block_back(print_ctl_t *c);

/**
 * @brief Whether a request or a scan runs: the device update's start is
 *        refused.
 * @param c The controller.
 * @return true from the request's start to its end, and while a scan is
 *         asked or runs.
 */
bool print_ctl_busy(const print_ctl_t *c);

/**
 * @brief The view.
 * @param c   The controller.
 * @param out Receives it.
 */
void print_ctl_view_of(const print_ctl_t *c, print_view_t *out);

/**
 * @brief The text of an error, TX_PRINT_ERR_*.
 * @param err print_err_t.
 * @return The text's id; TX_PRINT_ERR_SILENT for a value out of range.
 */
ui_text_id_t print_err_text(uint8_t err);

/**
 * @brief Whether a view shows a request in work.
 * @param v The view.
 * @return true from PRINT_CONNECTING to PRINT_CHECKING.
 */
bool print_view_busy(const print_view_t *v);

/* The frame task's side. */

/**
 * @brief Ask for a servo label.
 * @param id  The servo id, servo_id_parse()'s form.
 * @param dut DUT NAME, "" or NULL none.
 * @return false while a request waits or the view shows one in work, or
 *         for an id servo_id_parse() refuses.
 */
bool print_ctl_label(const char *id, const char *dut);

/**
 * @brief Ask for the test label.
 * @return false while a request waits or one is in work.
 */
bool print_ctl_test(void);

/** @brief Ask for the roll and the counter; nothing while a request waits
 *         or one is in work. */
void print_ctl_query(void);

/** @brief End the request in work; it wins over a request that waits. */
void print_ctl_abort(void);

/**
 * @brief Ask for a scan for printers.
 * @return false while a request waits or the view shows a job in work or
 *         a scan.
 */
bool print_ctl_scan(void);

/**
 * @brief Take the request that waits, for the controller's task.
 * @param out Receives it.
 * @return true when @p out holds one.
 */
bool print_ctl_take(print_req_t *out);

/**
 * @brief Keep the controller's last published view for the frame task.
 * @param v The view.
 */
void print_ctl_show(const print_view_t *v);

/**
 * @brief The view print_ctl_show() kept.
 * @return It; all zero but left -1 before the first.
 */
const print_view_t *print_ctl_view(void);

/** @brief Forget the waiting request and the kept view (the tests). */
void print_ctl_reset(void);

#ifdef __cplusplus
}
#endif
