/**
 * @file
 * @brief The TI INA228: 85 V, 20-bit current, voltage, power, energy and charge
 * monitor on I2C (Inter-Integrated Circuit).
 *
 * The register codec and a driver that talks to one part through sense_bus.h.
 *
 * Written from the datasheet, TI SLYS021A (January 2021, revised May 2022).
 * Tables and sections below are that document's.
 *
 * Calibration (§8.1.2).  The maximum current chooses the shunt range: ADCRANGE
 * 1 (±40.96 mV, 78.125 nV a step) when the maximum across the shunt fits it,
 * else ADCRANGE 0 (±163.84 mV, 312.5 nV a step); a maximum past 163.84 mV is
 * refused (Table 8-1).
 *
 * CURRENT_LSB is the ADC (analog-to-digital converter) step over the shunt, not
 * Equation 3 taken at the maximum (maximum / 2^19).  Equation 2, SHUNT_CAL =
 * 13107.2 × 10^6 × CURRENT_LSB × R, times 4 at ADCRANGE 1, then gives 13107.2 ×
 * 10^6 × 312.5 nV = 4096 at either range for every shunt, and with Equation 4
 * CURRENT = VSHUNT × 4096 / SHUNT_CAL = VSHUNT. Why:
 *
 * - The ADC's 20 bits bound the resolution.  A finer CURRENT_LSB from a maximum
 * below the range's top adds no information. - With Equation 3's LSB and such a
 * maximum, CURRENT reaches the end of its 20 bits before the ADC does, and the
 * datasheet does not say whether CURRENT then holds or wraps.  Here CURRENT and
 * VSHUNT reach their ends together, at the ADC's.
 *
 * Equation 3's condition, CURRENT_LSB at least maximum / 2^19, still holds: the
 * range is chosen so that the maximum fits it.  4096 fits SHUNT_CAL's 15-bit
 * field (Table 7-7) whatever the shunt, so no shunt is refused for it.  A shunt
 * whose full scale at the chosen range passes INA228_FS_MA_LIMIT is refused.
 *
 * The MATEK I2C-INA-BM (200 µΩ, address 0x45) at its 204.8 A: ADCRANGE 1,
 * SHUNT_CAL 4096, CURRENT_LSB 390.625 µA, power 1.25 mW, energy 20 mJ and
 * charge 390.625 µC a step.
 *
 * Identity (Tables 7-23, 7-24): MANUFACTURER_ID 5449h ("TI") and DEVICE_ID bits
 * 15-4 equal to 228h, any revision in bits 3-0.  Anything else, an INA238
 * (238xh) included, is not used and nothing is written to it.
 *
 * Clipping.  A CURRENT or VSHUNT reading at an end of its 20-bit range (2^19 -
 * 1 or -2^19) is a clip, never a value.  With the calibration above the two
 * reach their ends together, at the ADC's full scale.  What the ADC reports
 * past its full scale is not stated; it is taken to stay at the end code.
 * DIAG_ALRT.MATHOF (Table 7-16) reports an arithmetic overflow and is the
 * caller's to read.
 *
 * Set-up written at each probe: CONFIG with ADCRANGE, SHUNT_CAL, ADC_CONFIG,
 * then CONFIG with RSTACC and CONFIG without it, and the three read back.
 * Whether RSTACC clears itself is not stated (Table 7-5); it is written back to
 * 0.  Shunt temperature compensation stays off: the shunts' coefficients are
 * not known.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sense_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers, Table 7-3.
 */
