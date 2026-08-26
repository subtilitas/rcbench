/*
 * The panel's end of the RS485 link: bytes in, bytes out, and nothing else.
 *
 * Deliberately dumb.  Framing, CRC, page semantics and both watchdogs live in
 * shared/link and are tested on a laptop; this file exists only because those
 * bytes have to reach a wire.  Every decision it could make is one that could
 * then only be tested on hardware.
 *
 * There is no direction pin.  The board switches its own transceiver -- see
 * the turnaround note in shared/link/include/link_wire.h -- so the UART is an
 * ordinary full-duplex port here and never asserts RTS.  The consequence that
 * matters is on the *other* end: the coprocessor must not answer until this
 * board's driver has released.
 */
#ifndef RCBENCH_LINK_UART_H
#define RCBENCH_LINK_UART_H

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bring the port up at @p baud on the given pins.
 *
 * The pins are arguments rather than something this file looks up.  A
 * transport that reached into the application for its own pin map would be a
 * component depending on main, which is backwards, and it would make this file
 * specific to one board for no gain -- the pin map is knowledge the
 * application has and the wire does not care about.
 *
 * Refuses a rate below LINK_BAUD_FLOOR rather than running one: below the
 * floor a run of high bits inside a frame outlasts the direction circuit's
 * hold and the driver switches off mid-transmission, which presents as
 * intermittent corruption rather than as a slow link.
 */
esp_err_t link_uart_init(uart_port_t port, uint32_t baud,
                         gpio_num_t tx, gpio_num_t rx);

/** Write a whole frame.  Returns bytes written, or -1. */
int link_uart_write(const uint8_t *frame, size_t len);

/**
 * Read up to @p max bytes, waiting at most @p timeout_ms for the first.
 * Returns the count, 0 on timeout, or -1.
 */
int link_uart_read(uint8_t *out, size_t max, uint32_t timeout_ms);

/**
 * Drop whatever is in the receive buffer.
 *
 * For use after a timeout, and only there: the bytes of an abandoned reply
 * are still on their way, and feeding them to the decoder afterwards is how a
 * stale frame gets offered as the answer to the next question.  The host
 * layer would reject it -- that is what its page and offset checks are for --
 * but there is no reason to make it work for a living.
 */
void link_uart_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_UART_H */
