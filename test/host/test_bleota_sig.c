/*
 * The device's two signature checks in bleota protocol 2
 * (protocols/bleota/bleota_dev.c), run with the real verifier
 * (shared/relsig/relsig.c) and the throw-away keys of sign_vectors.h.
 *
 * A Dial (need_sig) on the ESP32-S3:
 * - refuses protocol 1 with BLEOTA_E_SIG at BEGIN, before any
 *   confirmation; a node, which needs no signature, takes protocol 1;
 * - check 1, after the last HASH part and before WAIT: a signature of
 *   another image or under another key ends the transfer with
 *   BLEOTA_E_SIG, no confirmation is asked and nothing opens;
 * - check 2, after the last write and before the install: the SHA-256 it
 *   checks is the one the device computed of what it wrote, never the
 *   central's.  A central whose HASH and signature belong together but
 *   whose bytes are another image's passes check 1 and is refused at
 *   check 2, with no install;
 * - a transfer that ends while the signature is verified takes no
 *   verdict afterwards.
 *
 * The vectors sign images without an ESP application head, so a transfer
 * that installs cannot carry one of them: the installing case verifies
 * through a stand-in verifier that accepts exactly one (SHA-256,
 * signature) pair, and checks that check 2 is asked about the computed
 * SHA-256.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bleota.h"
#include "bleota_head.h"
#include "fw_sha256.h"
#include "relsig.h"
#include "sign_vectors.h"

/** @brief The slot of the tests' Dial, bytes. */
#define SLOT 4096u
/** @brief The image the tests send, bytes. */
#define IMG 600u

/** @brief A device, its buffer and what was written. */
typedef struct {
    bleota_dev_t d;                   /**< the device */
    uint8_t      ring[BLEOTA_WINDOW]; /**< its buffer */
    uint8_t      flash[SLOT];         /**< what was written */
    uint32_t     flash_n;             /**< how many bytes */
    uint32_t     t;                   /**< the clock */
    int          sigs;                /**< BLEOTA_DO_SIG answered */
    uint8_t      asked[2][32];        /**< the SHA-256 each check named */
} sim_t;

/** @brief The image the tests send: a K518 Dial's head, then a pattern. */
static uint8_t s_img[IMG];

/** @brief Fill s_img. */
static void img_make(void)
{
    test_head_write(s_img, TEST_CHIP_S3, BLEOTA_KIND_DIAL_K518);
    for (uint32_t i = BLEOTA_HEAD_LEN; i < IMG; ++i) {
        s_img[i] = (uint8_t)(i * 13u);
    }
}

/** @brief Vector image @p i (tools/gen_sign_vectors.py) and its SHA-256.
 *  @param i its number @param sha receives the SHA-256 */
static void vec_sha(unsigned i, uint8_t sha[32])
{
    uint8_t img[1024];
    const size_t n = 64u + 13u * i;
    for (size_t j = 0; j < n; ++j) {
        img[j] = (uint8_t)((i * 31u + j * 7u) & 0xFFu);
    }
    fw_sha256(img, n, sha);
}

/** @brief The raw signature of vector image @p i.  @param i its number
 *  @param raw receives r then s */
static void vec_sig(unsigned i, uint8_t raw[RELSIG_RAW_LEN])
{
    CHECK(relsig_der_to_raw(sv_sig[i], sv_sig_len[i], raw));
}

/** @brief A device of @p kind on @p chip.  @param s the rig
 *  @param kind BLEOTA_KIND_* @param chip its chip @param need_sig only
 *  signed images */
static void dev_init(sim_t *s, uint8_t kind, uint16_t chip, bool need_sig)
{
    memset(s, 0, sizeof *s);
    const bleota_dev_cfg_t cfg = { .kind = kind,
                                   .confirm = BLEOTA_CONFIRM_PRESS,
                                   .slot = SLOT, .ver = { 0u, 24u, 0u },
                                   .chip = chip, .need_sig = need_sig };
    CHECK(bleota_dev_init(&s->d, &cfg, s->ring, sizeof s->ring));
    s->t = 1000u;
}

