/**
 * @file
 * @brief A firmware image over BLE (Bluetooth Low Energy) from a central to
 *        a peripheral: the GATT (generic attribute profile) service, the
 *        control writes and the status notifications with the bounds of
 *        every field, the data chunks, and the device's side of a transfer
 *        (bleota_dev_t): the checks before anything is erased, the
 *        physical confirmation, the credit window, the SHA-256 compare and
 *        the timeouts.
 *
 * The service: CTRL (WRITE with response, NOTIFY) carries the central's
 * control writes and the device's status notifications; DATA (WRITE
 * WITHOUT RESPONSE) carries the image in chunks, each with its offset.
 * Every control write and notification is 20 bytes or less, one ATT
 * (attribute protocol) payload at the default MTU (maximum transmission
 * unit) of 23.  A chunk fills the MTU the link agreed, up to
 * BLEOTA_CHUNK_MAX bytes of image.  All fields are little-endian.
 *
 * A transfer: BEGIN (kind, size, version, flags), then the image's
 * SHA-256 in two HASH writes.  The device checks the kind, the size
 * against its slot and the version, then asks for a physical confirmation
 * and notifies WAIT; a confirmation opens the slot and notifies CREDIT.
 * The central sends chunks up to the CREDIT's limit; the device notifies
 * CREDIT as it writes, and at once with its next offset when a chunk
 * skipped ahead (a chunk was lost).  END: the device compares the SHA-256
 * of what it wrote, sets the boot slot, notifies DONE and restarts.  Any
 * refusal, timeout or loss notifies FAIL with a code where the link
 * stands, and the device keeps the slot it runs.
 *
 * No pairing and no signature: integrity is the SHA-256 the central
 * gives, and the physical confirmation on the device is what keeps
 * another central in range from starting an update (README.md,
 * "Security").
 *
 * Pure C: no clock read, no I/O, no heap.  Times are unsigned differences
 * of a free-running 32-bit ms clock.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BLEOTA_H
#define BLEOTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The protocol's version, BEGIN's second byte. */
#define BLEOTA_PROTO 1u

/** @brief The 128-bit UUIDs (universally unique identifiers), base
 *         706E340C-76DB-4FB2-AEF1-A89F7CBDF9xx.  BLEOTA_UUID_LE(xx) is the
 *         16 bytes least significant first, as on the air and as NimBLE's
 *         BLE_UUID128_INIT() takes them. @{ */
#define BLEOTA_UUID_LE(xx) (xx), 0xF9, 0xBD, 0x7C, 0x9F, 0xA8, 0xF1, 0xAE, \
                           0xB2, 0x4F, 0xDB, 0x76, 0x0C, 0x34, 0x6E, 0x70
#define BLEOTA_UUID_SVC  0x00u /**< the service */
#define BLEOTA_UUID_CTRL 0x01u /**< CTRL: WRITE, NOTIFY */
#define BLEOTA_UUID_DATA 0x02u /**< DATA: WRITE WITHOUT RESPONSE */
/** @} */

/** @brief The service's UUID, least significant byte first. */
extern const uint8_t bleota_uuid_svc[16];
/** @brief CTRL's UUID, least significant byte first. */
extern const uint8_t bleota_uuid_ctrl[16];
/** @brief DATA's UUID, least significant byte first. */
extern const uint8_t bleota_uuid_data[16];

/** @brief What an image is for: BEGIN's kind. @{ */
#define BLEOTA_KIND_DIAL       1u /**< the M5Stack Dial */
#define BLEOTA_KIND_NODE_POWER 2u /**< the power node */
#define BLEOTA_KIND_NODE_BAL   3u /**< the balance node */
#define BLEOTA_KIND_MAX        3u /**< the highest kind */
/** @} */

/** @brief Control writes, the first byte of a CTRL write. @{ */
#define BLEOTA_OP_BEGIN 0x01u /**< kind, size, version, flags */
#define BLEOTA_OP_HASH  0x02u /**< one half of the SHA-256 */
#define BLEOTA_OP_END   0x03u /**< every byte is sent: compare, install */
#define BLEOTA_OP_ABORT 0x04u /**< stop; the device keeps its slot */
#define BLEOTA_OP_POLL  0x05u /**< notify CREDIT again */
/** @} */

