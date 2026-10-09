/*
 * The panel's half of the SENSE and SERVO_SENSE link pages.  See
 * sense_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_link.h"

#include <stddef.h>
#include <string.h>

/* The three frames of the set-up, each four registers from its first. */
#define FRAME_N  4u
#define BUS_AT   ((uint8_t)LINK_SN_ENABLE)
#define I228_AT  ((uint8_t)LINK_SN_I228_ADDR)
#define I3221_AT ((uint8_t)LINK_SN_I3221_ADDR)

/* A status register by its page number. */
#define ST(reg) ((unsigned)(reg) - (unsigned)LINK_SN_FLAGS)

void sense_link_init(sense_link_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
        s->want_sda = -1;
        s->want_scl = -1;
    }
}

/* Everything read under one set-up, forgotten when another is taken or the
 * link goes: what was said about it is news again under the next. */
static void forget_reads(sense_link_t *s)
{
    s->have_status  = false;
    s->have_servo   = false;
    s->window_taken = false;
    s->asked_status = false;
    s->asked_servo  = false;
    s->was_flags    = 0u;
    s->was_clipped  = 0u;
    s->silent_told[0] = s->silent_told[1] = false;
    s->online_seen[0] = s->online_seen[1] = false;
    s->enc_silent_told = false;
    s->enc_online_seen = false;
    s->enc_magnet_told = false;
    s->enc_magnet      = 0u;
}

void sense_link_lost(sense_link_t *s)
{
    if (s == NULL) {
        return;
    }
    s->up            = false;
    s->page          = false;
    s->enc_page      = false;
    s->known         = false;
    s->refused_bus   = false;
    s->bus_told      = false;
    s->refused_i228  = false;
    s->refused_i3221 = false;
    s->caps_owed     = false;
    s->pending       = SENSE_LINK_OP_NONE;
    s->was_faults    = 0u;
    /* What the coprocessor that went said, and not yet shown, is about a
     * page nobody reads now; what the settings say stands. */
    s->events &= (uint32_t)(SENSE_LINK_EV_PINS_UNSET
                            | SENSE_LINK_EV_SAME_ADDR);
    s->clip_pending = 0u;
    forget_reads(s);
}

void sense_link_came_up(sense_link_t *s, uint16_t minor, uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    sense_link_lost(s);
    s->up   = true;
    s->page = minor >= SENSE_LINK_MINOR;
    s->enc_page = minor >= SENSE_LINK_ENC_MINOR;
    s->setup_ms = now_ms;
    if (!s->page && (s->want_i228 || s->want_i3221)) {
        s->events |= SENSE_LINK_EV_NO_PAGE;
    }
    if (!s->enc_page && s->want_enc) {
        s->events |= SENSE_LINK_EV_ENC_OLD;
    }
}

/* Whether @p w is a set-up SETUP can name: every value inside the page's
 * own range.  A zeroed one -- a snapshot nobody has published into -- is
 * not, and is no reason to write anything. */
static bool setup_valid(const sense_setup_t *w)
{
    return w->sda >= -1 && w->scl >= -1
           && w->i228_addr >= LINK_SN_I228_ADDR_MIN
           && w->i228_addr <= LINK_SN_I228_ADDR_MAX
           && w->i228_uohm >= LINK_SN_I228_UOHM_MIN
           && w->i228_uohm <= LINK_SN_I228_UOHM_MAX
           && w->i228_max_da >= LINK_SN_I228_DA_MIN
           && w->i228_max_da <= LINK_SN_I228_DA_MAX
           && w->i3221_addr >= LINK_SN_I3221_ADDR_MIN
           && w->i3221_addr <= LINK_SN_I3221_ADDR_MAX
           && w->i3221_dmohm >= LINK_SN_I3221_DMOHM_MIN
           && w->i3221_dmohm <= LINK_SN_I3221_DMOHM_MAX
           && w->i3221_ch != 0u && w->i3221_ch <= LINK_SN_I3221_CH_ALL;
}

