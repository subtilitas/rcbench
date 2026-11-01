/**
 * @file link_pages_esc.h
 * @brief The ESC_WIRE and ESC_DATA pages (0x3A, 0x3B, since 4.34): the settings
 *        of an ESC (electronic speed controller) over its signal wire.
 *
 * Included by link_pages.h.  docs/LinkEsc.md holds the rules; the rules in code
 * are shared/outputs/esc_port.h.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LINK_PAGES_ESC_H
#define LINK_PAGES_ESC_H

/** @brief The first minor with ESC_WIRE and ESC_DATA.  An older IOmcu refuses
 *         both with LINK_NACK_BAD_PAGE and does not report LINK_CAP_PROGRAM. */
#define LINK_MINOR_ESC_WIRE 34u

/** @brief Read registers of ESC_WIRE. */
enum {
    LINK_EW_STATE = 0,   /**< a link_esc_state_t                             */
    LINK_EW_SEQ,         /**< the last SEQ taken                             */
    LINK_EW_OP,          /**< the last operation, a link_esc_op_t            */
    LINK_EW_RESULT,      /**< low byte link_esc_result_t of that operation,
                              high byte link_esc_ref_t of the last command   */
    LINK_EW_FLAGS,       /**< LINK_EW_F_*                                    */
    LINK_EW_LINE,        /**< low byte link_esc_line_t, high byte the GPIO,
                              0xFF while the page holds no pin               */
    LINK_EW_TARGET,      /**< LINK_EW_W_TARGET as held; 0 when free          */
    LINK_EW_SES,         /**< low byte the session's last result, high byte
                              the guard's last refusal                       */
    LINK_EW_ID,          /**< low byte the firmware, high byte the layout    */
    LINK_EW_WHY,         /**< low byte why it is not written, high byte the
                              processor the first contact named              */
    LINK_EW_SIGNATURE,   /**< an EFM8's signature                            */
    LINK_EW_LEN,         /**< bytes of the page                              */
    LINK_EW_BAD_OFF,     /**< the byte the guard refused; text: the key      */
    LINK_EW_CLIPPED,     /**< text: the value a clipping `set` stored        */
    LINK_EW_TRIES,       /**< low byte write procedures started, high byte
                              the last answer judged                         */
    LINK_EW_READS,       /**< reads of the page this session; wraps          */
    LINK_EW_READ_CRC,    /**< link_crc() of the read copy's LEN bytes        */
    LINK_EW_T_MS,        /**< ms in this state; stops at 65535               */
    LINK_EW_CONTACT,     /**< ARM: low byte pin code, high byte flash code   */
    LINK_EW_PROTOCOL,    /**< ARM: the bootloader's protocol                 */
    LINK_EW_BLOCK,       /**< the block ESC_DATA shows                       */
    LINK_EW_COUNT        /**< registers                                      */
};

/** @brief Write registers of ESC_WIRE.  0 to 3 are one frame, a command. */
enum {
    LINK_EW_W_CMD = 0,   /**< a link_esc_op_t                                */
    LINK_EW_W_SEQ,       /**< LINK_EW_SEQ + 1 modulo 65536                   */
    LINK_EW_W_KEY,       /**< LINK_ESC_WRITE_KEY for WRITE and RESTORE, else 0 */
    LINK_EW_W_CRC,       /**< link_kst_crc() of the staged registers, for
                              OPEN, ENTER, WRITE and RESTORE                 */
    LINK_EW_W_TARGET,    /**< staged: LINK_EW_TARGET_PORT and an index       */
    LINK_EW_W_POWER,     /**< staged: a link_esc_power_t                     */
    LINK_EW_W_MV,        /**< staged: the supply's voltage, mV               */
    LINK_EW_W_MA,        /**< staged: the supply's limit, mA                 */
    LINK_EW_W_HOLD_LO,   /**< staged: the hold after ON, ms, low word        */
    LINK_EW_W_HOLD_HI,   /**< staged: its high word                          */
    LINK_EW_W_OFF_MS,    /**< staged: the time off before ON, ms             */
    LINK_EW_W_READS,     /**< staged: LINK_EW_READS the draft was made from  */
    LINK_EW_W_READ_CRC,  /**< staged: LINK_EW_READ_CRC it was made from      */
    LINK_EW_W_DRAFT_CRC, /**< staged: link_crc() of the draft's LEN bytes    */
    LINK_EW_W_BLOCK,     /**< not staged: the block ESC_DATA shows           */
    LINK_EW_W_COUNT      /**< registers that take a write                    */
};