enum {
    INA228_CONFIG       = 0x00,   /**< 16 bit */
    INA228_ADC_CONFIG   = 0x01,   /**< 16 bit */
    INA228_SHUNT_CAL    = 0x02,   /**< 16 bit, 15-bit field */
    INA228_SHUNT_TEMPCO = 0x03,   /**< 16 bit */
    INA228_VSHUNT       = 0x04,   /**< 24 bit, 20-bit value in 23-4 */
    INA228_VBUS         = 0x05,   /**< 24 bit, 20-bit value in 23-4 */
    INA228_DIETEMP      = 0x06,   /**< 16 bit */
    INA228_CURRENT      = 0x07,   /**< 24 bit, 20-bit value in 23-4 */
    INA228_POWER        = 0x08,   /**< 24 bit, unsigned */
    INA228_ENERGY       = 0x09,   /**< 40 bit, unsigned */
    INA228_CHARGE       = 0x0A,   /**< 40 bit, two's complement */
    INA228_DIAG_ALRT    = 0x0B,   /**< 16 bit */
    INA228_MANUFACTURER = 0x3E,   /**< 16 bit, 5449h */
    INA228_DEVICE_ID    = 0x3F,   /**< 16 bit, 228xh */
};

/**
 * @brief Identity values of the INA228.
 */
#define INA228_MAKER_ID      0x5449u   /**< Table 7-23 */
#define INA228_DIE_ID         0x228u   /**< DEVICE_ID bits 15-4, Table 7-24 */
#define INA228_ADDR_MIN        0x40u   /**< A1, A0 at GND, GND: Table 7-2 */
#define INA228_ADDR_MAX        0x4Fu   /**< A1, A0 at SCL, SCL */
/**
 * @brief The largest full scale taken, mA: every reading then fits an int32 of
 * µA.
 *
 * 2000 A is 81.92 µΩ at ADCRANGE 0 and 20.48 µΩ at ADCRANGE 1.
 */
#define INA228_FS_MA_LIMIT 2000000u
/**
 * @brief SHUNT_CAL with CURRENT_LSB at the ADC step over the shunt: 13107.2 ×
 * 10^6 × 312.5 nV (Equation 2; the shunt cancels, and the factor 4 at ADCRANGE
 * 1 meets a step 4 times smaller).
 */
#define INA228_SHUNT_CAL_ADC 4096u

/**
 * @brief CONFIG bits, Table 7-5.
 */
#define INA228_CONFIG_RST      0x8000u
/**
 * @brief Bits of the INA228 CONFIG register.
 */
#define INA228_CONFIG_RSTACC   0x4000u
#define INA228_CONFIG_TEMPCOMP 0x0020u
#define INA228_CONFIG_ADCRANGE 0x0010u

/**
 * @brief DIAG_ALRT bits, Table 7-16.
 */
#define INA228_DIAG_ALATCH    0x8000u
/**
 * @brief Bits of the INA228 DIAG_ALRT register.
 */
#define INA228_DIAG_CNVR      0x4000u
#define INA228_DIAG_SLOWALERT 0x2000u
#define INA228_DIAG_APOL      0x1000u
#define INA228_DIAG_ENERGYOF  0x0800u   /**< ENERGY rolled over; cleared by
                                             reading ENERGY              */
#define INA228_DIAG_CHARGEOF  0x0400u   /**< CHARGE rolled over; cleared by
                                             reading CHARGE              */
#define INA228_DIAG_MATHOF    0x0200u   /**< current and power may be
                                             invalid                    */
#define INA228_DIAG_TMPOL     0x0080u
#define INA228_DIAG_SHNTOL    0x0040u
#define INA228_DIAG_SHNTUL    0x0020u
#define INA228_DIAG_BUSOL     0x0010u
#define INA228_DIAG_BUSUL     0x0008u
#define INA228_DIAG_POL       0x0004u
#define INA228_DIAG_CNVRF     0x0002u   /**< a conversion cycle completed */
#define INA228_DIAG_MEMSTAT   0x0001u   /**< 1: trim memory checksum good */

/**
 * @brief ADC_CONFIG MODE, Table 7-6: 0h and 8h shut down, 1h to 7h trigger one
 * conversion, 9h to Fh convert continuously.
 *
 * Bit 0 bus voltage, bit 1 shunt voltage, bit 2 temperature.
 */