void sense_link_want(sense_link_t *s, const sense_setup_t *w,
                     uint32_t now_ms)
{
    if (s == NULL || w == NULL || !setup_valid(w)) {
        return;
    }
    uint16_t next[LINK_SN_CONFIG_COUNT];
    memset(next, 0, sizeof(next));
    uint16_t en = (uint16_t)((w->i228 ? LINK_SN_EN_I228 : 0u)
                             | (w->i3221 ? LINK_SN_EN_I3221 : 0u)
                             | (w->as5600 ? LINK_SN_EN_AS5600 : 0u));
    uint32_t local = 0u;
    if (en != 0u && (w->sda < 0 || w->scl < 0)) {
        local = SENSE_LINK_EV_PINS_UNSET;
        en = 0u;
    } else if ((en & (LINK_SN_EN_I228 | LINK_SN_EN_I3221))
                   == (LINK_SN_EN_I228 | LINK_SN_EN_I3221)
               && w->i228_addr == w->i3221_addr) {
        local = SENSE_LINK_EV_SAME_ADDR;
        en = 0u;
    }
    next[LINK_SN_ENABLE]  = en;
    next[LINK_SN_SDA_PIN] = (w->sda < 0) ? 0u : (uint16_t)w->sda;
    next[LINK_SN_SCL_PIN] = (w->scl < 0) ? 0u : (uint16_t)w->scl;
    next[LINK_SN_KHZ]     = (uint16_t)LINK_SN_KHZ_BUS;
    next[LINK_SN_I228_ADDR]         = w->i228_addr;
    next[LINK_SN_I228_SHUNT_UOHM]   = w->i228_uohm;
    next[LINK_SN_I228_MAX_DA]       = w->i228_max_da;
    next[LINK_SN_I3221_ADDR]        = w->i3221_addr;
    next[LINK_SN_I3221_SHUNT_DMOHM] = w->i3221_dmohm;
    next[LINK_SN_I3221_CHANNELS]    = w->i3221_ch;

    const bool moved = !s->want_set
                       || memcmp(next, s->want, sizeof(next)) != 0
                       || s->want_i228 != w->i228
                       || s->want_i3221 != w->i3221
                       || s->want_enc != w->as5600
                       || s->want_sda != w->sda || s->want_scl != w->scl;
    if (!moved) {
        return;
    }
    /* A part enabled now, with a coprocessor answering that cannot read
     * it: said now, not at the next link-up. */
    const bool grew = (w->i228 && !s->want_i228)
                      || (w->i3221 && !s->want_i3221);
    if (s->up && !s->page && grew) {
        local |= SENSE_LINK_EV_NO_PAGE;
    }
    if (s->up && !s->enc_page && w->as5600 && !s->want_enc) {
        local |= SENSE_LINK_EV_ENC_OLD;
    }
    /*
     * What was said about the request this one replaces and not yet shown
     * no longer describes anything asked: unset pins, one address and the
     * refusals go, and come back only if they still hold.  A missing page
     * waiting stays while a part is still enabled.
     */
    uint32_t stale = (uint32_t)(SENSE_LINK_EV_PINS_UNSET
                                | SENSE_LINK_EV_SAME_ADDR
                                | SENSE_LINK_EV_BUS_REFUSED
                                | SENSE_LINK_EV_I228_REFUSED
                                | SENSE_LINK_EV_I3221_REFUSED);
    if (!w->i228 && !w->i3221) {
        stale |= (uint32_t)SENSE_LINK_EV_NO_PAGE;
    }
    if (!w->as5600) {
        stale |= (uint32_t)(SENSE_LINK_EV_ENC_OLD | SENSE_LINK_EV_ENC_SILENT
                            | SENSE_LINK_EV_ENC_MAGNET);
    }
    s->events &= ~stale;
    /* The first set-up is the one the panel starts with, not an edit: it
     * is due at once. */
    s->want_ms = s->want_set ? now_ms
                             : (uint32_t)(now_ms - SENSE_LINK_SETTLE_MS);
    s->want_set   = true;
    s->want_i228  = w->i228;
    s->want_i3221 = w->i3221;
    s->want_enc   = w->as5600;
    s->want_sda   = w->sda;
    s->want_scl   = w->scl;
    memcpy(s->want, next, sizeof(next));
    /* A new set-up is a new question: what was refused is asked again. */
    s->refused_bus   = false;
    s->bus_told      = false;
    s->refused_i228  = false;
    s->refused_i3221 = false;
    s->events |= local;
}

/* The bus frame as it is to be written: the pins asked and the parts
 * whose own frames the page has not refused.  The encoder's bit goes only
 * to a coprocessor that has it. */
static void bus_frame(const sense_link_t *s, uint16_t *out)
{
    uint16_t en = s->want[LINK_SN_ENABLE];
    if (!s->enc_page) {
        en &= (uint16_t)~LINK_SN_EN_AS5600;
    }
    if (s->refused_i228) {
        en &= (uint16_t)~LINK_SN_EN_I228;
    }
    if (s->refused_i3221) {
        en &= (uint16_t)~LINK_SN_EN_I3221;
    }
    out[0] = en;
    out[1] = s->want[LINK_SN_SDA_PIN];
    out[2] = s->want[LINK_SN_SCL_PIN];
    out[3] = (uint16_t)LINK_SN_KHZ_BUS;
}

static bool frame_differs(const sense_link_t *s, uint8_t at)
{
    return memcmp(&s->want[at], &s->held[at],
                  FRAME_N * sizeof(uint16_t)) != 0;
}

