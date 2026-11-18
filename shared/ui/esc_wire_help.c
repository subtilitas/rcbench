/**
 * @file
 * @brief The ESC signal-wire session's help line per field, from each firmware's
 *        settings: a table from a byte or a key's place to its text.  Text lookups
 *        only; see esc_wire_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_int.h"

/** @brief A byte and its help line.  A byte one field holds in every layout
 *         has @c from and @c to 0. */
typedef struct {
    uint8_t      off;  /**< the byte  */
    ui_text_id_t text; /**< the line  */
    uint8_t      from; /**< the first layout the line is for */
    uint8_t      to;   /**< the last; 0 with @c from 0 for every layout */
} help_t;

/** @brief help lines BLHeli_S and Bluejay share. */
static const help_t k_s[] = {
    { 0x09u, TX_EW_HS_09, 0u, 0u },
    { 0x0Bu, TX_EW_HS_0B, 0u, 0u },
    { 0x0Fu, TX_EW_HS_0F, 0u, 0u },
    { 0x15u, TX_EW_HS_15, 0u, 0u },
    { 0x19u, TX_EW_HS_19, 0u, 0u },
    { 0x1Au, TX_EW_HS_1A, 0u, 0u },
    { 0x1Bu, TX_EW_HS_1B, 0u, 0u },
    { 0x1Cu, TX_EW_HS_1C, 0u, 0u },
    { 0x1Du, TX_EW_HS_1D, 0u, 0u },
    { 0x1Fu, TX_EW_HS_1F, 0u, 0u },
    { 0x21u, TX_EW_HS_21, 0u, 0u },
    { 0x23u, TX_EW_HS_23, 0u, 0u },
    { 0x24u, TX_EW_HS_24, 0u, 0u },
    { 0x27u, TX_EW_HS_27, 0u, 0u },
    { 0x28u, TX_EW_HS_28, 0u, 0u },
};

/** @brief Bluejay's help lines.  Bytes 0x04 and 0x09 hold another field up to
 *         layout 200 (ew_map_bj.c); byte 0x09 has BLHeli_S's line there. */
static const help_t k_b[] = {
    { 0x04u, TX_EW_HB_04_BOOST, 200u, 200u },
    { 0x04u, TX_EW_HB_04, 201u, 208u },
    { 0x05u, TX_EW_HB_05, 0u, 0u },
    { 0x06u, TX_EW_HB_06, 0u, 0u },
    { 0x07u, TX_EW_HB_07, 0u, 0u },
    { 0x09u, TX_EW_HB_09, 201u, 208u },
    { 0x0Au, TX_EW_HB_0A, 0u, 0u },
    { 0x10u, TX_EW_HB_10, 0u, 0u },
    { 0x29u, TX_EW_HB_29, 0u, 0u },
    { 0x2Au, TX_EW_HB_2A, 0u, 0u },
};

/** @brief AM32's help lines. */
static const help_t k_a[] = {
    { 0x05u, TX_EW_HA_05, 0u, 0u },
    { 0x06u, TX_EW_HA_06, 0u, 0u },
    { 0x07u, TX_EW_HA_07, 0u, 0u },
    { 0x08u, TX_EW_HA_08, 0u, 0u },
    { 0x09u, TX_EW_HA_09, 0u, 0u },
    { 0x0Au, TX_EW_HA_0A, 0u, 0u },
    { 0x0Bu, TX_EW_HA_0B, 0u, 0u },
    { 0x0Cu, TX_EW_HA_0C, 0u, 0u },
    { 0x0Du, TX_EW_HA_0D, 0u, 0u },
    { 0x11u, TX_EW_HA_11, 0u, 0u },
    { 0x12u, TX_EW_HA_12, 0u, 0u },
    { 0x13u, TX_EW_HA_13, 0u, 0u },
    { 0x14u, TX_EW_HA_14, 0u, 0u },
    { 0x15u, TX_EW_HA_15, 0u, 0u },
    { 0x16u, TX_EW_HA_16, 0u, 0u },
    { 0x17u, TX_EW_HA_17, 0u, 0u },
    { 0x18u, TX_EW_HA_18, 0u, 0u },
    { 0x19u, TX_EW_HA_19, 0u, 0u },
    { 0x1Au, TX_EW_HA_1A, 0u, 0u },
    { 0x1Bu, TX_EW_HA_1B, 0u, 0u },
    { 0x1Cu, TX_EW_HA_1C, 0u, 0u },
    { 0x1Du, TX_EW_HA_1D, 0u, 0u },
    { 0x1Eu, TX_EW_HA_1E, 0u, 0u },
    { 0x1Fu, TX_EW_HA_1F, 0u, 0u },
    { 0x20u, TX_EW_HA_20, 0u, 0u },
    { 0x21u, TX_EW_HA_21, 0u, 0u },
    { 0x22u, TX_EW_HA_22, 0u, 0u },
    { 0x23u, TX_EW_HA_23, 0u, 0u },
    { 0x24u, TX_EW_HA_24, 0u, 0u },
    { 0x25u, TX_EW_HA_25, 0u, 0u },
    { 0x26u, TX_EW_HA_26, 0u, 0u },
    { 0x27u, TX_EW_HA_27, 0u, 0u },
    { 0x28u, TX_EW_HA_28, 0u, 0u },
    { 0x29u, TX_EW_HA_29, 0u, 0u },
    { 0x2Au, TX_EW_HA_2A, 0u, 0u },
    { 0x2Bu, TX_EW_HA_2B, 0u, 0u },
    { 0x2Cu, TX_EW_HA_2C, 0u, 0u },
    { 0x2Du, TX_EW_HA_2D, 0u, 0u },
    { 0x2Eu, TX_EW_HA_2E, 0u, 0u },
    { 0x2Fu, TX_EW_HA_2F, 0u, 0u },
};

