/*
 * The release signature of a panel image. See fw_sign.h.
 *
 * Arithmetic on 256-bit numbers as 8 little-endian limbs of 32 bits, modulo
 * p (the field) and n (the group order) in Montgomery form, and points in
 * Jacobian coordinates (X, Y, Z) standing for (X / Z^2, Y / Z^3); Z = 0 is
 * the point at infinity.
 *
 * SPDX-License-Identifier: MIT
 */

#include "fw_sign.h"

#include <string.h>

#include "fw_sha256.h"

#define LIMBS 8u

typedef struct {
    uint32_t w[LIMBS];
} u256_t;

/* The curve secp256r1 (prime256v1), FIPS 186-4 D.1.2.3, big-endian. */
static const uint8_t k_p[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t k_n[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17,
    0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51,
};
static const uint8_t k_b[32] = {
    0x5A, 0xC6, 0x35, 0xD8, 0xAA, 0x3A, 0x93, 0xE7, 0xB3, 0xEB, 0xBD,
    0x55, 0x76, 0x98, 0x86, 0xBC, 0x65, 0x1D, 0x06, 0xB0, 0xCC, 0x53,
    0xB0, 0xF6, 0x3B, 0xCE, 0x3C, 0x3E, 0x27, 0xD2, 0x60, 0x4B,
};
static const uint8_t k_gx[32] = {
    0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47, 0xF8, 0xBC, 0xE6,
    0xE5, 0x63, 0xA4, 0x40, 0xF2, 0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB,
    0x33, 0xA0, 0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96,
};
static const uint8_t k_gy[32] = {
    0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B, 0x8E, 0xE7, 0xEB,
    0x4A, 0x7C, 0x0F, 0x9E, 0x16, 0x2B, 0xCE, 0x33, 0x57, 0x6B, 0x31,
    0x5E, 0xCE, 0xCB, 0xB6, 0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5,
};

/* ------------------------------------------------------------- plain */

static void from_be(u256_t *r, const uint8_t b[32])
{
    for (unsigned i = 0; i < LIMBS; ++i) {
        const unsigned o = 28u - 4u * i;
        r->w[i] = ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1u] << 16)
                  | ((uint32_t)b[o + 2u] << 8) | (uint32_t)b[o + 3u];
    }
}

static int cmp(const u256_t *a, const u256_t *b)
{
    for (unsigned i = LIMBS; i-- > 0u;) {
        if (a->w[i] != b->w[i]) {
            return (a->w[i] > b->w[i]) ? 1 : -1;
        }
    }
    return 0;
}

static bool is_zero(const u256_t *a)
{
    uint32_t acc = 0u;
    for (unsigned i = 0; i < LIMBS; ++i) {
        acc |= a->w[i];
    }
    return acc == 0u;
}