/* The write owed now, or NONE, with its registers in @p regs. */
static sense_link_op_kind_t write_owed(const sense_link_t *s,
                                       uint16_t *regs)
{
    const bool i228  = !s->refused_i228 && frame_differs(s, I228_AT);
    const bool i3221 = !s->refused_i3221 && frame_differs(s, I3221_AT);
    /* The page holds two parts to two addresses only while both are
     * enabled, so that is when a part's frame goes with both off. */
    const uint16_t both = (uint16_t)(LINK_SN_EN_I228 | LINK_SN_EN_I3221);
    if ((i228 || i3221) && (s->held[LINK_SN_ENABLE] & both) == both) {
        if (s->refused_bus) {
            return SENSE_LINK_OP_NONE;
        }
        regs[0] = 0u;
        regs[1] = s->held[LINK_SN_SDA_PIN];
        regs[2] = s->held[LINK_SN_SCL_PIN];
        regs[3] = (uint16_t)LINK_SN_KHZ_BUS;
        return SENSE_LINK_OP_OFF;
    }
    if (i228) {
        memcpy(regs, &s->want[I228_AT], FRAME_N * sizeof(uint16_t));
        return SENSE_LINK_OP_I228;
    }
    if (i3221) {
        memcpy(regs, &s->want[I3221_AT], FRAME_N * sizeof(uint16_t));
        return SENSE_LINK_OP_I3221;
    }
    uint16_t bus[FRAME_N];
    bus_frame(s, bus);
    if (!s->refused_bus
        && memcmp(bus, &s->held[BUS_AT], sizeof(bus)) != 0) {
        memcpy(regs, bus, sizeof(bus));
        return SENSE_LINK_OP_BUS;
    }
    return SENSE_LINK_OP_NONE;
}

static void read_op(sense_link_op_t *op, sense_link_op_kind_t kind,
                    uint8_t page, uint8_t off, uint8_t n)
{
    op->kind  = kind;
    op->write = false;
    op->page  = page;
    op->off   = off;
    op->n     = n;
}

static bool due(bool asked, uint32_t asked_ms, uint32_t now_ms,
                uint32_t every)
{
    return !asked || (uint32_t)(now_ms - asked_ms) >= every;
}

/* The registers a SENSE status read takes: to the encoder's last when the
 * page has it enabled, else to ESC_FLAGS. */
static unsigned status_count(const sense_link_t *s)
{
    return ((s->held[LINK_SN_ENABLE] & LINK_SN_EN_AS5600) != 0u)
               ? SENSE_LINK_STATUS_COUNT
               : SENSE_LINK_STATUS_COUNT_V48;
}

bool sense_link_next(sense_link_t *s, uint32_t now_ms, bool idle,
                     sense_link_op_t *op)
{
    if (s == NULL || op == NULL) {
        return false;
    }
    memset(op, 0, sizeof(*op));
    s->pending = SENSE_LINK_OP_NONE;
    if (!s->page) {
        return false;
    }
    if (!s->known) {
        read_op(op, SENSE_LINK_OP_READ_SETUP, (uint8_t)LINK_PAGE_SENSE, 0u,
                (uint8_t)LINK_SN_CONFIG_COUNT);
        s->pending = op->kind;
        return true;
    }
    if (s->refused_bus && s->bus_retry
        && (uint32_t)(now_ms - s->bus_refused_ms) >= SENSE_LINK_BUS_RETRY_MS) {
        s->refused_bus = false;
    }
    if (s->want_set) {
        uint16_t regs[FRAME_N];
        const sense_link_op_kind_t w = write_owed(s, regs);
        const bool rested =
            (uint32_t)(now_ms - s->want_ms) >= SENSE_LINK_SETTLE_MS;
        if (w != SENSE_LINK_OP_NONE && idle && rested) {
            op->kind  = w;
            op->write = true;
            op->page  = (uint8_t)LINK_PAGE_SENSE;
            op->off   = (w == SENSE_LINK_OP_I228)    ? I228_AT
                        : (w == SENSE_LINK_OP_I3221) ? I3221_AT : BUS_AT;
            op->n     = (uint8_t)FRAME_N;
            memcpy(op->regs, regs, sizeof(regs));
            memcpy(s->out, regs, sizeof(regs));
            s->pending = w;
            return true;
        }
        /* The capability bits once nothing more can be written now. */
        if (s->caps_owed && (w == SENSE_LINK_OP_NONE || !idle)) {
            read_op(op, SENSE_LINK_OP_IDENTITY, (uint8_t)LINK_PAGE_IDENTITY,
                    0u, (uint8_t)LINK_ID_COUNT);
            s->pending = op->kind;
            return true;
        }
    }
    const uint16_t en = s->held[LINK_SN_ENABLE];
    if (en != 0u
        && due(s->asked_status, s->status_asked_ms, now_ms,
               SENSE_LINK_READ_MS)) {
        read_op(op, SENSE_LINK_OP_STATUS, (uint8_t)LINK_PAGE_SENSE,
                (uint8_t)LINK_SN_FLAGS, (uint8_t)status_count(s));
        s->pending = op->kind;
        s->asked_status = true;
        s->status_asked_ms = now_ms;
        return true;
    }
    if ((en & LINK_SN_EN_I3221) != 0u
        && due(s->asked_servo, s->servo_asked_ms, now_ms,
               SENSE_LINK_SERVO_MS)) {
        read_op(op, SENSE_LINK_OP_SERVO, (uint8_t)LINK_PAGE_SERVO_SENSE, 0u,
                (uint8_t)SENSE_LINK_SERVO_COUNT);
        s->pending = op->kind;
        s->asked_servo = true;
        s->servo_asked_ms = now_ms;
        return true;
    }
    return false;
}

