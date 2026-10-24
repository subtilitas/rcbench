/*
 * Writing a new panel application into the slot that is not running.
 *
 * Takes an image already in memory, a pointer and a length, and knows
 * nothing about where it came from (fw_upload.h brings it there).  The whole
 * image is checked before anything else happens (fw_image_check()): a file
 * cut short, a bad SHA-256, another chip's or another project's image, or
 * one larger than the slot touches no flash.
 *
 * Then, a step at a time so the caller can draw between two: open the slot,
 * write it in pieces of FW_WRITE_CHUNK, each erasing the 64kB it lands in
 * first, and finish -- the platform's own check of what is in flash and the
 * switch of the boot slot.  The running slot is never written, so a power
 * cut or a failed step at any point leaves the panel starting as before.
 *
 * Every flash operation stalls both of the panel's cores (flash_gate.h).  So
 * no step touches flash unless the caller says the bench is quiet: SAFE, the
 * supply off, no run open, no drawing being fetched or kept (fw_gate()), and
 * the control task stopped on purpose.
 *
 * The flash is passed in, so the whole sequence runs on the host against
 * memory.  Pure C, no ESP-IDF (Espressif Internet-of-Things Development
 * Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_INSTALL_H
#define RCBENCH_FW_INSTALL_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes per write step: one 64kB block, erased as the write reaches it. */
#define FW_WRITE_CHUNK (64u * 1024u)

/* ---------------------------------------------------------------- the gate */

/** Whether the bench lets the panel write its own flash for an update. */
typedef enum {
    FW_GATE_OPEN = 0,
    FW_GATE_ARMED,    /**< ARMED, or a switch to ARMED waiting            */
    FW_GATE_SUPPLY,   /**< the supply's output is on or an ON is queued   */
    FW_GATE_RUN,      /**< a run's log, or a servo test's, is still open  */
    FW_GATE_DRAWING,  /**< the board's drawing is being fetched or kept   */
    FW_GATE_COPROC,   /**< the IOmcu's update runs, or its BUY      */
} fw_gate_t;

/**
 * flash_gate_open()'s rule, and three more: no file the logger writes is
 * open, the board drawing's transfer and keep are not running, and no
 * update of the IOmcu runs (@p coproc).  The first reason that holds,
 * in the order above.
 */
fw_gate_t fw_gate(bool armed, bool supply, bool run_open, bool drawing,
                  bool coproc);

/**
 * The other direction: whether the IOmcu's update may start.  Only
 * with the panel's radio wholly off (@p radio_down) and no install of a
 * panel image under way (@p installing), from the moment INSTALL's hold
 * completes until the write has failed.
 */
bool fw_coproc_may_start(bool radio_down, bool installing);

/* ----------------------------------------------------------- the installer */

/** The slot that is not running, as the installer needs it. */
typedef struct {
    /** Open the slot for an image of @p bytes; erases nothing yet. */
    bool (*begin)(void *ctx, uint32_t bytes);
    /** Write @p len bytes at @p off, erasing what they land in first. */
    bool (*write)(void *ctx, uint32_t off, const uint8_t *src, uint32_t len);
    /** Check what is in the slot and make it the one that starts next. */
    bool (*finish)(void *ctx);
    /** Give up after a failed write or finish; the boot slot stays. */
    void (*abort)(void *ctx);
    void *ctx;
} fw_flash_t;

typedef enum {
    FW_INSTALL_IDLE = 0,  /**< nothing to install                         */
    FW_INSTALL_WAITING,   /**< not quiet: no flash touched in this step   */
    FW_INSTALL_RUNNING,   /**< one operation done, more to come           */
    FW_INSTALL_DONE,      /**< the slot starts at the next restart        */
    FW_INSTALL_FAILED,    /**< see fw_install_t.err; the boot slot stays  */
} fw_install_status_t;

typedef struct {
    const fw_flash_t *f;
    const uint8_t    *img;
    uint32_t          bytes;
    uint32_t          done;     /**< bytes written                    */
    bool              opened;   /**< begin() succeeded                 */
    bool              touched;  /**< any flash operation was attempted */
    bool              busy;
    fw_err_t          err;
    fw_offer_t        offer;
    fw_image_info_t   info;
} fw_install_t;

/**
 * Check @p bytes of image at @p img for a slot of @p slot bytes and against
 * @p running.  FW_OK with the installer busy, or why not: anything
 * fw_image_check() refuses, FW_ERR_RUNNING for the build that is running.
 * Touches no flash.  The image must stay unchanged until DONE or FAILED.
 */
fw_err_t fw_install_begin(fw_install_t *in, const fw_flash_t *f,
                          const uint8_t *img, uint32_t bytes, uint32_t slot,
                          const fw_image_info_t *running);

/**
 * One step when @p quiet: open, one write of FW_WRITE_CHUNK at most, or the
 * finish.  Not quiet: WAITING, and no flash is touched.  DONE and FAILED
 * are returned once; the installer is idle after them.
 */
fw_install_status_t fw_install_step(fw_install_t *in, bool quiet);

/** Whether every byte is written and only the finish is left. */
bool fw_install_finishing(const fw_install_t *in);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_INSTALL_H */
