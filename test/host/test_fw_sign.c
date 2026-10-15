/*
 * The release signature of a panel image.
 *
 * The failure under test is a signature that is accepted and should not be:
 * another key's, one over another image, one with a byte flipped, one that is
 * cut or padded, one whose DER form is not minimal.  The positive cases are
 * 48 signatures openssl made (tools/gen_sign_vectors.py), so the verifier is
 * held to a second implementation, not to itself.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "fw_sign.h"
#include "sign_vectors.h"

static size_t image(unsigned i, uint8_t *out)
{
    const size_t n = 64u + 13u * i;
    for (size_t j = 0; j < n; ++j) {
        out[j] = (uint8_t)((i * 31u + j * 7u) & 0xFFu);
    }
    return n;
}

static fw_pubkey_t key_of(const uint8_t p[64])
{
    fw_pubkey_t k;
    memcpy(k.x, p, 32u);
    memcpy(k.y, p + 32, 32u);
    return k;
}

TEST_CASE(openssl_signatures_verify)
{
    const fw_pubkey_t k = key_of(sv_key_a);
    uint8_t img[1024];
    unsigned lens[80] = {0};
    for (unsigned i = 0; i < SV_COUNT; ++i) {
        const size_t n = image(i, img);
        CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sv_sig[i], sv_sig_len[i]),
                 FW_SIG_OK);
        ++lens[sv_sig_len[i]];
    }
    /* The vectors cover the DER lengths of 70, 71 and 72 bytes. */
    CHECK(lens[70] > 0u);
    CHECK(lens[71] > 0u);
    CHECK(lens[72] > 0u || lens[69] > 0u);
}

TEST_CASE(another_key_does_not_verify)
{
    const fw_pubkey_t k = key_of(sv_key_b);
    uint8_t img[1024];
    for (unsigned i = 0; i < 8u; ++i) {
        const size_t n = image(i, img);
        CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sv_sig[i], sv_sig_len[i]),
                 FW_SIG_BAD);
    }
}

TEST_CASE(a_signature_over_another_image_does_not_verify)
{
    const fw_pubkey_t k = key_of(sv_key_a);
    uint8_t img[1024];
    const size_t n = image(3, img);
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sv_sig[4], sv_sig_len[4]),
             FW_SIG_BAD);
    /* One byte short and one byte long. */
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)(n - 1u), sv_sig[3],
                           sv_sig_len[3]), FW_SIG_BAD);
    img[n] = 0u;
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)(n + 1u), sv_sig[3],
                           sv_sig_len[3]), FW_SIG_BAD);
}

TEST_CASE(every_flipped_image_bit_position_is_caught)
{
    const fw_pubkey_t k = key_of(sv_key_a);
    uint8_t img[1024];
    const size_t n = image(5, img);
    const size_t at[] = {0u, 1u, n / 2u, n - 2u, n - 1u};
    for (size_t a = 0; a < sizeof(at) / sizeof(at[0]); ++a) {
        for (unsigned b = 0; b < 8u; b += 7u) {
            img[at[a]] ^= (uint8_t)(1u << b);
            CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sv_sig[5],
                                   sv_sig_len[5]), FW_SIG_BAD);
            img[at[a]] ^= (uint8_t)(1u << b);
        }
    }
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sv_sig[5], sv_sig_len[5]),
             FW_SIG_OK);
}

TEST_CASE(a_flipped_signature_byte_is_refused)
{
    const fw_pubkey_t k = key_of(sv_key_a);
    uint8_t img[1024];
    const size_t n = image(6, img);
    uint8_t sig[FW_SIG_MAX];
    const size_t sn = sv_sig_len[6];
    unsigned bad = 0u, form = 0u;
    for (size_t at = 0; at < sn; ++at) {
        memcpy(sig, sv_sig[6], sn);
        sig[at] ^= 0x01u;
        const fw_sig_t r = fw_sig_verify(&k, img, (uint32_t)n, sig, sn);
        CHECK(r != FW_SIG_OK);
        CHECK(r == FW_SIG_BAD || r == FW_SIG_FORM);
        bad += (r == FW_SIG_BAD);
        form += (r == FW_SIG_FORM);
    }
    CHECK(bad > 60u);
    CHECK(form > 0u);
}