/* A set-up was taken, or first read: what was read and said under the old
 * one is gone, and the wait before "not answering" starts again. */
static void new_setup(sense_link_t *s, uint32_t now_ms)
{
    s->setup_ms = now_ms;
    forget_reads(s);
    /* An event a read raised and the band has not shown yet is about the
     * set-up that was: shown now, it would name this one's address, ID or
     * shunt.  What describes the set-up asked -- unset pins, one address,
     * a refusal -- and the store stand. */
    s->events &= ~(uint32_t)(SENSE_LINK_EV_I228_SILENT
                             | SENSE_LINK_EV_I3221_SILENT
                             | SENSE_LINK_EV_I228_WRONG
                             | SENSE_LINK_EV_I3221_WRONG
                             | SENSE_LINK_EV_STUCK
                             | SENSE_LINK_EV_I228_CLIPPED
                             | SENSE_LINK_EV_I3221_CLIPPED
                             | SENSE_LINK_EV_ENC_SILENT
                             | SENSE_LINK_EV_ENC_MAGNET);
    s->clip_pending = 0u;
}

static void written(sense_link_t *s, sense_link_op_kind_t w, int result,
                    uint32_t now_ms)
{
    if (result == SENSE_LINK_ACK) {
        const uint8_t at = (w == SENSE_LINK_OP_I228)    ? I228_AT
                           : (w == SENSE_LINK_OP_I3221) ? I3221_AT : BUS_AT;
        memcpy(&s->held[at], s->out, FRAME_N * sizeof(uint16_t));
        s->caps_owed = true;
        new_setup(s, now_ms);
        return;
    }
    switch (w) {
    case SENSE_LINK_OP_I228:
        s->refused_i228 = true;
        s->events |= SENSE_LINK_EV_I228_REFUSED;
        break;
    case SENSE_LINK_OP_I3221:
        s->refused_i3221 = true;
        s->events |= SENSE_LINK_EV_I3221_REFUSED;
        break;
    case SENSE_LINK_OP_OFF:
        /* Nothing is changed under a page that will not let go of its
         * parts: every frame waits for the next edit. */
        s->refused_i228  = true;
        s->refused_i3221 = true;
        s->refused_bus   = true;
        s->bus_retry     = false;
        s->events |= SENSE_LINK_EV_BUS_REFUSED;
        break;
    case SENSE_LINK_OP_BUS:
    default:
        s->refused_bus = true;
        if (!s->bus_told) {
            s->events |= SENSE_LINK_EV_BUS_REFUSED;
            s->bus_told = true;
        }
        /* One I2C block's pair refused is pins something else holds -- an
         * output, the SUPPLY page, the board -- and that can let go: the
         * frame is offered again, quietly, every SENSE_LINK_BUS_RETRY_MS.
         * Any other pair is refused for itself, until the next edit. */
        s->bus_retry = (s->out[1] % 2u) == 0u
                       && s->out[2] == (uint16_t)(s->out[1] + 1u);
        s->bus_refused_ms = now_ms;
        break;
    }
}

static const uint16_t k_online[2] = { LINK_SN_I228_ONLINE,
                                      LINK_SN_I3221_ONLINE };
static const uint16_t k_wrong[2]  = { LINK_SN_I228_ID_WRONG,
                                      LINK_SN_I3221_ID_WRONG };
static const uint16_t k_en[2]     = { LINK_SN_EN_I228, LINK_SN_EN_I3221 };
static const uint32_t k_silent[2] = { SENSE_LINK_EV_I228_SILENT,
                                      SENSE_LINK_EV_I3221_SILENT };
static const uint32_t k_wrong_ev[2] = { SENSE_LINK_EV_I228_WRONG,
                                        SENSE_LINK_EV_I3221_WRONG };

/* The encoder's part of a SENSE read: not answering, once until it does,
 * and a magnet that is missing, weak or strong, once until it is right. */
