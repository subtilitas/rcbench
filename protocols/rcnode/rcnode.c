/**
 * @file
 * @brief The rcbench sensor nodes' packets: encode, decode, control writes
 * and the wrap arithmetic.  See rcnode.h for the layouts.
 *
 * SPDX-License-Identifier: MIT
 */

#include "rcnode.h"

#include <string.h>

/* ------------------------------------------------------------ bytes */

/**
 * @brief Write a 16-bit value, little-endian.
 *
 * @param b The destination.
 * @param v The value.
 */
static void put16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)(v >> 8);
}

/**
 * @brief Write a 32-bit value, little-endian.
 *
 * @param b The destination.
 * @param v The value.
 */
static void put32(uint8_t *b, uint32_t v)
{
    for (unsigned i = 0; i < 4u; ++i) {
        b[i] = (uint8_t)((v >> (8u * i)) & 0xFFu);
    }
}

/**
 * @brief Write the low 40 bits of a value, little-endian.
 *
 * @param b The destination.
 * @param v The value.
 */
static void put40(uint8_t *b, uint64_t v)
{
    for (unsigned i = 0; i < 5u; ++i) {
        b[i] = (uint8_t)((v >> (8u * i)) & 0xFFu);
    }
}

/**
 * @brief Read a 16-bit value, little-endian.
 *
 * @param b The source.
 * @return The value.
 */
static uint16_t get16(const uint8_t *b)
{
    return (uint16_t)(b[0] | ((unsigned)b[1] << 8));
}

/**
 * @brief Read a 32-bit value, little-endian.
 *
 * @param b The source.
 * @return The value.
 */
static uint32_t get32(const uint8_t *b)
{
    uint32_t v = 0u;
    for (unsigned i = 0; i < 4u; ++i) {
        v |= (uint32_t)b[i] << (8u * i);
    }
    return v;
}

/**
 * @brief Read a 40-bit value, little-endian.
 *
 * @param b The source.
 * @return The value in bits 39-0.
 */
static uint64_t get40(const uint8_t *b)
{
    uint64_t v = 0u;
    for (unsigned i = 0; i < 5u; ++i) {
        v |= (uint64_t)b[i] << (8u * i);
    }
    return v;
}

/**
 * @brief A uint32 as int32, two's complement, without implementation-defined
 * conversion.
 *
 * @param u The bits.
 * @return The signed value.
 */
static int32_t as_i32(uint32_t u)
{
    if (u <= (uint32_t)INT32_MAX) {
        return (int32_t)u;
    }
    return -(int32_t)(~u) - 1;
}

/**
 * @brief A uint16 as int16, two's complement.
 *
 * @param u The bits.
 * @return The signed value.
 */
static int16_t as_i16(uint16_t u)
{
    return (int16_t)(u <= 0x7FFFu ? (int32_t)u : (int32_t)u - 65536);
}

/**
 * @brief Write the header.
 *
 * @param b The destination.
 * @param type The packet type.
 * @param h The header; its type is ignored.
 */
static void put_hdr(uint8_t *b, uint8_t type, const rcnode_hdr_t *h)
{
    b[0] = type;
    b[1] = h->seq;
    put32(b + 2, h->t_node);
}

/**
 * @brief Read the header.
 *
 * @param b The source.
 * @param h Receives the header.
 */
static void get_hdr(const uint8_t *b, rcnode_hdr_t *h)
{
    h->type   = b[0];
    h->seq    = b[1];
    h->t_node = get32(b + 2);
}

/* ------------------------------------------------------------- uuid */

void rcnode_uuid(uint16_t id, uint8_t out[16])
{
    /* 72636265-6E63-6800-XXXX-000000000000, least significant byte first. */
    static const uint8_t k_base[16] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x68, 0x63, 0x6E, 0x65, 0x62, 0x63, 0x72,
    };
    if (out == NULL) {
        return;
    }
    memcpy(out, k_base, sizeof k_base);
    out[6] = (uint8_t)(id & 0xFFu);
    out[7] = (uint8_t)(id >> 8);
}

size_t rcnode_len(uint8_t type)
{
    static const struct {
        uint8_t type;   /* RCNODE_T_* */
        uint8_t len;    /* its length */
    } k_len[] = {
        { RCNODE_T_PWR_WIN,    RCNODE_PWR_WIN_LEN },
        { RCNODE_T_PWR_ACC,    RCNODE_PWR_ACC_LEN },
        { RCNODE_T_PWR_RUN,    RCNODE_PWR_RUN_LEN },
        { RCNODE_T_BAL_REPORT, RCNODE_BAL_REPORT_LEN },
        { RCNODE_T_STATUS,     RCNODE_STATUS_LEN },
        { RCNODE_T_INFO,       RCNODE_INFO_LEN },
    };
    for (size_t k = 0; k < sizeof k_len / sizeof k_len[0]; ++k) {
        if (k_len[k].type == type) {
            return k_len[k].len;
        }
    }
    return 0u;
}