/** @brief The registers 4 to 13, which the CRC covers. */
#define LINK_EW_W_STAGED 10u
/** @brief Registers a command frame has. */
#define LINK_EW_W_CMD_FRAME 4u
/** @brief LINK_EW_W_TARGET: a programming port; clear, an output slot. */
#define LINK_EW_TARGET_PORT 0x0100u
/** @brief What WRITE and RESTORE need in LINK_EW_W_KEY. */
#define LINK_ESC_WRITE_KEY 0x45C3u
/** @brief The lowest voltage ENTER takes, mV. */
#define LINK_ESC_SUPPLY_MIN_MV 3000u
/** @brief The longest time off ENTER takes, ms. */
#define LINK_ESC_OFF_MS_MAX 10000u
/** @brief Registers of ESC_DATA, 64B. */
#define LINK_ED_COUNT LINK_MAX_REGS
/** @brief The bytes of a block of ESC_DATA. */
#define LINK_ED_BLOCK_BYTES 64u

/** @brief Blocks of ESC_DATA: bytes, low byte first in each register. */
enum {
    LINK_ED_READ = 0,    /**< 4 blocks: the page as read; read-only          */
    LINK_ED_DRAFT = 4,   /**< 4 blocks: the page to write; read, write       */
    LINK_ED_PAGE = 8,    /**< 4 blocks: the page as last read, also after a
                              failed write; read-only                        */
    LINK_ED_NAME = 12,   /**< the name or text target, ends in 0; read-only  */
    LINK_ED_INFO = 13,   /**< 2 blocks: `info`'s revision and live values;
                              read-only                                      */
    LINK_ED_MUSIC = 15,  /**< 4 blocks: `music`, ends in 0; read-only        */
    LINK_ED_BACKUP = 19, /**< a backup's identity for RESTORE: pin code, flash
                              code, protocol, 0, the name; read, write       */
    LINK_ED_BLOCKS = 20  /**< blocks                                         */
};

/** @brief Where the page is. */
typedef enum {
    LINK_ESC_FREE = 0,    /**< the pin is its output's                       */
    LINK_ESC_STOPPING,    /**< the output renders nothing; the page has it next */
    LINK_ESC_OPEN,        /**< the pin is the page's, parked; no ESC         */
    LINK_ESC_POWER_OFF,   /**< ENTER: the supply off, read off, its off time */
    LINK_ESC_LEAD,        /**< ENTER: the line high before ON                */
    LINK_ESC_HAND_WAIT,   /**< ENTER BY HAND: the line high, waits for DONE  */
    LINK_ESC_RUN,         /**< an operation of the session runs              */
    LINK_ESC_READY,       /**< the ESC answered; nothing runs                */
    LINK_ESC_POWER_ON,    /**< ENTER: ON asked, waits for it to read on      */
    LINK_ESC_NOT_BACK,    /**< the output did not take the pin back: held, so
                               no ARM; CLOSE tries again                     */
    LINK_ESC_STATE_COUNT  /**< states                                        */
} link_esc_state_t;

/** @brief What a command asks. */
typedef enum {
    LINK_ESC_OP_NONE = 0, /**< never written                                 */
    LINK_ESC_OP_OPEN,     /**< take the staged target's pin                  */
    LINK_ESC_OP_CLOSE,    /**< give it back; refused while a write runs      */
    LINK_ESC_OP_ABORT,    /**< cut at once, also in a write; supply off      */
    LINK_ESC_OP_STOP,     /**< cut; a write runs to its read-back first      */
    LINK_ESC_OP_CLEAR,    /**< clear LINK_EW_F_STOPPED and LINK_EW_F_FAULT   */
    LINK_ESC_OP_ENTER,    /**< the staged power, then first contact          */
    LINK_ESC_OP_DONE,     /**< BY HAND: the ESC has its power                */
    LINK_ESC_OP_READ,     /**< read the page                                 */
    LINK_ESC_OP_WRITE,    /**< write the draft                               */
    LINK_ESC_OP_RESTORE,  /**< write the draft as a backup onto erased settings */
    LINK_ESC_OP_LEAVE,    /**< leave to the application; supply off          */
    LINK_ESC_OP_COUNT     /**< operations                                    */
} link_esc_op_t;