static void judge_enc(sense_link_t *s, uint32_t now_ms, bool waited)
{
    (void)now_ms;
    if ((s->held[LINK_SN_ENABLE] & LINK_SN_EN_AS5600) == 0u) {
        s->enc_silent_told = false;
        s->enc_online_seen = false;
        s->enc_magnet_told = false;
        return;
    }
    const uint16_t f = s->status[ST(LINK_SN_AS5600_FLAGS)];
    if ((f & LINK_SN_ENC_ONLINE) == 0u) {
        s->enc_magnet_told = false;
        if (!s->enc_silent_told && (s->enc_online_seen || waited)) {
            s->events |= SENSE_LINK_EV_ENC_SILENT;
            s->enc_silent_told = true;
            s->enc_online_seen = false;
        }
        return;
    }
    s->enc_silent_told = false;
    s->enc_online_seen = true;
    s->events &= ~(uint32_t)SENSE_LINK_EV_ENC_SILENT;
    const uint16_t bits = (uint16_t)(f & (LINK_SN_ENC_MD | LINK_SN_ENC_ML
                                          | LINK_SN_ENC_MH));
    const bool fine = (bits & LINK_SN_ENC_MD) != 0u
                      && (bits & (LINK_SN_ENC_ML | LINK_SN_ENC_MH)) == 0u;
    if (fine) {
        s->enc_magnet_told = false;
        s->events &= ~(uint32_t)SENSE_LINK_EV_ENC_MAGNET;
        s->enc_magnet = bits;
        return;
    }
    s->enc_magnet = bits;
    if (!s->enc_magnet_told) {
        s->events |= SENSE_LINK_EV_ENC_MAGNET;
        s->enc_magnet_told = true;
    }
}

/* What a SENSE read says that the operator is told. */
static void judge_status(sense_link_t *s, uint32_t now_ms)
{
    const uint16_t f   = s->status[ST(LINK_SN_FLAGS)];
    const uint16_t was = s->was_flags;
    const uint16_t en  = s->held[LINK_SN_ENABLE];
    const bool waited  = (uint32_t)(now_ms - s->setup_ms)
                         >= SENSE_LINK_GRACE_MS;
    for (unsigned p = 0u; p < 2u; ++p) {
        if ((en & k_en[p]) == 0u) {
            s->silent_told[p] = false;
            s->online_seen[p] = false;
            continue;
        }
        if ((f & k_online[p]) != 0u) {
            s->silent_told[p] = false;
            s->online_seen[p] = true;
            /* Answering as itself now: a "not answering" or "another
             * identity" still waiting for the band is over. */
            s->events &= ~(uint32_t)(k_silent[p] | k_wrong_ev[p]);
            continue;
        }
        if ((f & k_wrong[p]) != 0u) {
            if ((was & k_wrong[p]) == 0u) {
                s->events |= k_wrong_ev[p];
                /* The ID it gave, as the band will say it: a later read
                 * may show another. */
                s->ev_id[p] = s->status[(p == 0u) ? ST(LINK_SN_I228_ID)
                                                  : ST(LINK_SN_I3221_ID)];
            }
            continue;
        }
        if (!s->silent_told[p] && (s->online_seen[p] || waited)) {
            s->events |= k_silent[p];
            s->silent_told[p] = true;
            s->online_seen[p] = false;
            /* And what answered elsewhere, by this read's scan. */
            s->ev_found[p] = sense_link_found(s, (sense_link_part_t)p);
        }
    }
    if ((f & LINK_SN_BUS_STUCK) != 0u && (was & LINK_SN_BUS_STUCK) == 0u) {
        s->events |= SENSE_LINK_EV_STUCK;
    }
    if ((en & LINK_SN_EN_I228) != 0u && (f & LINK_SN_I228_CLIPPED) != 0u
        && (was & LINK_SN_I228_CLIPPED) == 0u) {
        s->events |= SENSE_LINK_EV_I228_CLIPPED;
    }
    s->was_flags = f;
    judge_enc(s, now_ms, waited);
}

/* What a SERVO_SENSE read says: a channel read newly clipped, of the
 * channels the page reads. */
static void judge_servo(sense_link_t *s)
{
    const uint16_t chans = s->held[LINK_SN_I3221_CHANNELS];
    uint16_t clipped = 0u;
    for (unsigned ch = 1u; ch <= LINK_SS_CHANNELS; ++ch) {
        if ((chans & (1u << (ch - 1u))) != 0u
            && (s->servo[LINK_SS_CH_FLAGS] & LINK_SS_CH_CLIPPED(ch)) != 0u) {
            clipped |= LINK_SS_CH_CLIPPED(ch);
        }
    }
    const uint16_t fresh = (uint16_t)(clipped & (uint16_t)~s->was_clipped);
    /* Every channel newly clipped waits its own turn on the band: CH2 and
     * CH3 move together in a synchronised pair. */
    for (unsigned ch = LINK_SS_CHANNELS; ch >= 1u; --ch) {
        if ((fresh & LINK_SS_CH_CLIPPED(ch)) != 0u) {
            s->clip_pending |= (uint8_t)(1u << (ch - 1u));
            s->clipped_ch = (uint8_t)ch;   /* the lowest, for events() */
            s->events |= SENSE_LINK_EV_I3221_CLIPPED;
        }
    }
    s->was_clipped = clipped;
}

