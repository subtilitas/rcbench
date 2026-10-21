/**
 * @file
 * @brief The panel's heartbeat input and the CAN bus.
 *
 * The file holds the controller and the receive loop that answers the link's
 * requests. See iomcu.h for the other files.
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
 * @brief The heartbeat line's level at the last sample.
 *
 * The panel's safety line, watched in firmware as well as in hardware.
 *
 * No board carries the retriggerable monostable that would gate the output
 * enable in hardware (docs/Safety.md), so this monitor is the whole check.
 * A ringing line, a short to a clock or a floating input next to a switching
 * supply look like edges. heartbeat_mon_t knows the expected period and
 * rejects what cannot be the panel's control task edging every 20 ms.
 *
 * The pin is sampled in the main loop rather than by an interrupt. A pass
 * that turns over faster than HEARTBEAT_MIN_GAP_MS (4 ms) cannot miss an
 * edge the monitor would accept. The pass time is not measured: a printf
 * every 3 s goes to USB (Universal Serial Bus) stdio, and the supply's
 * UART send blocks while its PIO FIFO is full. An ISR (interrupt service
 * routine) would notice edges faster than the floor, which the monitor
 * rejects anyway.
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
 * @brief True when the CAN controller answered at boot.
 *
 * Tried at boot, and not fatal if it fails.
 *
 * The controller either answers on SPI (Serial Peripheral Interface) or it
 * does not, and the datasheet guarantees the mode it wakes in, so this
 * distinguishes "no module fitted" and "SPI miswired" from anything on the
 * CAN (Controller Area Network) bus itself.
 *
 * The echo responder runs unconditionally. It costs one register read per
 * loop, answers only frames addressed to a page the map does not use, and
 * runs at the lowest priority on the bus, so it stays in every build.
 */
bool     s_can_up;
/** @brief Self-test echoes sent by the echo responder. */
uint32_t s_can_echoes;
/** @brief Receive overruns counted by can_service(). */
uint32_t s_can_overflows;

/**
 * @brief When a frame was last taken out of the controller, in ms.
 *
 * can_service() empties both receive buffers every pass, so where this is
 * read it is the age of the last frame on the bus rather than of the last one
 * collected. The flash windows are opened against it: this core answers
 * nothing while it writes, and the two buffers hold 260 us of frames. Zero
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

/*
 * One clock for the whole pass, handed in.  Every timeout is a wrap-safe
 * unsigned subtraction, so a last_request_ms stamped 1 ms later than the
 * `now` that link_dev_tick() receives reads as 4,294,967,295 ms of silence,
 * past every timeout there is.
 */
void can_service(uint32_t now)
{
    if (!s_can_up) {
        return;
    }
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
             * The transmit buffer holds one frame, so a multi-frame answer
             * waits for each frame to win arbitration.  Bounded at 1000
             * spins, because a bus that has stopped accepting must not stall
             * the failsafe.
             */
            for (int spin = 0; spin < 1000 && !xl2515_send(&frames[i]); ++spin) {
                tight_loop_contents();
            }
        }
    }
}
