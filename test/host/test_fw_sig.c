/**
 * @file test_fw_sig.c
 * @brief The release signature's policy and its hex form on the Wi-Fi page.
 *
 * The failure under test is an image without a valid release signature that
 * installs with one hold in a build that enforces the signature, or one
 * that needs a second hold in a build that only warns.  The file is built
 * twice: test_fw_sig with the default RCBENCH_SIGN_ENFORCE (0, warn only)
 * and test_fw_sig_enforce with RCBENCH_SIGN_ENFORCE=1, each against the
 * policy source compiled with the same setting.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "fw_sig.h"

/** @brief Every verdict there is. */
static const fw_sig_t k_all[] = {
    FW_SIG_NONE, FW_SIG_OK, FW_SIG_FORM, FW_SIG_KEY, FW_SIG_BAD,
};

/** @brief The count of k_all. */
#define N_ALL (sizeof(k_all) / sizeof(k_all[0]))

TEST_CASE(the_build_setting_is_the_one_compiled_in)
{
#if RCBENCH_SIGN_ENFORCE
    CHECK(fw_sig_enforced());
#else
    CHECK(!fw_sig_enforced());
#endif
}

TEST_CASE(only_an_enforcing_build_asks_for_the_second_hold)
{
    for (size_t i = 0; i < N_ALL; ++i) {
        const fw_sig_t s = k_all[i];
        /* Both policies, whatever the build. */
        CHECK(!fw_sig_needs_ack(s, false));
        CHECK_EQ(fw_sig_needs_ack(s, true), s != FW_SIG_OK);
        /* The build's own. */
#if RCBENCH_SIGN_ENFORCE
        CHECK_EQ(fw_sig_needs_ack(s, fw_sig_enforced()), s != FW_SIG_OK);
#else
        CHECK(!fw_sig_needs_ack(s, fw_sig_enforced()));
#endif
    }
    /* A verdict outside the enum is not a valid signature. */
    CHECK(fw_sig_needs_ack((fw_sig_t)99, true));
}

TEST_CASE(signed_bad_and_none_are_told_apart)
{
    CHECK(fw_sig_signed(FW_SIG_OK));
    CHECK(!fw_sig_bad(FW_SIG_OK));
    CHECK(!fw_sig_signed(FW_SIG_NONE));
    CHECK(!fw_sig_bad(FW_SIG_NONE));
    CHECK(!fw_sig_signed(FW_SIG_FORM));
    CHECK(fw_sig_bad(FW_SIG_FORM));
    CHECK(fw_sig_bad(FW_SIG_KEY));
    CHECK(fw_sig_bad(FW_SIG_BAD));
    CHECK(!fw_sig_signed((fw_sig_t)99));
    for (size_t i = 0; i < N_ALL; ++i) {
        CHECK(strlen(fw_sig_str(k_all[i])) > 0u);
        for (size_t j = i + 1u; j < N_ALL; ++j) {
            CHECK(strcmp(fw_sig_str(k_all[i]), fw_sig_str(k_all[j])) != 0);
        }
    }
    CHECK_STR_EQ(fw_sig_str((fw_sig_t)99), fw_sig_str(FW_SIG_BAD));
}

TEST_CASE(hex_reads_both_cases_at_the_limits)
{
    uint8_t out[FW_SIG_MAX];
    uint32_t n = 99u;
    /* The shortest: 8 bytes, 16 digits. */
    CHECK(fw_sig_from_hex("3006020101020101", 16u, out, &n));
    CHECK_EQ(n, 8u);
    CHECK_EQ(out[0], 0x30u);
    CHECK_EQ(out[7], 0x01u);
    CHECK(fw_sig_from_hex("aBcDeF0123456789", 16u, out, &n));
    CHECK_EQ(out[0], 0xABu);
    CHECK_EQ(out[1], 0xCDu);
    CHECK_EQ(out[2], 0xEFu);
    CHECK_EQ(out[7], 0x89u);
    /* The longest: 72 bytes, 144 digits. */
    char hex[FW_SIG_HEX_MAX + 4u];
    memset(hex, 'f', sizeof(hex));
    CHECK(fw_sig_from_hex(hex, FW_SIG_HEX_MAX, out, &n));
    CHECK_EQ(n, FW_SIG_MAX);
    CHECK_EQ(out[FW_SIG_MAX - 1u], 0xFFu);
}

TEST_CASE(hex_refuses_every_other_form)
{
    uint8_t out[FW_SIG_MAX];
    uint32_t n = 99u;
    char hex[FW_SIG_HEX_MAX + 4u];
    memset(hex, '0', sizeof(hex));
    /* One byte short of the shortest, one past the longest, odd counts. */
    CHECK(!fw_sig_from_hex(hex, 2u * FW_SIG_MIN - 2u, out, &n));
    CHECK_EQ(n, 0u);
    CHECK(!fw_sig_from_hex(hex, FW_SIG_HEX_MAX + 2u, out, &n));
    CHECK(!fw_sig_from_hex(hex, 2u * FW_SIG_MIN + 1u, out, &n));
    CHECK(!fw_sig_from_hex(hex, FW_SIG_HEX_MAX - 1u, out, &n));
    CHECK(!fw_sig_from_hex(hex, 0u, out, &n));
    /* A character that is not a hex digit, in either half of a byte and
     * at either end. */
    static const char *const bad[] = {
        "g006020101020101", "3g06020101020101", "300602010102010G",
        "30060201010201 1", "0x06020101020101", "3006020101020101"
    };
    for (size_t i = 0; i + 1u < sizeof(bad) / sizeof(bad[0]); ++i) {
        n = 99u;
        CHECK(!fw_sig_from_hex(bad[i], 16u, out, &n));
        CHECK_EQ(n, 0u);
    }
    /* The characters next to the digit ranges. */
    static const char edges[] = { '/', ':', '@', 'G', '`', 'g' };
    for (size_t i = 0; i < sizeof(edges); ++i) {
        char t[17];
        memcpy(t, bad[5], 17u);
        t[3] = edges[i];
        CHECK(!fw_sig_from_hex(t, 16u, out, &n));
    }
    CHECK(fw_sig_from_hex(bad[5], 16u, out, &n));
    CHECK(!fw_sig_from_hex(NULL, 16u, out, &n));
    CHECK(!fw_sig_from_hex(bad[5], 16u, NULL, &n));
    CHECK(!fw_sig_from_hex(bad[5], 16u, out, NULL));
}

int main(void)
{
    RUN(the_build_setting_is_the_one_compiled_in);
    RUN(only_an_enforcing_build_asks_for_the_second_hold);
    RUN(signed_bad_and_none_are_told_apart);
    RUN(hex_reads_both_cases_at_the_limits);
    RUN(hex_refuses_every_other_form);
    return test_summary(RCBENCH_SIGN_ENFORCE ? "fw_sig_enforce" : "fw_sig");
}
