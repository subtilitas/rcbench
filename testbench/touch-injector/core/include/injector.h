/*
 * The touch injector: a GT911 emulator at 0x14 that presses where a USB
 * (Universal Serial Bus) command line tells it to.  Pure C; the RP2350
 * build (../rp2350) wires it to the I2C (Inter-Integrated Circuit) target,
 * the USB serial port and the status LED, and the host suite drives it as
 * a panel and a host would.
 *
 * States:
 *
 *   LISTEN  after every reset.  GP4 and GP5 are inputs and nothing answers
 *           on the bus.  `wiring` asks the glue to listen to the bus for
 *           500 ms and report which line carries SCL; `confirm scl=GP5`
 *           after an `ok` verdict asks it to attach the I2C target.
 *   ACTIVE  the target answers at 0x14 and gestures run.
 *
 * Commands (one line each, at most INJ_LINE_MAX characters, LF or CRLF):
 *
 *   ping                       pong rcbench-touch 1 addr=0x14 res=1024x600
 *                              state=listen|active
 *   wiring                     ok wiring, then the verdict line
 *   confirm scl=GP5            ok confirm
 *   status                     status <state> reads= acks= last_read_ms=
 *                              down= x= y=
 *   tap x y [ms]               ok tap, then done tap frames= ms=
 *   hold x y ms                ok hold, then done hold frames= ms=
 *   swipe x0 y0 x1 y1 ms       ok swipe, then done swipe frames= ms= slowed=
 *   release                    ok release (a gesture under way first gets
 *                              fail <gesture> released)
 *
 * Coordinates are panel pixels, x 0..INJ_RES_X-1 and y 0..INJ_RES_Y-1;
 * ms is 1..INJ_MS_MAX.  Errors are one line, err <code> [detail], and the
 * command is not run.  A gesture that fails after its ok gets fail
 * <gesture> no_master, panel_reset or released.
 *
 * Concurrency: the byte functions of the emulator (gt911_emu.h) run in the
 * I2C interrupt.  injector_input() and injector_poll() read and change the
 * same state, so the glue calls them with that interrupt masked.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gt911_emu.h"
#include "inj_line_id.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INJ_PROTOCOL        1
#define INJ_RES_X           1024u
#define INJ_RES_Y           600u
#define INJ_LINE_MAX        63u    /* characters, without the LF         */
#define INJ_REPORT_MS       10u    /* least time between two frames      */
#define INJ_ACK_TIMEOUT_MS  100u   /* a frame not acknowledged by then   */
#define INJ_TAP_MS          100u   /* a tap's default length             */
#define INJ_MS_MAX          30000u /* the longest gesture                */
#define INJ_SWIPE_STEP_PX   100u   /* |dx|+|dy| between two swipe frames */
#define INJ_LIVE_MS         100u   /* a read this recent: panel polling  */
#define INJ_REPLY_LEN       128u
#define INJ_REPLIES         8u

typedef enum {
    INJ_LISTEN = 0,
    INJ_ACTIVE,
} inj_state_t;

typedef enum {
    INJ_G_NONE = 0,
    INJ_G_TAP,
    INJ_G_HOLD,
    INJ_G_SWIPE,
    INJ_G_RELEASE,   /* the release frame after `release` or a closed port */
} inj_gesture_t;

typedef enum {
    INJ_LED_LISTEN = 0,  /* white, blinking 1 Hz                         */
    INJ_LED_WIRING_BAD,  /* yellow: swapped, low or noise                */
    INJ_LED_OFF,         /* active, no read for INJ_LIVE_MS             */
    INJ_LED_POLLED,      /* dim green: the panel reads 0x14              */
    INJ_LED_DOWN,        /* blue: a contact is down                      */
    INJ_LED_FAILED,      /* red: the last gesture failed                 */
} inj_led_t;

typedef struct {
    inj_state_t state;
    gt911_emu_t emu;

    /* The line being typed. */
    char    line[INJ_LINE_MAX + 1u];
    uint8_t len;
    bool    overflow;

    /* Wiring. */
    inj_line_result_t wiring;
    bool wiring_running;
    bool wiring_wanted;
    bool attach_wanted;

    /* The gesture under way. */
    inj_gesture_t kind;
    bool     releasing;      /* the release frame is published          */
    uint16_t x0, y0, x1, y1;
    uint32_t ms;
    bool     pending;        /* a published frame awaits its ack        */
    bool     published;      /* a frame of this gesture was published   */
    uint32_t t_pub;
    bool     down_published;
    bool     first_acked;
    uint32_t t_first;
    uint32_t frames;
    uint16_t px, py;         /* the last point published                */
    bool     slowed;
    bool     end_published;  /* swipe: the frame at (x1, y1)            */
    bool     end_acked;
    bool     withdraw_due;
    bool     failed;

    /* What the emulator counted, as last seen. */
    uint32_t seen_reads;
    uint32_t seen_acks;
    uint32_t seen_product;
    bool     any_read;
    uint32_t t_last_read;

    /* Replies not yet sent. */
    char    replies[INJ_REPLIES][INJ_REPLY_LEN];
    uint8_t r_head;
    uint8_t r_count;
} injector_t;

void injector_init(injector_t *in);

/** Bytes from the host.  A full line runs at once. */
void injector_input(injector_t *in, const char *bytes, size_t n,
                    uint32_t now_ms);

/** Move the gesture on: frames, acknowledges, timeouts.  Call often. */
void injector_poll(injector_t *in, uint32_t now_ms);

/** True once after `wiring`: the glue starts listening to the lines. */
bool injector_take_wiring(injector_t *in);

/** The glue's 500 ms of listening, classified. */
void injector_wiring_done(injector_t *in, const inj_line_result_t *r);

/** True once after `confirm`: the glue attaches the I2C target. */
bool injector_take_attach(injector_t *in);

/**
 * The host closed the port (DTR, data terminal ready, fell).  A contact
 * that is down is released; nothing is replied.
 */
void injector_host_closed(injector_t *in);

/** The oldest reply into @p out; false when there is none. */
bool injector_take_reply(injector_t *in, char *out, size_t n);

/** What the status LED shows at @p now_ms. */
inj_led_t injector_led(const injector_t *in, uint32_t now_ms);

/** The gesture's word: tap, hold, swipe, release, or idle. */
const char *inj_gesture_name(inj_gesture_t g);

#ifdef __cplusplus
}
#endif