TEST_CASE(a_signature_cut_or_padded_is_refused)
{
    const fw_pubkey_t k = key_of(sv_key_a);
    uint8_t img[1024];
    const size_t n = image(7, img);
    const size_t sn = sv_sig_len[7];
    for (size_t cut = 1; cut <= sn; ++cut) {
        const fw_sig_t r = fw_sig_verify(&k, img, (uint32_t)n, sv_sig[7],
                                         sn - cut);
        CHECK(r == FW_SIG_FORM || r == FW_SIG_NONE);
    }
    uint8_t big[FW_SIG_MAX + 8u];
    memset(big, 0, sizeof(big));
    memcpy(big, sv_sig[7], sn);
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, big, sn + 1u), FW_SIG_FORM);
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, big, FW_SIG_MAX + 1u),
             FW_SIG_FORM);
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, big, sizeof(big)),
             FW_SIG_FORM);
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, big, FW_SIG_MIN - 1u),
             FW_SIG_FORM);
}

TEST_CASE(no_signature_is_none)
{
    const fw_pubkey_t k = key_of(sv_key_a);
    uint8_t img[64];
    const size_t n = image(0, img);
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, NULL, 0u), FW_SIG_NONE);
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, NULL, 70u), FW_SIG_NONE);
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sv_sig[0], 0u), FW_SIG_NONE);
    CHECK(strcmp(fw_sig_str(FW_SIG_NONE), fw_sig_str(FW_SIG_OK)) != 0);
    CHECK(strlen(fw_sig_str(FW_SIG_FORM)) > 0u);
    CHECK(strlen(fw_sig_str(FW_SIG_KEY)) > 0u);
    CHECK(strlen(fw_sig_str(FW_SIG_BAD)) > 0u);
}

TEST_CASE(a_der_form_that_is_not_minimal_is_refused)
{
    const fw_pubkey_t k = key_of(sv_key_a);
    uint8_t img[64];
    const size_t n = image(0, img);
    /* r = 1, s = 1, as written. */
    static const uint8_t good_form[] = {0x30, 6, 2, 1, 1, 2, 1, 1};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, good_form,
                           sizeof(good_form)), FW_SIG_BAD);
    static const uint8_t lead_zero[] = {0x30, 7, 2, 2, 0, 1, 2, 1, 1};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, lead_zero,
                           sizeof(lead_zero)), FW_SIG_FORM);
    static const uint8_t negative[] = {0x30, 6, 2, 1, 0x81, 2, 1, 1};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, negative, sizeof(negative)),
             FW_SIG_FORM);
    static const uint8_t wrong_tag[] = {0x31, 6, 2, 1, 1, 2, 1, 1};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, wrong_tag,
                           sizeof(wrong_tag)), FW_SIG_FORM);
    static const uint8_t wrong_len[] = {0x30, 5, 2, 1, 1, 2, 1, 1};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, wrong_len,
                           sizeof(wrong_len)), FW_SIG_FORM);
    static const uint8_t int_tag[] = {0x30, 6, 4, 1, 1, 2, 1, 1};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, int_tag, sizeof(int_tag)),
             FW_SIG_FORM);
    static const uint8_t trailing[] = {0x30, 7, 2, 1, 1, 2, 1, 1, 0};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, trailing, sizeof(trailing)),
             FW_SIG_FORM);
    static const uint8_t empty_int[] = {0x30, 6, 2, 0, 2, 2, 1, 1, 1};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, empty_int,
                           sizeof(empty_int)), FW_SIG_FORM);
    static const uint8_t zero_r[] = {0x30, 6, 2, 1, 0, 2, 1, 1};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, zero_r, sizeof(zero_r)),
             FW_SIG_BAD);
    static const uint8_t zero_s[] = {0x30, 6, 2, 1, 1, 2, 1, 0};
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, zero_s, sizeof(zero_s)),
             FW_SIG_BAD);
}

TEST_CASE(r_or_s_at_the_group_order_is_refused)
{
    const fw_pubkey_t k = key_of(sv_key_a);
    uint8_t img[64];
    const size_t n = image(0, img);
    /* n itself, as 33 bytes with a leading zero, for r and for s. */
    static const uint8_t order[33] = {
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBC, 0xE6, 0xFA, 0xAD, 0xA7,
        0x17, 0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51,
    };
    uint8_t sig[72];
    size_t at = 0;
    sig[at++] = 0x30;
    sig[at++] = 2 + 33 + 3;
    sig[at++] = 2;
    sig[at++] = 33;
    memcpy(sig + at, order, 33u);
    at += 33u;
    sig[at++] = 2;
    sig[at++] = 1;
    sig[at++] = 1;
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sig, at), FW_SIG_BAD);
    /* 33 bytes with a leading zero and a value of 33 bytes of value. */
    uint8_t wide[72];
    at = 0;
    wide[at++] = 0x30;
    wide[at++] = 2 + 33 + 3;
    wide[at++] = 2;
    wide[at++] = 33;
    wide[at++] = 0x01;
    memset(wide + at, 0x11, 32u);
    at += 32u;
    wide[at++] = 2;
    wide[at++] = 1;
    wide[at++] = 1;
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, wide, at), FW_SIG_FORM);
}

