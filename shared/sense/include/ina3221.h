/**
 * @file
 * @brief The TI INA3221: three channels of 13-bit shunt and bus voltage, 0 to
 * 26V, on I2C (Inter-Integrated Circuit).
 *
 * The register codec and a driver that talks to one part through sense_bus.h.
 *
 * Written from the datasheet, TI SBOS576C (May 2012, revised September
 * 2026).  Tables and sections below are that document's.
 *
 * Readings.  Shunt and bus registers hold a sign and 12 data bits in bits
 * 15-3 (Table 8-2): shunt 40µV a step, ±163.84mV; bus 8mV a step,
 * measured at IN-, the load side of the shunt (§7.3.1).  The DAOKAI
 * module's 0.1Ω shunts give 400µA a step and 1.638A at the top code
 * (7FF8h); a reading at either end of the code range is a clip, never a
 * value.  What the part reports past 163.84mV is not stated; a reading
 * there is taken to stay at the end code.
 *
 * Identity (§8.2.19, §8.2.20): manufacturer 5449h ("TI"), die 3220h.  A
 * DAOKAI unit read by one buyer answered 1408h, 0000h and configuration
 * 1101h: such a part is reported with what it answered and is not used.
 * Nothing is written to a part before both IDs match.
 *
 * Averaging.  Table 8-4 says AVG sets the samples averaged together;
 * §7.4.1 describes a running filter that updates on every conversion.
 * When a result updates with averaging above 1 is not settled by the
 * datasheet and not measured; this bench runs averaging at 1.
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
 * @brief Registers, Table 8-1.
 *
 * All 16 bit.
 */
enum {
    INA3221_CONFIG       = 0x00,
    INA3221_SHUNT1       = 0x01,   /**< channel n: 0x01 + 2 (n - 1) */
    INA3221_BUS1         = 0x02,   /**< channel n: 0x02 + 2 (n - 1) */
    INA3221_MASK_ENABLE  = 0x0F,
    INA3221_MANUFACTURER = 0xFE,
    INA3221_DIE_ID       = 0xFF,
};

/**
 * @brief Identity values and I2C addresses of the INA3221.
 */
#define INA3221_MAKER_ID  0x5449u   /**< §8.2.19 */
#define INA3221_DIE       0x3220u   /**< §8.2.20 */
#define INA3221_ADDR_MIN    0x40u   /**< A0 at GND, Table 7-1 */
#define INA3221_ADDR_MAX    0x43u   /**< A0 at SCL */
#define INA3221_CHANNELS       3u

/**
 * @brief Below this the top code, 163.8mV over the shunt, is more µA than an
 * int32 holds: 100 µΩ reads to 1638A.
 */
#define INA3221_SHUNT_MIN_UOHM 100u

/** @brief Configuration bits, Table 8-4. */
#define INA3221_CONFIG_RST    0x8000u
#define INA3221_CONFIG_CH1EN  0x4000u
#define INA3221_CONFIG_CH2EN  0x2000u
#define INA3221_CONFIG_CH3EN  0x1000u
#define INA3221_CONFIG_RESET  0x7127u   /**< power-on value */

/**
 * @brief Configuration MODE, Table 8-4.
 *
 * Bit 0 shunt, bit 1 bus, bit 2 continuous; 000b and 100b power down.
 */
enum {
    INA3221_MODE_POWER_DOWN = 0,
    INA3221_MODE_SHUNT_CONT = 5,
    INA3221_MODE_BUS_CONT   = 6,
    INA3221_MODE_CONT       = 7,     /**< shunt and bus */
};

/**
 * @brief Mask/Enable bits, Table 8-34.
 *
 * Reading the register clears CF1-3, SF, WF1-3 and CVRF.
 */
#define INA3221_ME_SCC1  0x4000u
#define INA3221_ME_SCC2  0x2000u
#define INA3221_ME_SCC3  0x1000u
#define INA3221_ME_WEN   0x0800u
#define INA3221_ME_CEN   0x0400u
#define INA3221_ME_CF1   0x0200u
#define INA3221_ME_CF2   0x0100u
#define INA3221_ME_CF3   0x0080u
#define INA3221_ME_SF    0x0040u
#define INA3221_ME_WF1   0x0020u
#define INA3221_ME_WF2   0x0010u
#define INA3221_ME_WF3   0x0008u
#define INA3221_ME_PVF   0x0004u
#define INA3221_ME_TCF   0x0002u
#define INA3221_ME_CVRF  0x0001u

/**
 * @brief The set-ups this bench runs (DESIGN §3.4): continuous shunt and bus,
 * 140µs each (code 0), averaging 1.
 *
 * CH1 alone gives a fresh result every 280µs; all three every 840µs.
 */
