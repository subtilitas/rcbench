/*
 * A binding prepared beside the one in force, and taken whole.
 *
 * The coprocessor's half of the BIND_CFG, BIND_OUT and BIND pages (protocol
 * 4.10; see link_pages.h).  A CHAN_CFG and an OUTPUTS page are written here
 * a frame at a time and change nothing; a commit that names their CRC
 * (cyclic redundancy check) puts both in force or neither.
 *
 * The rules that judge a page in force are the coprocessor's and need its
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

typedef struct {
    uint16_t cfg[LINK_CC_COUNT];    /**< the CHAN_CFG page prepared */
    uint16_t slots[LINK_OS_COUNT];  /**< the OUTPUTS page prepared  */
} out_stage_t;

/** Start from the pages in force; NULL starts from the pages' defaults. */
void out_stage_init(out_stage_t *st, const uint16_t *cfg,
                    const uint16_t *slots);

/**
 * A window of BIND_CFG or BIND_OUT written: 0, or the link_nack_t
 * outputs_chan_cfg_write() and outputs_slots_write() give for the same
 * window, and then nothing of it is stored.
 */
uint8_t out_stage_cfg_write(out_stage_t *st, uint8_t off, uint8_t n,
                            const uint16_t *in);
uint8_t out_stage_slots_write(out_stage_t *st, uint8_t off, uint8_t n,
                              const uint16_t *in);

void out_stage_cfg_read(const out_stage_t *st, uint8_t off, uint8_t n,
                        uint16_t *out);
void out_stage_slots_read(const out_stage_t *st, uint8_t off, uint8_t n,
                          uint16_t *out);

/**
 * The CRC a commit names: CRC-16/CCITT-FALSE (polynomial 0x1021, seed
 * 0xFFFF, link_crc()) over the 32 CHAN_CFG registers and then the 32
 * OUTPUTS registers, each as its low byte and then its high byte.  The
 * panel computes it over the pages it sent, the coprocessor over what it
 * holds prepared.
 */
uint16_t out_stage_crc(const uint16_t *cfg, const uint16_t *slots);

/** The same of what @p st holds; 0 for NULL. */
uint16_t out_stage_held_crc(const out_stage_t *st);

/**
 * What the coprocessor does with a page once the commit has matched.
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
 * copy of the bank from before take_cfg and puts the whole of it back
 * ahead of put_cfg when the OUTPUTS page is refused.
 */
typedef struct {
    uint8_t (*take_cfg)(void *ctx, const uint16_t *next);
    uint8_t (*take_slots)(void *ctx, const uint16_t *next);
    void    (*put_cfg)(void *ctx, const uint16_t *prev);
    void    (*keep)(void *ctx);
    outputs_t *bank;
} out_stage_ops_t;

/**
 * A write of LINK_BD_COMMIT.
 *
 * LINK_NACK_BAD_VALUE, and nothing is called, when @p crc is not the CRC of
 * what @p st holds.  Otherwise the prepared CHAN_CFG page is taken, then
 * the prepared OUTPUTS page.  A refusal of the first changes nothing; a
 * refusal of the second puts the bank and @p cfg_in_force, the CHAN_CFG
 * page, back as they were before the commit.  Either is returned as the
 * reason.  0 when both are in force.
 */
uint8_t out_stage_commit(const out_stage_t *st, uint16_t crc,
                         const uint16_t *cfg_in_force,
                         const out_stage_ops_t *ops, void *ctx);

#ifdef __cplusplus
}
#endif
