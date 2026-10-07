/*
 * The TI INA228: 85 V, 20-bit current, voltage, power, energy and charge
 * monitor on I2C (Inter-Integrated Circuit).  The register codec and a
 * driver that talks to one part through sense_bus.h.
 *
 * Written from the datasheet, TI SLYS021A (January 2021, revised May 2022).
 * Tables and sections below are that document's.
 *
 * Calibration (§8.1.2).  CURRENT_LSB is at least the maximum current over
 * 2^19 (Equation 3), and SHUNT_CAL = 13107.2 × 10^6 × CURRENT_LSB × R,
 * times 4 at ADCRANGE 1 (Equation 2).  With the maximum in mA and R in µΩ
 * that is max × R / 40000, times 4.  ADCRANGE is 1 (±40.96 mV) when the
 * maximum across the shunt fits it, 0 (±163.84 mV) when only that fits,
 * and a maximum beyond 163.84 mV, or above 1000 A, is refused (Table 8-1).
 * SHUNT_CAL is rounded up, so CURRENT_LSB is never below the minimum and
 * the CURRENT register reaches the maximum.  Within the ADC
 * (analog-to-digital converter) range SHUNT_CAL is at most 4096: it always
 * fits its 15-bit field (Table 7-7), and the CURRENT register reaches its
 * end at or before the shunt ADC does (derived from Equations 2 and 4:
 * CURRENT is VSHUNT × 4096 / SHUNT_CAL).  Readings are converted with the
 * CURRENT_LSB that the SHUNT_CAL written gives, so the rounding does not
 * scale them.
 *
 * The MATEK I2C-INA-BM (200 µΩ, address 0x45) at its 204.8 A: ADCRANGE 1,
 * SHUNT_CAL 4096, CURRENT_LSB 390.625 µA, power 1.25 mW, energy 20 mJ and
 * charge 390.625 µC a step.
 *
 * Identity (Tables 7-23, 7-24): MANUFACTURER_ID 5449h ("TI") and DEVICE_ID
 * bits 15-4 equal to 228h, any revision in bits 3-0.  Anything else, an
 * INA238 (238xh) included, is not used and nothing is written to it.
 *
 * Clipping.  A CURRENT or VSHUNT reading at an end of its 20-bit range
 * (2^19 - 1 or -2^19) is a clip, never a value.  The datasheet does not say
 * whether CURRENT holds at its end or wraps when the current passes
 * 2^19 × CURRENT_LSB; DIAG_ALRT.MATHOF (Table 7-16) reports an arithmetic
 * overflow and is the caller's to read.
 *
 * Set-up written at each probe: CONFIG with ADCRANGE, SHUNT_CAL,
 * ADC_CONFIG, then CONFIG with RSTACC and CONFIG without it, and the three
 * read back.  Whether RSTACC clears itself is not stated (Table 7-5); it is
 * written back to 0.  Shunt temperature compensation stays off: the
 * shunts' coefficients are not known.
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

/** Registers, Table 7-3. */
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

#define INA228_MAKER_ID      0x5449u   /**< Table 7-23 */
#define INA228_DIE_ID         0x228u   /**< DEVICE_ID bits 15-4, Table 7-24 */
#define INA228_ADDR_MIN        0x40u   /**< A1, A0 at GND, GND: Table 7-2 */
#define INA228_ADDR_MAX        0x4Fu   /**< A1, A0 at SCL, SCL */
/** The largest maximum current taken, mA: every reading then fits an
 *  int32 of µA, rounding of SHUNT_CAL included. */
#define INA228_MAX_MA_LIMIT 1000000u

/** CONFIG bits, Table 7-5. */
#define INA228_CONFIG_RST      0x8000u
#define INA228_CONFIG_RSTACC   0x4000u
#define INA228_CONFIG_TEMPCOMP 0x0020u
#define INA228_CONFIG_ADCRANGE 0x0010u

/** DIAG_ALRT bits, Table 7-16. */
#define INA228_DIAG_ALATCH    0x8000u
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

/** ADC_CONFIG MODE, Table 7-6: 0h and 8h shut down, 1h to 7h trigger one
 *  conversion, 9h to Fh convert continuously.  Bit 0 bus voltage, bit 1
 *  shunt voltage, bit 2 temperature. */
enum {
    INA228_MODE_SHUTDOWN   = 0x0,
    INA228_MODE_CONT_BUS   = 0x9,
    INA228_MODE_CONT_SHUNT = 0xA,
    INA228_MODE_CONT_VI    = 0xB,
    INA228_MODE_CONT_ALL   = 0xF,   /**< bus, shunt and temperature */
};

/** The set-up this bench runs (DESIGN §3.4): continuous, bus and shunt
 *  540 µs (code 4), temperature 50 µs (code 0), averaging 1: a fresh
 *  result every 1130 µs.  540 µs integrates over 4 to 26 periods of an
 *  ESC's (electronic speed controller's) 8 to 48 kHz switching. */
#define INA228_ADC_BENCH 0xF900u

/** The ADC_CONFIG reset value, FB68h (Table 7-6). */
#define INA228_ADC_RESET 0xFB68u

/** ADC_CONFIG from its fields; each is masked to its width. */
uint16_t ina228_adc_config(uint8_t mode, uint8_t vbusct, uint8_t vshct,
                           uint8_t vtct, uint8_t avg);

/** Conversion time of a VBUSCT, VSHCT or VTCT code, µs (Table 7-6). */
uint32_t ina228_ct_us(uint8_t code);

/** Samples averaged for an AVG code (Table 7-6). */
uint32_t ina228_averages(uint8_t code);