#define INA3221_CONFIG_BENCH_CH1 0x4007u
#define INA3221_CONFIG_BENCH_ALL 0x7007u

/**
 * @brief Configuration from its fields.  @p channels: bit 0 CH1, bit 1 CH2, bit
 * 2 CH3, which the register holds in bits 14, 13 and 12.
 *
 * @param channels Bit 0 CH1, bit 1 CH2, bit 2 CH3, which the register holds in
 *       bits 14, 13 and 12.
 * @param avg Averaging code.
 * @param vbusct Bus conversion time code.
 * @param vshct Shunt conversion time code.
 * @param mode Operating mode, an INA3221_MODE_* value.
 * @return The Configuration register value.
 */
uint16_t ina3221_config(uint8_t channels, uint8_t avg, uint8_t vbusct,
                        uint8_t vshct, uint8_t mode);

/**
 * @brief The channels a configuration enables: bit 0 CH1 to bit 2 CH3.
 *
 * @param config The Configuration register value.
 * @return Bit 0 CH1 to bit 2 CH3.
 */
uint8_t ina3221_channels(uint16_t config);

/**
 * @brief Conversion time of a VBUSCT or VSHCT code, µs, typical (Table 8-4; the
 * maxima in §6.5 run 10% longer).
 *
 * @param code The conversion time code.
 * @return The typical conversion time, in µs.
 */
uint32_t ina3221_ct_us(uint8_t code);

/**
 * @brief Time from one result to the next on every enabled channel, µs, with
 * averaging at 1: per channel the shunt then the bus conversion, channels one
 * after another (§7.3.1).
 *
 * 0 for a mode that does not convert continuously.
 *
 * @param config The Configuration register value.
 * @return The cycle time, in µs; 0 for a mode that does not convert
 *       continuously.
 */
uint32_t ina3221_cycle_us(uint16_t config);

/**
 * @brief Whether the manufacturer and die IDs name an INA3221.
 *
 * @param maker The manufacturer ID register value.
 * @param die The die ID register value.
 * @return true when maker is 5449h and die is 3220h.
 */
bool ina3221_identity_ok(uint16_t maker, uint16_t die);

/**
 * @brief The 13-bit value in bits 15-3 of a shunt or bus register: -4096 to
 * 4095.
 *
 * @param raw The register value.
 * @return The signed 13-bit code, -4096 to 4095.
 */
int32_t ina3221_code13(uint16_t raw);

/**
 * @brief A shunt code as current, µA: 40µV a step over @p shunt_uohm.
 *
 * A code at either end (4095, -4096) is a clip.
 *
 * @param shunt_uohm Shunt resistance, in µΩ.
 * @param code The shunt code, from ina3221_code13().
 * @return The current in µA.
 */
sense_value_t ina3221_current_ua(uint32_t shunt_uohm, int32_t code);

/**
 * @brief The current the end code of the range stands for, µA: code 4095 for
 * SENSE_CLIP_HIGH (1.638A on 0.1Ω), code -4096 for SENSE_CLIP_LOW (-1.6384
 * A).
 *
 * 0 for SENSE_CLIP_NONE.
 *
 * @param shunt_uohm Shunt resistance, in µΩ.
 * @param end The clip the reading sits at.
 * @return The current in µA.
 */
int32_t ina3221_end_ua(uint32_t shunt_uohm, sense_clip_t end);

/**
 * @brief A bus code in mV, 8mV a step.
 *
 * @param code The bus code, from ina3221_code13().
 * @return The voltage in mV.
 */
int32_t ina3221_bus_mv(int32_t code);

/* -------------------------------------------------------------- driver */

typedef enum {
    INA3221_SETUP_OK = 0,
    INA3221_SETUP_BAD_ADDR,     /**< outside 0x40 to 0x43                 */
    INA3221_SETUP_NO_SHUNT,     /**< below INA3221_SHUNT_MIN_UOHM         */
    INA3221_SETUP_NO_CHANNEL,   /**< the configuration enables none       */
} ina3221_setup_err_t;

/**
 * @brief One INA3221 on a sensor bus: its part record, shunts and Configuration.
 */
typedef struct {
    sense_part_t part;
    uint32_t     shunt_uohm[INA3221_CHANNELS]; /**< CH1 to CH3; 0 for a
                                    channel the configuration leaves off */
    uint16_t     config;       /**< the Configuration register as written */
} ina3221_t;

