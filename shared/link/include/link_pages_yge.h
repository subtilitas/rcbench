/**
 * @file link_pages_yge.h
 * @brief The YGE_PROG and YGE_DATA pages (0x44, 0x45, since 4.42):
 *        programming for YGE ESCs (electronic speed controllers) over the
 *        ESC's telemetry wire on a programming port.
 *
 * Included by link_pages.h.  docs/LinkYge.md holds the rules; the rules in
 * code are shared/outputs/yge_port.h.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LINK_PAGES_YGE_H
#define LINK_PAGES_YGE_H

/** @brief The first minor with YGE_PROG and YGE_DATA.  An older IOmcu refuses
 *         both with LINK_NACK_BAD_PAGE and does not report LINK_CAP_YGE. */
#define LINK_MINOR_YGE 42u

/** @brief Read registers of YGE_PROG. */
enum {
    LINK_YP_STATE = 0,   /**< a link_yge_state_t                             */
    LINK_YP_SEQ,         /**< the last SEQ taken                             */
    LINK_YP_OP,          /**< the last operation, a link_yge_op_t            */
    LINK_YP_RESULT,      /**< low byte link_yge_result_t of that operation,
                              high byte link_yge_ref_t of the last command   */
    LINK_YP_FLAGS,       /**< LINK_YP_F_*                                    */
    LINK_YP_LINE,        /**< low byte link_yge_line_t, high byte the GPIO,
                              0xFF while the page holds no pin               */
    LINK_YP_PORT,        /**< the programming port held; 0xFF when free      */
    LINK_YP_SES,         /**< low byte the session's last result (oy_res_t),
                              high byte the guard's last refusal (oy_ref_t)  */
    LINK_YP_BAD_INDEX,   /**< the index the guard refused                    */
    LINK_YP_TYPE,        /**< the read copy's type code (index 11)           */
    LINK_YP_FW_LO,       /**< its firmware (indices 12, 13), low word        */
    LINK_YP_FW_HI,       /**< high word                                      */
    LINK_YP_SERIAL_LO,   /**< its serial number (indices 14, 15), low word   */
    LINK_YP_SERIAL_HI,   /**< high word                                      */
    LINK_YP_ENTRIES,     /**< entries of the read copy; 0 before a read      */
    LINK_YP_READS,       /**< complete reads; wraps at 65535                 */
    LINK_YP_READ_CRC,    /**< oy_guard_table_crc() of the read copy          */
    LINK_YP_PROGRESS,    /**< low byte indices of the table being read seen,
                              high byte the index being written, 0xFF none   */
    LINK_YP_T_MS,        /**< ms in this state; stops at 65535               */
    LINK_YP_BLOCK,       /**< the block YGE_DATA shows                       */
    LINK_YP_SES_STATE,   /**< low byte the session's state (oy_state_t), high
                              byte why the read copy is read-only (oy_ref_t) */
    LINK_YP_MISSES,      /**< requests and writes not answered in time;
                              stops at 65535                                 */
    LINK_YP_DROPPED,     /**< frames dropped: sequence, type, late, auto;
                              stops at 65535                                 */
    LINK_YP_COUNT        /**< registers                                      */
};

/** @brief Write registers of YGE_PROG.  0 to 3 are one frame, a command. */
enum {
    LINK_YP_W_CMD = 0,   /**< a link_yge_op_t                                */
    LINK_YP_W_SEQ,       /**< LINK_YP_SEQ + 1 modulo 65536                   */
    LINK_YP_W_KEY,       /**< LINK_YGE_KEY for ENTER and WRITE, else 0       */
    LINK_YP_W_CRC,       /**< link_kst_crc() of the staged registers, for
                              OPEN, ENTER and WRITE                          */
    LINK_YP_W_PORT,      /**< staged: the programming port, 0 to 3           */
    LINK_YP_W_POWER,     /**< staged: a link_yge_power_t                     */
    LINK_YP_W_MV,        /**< staged: the supply's voltage, mV               */
    LINK_YP_W_MA,        /**< staged: the supply's limit, mA                 */
    LINK_YP_W_BOOT_MS,   /**< staged: the boot wait, ms                      */
    LINK_YP_W_READS,     /**< staged: LINK_YP_READS the draft was made from  */
    LINK_YP_W_READ_CRC,  /**< staged: LINK_YP_READ_CRC it was made from      */
    LINK_YP_W_DRAFT_CRC, /**< staged: oy_guard_table_crc() of the draft's
                              LINK_YP_ENTRIES entries                        */
    LINK_YP_W_BLOCK,     /**< not staged: the block YGE_DATA shows           */
    LINK_YP_W_COUNT      /**< registers that take a write                    */
};