void sense_link_done(sense_link_t *s, int result, const uint16_t *regs,
                     uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    const sense_link_op_kind_t op = s->pending;
    s->pending = SENSE_LINK_OP_NONE;
    if (result == SENSE_LINK_NO_ANSWER || op == SENSE_LINK_OP_NONE) {
        return;                         /* owed still: asked again */
    }
    switch (op) {
    case SENSE_LINK_OP_OFF:
    case SENSE_LINK_OP_I228:
    case SENSE_LINK_OP_I3221:
    case SENSE_LINK_OP_BUS:
        written(s, op, result, now_ms);
        return;
    case SENSE_LINK_OP_IDENTITY:
        s->caps_owed = false;
        if (result == SENSE_LINK_ACK && regs != NULL) {
            s->caps     = regs[LINK_ID_CAPABILITIES];
            s->caps_new = true;
        }
        return;
    default:
        break;
    }
    if (result != SENSE_LINK_ACK || regs == NULL) {
        /* A read refused: a coprocessor that names 4.7 and has not the
         * page.  Nothing more is sent to it until the link comes up
         * again. */
        s->page = false;
        if (s->want_i228 || s->want_i3221) {
            s->events |= SENSE_LINK_EV_NO_PAGE;
        }
        return;
    }
    switch (op) {
    case SENSE_LINK_OP_READ_SETUP:
        memcpy(s->held, regs, sizeof(s->held));
        s->known = true;
        new_setup(s, now_ms);
        break;
    case SENSE_LINK_OP_STATUS:
        memset(s->status, 0, sizeof(s->status));
        memcpy(s->status, regs, status_count(s) * sizeof(uint16_t));
        s->have_status = true;
        s->status_ms   = now_ms;
        ++s->status_reads;
        judge_status(s, now_ms);
        break;
    case SENSE_LINK_OP_SERVO:
        memcpy(s->servo, regs, sizeof(s->servo));
        s->have_servo = true;
        judge_servo(s);
        break;
    default:
        break;
    }
}

void sense_link_faults(sense_link_t *s, uint16_t faults)
{
    if (s == NULL) {
        return;
    }
    if ((faults & LINK_FAULT_STORE_OFF) != 0u
        && (s->was_faults & LINK_FAULT_STORE_OFF) == 0u) {
        s->events |= SENSE_LINK_EV_STORE_OFF;
    }
    s->was_faults = faults;
}

uint32_t sense_link_events(sense_link_t *s)
{
    if (s == NULL) {
        return 0u;
    }
    const uint32_t e = s->events;
    s->events = 0u;
    s->clip_pending = 0u;
    return e;
}

uint32_t sense_link_event(sense_link_t *s, uint32_t now_ms)
{
    /* Most pressing first: what stops the monitors being read at all, then
     * what makes a reading absent or wrong, then what makes it a bound. */
    static const uint32_t k_order[] = {
        SENSE_LINK_EV_NO_PAGE,      SENSE_LINK_EV_PINS_UNSET,
        SENSE_LINK_EV_SAME_ADDR,    SENSE_LINK_EV_BUS_REFUSED,
        SENSE_LINK_EV_I228_REFUSED, SENSE_LINK_EV_I3221_REFUSED,
        SENSE_LINK_EV_STUCK,        SENSE_LINK_EV_I228_SILENT,
        SENSE_LINK_EV_I3221_SILENT, SENSE_LINK_EV_I228_WRONG,
        SENSE_LINK_EV_I3221_WRONG,  SENSE_LINK_EV_I228_CLIPPED,
        SENSE_LINK_EV_I3221_CLIPPED, SENSE_LINK_EV_STORE_OFF,
        SENSE_LINK_EV_ENC_OLD,      SENSE_LINK_EV_ENC_SILENT,
        SENSE_LINK_EV_ENC_MAGNET,
    };
    if (s == NULL || s->events == 0u
        || (s->event_given
            && (uint32_t)(now_ms - s->event_ms) < SENSE_LINK_EVENT_GAP_MS)) {
        return 0u;
    }
    for (size_t i = 0u; i < sizeof(k_order) / sizeof(k_order[0]); ++i) {
        if ((s->events & k_order[i]) == 0u) {
            continue;
        }
        s->event_given = true;
        s->event_ms    = now_ms;
        if (k_order[i] == SENSE_LINK_EV_I3221_CLIPPED) {
            /* One channel a turn, the lowest; the event stands while
             * another waits. */
            for (unsigned ch = 1u; ch <= LINK_SS_CHANNELS; ++ch) {
                const uint8_t bit = (uint8_t)(1u << (ch - 1u));
                if ((s->clip_pending & bit) != 0u) {
                    s->clip_pending &= (uint8_t)~bit;
                    s->clipped_ch = (uint8_t)ch;
                    break;
                }
            }
            if (s->clip_pending == 0u) {
                s->events &= ~k_order[i];
            }
            return k_order[i];
        }
        s->events &= ~k_order[i];
        return k_order[i];
    }
    s->events = 0u;                 /* no bit this build knows */
    return 0u;
}

