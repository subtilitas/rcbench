/**
 * @file
 * @brief The panel's half of the SENSE, SERVO_SENSE and SERVO_WIN link pages.
 *
 * See sense_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_link.h"
#include "sense_link_int.h"

#include <stddef.h>
#include <string.h>

void sense_link_init(sense_link_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
        s->want_sda = -1;
        s->want_scl = -1;
    }
}

/**
 * @brief Everything read under one set-up, forgotten when another is taken or
 *        the link goes: what was said about it is news again under the next.
 *
 * Not the magnet: its bits are the part's and the shaft's, not the set-up's,
 * and a weak magnet said once is not said again for an edit of another row
 * (judge_enc()).
 *
 * @param s The link state.
 */
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
    /* The ring's cursor goes too, and the windows not yet taken: they are
     * of a set-up or a link that is gone. */
    s->asked_win   = false;
    s->win_all     = false;
    s->win_synced  = false;
    s->win_restart = false;
    s->win_have    = false;
    s->win_valid   = false;
    s->win_stands  = false;
    s->winq_n      = 0u;
    ++s->setups;
}

void sense_link_lost(sense_link_t *s)
{
    if (s == NULL) {
        return;
    }
    s->up            = false;
    s->page          = false;
    s->enc_page      = false;
    s->win_page      = false;
    s->known         = false;
    s->refused_bus   = false;
    s->bus_told      = false;
    s->refused_i228  = false;
    s->refused_i3221 = false;
    s->caps_owed     = false;
    s->pending       = SENSE_LINK_OP_NONE;
    s->was_faults    = 0u;
    /* What the IOmcu that went said, and not yet shown, is about a
     * page nobody reads now; what the settings say stands. */
    s->events &= (uint32_t)(SENSE_LINK_EV_PINS_UNSET
                            | SENSE_LINK_EV_SAME_ADDR);
    s->clip_pending = 0u;
    forget_reads(s);
    /* Another IOmcu may answer next, or the same one restarted: its
     * magnet is news. */
    s->enc_magnet_told = false;
    s->enc_magnet      = 0u;
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
    s->win_page = minor >= SENSE_LINK_WIN_MINOR;
    s->setup_ms = now_ms;
    if (!s->page && (s->want_i228 || s->want_i3221)) {
        s->events |= SENSE_LINK_EV_NO_PAGE;
    }
    if (!s->enc_page && s->want_enc) {
        s->events |= SENSE_LINK_EV_ENC_OLD;
    }
}

/**
 * @brief Whether the INA228 at @p i228 shares an address with the INA3221: one
 *        of the fitted parts @p parts, or with none given the one at @p i3221.
 *
 * @param i228  The INA228 address.
 * @param i3221 The INA3221 address.
 * @param parts The fitted INA3221 parts, one bit each, or 0.
 * @return true when the addresses are the same.
 */
static bool one_address(uint16_t i228, uint16_t i3221, uint8_t parts)
{
    if (parts != 0u) {
        return i228 >= LINK_SN_I3221_ADDR_MIN && i228 <= LINK_SN_I3221_ADDR_MAX
               && ((parts >> (i228 - LINK_SN_I3221_ADDR_MIN)) & 1u) != 0u;
    }
    return i228 == i3221;
}

/**
 * @brief Whether @p w is a set-up SETUP can name: every value inside the page's
 *        own range.
 *
 * A zeroed one -- a snapshot nobody has published into -- is
 * not, and is no reason to write anything.
 *
 * @param w The set-up.
 * @return true when every value is inside the page's range.
 */
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
               && one_address(w->i228_addr, w->i3221_addr, w->i3221_parts)) {
        local = SENSE_LINK_EV_SAME_ADDR;
        en = (uint16_t)(en & ~(LINK_SN_EN_I228 | LINK_SN_EN_I3221));
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
                       || s->want_sda != w->sda || s->want_scl != w->scl
                       || s->want_parts != w->i3221_parts
                       || s->want_unwatched != w->i3221_unwatched
                       || s->want_off_ch1 != w->i3221_fast_off_ch1;
    if (!moved) {
        return;
    }
    /* A part enabled now, with an IOmcu answering that cannot read
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
    s->want_parts = w->i3221_parts;
    s->want_unwatched = w->i3221_unwatched;
    s->want_off_ch1   = w->i3221_fast_off_ch1;
    memcpy(s->want, next, sizeof(next));
    /* A new set-up is a new question: what was refused is asked again. */
    s->refused_bus   = false;
    s->bus_told      = false;
    s->refused_i228  = false;
    s->refused_i3221 = false;
    s->events |= local;
}