/** @brief Send BEGIN for s_img.  @param s the rig @param kind its kind
 *  @param proto the protocol */
static void begin(sim_t *s, uint8_t kind, uint8_t proto)
{
    uint8_t b[BLEOTA_LEN_BEGIN];
    const uint16_t ver[3] = { 0u, 25u, 0u };
    (void)bleota_begin_encode_proto(proto, kind, IMG, ver, 0u, b);
    bleota_dev_ctrl(&s->d, b, sizeof b, s->t);
}

/** @brief Send the SHA-256's parts, then, unless NULL, the signature's.
 *  @param s the rig @param sha the SHA-256 @param sig r then s, or NULL */
static void parts(sim_t *s, const uint8_t sha[32], const uint8_t *sig)
{
    uint8_t b[BLEOTA_LEN_HASH];
    for (uint8_t p = 0; p < 2u; ++p) {
        (void)bleota_hash_encode(sha, p, b);
        bleota_dev_ctrl(&s->d, b, sizeof b, s->t);
    }
    for (uint8_t q = 0; sig != NULL && q < 4u; ++q) {
        (void)bleota_sig_encode(sig, q, b);
        bleota_dev_ctrl(&s->d, b, sizeof b, s->t);
    }
}

/** @brief A verifier: the real one under @p pem, or, for NULL, the
 *  stand-in that accepts only the pair (s_img's SHA-256, k_fake). */
typedef const char *verifier_t;

/** @brief The stand-in's one signature. */
static const uint8_t k_fake[RELSIG_RAW_LEN] = { 0x5Au, 0xA5u, 0x01u };

/** @brief Answer BLEOTA_DO_SIG.  @param s the rig @param pem the key,
 *  NULL for the stand-in */
static void answer_sig(sim_t *s, verifier_t pem)
{
    uint8_t sha[32];
    uint8_t sig[BLEOTA_SIG_LEN];
    CHECK(bleota_dev_sig_job(&s->d, sha, sig));
    if (s->sigs < 2) {
        memcpy(s->asked[s->sigs], sha, 32u);
    }
    ++s->sigs;
    bool ok;
    if (pem != NULL) {
        ok = relsig_verify(pem, strlen(pem), sha, sig);
    } else {
        uint8_t want[32];
        fw_sha256(s_img, IMG, want);
        ok = memcmp(sha, want, 32u) == 0
             && memcmp(sig, k_fake, sizeof k_fake) == 0;
    }
    bleota_dev_sig(&s->d, ok, s->t);
}

/** @brief The last notification's FAIL code, BLEOTA_OK for none.
 *  @param s the rig @param saw_wait set when a WAIT went
 *  @return the code */
static uint8_t drain(sim_t *s, bool *saw_wait)
{
    uint8_t b[BLEOTA_LEN_STATUS];
    uint8_t code = BLEOTA_OK;
    while (bleota_dev_status(&s->d, b)) {
        bleota_status_t st;
        CHECK(bleota_status_decode(b, sizeof b, &st));
        if (st.ev == BLEOTA_EV_FAIL) {
            code = st.code;
        } else if (st.ev == BLEOTA_EV_WAIT && saw_wait != NULL) {
            *saw_wait = true;
        }
    }
    return code;
}

/** @brief A press shorter than BLEOTA_HOLD_MS.  @param s the rig */
static void press(sim_t *s)
{
    bleota_dev_key(&s->d, true, s->t);
    s->t += 100u;
    bleota_dev_key(&s->d, false, s->t);
}

/** @brief Run the device's steps until it has nothing to do or asks for
 *  the signature, sending @p bytes of @p img after the slot opens.
 *  @param s the rig @param img the bytes @param bytes their count
 *  @return the step it stopped at */
