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

/* ------------------------------------------------------------------- bench */
/*
 * The numbers, read-only.
 *
 * Fixed-point rather than floats, because a register is sixteen bits and a
 * float is not, and because the scale is then part of the contract rather
 * than a convention two firmwares have to agree on by accident.
 *
 * The peaks are here rather than computed on the panel: the coprocessor has
 * the fast samples and the panel sees one poll in fifty of them.  A peak
 * derived from what crossed the wire is not a peak, it is the largest thing
 * that happened to be polled.
 */
enum {
    LINK_BN_VOLTAGE_CV   = 0,  /**< 10 mV steps, 0..655.35 V              */
    LINK_BN_CURRENT_CA   = 1,  /**< 10 mA steps, 0..655.35 A              */
    LINK_BN_POWER_W      = 2,  /**< watts                                  */
    LINK_BN_RPM          = 3,
    LINK_BN_TEMP_ESC_DC  = 4,  /**< 0.1 C, signed -- read as int16_t       */
    LINK_BN_TEMP_MOT_DC  = 5,  /**< 0.1 C, signed                          */
    LINK_BN_CHARGE_MAH   = 6,  /**< accumulated in the INA228, not here    */
    LINK_BN_ENERGY_DWH   = 7,  /**< 0.1 Wh                                 */
    LINK_BN_VOLT_MIN_CV  = 8,  /**< sag: the lowest the bus went           */
    LINK_BN_CURR_MAX_CA  = 9,
    LINK_BN_POWER_MAX_W  = 10,
    LINK_BN_RPM_MAX      = 11,
    LINK_BN_FLAGS        = 12, /**< a bitmap of link_bench_flag_t          */
    LINK_BN_COUNT        = 13,
};

/** What the coprocessor says about the numbers it is sending. */
typedef enum {
    LINK_BN_VOLTAGE_OK = 1u << 0, /**< a sensor answered; else the field is 0 */
    LINK_BN_CURRENT_OK = 1u << 1,
    LINK_BN_RPM_OK     = 1u << 2,
    LINK_BN_TEMP_OK    = 1u << 3,
    /**
     * The numbers are modelled, not measured.  Set by a coprocessor running
     * without a front end, and by the panel's own simulator -- the panel
     * writes SIMULATION across the screen either way, and this is how a
     * *remote* fake declares itself rather than being assumed honest.
     */
    LINK_BN_SIMULATED  = 1u << 7,
} link_bench_flag_t;

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
