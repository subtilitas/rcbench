/*
 * SPDX-License-Identifier: MIT
 */

#include "gt911_emu.h"

#include <string.h>

void gt911_emu_init(gt911_emu_t *e, uint16_t res_x, uint16_t res_y)
{
    memset(e, 0, sizeof(*e));
    e->res_x = res_x;
    e->res_y = res_y;
}

static uint8_t lo(uint16_t v) { return (uint8_t)(v & 0xFFu); }
static uint8_t hi(uint16_t v) { return (uint8_t)(v >> 8); }

uint8_t gt911_emu_peek(const gt911_emu_t *e, uint16_t reg)
{
    switch (reg) {
    case 0x8040: return e->command[0];
    case 0x8041: return e->command[1];
    case 0x8047: return 0x41;
    case 0x8048: return lo(e->res_x);
    case 0x8049: return hi(e->res_x);
    case 0x804A: return lo(e->res_y);
    case 0x804B: return hi(e->res_y);
    case 0x804C: return 5;
    case 0x8140: return '9';
    case 0x8141: return '1';
    case 0x8142: return '1';
    case 0x8143: return 0;
    case 0x8144: return 0x01;
    case 0x8145: return 0xE0;
    case 0x8146: return lo(e->res_x);
    case 0x8147: return hi(e->res_x);
    case 0x8148: return lo(e->res_y);
    case 0x8149: return hi(e->res_y);
    case GT911_EMU_REG_STATUS: return e->status;
    default:
        break;
    }
    if (reg >= GT911_EMU_REG_POINT1 && reg < GT911_EMU_REG_POINT1 + 8u) {
        return e->point[reg - GT911_EMU_REG_POINT1];
    }
    return 0;
}

void gt911_emu_rx(gt911_emu_t *e, uint8_t b)
{
    e->busy = true;
    e->reading = false;
    if (e->wbytes == 0u) {
        e->ptr = (uint16_t)(b << 8);
        e->wbytes = 1;
        return;
    }
    if (e->wbytes == 1u) {
        e->ptr = (uint16_t)(e->ptr | b);
        e->wbytes = 2;
        return;
    }
    switch (e->ptr) {
    case 0x8040: e->command[0] = b; break;
    case 0x8041: e->command[1] = b; break;
    case GT911_EMU_REG_STATUS:
        e->status = 0;
        ++e->acks;
        break;
    default:
        break;
    }
    e->ptr = (uint16_t)(e->ptr + 1u);
}

uint8_t gt911_emu_tx(gt911_emu_t *e)
{
    if (!e->reading) {
        e->reading = true;
        ++e->reads;
        if (e->ptr == GT911_EMU_REG_PRODUCT) {
            ++e->product_reads;
        }
    }
    e->busy = true;
    const uint8_t b = gt911_emu_peek(e, e->ptr);
    e->ptr = (uint16_t)(e->ptr + 1u);
    return b;
}

void gt911_emu_finish(gt911_emu_t *e)
{
    e->wbytes = 0;
    e->reading = false;
    e->busy = false;
}

bool gt911_emu_publish(gt911_emu_t *e, const gt911_emu_frame_t *f)
{
    if (e->busy || (e->status & GT911_EMU_READY) != 0u) {
        return false;
    }
    memset(e->point, 0, sizeof(e->point));
    if (f->down) {
        e->point[0] = 0;                    /* track ID */
        e->point[1] = lo(f->x);
        e->point[2] = hi(f->x);
        e->point[3] = lo(f->y);
        e->point[4] = hi(f->y);
        e->point[5] = (uint8_t)GT911_EMU_SIZE;
        e->point[6] = 0;
        e->status = (uint8_t)(GT911_EMU_READY | 1u);
    } else {
        e->status = (uint8_t)GT911_EMU_READY;
    }
    return true;
}

bool gt911_emu_withdraw(gt911_emu_t *e)
{
    if (e->busy) {
        return false;
    }
    e->status = 0;
    return true;
}