static bleota_do_t run(sim_t *s, const uint8_t *img, uint32_t bytes)
{
    for (int i = 0; i < 1000; ++i) {
        const bleota_do_t act = bleota_dev_poll(&s->d, s->t);
        switch (act) {
        case BLEOTA_DO_OPEN:
            bleota_dev_opened(&s->d, true, s->t);
            for (uint32_t off = 0; off < bytes; off += 200u) {
                uint8_t c[BLEOTA_CHUNK_HDR + 200u];
                const uint32_t n = bytes - off < 200u ? bytes - off : 200u;
                bleota_chunk_header(off, c);
                memcpy(c + BLEOTA_CHUNK_HDR, img + off, n);
                bleota_dev_data(&s->d, c, BLEOTA_CHUNK_HDR + n, s->t);
                for (int w = 0; w < 8; ++w) {
                    if (bleota_dev_poll(&s->d, s->t) != BLEOTA_DO_WRITE) {
                        break;
                    }
                    const uint32_t k = bleota_dev_take(
                        &s->d, s->flash + s->flash_n, SLOT - s->flash_n);
                    s->flash_n += k;
                    bleota_dev_written(&s->d, true, s->t);
                }
            }
            {
                uint8_t e[1];
                (void)bleota_op_encode(BLEOTA_OP_END, e);
                bleota_dev_ctrl(&s->d, e, 1u, s->t);
            }
            break;
        case BLEOTA_DO_WRITE: {
            const uint32_t k = bleota_dev_take(&s->d, s->flash + s->flash_n,
                                               SLOT - s->flash_n);
            s->flash_n += k;
            bleota_dev_written(&s->d, true, s->t);
            break;
        }
        case BLEOTA_DO_VERIFY: {
            uint8_t digest[32];
            fw_sha256(s->flash, s->flash_n, digest);
            (void)bleota_dev_verify(&s->d, digest, s->t);
            break;
        }
        case BLEOTA_DO_CLOSE:
            bleota_dev_closed(&s->d);
            break;
        default:
            return act;
        }
        ++s->t;
    }
    return BLEOTA_DO_NONE;
}

TEST_CASE(protocol_1_is_refused_where_the_signature_is_needed)
{
    sim_t s;
    uint8_t sha[32];
    bool wait = false;
    img_make();
    fw_sha256(s_img, IMG, sha);
    dev_init(&s, BLEOTA_KIND_DIAL_K518, TEST_CHIP_S3, true);
    begin(&s, BLEOTA_KIND_DIAL_K518, BLEOTA_PROTO_1);
    CHECK_EQ(drain(&s, &wait), BLEOTA_E_SIG);
    CHECK_EQ(s.d.st, BLEOTA_DEV_IDLE);
    CHECK_EQ(s.d.offer, 0u);
    /* A node needs none: protocol 1 and its two parts ask at once. */
    dev_init(&s, BLEOTA_KIND_NODE_POWER, TEST_CHIP_C6, false);
    begin(&s, BLEOTA_KIND_NODE_POWER, BLEOTA_PROTO_1);
    parts(&s, sha, NULL);
    CHECK_EQ(drain(&s, &wait), BLEOTA_OK);
    CHECK(wait);
    CHECK_EQ(s.d.st, BLEOTA_DEV_CONFIRM);
    /* In protocol 1 a signature part is out of order. */
    dev_init(&s, BLEOTA_KIND_NODE_POWER, TEST_CHIP_C6, false);
    begin(&s, BLEOTA_KIND_NODE_POWER, BLEOTA_PROTO_1);
    uint8_t b[BLEOTA_LEN_HASH];
    (void)bleota_sig_encode(k_fake, 0u, b);
    bleota_dev_ctrl(&s.d, b, sizeof b, s.t);
    CHECK_EQ(drain(&s, NULL), BLEOTA_E_STATE);
}

