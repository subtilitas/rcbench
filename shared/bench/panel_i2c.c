/*
 * SPDX-License-Identifier: MIT
 */

#include "panel_i2c.h"

#include <stddef.h>

const panel_i2c_range_t panel_i2c_reserved_table[] = {
    { 0x00, 0x03, "I2C reserved" },
    { 0x14, 0x14, "GT911" },
    { 0x20, 0x27, "CH422G" },
    { 0x30, 0x3F, "CH422G" },
    { 0x5D, 0x5D, "GT911" },
    { 0x78, 0x7F, "I2C reserved" },
};

const unsigned panel_i2c_reserved_count =
    sizeof panel_i2c_reserved_table / sizeof panel_i2c_reserved_table[0];

const char *panel_i2c_owner(unsigned addr)
{
    if (addr > 0x7Fu) {
        return "not a 7-bit address";
    }
    for (unsigned i = 0; i < panel_i2c_reserved_count; ++i) {
        const panel_i2c_range_t *r = &panel_i2c_reserved_table[i];
        if (addr >= r->first && addr <= r->last) {
            return r->owner;
        }
    }
    return NULL;
}

bool panel_i2c_addr_reserved(unsigned addr)
{
    return panel_i2c_owner(addr) != NULL;
}