void sense_link_event_back(sense_link_t *s, uint32_t ev)
{
    if (s == NULL || ev == 0u) {
        return;
    }
    /* What the settings say holds whatever answers; the rest is about the
     * coprocessor that answers now, and a link gone since took it. */
    const uint32_t settings = (uint32_t)(SENSE_LINK_EV_PINS_UNSET
                                         | SENSE_LINK_EV_SAME_ADDR);
    if (!s->up && (ev & ~settings) != 0u) {
        return;
    }
    if (ev == SENSE_LINK_EV_I3221_CLIPPED && s->clipped_ch >= 1u
        && s->clipped_ch <= LINK_SS_CHANNELS) {
        s->clip_pending |= (uint8_t)(1u << (s->clipped_ch - 1u));
    }
    s->events |= ev;
}

bool sense_link_settled(const sense_link_t *s)
{
    if (s == NULL) {
        return false;
    }
    if (!s->page) {
        return true;
    }
    uint16_t regs[FRAME_N];
    return s->known && write_owed(s, regs) == SENSE_LINK_OP_NONE;
}

bool sense_link_take_caps(sense_link_t *s, uint16_t *caps)
{
    if (s == NULL || caps == NULL || !s->caps_new) {
        return false;
    }
    *caps = s->caps;
    s->caps_new = false;
    return true;
}

bool sense_link_take_window(sense_link_t *s, bench_state_t *b)
{
    if (s == NULL || b == NULL || !s->have_servo) {
        return false;
    }
    const uint16_t flags = s->servo[LINK_SS_CH_FLAGS];
    uint8_t ok = 0u;
    for (unsigned ch = 1u; ch <= LINK_SS_CHANNELS; ++ch) {
        if ((flags & LINK_SS_CH_VALID(ch)) != 0u) {
            ok |= (uint8_t)(1u << (ch - 1u));
        }
    }
    const uint16_t window = s->servo[LINK_SS_WINDOW];
    if (ok == 0u || (s->window_taken && window == s->window_last)) {
        return false;
    }
    s->window_taken = true;
    s->window_last  = window;
    b->servo_new    = true;
    b->servo_window = window;
    b->servo_ok     = ok;
    for (size_t i = 0u; i < LINK_SS_CHANNELS; ++i) {
        const uint16_t *c = &s->servo[i * (size_t)LINK_SS_CH_STRIDE];
        b->servo_mean_ma[i] = (int16_t)c[LINK_SS_CH_MEAN_MA];
        b->servo_max_ma[i]  = (int16_t)c[LINK_SS_CH_MAX_MA];
        b->servo_min_mv[i]  = c[LINK_SS_CH_MIN_MV];
    }
    return true;
}

uint16_t sense_link_flags(const sense_link_t *s)
{
    return (s != NULL && s->have_status) ? s->status[ST(LINK_SN_FLAGS)] : 0u;
}

uint32_t sense_link_reads(const sense_link_t *s)
{
    return (s != NULL) ? s->status_reads : 0u;
}

bool sense_link_esc(const sense_link_t *s, uint32_t now_ms, bool *v_ok,
                    float *volts, bool *i_ok, float *amps)
{
    if (v_ok == NULL || volts == NULL || i_ok == NULL || amps == NULL) {
        return false;
    }
    *v_ok  = false;
    *i_ok  = false;
    *volts = 0.0f;
    *amps  = 0.0f;
    if (s == NULL || !s->have_status
        || (uint32_t)(now_ms - s->status_ms) >= SENSE_LINK_STALE_MS) {
        return false;
    }
    const uint16_t f = s->status[ST(LINK_SN_ESC_FLAGS)];
    *v_ok  = (f & LINK_SN_ESC_VOLTAGE_OK) != 0u;
    *i_ok  = (f & LINK_SN_ESC_CURRENT_OK) != 0u;
    *volts = (float)s->status[ST(LINK_SN_ESC_VOLTAGE_CV)] / 100.0f;
    *amps  = (float)s->status[ST(LINK_SN_ESC_CURRENT_CA)] / 100.0f;
    return true;
}

