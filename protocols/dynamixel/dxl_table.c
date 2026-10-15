/*
 * The X-series control table of ROBOTIS Dynamixel servos.  See
 * dxl_table.h.
 *
 * ROBOTIS and Dynamixel are names or trademarks of their owner; they are
 * used here only to say which servos this module works with.
 *
 * Sources: the ROBOTIS e-Manual pages of the XL430-W250 and the XM430-W350
 * (README.md, "Sources").  A range the pages read do not state is the
 * field's raw range.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dxl_table.h"

#define E  DXL_AREA_EEPROM
#define M  DXL_AREA_RAM
#define R  DXL_ACC_R
#define RW DXL_ACC_RW

/* In address order; dxl_reg_id_t follows it. */
static const dxl_reg_t k_regs[DXL_R_COUNT] = {
    [DXL_R_MODEL_NUMBER] = { "Model Number", 0, 2, E, R, DXL_UNIT_RAW, 0, 0, 65535 },
    [DXL_R_FIRMWARE] = { "Firmware Version", 6, 1, E, R, DXL_UNIT_RAW, 0, 0, 255 },
    [DXL_R_ID] = { "ID", 7, 1, E, RW, DXL_UNIT_RAW, DXL_F_LINK, 0, 252 },
    [DXL_R_BAUD] = { "Baud Rate", 8, 1, E, RW, DXL_UNIT_BAUD, DXL_F_LINK, 0, 7 },
    [DXL_R_RETURN_DELAY] = { "Return Delay Time", 9, 1, E, RW, DXL_UNIT_US_2, 0, 0, 254 },
    [DXL_R_DRIVE_MODE] = { "Drive Mode", 10, 1, E, RW, DXL_UNIT_RAW, 0, 0, 13 },
    [DXL_R_OPERATING_MODE] = { "Operating Mode", 11, 1, E, RW, DXL_UNIT_RAW, 0, 0, 16 },
    [DXL_R_SHADOW_ID] = { "Secondary ID", 12, 1, E, RW, DXL_UNIT_RAW, 0, 0, 255 },
    [DXL_R_PROTOCOL] = { "Protocol Type", 13, 1, E, RW, DXL_UNIT_RAW,
                         DXL_F_LINK | DXL_F_NO_WRITE, 1, 2 },
    [DXL_R_HOMING_OFFSET] = { "Homing Offset", 20, 4, E, RW, DXL_UNIT_PULSE,
                              DXL_F_SIGNED, -1044479, 1044479 },
    [DXL_R_MOVING_THRESHOLD] = { "Moving Threshold", 24, 4, E, RW, DXL_UNIT_RPM_229, 0, 0, 1023 },
    [DXL_R_TEMPERATURE_LIMIT] = { "Temperature Limit", 31, 1, E, RW, DXL_UNIT_DEG_C, 0, 0, 100 },
    [DXL_R_MAX_VOLTAGE] = { "Max Voltage Limit", 32, 2, E, RW, DXL_UNIT_DECIVOLT, 0, 60, 140 },
    [DXL_R_MIN_VOLTAGE] = { "Min Voltage Limit", 34, 2, E, RW, DXL_UNIT_DECIVOLT, 0, 60, 140 },
    [DXL_R_PWM_LIMIT] = { "PWM Limit", 36, 2, E, RW, DXL_UNIT_PWM_113, 0, 0, 885 },
    [DXL_R_CURRENT_LIMIT] = { "Current Limit", 38, 2, E, RW, DXL_UNIT_MA_269,
                              DXL_F_CURRENT, 0, 65535 },
    [DXL_R_VELOCITY_LIMIT] = { "Velocity Limit", 44, 4, E, RW, DXL_UNIT_RPM_229, 0, 0, 1023 },
    [DXL_R_MAX_POSITION] = { "Max Position Limit", 48, 4, E, RW, DXL_UNIT_PULSE, 0, 0, 4095 },
    [DXL_R_MIN_POSITION] = { "Min Position Limit", 52, 4, E, RW, DXL_UNIT_PULSE, 0, 0, 4095 },
    [DXL_R_STARTUP] = { "Startup Configuration", 60, 1, E, RW, DXL_UNIT_RAW, 0, 0, 3 },
    [DXL_R_SHUTDOWN] = { "Shutdown", 63, 1, E, RW, DXL_UNIT_RAW, 0, 0, 255 },
    [DXL_R_TORQUE_ENABLE] = { "Torque Enable", 64, 1, M, RW, DXL_UNIT_RAW, 0, 0, 1 },
    [DXL_R_LED] = { "LED", 65, 1, M, RW, DXL_UNIT_RAW, 0, 0, 1 },
    [DXL_R_STATUS_RETURN] = { "Status Return Level", 68, 1, M, RW, DXL_UNIT_RAW, 0, 0, 2 },
    [DXL_R_REGISTERED] = { "Registered Instruction", 69, 1, M, R, DXL_UNIT_RAW, 0, 0, 1 },
    [DXL_R_HW_ERROR] = { "Hardware Error Status", 70, 1, M, R, DXL_UNIT_RAW, 0, 0, 255 },
    [DXL_R_BUS_WATCHDOG] = { "Bus Watchdog", 98, 1, M, RW, DXL_UNIT_MS_20, 0, 0, 127 },
    [DXL_R_GOAL_PWM] = { "Goal PWM", 100, 2, M, RW, DXL_UNIT_PWM_113,
                         DXL_F_SIGNED, -885, 885 },
    [DXL_R_GOAL_CURRENT] = { "Goal Current", 102, 2, M, RW, DXL_UNIT_MA_269,
                             DXL_F_SIGNED | DXL_F_CURRENT, -32768, 32767 },
    [DXL_R_GOAL_VELOCITY] = { "Goal Velocity", 104, 4, M, RW, DXL_UNIT_RPM_229,
                              DXL_F_SIGNED, -1023, 1023 },
    [DXL_R_PROFILE_ACCELERATION] = { "Profile Acceleration", 108, 4, M, RW,
                                     DXL_UNIT_ACC_214, 0, 0, 32767 },
    [DXL_R_PROFILE_VELOCITY] = { "Profile Velocity", 112, 4, M, RW, DXL_UNIT_RPM_229, 0, 0, 32767 },
    [DXL_R_GOAL_POSITION] = { "Goal Position", 116, 4, M, RW, DXL_UNIT_PULSE,
                              DXL_F_SIGNED, -1048575, 1048575 },
    [DXL_R_REALTIME_TICK] = { "Realtime Tick", 120, 2, M, R, DXL_UNIT_MS, 0, 0, 32767 },
    [DXL_R_MOVING] = { "Moving", 122, 1, M, R, DXL_UNIT_RAW, 0, 0, 1 },
    [DXL_R_MOVING_STATUS] = { "Moving Status", 123, 1, M, R, DXL_UNIT_RAW, 0, 0, 255 },
    [DXL_R_PRESENT_PWM] = { "Present PWM", 124, 2, M, R, DXL_UNIT_PWM_113,
                            DXL_F_SIGNED, -885, 885 },
    [DXL_R_PRESENT_LOAD] = { "Present Load", 126, 2, M, R, DXL_UNIT_PERMILLE,
                             DXL_F_SIGNED, -1000, 1000 },
    [DXL_R_PRESENT_VELOCITY] = { "Present Velocity", 128, 4, M, R, DXL_UNIT_RPM_229,
                                 DXL_F_SIGNED, INT32_MIN, INT32_MAX },
    [DXL_R_PRESENT_POSITION] = { "Present Position", 132, 4, M, R, DXL_UNIT_PULSE,
                                 DXL_F_SIGNED, INT32_MIN, INT32_MAX },
    [DXL_R_PRESENT_VOLTAGE] = { "Present Input Voltage", 144, 2, M, R, DXL_UNIT_DECIVOLT, 0, 0, 65535 },
    [DXL_R_PRESENT_TEMPERATURE] = { "Present Temperature", 146, 1, M, R, DXL_UNIT_DEG_C, 0, 0, 255 },
};