/** @brief Control write lengths. @{ */
#define BLEOTA_LEN_BEGIN 14u /**< BEGIN */
#define BLEOTA_LEN_HASH  18u /**< HASH: op, part, 16 bytes */
#define BLEOTA_LEN_OP     1u /**< END, ABORT, POLL */
#define BLEOTA_CTRL_MAX  18u /**< the longest control write */
/** @} */

/** @brief BEGIN's flags. @{ */
#define BLEOTA_F_REINSTALL 0x01u /**< take the version the device runs */
#define BLEOTA_F_ALL       0x01u /**< every flag defined */
/** @} */

/** @brief Notifications, the first byte of a CTRL notification. @{ */
#define BLEOTA_EV_WAIT   0x81u /**< BEGIN taken: a confirmation is asked */
#define BLEOTA_EV_CREDIT 0x82u /**< next offset and the limit to send to */
#define BLEOTA_EV_DONE   0x83u /**< installed: the device restarts */
#define BLEOTA_EV_FAIL   0x84u /**< refused or ended; code says why */
/** @} */

/** @brief A notification's length. */
#define BLEOTA_LEN_STATUS 12u

/** @brief CREDIT's code. @{ */
#define BLEOTA_CREDIT_ROUTINE 0u /**< the limit moved, or every byte came */
#define BLEOTA_CREDIT_GAP     1u /**< bytes from next on did not arrive: a
                                      chunk past next came, or a POLL found
                                      next under the last limit; the
                                      central sends again from next */
/** @} */

/** @brief Why a transfer is refused or ended: FAIL's code. @{ */
#define BLEOTA_OK          0u  /**< none */
#define BLEOTA_E_KIND      1u  /**< the image is for another device */
#define BLEOTA_E_SIZE      2u  /**< 0 bytes, or larger than the slot */
#define BLEOTA_E_SAME      3u  /**< the version the device runs, and no
                                    BLEOTA_F_REINSTALL */
#define BLEOTA_E_PROTO     4u  /**< another protocol version or a flag
                                    not defined */
#define BLEOTA_E_BUSY      5u  /**< a BEGIN while a transfer runs */
#define BLEOTA_E_STATE     6u  /**< a write out of order */
#define BLEOTA_E_NO_CONFIRM 7u /**< no confirmation in BLEOTA_CONFIRM_MS */
#define BLEOTA_E_REFUSED   8u  /**< refused on the device */
#define BLEOTA_E_TIMEOUT   9u  /**< no write for BLEOTA_IDLE_MS */
#define BLEOTA_E_SHA       10u /**< the SHA-256 of what was written differs */
#define BLEOTA_E_FLASH     11u /**< the slot did not open, take a write or
                                    become the boot slot */
#define BLEOTA_E_SHORT     12u /**< END before the last byte */
#define BLEOTA_E_ABORTED   13u /**< the central's ABORT */
#define BLEOTA_E_IMAGE     14u /**< the device's own check refused the
                                    image */
#define BLEOTA_E_MAX       14u /**< the highest code */
/** @} */

/** @brief A chunk: its offset, then the image's bytes. @{ */
#define BLEOTA_CHUNK_HDR 4u   /**< the offset */
#define BLEOTA_CHUNK_MAX 240u /**< image bytes at an MTU of 247 */
#define BLEOTA_MTU       247u /**< the MTU a chunk fills at most */
#define BLEOTA_MTU_MIN   23u  /**< the MTU every link has */
/** @} */

/** @brief The credit window, bytes: the device holds this many bytes it has
 *         taken and not yet written, and the central keeps at most this
 *         many beyond the device's last written byte in flight.  2,048
 *         bytes are 8 full chunks; the reason is in README.md, "Credit
 *         window". */
#define BLEOTA_WINDOW 2048u
/** @brief A CREDIT is notified each time the limit moved this far. */
#define BLEOTA_CREDIT_STEP 512u