/* r = a + b; the carry out. */
static uint32_t add(u256_t *r, const u256_t *a, const u256_t *b)
{
    uint64_t c = 0u;
    for (unsigned i = 0; i < LIMBS; ++i) {
        c += (uint64_t)a->w[i] + b->w[i];
        r->w[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)c;
}

/* r = a - b; the borrow out. */
static uint32_t sub(u256_t *r, const u256_t *a, const u256_t *b)
{
    uint32_t br = 0u;
    for (unsigned i = 0; i < LIMBS; ++i) {
        const uint64_t d = (uint64_t)a->w[i] - b->w[i] - br;
        r->w[i] = (uint32_t)d;
        br = (uint32_t)((d >> 32) & 1u);
    }
    return br;
}

static unsigned bit(const u256_t *a, unsigned i)
{
    return (a->w[i / 32u] >> (i % 32u)) & 1u;
}

/* ------------------------------------------------------ modular, Montgomery */

typedef struct {
    u256_t   m;     /**< the modulus, odd                  */
    u256_t   r2;    /**< 2^512 mod m                        */
    u256_t   one;   /**< 2^256 mod m: 1 in Montgomery form  */
    uint32_t n0;    /**< -m^-1 mod 2^32                     */
} mod_t;

static void mod_add(u256_t *r, const u256_t *a, const u256_t *b,
                    const mod_t *f)
{
    const uint32_t c = add(r, a, b);
    if (c != 0u || cmp(r, &f->m) >= 0) {
        (void)sub(r, r, &f->m);
    }
}

static void mod_sub(u256_t *r, const u256_t *a, const u256_t *b,
                    const mod_t *f)
{
    if (sub(r, a, b) != 0u) {
        (void)add(r, r, &f->m);
    }
}

/* r = a * b / 2^256 mod m, both factors below m. */
static void mont_mul(u256_t *r, const u256_t *a, const u256_t *b,
                     const mod_t *f)
{
    uint32_t t[LIMBS + 2u];
    memset(t, 0, sizeof(t));
    for (unsigned i = 0; i < LIMBS; ++i) {
        uint64_t c = 0u;
        for (unsigned j = 0; j < LIMBS; ++j) {
            c += (uint64_t)t[j] + (uint64_t)a->w[j] * b->w[i];
            t[j] = (uint32_t)c;
            c >>= 32;
        }
        c += t[LIMBS];
        t[LIMBS] = (uint32_t)c;
        t[LIMBS + 1u] = (uint32_t)(c >> 32);

        const uint32_t q = t[0] * f->n0;
        c = ((uint64_t)t[0] + (uint64_t)f->m.w[0] * q) >> 32;
        for (unsigned j = 1; j < LIMBS; ++j) {
            c += (uint64_t)t[j] + (uint64_t)f->m.w[j] * q;
            t[j - 1u] = (uint32_t)c;
            c >>= 32;
        }
        c += t[LIMBS];
        t[LIMBS - 1u] = (uint32_t)c;
        t[LIMBS] = t[LIMBS + 1u] + (uint32_t)(c >> 32);
    }
    u256_t v;
    memcpy(v.w, t, sizeof(v.w));
    if (t[LIMBS] != 0u || cmp(&v, &f->m) >= 0) {
        (void)sub(&v, &v, &f->m);
    }
    *r = v;
}

static void mod_init(mod_t *f, const uint8_t be[32])
{
    from_be(&f->m, be);
    uint32_t x = f->m.w[0];
    for (unsigned i = 0; i < 5u; ++i) {
        x *= 2u - f->m.w[0] * x;      /* Newton: x = m^-1 mod 2^32 */
    }
    f->n0 = 0u - x;
    /* 2^256 mod m, then 2^512 mod m, by doubling 1 mod m. */
    u256_t v;
    memset(&v, 0, sizeof(v));
    v.w[0] = 1u;
    for (unsigned i = 0; i < 512u; ++i) {
        mod_add(&v, &v, &v, f);
        if (i == 255u) {
            f->one = v;
        }
    }
    f->r2 = v;
}

static void to_mont(u256_t *r, const u256_t *a, const mod_t *f)
{
    mont_mul(r, a, &f->r2, f);
}

static void from_mont(u256_t *r, const u256_t *a, const mod_t *f)
{
    u256_t one;
    memset(&one, 0, sizeof(one));
    one.w[0] = 1u;
    mont_mul(r, a, &one, f);
}

/* r = a^(m-2) for a in Montgomery form: the inverse. */
static void mont_inv(u256_t *r, const u256_t *a, const mod_t *f)
{
    u256_t e;
    u256_t two;
    memset(&two, 0, sizeof(two));
    two.w[0] = 2u;
    (void)sub(&e, &f->m, &two);
    u256_t acc = f->one;
    for (unsigned i = 256u; i-- > 0u;) {
        mont_mul(&acc, &acc, &acc, f);
        if (bit(&e, i) != 0u) {
            mont_mul(&acc, &acc, a, f);
        }
    }
    *r = acc;
}

/* ------------------------------------------------------------------ points */

typedef struct {
    u256_t x, y, z;
} pt_t;

static bool pt_inf(const pt_t *p) { return is_zero(&p->z); }

static void pt_set_inf(pt_t *p, const mod_t *fp)
{
    p->x = fp->one;
    p->y = fp->one;
    memset(&p->z, 0, sizeof(p->z));
}

/* a = -3: dbl-2001-b. */
static void pt_dbl(pt_t *r, const pt_t *p, const mod_t *f)
{
    if (pt_inf(p) || is_zero(&p->y)) {
        pt_set_inf(r, f);
        return;
    }
    u256_t delta, gamma, beta, alpha, t1, t2, x3, y3, z3;
    mont_mul(&delta, &p->z, &p->z, f);
    mont_mul(&gamma, &p->y, &p->y, f);
    mont_mul(&beta, &p->x, &gamma, f);
    mod_sub(&t1, &p->x, &delta, f);
    mod_add(&t2, &p->x, &delta, f);
    mont_mul(&alpha, &t1, &t2, f);
    mod_add(&t1, &alpha, &alpha, f);
    mod_add(&alpha, &t1, &alpha, f);              /* 3 (x - d)(x + d)    */
    mont_mul(&x3, &alpha, &alpha, f);
    mod_add(&t1, &beta, &beta, f);
    mod_add(&t1, &t1, &t1, f);                    /* 4 beta              */
    mod_add(&t2, &t1, &t1, f);                    /* 8 beta              */
    mod_sub(&x3, &x3, &t2, f);
    mod_add(&z3, &p->y, &p->z, f);
    mont_mul(&z3, &z3, &z3, f);
    mod_sub(&z3, &z3, &gamma, f);
    mod_sub(&z3, &z3, &delta, f);
    mod_sub(&t1, &t1, &x3, f);
    mont_mul(&y3, &alpha, &t1, f);
    mont_mul(&t2, &gamma, &gamma, f);
    mod_add(&t2, &t2, &t2, f);
    mod_add(&t2, &t2, &t2, f);
    mod_add(&t2, &t2, &t2, f);                    /* 8 gamma^2           */
    mod_sub(&y3, &y3, &t2, f);
    r->x = x3;
    r->y = y3;
    r->z = z3;
}

/* add-2007-bl, with the equal points and the opposite points handled. */
static void pt_add(pt_t *r, const pt_t *p, const pt_t *q, const mod_t *f)
{
    if (pt_inf(p)) {
        *r = *q;
        return;
    }
    if (pt_inf(q)) {
        *r = *p;
        return;
    }
    u256_t z1z1, z2z2, u1, u2, s1, s2, h, rr, h2, h3, v, t, x3, y3, z3;
    mont_mul(&z1z1, &p->z, &p->z, f);
    mont_mul(&z2z2, &q->z, &q->z, f);
    mont_mul(&u1, &p->x, &z2z2, f);
    mont_mul(&u2, &q->x, &z1z1, f);
    mont_mul(&t, &q->z, &z2z2, f);
    mont_mul(&s1, &p->y, &t, f);
    mont_mul(&t, &p->z, &z1z1, f);
    mont_mul(&s2, &q->y, &t, f);
    if (cmp(&u1, &u2) == 0) {
        if (cmp(&s1, &s2) == 0) {
            pt_dbl(r, p, f);
        } else {
            pt_set_inf(r, f);
        }
        return;
    }
    mod_sub(&h, &u2, &u1, f);
    mod_sub(&rr, &s2, &s1, f);
    mont_mul(&h2, &h, &h, f);
    mont_mul(&h3, &h, &h2, f);
    mont_mul(&v, &u1, &h2, f);
    mont_mul(&x3, &rr, &rr, f);
    mod_sub(&x3, &x3, &h3, f);
    mod_sub(&x3, &x3, &v, f);
    mod_sub(&x3, &x3, &v, f);
    mod_sub(&t, &v, &x3, f);
    mont_mul(&y3, &rr, &t, f);
    mont_mul(&t, &s1, &h3, f);
    mod_sub(&y3, &y3, &t, f);
    mont_mul(&z3, &p->z, &q->z, f);
    mont_mul(&z3, &z3, &h, f);
    r->x = x3;
    r->y = y3;
    r->z = z3;
}

/* y^2 = x^3 - 3x + b for an affine point, both coordinates below p. */
static bool on_curve(const u256_t *x, const u256_t *y, const mod_t *fp)
{
    u256_t xm, ym, bm, bb, l, r, t;
    from_be(&bb, k_b);
    if (cmp(x, &fp->m) >= 0 || cmp(y, &fp->m) >= 0) {
        return false;
    }
    to_mont(&xm, x, fp);
    to_mont(&ym, y, fp);
    to_mont(&bm, &bb, fp);
    mont_mul(&l, &ym, &ym, fp);
    mont_mul(&r, &xm, &xm, fp);
    mont_mul(&r, &r, &xm, fp);
    mod_add(&t, &xm, &xm, fp);
    mod_add(&t, &t, &xm, fp);
    mod_sub(&r, &r, &t, fp);
    mod_add(&r, &r, &bm, fp);
    return cmp(&l, &r) == 0;
}

/* ------------------------------------------------------------------- ECDSA */

/* One DER INTEGER at *at: positive, minimal, at most 32 bytes of value. */
static bool der_int(const uint8_t *p, size_t n, size_t *at, u256_t *out)
{
    if (*at + 2u > n || p[*at] != 0x02u) {
        return false;
    }
    const size_t len = p[*at + 1u];
    *at += 2u;
    if (len == 0u || len > 33u || *at + len > n) {
        return false;
    }
    const uint8_t *v = p + *at;
    *at += len;
    if ((v[0] & 0x80u) != 0u) {
        return false;                         /* negative */
    }
    if (len > 1u && v[0] == 0u && (v[1] & 0x80u) == 0u) {
        return false;                         /* not minimal */
    }
    const size_t skip = (v[0] == 0u && len > 1u) ? 1u : 0u;
    if (len - skip > 32u) {
        return false;
    }
    uint8_t be[32];
    memset(be, 0, sizeof(be));
    memcpy(be + 32u - (len - skip), v + skip, len - skip);
    from_be(out, be);
    return true;
}

fw_sig_t fw_sig_verify_hash(const fw_pubkey_t *key, const uint8_t hash[32],
                            const uint8_t *sig, size_t sig_n)
{
    if (sig == NULL || sig_n == 0u) {
        return FW_SIG_NONE;
    }
    if (sig_n < FW_SIG_MIN || sig_n > FW_SIG_MAX || sig[0] != 0x30u
        || sig[1] != sig_n - 2u) {
        return FW_SIG_FORM;
    }
    u256_t r, s;
    size_t at = 2u;
    if (!der_int(sig, sig_n, &at, &r) || !der_int(sig, sig_n, &at, &s)
        || at != sig_n) {
        return FW_SIG_FORM;
    }

    mod_t fp, fn;
    mod_init(&fp, k_p);
    mod_init(&fn, k_n);

    u256_t qx, qy;
    from_be(&qx, key->x);
    from_be(&qy, key->y);
    if (!on_curve(&qx, &qy, &fp)) {
        return FW_SIG_KEY;
    }
    if (is_zero(&r) || is_zero(&s) || cmp(&r, &fn.m) >= 0
        || cmp(&s, &fn.m) >= 0) {
        return FW_SIG_BAD;
    }

    /* u1 = e / s, u2 = r / s, modulo n. */
    u256_t e, sm, w, em, rm, u1m, u2m, u1, u2;
    from_be(&e, hash);
    if (cmp(&e, &fn.m) >= 0) {
        (void)sub(&e, &e, &fn.m);
    }
    to_mont(&sm, &s, &fn);
    mont_inv(&w, &sm, &fn);
    to_mont(&em, &e, &fn);
    to_mont(&rm, &r, &fn);
    mont_mul(&u1m, &em, &w, &fn);
    mont_mul(&u2m, &rm, &w, &fn);
    from_mont(&u1, &u1m, &fn);
    from_mont(&u2, &u2m, &fn);

    /* R = u1 G + u2 Q, both scalars in one pass over the bits. */
    pt_t tab[4];
    u256_t gx, gy;
    from_be(&gx, k_gx);
    from_be(&gy, k_gy);
    pt_set_inf(&tab[0], &fp);
    to_mont(&tab[1].x, &gx, &fp);
    to_mont(&tab[1].y, &gy, &fp);
    tab[1].z = fp.one;
    to_mont(&tab[2].x, &qx, &fp);
    to_mont(&tab[2].y, &qy, &fp);
    tab[2].z = fp.one;
    pt_add(&tab[3], &tab[1], &tab[2], &fp);

    pt_t acc;
    pt_set_inf(&acc, &fp);
    for (unsigned i = 256u; i-- > 0u;) {
        pt_dbl(&acc, &acc, &fp);
        const unsigned sel = bit(&u1, i) | (bit(&u2, i) << 1);
        if (sel != 0u) {
            pt_add(&acc, &acc, &tab[sel], &fp);
        }
    }
    if (pt_inf(&acc)) {
        return FW_SIG_BAD;
    }

    /* x(R) mod n == r */
    u256_t zi, zi2, xa, xr;
    mont_inv(&zi, &acc.z, &fp);
    mont_mul(&zi2, &zi, &zi, &fp);
    mont_mul(&xa, &acc.x, &zi2, &fp);
    from_mont(&xr, &xa, &fp);
    if (cmp(&xr, &fn.m) >= 0) {
        (void)sub(&xr, &xr, &fn.m);
    }
    return (cmp(&xr, &r) == 0) ? FW_SIG_OK : FW_SIG_BAD;
}

fw_sig_t fw_sig_verify(const fw_pubkey_t *key, const uint8_t *img,
                       uint32_t bytes, const uint8_t *sig, size_t sig_n)
{
    if (sig == NULL || sig_n == 0u) {
        return FW_SIG_NONE;
    }
    uint8_t h[FW_SHA256_BYTES];
    fw_sha256(img, bytes, h);
    return fw_sig_verify_hash(key, h, sig, sig_n);
}

const char *fw_sig_str(fw_sig_t r)
{
    switch (r) {
    case FW_SIG_OK:   return "signature verifies";
    case FW_SIG_NONE: return "no signature";
    case FW_SIG_FORM: return "signature is not a P-256 DER signature";
    case FW_SIG_KEY:  return "release key is not a P-256 point";
    case FW_SIG_BAD:
    default:          return "signature does not verify";
    }
}

/* ------------------------------------------------------------------- PEM */

static int b64(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

/* SubjectPublicKeyInfo of id-ecPublicKey with prime256v1, then the BIT
 * STRING header and the 0x04 marker of an uncompressed point. */
static const uint8_t k_spki[27] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02,
    0x01, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04,
};

#define SPKI_BYTES 91u

bool fw_pubkey_parse_pem(const char *pem, size_t n, fw_pubkey_t *out)
{
    static const char head[] = "-----BEGIN PUBLIC KEY-----";
    static const char foot[] = "-----END PUBLIC KEY-----";
    const size_t hn = sizeof(head) - 1u;
    const size_t fn = sizeof(foot) - 1u;
    if (pem == NULL || out == NULL || n < hn + fn) {
        return false;
    }
    size_t at = 0u;
    while (at < n && (pem[at] == ' ' || pem[at] == '\n' || pem[at] == '\r')) {
        ++at;
    }
    /* cppcheck-suppress knownConditionTrueFalse */
    if (n - at < hn || memcmp(pem + at, head, hn) != 0) {
        return false;
    }
    at += hn;
    uint8_t der[SPKI_BYTES];
    size_t have = 0u;
    uint32_t acc = 0u;
    unsigned bits = 0u;
    while (at < n && pem[at] != '-') {
        const char c = pem[at++];
        if (c == '\n' || c == '\r' || c == ' ' || c == '=') {
            continue;
        }
        const int v = b64(c);
        if (v < 0) {
            return false;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6u;
        if (bits >= 8u) {
            bits -= 8u;
            if (have >= sizeof(der)) {
                return false;
            }
            der[have++] = (uint8_t)(acc >> bits);
            acc &= (1u << bits) - 1u;
        }
    }
    /* cppcheck-suppress knownConditionTrueFalse */
    if (n - at < fn || memcmp(pem + at, foot, fn) != 0) {
        return false;
    }
    if (have != SPKI_BYTES || memcmp(der, k_spki, sizeof(k_spki)) != 0) {
        return false;
    }
    fw_pubkey_t k;
    memcpy(k.x, der + sizeof(k_spki), 32u);
    memcpy(k.y, der + sizeof(k_spki) + 32u, 32u);
    mod_t fp;
    mod_init(&fp, k_p);
    u256_t x, y;
    from_be(&x, k.x);
    from_be(&y, k.y);
    if (!on_curve(&x, &y, &fp)) {
        return false;
    }
    *out = k;
    return true;
}
