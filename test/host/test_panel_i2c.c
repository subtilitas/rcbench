/*
 * The addresses the panel board answers at on its I2C (Inter-Integrated
 * Circuit) bus: the table's edges, the constant-expression form against the
 * function for every byte, the board's own addresses inside it, and the
 * addresses the I2C specification reserves (NXP UM10204, table 4), less
 * the Hs-mode master codes 0x04 to 0x07, which the MT6701 knob's 0x06 sits
 * in.
 *
 * board_pins.h includes ESP-IDF headers and is not built here, so its
 * addresses are restated below; the comments name the constants.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "panel_i2c.h"

/* The owner, or "(free)", so a failed check prints instead of crashing. */
static const char *own(unsigned addr)
{
    const char *o = panel_i2c_owner(addr);
    return o != NULL ? o : "(free)";
}

TEST_CASE(the_table_holds_at_its_edges)
{
    static const struct {
        unsigned addr;
        bool     reserved;
    } edge[] = {
        { 0x00, true  }, { 0x03, true  },              /* I2C reserved   */
        { 0x04, false }, { 0x05, false }, { 0x06, false }, { 0x07, false },
        { 0x08, false },
        { 0x13, false }, { 0x14, true  }, { 0x15, false },
        { 0x1F, false }, { 0x20, true  }, { 0x27, true  },
        { 0x28, false }, { 0x2F, false },
        { 0x30, true  }, { 0x36, true  }, { 0x3F, true  },
        { 0x40, false }, { 0x46, false },
        { 0x5C, false }, { 0x5D, true  }, { 0x5E, false },
        { 0x77, false }, { 0x78, true  }, { 0x7F, true  }, /* I2C reserved */
        { 0x80, true  }, { 0xFF, true  }, { 0x100, true },
    };
    for (size_t i = 0; i < sizeof edge / sizeof edge[0]; ++i) {
        if (panel_i2c_addr_reserved(edge[i].addr) != edge[i].reserved) {
            CHECK_EQ((int)edge[i].addr, -1);    /* names the address */
        }
    }
}

TEST_CASE(the_owners_are_named)
{
    CHECK_STR_EQ(own(0x20), "CH422G");
    CHECK_STR_EQ(own(0x27), "CH422G");
    CHECK_STR_EQ(own(0x30), "CH422G");
    CHECK_STR_EQ(own(0x3F), "CH422G");
    CHECK_STR_EQ(own(0x14), "GT911");
    CHECK_STR_EQ(own(0x5D), "GT911");
    CHECK_STR_EQ(own(0x00), "I2C reserved");
    CHECK_STR_EQ(own(0x03), "I2C reserved");
    CHECK_STR_EQ(own(0x78), "I2C reserved");
    CHECK_STR_EQ(own(0x7F), "I2C reserved");
    CHECK_STR_EQ(own(0x80), "not a 7-bit address");
    CHECK(panel_i2c_owner(0x06) == NULL);
    CHECK(panel_i2c_owner(0x28) == NULL);
}

TEST_CASE(the_macro_and_the_function_agree_everywhere)
{
    for (unsigned a = 0; a <= 0x1FFu; ++a) {
        if (PANEL_I2C_RESERVED(a) != panel_i2c_addr_reserved(a)) {
            CHECK_EQ((int)a, -1);
            break;
        }
    }
    /* 0x00 to 0x03, 0x14, 0x20 to 0x27, 0x30 to 0x3F, 0x5D, 0x78 to 0x7F. */
    int n = 0;
    for (unsigned a = 0; a <= 0x7Fu; ++a) {
        n += panel_i2c_addr_reserved(a) ? 1 : 0;
    }
    CHECK_EQ(n, 4 + 1 + 8 + 16 + 1 + 8);
}

TEST_CASE(the_ranges_are_ordered_apart_and_seven_bit)
{
    CHECK_EQ(panel_i2c_reserved_count, 6u);
    for (unsigned i = 0; i < panel_i2c_reserved_count; ++i) {
        const panel_i2c_range_t *r = &panel_i2c_reserved_table[i];
        CHECK(r->first <= r->last);
        CHECK(r->last <= 0x7Fu);
        CHECK(r->owner != NULL && r->owner[0] != '\0');
        if (i > 0) {
            CHECK(r->first > panel_i2c_reserved_table[i - 1].last);
        }
    }
}

TEST_CASE(the_boards_own_addresses_are_inside)
{
    /* BOARD_CH422G_ADDR_WR_OC, _WR_SET, _RD_IO and _WR_IO. */
    CHECK_STR_EQ(own(0x23), "CH422G");
    CHECK_STR_EQ(own(0x24), "CH422G");
    CHECK_STR_EQ(own(0x26), "CH422G");
    CHECK_STR_EQ(own(0x38), "CH422G");
    /* BOARD_TOUCH_I2C_ADDR and BOARD_TOUCH_I2C_ADDR_ALT. */
    CHECK_STR_EQ(own(0x5D), "GT911");
    CHECK_STR_EQ(own(0x14), "GT911");
}

int main(void)
{
    RUN(the_table_holds_at_its_edges);
    RUN(the_owners_are_named);
    RUN(the_macro_and_the_function_agree_everywhere);
    RUN(the_ranges_are_ordered_apart_and_seven_bit);
    RUN(the_boards_own_addresses_are_inside);
    return test_summary("panel_i2c");
}
