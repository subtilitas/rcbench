# bleota

This module carries a firmware image over Bluetooth LE (Low Energy) from a
central to a peripheral. In rcbench the panel is the central and the
M5Stack Dial and the sensor nodes are the peripherals. The module holds the
GATT (generic attribute profile) service's UUIDs (universally unique
identifiers), the codec of every control write, notification and chunk, and
the device's side of a transfer (`bleota_dev_t`): the checks before
anything is erased, the physical confirmation, the credit window, the
SHA-256 compare and the timeouts. The panel's side is `shared/devupd`.

| Part | Files | Depends on |
|---|---|---|
| Core | `bleota.c`, `bleota_dev.c`, `include/bleota.h` | C11 standard headers |
| RP2350 pin driver | none | |

The module has no pin driver. Both ends run their own Bluetooth stack; the
caller moves the bytes, writes the flash and computes the SHA-256. The core
reads no clock, does no I/O and takes no heap. Times are unsigned
differences of a free-running 32-bit ms clock.

## Constraints

- All fields are little-endian.
- Every control write and every notification is 20 bytes or fewer: one ATT
  (attribute protocol) payload at the default MTU (maximum transmission
  unit) of 23.
- A chunk is a 4-byte offset and up to `MTU - 7` bytes of image: 240 bytes
  at an MTU of 247, 16 bytes at 23. ATT's write header takes 3 bytes.
- An image is 1 byte to the device's slot. The slot is 3,145,728 bytes
  (0x300000) on the Dial and 1,572,864 bytes (0x180000) on a node.
- The device takes the chunks in order. A chunk past its next offset is
  dropped and answered once with a CREDIT that names the next offset and
  carries the gap mark.
- Nothing is written before the image's first 296 bytes (`BLEOTA_HEAD_LEN`)
  name the device's chip and role ([The image head](#the-image-head)).
- The device takes an older version than its own, with the confirmation.
  It refuses its own version unless BEGIN sets `BLEOTA_F_REINSTALL`.

| Time | ms | Ends with |
|---|---:|---|
| `BLEOTA_HASH_MS`, BEGIN to the second HASH | 5,000 | FAIL `E_STATE` |
| `BLEOTA_CONFIRM_MS`, the confirmation window | 30,000 | FAIL `E_NO_CONFIRM` |
| `BLEOTA_HOLD_MS`, a hold of the confirming key | 1,000 | accept or refuse |
| `BLEOTA_IDLE_MS`, a transfer without a write | 10,000 | FAIL `E_TIMEOUT` |

## The service

Base UUID `706E340C-76DB-4FB2-AEF1-A89F7CBDF9xx`:

| xx | Characteristic | Properties | Content |
|---|---|---|---|
| 00 | the service | | |
| 01 | CTRL | WRITE, NOTIFY | control writes in, status notifications out |
| 02 | DATA | WRITE WITHOUT RESPONSE | chunks |

### Control writes

| Op | Name | Length | Bytes |
|---|---|---:|---|
| 0x01 | BEGIN | 14 | op, protocol 1, kind, flags, size u32, version u16 x 3 (MAJOR, MINOR, PATCH) |
| 0x02 | HASH | 18 | op, part 0 or 1, 16 bytes of the SHA-256 |
| 0x03 | END | 1 | op |
| 0x04 | ABORT | 1 | op |
| 0x05 | POLL | 1 | op |

Kinds: 1 the Dial, 2 the power node, 3 the balance node. Flags: bit 0
`BLEOTA_F_REINSTALL`; the other bits are refused.

### Notifications

Every notification is 12 bytes: event, code, next u32, limit u32, seconds
u16.

| Event | Name | Fields used |
|---|---|---|
| 0x81 | WAIT | seconds left of the confirmation window |
| 0x82 | CREDIT | the next offset the device takes, the limit to send below; code 0 routine, 1 the gap mark (`BLEOTA_CREDIT_GAP`) |
| 0x83 | DONE | none: the device restarts into the new image |
| 0x84 | FAIL | code |

FAIL's codes:

| Code | Name | Meaning |
|---:|---|---|
| 1 | `E_KIND` | the image is for another device: BEGIN's kind, or the head's chip or role |
| 2 | `E_SIZE` | 0 bytes, or larger than the slot |
| 3 | `E_SAME` | the version the device runs, without the reinstall flag |
| 4 | `E_PROTO` | another protocol version, or a flag not defined |
| 5 | `E_BUSY` | a BEGIN while a transfer runs |
| 6 | `E_STATE` | a write out of order, or the HASH writes late |
| 7 | `E_NO_CONFIRM` | no confirmation in 30s |
| 8 | `E_REFUSED` | refused on the device |
| 9 | `E_TIMEOUT` | no write for 10s |
| 10 | `E_SHA` | the SHA-256 of the bytes written differs from BEGIN's |
| 11 | `E_FLASH` | the slot did not open, take a write or become the boot slot |
| 12 | `E_SHORT` | END before the last byte |
| 13 | `E_ABORTED` | the central's ABORT |
| 14 | `E_IMAGE` | the head is no application with a role, or the device's own image check refused the image |

