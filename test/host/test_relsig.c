/*
 * The release signature in its raw form (shared/relsig/relsig.c): the 48
 * openssl signatures of sign_vectors.h turned from DER into r then s and
 * back, and verified over each image's SHA-256 with the vectors' public
 * key.  Another key, another image, a flipped byte, an r or s of zero, a
 * P-384 key and a DER form that is cut or padded do not verify.
 *
 * The keys are the vectors' throw-away keys; no release key is read.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "fw_sha256.h"
#include "relsig.h"
#include "sign_vectors.h"

/** @brief Image @p i of the vectors (tools/gen_sign_vectors.py).
 *  @param i its number @param out its bytes @return its length */
static size_t image(unsigned i, uint8_t *out)
{
    const size_t n = 64u + 13u * i;
    for (size_t j = 0; j < n; ++j) {
        out[j] = (uint8_t)((i * 31u + j * 7u) & 0xFFu);
    }
    return n;
}

/** @brief The SHA-256 of image @p i.  @param i its number
 *  @param sha receives it */
static void image_sha(unsigned i, uint8_t sha[32])
{
    uint8_t img[1024];
    const size_t n = image(i, img);
    fw_sha256(img, n, sha);
}

TEST_CASE(every_vector_verifies_in_its_raw_form)
{
    for (unsigned i = 0; i < SV_COUNT; ++i) {
        uint8_t sha[32];
        uint8_t raw[RELSIG_RAW_LEN];
        image_sha(i, sha);
        CHECK(relsig_der_to_raw(sv_sig[i], sv_sig_len[i], raw));
        CHECK(relsig_verify(sv_pem_a, sizeof sv_pem_a, sha, raw));
    }
}

TEST_CASE(raw_and_der_round_trip)
{
    for (unsigned i = 0; i < SV_COUNT; ++i) {
        uint8_t raw[RELSIG_RAW_LEN];
        uint8_t der[RELSIG_DER_MAX];
        size_t n = 0u;
        CHECK(relsig_der_to_raw(sv_sig[i], sv_sig_len[i], raw));
        CHECK(relsig_raw_to_der(raw, der, &n));
        CHECK_EQ(n, (size_t)sv_sig_len[i]);
        CHECK(memcmp(der, sv_sig[i], n) == 0);
    }
}

TEST_CASE(another_key_image_or_byte_does_not_verify)
{
    uint8_t sha[32];
    uint8_t raw[RELSIG_RAW_LEN];
    image_sha(0u, sha);
    CHECK(relsig_der_to_raw(sv_sig[0], sv_sig_len[0], raw));
    CHECK(!relsig_verify(sv_pem_b, sizeof sv_pem_b, sha, raw));
    CHECK(!relsig_verify(sv_pem_p384, sizeof sv_pem_p384, sha, raw));
    uint8_t other[32];
    image_sha(1u, other);
    CHECK(!relsig_verify(sv_pem_a, sizeof sv_pem_a, other, raw));
    for (unsigned k = 0; k < RELSIG_RAW_LEN; k += 7u) {
        uint8_t bad[RELSIG_RAW_LEN];
        memcpy(bad, raw, sizeof bad);
        bad[k] ^= 0x01u;
        CHECK(!relsig_verify(sv_pem_a, sizeof sv_pem_a, sha, bad));
    }
    sha[31] ^= 0x80u;
    CHECK(!relsig_verify(sv_pem_a, sizeof sv_pem_a, sha, raw));
}

TEST_CASE(zero_halves_and_broken_forms_are_refused)
{
    uint8_t sha[32];
    uint8_t raw[RELSIG_RAW_LEN];
    uint8_t der[RELSIG_DER_MAX + 1u];
    size_t n = 0u;
    image_sha(0u, sha);
    CHECK(relsig_der_to_raw(sv_sig[0], sv_sig_len[0], raw));
    uint8_t zero_r[RELSIG_RAW_LEN];
    memcpy(zero_r, raw, sizeof zero_r);
    memset(zero_r, 0, RELSIG_RAW_LEN / 2u);
    CHECK(!relsig_raw_to_der(zero_r, der, &n));
    CHECK(!relsig_verify(sv_pem_a, sizeof sv_pem_a, sha, zero_r));
    uint8_t zero_s[RELSIG_RAW_LEN];
    memcpy(zero_s, raw, sizeof zero_s);
    memset(zero_s + RELSIG_RAW_LEN / 2u, 0, RELSIG_RAW_LEN / 2u);
    CHECK(!relsig_verify(sv_pem_a, sizeof sv_pem_a, sha, zero_s));
    /* Cut by a byte, or one byte after the form. */
    CHECK(!relsig_der_to_raw(sv_sig[0], sv_sig_len[0] - 1u, raw));
    memcpy(der, sv_sig[0], sv_sig_len[0]);
    der[sv_sig_len[0]] = 0u;
    CHECK(!relsig_der_to_raw(der, sv_sig_len[0] + 1u, raw));
    CHECK(!relsig_der_to_raw(NULL, 8u, raw));
    CHECK(!relsig_verify(NULL, 0u, sha, raw));
    CHECK(!relsig_verify(sv_pem_a, 0u, sha, raw));
}

int main(void)
{
    RUN(every_vector_verifies_in_its_raw_form);
    RUN(raw_and_der_round_trip);
    RUN(another_key_image_or_byte_does_not_verify);
    RUN(zero_halves_and_broken_forms_are_refused);
    return test_summary("relsig");
}
