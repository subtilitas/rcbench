/*
 * S.BUS and S.BUS2 output: frames at a fixed period on a driver, and for
 * S.BUS2 the telemetry slots read after each frame.
 *
 * Implements the S.BUS and S.BUS2 protocols of Futaba.  Futaba, S.BUS and
 * S.BUS2 are names or trademarks of their respective owners; they are used
 * here only to say which servos and sensors this module works with (NOTICE,
 * "Trademarks").
 *
 * A driver of 3 functions puts one frame on the wire per call and, for
 * S.BUS2, listens for SBUS2_WINDOW_US after it.  sbus_out_step() starts a
 * frame each period, rotates the S.BUS2 footer through the 4 slot groups,
 * and takes every finished window into the slot table.  It never waits.
 *
 * Frames go out from sbus_out_start() to sbus_out_stop().  A caller that
 * drives servos starts the output only while the bench is ARMED and stops it
 * on STOP and SAFE; a frame on the wire at the stop ends, and no frame
 * follows.
 *
 * No heap, no floating point, no static mutable state.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_SBUS_OUT_H
#define RCBENCH_SBUS_OUT_H

#include <stdbool.h>
#include <stdint.h>

#include "sbus.h"
#include "sbus2.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest period sbus_out_config() takes. */
#define SBUS_PERIOD_MAX_US  50000u
/**
 * Shortest S.BUS2 period: 0.2 ms of lead, 3.0 ms of frame and
 * SBUS2_WINDOW_US of listening are 11.2 ms; 0.8 ms more for the caller's
 * step interval.
 */
#define SBUS2_PERIOD_MIN_US 12000u

typedef struct {
    void *ctx;
    /**
     * Send the SBUS_FRAME_BYTES bytes at @p frame, then listen for
     * @p window_us after the frame's last stop bit; 0 listens not at all.
     * Returns at once, false when a transaction runs or the line is not
     * open.
     */
    bool (*start)(void *ctx, const uint8_t *frame, uint32_t window_us);
    /** False while the transaction runs; true once after it ended, with the
     *  bytes of the window in @p out (n = 0 without a window). */
    bool (*poll)(void *ctx, sbus2_capture_t *out);
    /** A free-running microsecond clock; it may wrap. */
    uint32_t (*now_us)(void *ctx);
} sbus_driver_t;

typedef enum {
    SBUS_MODE_SBUS = 0,   /**< footer 0x00, no listening */
    SBUS_MODE_SBUS2,      /**< footers 0x04, 0x14, 0x24, 0x34 and the slots */
} sbus_mode_t;

typedef enum {
    SBUS_OUT_OK = 0,
    SBUS_OUT_ERR_ARG,      /**< NULL, or a mode that does not exist */
    SBUS_OUT_ERR_PERIOD,   /**< outside the range the mode takes */
} sbus_out_err_t;

typedef struct {
    sbus_driver_t drv;
    /** What every frame carries: the channels, ch17, ch18 and the flags. */
    sbus_frame_t  frame;
    uint32_t period_us;
    uint8_t  mode;          /**< sbus_mode_t */
    uint8_t  group;         /**< the slot group of the next S.BUS2 frame */
    uint8_t  busy_footer;   /**< the footer of the frame in flight */
    bool     running;
    bool     busy;
    uint32_t due_us;        /**< the next frame's start */
    sbus2_timing_t  timing;
    sbus2_capture_t cap;
    sbus2_window_t  window;   /**< the last window read */
    sbus2_table_t   table;
    /* Counters. */
    uint32_t frames;       /**< frames the driver took */
    uint32_t refused;      /**< frames the driver refused */
    uint32_t late;         /**< frames started a whole period late; the
                                schedule restarts from that frame */
} sbus_out_t;

/** Stopped, S.BUS at SBUS_PERIOD_US, every channel at 1500 us (992), the
 *  flags 0, the table empty.  @p drv is copied. */
void sbus_out_init(sbus_out_t *o, const sbus_driver_t *drv);

/** Mode and period, from the next frame on.  S.BUS takes
 *  SBUS_PERIOD_FAST_US to SBUS_PERIOD_MAX_US, S.BUS2 SBUS2_PERIOD_MIN_US to
 *  SBUS_PERIOD_MAX_US.  On an error nothing changes. */
sbus_out_err_t sbus_out_config(sbus_out_t *o, unsigned mode,
                               uint32_t period_us);

/** Channel @p ch (0 to 15) to @p raw, from the next frame on; a value above
 *  2047 is sent as 2047.  False for a channel above 15. */
bool sbus_out_set_raw(sbus_out_t *o, unsigned ch, uint16_t raw);

/** Channel @p ch to a pulse of @p us microseconds through sbus_from_us(). */
bool sbus_out_set_us(sbus_out_t *o, unsigned ch, uint16_t us);

/** The flag byte from the next frame on. */
void sbus_out_set_flags(sbus_out_t *o, bool ch17, bool ch18, bool frame_lost,
                        bool failsafe);

/** The first frame goes out at the next step, S.BUS2 with group 0. */
void sbus_out_start(sbus_out_t *o);

/** No frame after this one.  A transaction in flight ends; its window is
 *  still read. */
void sbus_out_stop(sbus_out_t *o);

/** Clear the slot table and the last window. */
void sbus_out_clear_slots(sbus_out_t *o);

/** Poll the transaction in flight and start the next frame when it is due.
 *  Call it at least once per millisecond; a frame starts up to one call
 *  interval after its due time, and the schedule does not drift. */
void sbus_out_step(sbus_out_t *o);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_SBUS_OUT_H */