/* ----------------------------------------------------------- encode */

size_t rcnode_enc_pwr_win(const rcnode_pwr_win_t *p, uint8_t *out, size_t cap)
{
    if (p == NULL || out == NULL || cap < RCNODE_PWR_WIN_LEN) {
        return 0u;
    }
    put_hdr(out, RCNODE_T_PWR_WIN, &p->hdr);
    out[6] = p->flags;
    out[7] = p->n;
    put32(out + 8, (uint32_t)p->i_mean);
    put32(out + 12, (uint32_t)p->i_max);
    put16(out + 16, p->v_mean);
    put16(out + 18, p->v_min);
    return RCNODE_PWR_WIN_LEN;
}

size_t rcnode_enc_pwr_acc(const rcnode_pwr_acc_t *p, uint8_t *out, size_t cap)
{
    if (p == NULL || out == NULL || cap < RCNODE_PWR_ACC_LEN) {
        return 0u;
    }
    put_hdr(out, RCNODE_T_PWR_ACC, &p->hdr);
    out[6] = p->flags;
    out[7] = p->epoch;
    put40(out + 8, p->charge);
    put40(out + 13, p->energy);
    put16(out + 18, (uint16_t)p->temp);
    return RCNODE_PWR_ACC_LEN;
}

size_t rcnode_enc_pwr_run(const rcnode_pwr_run_t *p, uint8_t *out, size_t cap)
{
    if (p == NULL || out == NULL || cap < RCNODE_PWR_RUN_LEN) {
        return 0u;
    }
    put_hdr(out, RCNODE_T_PWR_RUN, &p->hdr);
    out[6] = p->tag;
    out[7] = p->flags;
    put32(out + 8, (uint32_t)p->i_max);
    put16(out + 12, p->v_min);
    put32(out + 14, p->windows);
    return RCNODE_PWR_RUN_LEN;
}

/**
 * @brief Write one harmonic.
 *
 * @param b The destination, 4 bytes.
 * @param h The harmonic.
 */
static void put_harm(uint8_t *b, const rcnode_harm_t *h)
{
    put16(b, h->mag);
    put16(b + 2, h->phase);
}

size_t rcnode_enc_bal_report(const rcnode_bal_report_t *p, uint8_t *out,
                             size_t cap)
{
    if (p == NULL || out == NULL || cap < RCNODE_BAL_REPORT_LEN) {
        return 0u;
    }
    put_hdr(out, RCNODE_T_BAL_REPORT, &p->hdr);
    out[6] = p->flags;
    out[7] = p->quality;
    put16(out + 8, p->rpm);
    put_harm(out + 10, &p->h1);
    put_harm(out + 14, &p->h2);
    put_harm(out + 18, &p->blade);
    put16(out + 22, p->rms);
    return RCNODE_BAL_REPORT_LEN;
}

size_t rcnode_enc_status(const rcnode_status_t *p, uint8_t *out, size_t cap)
{
    if (p == NULL || out == NULL || cap < RCNODE_STATUS_LEN) {
        return 0u;
    }
    put_hdr(out, RCNODE_T_STATUS, &p->hdr);
    put16(out + 6, p->flags);
    put16(out + 8, p->vbat);
    out[10] = p->fw[0];
    out[11] = p->fw[1];
    out[12] = p->fw[2];
    out[13] = p->sensor;
    put16(out + 14, p->errors);
    put16(out + 16, p->resets);
    put16(out + 18, p->dropped);
    return RCNODE_STATUS_LEN;
}

size_t rcnode_enc_info(const rcnode_info_t *p, uint8_t *out, size_t cap)
{
    if (p == NULL || out == NULL || cap < RCNODE_INFO_LEN) {
        return 0u;
    }
    put_hdr(out, RCNODE_T_INFO, &p->hdr);
    out[6] = p->proto;
    out[7] = p->role;
    out[8] = p->board;
    out[9] = p->addr_type;
    memcpy(out + 10, p->addr, 6);
    out[16] = p->i228_addr;
    out[17] = p->adcrange;
    put16(out + 18, p->shunt_uohm);
    return RCNODE_INFO_LEN;
}

/* ----------------------------------------------------------- decode */