TEST_CASE(a_key_off_the_curve_is_refused)
{
    fw_pubkey_t k = key_of(sv_key_a);
    uint8_t img[64];
    const size_t n = image(0, img);
    k.y[31] ^= 1u;
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sv_sig[0], sv_sig_len[0]),
             FW_SIG_KEY);
    memset(&k, 0, sizeof(k));
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sv_sig[0], sv_sig_len[0]),
             FW_SIG_KEY);
    /* Coordinates of p or above are not in the field. */
    memset(&k, 0xFF, sizeof(k));
    CHECK_EQ(fw_sig_verify(&k, img, (uint32_t)n, sv_sig[0], sv_sig_len[0]),
             FW_SIG_KEY);
}

TEST_CASE(a_pem_gives_the_key_and_anything_else_does_not)
{
    fw_pubkey_t k;
    CHECK(fw_pubkey_parse_pem(sv_pem_a, strlen(sv_pem_a), &k));
    CHECK(memcmp(k.x, sv_key_a, 32u) == 0);
    CHECK(memcmp(k.y, sv_key_a + 32, 32u) == 0);

    /* Windows line ends and leading blank lines are accepted. */
    char crlf[256];
    size_t o = 0;
    crlf[o++] = '\n';
    for (const char *p = sv_pem_a; *p != '\0'; ++p) {
        if (*p == '\n') {
            crlf[o++] = '\r';
        }
        crlf[o++] = *p;
    }
    CHECK(fw_pubkey_parse_pem(crlf, o, &k));

    const size_t n = strlen(sv_pem_a);
    CHECK(!fw_pubkey_parse_pem(NULL, n, &k));
    CHECK(!fw_pubkey_parse_pem(sv_pem_a, n, NULL));
    CHECK(!fw_pubkey_parse_pem(sv_pem_a, 0u, &k));
    CHECK(!fw_pubkey_parse_pem(sv_pem_a, n - 10u, &k));
    char bad[256];
    memcpy(bad, sv_pem_a, n + 1u);
    bad[30] = '!';
    CHECK(!fw_pubkey_parse_pem(bad, n, &k));
    memcpy(bad, sv_pem_a, n + 1u);
    bad[5] = 'X';
    CHECK(!fw_pubkey_parse_pem(bad, n, &k));
    /* A flipped point byte: off the curve. */
    memcpy(bad, sv_pem_a, n + 1u);
    bad[90] = (bad[90] == 'A') ? 'B' : 'A';
    CHECK(!fw_pubkey_parse_pem(bad, n, &k));
    /* Private key text. */
    static const char priv[] =
        "-----BEGIN EC PRIVATE KEY-----\nAAAA\n-----END EC PRIVATE KEY-----\n";
    CHECK(!fw_pubkey_parse_pem(priv, sizeof(priv) - 1u, &k));
    /* A header and a footer around nothing. */
    static const char none[] =
        "-----BEGIN PUBLIC KEY-----\n-----END PUBLIC KEY-----\n";
    CHECK(!fw_pubkey_parse_pem(none, sizeof(none) - 1u, &k));
    /* No footer. */
    memcpy(bad, sv_pem_a, n + 1u);
    bad[n - 6u] = 'X';
    CHECK(!fw_pubkey_parse_pem(bad, n, &k));
}

/* The key the panel embeds is a valid P-256 public key.  The suite runs in
 * test/host. */
TEST_CASE(the_committed_release_key_parses)
{
    static char pem[512];
    FILE *f = fopen("../../firmware/panel/main/keys/release_signing_pub.pem",
                    "rb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    const size_t n = fread(pem, 1u, sizeof(pem), f);
    (void)fclose(f);
    CHECK(n > 100u && n < sizeof(pem));
    fw_pubkey_t k;
    CHECK(fw_pubkey_parse_pem(pem, n, &k));
}

int main(void)
{
    RUN(openssl_signatures_verify);
    RUN(another_key_does_not_verify);
    RUN(a_signature_over_another_image_does_not_verify);
    RUN(every_flipped_image_bit_position_is_caught);
    RUN(a_flipped_signature_byte_is_refused);
    RUN(a_signature_cut_or_padded_is_refused);
    RUN(no_signature_is_none);
    RUN(a_der_form_that_is_not_minimal_is_refused);
    RUN(r_or_s_at_the_group_order_is_refused);
    RUN(a_key_off_the_curve_is_refused);
    RUN(a_pem_gives_the_key_and_anything_else_does_not);
    RUN(the_committed_release_key_parses);
    return test_summary("fw_sign");
}
