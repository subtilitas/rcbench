/**
 * @file
 * @brief The panel's heartbeat input and the CAN bus: the controller and the
 * receive loop that answers the link's requests; see iomcu.h for the
 * other files.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "iomcu.h"

#include "can_selftest.h"
#include "iomcu_pins.h"
#include "xl2515.h"

/* ------------------------------------------------------------ the heartbeat */

/**
 * @brief The panel's safety line, watched in firmware as well as in hardware.
 *
 * No board carries the retriggerable monostable that would gate the output
 * enable in hardware (docs/Safety.md), so this monitor is the whole check.
 * A ringing line, a short to a clock or a floating input next to a switching
 * supply look like edges.  heartbeat_mon_t knows the expected period and
 * rejects what cannot be the panel's control task edging every 20ms.
 *
 * The pin is sampled in the main loop rather than by an interrupt.  A pass
 * that turns over faster than HEARTBEAT_MIN_GAP_MS (4ms) cannot miss an
 * edge the monitor would accept.  An ISR (interrupt service routine) would
 * notice edges faster than the floor, which the monitor rejects anyway.
 *
 * The waits a pass can hold, with their bounds:
 *
 * - The 3s report goes to USB (Universal Serial Bus) stdio.  A printf()
 *   waits for room in the transmit buffer for at most
 *   IOMCU_USB_STDOUT_WAIT_US (1,000us) after the host last took bytes, so a
 *   host that holds the port open and stops reading costs one wait of up
 *   to 1,000us, and later lines are dropped at once.
 * - can_service() waits for the transmit buffer for at most
 *   IOMCU_CAN_TX_WAIT_US (1,000us) per reply frame, and abandons the rest
 *   of a reply whose frame did not leave in that time: a bus that accepts
 *   nothing costs one such wait per reply.
 * - The supply's UART send blocks while its PIO FIFO is full; that wait is
 *   not bounded here.
 * - A flash window of the output store stops this core for about 19ms.
 *
 * The heartbeat is lost when a pass runs past HEARTBEAT_MAX_GAP_MS (150ms).
 * The time of a whole pass on the board is not measured.
 */
bool s_beat_level;

void heartbeat_init(void)
{
    gpio_init(IOMCU_HEARTBEAT_PIN);
    gpio_set_dir(IOMCU_HEARTBEAT_PIN, GPIO_IN);
    /* Pulled down, so an unplugged or unpowered panel reads as a line that is
     * not edging rather than as one held high. */
    gpio_pull_down(IOMCU_HEARTBEAT_PIN);
    heartbeat_mon_init(&s_beat);
    s_beat_level = gpio_get(IOMCU_HEARTBEAT_PIN);
}


/* ------------------------------------------------------------ the CAN bus */

/**
 * @brief Tried at boot, and not fatal if it fails.
 *
 * The controller either answers on SPI (Serial Peripheral Interface) or it
 * does not, and the datasheet guarantees the mode it wakes in, so this
 * distinguishes "no module fitted" and "SPI miswired" from anything on the
 * CAN (Controller Area Network) bus itself.
 *
 * The echo responder runs unconditionally.  It costs one register read per
 * loop, answers only frames addressed to a page the map does not use, and
 * runs at the lowest priority on the bus, so it stays in every build.
 */
bool     s_can_up;
uint32_t s_can_echoes; /**< echo frames answered since power-up */
/** @brief Receive overruns the controller reported since power-up. */
uint32_t s_can_overflows;
/** @brief Reply frames not sent because the transmit buffer stayed full. */
uint32_t s_can_tx_dropped;

/**
 * @brief How long one reply frame waits for the transmit buffer, in us.
 *
 * The buffer holds one frame, so each frame of a multi-frame reply waits for
 * the one before it to win arbitration.  At 1Mbit/s an extended frame of 8
 * data bytes takes at most 155us on the bus, stuff bits and the gap after it
 * counted; 1,000us is 6 of them.  A bus that has stopped accepting (no
 * other node acknowledges, or the controller is bus-off) costs one such wait
 * per reply, then the rest of the reply is abandoned: the panel needs every
 * frame of it and waits out its exchange without one.
 */