/**
 * @brief The bus frame as it is to be written: the pins asked and the parts
 *        whose own frames the page has not refused.
 *
 * The encoder's bit goes only to an IOmcu that has it.
 *
 * @param s   The link state.
 * @param out Receives the frame.
 */
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
    if (s->map) {
        /* The map's mirror, as the page holds it. */
        en = (uint16_t)((en & ~LINK_SN_EN_I3221)
                        | (s->held[LINK_SN_ENABLE] & LINK_SN_EN_I3221));
    }
    out[0] = en;
    out[1] = s->want[LINK_SN_SDA_PIN];
    out[2] = s->want[LINK_SN_SCL_PIN];
    out[3] = (uint16_t)LINK_SN_KHZ_BUS;
}

/**
 * @brief Whether the wanted frame differs from the held one.
 *
 * @param s  The link state.
 * @param at The first register of the frame.
 * @return true when they differ.
 */
static bool frame_differs(const sense_link_t *s, uint8_t at)
{
    return memcmp(&s->want[at], &s->held[at],
                  FRAME_N * sizeof(uint16_t)) != 0;
}

/* The write owed now, or NONE, with its registers in @p regs. */
sense_link_op_kind_t sense_link_write_owed(const sense_link_t *s,
                                           uint16_t *regs)
{
    const bool i228  = !s->refused_i228 && frame_differs(s, I228_AT);
    const bool i3221 = !s->map && !s->refused_i3221
                       && frame_differs(s, I3221_AT);
    /* The page holds two parts to two addresses only while both are
     * enabled, so that is when a part's frame goes with both off. */
    const uint16_t both = (uint16_t)(LINK_SN_EN_I228 | LINK_SN_EN_I3221);
    if ((i228 || i3221) && (s->held[LINK_SN_ENABLE] & both) == both) {
        if (s->refused_bus) {
            return SENSE_LINK_OP_NONE;
        }
        /* Both monitors off; the encoder shares the bus pins, not their
         * addresses, and stays as it is. */
        regs[0] = (uint16_t)(s->held[LINK_SN_ENABLE] & LINK_SN_EN_AS5600);
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

/**
 * @brief Sets up a read operation.
 *
 * @param op   Receives the operation.
 * @param kind The operation kind.
 * @param page The page number.
 * @param off  The first register.
 * @param n    The number of registers.
 */
static void read_op(sense_link_op_t *op, sense_link_op_kind_t kind,
                    uint8_t page, uint8_t off, uint8_t n)
{
    op->kind  = kind;
    op->write = false;
    op->page  = page;
    op->off   = off;
    op->n     = n;
}

/**
 * @brief Whether a read is due.
 *
 * @param asked    true when a read was asked.
 * @param asked_ms The time the read was asked, in ms.
 * @param now_ms   The time, in ms.
 * @param every    The period, in ms.
 * @return true when the read is due.
 */
static bool due(bool asked, uint32_t asked_ms, uint32_t now_ms,
                uint32_t every)
{
    return !asked || (uint32_t)(now_ms - asked_ms) >= every;
}

bool sense_link_win_on(const sense_link_t *s)
{
    /* With the map in force SERVO_WIN carries FAST, whichever channel of
     * whichever part that is, and only once the page holds the map asked:
     * the ring is the held FAST's.  Without it, CH1 of the one part: FAST
     * on CH2 or CH3 is not what an older IOmcu reads at 1 kHz. */
    return s != NULL && s->win_page && s->known
           && (s->held[LINK_SN_ENABLE] & LINK_SN_EN_I3221) != 0u
           && (s->map ? !s->want_unwatched && s->map_held
                      : (s->held[LINK_SN_I3221_CHANNELS] & 0x01u) != 0u
                            && !s->want_off_ch1);
}

/* The registers a SENSE status read takes: to the page's last when it has
 * the encoder enabled, or while the ring is read, for RESETS; else to
 * ESC_FLAGS. */
unsigned sense_link_status_count(const sense_link_t *s)
{
    return ((s->held[LINK_SN_ENABLE] & LINK_SN_EN_AS5600) != 0u
            || sense_link_win_on(s))
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
        const sense_link_op_kind_t w = sense_link_write_owed(s, regs);
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
                (uint8_t)LINK_SN_FLAGS,
                (uint8_t)sense_link_status_count(s));
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
    /* The ring last: SERVO_SENSE's read of this poll shows a window number
     * the ring's read then holds as well. */
    if (sense_link_win_on(s)
        && (s->win_all
            || due(s->asked_win, s->win_asked_ms, now_ms,
                   SENSE_LINK_WIN_MS))) {
        /* The whole page at once when the cursor has stood long enough
         * for a third window to close: one reply then carries every
         * window owed, read at one moment. */
        const bool all = s->win_all
                         || (s->win_synced
                             && (uint32_t)(now_ms - s->win_sync_ms)
                                    >= SENSE_LINK_WIN_ALL_MS);
        read_op(op, all ? SENSE_LINK_OP_WIN_ALL : SENSE_LINK_OP_WIN,
                (uint8_t)LINK_PAGE_SERVO_WIN, 0u,
                (uint8_t)(all ? (unsigned)LINK_SW_COUNT
                              : SENSE_LINK_WIN_HEAD));
        s->pending = op->kind;
        s->asked_win = true;
        s->win_asked_ms = now_ms;
        return true;
    }
    return false;
}

/**
 * @brief A set-up was taken, or first read: what was read and said under the
 *        old one is gone, and the wait before "not answering" starts again.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 */
static void new_setup(sense_link_t *s, uint32_t now_ms)
{
    s->setup_ms = now_ms;
    forget_reads(s);
    /* A magnet event dropped below before the band showed it was never
     * said: the next read says it, if the magnet still reads so. */
    if ((s->events & (uint32_t)SENSE_LINK_EV_ENC_MAGNET) != 0u) {
        s->enc_magnet_told = false;
    }
    /* An encoder switched off is said again when it is switched on: no
     * read runs with every part off, so judge_enc() would not see it. */
    if ((s->held[LINK_SN_ENABLE] & LINK_SN_EN_AS5600) == 0u) {
        s->enc_magnet_told = false;
        s->enc_magnet      = 0u;
    }
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

/**
 * @brief Handles the result of a write.
 *
 * @param s      The link state.
 * @param w      The write.
 * @param result The result: an acknowledgement, a NACK reason or no answer.
 * @param now_ms The time, in ms.
 */
static void written(sense_link_t *s, sense_link_op_kind_t w, int result,
                    uint32_t now_ms)
{
    if (result == SENSE_LINK_ACK) {
        const uint8_t at = (w == SENSE_LINK_OP_I228)    ? I228_AT
                           : (w == SENSE_LINK_OP_I3221) ? I3221_AT : BUS_AT;
        memcpy(&s->held[at], s->out, FRAME_N * sizeof(uint16_t));
        s->caps_owed = true;
        new_setup(s, now_ms);
        /* The page empties its ring with a set-up it takes: whatever the
         * ring shows from here on closed after this write. */
        s->win_restart = true;
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
        s->bus_retry = link_sn_pins_pair(s->out[1], s->out[2]);
        s->bus_refused_ms = now_ms;
        break;
    }
}

static const uint16_t k_online[2] = { LINK_SN_I228_ONLINE,
                                      LINK_SN_I3221_ONLINE };
static const uint16_t k_wrong[2]  = { LINK_SN_I228_ID_WRONG,
                                      LINK_SN_I3221_ID_WRONG };
const uint16_t sense_link_en_bit[2] = { LINK_SN_EN_I228,
                                        LINK_SN_EN_I3221 };
static const uint32_t k_silent[2] = { SENSE_LINK_EV_I228_SILENT,
                                      SENSE_LINK_EV_I3221_SILENT };
static const uint32_t k_wrong_ev[2] = { SENSE_LINK_EV_I228_WRONG,
                                        SENSE_LINK_EV_I3221_WRONG };

/**
 * @brief The encoder's part of a SENSE read: not answering, once until it does,
 *        and a magnet that is missing, weak or strong, once while its bits stay
 *        as they were said -- across set-ups, as an edit of another row on
 *        SETUP is one.
 *
 * A part said not to answer has its magnet said again when it
 * answers.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @param waited true when the grace time has passed.
 */
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
        /* Not online for a while after a set-up is taken, until the
         * IOmcu has found the part again: that keeps what was said. */
        if (!s->enc_silent_told && (s->enc_online_seen || waited)) {
            s->events |= SENSE_LINK_EV_ENC_SILENT;
            s->enc_silent_told = true;
            s->enc_online_seen = false;
            s->enc_magnet_told = false;
        }
        return;
    }
    s->enc_silent_told = false;
    s->enc_online_seen = true;
    s->events &= ~(uint32_t)SENSE_LINK_EV_ENC_SILENT;
    if ((f & LINK_SN_ENC_VALID) == 0u) {
        /* Online but no register set read yet: the magnet bits are not
         * yet a reading, and a zero is not "no magnet". */
        return;
    }
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
    /* Said once, and again when a bit changes under it: a weak magnet
     * that goes missing takes the angle away, one that comes back weak
     * gives it back, and one that turns from weak to strong is said as
     * strong. */
    const bool moved = bits != s->enc_magnet;
    s->enc_magnet = bits;
    if (!s->enc_magnet_told || moved) {
        s->events |= SENSE_LINK_EV_ENC_MAGNET;
        s->enc_magnet_told = true;
    }
}