TEST_CASE(check_1_refuses_before_the_confirmation)
{
    sim_t s;
    uint8_t sha[32];
    uint8_t raw[RELSIG_RAW_LEN];
    bool wait = false;
    /* Image 0's SHA-256 with image 1's signature. */
    dev_init(&s, BLEOTA_KIND_DIAL_K518, TEST_CHIP_S3, true);
    vec_sha(0u, sha);
    vec_sig(1u, raw);
    begin(&s, BLEOTA_KIND_DIAL_K518, BLEOTA_PROTO);
    parts(&s, sha, raw);
    CHECK_EQ(s.d.st, BLEOTA_DEV_SIG);
    CHECK_EQ(run(&s, NULL, 0u), BLEOTA_DO_SIG);
    answer_sig(&s, sv_pem_a);
    CHECK_EQ(drain(&s, &wait), BLEOTA_E_SIG);
    CHECK(!wait);
    CHECK_EQ(s.d.offer, 0u);
    CHECK(!s.d.open);
    CHECK(!bleota_dev_offer(&s.d, NULL));
    CHECK(memcmp(s.asked[0], sha, 32u) == 0);   /* the central's */
    /* The right pair under another key. */
    dev_init(&s, BLEOTA_KIND_DIAL_K518, TEST_CHIP_S3, true);
    vec_sig(0u, raw);
    begin(&s, BLEOTA_KIND_DIAL_K518, BLEOTA_PROTO);
    parts(&s, sha, raw);
    CHECK_EQ(run(&s, NULL, 0u), BLEOTA_DO_SIG);
    answer_sig(&s, sv_pem_b);
    CHECK_EQ(drain(&s, &wait), BLEOTA_E_SIG);
    CHECK(!wait);
    /* The right pair under its key: the confirmation is asked. */
    dev_init(&s, BLEOTA_KIND_DIAL_K518, TEST_CHIP_S3, true);
    begin(&s, BLEOTA_KIND_DIAL_K518, BLEOTA_PROTO);
    parts(&s, sha, raw);
    CHECK_EQ(run(&s, NULL, 0u), BLEOTA_DO_SIG);
    answer_sig(&s, sv_pem_a);
    CHECK_EQ(drain(&s, &wait), BLEOTA_OK);
    CHECK(wait);
    CHECK_EQ(s.d.st, BLEOTA_DEV_CONFIRM);
}

TEST_CASE(a_matching_pair_with_other_bytes_is_refused_at_check_2)
{
    sim_t s;
    uint8_t sha[32];
    uint8_t raw[RELSIG_RAW_LEN];
    uint8_t mine[32];
    bool wait = false;
    img_make();
    fw_sha256(s_img, IMG, mine);
    dev_init(&s, BLEOTA_KIND_DIAL_K518, TEST_CHIP_S3, true);
    vec_sha(0u, sha);
    vec_sig(0u, raw);
    begin(&s, BLEOTA_KIND_DIAL_K518, BLEOTA_PROTO);
    parts(&s, sha, raw);
    CHECK_EQ(run(&s, NULL, 0u), BLEOTA_DO_SIG);
    answer_sig(&s, sv_pem_a);                 /* check 1 passes */
    CHECK_EQ(drain(&s, &wait), BLEOTA_OK);
    CHECK(wait);
    press(&s);
    /* The bytes are s_img, not vector image 0. */
    CHECK_EQ(run(&s, s_img, IMG), BLEOTA_DO_SIG);
    CHECK_EQ(s.d.st, BLEOTA_DEV_SIG_END);
    answer_sig(&s, sv_pem_a);
    CHECK(memcmp(s.asked[1], mine, 32u) == 0);  /* the device's own */
    CHECK(memcmp(s.asked[1], sha, 32u) != 0);
    CHECK_EQ(drain(&s, NULL), BLEOTA_E_SIG);
    CHECK_EQ(s.d.st, BLEOTA_DEV_IDLE);
    CHECK_EQ(bleota_dev_poll(&s.d, s.t), BLEOTA_DO_CLOSE);
    bleota_dev_closed(&s.d);
    CHECK_EQ(bleota_dev_poll(&s.d, s.t), BLEOTA_DO_NONE);
}

