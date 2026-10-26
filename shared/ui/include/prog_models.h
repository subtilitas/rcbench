/**
 * @file
 * @brief The modelled ESC (electronic speed controller) protocols: BLHeli_S,
 *        AM32, ESCape32 and VESC, each a table of parameters.
 *
 * No transport on the IOmcu carries these protocols, so no PROGRAM list
 * shows them; a protocol gets a row in the programmers' registry
 * (prog_registry.h) once one does.
 *
 * Pure data; no state.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>

#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The most parameters a protocol's table holds. */
#define PROG_MODEL_PARAMS_MAX 10

/** @brief What a parameter's value is. */
typedef enum {
    PROG_PARAM_ENUM = 0,   /**< one of a named few */
    PROG_PARAM_BOOL,       /**< on or off */
    PROG_PARAM_NUMBER,     /**< a bounded number, with a unit */
} prog_param_kind_t;

/** @brief One parameter of a protocol. */
typedef struct {
    ui_text_id_t       group;   /**< TX_COUNT continues the one above      */
    const char        *name;    /**< the firmware's own, English always    */
    prog_param_kind_t  kind;    /**< what its value is                     */
    ui_text_id_t       help;    /**< what it does, in one line             */

    const char *const *choices; /**< ENUM                                  */
    int                count;   /**< ENUM: the choices                     */

    int                lo;      /**< NUMBER: the lowest, in tenths if
                                     decimals is 1                         */
    int                hi;      /**< NUMBER: the highest                   */
    int                step;    /**< NUMBER: the step                      */
    int                decimals; /**< NUMBER: 0 or 1                       */
    const char        *unit;    /**< NUMBER: its unit                      */

    int                initial; /**< the value it starts at                */
} prog_param_t;

/** The class a protocol programs. */
typedef enum { PROG_MODEL_ESC = 0, PROG_MODEL_SERVO } prog_model_class_t;

/** @brief One modelled protocol. */
typedef struct {
    const char         *name;   /**< the firmware's                         */
    ui_text_id_t        transport; /**< how it travels                      */
    ui_text_id_t        device;    /**< the device line                     */
    const prog_param_t *params; /**< its parameters                     */
    int                 count;  /**< how many                               */
    int                 klass;  /**< a prog_model_class_t                   */
} prog_model_t;

/** The protocols, and their count in @p count (may be NULL). */
const prog_model_t *prog_models(size_t *count);

/**
 * @brief A protocol's name: the firmware's, the same in every language.
 * @param m the protocol
 * @return its name; "" for NULL
 */
const char *prog_model_name(const prog_model_t *m);

#ifdef __cplusplus
}
#endif