/** @brief The device's times, ms. @{ */
#define BLEOTA_CONFIRM_MS 30000u /**< the window for the confirmation */
#define BLEOTA_HOLD_MS     1000u /**< a hold of the confirming key */
#define BLEOTA_HASH_MS     5000u /**< BEGIN to the second HASH */
#define BLEOTA_IDLE_MS    10000u /**< a transfer without a write */
/** @} */

/** @brief A control write as read. */
typedef struct {
    uint8_t  op;        /**< BLEOTA_OP_* */
    uint8_t  proto;     /**< BEGIN: the protocol version */
    uint8_t  kind;      /**< BEGIN: BLEOTA_KIND_* */
    uint8_t  flags;     /**< BEGIN: BLEOTA_F_* */
    uint32_t size;      /**< BEGIN: the image, bytes */
    uint16_t ver[3];    /**< BEGIN: MAJOR, MINOR, PATCH */
    uint8_t  part;      /**< HASH: 0 the first 16 bytes, 1 the last */
    uint8_t  half[16];  /**< HASH: those bytes */
} bleota_ctrl_t;

/** @brief A notification as read. */
typedef struct {
    uint8_t  ev;        /**< BLEOTA_EV_* */
    uint8_t  code;      /**< FAIL: BLEOTA_E_* */
    uint32_t next;      /**< CREDIT: the next offset the device takes */
    uint32_t limit;     /**< CREDIT: send no byte at or past this */
    uint16_t secs;      /**< WAIT: seconds left for the confirmation */
} bleota_status_t;

/**
 * @brief Encode BEGIN.
 * @param kind  BLEOTA_KIND_*
 * @param size  the image, bytes
 * @param ver   MAJOR, MINOR, PATCH
 * @param flags BLEOTA_F_*
 * @param out   BLEOTA_LEN_BEGIN bytes
 * @return BLEOTA_LEN_BEGIN, or 0 for NULL
 */
size_t bleota_begin_encode(uint8_t kind, uint32_t size, const uint16_t ver[3],
                           uint8_t flags, uint8_t *out);

/**
 * @brief Encode one half of the SHA-256.
 * @param sha  the 32 bytes
 * @param part 0 or 1
 * @param out  BLEOTA_LEN_HASH bytes
 * @return BLEOTA_LEN_HASH, or 0 for NULL or a part over 1
 */
size_t bleota_hash_encode(const uint8_t sha[32], uint8_t part, uint8_t *out);

/**
 * @brief Encode END, ABORT or POLL.
 * @param op  BLEOTA_OP_END, BLEOTA_OP_ABORT or BLEOTA_OP_POLL
 * @param out 1 byte
 * @return 1, or 0 for NULL or another op
 */
size_t bleota_op_encode(uint8_t op, uint8_t *out);

/**
 * @brief Read a control write.  The length must be the op's exactly.
 * @param b   the bytes
 * @param n   their count
 * @param out the write, filled on success
 * @return false for NULL, an unknown op or a wrong length
 */
bool bleota_ctrl_decode(const uint8_t *b, size_t n, bleota_ctrl_t *out);

/**
 * @brief Encode a notification.
 * @param s   the notification
 * @param out BLEOTA_LEN_STATUS bytes
 * @return BLEOTA_LEN_STATUS, or 0 for NULL or an unknown event
 */
size_t bleota_status_encode(const bleota_status_t *s, uint8_t *out);

/**
 * @brief Read a notification.
 * @param b   the bytes
 * @param n   their count, BLEOTA_LEN_STATUS
 * @param out the notification, filled on success
 * @return false for NULL, a wrong length, an unknown event, a FAIL code
 *         over BLEOTA_E_MAX or a CREDIT whose limit is below its next
 */
bool bleota_status_decode(const uint8_t *b, size_t n, bleota_status_t *out);

/**
 * @brief The image bytes one chunk carries at an MTU.
 * @param mtu the ATT MTU the link agreed; under BLEOTA_MTU_MIN is
 *            BLEOTA_MTU_MIN
 * @return mtu - 3 - BLEOTA_CHUNK_HDR, at most BLEOTA_CHUNK_MAX
 */
uint16_t bleota_chunk_bytes(uint16_t mtu);