/**
 * @brief What a SENSE read says that the operator is told.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 */
static void judge_status(sense_link_t *s, uint32_t now_ms)
{
    const uint16_t f   = s->status[ST(LINK_SN_FLAGS)];
    const uint16_t was = s->was_flags;
    const uint16_t en  = s->held[LINK_SN_ENABLE];
    const bool waited  = (uint32_t)(now_ms - s->setup_ms)
                         >= SENSE_LINK_GRACE_MS;
    for (unsigned p = 0u; p < 2u; ++p) {
        if ((en & sense_link_en_bit[p]) == 0u) {
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

/**
 * @brief What a SERVO_SENSE read says: a channel read newly clipped, of the
 *        channels the page reads.
 *
 * @param s The link state.
 */
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

/**
 * @brief Entry @p k of a SERVO_WIN read, whose newest number is @p newest.
 *
 * @param regs   The registers read.
 * @param k      The entry index.
 * @param newest The newest window number.
 * @param now_ms The time, in ms.
 * @param w      Receives the window.
 */
static void win_entry(const uint16_t *regs, unsigned k, uint16_t newest,
                      uint32_t now_ms, sense_link_win_t *w)
{
    const uint16_t *e = &regs[LINK_SW_ENTRY(k, 0)];
    const uint16_t f = e[LINK_SW_E_FLAGS];
    w->number   = (uint16_t)(newest - k);
    w->current  = (f & LINK_SW_E_CURRENT) != 0u;
    w->voltage  = (f & LINK_SW_E_VOLTAGE) != 0u;
    w->clip_hi  = (f & LINK_SW_E_CLIP_HI) != 0u;
    w->clip_lo  = (f & LINK_SW_E_CLIP_LO) != 0u;
    w->clipped  = LINK_SW_E_CLIPPED(f);
    w->mean_ma  = (int16_t)e[LINK_SW_E_MEAN_MA];
    w->max_ma   = (int16_t)e[LINK_SW_E_MAX_MA];
    w->min_ma   = (int16_t)e[LINK_SW_E_MIN_MA];
    w->mean_mv  = e[LINK_SW_E_MEAN_MV];
    w->min_mv   = e[LINK_SW_E_MIN_MV];
    /* The newest closed before this read; entry k closed k windows before
     * the newest did. */
    w->taken_ms = now_ms - (uint32_t)k * SENSE_LINK_WINDOW_MS;
    w->stamped  = true;
}

/**
 * @brief A SERVO_WIN read of @p n registers from 0: the windows it owes into
 *        the queue, oldest first, and the cursor after the last one taken.
 *
 * A read that owes an entry it does not carry takes nothing and asks for the
 * whole page.
 *
 * @param s      The link state.
 * @param regs   The registers read.
 * @param n      The number of registers read, from 0.
 * @param now_ms The time, in ms.
 */
static void judge_win(sense_link_t *s, const uint16_t *regs, unsigned n,
                      uint32_t now_ms)
{
    const unsigned carried = (n - (unsigned)LINK_SW_ENTRIES)
                             / (unsigned)LINK_SW_E_STRIDE;
    s->win_all = false;
    if ((regs[LINK_SW_FLAGS] & LINK_SW_HAVE) == 0u) {
        /* No window has closed under the set-up in force: the ring is
         * empty, and what it shows next closed after this read. */
        s->win_have    = false;
        s->win_valid   = false;
        s->win_synced  = false;
        s->win_restart = true;
        return;
    }
    const uint16_t newest = regs[LINK_SW_WINDOW];
    if (!s->win_have || newest != s->win_newest) {
        s->win_moved_ms = now_ms;
        s->win_stands   = false;
    } else if ((uint32_t)(now_ms - s->win_moved_ms) >= SENSE_LINK_STALE_MS) {
        /* Latched, so a number that stands for 2^32 ms does not read as
         * one that has just moved. */
        s->win_stands = true;
    }
    s->win_have   = true;
    s->win_newest = newest;
    const uint16_t f0 = regs[LINK_SW_ENTRY(0, LINK_SW_E_FLAGS)];
    s->win_valid = (f0 & LINK_SW_E_CLOSED) != 0u
                   && (f0 & LINK_SW_E_CURRENT) != 0u;

    if (!s->win_synced && !s->win_restart) {
        /* The first read of a link: the ring's windows may have been
         * handed over before the link went. */
        s->win_synced  = true;
        s->win_next    = (uint16_t)(newest + 1u);
        s->win_sync_ms = now_ms;
        return;
    }
    /* Window numbers from the cursor to the newest. */
    unsigned owed = LINK_SW_RING;
    if (s->win_synced) {
        owed = (uint16_t)(newest - s->win_next + 1u);
        if (owed == 0u) {
            s->win_sync_ms = now_ms;
            return;
        }
        if (owed >= 0x8000u) {
            /* The newest number lies behind the cursor: the ring started
             * again between two reads, and all it holds is owed. */
            s->win_synced  = false;
            s->win_restart = true;
            owed = LINK_SW_RING;
        }
    }
    const unsigned in_ring = (owed > LINK_SW_RING) ? LINK_SW_RING : owed;
    if (in_ring > carried) {
        s->win_all = true;
        return;
    }
    s->win_lost += (uint32_t)(owed - in_ring);
    s->win_synced  = true;
    s->win_restart = false;
    for (unsigned k = in_ring; k-- > 0u;) {
        if ((regs[LINK_SW_ENTRY(k, LINK_SW_E_FLAGS)] & LINK_SW_E_CLOSED)
            == 0u) {
            continue;                   /* a number that never closed */
        }
        if (s->winq_n >= LINK_SW_RING) {
            /* Nobody took the last read's windows: this one and the
             * newer stay owed on the page. */
            s->win_next = (uint16_t)(newest - k);
            return;
        }
        win_entry(regs, k, newest, now_ms, &s->winq[s->winq_n++]);
    }
    s->win_next    = (uint16_t)(newest + 1u);
    s->win_sync_ms = now_ms;
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
    if ((op == SENSE_LINK_OP_WIN || op == SENSE_LINK_OP_WIN_ALL)
        && (result != SENSE_LINK_ACK || regs == NULL)) {
        /* SERVO_WIN refused by an IOmcu that names 4.11: nothing
         * more is sent to that page until the link comes up again, and
         * the windows are SERVO_SENSE's. */
        s->win_page = false;
        s->win_all  = false;
        s->win_have = false;
        s->winq_n   = 0u;
        return;
    }
    if (result != SENSE_LINK_ACK || regs == NULL) {
        /* A read refused: an IOmcu that names 4.7 and has not the
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
        memcpy(s->status, regs,
               sense_link_status_count(s) * sizeof(uint16_t));
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
    case SENSE_LINK_OP_WIN:
        judge_win(s, regs, SENSE_LINK_WIN_HEAD, now_ms);
        break;
    case SENSE_LINK_OP_WIN_ALL:
        judge_win(s, regs, (unsigned)LINK_SW_COUNT, now_ms);
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

bool sense_link_event_stands(const sense_link_t *s, uint32_t ev)
{
    if (s == NULL) {
        return false;
    }
    /* What the page holds is known: an IOmcu with the page answers and
     * the page has been read since it came up. */
    const bool known = s->up && s->page && s->known && s->want_set;
    switch (ev) {
    case SENSE_LINK_EV_PINS_UNSET:
        return (s->want_i228 || s->want_i3221 || s->want_enc)
               && (s->want_sda < 0 || s->want_scl < 0);
    case SENSE_LINK_EV_SAME_ADDR:
        return s->want_i228 && s->want_i3221
               && one_address(s->want[LINK_SN_I228_ADDR],
                              s->want[LINK_SN_I3221_ADDR], s->want_parts);
    case SENSE_LINK_EV_BUS_REFUSED: {
        if (!known || s->refused_bus) {
            return true;
        }
        uint16_t bus[FRAME_N];
        bus_frame(s, bus);
        return memcmp(bus, &s->held[BUS_AT], sizeof(bus)) != 0;
    }
    case SENSE_LINK_EV_I228_REFUSED:
        return !known || s->refused_i228 || frame_differs(s, I228_AT);
    case SENSE_LINK_EV_I3221_REFUSED:
        return !s->map
               && (!known || s->refused_i3221 || frame_differs(s, I3221_AT));
    default:
        return true;
    }
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
        if ((k_order[i] & SENSE_LINK_EV_SETUP) != 0u
            && !sense_link_event_stands(s, k_order[i])) {
            /* Put back from a band that never showed it, and over since:
             * the page holds what is asked, or the settings no longer ask
             * it. */
            s->events &= ~k_order[i];
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
     * IOmcu that answers now, and a link gone since took it. */
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
    return s->known && sense_link_write_owed(s, regs) == SENSE_LINK_OP_NONE;
}
