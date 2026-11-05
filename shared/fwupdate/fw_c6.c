/**
 * @file fw_c6.c
 * @brief The C6 radio's update: image check, UPDATES' row and the steps.
 *        See fw_c6.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "fw_c6.h"

#include <stddef.h>
#include <string.h>

fw_err_t fw_c6_image_check(const uint8_t *img, uint32_t bytes,
                           fw_image_info_t *out)
{
    if (img == NULL) {
        return FW_ERR_EMPTY;
    }
    return fw_image_check_as(img, bytes, FW_C6_SLOT_BYTES, FW_CHIP_ESP32C6,
                             FW_C6_PROJECT, out);
}

/**
 * @brief The bench's reason against an update now, in fw_c6_why_t's order.
 * @param f the feed
 * @return the first reason that holds, FW_C6_WHY_NONE for none
 */
static fw_c6_why_t bench_why(const fw_c6_feed_t *f)
{
    if (f->armed) {
        return FW_C6_WHY_ARMED;
    }
    if (f->supply) {
        return FW_C6_WHY_SUPPLY;
    }
    if (f->run) {
        return FW_C6_WHY_RUN;
    }
    if (f->drawing) {
        return FW_C6_WHY_DRAWING;
    }
    if (f->iomcu) {
        return FW_C6_WHY_IOMCU;
    }
    if (f->installing) {
        return FW_C6_WHY_INSTALL;
    }
    if (f->radio_up) {
        return FW_C6_WHY_RADIO;
    }
    return f->ending ? FW_C6_WHY_ENDING : FW_C6_WHY_NONE;
}

fw_c6_row_t fw_c6_row(const fw_c6_feed_t *f, fw_c6_why_t *why)
{
    if (why != NULL) {
        *why = FW_C6_WHY_NONE;
    }
    if (f == NULL || !f->have_image) {
        return FW_C6_ROW_NO_IMAGE;
    }
    if (!f->image_ok) {
        return FW_C6_ROW_BAD_IMAGE;
    }
    if (f->updating) {
        return FW_C6_ROW_UPDATING;
    }
    if (f->link_failed) {
        return FW_C6_ROW_LINK_FAILED;
    }
    if (f->known && fw_version_cmp(f->running, f->carried) == 0) {
        return FW_C6_ROW_SAME;
    }
    const fw_c6_why_t w = bench_why(f);
    if (w != FW_C6_WHY_NONE) {
        if (why != NULL) {
            *why = w;
        }
        return FW_C6_ROW_BLOCKED;
    }
    return FW_C6_ROW_READY;
}

void fw_c6_ota_init(fw_c6_ota_t *o)
{
    memset(o, 0, sizeof(*o));
}

bool fw_c6_ota_active(const fw_c6_ota_t *o)
{
    return o != NULL && o->phase != FW_C6_PH_IDLE
           && o->phase != FW_C6_PH_ENDED;
}

bool fw_c6_has_activate(bool known, fw_version_t v)
{
    if (!known) {
        return false;
    }
    const fw_version_t from = { FW_C6_ACTIVATE_MAJOR, FW_C6_ACTIVATE_MINOR,
                                0u };
    return fw_version_cmp(v, from) >= 0;
}

bool fw_c6_ota_start(fw_c6_ota_t *o, uint32_t total, fw_version_t carried,
                     uint32_t now_ms)
{
    (void)now_ms;
    if (fw_c6_ota_active(o) || total == 0u || total > FW_C6_SLOT_BYTES) {
        return false;
    }
    const uint32_t gen = o->gen + 1u;
    memset(o, 0, sizeof(*o));
    o->gen = gen;
    o->phase = FW_C6_PH_LINK;
    o->total = total;
    o->carried = carried;
    return true;
}

uint32_t fw_c6_ota_limit_ms(fw_c6_phase_t ph)
{
    /* In fw_c6_phase_t's order; IDLE and ENDED have no step. */
    static const uint32_t k_limit[] = {
        0u, FW_C6_LINK_MS, FW_C6_BEGIN_MS, FW_C6_WRITE_MS, FW_C6_END_MS,
        FW_C6_ACTIVATE_MS, FW_C6_END_MS, 0u,
    };
    _Static_assert(sizeof(k_limit) / sizeof(k_limit[0])
                       == (size_t)FW_C6_PH_ENDED + 1u,
                   "a limit for every phase");
    const unsigned i = (unsigned)ph;
    return (i < sizeof(k_limit) / sizeof(k_limit[0])) ? k_limit[i] : 0u;
}