/** @brief The registers 4 to 11, which the CRC covers. */
#define LINK_YP_W_STAGED 8u
/** @brief Registers a command frame has. */
#define LINK_YP_W_CMD_FRAME 4u
/** @brief What ENTER and WRITE need in LINK_YP_W_KEY: a panel sends it at the
 *         end of the operator's 2s hold, and with no other command. */
#define LINK_YGE_KEY 0x59C7u
/** @brief The lowest voltage ENTER takes, mV. */
#define LINK_YGE_SUPPLY_MIN_MV 3000u
/** @brief Registers of YGE_DATA: one value of the table each. */
#define LINK_YD_COUNT 32u

/** @brief Blocks of YGE_DATA, 32 values each: the first holds indices 0 to 31,
 *         the second 32 to 63. */
enum {
    LINK_YD_READ = 0,    /**< 2 blocks: the read copy; read-only             */
    LINK_YD_DRAFT = 2,   /**< 2 blocks: the table to write; read, write      */
    LINK_YD_LAST = 4,    /**< 2 blocks: the table as last received, also a
                              read cut short; read-only                      */
    LINK_YD_BLOCKS = 6   /**< blocks                                         */
};

/** @brief Where the page is. */
typedef enum {
    LINK_YGE_FREE = 0,    /**< the port is its own                           */
    LINK_YGE_STOPPING,    /**< the port renders nothing; the page has it next */
    LINK_YGE_OPEN,        /**< the pin is the page's, parked; no ESC         */
    LINK_YGE_POWER_OFF,   /**< ENTER: the supply off and read off            */
    LINK_YGE_POWER_ON,    /**< ENTER: ON asked, waits for it to read on      */
    LINK_YGE_HAND_WAIT,   /**< ENTER BY HAND: the line parked, waits for
                               HAND_DONE                                     */
    LINK_YGE_RUN,         /**< an operation of the session runs              */
    LINK_YGE_READY,       /**< the ESC answered; nothing runs                */
    LINK_YGE_NOT_BACK,    /**< the port did not take the pin back: held, so
                               no ARM; CLOSE tries again                     */
    LINK_YGE_STATE_COUNT  /**< states                                        */
} link_yge_state_t;

/** @brief What a command asks. */
typedef enum {
    LINK_YGE_OP_NONE = 0,  /**< never written                                */
    LINK_YGE_OP_OPEN,      /**< take the staged port's pin                   */
    LINK_YGE_OP_CLOSE,     /**< give it back; refused while a write runs     */
    LINK_YGE_OP_ABORT,     /**< cut at once; supply off                      */
    LINK_YGE_OP_STOP,      /**< cut at once, also in a write; supply off     */
    LINK_YGE_OP_CLEAR,     /**< clear LINK_YP_F_STOPPED and LINK_YP_F_FAULT  */
    LINK_YGE_OP_ENTER,     /**< the staged power, the boot wait, the takeover */
    LINK_YGE_OP_HAND_DONE, /**< BY HAND: the ESC has its power               */
    LINK_YGE_OP_READ,      /**< read the table                               */
    LINK_YGE_OP_WRITE,     /**< write the draft                              */
    LINK_YGE_OP_LEAVE,     /**< end the session; supply off                  */
    LINK_YGE_OP_COUNT      /**< operations                                   */
} link_yge_op_t;

/** @brief What became of the last operation. */
typedef enum {
    LINK_YGE_RESULT_NONE = 0, /**< none since the start                      */
    LINK_YGE_RESULT_RUNNING,  /**< it runs                                   */
    LINK_YGE_RESULT_DONE,     /**< it ended as asked                         */
    LINK_YGE_RESULT_NO_LINE,  /**< OPEN: no state machine, program space or
                                   DMA (direct memory access) channel; the
                                   pin went back                             */
    LINK_YGE_RESULT_SUPPLY,   /**< the supply did not read off, refused ON,
                                   or went off under the session             */
    LINK_YGE_RESULT_TIMEOUT,  /**< BY HAND: no HAND_DONE in time             */
    LINK_YGE_RESULT_CUT,      /**< ABORT, STOP or a stop ended it            */
    LINK_YGE_RESULT_SESSION,  /**< the session ended on LINK_YP_SES          */
    LINK_YGE_RESULT_NOT_BACK, /**< CLOSE: the port did not take the pin back;
                                   LINK_YGE_NOT_BACK                         */
} link_yge_result_t;