/**
 * @brief Write a chunk's header.
 * @param off the offset of its first byte
 * @param out BLEOTA_CHUNK_HDR bytes
 */
void bleota_chunk_header(uint32_t off, uint8_t out[BLEOTA_CHUNK_HDR]);

/**
 * @brief Read a chunk.
 * @param b    the DATA write
 * @param n    its length
 * @param off  the offset of its first image byte
 * @param data the first image byte, inside @p b
 * @param len  the image bytes, 1 to BLEOTA_CHUNK_MAX
 * @return false for NULL, a write without an image byte, or one longer
 *         than BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX
 */
bool bleota_chunk_decode(const uint8_t *b, size_t n, uint32_t *off,
                         const uint8_t **data, uint16_t *len);

/* ------------------------------------------------------- the image head */

/** @brief The head of an ESP-IDF application image as the device reads it
 *         before its first flash write: the image header, the first
 *         segment's header, esp_app_desc_t, then the rcbench role
 *         (bleota_role_t), which ESP-IDF's `.rodata_custom_desc` places
 *         right after esp_app_desc_t. @{ */
#define BLEOTA_IMG_MAGIC  0xE9u       /**< byte 0 of the image */
#define BLEOTA_HEAD_CHIP  12u         /**< the chip ID, 16 bits */
#define BLEOTA_HEAD_DESC  32u         /**< esp_app_desc_t */
#define BLEOTA_DESC_MAGIC 0xABCD5432u /**< its first word */
#define BLEOTA_HEAD_ROLE  288u        /**< bleota_role_t: 32 + 256 */
#define BLEOTA_ROLE_MAGIC 0x52626372u /**< "rcbR", least significant first */
#define BLEOTA_ROLE_LEN   8u          /**< bleota_role_t's bytes */
#define BLEOTA_HEAD_LEN   (BLEOTA_HEAD_ROLE + BLEOTA_ROLE_LEN) /**< 296 */
/** @} */

/** @brief The role an image is built for, in its `.rodata_custom_desc`. */
typedef struct {
    uint32_t magic;       /**< BLEOTA_ROLE_MAGIC */
    uint8_t  kind;        /**< BLEOTA_KIND_* */
    uint8_t  proto;       /**< BLEOTA_PROTO */
    uint8_t  reserved[2]; /**< 0 */
} bleota_role_t;

/**
 * @brief Check an image's head against the device that would take it.
 * @param b    the image's first bytes
 * @param n    their count; under BLEOTA_HEAD_LEN refuses
 * @param chip the device's ESP chip ID
 * @param kind the device's BLEOTA_KIND_*
 * @return BLEOTA_OK; BLEOTA_E_KIND for another chip or another role;
 *         BLEOTA_E_IMAGE for a head that is no application with a role
 */
uint8_t bleota_head_check(const uint8_t *b, size_t n, uint16_t chip,
                          uint8_t kind);

/* ---------------------------------------------------------- the device */

/** @brief How a device takes its confirmation. @{ */
#define BLEOTA_CONFIRM_PRESS 0u /**< a press accepts; a hold of
                                     BLEOTA_HOLD_MS refuses (the Dial) */