/* The models the research read the number of.  A model added here needs
 * the number from its own e-Manual page. */
static const dxl_model_t k_models[] = {
    { 1060u, "XL430-W250", false },
    { 1030u, "XM430-W210", true },
};

static const uint32_t k_baud[DXL_BAUD_COUNT] = {
    9600u, 57600u, 115200u, 1000000u, 2000000u, 3000000u, 4000000u, 4500000u,
};

const dxl_reg_t *dxl_reg(dxl_reg_id_t id)
{
    if ((unsigned)id >= (unsigned)DXL_R_COUNT) {
        return NULL;
    }
    return &k_regs[id];
}

const dxl_reg_t *dxl_reg_at(uint16_t addr)
{
    for (unsigned i = 0; i < (unsigned)DXL_R_COUNT; ++i) {
        if (k_regs[i].addr == addr) {
            return &k_regs[i];
        }
    }
    return NULL;
}

/* The field holding byte @p addr, or NULL. */
static const dxl_reg_t *reg_holding(unsigned addr)
{
    for (unsigned i = 0; i < (unsigned)DXL_R_COUNT; ++i) {
        if (addr >= k_regs[i].addr
            && addr < (unsigned)k_regs[i].addr + k_regs[i].size) {
            return &k_regs[i];
        }
    }
    return NULL;
}

bool dxl_reg_get(const dxl_reg_t *r, const uint8_t *table, uint16_t base,
                 size_t n, int32_t *value)
{
    uint32_t raw = 0;

    if (r == NULL || table == NULL || value == NULL || r->addr < base
        || (size_t)(r->addr - base) + r->size > n) {
        return false;
    }
    for (unsigned i = r->size; i > 0u; --i) {
        raw = (raw << 8) | table[r->addr - base + i - 1u];
    }
    if ((r->flags & DXL_F_SIGNED) != 0u && r->size < 4u) {
        const uint32_t sign = 1u << (8u * r->size - 1u);

        raw = (raw ^ sign) - sign;
    }
    *value = (int32_t)raw;
    return true;
}

