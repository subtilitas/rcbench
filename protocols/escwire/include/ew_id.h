/**
 * @file ew_id.h
 * @brief Identification: what a first contact's answer and the first reads say
 *        about the ESC (electronic speed controller) on the wire, and whether
 *        its page may be written.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_ID_H
#define EW_ID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ew_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The bytes of the name region of an AM32 page. */
#define EW_ID_NAME_REGION 32u
/** @brief The most characters of the name. */
#define EW_ID_NAME_MAX 29u
/** @brief The bytes of the device-info record. */
#define EW_ID_RECORD_LEN 27u
/** @brief The firmware start in the record that marks a CAN (Controller Area
 *         Network) build. */
#define EW_ID_CAN_START 0x4000u

/** @brief Settings address on EFM8BB1 and EFM8BB2. */
#define EW_EFM8_BASE_BB2 0x1A00u
/** @brief Flash page on EFM8BB1 and EFM8BB2, bytes. */
#define EW_EFM8_PAGE_BB2 512u
/** @brief Bootloader on EFM8BB1 and EFM8BB2. */
#define EW_EFM8_BOOT_BB2 0x1C00u
/** @brief Settings address on EFM8BB5x. */
#define EW_EFM8_BASE_BB5 0x3000u
/** @brief Flash page on EFM8BB5x, bytes. */
#define EW_EFM8_PAGE_BB5 2048u
/** @brief Bootloader on EFM8BB5x. */
#define EW_EFM8_BOOT_BB5 0xF000u
/** @brief AM32's special address of the settings. */
#define EW_AM_ADDR_SETTINGS 0x0020u
/** @brief AM32's special address of the name region. */
#define EW_AM_ADDR_NAME 0x0021u
/** @brief AM32's special address: continue after the last byte read. */
#define EW_AM_ADDR_NEXT 0x0022u
/** @brief AM32's special address of the device-info record. */
#define EW_AM_ADDR_RECORD 0x0023u
/** @brief The first plain flash address on AM32. */
#define EW_AM_ADDR_FLASH 0x0400u

/** @brief The processor a first contact names. */
typedef enum {
    EW_CPU_NONE = 0, /**< no acceptable answer       */
    EW_CPU_EFM8,     /**< BLHeli_S or Bluejay        */
    EW_CPU_ARM,      /**< AM32, or a firmware not supported */
    EW_CPU_TEXT,     /**< ESCape32: no first contact, a text interface */
} ew_cpu_t;

/** @brief Why a page is not written, or why an ESC is refused. */
typedef enum {
    EW_WHY_NONE = 0,     /**< written                                      */
    /* Read-only, with the firmware known. */
    EW_WHY_OLD_LAYOUT,   /**< a layout shown and not written               */
    EW_WHY_RESET_PAGE,   /**< the ESC rewrote its page: tags are 0xFF      */
    EW_WHY_MARKER,       /**< the settings marker is wrong                 */
    EW_WHY_OLD_REVISION, /**< AM32 revision below 3                        */
    EW_WHY_NEW_REVISION, /**< AM32 revision above 4                        */
    EW_WHY_ERASED,       /**< AM32: byte 0 is 0xFF                         */
    EW_WHY_CAN_BUILD,    /**< AM32 built for CAN boards                    */
    EW_WHY_TEXT_REVISION, /**< ESCape32: a revision not written            */
    EW_WHY_SHOW_KEYS,    /**< ESCape32: `show` lacks a key or has others   */
    /* Refused, the firmware not known. */
    EW_WHY_SIGNATURE,    /**< a pair of signature bytes no part has        */
    EW_WHY_PROTOCOL,     /**< AM32 bootloader protocol not 2 or 3          */
    EW_WHY_PIN_CODE,     /**< a pin code not listed                        */
    EW_WHY_START_MARKER, /**< ARM: byte 0 is neither 0x01 nor 0xFF         */
    EW_WHY_VARIANT,      /**< a blank tag with sub revision 8 or 9         */
    EW_WHY_BJ_ON_BB1,    /**< Bluejay on an EFM8BB1: signature not known   */
    EW_WHY_LAYOUT,       /**< a layout no table has                        */
    EW_WHY_NOT_KNOWN,    /**< anything else                                */
} ew_why_t;

/** @brief What a first contact's answer says. */
typedef struct {
    uint8_t  cpu;        /**< an ew_cpu_t                               */
    uint8_t  why;        /**< an ew_why_t: EW_WHY_NONE, or the refusal  */
    uint8_t  pin_code;   /**< ARM: byte 3                               */
    uint8_t  flash_code; /**< ARM: byte 4                               */
    uint8_t  protocol;   /**< ARM: byte 7                               */
    uint16_t signature;  /**< bytes 4 and 5, byte 4 high                */
    uint16_t base;       /**< the settings address                      */
    uint16_t page;       /**< EFM8: the flash page, bytes               */
    uint16_t boot;       /**< EFM8: the bootloader's address            */
} ew_contact_t;

/** @brief What identification found. */
typedef struct {
    uint8_t  fw;       /**< an ew_fw_t; EW_FW_NONE for a refused ESC      */
    uint8_t  layout;   /**< the layout or revision                        */
    uint8_t  why;      /**< an ew_why_t                                   */
    bool     writable; /**< a write is possible                           */
    bool     erased;   /**< AM32 settings erased: only a backup restores  */
    bool     can_build; /**< AM32 built for CAN boards: nothing is written */
    uint16_t len;      /**< bytes read and written of the page            */
    char     name[EW_ID_NAME_MAX + 1u]; /**< AM32's name, "" for none     */
} ew_ident_t;

/**
 * @brief Read a first contact's answer.
 * @param ans the 9 bytes, the first 3 checked by ew_a_contact_reply()
 * @param out takes the result; cpu is the processor also when why refuses
 */
void ew_id_contact(const uint8_t *ans, ew_contact_t *out);

/**
 * @brief Identify an EFM8's page.
 * @param c    its contact
 * @param page the first EW_MAP_LEN_S bytes of its settings
 * @param out  takes the result
 */
void ew_id_efm8(const ew_contact_t *c, const uint8_t *page, ew_ident_t *out);

/**
 * @brief Identify an ARM part's page.
 * @param c      its contact
 * @param page   the EW_MAP_LEN_AM bytes of its settings
 * @param region the EW_ID_NAME_REGION bytes of the name region
 * @param record the EW_ID_RECORD_LEN bytes of the device-info record, NULL
 *               when the bootloader protocol has none
 * @param out    takes the result
 */
void ew_id_arm(const ew_contact_t *c, const uint8_t *page,
               const uint8_t *region, const uint8_t *record, ew_ident_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EW_ID_H */