/** @brief Why a command started nothing. */
typedef enum {
    LINK_YGE_REF_NONE = 0,   /**< it started                                 */
    LINK_YGE_REF_BUSY,       /**< another operation runs, or the page is held */
    LINK_YGE_REF_TARGET,     /**< no such port                               */
    LINK_YGE_REF_HELD,       /**< KST, HITEC, ESC_WIRE or a firmware update
                                  has the bench                              */
    LINK_YGE_REF_ARMED,      /**< the bank is armed or asked to arm          */
    LINK_YGE_REF_UNSAFE,     /**< heartbeat not trusted or link silent       */
    LINK_YGE_REF_NOT_OPEN,   /**< the page holds no pin, or no ESC answered  */
    LINK_YGE_REF_NO_KEY,     /**< ENTER or WRITE without the key             */
    LINK_YGE_REF_STAGED,     /**< the staged registers do not have the CRC   */
    LINK_YGE_REF_STOPPED,    /**< a stop or fault stands: CLEAR first        */
    LINK_YGE_REF_NO_PIN,     /**< the port has no pin to hand over           */
    LINK_YGE_REF_RANGE,      /**< a staged value out of range                */
    LINK_YGE_REF_NO_SUPPLY,  /**< SWITCHED: no PD mini answers               */
    LINK_YGE_REF_STALE,      /**< the draft was not made from this read copy */
    LINK_YGE_REF_DRAFT,      /**< the draft does not have the CRC            */
    LINK_YGE_REF_WRITING,    /**< CLOSE or LEAVE while a write runs          */
    LINK_YGE_REF_SESSION,    /**< the session refused: LINK_YP_SES           */
    LINK_YGE_REF_SUPPLY_OFF, /**< SWITCHED: the panel's OFF stands on the
                                  SUPPLY page                                */
} link_yge_ref_t;

/** @brief How the ESC gets its power. */
typedef enum {
    LINK_YGE_SWITCHED = 0, /**< the PD mini, which the page switches         */
    LINK_YGE_BY_HAND = 1,  /**< a person connects the ESC's own battery      */
    LINK_YGE_POWER_COUNT   /**< values                                       */
} link_yge_power_t;

/** @brief The line, as LINK_YP_LINE says it. */
typedef enum {
    LINK_YGE_LINE_PARK = 0, /**< an input with its pull-down, buffer off     */
    LINK_YGE_LINE_LISTEN,   /**< an input with its pull-up, the receiver on;
                                 driven while a frame goes out              */
} link_yge_line_t;

/** @name Bits of LINK_YP_FLAGS. */
/** @{ */
#define LINK_YP_F_STOPPED   (1u << 0) /**< a stop stands: CLEAR, or CLOSE      */
#define LINK_YP_F_FAULT     (1u << 1) /**< an error switched line and supply off */
#define LINK_YP_F_SUPPLY    (1u << 2) /**< this session switched the supply on */
#define LINK_YP_F_RAIL      (1u << 3) /**< a PD mini answers                   */
#define LINK_YP_F_WRITABLE  (1u << 4) /**< a table was read and may be written */
#define LINK_YP_F_LEGACY    (1u << 5) /**< the ESC sends legacy headers: listen
                                           only, read-only                    */
#define LINK_YP_F_TAKEN     (1u << 6) /**< the ESC answered and the session
                                           has not ended                      */
#define LINK_YP_F_WRITING   (1u << 7) /**< a write frame went out and its read
                                           after has not ended                */
#define LINK_YP_F_HAVE_READ (1u << 8) /**< a whole table was read this session */
#define LINK_YP_F_CUT_WRITE (1u << 9) /**< the last write was cut after its
                                           first frame: the table is not known */
#define LINK_YP_F_BY_HAND   (1u << 10) /**< ENTER's power is BY HAND          */
/** @} */

#endif /* LINK_PAGES_YGE_H */
