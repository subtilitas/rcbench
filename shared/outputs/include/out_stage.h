/**
 * @file
 * @brief A binding prepared beside the one in force, and taken whole.
 *
 * The IOmcu's half of the BIND_CFG, BIND_OUT and BIND pages (protocol
 * 4.10; see link_pages.h).  A CHAN_CFG and an OUTPUTS page are written here
 * a frame at a time and change nothing; a commit that names their CRC
 * (cyclic redundancy check) puts both in force or neither.
 * From protocol 4.41 BIND_CFG_HI and BIND_OUT_HI prepare the _HI halves
 * (channels 9 to 16, slots 9 to 16) beside them, and a commit names either
 * the first two pages or all four.
 *
 * The rules that judge a page in force are the IOmcu's and need its
 * bank and its silicon, so the commit calls them through out_stage_ops_t.
 * The order, the comparison and what is put back after a refusal are here
 * and host-tested.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The CHAN_CFG and OUTPUTS pages prepared beside the bank in force,
 *         each with its _HI half after it (protocol 4.41). */
typedef struct {
    uint16_t cfg[LINK_CC_ALL];    /**< BIND_CFG, then BIND_CFG_HI */
    uint16_t slots[LINK_OS_ALL];  /**< BIND_OUT, then BIND_OUT_HI */
} out_stage_t;

/**
 * @brief Start from the pages in force; NULL starts from the pages' defaults.
 *
 * Does nothing when @p st is NULL.
 *
 * @param st The staged pages, overwritten.
 * @param cfg The CHAN_CFG pages in force (LINK_CC_ALL registers), or NULL.
 * @param slots The OUTPUTS pages in force (LINK_OS_ALL registers), or NULL.
 */
void out_stage_init(out_stage_t *st, const uint16_t *cfg,
                    const uint16_t *slots);

/**
 * @brief A window of BIND_CFG, or with @p hi of BIND_CFG_HI, written: 0, or
 *        the link_nack_t outputs_chan_cfg_write() gives for the same window,
 *        and then nothing of it is stored.
 *
 * @param st The staged pages.
 * @param hi true for BIND_CFG_HI.
 * @param off First register of the window.
 * @param n Number of registers in the window.
 * @param in The @p n register values to write.
 * @return 0 when stored. LINK_NACK_BAD_RANGE when @p st is NULL, otherwise the
 *         refusal of outputs_chan_cfg_write() for the window.
 */
uint8_t out_stage_cfg_write(out_stage_t *st, bool hi, uint8_t off,
                            uint8_t n, const uint16_t *in);

/**
 * @brief A window of BIND_OUT, or with @p hi of BIND_OUT_HI, written: 0, or
 *        the link_nack_t outputs_slots_write() gives for the same window, and
 *        then nothing of it is stored.
 *
 * @param st The staged pages.
 * @param hi true for BIND_OUT_HI.
 * @param off First register of the window.
 * @param n Number of registers in the window.
 * @param in The @p n register values to write.
 * @return 0 when stored. LINK_NACK_BAD_RANGE when @p st is NULL, otherwise the
 *         refusal of outputs_slots_write() for the window.
 */
uint8_t out_stage_slots_write(out_stage_t *st, bool hi, uint8_t off,
                              uint8_t n, const uint16_t *in);

/**
 * @brief Copies a window of the prepared CHAN_CFG page, or with @p hi of the
 *        prepared CHAN_CFG_HI page.
 *
 * Registers at or past LINK_CC_COUNT read as 0. Nothing is done when @p st or
 * @p out is NULL.
 *
 * @param st The staged pages, not changed.
 * @param hi true for the _HI page.
 * @param off First register of the window.
 * @param n Number of registers to copy.
 * @param out Receives the @p n register values.
 */
void out_stage_cfg_read(const out_stage_t *st, bool hi, uint8_t off,
                        uint8_t n, uint16_t *out);

/**
 * @brief Copies a window of the prepared OUTPUTS page, or with @p hi of the
 *        prepared OUTPUTS_HI page.
 *
 * Registers at or past LINK_OS_COUNT read as 0. Nothing is done when @p st or
 * @p out is NULL.
 *
 * @param st The staged pages, not changed.
 * @param hi true for the _HI page.
 * @param off First register of the window.
 * @param n Number of registers to copy.
 * @param out Receives the @p n register values.
 */
void out_stage_slots_read(const out_stage_t *st, bool hi, uint8_t off,
                          uint8_t n, uint16_t *out);

/**
 * @brief The CRC LINK_BD_COMMIT names: CRC-16/CCITT-FALSE (polynomial 0x1021,
 *        seed 0xFFFF, link_crc()) over the 32 CHAN_CFG registers and then the
 *        32 OUTPUTS registers, each as its low byte and then its high byte.
 *
 * The panel computes it over the pages it sent, the IOmcu over what it holds
 * prepared.  The _HI halves of @p cfg and @p slots are not read.
 *
 * @param cfg The CHAN_CFG page, at least LINK_CC_COUNT registers.
 * @param slots The OUTPUTS page, at least LINK_OS_COUNT registers.
 * @return The CRC-16 value. 0 when @p cfg or @p slots is NULL.
 */
uint16_t out_stage_crc(const uint16_t *cfg, const uint16_t *slots);