/** Time from one result to the next for an ADC_CONFIG, µs: the enabled
 *  inputs' conversion times, one after another, times the averages
 *  (§7.3.4).  0 for a mode that does not convert continuously. */
uint32_t ina228_cycle_us(uint16_t adc_config);

/** Whether MANUFACTURER_ID and DEVICE_ID name an INA228. */
bool ina228_identity_ok(uint16_t maker, uint16_t device);

/* ------------------------------------------------------- calibration */

typedef enum {
    INA228_SETUP_OK = 0,
    INA228_SETUP_BAD_ADDR,     /**< outside 0x40 to 0x4F                  */
    INA228_SETUP_NO_SHUNT,     /**< a shunt of 0 µΩ                       */
    INA228_SETUP_NO_MAX,       /**< a maximum current of 0 mA             */
    INA228_SETUP_OVER_RANGE,   /**< the maximum drops more than 163.84 mV
                                    across the shunt, or passes
                                    INA228_MAX_MA_LIMIT                   */
    INA228_SETUP_UNDER_LSB,    /**< SHUNT_CAL would come to less than 1:
                                    the CURRENT register would read 0      */
} ina228_setup_err_t;

typedef struct {
    uint32_t shunt_uohm;   /**< shunt resistance, µΩ                       */
    uint32_t max_ma;       /**< the maximum current asked for, mA          */
    uint16_t shunt_cal;    /**< SHUNT_CAL as written, 1 to 4096            */
    uint8_t  adcrange;     /**< CONFIG.ADCRANGE, 0 or 1                    */
} ina228_cal_t;

/** SHUNT_CAL and ADCRANGE for a shunt and a maximum current.  @p out is
 *  filled only on INA228_SETUP_OK. */
ina228_setup_err_t ina228_calibrate(uint32_t shunt_uohm, uint32_t max_ma,
                                    ina228_cal_t *out);

/* ------------------------------------------------------------ decoding */

/** The 20-bit value in bits 23-4 of a 24-bit register (VSHUNT, VBUS,
 *  CURRENT), as raw: -2^19 to 2^19 - 1. */
int32_t ina228_code20(uint32_t raw24);

/** CURRENT in µA; a code at either end is a clip. */
sense_value_t ina228_current_ua(const ina228_cal_t *cal, int32_t code);

/** VSHUNT in nV: 312.5 nV a step at ADCRANGE 0, 78.125 nV at 1
 *  (Table 7-9); a code at either end is a clip. */
sense_value_t ina228_vshunt_nv(const ina228_cal_t *cal, int32_t code);

/** VBUS in µV, 195.3125 µV a step (Table 7-10). */
int32_t ina228_vbus_uv(int32_t code);

/** DIETEMP in m°C, 7.8125 m°C a step (Table 7-11). */
int32_t ina228_dietemp_mdegc(uint16_t raw);

/** POWER (24 bit, unsigned) in µW: 3.2 × CURRENT_LSB a step (Eq. 5). */
uint64_t ina228_power_uw(const ina228_cal_t *cal, uint32_t raw24);

/** ENERGY (40 bit, unsigned) in mJ: 16 × 3.2 × CURRENT_LSB a step
 *  (Eq. 6). */
uint64_t ina228_energy_mj(const ina228_cal_t *cal, uint64_t raw40);

/** CHARGE (40 bit, two's complement) in µC: CURRENT_LSB a step
 *  (Eq. 7). */
int64_t ina228_charge_uc(const ina228_cal_t *cal, uint64_t raw40);

/* -------------------------------------------------------------- driver */

typedef struct {
    sense_part_t part;
    ina228_cal_t cal;
    uint16_t     config;       /**< CONFIG as written, RSTACC clear      */
    uint16_t     adc_config;   /**< ADC_CONFIG as written                */
} ina228_t;

/** A part at @p addr with a shunt, a maximum current and an ADC_CONFIG
 *  (INA228_ADC_BENCH for this bench).  On anything but INA228_SETUP_OK
 *  the part is left SENSE_PART_UNSET and nothing is ever sent to it. */
ina228_setup_err_t ina228_init(ina228_t *d, sense_bus_t *bus, uint8_t addr,
                               uint32_t shunt_uohm, uint32_t max_ma,
                               uint16_t adc_config);

/** Every tick: probes the part when it is due -- at the first call, and
 *  SENSE_RETRY_MS after it was found absent, wrong, refused or offline.
 *  A probe is 10 transactions, 1075 µs of bus time at 400 kHz without
 *  the controller's own time between them.  Returns whether the part is
 *  online. */
bool ina228_step(ina228_t *d, uint32_t now_ms);

sense_state_t ina228_state(const ina228_t *d);

/** Readings.  Each is one transaction; on anything but SENSE_OK the output
 *  is left as it was. */
sense_err_t ina228_read_current(ina228_t *d, sense_value_t *ua);
sense_err_t ina228_read_vshunt(ina228_t *d, sense_value_t *nv);
sense_err_t ina228_read_vbus(ina228_t *d, int32_t *uv);
sense_err_t ina228_read_power(ina228_t *d, uint64_t *uw);
sense_err_t ina228_read_dietemp(ina228_t *d, int32_t *mdegc);
/** DIAG_ALRT as read; the read clears the latched flags (Table 7-16). */
sense_err_t ina228_read_diag(ina228_t *d, uint16_t *diag);
sense_err_t ina228_read_energy(ina228_t *d, uint64_t *mj);
sense_err_t ina228_read_charge(ina228_t *d, int64_t *uc);

/** ENERGY and CHARGE to 0: CONFIG with RSTACC, then without.  A failure
 *  of the second write leaves RSTACC set; the call is to be repeated. */
sense_err_t ina228_clear_totals(ina228_t *d);

#ifdef __cplusplus
}
#endif