## A transfer

1. The central writes BEGIN and both HASH parts. The device checks the
   protocol and flags, the kind, the size against its slot and the version,
   in that order. A refusal is a FAIL with its code. Nothing is erased.
2. The device notifies WAIT and asks for the confirmation, for 30s.
   - `BLEOTA_CONFIRM_PRESS` (the Dial): a press released within 1s
     accepts, a hold of 1s refuses.
   - `BLEOTA_CONFIRM_HOLD` (a node's BOOT key): a hold of 1s accepts.
   - A key already down when the window opens counts only after it is
     released. No confirmation is a refusal, `E_NO_CONFIRM`.
3. Accepted: the caller opens the slot without erasing it, and the device
   notifies CREDIT with offset 0 and its limit.
4. The central writes chunks below the limit. The device puts them in its
   ring. Once 296 bytes are in, it checks the head; then the caller writes
   them to flash and adds them to the SHA-256, and the device notifies
   CREDIT as the limit moves.
5. The central writes END after the last byte. The device compares the
   SHA-256, the caller checks the image and sets the boot slot, the device
   notifies DONE, and the caller restarts.

A refusal, a timeout, a lost link or an ABORT closes the open slot. The
device keeps running the image it started with. A new transfer starts with
BEGIN at offset 0; no transfer resumes.

The install is the exception. Once the SHA-256 matched, the caller runs
ESP-IDF's image check and sets the boot slot outside the device's lock, so
the slot may be switched when the link drops or an ABORT comes. In
`BLEOTA_DEV_INSTALL` the device takes neither: the install's result
decides, and an installed image restarts without a link to report DONE.
The central reads such an end as unknown (`DEVUPD_E_UNSURE`) and settles it
by the version the device reports once it is back.

## The image head

The device reads the image's first 296 bytes before its first flash write:

| Offset | Bytes | Field | Check |
|---:|---:|---|---|
| 0 | 1 | image magic | 0xE9 |
| 1 | 1 | segment count | 1 or more |
| 12 | 2 | chip ID | the device's (`CONFIG_IDF_FIRMWARE_CHIP_ID`), else `E_KIND` |
| 32 | 4 | `esp_app_desc_t`'s magic | 0xABCD5432 |
| 288 | 4 | the role's magic | `BLEOTA_ROLE_MAGIC`, "rcbR" |
| 292 | 1 | the role's kind | the device's, else `E_KIND` |
| 293 | 1 | the role's protocol | 1 |

The role (`bleota_role_t`) sits in ESP-IDF's `.rodata_custom_desc`
section, which the linker places right after the 256-byte
`esp_app_desc_t` at offset 32. `firmware/devota` defines it with
`DEVOTA_ROLE(kind)`. The two node roles share chip and project name, so
the role is what tells them apart. Any other field failing is `E_IMAGE`.
An image without the role, such as one built before it, is refused.

## Credit window

The device holds `BLEOTA_WINDOW`, 2,048 bytes, of image it has taken and
not yet written. A CREDIT's limit is the device's written bytes plus
2,048, capped at the size. The device notifies a CREDIT when the limit has
moved 512 bytes (`BLEOTA_CREDIT_STEP`) since the last one, when the limit
reaches the size, and when the last byte is taken. A POLL asks for a
CREDIT at once.

A routine CREDIT's next offset lies behind the chunks still in flight, so
the central does not go back to it. The device marks bytes as lost with
code 1 (`BLEOTA_CREDIT_GAP`) in two cases: a chunk past its next offset
came, or a POLL found its next offset under the last limit it gave. The
central polls only after 1,000ms without a CREDIT, so by then bytes
under the limit that have not come are lost. The central goes back to
the next offset on the mark, or when the same next offset and limit come
twice. On a lossless link every byte goes out once, at any latency (the
host suite runs 0ms, 20ms and 40ms).

2,048 bytes are 8 chunks of 240. The panel's NimBLE host has 36 message
buffers (12 of 256 bytes and 24 of 320); 8 chunks in flight hold at most 8
of them, so the IMU's and the Dial's links keep the rest. The device's
ring holds the window while its task writes and erases the flash
(`esp_ota_begin()`'s sequential mode erases each 4,096-byte sector ahead
of its first write).

Not measured: the transfer rate over the panel's ESP32-C6 link, the round
trip from a chunk to its CREDIT, and the erase and write times on the Dial
and the nodes. The window caps the rate at 2,048 bytes per round trip:
25.6kB/s at 80ms. The planned rate is about 6kB/s, an image of 840kB in about
140s; a measurement replaces this figure.

## Security

- No pairing, no encryption and no signature. Integrity is the SHA-256
  the central gives in BEGIN's HASH writes.
- Any central in range can write BEGIN. The physical confirmation on the
  device is what keeps another central from installing an image: nothing
  is erased before it.
- Before its first write the device checks the head: its chip and its
  role. After the SHA-256 it checks the image's format (ESP-IDF's
  `esp_ota_end()`: chip, segments, the image's own appended SHA-256).
- The rollback keeps a new image on trial until it takes a connection with
  its Bluetooth LE stack running (`firmware/devota`). The trial is the
  bootloader's: a device whose bootloader was flashed before rollback was
  enabled has none until it is flashed once over USB.

## Interface

`bleota.h`:

```c
size_t      bleota_begin_encode(uint8_t kind, uint32_t size, const uint16_t ver[3],
                                uint8_t flags, uint8_t *out);
size_t      bleota_hash_encode(const uint8_t sha[32], uint8_t part, uint8_t *out);
size_t      bleota_op_encode(uint8_t op, uint8_t *out);
bool        bleota_ctrl_decode(const uint8_t *b, size_t n, bleota_ctrl_t *out);
size_t      bleota_status_encode(const bleota_status_t *s, uint8_t *out);
bool        bleota_status_decode(const uint8_t *b, size_t n, bleota_status_t *out);
uint16_t    bleota_chunk_bytes(uint16_t mtu);
uint8_t     bleota_head_check(const uint8_t *b, size_t n, uint16_t chip, uint8_t kind);
void        bleota_chunk_header(uint32_t off, uint8_t out[BLEOTA_CHUNK_HDR]);
bool        bleota_chunk_decode(const uint8_t *b, size_t n, uint32_t *off,
                                const uint8_t **data, uint16_t *len);
bool        bleota_dev_init(bleota_dev_t *d, const bleota_dev_cfg_t *cfg,
                            uint8_t *ring, uint32_t ring_size);
void        bleota_dev_ctrl(bleota_dev_t *d, const uint8_t *b, size_t n, uint32_t now);
void        bleota_dev_data(bleota_dev_t *d, const uint8_t *b, size_t n, uint32_t now);
void        bleota_dev_key(bleota_dev_t *d, bool down, uint32_t now);
void        bleota_dev_lost(bleota_dev_t *d);
bleota_do_t bleota_dev_poll(bleota_dev_t *d, uint32_t now);
void        bleota_dev_opened(bleota_dev_t *d, bool ok, uint32_t now);
uint32_t    bleota_dev_take(bleota_dev_t *d, uint8_t *out, uint32_t max);
void        bleota_dev_written(bleota_dev_t *d, bool ok, uint32_t now);
bool        bleota_dev_verify(bleota_dev_t *d, const uint8_t digest[32], uint32_t now);
void        bleota_dev_installed(bleota_dev_t *d, bool ok, uint8_t code, uint32_t now);
void        bleota_dev_closed(bleota_dev_t *d);
bool        bleota_dev_status(bleota_dev_t *d, uint8_t *out);
uint16_t    bleota_dev_secs(const bleota_dev_t *d, uint32_t now);
unsigned    bleota_dev_percent(const bleota_dev_t *d);
```

The device's caller runs `bleota_dev_poll()` on its own task and does what
it returns (`bleota_do_t`): open the slot, write what `bleota_dev_take()`
hands it, finish the SHA-256, install, close or restart. Each step reports
back with its own call. `bleota_dev_status()` gives the next notification.

## Use in another project

```cmake
add_subdirectory(protocols/bleota)
target_link_libraries(my_target PRIVATE rcbench_bleota)
```

Under ESP-IDF (Espressif Internet-of-Things Development Framework) the
folder is a component: name it in `EXTRA_COMPONENT_DIRS` and add `bleota`
to a component's `REQUIRES`. The header compiles from C++.

## Measured and not measured

The host suite runs the codec and the device side, and runs the panel's
engine against the device side. Not measured: any transfer over the air,
the rate, the round trip, and a rollback on a Dial or a node.

## Trademarks

M5Stack is the name of its owner, and Dial and Bluetooth are names of their
owners' products and trademarks. They are used here only to say which
device this code talks to. The project is not affiliated with the owners.
The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_bleota.c` runs every control write and notification at the
ends of each field and one past them, the wrong lengths and ops, the chunk
size at MTU 23, 247 and above, and 200,000 random writes, each in a heap
block of its own length so that a read past it fails under
AddressSanitizer: every decoded one encodes to its own bytes, and the same
writes go to a device side as CTRL and DATA.
`test_bleota.c` also checks the head field by field.
`test_bleota_dev.c` runs the device side: each refusal code, the
confirmation window in both modes at its limits, the HASH timeout, the idle
timeout, a lost chunk, a chunk past the limit, the gap mark, the head
checked before the first write, the SHA-256 compare, an ABORT and a lost
link during the install, a lost link and a new BEGIN after it.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R bleota
```