/**
 * @brief A part at @p addr with one shunt value for its channels and a
 * Configuration value (INA3221_CONFIG_BENCH_*).
 *
 * On anything but INA3221_SETUP_OK the part is left SENSE_PART_UNSET and
 * nothing is ever sent to it.
 *
 * @param d The driver to set up.
 * @param bus The sensor bus the part sits on.
 * @param addr I2C address, INA3221_ADDR_MIN to INA3221_ADDR_MAX.
 * @param shunt_uohm Shunt resistance for every channel, in µΩ, at least
 *       INA3221_SHUNT_MIN_UOHM.
 * @param config The Configuration register value.
 * @return INA3221_SETUP_OK, or the reason the set-up was refused.
 */
ina3221_setup_err_t ina3221_init(ina3221_t *d, sense_bus_t *bus, uint8_t addr,
                                 uint32_t shunt_uohm, uint16_t config);

/**
 * @brief The same with a shunt for each channel, @p shunt_uohm[0] CH1's.
 *
 * Each channel the configuration enables needs one of at least
 * INA3221_SHUNT_MIN_UOHM (INA3221_SETUP_NO_SHUNT otherwise); a channel it
 * leaves off keeps 0 whatever is given.
 *
 * @param d The driver to set up.
 * @param bus The sensor bus the part sits on.
 * @param addr I2C address, INA3221_ADDR_MIN to INA3221_ADDR_MAX.
 * @param shunt_uohm One shunt value per channel, in µΩ, CH1 first.
 * @param config The Configuration register value.
 * @return INA3221_SETUP_OK, or the reason the set-up was refused.
 */
ina3221_setup_err_t ina3221_init_shunts(ina3221_t *d, sense_bus_t *bus,
                                        uint8_t addr,
                                        const uint32_t *shunt_uohm,
                                        uint16_t config);

/**
 * @brief Every tick: probes the part when it is due -- at the first call, and
 * SENSE_RETRY_MS after it was found absent, wrong, refused or offline.
 *
 * A probe is 4 transactions, 455µs of bus time at 400kHz without the
 * controller's own time between them.  Returns whether the part is online.
 *
 * @param d The driver.
 * @param now_ms The current time, in ms.
 * @return true when the part is online.
 */
bool ina3221_step(ina3221_t *d, uint32_t now_ms);

/**
 * @brief The part's state as of the last probe.
 *
 * @param d The driver.
 * @return The sense_state_t of the part.
 */
sense_state_t ina3221_state(const ina3221_t *d);

/**
 * @brief Channel @p ch (1 to 3) current, µA.
 *
 * A channel the configuration does not enable, or a mode that does not convert
 * the shunt, is SENSE_BAD_ARG and nothing is sent.  On anything but SENSE_OK @p
 * ua is left as it was.
 *
 * @param d The driver.
 * @param ch Channel, 1 to 3.
 * @param ua Receives the current, in µA.
 * @return SENSE_OK, SENSE_BAD_ARG, or the bus error.
 */
sense_err_t ina3221_read_current(ina3221_t *d, uint8_t ch, sense_value_t *ua);

/**
 * @brief Channel @p ch (1 to 3) bus voltage, mV, at IN-; the same refusals.
 *
 * @param d The driver.
 * @param ch Channel, 1 to 3.
 * @param mv Receives the bus voltage, in mV.
 * @return SENSE_OK, SENSE_BAD_ARG, or the bus error.
 */
sense_err_t ina3221_read_bus(ina3221_t *d, uint8_t ch, int32_t *mv);

/**
 * @brief Mask/Enable as read; the read clears its flags.
 *
 * @param d The driver.
 * @param mask_enable Receives the Mask/Enable value.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina3221_read_flags(ina3221_t *d, uint16_t *mask_enable);

/**
 * @brief The Configuration register read back against the value written.
 *
 * A part that resets itself answers with INA3221_CONFIG_RESET: all three
 * channels, 1.1ms conversions, a result every 6.6ms.  Another value than the
 * one written is read a second time at once, so one corrupted read is no reset;
 * when both differ the part is taken offline (sense_part_lost()), @p lost is
 * set and SENSE_OK returned.  A read of FFFFh, the bus that no part drives,
 * takes it offline on that read without a second.  @p lost is false otherwise.
 * The readings are not checked for all ones: a shunt or bus register FFFFh is
 * code -1 and taken.  One transaction, two on a first mismatch other than
 * FFFFh: 120µs of bus time each at 400kHz.
 *
 * @param d The driver.
 * @param lost Set true when the part was taken offline.
 * @return SENSE_OK, or the bus error.
 */
sense_err_t ina3221_verify(ina3221_t *d, bool *lost);

#ifdef __cplusplus
}
#endif