TEST_CASE(a_signed_image_installs_after_both_checks)
{
    sim_t s;
    uint8_t sha[32];
    bool wait = false;
    img_make();
    fw_sha256(s_img, IMG, sha);
    dev_init(&s, BLEOTA_KIND_DIAL_K518, TEST_CHIP_S3, true);
    begin(&s, BLEOTA_KIND_DIAL_K518, BLEOTA_PROTO);
    parts(&s, sha, k_fake);
    CHECK_EQ(run(&s, NULL, 0u), BLEOTA_DO_SIG);
    answer_sig(&s, NULL);
    CHECK_EQ(drain(&s, &wait), BLEOTA_OK);
    CHECK(wait);
    press(&s);
    CHECK_EQ(run(&s, s_img, IMG), BLEOTA_DO_SIG);
    answer_sig(&s, NULL);
    CHECK_EQ(s.sigs, 2);
    CHECK_EQ(bleota_dev_poll(&s.d, s.t), BLEOTA_DO_INSTALL);
    bleota_dev_installed(&s.d, true, BLEOTA_OK, s.t);
    CHECK_EQ(drain(&s, NULL), BLEOTA_OK);
    CHECK_EQ(bleota_dev_poll(&s.d, s.t), BLEOTA_DO_RESTART);
    /* The same image with one byte changed on the way: check 2 refuses
     * it before the compare could. */
    dev_init(&s, BLEOTA_KIND_DIAL_K518, TEST_CHIP_S3, true);
    begin(&s, BLEOTA_KIND_DIAL_K518, BLEOTA_PROTO);
    parts(&s, sha, k_fake);
    (void)run(&s, NULL, 0u);
    answer_sig(&s, NULL);
    press(&s);
    uint8_t bent[IMG];
    memcpy(bent, s_img, IMG);
    bent[IMG - 1u] ^= 1u;
    CHECK_EQ(run(&s, bent, IMG), BLEOTA_DO_SIG);
    answer_sig(&s, NULL);
    CHECK_EQ(drain(&s, NULL), BLEOTA_E_SIG);
}

TEST_CASE(a_transfer_that_ends_during_a_check_takes_no_verdict)
{
    sim_t s;
    uint8_t sha[32];
    img_make();
    fw_sha256(s_img, IMG, sha);
    dev_init(&s, BLEOTA_KIND_DIAL_K518, TEST_CHIP_S3, true);
    begin(&s, BLEOTA_KIND_DIAL_K518, BLEOTA_PROTO);
    parts(&s, sha, k_fake);
    CHECK_EQ(run(&s, NULL, 0u), BLEOTA_DO_SIG);
    bleota_dev_lost(&s.d);
    bleota_dev_sig(&s.d, true, s.t);
    CHECK_EQ(s.d.st, BLEOTA_DEV_IDLE);
    CHECK_EQ(s.d.offer, 0u);
    uint8_t got[32];
    uint8_t sig[BLEOTA_SIG_LEN];
    CHECK(!bleota_dev_sig_job(&s.d, got, sig));
    /* An ABORT while check 2 runs: the slot is released, no install. */
    dev_init(&s, BLEOTA_KIND_DIAL_K518, TEST_CHIP_S3, true);
    begin(&s, BLEOTA_KIND_DIAL_K518, BLEOTA_PROTO);
    parts(&s, sha, k_fake);
    (void)run(&s, NULL, 0u);
    answer_sig(&s, NULL);
    press(&s);
    CHECK_EQ(run(&s, s_img, IMG), BLEOTA_DO_SIG);
    uint8_t a[1];
    (void)bleota_op_encode(BLEOTA_OP_ABORT, a);
    bleota_dev_ctrl(&s.d, a, 1u, s.t);
    bleota_dev_sig(&s.d, true, s.t);
    CHECK_EQ(drain(&s, NULL), BLEOTA_E_ABORTED);
    CHECK_EQ(bleota_dev_poll(&s.d, s.t), BLEOTA_DO_CLOSE);
}

int main(void)
{
    RUN(protocol_1_is_refused_where_the_signature_is_needed);
    RUN(check_1_refuses_before_the_confirmation);
    RUN(a_matching_pair_with_other_bytes_is_refused_at_check_2);
    RUN(a_signed_image_installs_after_both_checks);
    RUN(a_transfer_that_ends_during_a_check_takes_no_verdict);
    return test_summary("bleota_sig");
}
