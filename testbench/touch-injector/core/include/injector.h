/**
 * @file
 * @brief The touch injector: a GT911 emulator at 0x14 that presses where a USB
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
 *   status                     status \<state\> reads= acks= last_read_ms=
 *                              down= x= y=  (contact 0)
 *   contacts                   contacts n= \<id\>=\<gesture\>,\<down\>,\<x\>,\<y\>
 *                              for each contact under way
 *   tap x y [ms] [id=N]        ok tap, then done tap frames= ms=
 *   hold x y ms [id=N]         ok hold, then done hold frames= ms=
 *   swipe x0 y0 x1 y1 ms [id=N]
 *                              ok swipe, then done swipe frames= ms= slowed=
 *   release [id=N]             ok release (each gesture under way first gets
 *                              fail \<gesture\> released)
 *
 * Contacts: up to INJ_CONTACTS gestures run at once, one per contact id
 * 0..INJ_ID_MAX, each a GT911 track ID.  A command without id= drives
 * contact 0, and its replies are the ones of protocol 1.  A gesture started
 * with id=N names the contact in every reply about it: ok tap id=N,
 * done tap id=N frames= ms=, fail tap id=N released.  release without id=
 * releases every contact.  A frame carries every contact that is down; a
 * contact's frames= counts the frames that put it down, moved it or lifted
 * it.
 *
 * Coordinates are panel pixels, x 0..INJ_RES_X-1 and y 0..INJ_RES_Y-1;
 * ms is 1..INJ_MS_MAX.  Errors are one line, err \<code\> [detail], and the
 * command is not run.  A gesture that fails after its ok gets fail
 * \<gesture\> no_master, panel_reset or released; no_master and
 * panel_reset end every contact.
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

/** @brief The line protocol's version, in the ping's reply. */
#define INJ_PROTOCOL        1
/** @brief The resolution the injector reports: the panel's 1024 columns. */
#define INJ_RES_X           1024u
/** @brief The resolution the injector reports: the panel's 600 rows. */
#define INJ_RES_Y           600u
#define INJ_LINE_MAX        63u    /**< characters, without the LF         */
#define INJ_REPORT_MS       10u    /**< least time between two frames      */
#define INJ_ACK_TIMEOUT_MS  100u   /**< a frame not acknowledged by then   */
#define INJ_TAP_MS          100u   /**< a tap's default length             */
#define INJ_MS_MAX          30000u /**< the longest gesture                */
#define INJ_SWIPE_STEP_PX   100u   /**< |dx|+|dy| between two swipe frames */
#define INJ_LIVE_MS         100u   /**< a read this recent: panel polling  */
/** @brief Contacts down at once: the GT911's 5 points. */
#define INJ_CONTACTS        GT911_EMU_POINTS
/** @brief The highest contact id: ids are 0 to 15. */
#define INJ_ID_MAX          15u
/** @brief A reply line's buffer: 128 characters. */
#define INJ_REPLY_LEN       128u
/** @brief The replies queued: 8. */
#define INJ_REPLIES         8u

/** @brief The injector's state: LISTEN after a reset, ACTIVE once confirmed. */
typedef enum {
    INJ_LISTEN = 0,   /**< nothing answers on the bus                  */
    INJ_ACTIVE,       /**< the target answers at 0x14, gestures run    */
} inj_state_t;

/** @brief What a contact is doing. */
typedef enum {
    INJ_G_NONE = 0,   /**< idle                                        */
    INJ_G_TAP,        /**< tap x y [ms]                                */
    INJ_G_HOLD,       /**< hold x y ms                                 */
    INJ_G_SWIPE,      /**< swipe x0 y0 x1 y1 ms                        */
    INJ_G_RELEASE,    /**< the lift after `release` or a closed port   */
} inj_gesture_t;

/** @brief What the status LED shows. */
typedef enum {
    INJ_LED_LISTEN = 0,  /**< white, blinking 1 Hz                       */
    INJ_LED_WIRING_BAD,  /**< yellow: swapped, low or noise              */
    INJ_LED_OFF,         /**< active, no read for INJ_LIVE_MS            */
    INJ_LED_POLLED,      /**< dim green: the panel reads 0x14            */
    INJ_LED_DOWN,        /**< blue: a contact is down                    */
    INJ_LED_FAILED,      /**< red: the last gesture failed               */
} inj_led_t;