#define IOMCU_CAN_TX_WAIT_US 1000u

/**
 * @brief When a frame was last taken out of the controller.
 *
 * can_service() empties both receive buffers every pass, so where this is
 * read it is the age of the last frame on the bus rather than of the last one
 * collected.  The flash windows are opened against it: this core answers
 * nothing while it writes, and the two buffers hold 260us of frames.  Zero
 * until the first frame, so a board that has heard nothing since boot reads
 * as a quiet bus, which is what it has.
 */
uint32_t s_last_rx_ms;

void can_start(void)
{
    s_can_up = xl2515_init(IOMCU_CAN_BITRATE);
    printf("rcbench-iomcu: CAN %s at %u bit/s\n",
           s_can_up ? "up" : "DID NOT ANSWER (module fitted? SPI wiring?)",
           (unsigned)IOMCU_CAN_BITRATE);
}

/**
 * @brief Empties the receive buffers and answers each frame; can_service()
 * times it.
 *
 * One clock for the whole pass, handed in.  Every timeout is a wrap-safe
 * unsigned subtraction, so a last_request_ms stamped 1ms later than the
 * `now` that link_dev_tick() receives reads as 4,294,967,295ms of silence,
 * past every timeout there is.
 *
 * @param now  The pass's clock in ms.
 */
static void can_serve(uint32_t now)
{
    if (xl2515_take_overflow()) {
        ++s_can_overflows;
    }
    link_can_frame_t in, out;
    while (xl2515_recv(&in, &s_refused)) {
        s_last_rx_ms = now;
        if (can_selftest_echo(&in, &out)) {
            if (xl2515_send(&out)) {
                ++s_can_echoes;
            }
            continue;
        }
        /*
         * The far end asking what this end can see, so one console shows
         * both halves of a fault.
         */
        can_remote_status_t st;
        memset(&st, 0, sizeof(st));
        st.up = s_can_up;
        xl2515_errors(&st.tx_errors, &st.rx_errors, &st.flags);
        st.echoes    = (uint16_t)s_can_echoes;
        st.overflows = (uint16_t)s_can_overflows;
        if (can_selftest_status_reply(&in, &st, &out)) {
            (void)xl2515_send(&out);
            continue;
        }

        /*
         * Everything else is the link itself: a page request, answered by
         * the dispatcher, which has no transport in it.
         */
        link_msg_t req, reply;
        if (!link_can_decode_counted(&in, &req, &s_refused)) {
            continue;
        }
        ++s_frames;
        if (!link_dev_dispatch(&s_dev, &req, &reply, now)) {
            continue;
        }
        link_can_frame_t frames[LINK_CAN_MAX_FRAMES];
        const size_t n = link_can_encode(&reply, frames, LINK_CAN_MAX_FRAMES);
        for (size_t i = 0; i < n; ++i) {
            /*
             * Bounded in time, not in tries, because a bus that has stopped
             * accepting must not stall the failsafe: IOMCU_CAN_TX_WAIT_US.
             * Each try is one SPI register read.
             */
            const uint32_t since = time_us_32();
            bool sent = xl2515_send(&frames[i]);
            while (!sent && time_us_32() - since < IOMCU_CAN_TX_WAIT_US) {
                tight_loop_contents();
                sent = xl2515_send(&frames[i]);
            }
            if (!sent) {
                /* Counted, this frame and the ones after it: the panel then
                 * waits out its exchange. */
                s_can_tx_dropped += (uint32_t)(n - i);
                break;
            }
        }
    }
}

void can_service(uint32_t now)
{
    if (!s_can_up) {
        return;
    }
    const uint32_t t0 = time_us_32();
    can_serve(now);
    pass_stats_section(&s_pass, PASS_SEC_CAN, t0, time_us_32());
}