enum {
    INA228_MODE_SHUTDOWN   = 0x0,
    INA228_MODE_CONT_BUS   = 0x9,
    INA228_MODE_CONT_SHUNT = 0xA,
    INA228_MODE_CONT_VI    = 0xB,
    INA228_MODE_CONT_ALL   = 0xF,   /**< bus, shunt and temperature */
};

/**
 * @brief The set-up this bench runs (DESIGN §3.4): continuous, bus and shunt
 * 540 µs (code 4), temperature 50 µs (code 0), averaging 1: a fresh result
 * every 1130 µs.
 *
 * 540 µs integrates over 4 to 26 periods of an ESC's (electronic speed
 * controller's) 8 to 48 kHz switching.
 */
#define INA228_ADC_BENCH 0xF900u

/**
 * @brief The ADC_CONFIG reset value, FB68h (Table 7-6).
 */
#define INA228_ADC_RESET 0xFB68u

/**
 * @brief ADC_CONFIG from its fields; each is masked to its width.
 *
 * @param mode Operating mode.
 * @param vbusct Bus voltage conversion time code.
 * @param vshct Shunt voltage conversion time code.
 * @param vtct Temperature conversion time code.
 * @param avg Averaging code.
 * @return The ADC_CONFIG register value.
 */
uint16_t ina228_adc_config(uint8_t mode, uint8_t vbusct, uint8_t vshct,
                           uint8_t vtct, uint8_t avg);

/**
 * @brief Samples averaged for an AVG code (Table 7-6).
 *
 * @param code The AVG code.
 * @return The number of samples averaged.
 */
uint32_t ina228_averages(uint8_t code);

/**
 * @brief Time from one result to the next for an ADC_CONFIG, µs: the enabled
 * inputs' conversion times, one after another, times the averages (§7.3.4).
 *
 * 0 for a mode that does not convert continuously.
 *
 * @param adc_config The ADC_CONFIG register value.
 * @return The cycle time, in µs.
 */
uint32_t ina228_cycle_us(uint16_t adc_config);

/**
 * @brief Whether MANUFACTURER_ID and DEVICE_ID name an INA228.
 *
 * @param maker The MANUFACTURER_ID register value.
 * @param device The DEVICE_ID register value.
 * @return true when the IDs name an INA228.
 */
bool ina228_identity_ok(uint16_t maker, uint16_t device);

/** @brief calibration */

/**
 * @brief Result of ina228_init() and ina228_calibrate().
 */
typedef enum {
    INA228_SETUP_OK = 0,
    INA228_SETUP_BAD_ADDR,     /**< outside 0x40 to 0x4F                  */
    INA228_SETUP_NO_SHUNT,     /**< a shunt of 0 µΩ                       */
    INA228_SETUP_NO_MAX,       /**< a maximum current of 0 mA             */
    INA228_SETUP_OVER_RANGE,   /**< the maximum drops more than 163.84 mV
                                    across the shunt, or the range's full
                                    scale passes INA228_FS_MA_LIMIT        */
} ina228_setup_err_t;

/**
 * @brief The calibration of an INA228: shunt, maximum current, SHUNT_CAL and ADCRANGE.
 */
typedef struct {
    uint32_t shunt_uohm;   /**< shunt resistance, µΩ                       */
    uint32_t max_ma;       /**< the maximum current asked for, mA          */
    uint16_t shunt_cal;    /**< SHUNT_CAL as written: 4096                 */
    uint8_t  adcrange;     /**< CONFIG.ADCRANGE, 0 or 1                    */
} ina228_cal_t;

/**
 * @brief SHUNT_CAL and ADCRANGE for a shunt and a maximum current.
 *
 * @p out is filled only on INA228_SETUP_OK.
 *
 * @param shunt_uohm Shunt resistance, in µΩ.
 * @param max_ma Maximum current, in mA.
 * @param out Receives the calibration.
 * @return INA228_SETUP_OK, or the reason the set-up was refused.
 */
