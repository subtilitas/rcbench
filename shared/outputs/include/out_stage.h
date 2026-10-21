/**
 * @file
 * @brief A binding prepared beside the one in force, and taken whole.
 *
 * The IOmcu's half of the BIND_CFG, BIND_OUT and BIND pages (protocol
 * 4.10; see link_pages.h).  A CHAN_CFG and an OUTPUTS page are written here
 * a frame at a time and change nothing; a commit that names their CRC
 * (cyclic redundancy check) puts both in force or neither.
 *
 * The rules that judge a page in force are the IOmcu's and need its
 * bank and its silicon, so the commit calls them through out_stage_ops_t.
 * The order, the comparison and what is put back after a refusal are here
 * and host-tested.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "link_pages.h"
#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The CHAN_CFG and OUTPUTS pages prepared beside the bank in force. */
typedef struct {
    uint16_t cfg[LINK_CC_COUNT];    /**< the CHAN_CFG page prepared */
    uint16_t slots[LINK_OS_COUNT];  /**< the OUTPUTS page prepared  */
} out_stage_t;

/**
 * @brief Start from the pages in force; NULL starts from the pages' defaults.
 *
 * Does nothing when @p st is NULL.
 *
 * @param st The staged pages, overwritten.
 * @param cfg The CHAN_CFG page in force (LINK_CC_COUNT registers), or NULL.
 * @param slots The OUTPUTS page in force (LINK_OS_COUNT registers), or NULL.
 */
void out_stage_init(out_stage_t *st, const uint16_t *cfg,
                    const uint16_t *slots);

/**
 * @brief A window of BIND_CFG or BIND_OUT written: 0, or the link_nack_t
 *        outputs_chan_cfg_write() and outputs_slots_write() give for the same
 *        window, and then nothing of it is stored.
 *
 * @param st The staged pages.
 * @param off First register of the window.
 * @param n Number of registers in the window.
 * @param in The @p n register values to write.
 * @return 0 when stored. LINK_NACK_BAD_RANGE when @p st is NULL, otherwise the
 *         refusal of outputs_chan_cfg_write() for the window.
 */
uint8_t out_stage_cfg_write(out_stage_t *st, uint8_t off, uint8_t n,
                            const uint16_t *in);

/**
 * @brief A window of BIND_OUT written: 0, or the link_nack_t
 *        outputs_slots_write() gives for the same window, and then nothing of
 *        it is stored.
 *
 * @param st The staged pages.
 * @param off First register of the window.
 * @param n Number of registers in the window.
 * @param in The @p n register values to write.
 * @return 0 when stored. LINK_NACK_BAD_RANGE when @p st is NULL, otherwise the
 *         refusal of outputs_slots_write() for the window.
 */
uint8_t out_stage_slots_write(out_stage_t *st, uint8_t off, uint8_t n,
                              const uint16_t *in);

/**
 * @brief Copies a window of the prepared CHAN_CFG page.
 *
 * Registers at or past LINK_CC_COUNT read as 0. Nothing is done when @p st or
 * @p out is NULL.
 *
 * @param st The staged pages, not changed.
 * @param off First register of the window.
 * @param n Number of registers to copy.
 * @param out Receives the @p n register values.
 */
void out_stage_cfg_read(const out_stage_t *st, uint8_t off, uint8_t n,
                        uint16_t *out);

/**
 * @brief Copies a window of the prepared OUTPUTS page.
 *
 * Registers at or past LINK_OS_COUNT read as 0. Nothing is done when @p st or
 * @p out is NULL.
 *
 * @param st The staged pages, not changed.
 * @param off First register of the window.
 * @param n Number of registers to copy.
 * @param out Receives the @p n register values.
 */
void out_stage_slots_read(const out_stage_t *st, uint8_t off, uint8_t n,
                          uint16_t *out);

