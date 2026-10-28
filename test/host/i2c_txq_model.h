/*
 * A modelled I2C bus under protocols/i2c_txq: a controller that finishes a
 * transaction a computed bus time after its start, the parts on the bus,
 * and the two lines with their pull-ups.  The suites drive the queue over
 * it on a clock they advance one microsecond at a time.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "i2c_txq.h"

/** @brief The bus, its parts and what the queue did to it. */
typedef struct {
    uint64_t now;            /**< the clock, in us                          */
    /* the parts */
    bool     present[128];   /**< an address that acknowledges             */
    int      nack_byte;      /**< written byte not acknowledged; -1 none   */
    uint32_t extra_us;       /**< added to the bus time of each transaction */
    uint32_t abort_extra;    /**< extra abort causes reported with a NACK   */
    bool     short_read;     /**< one byte fewer read than asked            */
    /* the lines */
    bool     pullups;        /**< external pull-ups fitted                  */
    bool     pad_pu;         /**< the pads' pull-ups on                     */
    int      sda_hold;       /**< SCL clocks a part still holds SDA low
                                  for; -1 forever, 0 not held              */
    uint32_t sda_hold_us;    /**< or: SDA held low until this time          */
    bool     scl_hold;       /**< a part holds SCL low                      */
    bool     stall;          /**< a part holds SCL low from the first byte
                                  of every transaction started on           */
    bool     gpio;           /**< the pins are GPIO                         */
    bool     drive_sda;      /**< GPIO mode: SDA driven low                 */
    bool     drive_scl;      /**< GPIO mode: SCL driven low                 */
    /* the controller */
    bool     pending;        /**< a transaction on the wire                 */
    bool     hang;           /**< it never ends                             */
    uint64_t end_at;         /**< when it ends                              */
    uint32_t end_abrt;       /**< with these causes                         */
    uint8_t  rx[I2C_TXQ_RD_MAX];
    uint8_t  rx_n;
    i2c_txq_xfer_t last;     /**< a copy of the last one started            */
    /* counts */
    unsigned ops;            /**< driver calls of every kind                */
    unsigned starts;
    unsigned aborts;
    unsigned resets;
    unsigned pullup_ons;
    unsigned gpio_ons;
    unsigned gpio_offs;
    unsigned scl_rises;      /**< SCL released in GPIO mode                 */
    unsigned sda_rises;
    uint64_t scl_rise_at[16]; /**< the times of the first 16              */
} model_t;

/** @brief The model's driver, ctx the model_t. */
extern const i2c_txq_ops_t model_ops;

/** @brief An idle bus with pull-ups and no part, at time @p now. */
void model_init(model_t *m, uint64_t now);

/** @brief The level of a line: true for high. */
bool model_level(const model_t *m, unsigned line);

/**
 * @brief One microsecond: the clock advanced, a transaction that has ended
 *        reported, then the queue polled.
 */
void model_tick(model_t *m, i2c_txq_t *q);

/** @brief model_tick() until the clock reads @p t. */
void model_run_to(model_t *m, i2c_txq_t *q, uint64_t t);

/** @brief A transaction that ends now, reported without a poll. */
void model_report(model_t *m, i2c_txq_t *q);