/** @brief What became of the last operation. */
typedef enum {
    LINK_ESC_RESULT_NONE = 0, /**< none since the start                      */
    LINK_ESC_RESULT_RUNNING,  /**< it runs                                   */
    LINK_ESC_RESULT_DONE,     /**< it ended as asked                         */
    LINK_ESC_RESULT_NO_LINE,  /**< OPEN: no state machine, program space or
                                   DMA (direct memory access) channel; the
                                   pin went back                             */
    LINK_ESC_RESULT_SUPPLY,   /**< the supply did not read off, refused ON,
                                   or went off under the session             */
    LINK_ESC_RESULT_TIMEOUT,  /**< BY HAND: no DONE in time                  */
    LINK_ESC_RESULT_CUT,      /**< ABORT, STOP or a stop ended it            */
    LINK_ESC_RESULT_SESSION,  /**< the session ended on LINK_EW_SES          */
    LINK_ESC_RESULT_NOT_BACK, /**< CLOSE: the output did not take the pin back;
                                   LINK_ESC_NOT_BACK                         */
} link_esc_result_t;

/** @brief Why a command started nothing. */
typedef enum {
    LINK_ESC_REF_NONE = 0,  /**< it started                                  */
    LINK_ESC_REF_BUSY,      /**< another operation runs, or the page is held */
    LINK_ESC_REF_TARGET,    /**< no such slot or port                        */
    LINK_ESC_REF_HELD,      /**< KST, HITEC or a firmware update has the bench */
    LINK_ESC_REF_ARMED,     /**< the bank is armed or asked to arm           */
    LINK_ESC_REF_UNSAFE,    /**< heartbeat not trusted or link silent        */
    LINK_ESC_REF_NOT_OPEN,  /**< the page holds no pin, or no ESC answered   */
    LINK_ESC_REF_NO_KEY,    /**< WRITE or RESTORE without the key            */
    LINK_ESC_REF_STAGED,    /**< the staged registers do not have the CRC    */
    LINK_ESC_REF_STOPPED,   /**< a stop or fault stands: CLEAR first         */
    LINK_ESC_REF_NO_PIN,    /**< no throttle slot or DShot port there        */
    LINK_ESC_REF_RANGE,     /**< a staged value out of range                 */
    LINK_ESC_REF_NO_SUPPLY, /**< SWITCHED: no PD mini answers                */
    LINK_ESC_REF_STALE,     /**< the draft was not made from this read copy  */
    LINK_ESC_REF_DRAFT,     /**< the draft does not have the CRC             */
    LINK_ESC_REF_WRITING,   /**< CLOSE or LEAVE while a write runs or its tail */
    LINK_ESC_REF_SESSION,   /**< the session refused: LINK_EW_SES            */
    LINK_ESC_REF_SUPPLY_OFF, /**< SWITCHED: the panel's OFF stands on the SUPPLY
                                  page until the hold ends                    */
} link_esc_ref_t;

/** @brief How the ESC gets its power. */
typedef enum {
    LINK_ESC_SWITCHED = 0, /**< the PD mini, which the page switches         */
    LINK_ESC_BY_HAND = 1,  /**< a person connects it after the line is high  */
    LINK_ESC_POWER_COUNT   /**< values                                       */
} link_esc_power_t;

/** @brief The line, as LINK_EW_LINE says it. */
typedef enum {
    LINK_ESC_LINE_PARK = 0, /**< an input with its pull-down                 */
    LINK_ESC_LINE_HIGH,     /**< driven high                                 */
    LINK_ESC_LINE_UP,       /**< an input with its pull-up                   */
    LINK_ESC_LINE_LOW,      /**< driven low                                  */
    LINK_ESC_LINE_SEND,     /**< a transaction runs                          */
} link_esc_line_t;

/** @name Bits of LINK_EW_FLAGS. */
/** @{ */
#define LINK_EW_F_STOPPED  (1u << 0) /**< a stop stands: CLEAR, or CLOSE      */
#define LINK_EW_F_FAULT    (1u << 1) /**< an error switched line and supply off */
#define LINK_EW_F_SUPPLY   (1u << 2) /**< this session switched the supply on */
#define LINK_EW_F_RAIL     (1u << 3) /**< a PD mini answers                   */
#define LINK_EW_F_WRITABLE (1u << 4) /**< the ESC's page can be written       */
#define LINK_EW_F_ERASED   (1u << 5) /**< AM32 settings erased: only RESTORE  */
#define LINK_EW_F_CAN      (1u << 6) /**< AM32 built for CAN boards           */
#define LINK_EW_F_IN_MODE  (1u << 7) /**< the ESC answered and has not left   */
#define LINK_EW_F_WRITING  (1u << 8) /**< SUPPLY holds the panel's OFF back   */
#define LINK_EW_F_DIRTY    (1u << 9) /**< a page is half written              */
#define LINK_EW_F_HAVE_READ (1u << 10) /**< a whole page was read             */
#define LINK_EW_F_SAVE_OPEN (1u << 11) /**< text: a `save` is not proven      */
/** @} */

#endif /* LINK_PAGES_ESC_H */