bool fw_c6_ota_next(fw_c6_ota_t *o, uint32_t now_ms, fw_c6_step_t *step)
{
    if (!fw_c6_ota_active(o) || o->issued || step == NULL) {
        return false;
    }
    memset(step, 0, sizeof(*step));
    step->gen = o->gen;
    switch (o->phase) {
    case FW_C6_PH_LINK:     step->act = FW_C6_ACT_LINK;     break;
    case FW_C6_PH_BEGIN:    step->act = FW_C6_ACT_BEGIN;    break;
    case FW_C6_PH_END:      step->act = FW_C6_ACT_END;      break;
    case FW_C6_PH_ACTIVATE: step->act = FW_C6_ACT_ACTIVATE; break;
    case FW_C6_PH_CLOSE:    step->act = FW_C6_ACT_CLOSE;    break;
    case FW_C6_PH_WRITE:
    default: {
        const uint32_t left = o->total - o->done;
        step->act = FW_C6_ACT_WRITE;
        step->offset = o->done;
        step->len = (left < FW_C6_RUN_BYTES) ? left : FW_C6_RUN_BYTES;
        o->step_len = step->len;
        break;
    }
    }
    o->issued = true;
    o->since_ms = now_ms;
    return true;
}

/**
 * @brief Ends the update with @p res.
 * @param o   the update
 * @param res how it ended
 */
static void end_with(fw_c6_ota_t *o, fw_c6_res_t res)
{
    if (res == FW_C6_RES_UPDATED || res == FW_C6_RES_SAME) {
        o->failed_in = FW_C6_PH_IDLE;
    } else {
        o->failed_in = o->phase;
    }
    o->phase = FW_C6_PH_ENDED;
    o->result = res;
    o->issued = false;
}

/**
 * @brief Moves to @p ph with no step out.
 * @param o  the update
 * @param ph the next phase
 */
static void go(fw_c6_ota_t *o, fw_c6_phase_t ph)
{
    o->phase = ph;
    o->issued = false;
}

/**
 * @brief The end call answered OK: activate where the C6 has the call.
 * @param o the update
 */
static void ended_ok(fw_c6_ota_t *o)
{
    if (fw_c6_has_activate(o->known, o->running)) {
        go(o, FW_C6_PH_ACTIVATE);
    } else {
        /* Before 2.6.0 the end call itself sets the slot the C6 starts
         * from; a C6 that reports no version is taken for one of those. */
        end_with(o, FW_C6_RES_UPDATED);
    }
}

bool fw_c6_ota_answer(fw_c6_ota_t *o, uint32_t gen, int32_t err, bool known,
                      fw_version_t running)
{
    if (!fw_c6_ota_active(o) || !o->issued || gen != o->gen) {
        return false;
    }
    if (err != 0) {
        o->error = err;
    }
    switch (o->phase) {
    case FW_C6_PH_LINK:
        if (err != 0) {
            end_with(o, FW_C6_RES_NO_LINK);
            break;
        }
        o->known = known;
        o->running = running;
        if (known && fw_version_cmp(running, o->carried) == 0) {
            end_with(o, FW_C6_RES_SAME);
        } else {
            go(o, FW_C6_PH_BEGIN);
        }
        break;
    case FW_C6_PH_BEGIN:
        if (err != 0) {
            end_with(o, FW_C6_RES_REFUSED);
        } else {
            go(o, FW_C6_PH_WRITE);
        }
        break;
    case FW_C6_PH_WRITE:
        if (err != 0) {
            o->close_for = FW_C6_RES_WRITE;
            go(o, FW_C6_PH_CLOSE);
            break;
        }
        o->done += o->step_len;
        go(o, (o->done >= o->total) ? FW_C6_PH_END : FW_C6_PH_WRITE);
        break;
    case FW_C6_PH_END:
        if (err != 0) {
            end_with(o, FW_C6_RES_VERIFY);
        } else {
            ended_ok(o);
        }
        break;
    case FW_C6_PH_ACTIVATE:
        end_with(o, (err != 0) ? FW_C6_RES_ACTIVATE : FW_C6_RES_UPDATED);
        break;
    case FW_C6_PH_CLOSE:
    default:
        /* Whatever the close answers, the write had failed. */
        o->phase = FW_C6_PH_WRITE;
        end_with(o, o->close_for);
        break;
    }
    return true;
}

void fw_c6_ota_tick(fw_c6_ota_t *o, uint32_t now_ms)
{
    if (!fw_c6_ota_active(o) || !o->issued) {
        return;
    }
    if ((uint32_t)(now_ms - o->since_ms) < fw_c6_ota_limit_ms(o->phase)) {
        return;
    }
    if (o->phase == FW_C6_PH_CLOSE) {
        o->phase = FW_C6_PH_WRITE;
        end_with(o, o->close_for);
        return;
    }
    end_with(o, FW_C6_RES_NO_ANSWER);
}

unsigned fw_c6_ota_percent(const fw_c6_ota_t *o)
{
    if (o == NULL || o->total == 0u) {
        return 0u;
    }
    const uint64_t p = (uint64_t)o->done * 100u / o->total;
    return (p > 100u) ? 100u : (unsigned)p;
}
