/**
 * @file fw_sig.c
 * @brief The verdict on a release signature, the policy and the hex form.
 *        See fw_sig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fw_sig.h"

bool fw_sig_enforced(void)
{
    return RCBENCH_SIGN_ENFORCE != 0;
}

bool fw_sig_needs_ack(fw_sig_t s, bool enforce)
{
    return enforce && s != FW_SIG_OK;
}

bool fw_sig_signed(fw_sig_t s)
{
    return s == FW_SIG_OK;
}

bool fw_sig_bad(fw_sig_t s)
{
    return s == FW_SIG_FORM || s == FW_SIG_KEY || s == FW_SIG_BAD;
}

/**
 * @brief The value of hex digit @p c.
 * @param c the character
 * @return 0 to 15, or -1 for a character that is not a hex digit
 */
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool fw_sig_from_hex(const char *hex, size_t n, uint8_t out[FW_SIG_MAX],
                     uint32_t *out_n)
{
    if (out_n == NULL) {
        return false;
    }
    *out_n = 0u;
    if (hex == NULL || out == NULL || (n % 2u) != 0u
        || n < (size_t)FW_SIG_MIN * 2u || n > (size_t)FW_SIG_HEX_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i += 2u) {
        const int hi = hex_val(hex[i]);
        const int lo = hex_val(hex[i + 1u]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i / 2u] = (uint8_t)((hi << 4) | lo);
    }
    *out_n = (uint32_t)(n / 2u);
    return true;
}

const char *fw_sig_str(fw_sig_t s)
{
    switch (s) {
    case FW_SIG_OK:   return "signed by the release key";
    case FW_SIG_NONE: return "no signature";
    case FW_SIG_FORM: return "signature is not a P-256 DER signature";
    case FW_SIG_KEY:  return "release key is not a P-256 public key";
    case FW_SIG_BAD:
    default:          return "signature does not verify";
    }
}