/** @brief ESCape32's help lines, in the order of ew_map_e32. */
static const ui_text_id_t k_e[EW_E32_KEYS] = {
    TX_EW_HE_ARM,
    TX_EW_HE_DAMP,
    TX_EW_HE_REVDIR,
    TX_EW_HE_BRUSHED,
    TX_EW_HE_TIMING,
    TX_EW_HE_SINE_RANGE,
    TX_EW_HE_SINE_POWER,
    TX_EW_HE_FREQ_MIN,
    TX_EW_HE_FREQ_MAX,
    TX_EW_HE_DUTY_MIN,
    TX_EW_HE_DUTY_MAX,
    TX_EW_HE_DUTY_SPUP,
    TX_EW_HE_DUTY_RAMP,
    TX_EW_HE_DUTY_RATE,
    TX_EW_HE_DUTY_DRAG,
    TX_EW_HE_DUTY_LOCK,
    TX_EW_HE_THROT_MODE,
    TX_EW_HE_THROT_REV,
    TX_EW_HE_THROT_BRK,
    TX_EW_HE_THROT_SET,
    TX_EW_HE_THROT_ZTC,
    TX_EW_HE_THROT_CAL,
    TX_EW_HE_THROT_MIN,
    TX_EW_HE_THROT_MID,
    TX_EW_HE_THROT_MAX,
    TX_EW_HE_ANALOG_MIN,
    TX_EW_HE_ANALOG_MAX,
    TX_EW_HE_INPUT_MODE,
    TX_EW_HE_INPUT_CH1,
    TX_EW_HE_INPUT_CH2,
    TX_EW_HE_TELEM_MODE,
    TX_EW_HE_TELEM_PHID,
    TX_EW_HE_TELEM_POLES,
    TX_EW_HE_TELEM_VOLT,
    TX_EW_HE_TELEM_CURR,
    TX_EW_HE_PROT_STALL,
    TX_EW_HE_PROT_TEMP,
    TX_EW_HE_PROT_SENS,
    TX_EW_HE_PROT_VOLT,
    TX_EW_HE_PROT_CELLS,
    TX_EW_HE_PROT_CURR,
    TX_EW_HE_PROT_PARK,
    TX_EW_HE_VOLUME,
    TX_EW_HE_BEACON,
    TX_EW_HE_BEC,
    TX_EW_HE_LED,
};

/**
 * @brief A byte's line in a table.
 * @param t      The table.
 * @param n      Its rows.
 * @param layout The layout the page reports.
 * @param off    The byte.
 * @param out    Takes the text.
 * @return true when the table has the byte for that layout.
 */
static bool find(const help_t *t, size_t n, uint8_t layout, uint8_t off,
                 ui_text_id_t *out)
{
    for (size_t i = 0u; i < n; ++i) {
        if (t[i].off == off
            && ((t[i].from == 0u && t[i].to == 0u)
                || (layout >= t[i].from && layout <= t[i].to))) {
            *out = t[i].text;
            return true;
        }
    }
    return false;
}

const char *esc_wire_help_field(uint8_t fw, uint8_t layout, uint8_t off)
{
    ui_text_id_t id = TX_COUNT;
    bool got = false;

    if (fw == (uint8_t)EW_FW_AM) {
        got = find(k_a, sizeof(k_a) / sizeof(k_a[0]), layout, off, &id);
    } else if (fw == (uint8_t)EW_FW_BJ) {
        got = find(k_b, sizeof(k_b) / sizeof(k_b[0]), layout, off, &id)
              || find(k_s, sizeof(k_s) / sizeof(k_s[0]), layout, off, &id);
    } else if (fw == (uint8_t)EW_FW_S) {
        got = find(k_s, sizeof(k_s) / sizeof(k_s[0]), layout, off, &id);
    }
    return got ? ui_tr(id) : "";
}

const char *esc_wire_help_key(size_t slot)
{
    return slot < EW_E32_KEYS ? ui_tr(k_e[slot]) : "";
}