/** @brief One contact id's gesture. */
typedef struct {
    inj_gesture_t kind;      /**< INJ_G_NONE when the id is free         */
    char     tag[8];         /**< " id=N" when started with id=, or "" */
    uint16_t x0;             /**< where it starts: column                */
    uint16_t y0;             /**< where it starts: row                   */
    uint16_t x1;             /**< where it ends: column                  */
    uint16_t y1;             /**< where it ends: row                     */
    uint32_t ms;             /**< its length                             */
    bool     down_published; /**< a frame put it down                    */
    bool     lifting;        /**< the frame out lifts it                 */
    bool     in_frame;       /**< the frame out was published for it     */
    bool     first_acked;    /**< the frame that put it down was read    */
    uint32_t t_first;        /**< when that frame was acknowledged       */
    uint32_t frames;         /**< frames read that changed it            */
    uint16_t px;             /**< the last point published: column       */
    uint16_t py;             /**< the last point published: row          */
    bool     slowed;         /**< swipe: the 100 px step stretched it    */
    bool     end_published;  /**< swipe: the frame at (x1, y1)           */
    bool     end_acked;      /**< swipe: that frame was read             */
} inj_contact_t;

/** @brief The whole injector: protocol, contacts, frames and replies. */
typedef struct {
    inj_state_t state;       /**< LISTEN or ACTIVE                       */
    gt911_emu_t emu;         /**< the register file at 0x14              */

    char    line[INJ_LINE_MAX + 1u]; /**< the line being typed           */
    uint8_t len;             /**< its length                             */
    bool    overflow;        /**< it is longer than INJ_LINE_MAX         */

    inj_line_result_t wiring; /**< the last wiring verdict               */
    bool wiring_running;     /**< the glue is listening                  */
    bool wiring_wanted;      /**< for injector_take_wiring()             */
    bool attach_wanted;      /**< for injector_take_attach()             */

    inj_contact_t c[INJ_ID_MAX + 1u]; /**< by contact id                 */
    bool     pending;        /**< a published frame awaits its ack       */
    bool     published;      /**< a frame was published since idle       */
    uint32_t t_pub;          /**< when the last frame was published      */
    bool     withdraw_due;   /**< withdraw the frame once the bus is free */
    bool     failed;         /**< the last gesture failed: the LED is red */

    uint32_t seen_reads;     /**< emu.reads as last seen                 */
    uint32_t seen_acks;      /**< emu.acks as last seen                  */
    uint32_t seen_product;   /**< emu.product_reads as last seen         */
    bool     any_read;       /**< the panel has read once                */
    uint32_t t_last_read;    /**< when it last read                      */

    char    replies[INJ_REPLIES][INJ_REPLY_LEN]; /**< not yet sent       */
    uint8_t r_head;          /**< the oldest                             */
    uint8_t r_count;         /**< how many                               */
} injector_t;

/**
 * @brief Reset: LISTEN, no contacts, no replies.
 * @param in The injector.
 */
void injector_init(injector_t *in);

/**
 * @brief Bytes from the host.  A full line runs at once.
 * @param in The injector.
 * @param bytes The bytes.
 * @param n How many.
 * @param now_ms The clock.
 */
void injector_input(injector_t *in, const char *bytes, size_t n,
                    uint32_t now_ms);

/**
 * @brief Move the gestures on: frames, acknowledges, timeouts.  Call often.
 * @param in The injector.
 * @param now_ms The clock.
 */
void injector_poll(injector_t *in, uint32_t now_ms);

/**
 * @brief True once after `wiring`: the glue starts listening to the lines.
 * @param in The injector.
 * @return Whether to start.
 */
bool injector_take_wiring(injector_t *in);

/**
 * @brief The glue's 500 ms of listening, classified.
 * @param in The injector.
 * @param r The verdict.
 */
void injector_wiring_done(injector_t *in, const inj_line_result_t *r);

/**
 * @brief True once after `confirm`: the glue attaches the I2C target.
 * @param in The injector.
 * @return Whether to attach.
 */
bool injector_take_attach(injector_t *in);

/**
 * @brief The host closed the port (DTR, data terminal ready, fell).
 *
 * Every contact that is down is released; nothing is replied.
 *
 * @param in The injector.
 */
void injector_host_closed(injector_t *in);

/**
 * @brief The oldest reply.
 * @param in The injector.
 * @param out Where it goes.
 * @param n The size of @p out.
 * @return False when there is none.
 */
bool injector_take_reply(injector_t *in, char *out, size_t n);

/**
 * @brief What the status LED shows at @p now_ms.
 * @param in The injector.
 * @param now_ms The clock.
 * @return The colour.
 */
inj_led_t injector_led(const injector_t *in, uint32_t now_ms);

/**
 * @brief The gesture's word.
 * @param g The gesture.
 * @return tap, hold, swipe, release, or idle.
 */
const char *inj_gesture_name(inj_gesture_t g);

#ifdef __cplusplus
}
#endif