/**
 * @brief The CRC LINK_BD_COMMIT_ALL names (protocol 4.41): out_stage_crc()
 *        carried on over the 32 CHAN_CFG_HI registers and then the 32
 *        OUTPUTS_HI registers, 128 registers in all.
 *
 * @param cfg CHAN_CFG and CHAN_CFG_HI, LINK_CC_ALL registers.
 * @param slots OUTPUTS and OUTPUTS_HI, LINK_OS_ALL registers.
 * @return The CRC-16 value. 0 when @p cfg or @p slots is NULL.
 */
uint16_t out_stage_crc_all(const uint16_t *cfg, const uint16_t *slots);

/**
 * @brief out_stage_crc() of what @p st holds; 0 for NULL.
 *
 * @param st The staged pages, not changed.
 * @return The CRC of the first halves of st->cfg and st->slots.
 */
uint16_t out_stage_held_crc(const out_stage_t *st);

/**
 * @brief out_stage_crc_all() of what @p st holds; 0 for NULL.
 *
 * @param st The staged pages, not changed.
 * @return The CRC of all of st->cfg and st->slots.
 */
uint16_t out_stage_held_crc_all(const out_stage_t *st);

/**
 * @brief What the IOmcu does with the pages once the commit has matched.
 *
 * take_cfg and take_slots judge both halves by the rules of a CHAN_CFG and
 * an OUTPUTS write and put them in force: 0, or a link_nack_t with the pages
 * in force unchanged.  take_cfg is handed the OUTPUTS pages the commit puts
 * in force beside it.  put_cfg puts the CHAN_CFG pages' registers back
 * without judging them, and makes the silicon agree with the bank again.
 * keep is called once when both are in force.
 *
 * bank is the bank the two takes change, or NULL.  A CHAN_CFG page that
 * changes a role moves that channel's command and output to the new role's
 * rest (outputs_set_role()), so taking the old page again would leave the
 * old role at its rest and not at the command it held.  The commit keeps a
 * copy of the bank's channels from before take_cfg and puts them back ahead
 * of put_cfg when the OUTPUTS pages are refused.  Nothing else of the bank
 * is put back: take_slots leaves the slots as they were when it refuses,
 * and the pins reserved may have been brought up to date inside it.
 */
typedef struct {
    /** @brief Judges the CHAN_CFG pages (LINK_CC_ALL registers) beside the
     *         OUTPUTS pages (LINK_OS_ALL) and puts them in force, or
     *         refuses them. */
    uint8_t (*take_cfg)(void *ctx, const uint16_t *next,
                        const uint16_t *slots);
    /** @brief Judges the OUTPUTS pages and puts them in force, or refuses
     *         them. */
    uint8_t (*take_slots)(void *ctx, const uint16_t *next);
    /** @brief Puts the CHAN_CFG pages back, without judging them. */
    void    (*put_cfg)(void *ctx, const uint16_t *prev);
    /** @brief Called once when both pages are in force.
     *
     * May be NULL.
     */
    void    (*keep)(void *ctx);
    /** @brief The bank the two takes change, or NULL. */
    outputs_t *bank;
} out_stage_ops_t;

/**
 * @brief A write of LINK_BD_COMMIT: the prepared CHAN_CFG and OUTPUTS pages,
 *        with CHAN_CFG_HI and OUTPUTS_HI as they are in force.
 *
 * LINK_NACK_BAD_VALUE, and nothing is called, when @p crc is not
 * out_stage_held_crc() of @p st.  Otherwise the CHAN_CFG pages are taken,
 * then the OUTPUTS pages, each judged whole.  A refusal of the first changes
 * nothing; a refusal of the second puts the bank and @p cfg_in_force back as
 * they were before the commit.  Either is returned as the reason.  0 when
 * both are in force.  What @p st holds prepared for the _HI halves is not
 * read.
 *
 * @param st The prepared pages.
 * @param crc The CRC the host named in the commit.
 * @param cfg_in_force The CHAN_CFG pages in force, LINK_CC_ALL registers.
 * @param slots_in_force The OUTPUTS pages in force, LINK_OS_ALL registers.
 * @param ops The callbacks that judge and store the pages. The pointers
 *        take_cfg, take_slots and put_cfg must be set.
 * @param ctx Passed to each callback in @p ops unchanged.
 * @return 0 when both are in force. The refusal of take_cfg or take_slots
 *         otherwise. LINK_NACK_BAD_VALUE when a pointer or a required callback
 *         is NULL, or when @p crc does not match.
 */
uint8_t out_stage_commit(const out_stage_t *st, uint16_t crc,
                         const uint16_t *cfg_in_force,
                         const uint16_t *slots_in_force,
                         const out_stage_ops_t *ops, void *ctx);

/**
 * @brief A write of LINK_BD_COMMIT_ALL (protocol 4.41): all four prepared
 *        pages, as out_stage_commit() takes two.
 *
 * LINK_NACK_BAD_VALUE, and nothing is called, when @p crc is not
 * out_stage_held_crc_all() of @p st.
 *
 * @param st The prepared pages.
 * @param crc The CRC the host named in the commit.
 * @param cfg_in_force The CHAN_CFG pages in force, LINK_CC_ALL registers.
 * @param slots_in_force The OUTPUTS pages in force, LINK_OS_ALL registers.
 * @param ops The callbacks, as out_stage_commit().
 * @param ctx Passed to each callback in @p ops unchanged.
 * @return As out_stage_commit().
 */
uint8_t out_stage_commit_all(const out_stage_t *st, uint16_t crc,
                             const uint16_t *cfg_in_force,
                             const uint16_t *slots_in_force,
                             const out_stage_ops_t *ops, void *ctx);

#ifdef __cplusplus
}
#endif