ina228_setup_err_t ina228_calibrate(uint32_t shunt_uohm, uint32_t max_ma,
                                    ina228_cal_t *out);

/** @brief decoding */

/**
 * @brief The 20-bit value in bits 23-4 of a 24-bit register (VSHUNT, VBUS,
 * CURRENT), as raw: -2^19 to 2^19 - 1.
 *
 * @param raw24 The 24-bit register value.
 * @return The signed 20-bit code.
 */
int32_t ina228_code20(uint32_t raw24);

/**
 * @brief CURRENT in µA; a code at either end is a clip.
 *
 * @param cal The calibration.
 * @param code The CURRENT code.
 * @return The current in µA.
 */
sense_value_t ina228_current_ua(const ina228_cal_t *cal, int32_t code);

/**
 * @brief VSHUNT in nV: 312.5 nV a step at ADCRANGE 0, 78.125 nV at 1 (Table
 * 7-9); a code at either end is a clip.
 *
 * @param cal The calibration.
 * @param code The VSHUNT code.
 * @return The shunt voltage in nV.
 */
sense_value_t ina228_vshunt_nv(const ina228_cal_t *cal, int32_t code);

/**
 * @brief VBUS in µV, 195.3125 µV a step (Table 7-10).
 *
 * @param code The VBUS code.
 * @return The bus voltage in µV.
 */
int32_t ina228_vbus_uv(int32_t code);

/**
 * @brief DIETEMP in m°C, 7.8125 m°C a step (Table 7-11).
 *
 * @param raw The DIETEMP register value.
 * @return The die temperature in m°C.
 */
int32_t ina228_dietemp_mdegc(uint16_t raw);

/**
 * @brief POWER (24 bit, unsigned) in µW: 3.2 × CURRENT_LSB a step (Eq.
 *
 * 5).
 *
 * @param cal The calibration.
 * @param raw24 The 24-bit POWER register value.
 * @return The power in µW.
 */
uint64_t ina228_power_uw(const ina228_cal_t *cal, uint32_t raw24);

/**
 * @brief ENERGY (40 bit, unsigned) in mJ: 16 × 3.2 × CURRENT_LSB a step (Eq.
 *
 * 6).
 *
 * @param cal The calibration.
 * @param raw40 The 40-bit ENERGY register value.
 * @return The energy in mJ.
 */
uint64_t ina228_energy_mj(const ina228_cal_t *cal, uint64_t raw40);

/**
 * @brief CHARGE (40 bit, two's complement) in µC: CURRENT_LSB a step (Eq.
 *
 * 7).
 *
 * @param cal The calibration.
 * @param raw40 The 40-bit CHARGE register value.
 * @return The charge in µC.
 */
int64_t ina228_charge_uc(const ina228_cal_t *cal, uint64_t raw40);

/** @brief driver */

/**
 * @brief One INA228 on a sensor bus: its part record, calibration and registers as written.
 */
typedef struct {
    sense_part_t part;
    ina228_cal_t cal;
    uint16_t     config;       /**< CONFIG as written, RSTACC clear      */
    uint16_t     adc_config;   /**< ADC_CONFIG as written                */
} ina228_t;

/**
 * @brief A part at @p addr with a shunt, a maximum current and an ADC_CONFIG
 * (INA228_ADC_BENCH for this bench).
 *
 * On anything but INA228_SETUP_OK the part is left SENSE_PART_UNSET and nothing
 * is ever sent to it.
 *
 * @param d The driver to set up.
 * @param bus The sensor bus the part sits on.
 * @param addr I2C address of the part.
 * @param shunt_uohm Shunt resistance, in µΩ.
 * @param max_ma Maximum current, in mA.
 * @param adc_config The ADC_CONFIG register value.
 * @return INA228_SETUP_OK, or the reason the set-up was refused.
 */
