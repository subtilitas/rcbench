/**
 * @file test_devupd_sig.c
 * @brief The device update's protocol 2 against the simulated device of
 *        devupd_rig.h: the release signature in HASH parts 2 to 5 and a
 *        device that installs after both checks, zeros without one, a
 *        refused signature that ends the transfer without a fallback, and
 *        the fallback to protocol 1 for an older device, once.
 *
 * SPDX-License-Identifier: MIT
 */
#include "devupd_rig.h"

/**
 * @brief The simulated Dial needs the signature, as the Dial's firmware
 *        does; the verdict is s->sig_ok.
 * @param s the simulation
 */
static void need_sig(sim_t *s)
{
    s->d.cfg.need_sig = true;
}

/* A device of protocol 1 refuses protocol 2 with BLEOTA_E_PROTO before
 * anything opens: the engine drops the answers to the HASH writes that
 * followed and starts once more, in protocol 1, and the image installs. */
TEST_CASE(an_older_device_takes_the_update_once_in_protocol_1)
{
    static sim_t s;
    sim_init(&s);
    s.old = true;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_DONE);
    CHECK(s.e.fell_back);
    CHECK_EQ(s.e.proto, BLEOTA_PROTO_1);
    CHECK_EQ(s.begins, 2u);
    CHECK_EQ(s.flash_n, IMG);
    CHECK(memcmp(s_flash, s_img, IMG) == 0);
    CHECK(s.restarted);
}

/* The fallback happens once: a device that refuses protocol 1 too ends
 * the transfer with its code after the second BEGIN. */
TEST_CASE(the_fallback_happens_once)
{
    static sim_t s;
    sim_init(&s);
    s.old = true;
    s.old_both = true;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 20000u), DEVUPD_FAILED);
    CHECK_EQ(s.e.err, DEVUPD_E_DEVICE);
    CHECK_EQ(s.e.dev_code, BLEOTA_E_PROTO);
    CHECK_EQ(s.begins, 2u);
    CHECK_EQ(s.flash_n, 0u);
}

/* A refused signature ends the transfer: no protocol 1 BEGIN follows. */
TEST_CASE(a_refused_signature_never_falls_back)
{
    static sim_t s;
    sim_init(&s);
    need_sig(&s);
    s.sig_ok = false;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 20000u), DEVUPD_FAILED);
    CHECK_EQ(s.e.err, DEVUPD_E_DEVICE);
    CHECK_EQ(s.e.dev_code, BLEOTA_E_SIG);
    CHECK(!s.e.fell_back);
    CHECK_EQ(s.begins, 1u);
    CHECK_EQ(s.flash_n, 0u);
    CHECK(!s.d.open);
}

/* The signature goes in HASH parts 2 to 5 as given, and a device that
 * needs it installs after both checks. */
TEST_CASE(the_signature_reaches_the_device_and_it_installs)
{
    static sim_t s;
    sim_init(&s);
    need_sig(&s);
    s.sig_ok = true;
    for (unsigned i = 0; i < BLEOTA_SIG_LEN; ++i) {
        s.im.sig[i] = (uint8_t)(0xC0u ^ i);
    }
    s.im.has_sig = true;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_DONE);
    CHECK(memcmp(s.d.sig, s.im.sig, BLEOTA_SIG_LEN) == 0);
    CHECK_EQ(s.begins, 1u);
    CHECK(!s.e.fell_back);
    CHECK(s.restarted);
    /* Without one, zeros go, and the device refuses them at check 1. */
    sim_init(&s);
    need_sig(&s);
    s.sig_ok = false;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 20000u), DEVUPD_FAILED);
    CHECK_EQ(s.e.dev_code, BLEOTA_E_SIG);
    uint8_t zero[BLEOTA_SIG_LEN];
    memset(zero, 0, sizeof zero);
    CHECK(memcmp(s.d.sig, zero, sizeof zero) == 0);
}

int main(void)
{
    RUN(an_older_device_takes_the_update_once_in_protocol_1);
    RUN(the_fallback_happens_once);
    RUN(a_refused_signature_never_falls_back);
    RUN(the_signature_reaches_the_device_and_it_installs);
    return test_summary("devupd_sig");
}
