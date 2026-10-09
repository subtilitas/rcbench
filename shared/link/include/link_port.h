/*
 * A write of any width, one frame in flight at a time.
 *
 * link_host_write() builds one request, and the CAN (Controller Area
 * Network) mapping renders a request wider than LINK_CAN_REGS_PER_FRAME (4)
 * registers as several frames sent back to back.  The coprocessor's
 * controller buffers 2 received frames and is read from a polled loop, so
 * the 8 frames of a 32-register page arrive faster than a loop that is busy
 * for one pass can take them, and the frame that finds both buffers full is
 * gone.
 *
 * link_write_acked() sends the same registers as one request per frame and
 * waits for each acknowledgement before it builds the next.  At most one
 * request frame is on its way at any time, so a slow pass at the far end
 * delays a frame and cannot lose one to a full buffer.
 *
 * The transport and the clock are the caller's, handed in as a link_port_t,
 * so the sequence runs on the host against a modelled bus.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_PORT_H
#define RCBENCH_LINK_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "link_host.h"
#include "link_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How a request reaches the far end and its answer comes back.
 *
 * `exchange` carries one request built by link_host_read() or
 * link_host_write() and returns true with the answer in @p reply, a refusal
 * included, or false when it ended unanswered.  It leaves no request
 * outstanding on @p h either way.  `now_ms` is the millisecond clock the
 * requests are stamped with.
 */
typedef struct {
    bool     (*exchange)(void *ctx, link_host_t *h, const link_msg_t *req,
                         link_msg_t *reply);
    uint32_t (*now_ms)(void *ctx);
    void      *ctx;
} link_port_t;

/**
 * Write @p count registers of @p page from @p offset, one frame of up to
 * LINK_CAN_REGS_PER_FRAME registers per exchange, each acknowledged before
 * the next is sent.
 *
 * True when the far end answered every frame sent.  @p reply is then one of
 * two things: an ACK (acknowledge) for the whole window, carrying what the
 * far end stored for each register, or the NACK (negative acknowledge) of
 * the first frame it refused, with that frame's offset.  Nothing is sent
 * after a refusal.
 *
 * False when a frame ended unanswered, when a request is already outstanding
 * on @p h, or when the window does not fit a page; @p reply is then not
 * meaningful.
 *
 * The frames before a refused or unanswered one stay applied at the far end:
 * the transport has no rollback.  A page that must change whole is written
 * through pages that prepare it; see LINK_PAGE_BIND.
 */
bool link_write_acked(link_host_t *h, const link_port_t *port, uint8_t page,
                      uint8_t offset, uint8_t count, const uint16_t *regs,
                      link_msg_t *reply);

/**
 * Read @p count registers of @p page from @p offset in one exchange.  True
 * when it was answered; @p reply is the data or the refusal.
 */
bool link_read_window(link_host_t *h, const link_port_t *port, uint8_t page,
                      uint8_t offset, uint8_t count, link_msg_t *reply);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_PORT_H */