/**
 * @brief Read one harmonic.
 *
 * @param b The source, 4 bytes.
 * @param h Receives the harmonic.
 * @return false for a phase of RCNODE_PHASE_STEPS or more.
 */
static bool get_harm(const uint8_t *b, rcnode_harm_t *h)
{
    h->mag   = get16(b);
    h->phase = get16(b + 2);
    return h->phase < RCNODE_PHASE_STEPS;
}

/**
 * @brief Decode the body of a packet whose type and length are checked.
 *
 * @param b The bytes.
 * @param p Receives the packet.
 * @return RCNODE_OK or RCNODE_ERR_RANGE.
 */
static rcnode_err_t decode_body(const uint8_t *b, rcnode_pkt_t *p)
{
    switch (b[0]) {
    case RCNODE_T_PWR_WIN: {
        rcnode_pwr_win_t *w = &p->u.win;
        get_hdr(b, &w->hdr);
        w->flags  = b[6];
        w->n      = b[7];
        w->i_mean = as_i32(get32(b + 8));
        w->i_max  = as_i32(get32(b + 12));
        w->v_mean = get16(b + 16);
        w->v_min  = get16(b + 18);
        return RCNODE_OK;
    }
    case RCNODE_T_PWR_ACC: {
        rcnode_pwr_acc_t *a = &p->u.acc;
        get_hdr(b, &a->hdr);
        a->flags  = b[6];
        a->epoch  = b[7];
        a->charge = get40(b + 8);
        a->energy = get40(b + 13);
        a->temp   = as_i16(get16(b + 18));
        return RCNODE_OK;
    }
    case RCNODE_T_PWR_RUN: {
        rcnode_pwr_run_t *r = &p->u.run;
        get_hdr(b, &r->hdr);
        r->tag     = b[6];
        r->flags   = b[7];
        r->i_max   = as_i32(get32(b + 8));
        r->v_min   = get16(b + 12);
        r->windows = get32(b + 14);
        return RCNODE_OK;
    }
    case RCNODE_T_BAL_REPORT: {
        rcnode_bal_report_t *r = &p->u.bal;
        get_hdr(b, &r->hdr);
        r->flags   = b[6];
        r->quality = b[7];
        r->rpm     = get16(b + 8);
        const bool ok = get_harm(b + 10, &r->h1) & get_harm(b + 14, &r->h2)
                      & get_harm(b + 18, &r->blade);
        r->rms     = get16(b + 22);
        return (ok && r->quality <= 100u) ? RCNODE_OK : RCNODE_ERR_RANGE;
    }
    case RCNODE_T_STATUS: {
        rcnode_status_t *s = &p->u.status;
        get_hdr(b, &s->hdr);
        s->flags   = get16(b + 6);
        s->vbat    = get16(b + 8);
        s->fw[0]   = b[10];
        s->fw[1]   = b[11];
        s->fw[2]   = b[12];
        s->sensor  = b[13];
        s->errors  = get16(b + 14);
        s->resets  = get16(b + 16);
        s->dropped = get16(b + 18);
        return s->sensor < (uint8_t)RCNODE_SENSOR_COUNT ? RCNODE_OK
                                                        : RCNODE_ERR_RANGE;
    }
    default: {   /* RCNODE_T_INFO: the only other type with a length */
        rcnode_info_t *i = &p->u.info;
        get_hdr(b, &i->hdr);
        i->proto      = b[6];
        i->role       = b[7];
        i->board      = b[8];
        i->addr_type  = b[9];
        memcpy(i->addr, b + 10, 6);
        i->i228_addr  = b[16];
        i->adcrange   = b[17];
        i->shunt_uohm = get16(b + 18);
        return (i->proto != 0u && i->role != 0u) ? RCNODE_OK : RCNODE_ERR_RANGE;
    }
    }
}

rcnode_err_t rcnode_decode(const uint8_t *b, size_t n, rcnode_pkt_t *out)
{
    if (b == NULL || out == NULL) {
        return RCNODE_ERR_ARG;
    }
    if (n == 0u) {
        return RCNODE_ERR_LEN;
    }
    const size_t want = rcnode_len(b[0]);
    if (want == 0u) {
        return RCNODE_ERR_TYPE;
    }
    if (n != want) {
        return RCNODE_ERR_LEN;
    }
    rcnode_pkt_t p;
    memset(&p, 0, sizeof p);
    const rcnode_err_t e = decode_body(b, &p);
    if (e != RCNODE_OK) {
        return e;
    }
    get_hdr(b, &p.hdr);
    *out = p;
    return RCNODE_OK;
}

/* ---------------------------------------------------------- control */

