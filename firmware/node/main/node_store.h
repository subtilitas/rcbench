/**
 * @file node_store.h
 * @brief The node's own settings and their store: the name, the INA228's
 *        address setting, the shunt and the shunt both ends last agreed
 *        on, and the language the last panel wrote.  Pure C over a store
 *        of keyed values (NVS on the node, a fake in the host suite),
 *        under the host suite (test_node_store).
 *
 * The values and their defaults.  NAME: "" (the node is named
 * "rcbench-pwr-XXXX" then), up to RCNODE_UNAME_MAX (16) characters that
 * rcnode_uname_ok() takes.  INA228 ADDRESS: RCNODE_I228_AUTO, else a fixed
 * address 0x40 to 0x4F.  SHUNT: RCNODE_SHUNT_DEF_UOHM (200µΩ), 50 to
 * 10,000µΩ.  AGREED: the shunt when none is stored.  LANG: English.  A
 * stored value that is out of range, or a name the rule refuses, reads as
 * its default; a read that fails reads as nothing stored.
 *
 * Two writers.  The node's own SETUP (nst_setup_save()) changes the name,
 * the address and the shunt; a changed shunt keeps AGREED and starts the
 * change's clock, so a panel that links later sees a node-side change
 * (rcnode_shunt_sync()).  A panel's write over the link (nst_panel_op())
 * changes one value; its SHUNT sets the shunt and AGREED to the value
 * written and stops the clock.  Each change is stored at once.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rcnode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The stored values' keys. */
typedef enum {
    NST_K_NAME = 0,  /**< the name, a string of 16 characters at most */
    NST_K_I228,      /**< the address setting, 1 byte */
    NST_K_SHUNT,     /**< the shunt, 2 bytes, µΩ */
    NST_K_AGREED,    /**< the agreed shunt, 2 bytes, µΩ */
    NST_K_LANG,      /**< RCNODE_LANG_*, 1 byte */
    NST_K_COUNT      /**< the number of keys */
} nst_key_t;

/** @brief Where the values are kept. */
typedef struct {
    /** @brief Read a value.  @param ctx ctx.  @param k The key.  @param buf
     *  Receives it.  @param n Its size; a name's buffer holds 17 bytes.
     *  @return false when none is stored or the read failed. */
    bool (*load)(void *ctx, nst_key_t k, void *buf, size_t n);
    /** @brief Store a value.  @param ctx ctx.  @param k The key.  @param buf
     *  The value.  @param n Its size; a name is its characters and a NUL.
     *  @return false when the write failed. */
    bool (*save)(void *ctx, nst_key_t k, const void *buf, size_t n);
    void *ctx;  /**< passed unchanged */
} nst_io_t;

/** @brief What a change touched. @{ */
#define NST_CH_NAME  0x01u  /**< the name */
#define NST_CH_I228  0x02u  /**< the address setting */
#define NST_CH_SHUNT 0x04u  /**< the shunt in force */
#define NST_CH_AGREE 0x08u  /**< the agreed shunt only */
/** @} */

/** @brief The values the node's SETUP edits. */
typedef struct {
    char     name[RCNODE_UNAME_MAX + 1u];  /**< the name, "" none */
    uint8_t  i228;                         /**< RCNODE_I228_AUTO or fixed */
    uint16_t shunt;                        /**< µΩ */
} nst_vals_t;

/** @brief The node's settings.  Members are private but for the tests. */
typedef struct {
    nst_vals_t v;          /**< the values in force */
    uint16_t   agreed;     /**< the shunt both ends last agreed on, µΩ */
    uint8_t    lang;       /**< RCNODE_LANG_* the last panel wrote */
    bool       power;      /**< the role keeps a shunt and an address */
    bool       aged;       /**< the node's own SETUP changed the shunt since
                                the start, and no SHUNT write came after */
    uint32_t   changed_ms; /**< when, the node's ms clock */
    uint32_t   failed;     /**< store writes that failed */
    bool       asked;      /**< the central on the link wrote CONFIG */
} nst_t;

/**
 * @brief The values from the store, each checked; one out of range is its
 *        default (above).
 * @param s     The settings.
 * @param io    The store; NULL reads every value as its default.
 * @param power The power role: a shunt and an address are kept; else they
 *              read as their defaults and nothing writes them.
 */
void nst_load(nst_t *s, const nst_io_t *io, bool power);

/**
 * @brief The node's own SETUP saved: each value that differs is stored.
 *        A changed shunt starts the change's clock and keeps the agreed
 *        shunt.  Values out of range change nothing.
 * @param s      The settings.
 * @param io     The store.
 * @param v      The values.
 * @param now_ms The node's ms clock.
 * @return NST_CH_* of what changed; 0 for nothing or a value refused.
 */
unsigned nst_setup_save(nst_t *s, const nst_io_t *io, const nst_vals_t *v,
                        uint32_t now_ms);

/**
 * @brief A panel's write over the link: NAME, I228 or SHUNT.  SHUNT sets
 *        the shunt and the agreed shunt to its value and stops the change's
 *        clock.  Another operation, and I228 or SHUNT on a role without a
 *        shunt, change nothing.
 * @param s  The settings.
 * @param io The store.
 * @param c  The write, decoded (rcnode_dec_ctl()).
 * @return NST_CH_* of what changed; 0 for nothing.
 */
unsigned nst_panel_op(nst_t *s, const nst_io_t *io, const rcnode_ctl_t *c);

/**
 * @brief LANG from a panel: kept, so the node's own SETUP shows the last
 *        panel's language after a restart.
 * @param s    The settings.
 * @param io   The store.
 * @param lang RCNODE_LANG_*; another value is English.
 * @return true when it changed.
 */
bool nst_lang(nst_t *s, const nst_io_t *io, uint8_t lang);

/**
 * @brief RCNODE_OP_CONFIG on this link, or the link's end: CONFIG goes on
 *        STREAM only between the two, so a central built before CONFIG,
 *        which never writes it, never receives a packet it does not know.
 * @param s  The settings.
 * @param on true for the write, false for the link's end.
 */
void nst_ask(nst_t *s, bool on);

/**
 * @brief Whether a change goes out as CONFIG now.
 * @param s The settings.
 * @return true once the central on the link wrote CONFIG.
 */
bool nst_sends_config(const nst_t *s);

/**
 * @brief The settings as RCNODE_T_CONFIG carries them, the header left 0.
 * @param s      The settings.
 * @param now_ms The node's ms clock.
 * @param out    Receives them.  shunt_age_s is the time since the node's
 *               own change, whole seconds, up to 65,534; RCNODE_AGE_NONE
 *               without one.
 */
void nst_config(const nst_t *s, uint32_t now_ms, rcnode_config_t *out);

#ifdef __cplusplus
}
#endif
