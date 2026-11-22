/**
 * @file
 * @brief The link's transport: CAN (Controller Area Network) through the
 * XL2515 or a UART (universal asynchronous receiver-transmitter), one
 * interface, chosen by the board the image is built for.
 *
 * Both answer the same requests through the same link_dev_t (s_dev) and
 * count into the same s_frames and s_refused; what differs is how a frame
 * arrives and leaves.  main.c and the report call the transport through
 * the iomcu_link_*() functions below only.
 *
 * | Transport | Boards | Source |
 * | --- | --- | --- |
 * | CAN, XL2515 on spi1 | rp2350_can (board 1), rbmod (board 2) | iomcu_can.c |
 * | UART0, 1Mbaud 8N1, TX GPIO 0, RX GPIO 1, RX by DMA | rsn (board 3) | iomcu_uart.c |
 *
 * CMake's IOMCU_LINK chooses it: empty takes the board's, `can` or `uart`
 * names one.  It sets IOMCU_LINK_UART to 0 or 1.  Both sources are built
 * into every image, so both keep compiling; the linker keeps the one the
 * image calls.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef IOMCU_LINK_H
#define IOMCU_LINK_H

#include <stdint.h>

#include "iomcu.h"
#include "report_lines.h"

#ifdef __cplusplus
extern "C" {
#endif

/* iomcu_can.c: CAN through the XL2515; can_start(), can_service() and
 * can_drain() are in iomcu.h. */
/**
 * @brief STATUS registers 6 and 7 on CAN: the controller's receive and
 * transmit error counters, 0 to 255.
 *
 * @param crc_errors Receives the receive error counter.
 * @param resyncs Receives the transmit error counter.
 */
void can_wire_errors(uint16_t *crc_errors, uint16_t *resyncs);
/**
 * @brief The CAN lines' figures into the 3s report's snapshot; the transmit
 * queue's peak is read and cleared.
 *
 * @param s The snapshot.
 */
void can_snap(report_snap_t *s);

/* iomcu_uart.c: UART0 with its receive DMA ring */
/**
 * @brief Bring UART0 up at LINK_UART_BAUD and start its receive DMA
 * channel; without a free channel the transport stays down, which the boot
 * line and the report say.  The IOmcu then hears nothing, and the link's
 * 200ms silence failsafe holds every output off.
 */
void uart_link_start(void);
/**
 * @brief Take every frame the DMA wrote, answer each, and move replies into
 * the UART's FIFO; waits for nothing.  Its time goes into s_pass as
 * PASS_SEC_CAN.
 *
 * @param now The pass's clock in ms.
 */
void uart_link_service(uint32_t now);
/**
 * @brief Send what is queued before a restart: at most 5,120us, never
 * longer than LOOP_WATCH_DRAIN_US.
 */
void uart_link_drain(void);
/**
 * @brief STATUS registers 6 and 7 on the UART, each modulo 65536: frames
 * refused for their CRC-16, and resyncs.
 *
 * @param crc_errors Receives register 6.
 * @param resyncs Receives register 7.
 */
void uart_link_wire_errors(uint16_t *crc_errors, uint16_t *resyncs);
/**
 * @brief The UART line's figures into the 3s report's snapshot; the
 * transmit queue's peak is read and cleared.
 *
 * @param s The snapshot.
 */
void uart_link_snap(report_snap_t *s);

/** @brief UART0's transmit pin to the node, GPIO 0. */
#define IOMCU_UART_PIN_TX 0
/** @brief UART0's receive pin from the node, GPIO 1. */
#define IOMCU_UART_PIN_RX 1

#if defined(IOMCU_LINK_UART) && IOMCU_LINK_UART
/** @brief The transport's pins, never handed to an output. */
#define IOMCU_LINK_PINS \
    ((1ull << IOMCU_UART_PIN_TX) | (1ull << IOMCU_UART_PIN_RX))
#else
/** @brief The transport's pins beyond IOMCU_RESERVED_PINS: none. */
#define IOMCU_LINK_PINS 0ull
#endif

/*
 * The interface main.c and the report call.  Direct calls, chosen when the
 * image is built, so tools/stack_check.py follows each into the transport.
 */

/** @brief Bring the transport up; a failure is reported and not fatal. */
static inline void iomcu_link_start(void)
{
#if defined(IOMCU_LINK_UART) && IOMCU_LINK_UART
    uart_link_start();
#else
    can_start();
#endif
}

/**
 * @brief Take every frame received, answer each, and move queued frames
 * out; waits for nothing.
 *
 * @param now The pass's clock in ms, stamped on each frame received.
 */
static inline void iomcu_link_service(uint32_t now)
{
#if defined(IOMCU_LINK_UART) && IOMCU_LINK_UART
    uart_link_service(now);
#else
    can_service(now);
#endif
}

/**
 * @brief Send every queued frame before a restart; waits at most
 * LOOP_WATCH_DRAIN_US (35,000us).
 */
static inline void iomcu_link_drain(void)
{
#if defined(IOMCU_LINK_UART) && IOMCU_LINK_UART
    uart_link_drain();
#else
    can_drain();
#endif
}

/**
 * @brief The figures for STATUS registers 6 and 7, LINK_ST_CRC_ERRORS and
 * LINK_ST_RESYNCS.
 *
 * @param crc_errors Receives register 6.
 * @param resyncs Receives register 7.
 */
static inline void iomcu_link_wire_errors(uint16_t *crc_errors,
                                          uint16_t *resyncs)
{
#if defined(IOMCU_LINK_UART) && IOMCU_LINK_UART
    uart_link_wire_errors(crc_errors, resyncs);
#else
    can_wire_errors(crc_errors, resyncs);
#endif
}

/**
 * @brief The transport's figures into the 3s report's snapshot.
 *
 * @param s The snapshot; its transport fields are written.
 */
static inline void iomcu_link_snap(report_snap_t *s)
{
#if defined(IOMCU_LINK_UART) && IOMCU_LINK_UART
    uart_link_snap(s);
#else
    can_snap(s);
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* IOMCU_LINK_H */