#define BLEOTA_CONFIRM_HOLD  1u /**< a hold of BLEOTA_HOLD_MS accepts
                                     (a node's BOOT key) */
/** @} */

/** @brief What the device is. */
typedef struct {
    uint8_t  kind;    /**< BLEOTA_KIND_* it takes */
    uint8_t  confirm; /**< BLEOTA_CONFIRM_* */
    uint32_t slot;    /**< the slot an image goes into, bytes */
    uint16_t ver[3];  /**< the version it runs */
    uint16_t chip;    /**< its ESP chip ID, for bleota_head_check() */
} bleota_dev_cfg_t;

/** @brief Where a device's transfer stands. */
typedef enum {
    BLEOTA_DEV_IDLE = 0, /**< none */
    BLEOTA_DEV_HASH,     /**< BEGIN taken: the SHA-256's halves due */
    BLEOTA_DEV_CONFIRM,  /**< waiting for the confirmation */
    BLEOTA_DEV_OPEN,     /**< confirmed: the slot is being opened */
    BLEOTA_DEV_RECV,     /**< taking chunks */
    BLEOTA_DEV_VERIFY,   /**< every byte written: the SHA-256 is due */
    BLEOTA_DEV_INSTALL,  /**< the SHA-256 matches: the boot slot is set */
    BLEOTA_DEV_DONE,     /**< installed: the restart is due */
} bleota_dev_state_t;

/** @brief What the device's caller does next (bleota_dev_poll()). */
typedef enum {
    BLEOTA_DO_NONE = 0, /**< nothing */
    BLEOTA_DO_OPEN,     /**< open the slot without erasing it, then
                             bleota_dev_opened() */
    BLEOTA_DO_WRITE,    /**< bleota_dev_take() bytes, write them, add
                             them to the SHA-256, bleota_dev_written();
                             only once the image's head passed
                             bleota_head_check() */
    BLEOTA_DO_VERIFY,   /**< finish the SHA-256, bleota_dev_verify() */
    BLEOTA_DO_INSTALL,  /**< check the image, set the boot slot,
                             bleota_dev_installed() */
    BLEOTA_DO_CLOSE,    /**< drop the open slot, bleota_dev_closed() */
    BLEOTA_DO_RESTART,  /**< restart into the new image */
} bleota_do_t;

/** @brief Notifications a device holds before its caller sends them. */
#define BLEOTA_DEV_OUT 4u

/** @brief A device's transfer.  The members are private but for the
 *         tests and the device's face. */
typedef struct {
    bleota_dev_cfg_t cfg;   /**< what the device is */
    uint8_t  st;            /**< bleota_dev_state_t */
    uint8_t  code;          /**< the last FAIL's code, BLEOTA_OK before */
    uint8_t  flags;         /**< BEGIN's flags */
    uint8_t  halves;        /**< HASH parts taken: bit 0 and bit 1 */
    uint32_t size;          /**< BEGIN's size */
    uint16_t ver[3];        /**< BEGIN's version */
    uint8_t  sha[32];       /**< the SHA-256 BEGIN's HASH writes gave */
    uint32_t t;             /**< the state's start, or the last write */
    bool     open;          /**< the slot is open, or being opened: a CLOSE
                                 is due on a fail */
    bool     opening;       /**< DO_OPEN is out, bleota_dev_opened() not
                                 yet called */
    bool     close_due;     /**< DO_CLOSE is due */
    bool     end_asked;     /**< END came */
    bool     head_ok;       /**< the image's head passed: writes may go */
    bool     key_down;      /**< the confirming key is down */
    bool     key_armed;     /**< the key was up since the window opened */
    uint32_t key_t;         /**< when it went down */
    uint8_t *ring;          /**< the caller's buffer */
    uint32_t ring_size;     /**< its size, a power of two of at least
                                 BLEOTA_WINDOW */
    uint32_t rx;            /**< the next offset taken: bytes in the ring
                                 and written */
    uint32_t wr;            /**< bytes written */
    uint32_t taken;         /**< bytes handed to the caller, not yet
                                 written */
    uint32_t told;          /**< the limit the last CREDIT gave */
    uint32_t nak_at;        /**< rx when a skipped chunk was answered;
                                 UINT32_MAX none */
    uint32_t chunks;        /**< chunks taken */
    uint32_t dups;          /**< chunks dropped as already taken */
    uint32_t gaps;          /**< chunks dropped for a gap before them */
    uint32_t over;          /**< chunks dropped past the limit or size */
    uint8_t  out[BLEOTA_DEV_OUT][BLEOTA_LEN_STATUS]; /**< notifications */
    uint8_t  out_head;      /**< the first of them */
    uint8_t  out_n;         /**< their count */
} bleota_dev_t;

/**
 * @brief Set up a device: idle, over the caller's buffer.
 * @param d         the device
 * @param cfg       what it is
 * @param ring      the buffer for taken bytes
 * @param ring_size its size: a power of two of at least BLEOTA_WINDOW
 * @return false for NULL, a buffer that is too small or not a power of
 *         two, or a kind out of range
 */
bool bleota_dev_init(bleota_dev_t *d, const bleota_dev_cfg_t *cfg,
                     uint8_t *ring, uint32_t ring_size);

/**
 * @brief Take a control write.
 * @param d   the device
 * @param b   the bytes
 * @param n   their count
 * @param now the clock
 */
void bleota_dev_ctrl(bleota_dev_t *d, const uint8_t *b, size_t n,
                     uint32_t now);

/**
 * @brief Take a DATA write: a chunk at the next offset goes into the
 *        buffer; one past it is dropped and answered with the next offset
 *        once; one at or before it is dropped as already taken, or its new
 *        tail taken.
 * @param d   the device
 * @param b   the bytes
 * @param n   their count
 * @param now the clock
 */
void bleota_dev_data(bleota_dev_t *d, const uint8_t *b, size_t n,
                     uint32_t now);

/**
 * @brief The confirming key's state, at least every 20ms.
 * @param d    the device
 * @param down the key is down
 * @param now  the clock
 */
void bleota_dev_key(bleota_dev_t *d, bool down, uint32_t now);

/**
 * @brief The link to the central ended: an open slot is dropped with no
 *        notification.  An install under way (BLEOTA_DEV_INSTALL) goes on:
 *        the boot slot may already be set, so its result decides, and an
 *        installed image restarts.
 * @param d   the device
 */
void bleota_dev_lost(bleota_dev_t *d);

/**
 * @brief Times run out, and what the caller does next.
 * @param d   the device
 * @param now the clock
 * @return the next step
 */
bleota_do_t bleota_dev_poll(bleota_dev_t *d, uint32_t now);

/**
 * @brief The slot opened, or did not.  A transfer that ended while the slot
 *        opened (a lost link, an ABORT) already holds a CLOSE due for an
 *        open that worked; one that did not open clears it, so exactly one
 *        release follows either way.
 * @param d   the device
 * @param ok  it opened
 * @param now the clock
 */
void bleota_dev_opened(bleota_dev_t *d, bool ok, uint32_t now);

/**
 * @brief Bytes to write, in order, from the buffer.
 * @param d   the device
 * @param out where they go
 * @param max at most this many
 * @return their count; 0 when none waits
 */
uint32_t bleota_dev_take(bleota_dev_t *d, uint8_t *out, uint32_t max);

/**
 * @brief The bytes of the last bleota_dev_take() are written, or the write
 *        failed.
 * @param d   the device
 * @param ok  they are written
 * @param now the clock
 */
void bleota_dev_written(bleota_dev_t *d, bool ok, uint32_t now);

/**
 * @brief Compare the SHA-256 of every byte written with BEGIN's.
 * @param d      the device
 * @param digest the SHA-256 of what was written
 * @param now    the clock
 * @return true when they are equal: the install is due
 */
bool bleota_dev_verify(bleota_dev_t *d, const uint8_t digest[32],
                       uint32_t now);

/**
 * @brief The image is installed and the boot slot set, or not.
 * @param d    the device
 * @param ok   it is installed
 * @param code BLEOTA_E_IMAGE or BLEOTA_E_FLASH when not
 * @param now  the clock
 */
void bleota_dev_installed(bleota_dev_t *d, bool ok, uint8_t code,
                          uint32_t now);

/**
 * @brief The open slot is dropped.
 * @param d the device
 */
void bleota_dev_closed(bleota_dev_t *d);

/**
 * @brief The next notification to send.
 * @param d   the device
 * @param out BLEOTA_LEN_STATUS bytes
 * @return false when none waits
 */
bool bleota_dev_status(bleota_dev_t *d, uint8_t *out);

/**
 * @brief Seconds left in the confirmation window.
 * @param d   the device
 * @param now the clock
 * @return 0 outside BLEOTA_DEV_CONFIRM
 */
uint16_t bleota_dev_secs(const bleota_dev_t *d, uint32_t now);

/**
 * @brief The share of the image taken, 0 to 100.
 * @param d the device
 * @return the share
 */
unsigned bleota_dev_percent(const bleota_dev_t *d);

#ifdef __cplusplus
}
#endif
#endif