dxl_wclass_t dxl_write_class(uint16_t addr, uint16_t len)
{
    const unsigned end = (unsigned)addr + len;
    bool eeprom = false;
    unsigned a = addr;

    if (len == 0u) {
        return DXL_WR_UNKNOWN;
    }
    /* Read-only first: a write that touches one is refused as such. */
    for (unsigned b = addr; b < end; ++b) {
        const dxl_reg_t *r = reg_holding(b);

        if (r != NULL && r->access == (uint8_t)DXL_ACC_R) {
            return DXL_WR_READ_ONLY;
        }
    }
    while (a < end) {
        const dxl_reg_t *r = reg_holding(a);

        if (r == NULL || r->addr != a || a + r->size > end
            || (r->flags & DXL_F_NO_WRITE) != 0u) {
            return DXL_WR_UNKNOWN;
        }
        if (r->area == (uint8_t)DXL_AREA_EEPROM) {
            eeprom = true;
        }
        a += r->size;
    }
    return eeprom ? DXL_WR_EEPROM : DXL_WR_RAM;
}

bool dxl_reg_value_ok(const dxl_reg_t *r, int32_t value)
{
    return r != NULL && r->access == (uint8_t)DXL_ACC_RW
           && (r->flags & DXL_F_NO_WRITE) == 0u && value >= r->min
           && value <= r->max;
}

uint32_t dxl_baud_rate(uint8_t index)
{
    return index < DXL_BAUD_COUNT ? k_baud[index] : 0u;
}

int dxl_baud_index(uint32_t rate)
{
    for (unsigned i = 0; i < DXL_BAUD_COUNT; ++i) {
        if (k_baud[i] == rate) {
            return (int)i;
        }
    }
    return -1;
}

const dxl_model_t *dxl_model_find(uint16_t number)
{
    for (size_t i = 0; i < sizeof(k_models) / sizeof(k_models[0]); ++i) {
        if (k_models[i].number == number) {
            return &k_models[i];
        }
    }
    return NULL;
}

/* a / b rounded to nearest, halves away from zero; b > 0. */
static int64_t div_round(int64_t a, int64_t b)
{
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

static int32_t clamp32(int64_t v)
{
    if (v > INT32_MAX) {
        return INT32_MAX;
    }
    if (v < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)v;
}

int32_t dxl_pulse_to_mdeg(int32_t pulse)
{
    return clamp32(div_round((int64_t)pulse * 45000, 512));
}

int32_t dxl_mdeg_to_pulse(int32_t mdeg)
{
    return clamp32(div_round((int64_t)mdeg * 512, 45000));
}

int32_t dxl_current_ma(int32_t raw)
{
    return clamp32(div_round((int64_t)raw * 269, 100));
}

int32_t dxl_voltage_mv(int32_t raw)
{
    return clamp32((int64_t)raw * 100);
}

uint32_t dxl_return_delay_us(uint8_t raw)
{
    return 2u * raw;
}

static bool differs(const dxl_reg_t *r, const uint8_t *a, const uint8_t *b)
{
    for (unsigned i = 0; i < r->size; ++i) {
        if (a[r->addr + i] != b[r->addr + i]) {
            return true;
        }
    }
    return false;
}

size_t dxl_restore_plan(const uint8_t *backup, const uint8_t *now,
                        dxl_wr_t *out, size_t max, int *refused)
{
    /* Pass 0: every field but ID and baud; pass 1: ID; pass 2: baud. */
    static const dxl_reg_id_t late[2] = { DXL_R_ID, DXL_R_BAUD };
    size_t n = 0;
    int dummy;

    if (refused == NULL) {
        refused = &dummy;
    }
    *refused = -1;
    if (backup == NULL || now == NULL || out == NULL) {
        return 0;
    }
    for (unsigned pass = 0; pass < 3u; ++pass) {
        for (unsigned i = 0; i < (unsigned)DXL_R_COUNT; ++i) {
            const dxl_reg_t *r = &k_regs[i];
            int32_t v = 0;

            if (r->area != (uint8_t)DXL_AREA_EEPROM
                || r->access != (uint8_t)DXL_ACC_RW
                || (r->flags & DXL_F_NO_WRITE) != 0u
                || (pass == 0u && (i == (unsigned)late[0]
                                   || i == (unsigned)late[1]))
                || (pass > 0u && i != (unsigned)late[pass - 1u])
                || !differs(r, backup, now)) {
                continue;
            }
            (void)dxl_reg_get(r, backup, 0, DXL_EEPROM_SIZE, &v);
            if (!dxl_reg_value_ok(r, v)) {
                *refused = r->addr;
                return 0;
            }
            if (n == max) {
                *refused = DXL_RESTORE_NO_ROOM;
                return 0;
            }
            out[n].addr = r->addr;
            out[n].size = r->size;
            out[n].value = (uint32_t)v;
            ++n;
        }
    }
    return n;
}
