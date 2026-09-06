/*
 * The screen a bad CAN (Controller Area Network) bus gets at start-up.
 *
 * The panel runs the echo self-test once, before the bench is usable, and
 * shows this instead of the menu when the test does not pass.  It exists
 * because the fault it reports is not visible from any other screen: a bus
 * that does not carry frames looks exactly like a coprocessor that is not
 * there, and both look like a bench that simply shows no numbers.
 *
 * What it puts on the panel is the verdict, the counts behind it, and the
 * things to check in the order that costs least to check.  An operator with
 * no console and no meter can act on it.
 *
 * Leaving it takes a two-second hold, the ARM gesture: this is the one screen
 * that says the bench cannot be trusted, and an acknowledgement given by
 * brushing the panel is not one.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_BUSFAULT_SCREEN_H
#define RCBENCH_BUSFAULT_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "can_selftest.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What the start-up test found, as the screen needs it.
 *
 * Both ends, because the pair is what separates the faults: frames the far
 * end answered and this end never heard are a return-path fault, and frames
 * it never answered are an outbound one.
 */
typedef struct {
    can_selftest_verdict_t verdict;

    uint32_t sent;
    uint32_t echoed;
    uint32_t corrupt;
    uint32_t lost;

    uint32_t tx_errors;   /**< the panel's controller */
    uint32_t rx_errors;
    uint32_t bus_errors;
    bool     bus_off;

    bool     have_remote; /**< the coprocessor answered a status request */
    bool     remote_up;   /**< and said its own controller came up       */
    uint8_t  remote_tx_errors;
    uint8_t  remote_rx_errors;
    uint8_t  remote_flags;
    uint16_t remote_overflows;
} busfault_report_t;

/** Hand the screen what to show. A NULL report clears it. */
void busfault_screen_set(const busfault_report_t *r);

/** What was handed in, for the application deciding whether to show it. */
const busfault_report_t *busfault_screen_report(void);

/**
 * True once the acknowledgement hold has completed, and clears when read.
 *
 * A latch rather than a jump: which screen an acknowledged fault leads to is
 * the application's business, not the screen's.
 */
bool busfault_screen_take_ack(void);

void busfault_screen_invalidate(void);

const ui_screen_t *busfault_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_BUSFAULT_SCREEN_H */
