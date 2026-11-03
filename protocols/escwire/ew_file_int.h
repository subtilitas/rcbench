/**
 * @file ew_file_int.h
 * @brief What the parts of the settings file share: ew_file.c writes,
 *        ew_file_read.c reads, ew_file_load.c holds a file against an ESC.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_FILE_INT_H
#define EW_FILE_INT_H

#include "ew_file.h"

/** @brief The start of the first line, before the format number. */
#define HEADER "ESC settings backup, format "
/** @brief The keyword of the line in front of the bytes. */
#define BYTES "bytes"
/** @brief The keyword of the last line of a verified file. */
#define CHECKSUM "checksum"
/** @brief The last line of an edited file. */
#define EDITED "edited"
/** @brief Behind a field no load takes. */
#define NOT_LOADED " [not loaded]"
/** @brief The byte order mark of UTF-8. */
#define BOM "\xEF\xBB\xBF"
/** @brief The length of the string literal @p s. */
#define STR_LEN(s) (sizeof(s) - 1u)

/** @brief Bytes on a line of the block. */
#define ROW_BYTES 16u
/** @brief "00:" and 16 times " xx". */
#define ROW_LEN (3u + 3u * ROW_BYTES)
/** @brief "checksum", a space and 8 digits. */
#define CHECKSUM_LEN (STR_LEN(CHECKSUM) + 9u)
/** @brief Room for a value's text. */
#define VALUE_MAX 32u

/** @brief The key of the firmware line. */
#define KEY_FIRMWARE  "Firmware"
/** @brief The key of the layout line. */
#define KEY_LAYOUT    "Layout"
/** @brief The key of the processor line. */
#define KEY_CPU       "Processor"
/** @brief The key of the pin code line. */
#define KEY_PIN       "Pin code"
/** @brief The key of the flash code line. */
#define KEY_FLASH     "Flash code"
/** @brief The key of the bootloader protocol line. */
#define KEY_PROTOCOL  "Bootloader protocol"
/** @brief The key of the bootloader version line. */
#define KEY_VERSION   "Bootloader version"
/** @brief The key of the name line. */
#define KEY_NAME      "Name"
/** @brief The key of the signature line. */
#define KEY_SIGNATURE "Signature"
/** @brief The key of the target line. */
#define KEY_TARGET    "Target"
/** @brief The key of ESCape32's text value. */
#define KEY_MUSIC     "music"
/** @brief A key a read did not bring. */
#define KEY_NO_VALUE  "none"

/** @brief The start of the CRC-32 register. */
#define CRC_START 0xFFFFFFFFu

/** @brief The text as it grows.  A byte past the end is counted, not stored. */
typedef struct {
    char  *out; /**< the buffer             */
    size_t cap; /**< its bytes              */
    size_t n;   /**< the bytes of the text  */
} ew_fi_buf_t;

/**
 * @brief Add one byte to the CRC-32 register.
 * @param crc  the register
 * @param byte the byte
 * @return the register
 */
uint32_t ew_fi_crc_add(uint32_t crc, uint8_t byte);

/**
 * @brief The word a file uses for a firmware.
 * @param fw an ew_fw_t
 * @return the word; "none" for EW_FW_NONE
 */
const char *ew_fi_fw_word(uint8_t fw);

/**
 * @brief Whether the lines of a file agree with its page and each other.
 * @param f the file, its bytes read
 * @return true when they do
 */
bool ew_fi_agrees(const ew_file_t *f);

/**
 * @brief Add a string.
 * @param b the text
 * @param s the string
 */
void ew_fi_put_str(ew_fi_buf_t *b, const char *s);

/**
 * @brief Add the low digits of a value in upper-case hexadecimal.
 * @param b      the text
 * @param v      the value
 * @param digits how many
 */
void ew_fi_put_hex(ew_fi_buf_t *b, uint32_t v, unsigned digits);

#endif /* EW_FILE_INT_H */