ina228_setup_err_t ina228_init(ina228_t *d, sense_bus_t *bus, uint8_t addr,
                               uint32_t shunt_uohm, uint32_t max_ma,
                               uint16_t adc_config);

/**
 * @brief Every tick: probes the part when it is due -- at the first call, and
 * SENSE_RETRY_MS after it was found absent, wrong, refused or offline.
 *
 * A probe is 10 transactions, 1075 µs of bus time at 400 kHz without the
 * controller's own time between them.  Returns whether the part is online.
 *
 * @param d The driver.
 * @param now_ms The current time, in ms.
 * @return true when the part is online.
 */
bool ina228_step(ina228_t *d, uint32_t now_ms);

/**
 * @brief The part's state as of the last probe.
 *
 * @param d The driver.
 * @return The sense_state_t of the part.
 */
sense_state_t ina228_state(const ina228_t *d);

/**
 * @brief Readings.
 *
 * Each is one transaction; on anything but SENSE_OK the output is left as it
 * was.
 *
 * @param d The driver.
 * @param ua Receives the current, in µA.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_read_current(ina228_t *d, sense_value_t *ua);
/**
 * @brief Read the shunt voltage.
 *
 * @param d The driver.
 * @param nv Receives the shunt voltage, in nV.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_read_vshunt(ina228_t *d, sense_value_t *nv);
/**
 * @brief Read the bus voltage.
 *
 * @param d The driver.
 * @param uv Receives the bus voltage, in µV.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_read_vbus(ina228_t *d, int32_t *uv);
/**
 * @brief Read the power.
 *
 * @param d The driver.
 * @param uw Receives the power, in µW.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_read_power(ina228_t *d, uint64_t *uw);
/**
 * @brief Read the die temperature.
 *
 * @param d The driver.
 * @param mdegc Receives the die temperature, in m°C.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_read_dietemp(ina228_t *d, int32_t *mdegc);
/**
 * @brief DIAG_ALRT as read; the read clears the latched flags (Table 7-16).
 *
 * @param d The driver.
 * @param diag Receives the DIAG_ALRT value.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_read_diag(ina228_t *d, uint16_t *diag);
/**
 * @brief Read the energy.
 *
 * @param d The driver.
 * @param mj Receives the energy, in mJ.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_read_energy(ina228_t *d, uint64_t *mj);
/**
 * @brief Read the charge.
 *
 * @param d The driver.
 * @param uc Receives the charge, in µC.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_read_charge(ina228_t *d, int64_t *uc);

/**
 * @brief ENERGY and CHARGE to 0: CONFIG with RSTACC, then without.
 *
 * A failure of the second write leaves RSTACC set; the call is to be repeated.
 *
 * @param d The driver.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_clear_totals(ina228_t *d);

/**
 * @brief ADC_CONFIG read back against the value written.
 *
 * A part that resets itself answers with INA228_ADC_RESET at either range, and
 * CONFIG with 0, which is also what ADCRANGE 0 writes: ADC_CONFIG is the
 * register that tells.  Such a part computes CURRENT at ADCRANGE 0, a quarter
 * of the truth for a set-up at ADCRANGE 1.  Another value than the one written
 * is read a second time at once, so one corrupted read is no reset; when both
 * differ the part is taken offline (sense_part_lost()), @p lost is set and
 * SENSE_OK returned.  A read of FFFFh, the bus that no part drives, takes it
 * offline on that read without a second.  @p lost is false otherwise.  The
 * readings are not checked for all ones: CURRENT FFFFFFh is code -1 and taken.
 * One transaction, two on a first mismatch other than FFFFh: 120 µs of bus time
 * each at 400 kHz.
 *
 * @param d The driver.
 * @param lost Set true when the part was taken offline.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina228_verify(ina228_t *d, bool *lost);

#ifdef __cplusplus
}
#endif
