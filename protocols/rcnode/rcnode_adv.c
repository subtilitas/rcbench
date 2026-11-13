/**
 * @file
 * @brief The nodes' advertising: the name, the advertising data and scan
 * response the node sends, and the match the panel's scan runs.
 *
 * SPDX-License-Identifier: MIT
 */

#include "rcnode.h"

#include <string.h>

/** @brief AD (advertising data) types, Core Specification Supplement. @{ */
#define AD_FLAGS      0x01u  /**< flags */
#define AD_UUID128_LIST 0x07u  /**< complete list of 128-bit UUIDs */
#define AD_UUID128_SOME 0x06u  /**< incomplete list of 128-bit UUIDs */
#define AD_NAME_SHORT 0x08u  /**< shortened local name */
#define AD_NAME_FULL  0x09u  /**< complete local name */
#define AD_MAKER      0xFFu  /**< manufacturer specific data */
/** @} */

/** @brief The name's prefix and its length. @{ */
#define NAME_PREFIX     "rcbench-"  /**< what every node name starts with */
#define NAME_PREFIX_LEN 8u  /**< its length */
#define NAME_ROLE_LEN   4u     /**< "pwr-" or "bal-" */
/** @} */

/** @brief The manufacturer data after the company: 'r', 'c', protocol, role. */
#define MAKER_LEN 6u

/**
 * @brief The 3 letters of a role.
 *
 * @param role RCNODE_ROLE_*.
 * @return "pwr" or "bal", NULL for another role.
 */
static const char *role_word(uint8_t role)
{
    switch (role) {
    case RCNODE_ROLE_POWER:   return "pwr";
    case RCNODE_ROLE_BALANCE: return "bal";
    default:                  return NULL;
    }
}

bool rcnode_name(uint8_t role, const uint8_t addr[6], char out[RCNODE_NAME_MAX])
{
    static const char k_hex[] = "0123456789ABCDEF";
    const char *word = role_word(role);
    if (word == NULL || addr == NULL || out == NULL) {
        return false;
    }
    memcpy(out, NAME_PREFIX, NAME_PREFIX_LEN);
    memcpy(out + NAME_PREFIX_LEN, word, 3);
    out[NAME_PREFIX_LEN + 3u] = '-';
    out[12] = k_hex[addr[1] >> 4];
    out[13] = k_hex[addr[1] & 0xFu];
    out[14] = k_hex[addr[0] >> 4];
    out[15] = k_hex[addr[0] & 0xFu];
    out[16] = '\0';
    return true;
}

size_t rcnode_adv_build(uint8_t role, const uint8_t addr[6], uint8_t *out,
                        size_t cap)
{
    char name[RCNODE_NAME_MAX];
    if (out == NULL || cap < RCNODE_ADV_LEN || !rcnode_name(role, addr, name)) {
        return 0u;
    }
    size_t i = 0u;
    out[i++] = 2u;
    out[i++] = AD_FLAGS;
    out[i++] = 0x06u;                    /* LE general discoverable, no BR/EDR */
    out[i++] = (uint8_t)(1u + RCNODE_NAME_MAX - 1u);
    out[i++] = AD_NAME_FULL;
    memcpy(out + i, name, RCNODE_NAME_MAX - 1u);
    i += RCNODE_NAME_MAX - 1u;
    out[i++] = (uint8_t)(1u + MAKER_LEN);
    out[i++] = AD_MAKER;
    out[i++] = (uint8_t)(RCNODE_COMPANY & 0xFFu);
    out[i++] = (uint8_t)(RCNODE_COMPANY >> 8);
    out[i++] = (uint8_t)'r';
    out[i++] = (uint8_t)'c';
    out[i++] = RCNODE_PROTO;
    out[i++] = role;
    return i;
}

size_t rcnode_rsp_build(uint8_t *out, size_t cap)
{
    if (out == NULL || cap < RCNODE_RSP_LEN) {
        return 0u;
    }
    out[0] = 17u;
    out[1] = AD_UUID128_LIST;
    rcnode_uuid(RCNODE_ID_SERVICE, out + 2);
    return RCNODE_RSP_LEN;
}

/**
 * @brief The role in a name, when the name is an rcbench node's.
 *
 * @param s The name's bytes, not NUL-terminated.
 * @param n Their count.
 * @return RCNODE_ROLE_*, 0 for another name.
 */
static uint8_t name_role(const uint8_t *s, size_t n)
{
    if (n < NAME_PREFIX_LEN + NAME_ROLE_LEN
        || memcmp(s, NAME_PREFIX, NAME_PREFIX_LEN) != 0) {
        return 0u;
    }
    const uint8_t *r = s + NAME_PREFIX_LEN;
    if (memcmp(r, "pwr-", NAME_ROLE_LEN) == 0) {
        return RCNODE_ROLE_POWER;
    }
    if (memcmp(r, "bal-", NAME_ROLE_LEN) == 0) {
        return RCNODE_ROLE_BALANCE;
    }
    return 0u;
}

/**
 * @brief Whether a list of 128-bit UUIDs holds the node service.
 *
 * @param s The list's bytes.
 * @param n Their count.
 * @return true when it does.
 */
static bool has_service(const uint8_t *s, size_t n)
{
    uint8_t svc[16];
    rcnode_uuid(RCNODE_ID_SERVICE, svc);
    for (size_t k = 0u; k + 16u <= n; k += 16u) {
        if (memcmp(s + k, svc, 16) == 0) {
            return true;
        }
    }
    return false;
}

bool rcnode_adv_match(const uint8_t *ad, size_t len, uint8_t *role)
{
    if (ad == NULL || role == NULL) {
        return false;
    }
    uint8_t by_maker = 0u;
    uint8_t by_name  = 0u;
    bool    by_svc   = false;
    size_t  i = 0u;
    while (i < len) {
        const size_t l = ad[i];
        if (l == 0u) {
            break;                       /* the rest is padding */
        }
        if (i + 1u + l > len) {
            return false;                /* a length past the end */
        }
        const uint8_t  t = ad[i + 1u];
        const uint8_t *d = ad + i + 2u;
        const size_t   n = l - 1u;
        if (t == AD_MAKER && n == MAKER_LEN
            && d[0] == (RCNODE_COMPANY & 0xFFu) && d[1] == (RCNODE_COMPANY >> 8)
            && d[2] == 'r' && d[3] == 'c'
            && (d[5] == RCNODE_ROLE_POWER || d[5] == RCNODE_ROLE_BALANCE)) {
            by_maker = d[5];
        } else if (t == AD_NAME_FULL || t == AD_NAME_SHORT) {
            by_name = name_role(d, n);
        } else if (t == AD_UUID128_LIST || t == AD_UUID128_SOME) {
            by_svc = by_svc || has_service(d, n);
        }
        i += 1u + l;
    }
    if (by_maker != 0u) {
        *role = by_maker;
        return true;
    }
    if (by_name != 0u) {
        *role = by_name;
        return true;
    }
    if (by_svc) {
        *role = 0u;
        return true;
    }
    return false;
}