bool sense_link_totals(const sense_link_t *s, uint32_t now_ms,
                       int32_t *charge_cmah, uint32_t *energy_cwh)
{
    if (s == NULL || charge_cmah == NULL || energy_cwh == NULL
        || !s->have_status
        || (uint32_t)(now_ms - s->status_ms) >= SENSE_LINK_TOTALS_MS) {
        return false;
    }
    const uint32_t c = (uint32_t)s->status[ST(LINK_SN_I228_CHARGE_LO)]
                       | ((uint32_t)s->status[ST(LINK_SN_I228_CHARGE_HI)]
                          << 16);
    *charge_cmah = (int32_t)c;
    *energy_cwh  = (uint32_t)s->status[ST(LINK_SN_I228_ENERGY_LO)]
                   | ((uint32_t)s->status[ST(LINK_SN_I228_ENERGY_HI)] << 16);
    return true;
}

bool sense_link_enc_on(const sense_link_t *s)
{
    return s != NULL && s->known
           && (s->held[LINK_SN_ENABLE] & LINK_SN_EN_AS5600) != 0u;
}

bool sense_link_enc(const sense_link_t *s, uint32_t now_ms,
                    sense_link_enc_t *out)
{
    if (out == NULL || !sense_link_enc_on(s) || !s->have_status
        || (uint32_t)(now_ms - s->status_ms) >= SENSE_LINK_STALE_MS) {
        return false;
    }
    const uint16_t f = s->status[ST(LINK_SN_AS5600_FLAGS)];
    if ((f & LINK_SN_ENC_ONLINE) == 0u || (f & LINK_SN_ENC_VALID) == 0u) {
        return false;
    }
    out->raw      = s->status[ST(LINK_SN_AS5600_ANGLE)];
    out->samples  = s->status[ST(LINK_SN_AS5600_SAMPLES)];
    out->still_ms = s->status[ST(LINK_SN_AS5600_STILL_MS)];
    out->taken_ms = s->status_ms;
    out->magnet   = (f & LINK_SN_ENC_MD) != 0u;
    out->weak     = (f & LINK_SN_ENC_ML) != 0u;
    out->strong   = (f & LINK_SN_ENC_MH) != 0u;
    return true;
}

uint16_t sense_link_enc_magnet(const sense_link_t *s)
{
    return (s != NULL) ? s->enc_magnet : 0u;
}

uint8_t sense_link_addr(const sense_link_t *s, sense_link_part_t part)
{
    if (s == NULL || !s->known) {
        return 0u;
    }
    return (uint8_t)s->held[(part == SENSE_LINK_INA228) ? LINK_SN_I228_ADDR
                                                        : LINK_SN_I3221_ADDR];
}

uint16_t sense_link_id(const sense_link_t *s, sense_link_part_t part)
{
    if (s == NULL || !s->have_status) {
        return 0u;
    }
    return s->status[(part == SENSE_LINK_INA228) ? ST(LINK_SN_I228_ID)
                                                 : ST(LINK_SN_I3221_ID)];
}

uint8_t sense_link_found(const sense_link_t *s, sense_link_part_t part)
{
    if (s == NULL || !s->have_status || !s->known) {
        return 0u;
    }
    const uint16_t present = s->status[ST(LINK_SN_PRESENT)];
    const bool i228 = part == SENSE_LINK_INA228;
    const unsigned last = i228 ? LINK_SN_I228_ADDR_MAX : LINK_SN_I3221_ADDR_MAX;
    const sense_link_part_t other = i228 ? SENSE_LINK_INA3221
                                         : SENSE_LINK_INA228;
    const bool other_on = (s->held[LINK_SN_ENABLE] & k_en[other]) != 0u;
    const unsigned mine = sense_link_addr(s, part);
    const unsigned theirs = sense_link_addr(s, other);
    for (unsigned a = 0x40u; a <= last; ++a) {
        if ((present & (1u << (a - 0x40u))) == 0u || a == mine
            || (other_on && a == theirs)) {
            continue;
        }
        return (uint8_t)a;
    }
    return 0u;
}

uint16_t sense_link_event_id(const sense_link_t *s, sense_link_part_t part)
{
    return (s != NULL && (unsigned)part < 2u) ? s->ev_id[part] : 0u;
}

uint8_t sense_link_event_found(const sense_link_t *s, sense_link_part_t part)
{
    return (s != NULL && (unsigned)part < 2u) ? s->ev_found[part] : 0u;
}

uint8_t sense_link_clipped_channel(const sense_link_t *s)
{
    return (s != NULL) ? s->clipped_ch : 0u;
}

uint16_t sense_link_i3221_dmohm(const sense_link_t *s)
{
    return (s != NULL) ? s->held[LINK_SN_I3221_SHUNT_DMOHM] : 0u;
}

int sense_link_sda(const sense_link_t *s)
{
    return (s != NULL) ? (int)s->want_sda : -1;
}

int sense_link_scl(const sense_link_t *s)
{
    return (s != NULL) ? (int)s->want_scl : -1;
}