/**
 * @brief The length of a control operation.
 *
 * @param op RCNODE_OP_*.
 * @return The bytes, 0 for an unknown operation.
 */
static size_t ctl_len(uint8_t op)
{
    switch (op) {
    case RCNODE_OP_SLEEP:
    case RCNODE_OP_OFF:    return 1u;
    case RCNODE_OP_RUN:
    case RCNODE_OP_BLADES: return 2u;
    case RCNODE_OP_RPM:    return 3u;
    default:               return 0u;
    }
}

/**
 * @brief Whether a control write's value is in range.
 *
 * @param c The write.
 * @return true when it is.
 */
static bool ctl_in_range(const rcnode_ctl_t *c)
{
    if (c->op == RCNODE_OP_BLADES) {
        return c->blades >= RCNODE_BLADES_MIN && c->blades <= RCNODE_BLADES_MAX;
    }
    if (c->op == RCNODE_OP_RPM) {
        return c->rpm <= RCNODE_RPM_MAX;
    }
    return true;
}

size_t rcnode_enc_ctl(const rcnode_ctl_t *c, uint8_t *out, size_t cap)
{
    if (c == NULL || out == NULL) {
        return 0u;
    }
    const size_t n = ctl_len(c->op);
    if (n == 0u || cap < n || !ctl_in_range(c)) {
        return 0u;
    }
    out[0] = c->op;
    if (c->op == RCNODE_OP_RUN) {
        out[1] = c->tag;
    } else if (c->op == RCNODE_OP_BLADES) {
        out[1] = c->blades;
    } else if (c->op == RCNODE_OP_RPM) {
        put16(out + 1, c->rpm);
    }
    return n;
}

rcnode_err_t rcnode_dec_ctl(const uint8_t *b, size_t n, rcnode_ctl_t *out)
{
    if (b == NULL || out == NULL) {
        return RCNODE_ERR_ARG;
    }
    if (n == 0u) {
        return RCNODE_ERR_LEN;
    }
    const size_t want = ctl_len(b[0]);
    if (want == 0u) {
        return RCNODE_ERR_TYPE;
    }
    if (n != want) {
        return RCNODE_ERR_LEN;
    }
    rcnode_ctl_t c;
    memset(&c, 0, sizeof c);
    c.op = b[0];
    if (c.op == RCNODE_OP_RUN) {
        c.tag = b[1];
    } else if (c.op == RCNODE_OP_BLADES) {
        c.blades = b[1];
    } else if (c.op == RCNODE_OP_RPM) {
        c.rpm = get16(b + 1);
    }
    if (!ctl_in_range(&c)) {
        return RCNODE_ERR_RANGE;
    }
    *out = c;
    return RCNODE_OK;
}

bool rcnode_ctl_for_role(uint8_t role, uint8_t op)
{
    if (role != RCNODE_ROLE_POWER && role != RCNODE_ROLE_BALANCE) {
        return false;
    }
    switch (op) {
    case RCNODE_OP_RUN:
    case RCNODE_OP_SLEEP:
    case RCNODE_OP_OFF:
        return true;
    case RCNODE_OP_BLADES:
    case RCNODE_OP_RPM:
        return role == RCNODE_ROLE_BALANCE;
    default:
        return false;
    }
}

/* ------------------------------------------------------- arithmetic */

uint8_t rcnode_seq_lost(uint8_t prev, uint8_t now)
{
    return (uint8_t)((uint8_t)(now - prev) - 1u);
}

int32_t rcnode_t_diff(uint32_t later, uint32_t earlier)
{
    return as_i32(later - earlier);
}

int64_t rcnode_sext40(uint64_t raw)
{
    const uint64_t v = raw & RCNODE_ACC_MASK;
    if ((v & 0x8000000000ull) == 0u) {
        return (int64_t)v;
    }
    /* v - 2^40, computed without an unsigned value past INT64_MAX. */
    return -(int64_t)(RCNODE_ACC_MASK - v) - 1;
}

uint64_t rcnode_energy_delta(uint64_t prev, uint64_t now)
{
    return (now - prev) & RCNODE_ACC_MASK;
}

int64_t rcnode_charge_delta(uint64_t prev, uint64_t now)
{
    return rcnode_sext40(now - prev);
}

uint16_t rcnode_v_field(int32_t uv)
{
    if (uv <= 0) {
        return 0u;
    }
    const uint32_t steps = ((uint32_t)uv + RCNODE_V_STEP_UV / 2u) / RCNODE_V_STEP_UV;
    return steps > UINT16_MAX ? (uint16_t)UINT16_MAX : (uint16_t)steps;
}
