/*
 * The page map: what the panel can ask the coprocessor for, and set.
 *
 * A page is a window of up to LINK_MAX_REGS sixteen-bit registers with one
 * function.  Adding a capability adds a page, not a message type -- which is
 * the property that keeps the wire format frozen while the bench grows.
 *
 * Pages are numbered with room between the groups on purpose.  A page number
 * is part of the contract between two firmwares that are flashed separately
 * and can disagree about their versions, so renumbering an existing page is a
 * breaking change; leaving gaps means a new one never has to be squeezed in
 * beside an unrelated neighbour.
 */
#ifndef RCBENCH_LINK_PAGES_H
#define RCBENCH_LINK_PAGES_H

#include "link_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LINK_PAGE_IDENTITY = 0x00, /**< who and what, read-only      */
    LINK_PAGE_STATUS   = 0x01, /**< how it is, read-only         */
    LINK_PAGE_CONTROL  = 0x10, /**< what to do                   */
    LINK_PAGE_LIMITS   = 0x11, /**< when to stop without asking  */
    LINK_PAGE_FAILSAFE = 0x12, /**< where to go when nobody asks */
    LINK_PAGE_BENCH    = 0x20, /**< the numbers, read-only       */
} link_page_id_t;

/*
 * The protocol version is the first register of the first page for a reason:
 * it is the one thing a host must be able to read from a coprocessor whose
 * every other page it might not understand.  Bump the major when a register
 * changes meaning or a page is renumbered; bump the minor when a page or a
 * register is added at the end, which an older host can ignore.
 */
#define LINK_PROTOCOL_MAJOR 1u
#define LINK_PROTOCOL_MINOR 0u

/* ---------------------------------------------------------------- identity */
enum {
    LINK_ID_PROTOCOL_MAJOR = 0,
    LINK_ID_PROTOCOL_MINOR = 1,
    LINK_ID_FIRMWARE_MAJOR = 2,
    LINK_ID_FIRMWARE_MINOR = 3,
    LINK_ID_FIRMWARE_PATCH = 4,
    LINK_ID_HARDWARE       = 5, /**< board revision                        */
    LINK_ID_CAPABILITIES   = 6, /**< bitmap: which optional pages are real */
    LINK_ID_COUNT          = 7,
};

/* ------------------------------------------------------------------ status */
enum {
    LINK_ST_STATE      = 0, /**< a link_dev_state_t                      */
    LINK_ST_FAULTS     = 1, /**< a bitmap of link_fault_t                */
    LINK_ST_UPTIME_MS_LO = 2,
    LINK_ST_UPTIME_MS_HI = 3,
    LINK_ST_FRAMES_LO  = 4, /**< frames accepted, for a link that is only */
    LINK_ST_FRAMES_HI  = 5, /**<   sometimes wrong                        */
    LINK_ST_CRC_ERRORS = 6,
    LINK_ST_RESYNCS    = 7,
    LINK_ST_COUNT      = 8,
};

/**
 * What the coprocessor did on its own authority.  Sticky until read and
 * explicitly cleared, because a fault that lasted four milliseconds is still
 * the reason the motor stopped and the operator is owed the explanation.
 */
typedef enum {
    LINK_FAULT_NONE          = 0,
    LINK_FAULT_LINK_SILENT   = 1u << 0,
    LINK_FAULT_OVERCURRENT   = 1u << 1,
    LINK_FAULT_OVERTEMP      = 1u << 2,
    LINK_FAULT_STALL         = 1u << 3,
    LINK_FAULT_HEARTBEAT     = 1u << 4, /**< the safety line stopped edging */
    LINK_FAULT_VERSION       = 1u << 5, /**< the two ends disagree          */
} link_fault_t;

typedef enum {
    LINK_STATE_IDLE     = 0, /**< alive, outputs off            */
    LINK_STATE_ARMED    = 1, /**< outputs live                  */
    LINK_STATE_FAILSAFE = 2, /**< acted on its own authority    */
} link_dev_state_t;

/* ----------------------------------------------------------------- control */
enum {
    LINK_CT_ARM        = 0, /**< non-zero asks for ARMED                    */
    LINK_CT_THROTTLE   = 1, /**< 0..10000, hundredths of a percent          */
    LINK_CT_CLEAR      = 2, /**< write LINK_CLEAR_MAGIC to leave FAILSAFE   */
    LINK_CT_COUNT      = 3,
};

/**
 * Leaving failsafe takes a deliberate value, not any write and not merely the
 * link coming back.  A link that recovers and re-arms itself is a bench that
 * spins up while nobody is looking at it -- the operator has to ask.
 */
#define LINK_CLEAR_MAGIC 0x5AFEu

/** Throttle is hundredths of a percent: 0..10000 inclusive. */
#define LINK_THROTTLE_MAX 10000u

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_PAGES_H */
