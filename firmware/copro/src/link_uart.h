/*
 * The coprocessor's end of the RS485 link.
 *
 * Two things this end has to do that the panel's does not, both consequences
 * of the hardware rather than of the protocol:
 *
 *   Drive the direction pin, and hold it until the last stop bit has actually
 *   left the shift register -- not until the FIFO is empty, which is a
 *   different and earlier moment, and the classic way to truncate the final
 *   byte of every frame.
 *
 *   Wait out the panel's own turnaround before answering.  The panel switches
 *   its transceiver with an RC one-shot that holds the bus for up to 179 us
 *   after its last falling edge, so answering promptly means answering into a
 *   driver that is still enabled.
 */
#ifndef RCBENCH_COPRO_LINK_UART_H
#define RCBENCH_COPRO_LINK_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Returns false if @p baud is below the link's floor. */
bool link_uart_init(uint32_t baud);

/**
 * Send a whole frame: assert the driver, write, wait for the transmitter to
 * go idle, release.
 */
void link_uart_write(const uint8_t *frame, size_t len);

/**
 * Read one byte, waiting at most @p timeout_us.  Returns the byte, or -1 on
 * timeout.
 */
int link_uart_read_byte(uint32_t timeout_us);

/** Microseconds since the last byte arrived, for honouring the turnaround. */
uint32_t link_uart_since_last_rx_us(void);

#endif /* RCBENCH_COPRO_LINK_UART_H */