/**
 * @brief The CRC a commit names: CRC-16/CCITT-FALSE (polynomial 0x1021, seed
 *        0xFFFF, link_crc()) over the 32 CHAN_CFG registers and then the 32
 *        OUTPUTS registers, each as its low byte and then its high byte.
 *
 * The panel computes it over the pages it sent, the IOmcu over what it holds
 * prepared.
 *
 * @param cfg The CHAN_CFG page, LINK_CC_COUNT registers.
 * @param slots The OUTPUTS page, LINK_OS_COUNT registers.
 * @return The CRC-16 value. 0 when @p cfg or @p slots is NULL.
 */
uint16_t out_stage_crc(const uint16_t *cfg, const uint16_t *slots);

/**
 * @brief The same of what @p st holds; 0 for NULL.
 *
 * @param st The staged pages, not changed.
 * @return The CRC of st->cfg and st->slots, as out_stage_crc() gives it.
 */
uint16_t out_stage_held_crc(const out_stage_t *st);

/**
 * @brief What the IOmcu does with a page once the commit has matched.
 *
 * take_cfg and take_slots judge a whole page by the rules of a CHAN_CFG and
 * an OUTPUTS write and put it in force: 0, or a link_nack_t with the page in
 * force unchanged.  put_cfg puts a CHAN_CFG page's registers back without
 * judging them, and makes the silicon agree with the bank again.  keep is
 * called once when both pages are in force.
 *
 * bank is the bank the two takes change, or NULL.  A CHAN_CFG page that
 * changes a role moves that channel's command and output to the new role's
 * rest (outputs_set_role()), so taking the old page again would leave the
 * old role at its rest and not at the command it held.  The commit keeps a
 * copy of the bank's channels from before take_cfg and puts them back ahead
 * of put_cfg when the OUTPUTS page is refused.  Nothing else of the bank is
 * put back: take_slots leaves the slots as they were when it refuses, and
 * the pins reserved may have been brought up to date inside it.
 */
typedef struct {
    /** Judges a CHAN_CFG page and puts it in force, or refuses it. */
    uint8_t (*take_cfg)(void *ctx, const uint16_t *next);
    /** Judges an OUTPUTS page and puts it in force, or refuses it. */
    uint8_t (*take_slots)(void *ctx, const uint16_t *next);
    /** Puts a CHAN_CFG page back, without judging it. */
    void    (*put_cfg)(void *ctx, const uint16_t *prev);
    /** Called once when both pages are in force. May be NULL. */
    void    (*keep)(void *ctx);
    /** The bank the two takes change, or NULL. */
    outputs_t *bank;
} out_stage_ops_t;

/**
 * @brief A write of LINK_BD_COMMIT.
 *
 * LINK_NACK_BAD_VALUE, and nothing is called, when @p crc is not the CRC of
 * what @p st holds.  Otherwise the prepared CHAN_CFG page is taken, then
 * the prepared OUTPUTS page.  A refusal of the first changes nothing; a
 * refusal of the second puts the bank and @p cfg_in_force, the CHAN_CFG
 * page, back as they were before the commit.  Either is returned as the
 * reason.  0 when both are in force.
 *
 * @param st The prepared pages.
 * @param crc The CRC the host named in the commit.
 * @param cfg_in_force The CHAN_CFG page in force, read before the takes.
 * @param ops The callbacks that judge and store the pages. The pointers
 *        take_cfg, take_slots and put_cfg must be set.
 * @param ctx Passed to each callback in @p ops unchanged.
 * @return 0 when both pages are in force. The refusal of take_cfg or
 *         take_slots otherwise. LINK_NACK_BAD_VALUE when @p st, @p cfg_in_force
 *         or @p ops is NULL, when one of the required callbacks is NULL, or
 *         when @p crc does not match.
 */
uint8_t out_stage_commit(const out_stage_t *st, uint16_t crc,
                         const uint16_t *cfg_in_force,
                         const out_stage_ops_t *ops, void *ctx);

#ifdef __cplusplus
}
#endif
