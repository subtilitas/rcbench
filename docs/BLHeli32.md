# The BLHeli_32 configuration protocol

A written specification of how a host talks to a BLHeli_32 ESC: the three
transports, the 4-way interface framing, the ESC bootloader command set, the
settings block and its parameter table — with the arithmetic needed to
implement it.

> **Where this came from.** There is no published document. This page was
> written from a static analysis of **BLHeliSuite32 32.10.0.0** (`BLHeliSuite32.exe`,
> a 32-bit Delphi binary, dated 2023-11-12) — its symbol tables, its constant
> tables and the disassembly of about forty routines. No source was available
> and none was copied; what is recorded here is what cannot be owned, a command
> byte and a polynomial and a field offset, the same posture as
> [the OpenYGE specification](OpenYGE.md).

**Marked ° = inferred, not proven.** Those come from a field name, a table
position or ordinary ESC practice rather than from a routine that was read
end to end. Nothing on this page has met an ESC. Section 10 is the list of
what a bench session must settle before anything here is *written* to
hardware.

**Read section 7 before planning any work.** The transports and the command
sets below are complete and implementable. The settings block they carry is
enciphered, and that is the whole difficulty with BLHeli_32.

---

## 1. The shape of the thing

The layers stack, and which of them you need depends on how the ESC is wired:

    parameter table          the 192 bytes of section 8
        |
    settings block           256 bytes of ciphertext at a fixed flash address (7)
        |
    ESC bootloader           the "BLB" command set, on the signal wire (5)
        |
    4-way interface          optional: framing that multiplexes 1..n ESCs (4)
        |
    MSP passthrough          optional: the flight controller carries 4-way (6)

Reading or writing a setting is always the same act underneath: **read 256
bytes of flash, decode, change a byte, encode, erase the page, write it
back**. There is no per-parameter command anywhere in this protocol. The ESC
has no idea what a "motor timing" is; it has a flash page, and the meaning
lives entirely in the host.

Three ways to reach the bootloader, in increasing order of indirection:

| | Transport | Used when |
| --- | --- | --- |
| **A** | Serial adapter straight onto the ESC signal wire, one ESC | A bench with its own half-duplex UART — **this is rcbench's case** |
| **B** | A 4-way interface box (an Arduino, historically) on the serial port, fanning out to several signal wires | Bench programmers with a fixed harness |
| **C** | MSP to a flight controller, which is then told to become a 4-way interface on its motor outputs | A built aircraft, nothing unplugged |

**A** needs sections 5, 7 and 8 only. **B** adds section 4. **C** adds sections
4 and 6 — the flight controller becomes the interface box of **B**, so the
4-way framing is identical and only the way you open the port differs.

---

## 2. Physical layer

| | Direct to ESC (**A**) | 4-way box (**B**) | Via flight controller (**C**) |
| --- | --- | --- | --- |
| Signalling | UART 8N1 | UART 8N1 | UART 8N1 |
| Baud | **19200** | **38400**° | The FC's MSP baud, **115200** typical |
| Wiring | **Single wire**, TX and RX commoned to the ESC signal pin | Ordinary 3-wire to the box | Ordinary, or USB CDC |
| Echo | **The sender hears itself** and must discard exactly as many bytes as it sent | none | none |

Those baud rates are the defaults the tool sets on the port object: 19200 for
the direct bootloader path, 38400 for the 4-way interface path, 115200 for the
MSP path. **The 38400 is marked ° because it is the tool's default rather than
a property of the protocol** — the 4-way interface can be told a different
baud, and over MSP the rate is whatever the flight controller's port runs at.
19200 for the direct path is the same figure BLHeli_S and AM32 use on the same
wire, which is a useful corroboration.

The single-wire echo on path **A** is the same arrangement rcbench already
plans for OpenYGE: a hardware UART with TX and RX tied through a resistor, or a
PIO soft UART. The ESC's signal pin is the only connection; there is no
telemetry lead involved in configuration.

---

## 3. Getting the ESC into its bootloader

The bootloader is entered **at power-up and only at power-up**, and it listens
for a short window before handing control to the running firmware. So the
sequence is always:

1. Open the port.
2. Power the ESC.
3. Send the boot-init sequence of section 5.3 immediately and repeatedly until
   it answers.

There is no "reboot into bootloader" command that works from the running
firmware. `CMD_RUN` goes the other way — it leaves the bootloader for the
application — and once it has run, the ESC must be power-cycled to get back.
This is why every BLHeli tool asks the user to unplug and replug, and it is a
hard constraint on the bench's user flow rather than an inconvenience of the
old tooling.

---

## 4. The 4-way interface protocol

Only needed on paths **B** and **C**. This is the layer BLHeli's own
documentation calls "4way-if"; the tool's internal name for the command
enumeration is `TSerialInterfaceCmd_ID`.

### 4.1 Frame format

Host to interface — **6 bytes of header, then the parameter, then 2 bytes of
CRC**:

| Off | Field | |
| ---: | --- | --- |
| 0 | `escape` | Always **0x2F** (`cmd_Local_Escape`) |
| 1 | `command` | Section 4.2 |
| 2 | `addr_hi` | Address, **big-endian** |
| 3 | `addr_lo` | |
| 4 | `len` | Parameter length, **0 means 256** |
| 5.. | `param[len]` | May be empty; `len` is then 0 and 256 bytes are *not* sent — see below |
| | `crc_hi`, `crc_lo` | **Big-endian**, over every preceding byte |

Interface to host — the same, with **0x2E** (`cmd_Remote_Escape`) as the escape
and **one extra byte before the CRC**:

| Off | Field | |
| ---: | --- | --- |
| 0 | `escape` | Always **0x2E** |
| 1..4 | `command`, `addr_hi`, `addr_lo`, `len` | Echoed |
| 5.. | `param[len]` | |
| | `ack` | Section 4.3 |
| | `crc_hi`, `crc_lo` | **Big-endian**, over every byte from `escape` up to and including `ack` |

**The `len == 0` ambiguity is resolved by the command, not by the frame.** The
tool encodes a length of 256 as 0 only for `_DeviceWrite`, `_DeviceWriteEEprom`
and `_DeviceVerify`; for every other command an empty parameter is a genuine 0.
On the receive side it expects 256 bytes of payload when `len == 0` and the
command was `_DeviceRead` (0x3A) or `_DeviceReadEEprom` (0x3D). A decoder must
therefore know which command it sent to know how many bytes to expect — this
protocol is not self-delimiting.

A reply is accepted only when **the CRC matches and `ack == 0x00`** and the
echoed command equals the one sent. A CRC mismatch is reported as
`ACK_ERROR_COM_CRC` rather than as a transport error, which is worth copying:
it keeps one error channel.

### 4.2 CRC — CRC-16/XMODEM

| Poly | Init | Reflected | XorOut | Check over `"123456789"` |
| --- | --- | --- | --- | --- |
| 0x1021 | 0x0000 | none | none | 0x31C3 |

The routine, byte at a time, MSB first:

    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x8000) ? (uint16_t)(crc << 1) ^ 0x1021 : (uint16_t)(crc << 1);

**This is the same CRC as OpenYGE's** — same polynomial, same zero seed — so
rcbench's existing `link_crc()` with a seed of 0 covers this layer with no new
code. Note that it is *not* the CRC used one layer down in section 5.4; the two
links have different polynomials and get confused easily.

### 4.3 Command bytes

The wire byte is **the enumeration ordinal plus 0x2D**, and any byte outside
0x2E–0x40 is rejected before it is sent.

| Byte | Name | Parameter | Reply |
| ---: | --- | --- | --- |
| 0x2E | `cmd_Remote_Escape` | — | Frame marker, interface → host |
| 0x2F | `cmd_Local_Escape` | — | Frame marker, host → interface |
| 0x30 | `cmd_InterfaceTestAlive` | none | none |
| 0x31 | `cmd_ProtocolGetVersion` | none | 1 byte, protocol version |
| 0x32 | `cmd_InterfaceGetName` | none | Name string |
| 0x33 | `cmd_InterfaceGetVersion` | none | 2 bytes; version = `hi * 100 + lo` |
| 0x34 | `cmd_InterfaceExit` | none | Leaves 4-way mode |
| 0x35 | `cmd_DeviceReset` | 1 byte: ESC index, **0-based** | |
| 0x36 | `cmd_DeviceGetID` | | Deprecated, unused by the tool |
| 0x37 | `cmd_DeviceInitFlash` | 1 byte: ESC index, **0-based** | **4 bytes**, section 4.4 |
| 0x38 | `cmd_DeviceEraseAll` | | |
| 0x39 | `cmd_DevicePageErase` | 1 byte: **page number**, `(address & 0xFFFF) >> 10` | |
| 0x3A | `cmd_DeviceRead` | 1 byte: byte count, 0 = 256 | The bytes |
| 0x3B | `cmd_DeviceWrite` | The bytes, up to 256 | |
| 0x3C | `cmd_DeviceC2CK_LOW` | | SiLabs only |
| 0x3D | `cmd_DeviceReadEEprom` | 1 byte: byte count | The bytes |
| 0x3E | `cmd_DeviceWriteEEprom` | The bytes | |
| 0x3F | `cmd_InterfaceSetMode` | 1 byte: mode, section 4.5 | |
| 0x40 | `cmd_DeviceVerify` | The bytes, up to 256 | |

The page number for `cmd_DevicePageErase` is a **1 KiB page index derived from
the low 16 bits of the address**, which is right for every BLHeli_32 MCU in
section 9 but is a property of those parts rather than of the protocol.

### 4.4 What `cmd_DeviceInitFlash` returns

Four bytes. This is the ESC identification step, and everything downstream
branches on it:

| Off | | |
| ---: | --- | --- |
| 0–1 | MCU signature, **little-endian** — `b0 + (b1 << 8)` | Looked up in the table of section 9 |
| 2 | Bootloader revision | **Must be ≥ 0x32**; the tool rejects the device otherwise |
| 3 | ° Second signature or page count | Stored, not obviously used |

A bootloader revision in **0x65–0x7A** together with a small flash size is what
the tool takes as "this is an ARM part running BLHeli_32" as opposed to SiLabs
or Atmel. Marked ° as a heuristic — it is a range check in one routine, not a
documented contract.

### 4.5 Interface modes

`cmd_InterfaceSetMode` selects which ESC family's bootloader the interface
should speak on the signal wire:

| Value | Mode | |
| ---: | --- | --- |
| 0 | `imC2` | SiLabs C2 debug interface |
| 1 | `imSIL_BLB` | SiLabs BLHeli bootloader |
| 2 | `imATM_BLB` | Atmel BLHeli bootloader |
| 3 | `imSK` | SimonK bootloader |
| 4 | `imBLHeli32_BLB` | **BLHeli_32, ARM** |

### 4.6 ACK codes

Returned in the reply's `ack` byte. The first eleven come from the wire; the
last five are the tool's own transport failures occupying the same enumeration,
which is why the numbering has a gap.

| | Name | |
| ---: | --- | --- |
| 0x00 | `ACK_OK` | The only success |
| 0x01 | `ACK_I_UNKNOWN_ERROR` | |
| 0x02 | `ACK_I_INVALID_CMD` | |
| 0x03 | `ACK_I_INVALID_CRC` | Interface rejected *our* CRC |
| 0x04 | `ACK_I_VERIFY_ERROR` | |
| 0x05 | `ACK_D_INVALID_COMMAND` | The *device* rejected it |
| 0x06 | `ACK_D_COMMAND_FAILED` | |
| 0x07 | `ACK_D_UNKNOWN_ERROR` | |
| 0x08 | `ACK_I_INVALID_CHANNEL` | No such ESC index |
| 0x09 | `ACK_I_INVALID_PARAM` | |
| 0x0A | `ACK_I_INVALID_PACKAGE` | |
| 0x0F | `ACK_D_GENERAL_ERROR` | |
| 0x10–0x14 | `ACK_ERROR_COM_{READ,WRITE,ECHO,TIMEOUT,CRC}` | Host-side, not from the wire |

---

## 5. The ESC bootloader command set

This is the layer that actually touches flash, and on path **A** it is the only
layer. On paths **B** and **C** the interface box speaks it on your behalf and
you never see these bytes — which is the point of the 4-way layer.

### 5.1 Frame format

There is no framing. A command is **its bytes followed by a 16-bit CRC,
little-endian**, and the reply is either data followed by an ACK byte, or an
ACK byte alone. The sender hears its own transmission on the single wire and
discards it.

### 5.2 Commands

| Byte | Name | Payload after the command byte | Reply |
| ---: | --- | --- | --- |
| 0x00 | `CMD_RUN` | 1 byte, 0 | ACK — leaves the bootloader |
| 0x01 | `CMD_PROG_FLASH` | 1 byte, repeat count | ACK |
| 0x02 | `CMD_ERASE_FLASH` | 1 byte, page count | ACK |
| 0x03 | `CMD_READ_FLASH_ARM` | 1 byte, byte count (0 = 256) | data + CRC + ACK |
| 0x04 | `CMD_VERIFY_FLASH_ARM` | 1 byte, repeat count | ACK |
| 0x05 | `CMD_PROG_EEPROM` | | Not used on ARM |
| 0x06 | `CMD_READ_SRAM` | | |
| 0x07 | `CMD_READ_FLASH_ATM` | | Atmel parts only |
| 0x0A | `CMD_BOOTINIT` | | **No ACK** — the boot message comes back instead |
| 0x0B | `CMD_BOOTSIGN` | | Section 5.3 |
| 0xFC | `CMD_SET_BAUD_RATE` | | |
| 0xFD | `CMD_KEEP_ALIVE` | | ACK |
| 0xFE | `CMD_SET_BUFFER` | 0x00, count_hi, count_lo, then the data | The data, then ACK |
| 0xFF | `CMD_SET_ADDRESS` | `addr>>16`, `addr>>8`, `addr` | ACK |

**0x03 and 0x04 are family-dependent.** On SiLabs the same bytes are
`CMD_READ_FLASH_SIL` and `CMD_READ_EEPROM`; on Atmel, `CMD_VERIFY_FLASH` and
`CMD_READ_EEPROM`. The command byte does not tell you which — the device family
from section 4.4 does. For BLHeli_32 the ARM meanings above are the right ones.

Commands 0x00, 0x01, 0x02, 0x04, 0xFC, 0xFD and 0xFF return a **plain ACK
byte** and nothing else. 0x03 returns data first. 0xFE echoes the buffer. 0x0A
returns the boot message and no ACK at all.

### 5.3 The boot handshake

Connecting is three steps, and the middle one is the odd one:

1. Send **`00 00`** — two zero bytes, as a line wake-up.
2. Send the boot-init sequence: **eight 0x00 bytes, then 0x0D, then the boot
   signature, then the signature's CRC low byte and high byte**. With the
   signature `"BLHeli"` the trailing CRC bytes are **0xF4 0x7D**, which is a
   useful test vector for section 5.4 in its own right. `FLastCMD` is set to
   `CMD_BOOTSIGN` (0x0B) for this exchange.
3. Read the boot message and check the ACK.

The count of leading zeros is padding to let the ESC's receiver find the byte
boundary from a cold start; **eight is what this tool sends** and other tools
have used twelve, so treat it as a minimum rather than a constant.

### 5.4 CRC — CRC-16/ARC, and not the one above

| Poly | Init | Reflected | XorOut | Check over `"123456789"` |
| --- | --- | --- | --- | --- |
| **0xA001** (reflected 0x8005) | 0x0000 | in and out | none | 0xBB3D |

The routine, byte at a time, LSB first:

    for (int i = 0; i < 8; i++) {
        crc = ((crc ^ byte) & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        byte >>= 1;
    }

Sent **low byte first**, which is the opposite order to the 4-way layer's.

> **Two links, two CRCs, opposite byte orders.** The 4-way layer is
> CRC-16/XMODEM appended big-endian; the bootloader layer is CRC-16/ARC
> appended little-endian. rcbench's `link_crc()` covers the first and not the
> second, so path **A** needs one new eight-line routine and its own test
> vector. `crc16_arc("BLHeli") == 0x7DF4` is the vector, and it is worth
> keeping because it comes out of the protocol rather than out of a table.

### 5.5 Reading the settings block, path A

    CMD_SET_ADDRESS   0x7C00           (or 0xF800 on a 64 KiB part)
    CMD_READ_FLASH    256

and writing it back:

    CMD_ERASE_FLASH   1 page
    CMD_SET_ADDRESS   0x7C00
    CMD_SET_BUFFER    256 bytes
    CMD_PROG_FLASH
    CMD_SET_ADDRESS   0x7C00
    CMD_VERIFY_FLASH  256

**`CMD_SET_ADDRESS` remaps addresses inside the settings region.** For an
address `a` with `start <= a < end` the tool sends `start + ((a - start) / 3) * 4`
instead of `a`, which is the 3-to-4 byte expansion of section 7 showing through
into the addressing. An implementation that reads the whole block from `start`
never sees this; one that seeks into the middle of the block must reproduce it.

---

## 6. MSP passthrough

Path **C** only. The flight controller is asked to stop being a flight
controller and become a 4-way interface on its motor outputs.

MSP framing itself is Betaflight's and is not restated here. The opcodes the
tool uses, confirmed against the binary:

| | Opcode | |
| ---: | --- | --- |
| 1 | `MSP_API_VERSION` | |
| 2 | `MSP_FC_VARIANT` | |
| 3 | `MSP_FC_VERSION` | |
| 4 | `MSP_BOARD_INFO` | |
| 5 | `MSP_BUILD_INFO` | |
| 36 | `MSP_FEATURE_CONFIG` | |
| 68 | `MSP_REBOOT` | |
| 70, 71 | `MSP_DATAFLASH_SUMMARY`, `MSP_DATAFLASH_READ` | |
| 90 | `MSP_ADVANCED_CONFIG` | Motor protocol, `motorPwmProtocol` |
| 101 | `MSP_STATUS` | Armed state |
| 104 | `MSP_MOTOR` | |
| 114 | `MSP_MISC` | |
| 116, 119 | `MSP_BOXNAMES`, `MSP_BOXIDS` | |
| 124 | `MSP_3D` | |
| 125 | `MSP_RC_DEADBAND` | |
| 131 | `MSP_MOTOR_CONFIG` | |
| 134 | `MSP_ESC_SENSOR_DATA` | |
| 139 | `MSP_MOTOR_TELEMETRY` | |
| 214 | `MSP_SET_MOTOR` | Also used to command the bootloader entry |
| **245** | **`MSP_SET_4WAY_IF`** | **The one that matters** |

`MSP_SET_4WAY_IF` is sent with **no payload** and replies with **one byte: the
number of ESCs** the interface will address. From that moment the same serial
port carries 4-way frames as in section 4 and nothing else; `cmd_InterfaceExit`
(0x34) hands the port back to MSP.

**The tool checks that motors are stopped and warns if they are not** before
initialising 4-way. That check belongs in any reimplementation: the flight
controller stops running its mixer while it is being an interface box.

---

## 7. The settings block, and why BLHeli_32 is not like the others

### 7.1 Where it is

| Flash | Settings region | Size read |
| --- | --- | ---: |
| 32 KiB | **0x7C00 – 0x7FFF** | 256 bytes |
| 64 KiB | **0xF800 – 0xFEFF** | 256 bytes |

The tool chooses by flash size from the MCU table of section 9 — `Is_64k` is
literally `flash == 0x10000` — and, when a read does not produce a plausible
block, tries the other address and then two key variants, four attempts in all.
A block is judged plausible by its layout revision.

### 7.2 It is enciphered

**256 bytes are read from flash and 192 bytes of parameters come out.** The
ratio is exact and it is not a coincidence: the codec expands **3 plaintext
bytes into 4 stored bytes** across the settings region, and 0xC0 × 4/3 = 0x100.

The cipher underneath is **XTEA** — 64-bit block, 128-bit key, round count from
a variable — applied to every 8-byte block of the region. Two 16-byte key
tables sit in the binary, selected by which region the address falls in:
the settings region has one, the flash above it another, and the application
flash below gets a third derived at runtime. The `SUCCESS` ACK from the
bootloader carries flags saying which of them the device has enabled:

| Bit | Flag | |
| ---: | --- | --- |
| 0 | `FLAG_Crypt` | |
| 1 | `FLAG_CryptFLASH` | Firmware image is encrypted |
| 2 | `FLAG_CryptE2` | Settings block is encrypted |
| 3 | `FLAG_Versioning` | |

so an ACK of `0x30` is a plain success and `0x36` is a success from a device
with encrypted flash, encrypted settings and versioning. Any value in
**0x30–0x3F is success**; only the low nibble varies.

There is an activation and licensing mechanism alongside it — the tool tracks
an `ActivationStatus` per ESC with states for *activated*, *not activated* and
*activation failed*, and refuses some operations on some devices.

### 7.3 What that means for a bench

Plainly: **reading or writing BLHeli_32 settings requires the vendor's key**,
which exists only inside BLHeliSuite32. This page deliberately does not carry
it. Extracting and shipping a key to decrypt a commercial product's protected
configuration is a different act from writing an interoperable protocol
implementation, it carries legal weight in several jurisdictions, and it is the
owner's decision rather than a technical detail — so it is recorded here as the
blocker it is and left there.

Everything *above* the settings block is unencumbered. The transports of
sections 4–6, the bootloader command set of section 5, both CRCs and the
identification of section 4.4 are ordinary protocol, and an implementation of
them can connect to a BLHeli_32 ESC, identify it, read its 256 bytes and tell
the user what it is. It just cannot tell the user what the bytes mean.

**Three things still work without any of it**, and they are what a bench should
lean on:

- **DShot commands** — direction, direction reversal, 3D mode, beacon and save
  settings are all standard DShot special commands, sent on the signal wire by
  the running firmware. They are open, they are already the way a flight
  controller configures an ESC, and they cover the settings a user changes at
  the bench most often.
- **Bidirectional DShot and ESC telemetry** — rpm, voltage, current and
  temperature come back without the configuration path being involved at all.
- **Identification** — connect, `cmd_DeviceInitFlash`, read the MCU signature
  and the bootloader revision, and the bench can name the ESC and its MCU
  correctly even when it cannot show a parameter.

---

## 8. The parameter table

This is the layout of the **192 decoded bytes**, and it is recorded in full
because it is the part that is most expensive to rediscover and least likely to
change. It is useful today for the four fields at fixed offsets that identify
the block, and it is what section 7.3 unlocks if the key question is ever
answered.

### 8.1 Fixed fields

| Off | Size | Field | |
| ---: | ---: | --- | --- |
| 0x00 | 1 | Firmware main revision | Valid range 0x20–0x40 |
| 0x01 | 1 | Firmware sub revision | 0–9 |
| 0x02 | 1 | **EEPROM layout revision** | 0x40–0x80. Gates which parameters exist |
| 0x40 | 32 | ESC layout name | ASCII, space padded |
| 0x60 | 32 | ESC MCU name | ASCII |
| 0x80 | 16 | User ESC name | ASCII, space padded |
| 0x90 | 48 | Startup music note array | |

The whole block is 0xFF-filled before the fields are written, so an erased or
short block reads as all-ones and the tool refuses anything under 0xC0 bytes.

### 8.2 Programmable parameters

Sizes are 1 byte unless stated; 2-byte values are **little-endian, low byte at
the offset**.

| Off | Size | Parameter | Min | Max | |
| ---: | ---: | --- | ---: | ---: | --- |
| 0x03 | 1 | Motor direction | 1 | 4 or **6** | 6 when layout revision ≥ 0x2A |
| 0x04 | 1 | Rampup / startup power | 3 | 150 | Percent |
| 0x05 | 1 | PWM frequency | \* | \* | kHz |
| 0x06 | 1 | Motor timing | 0 | 31 | Steps of **15/16 of a degree** |
| 0x07 | 1 | Demag compensation | 1 | 3 or **4** | 4 (*Very High*) when layout revision ≥ 0x2F |
| 0x08 | **2** | Minimum throttle | 900 | 2100 | µs |
| 0x0A | **2** | Centre throttle | 900 | 2100 | µs, bidirectional only |
| 0x0C | **2** | Maximum throttle | 900 | 2100 | µs |
| 0x0E | 1 | Throttle calibration enable | 0 | 1 | |
| 0x0F | 1 | Temperature protection | 0 | 255 | °C, 0 = off° |
| 0x10 | 1 | Low voltage protection | 0 | 16 | **Stored value is the setting + 24 when non-zero** |
| 0x11 | 1 | Current protection | 0 | 200 | A |
| 0x12 | 1 | Low RPM power protect | 0 | 1 or **2** | 2 (*adaptive*) when layout revision ≥ 0x2F |
| 0x13 | 1 | Brake on stop | 0 | 1 or **100** | 0–100 as a brake *force* when the ESC declares it programmable, else a flag |
| 0x14 | 1 | Startup beep volume | 0 | 254 | |
| 0x15 | 1 | Beacon / signal volume | 0 | 255 | |
| 0x16 | **2** | Beacon delay | 0 | 3600 | ° **Seconds** — the range is exact, the unit is read from 3600 being an hour |
| 0x18 | 1 | LED control | 0 | 255 | Bitfield, 2 bits per LED |
| 0x19 | 1 | Maximum acceleration | 0 | 255 | 0.1 %/ms; 255 = unlimited° |
| 0x1A | 1 | Non-damped mode | 0 | 1 | |
| 0x1B | 1 | Current sense calibration | 1 | 254 | |
| 0x1C | 1 | Music note config | 0 | 255 | |
| 0x1D | 1 | Sine modulation mode | 0 | 1 | |
| 0x1E | 1 | Auto telemetry | 0 | 1 | |
| 0x1F | 1 | Stall protection | 0 | 1 | |
| 0x20 | 1 | SBUS channel | 0 | 17 | |
| 0x21 | 1 | S.PORT physical ID | 0 | 28 | |
| 0x05 | 1 | PWM frequency **low** | \* | \* | **The same byte as PWM frequency** |
| 0x22 | 1 | PWM frequency **high** | \* | \* | One step above the maximum from layout revision 0x30, and that step selects *by RPM* |
| 0x23 | 1 | (unused) | | | |
| 0x2F | 1 | Flash counter | | | Times the ESC has been flashed |

\* The three PWM frequency fields are bounded by the ESC's own capability bytes
at 0x36 and 0x37 — but only when those are credible, meaning both are non-zero,
neither is 0xFF, and the maximum exceeds the minimum. When they are not, the
bounds fall back to **16–48 kHz**. PWM frequency *high* then takes one extra
step above that maximum from layout revision 0x30 onward, and selecting it is
what puts the ESC into *by RPM* mode.

**Motor direction** — 1 *Normal*, 2 *Reversed*, 3 *Bidirectional 3D*,
4 *Bidirectional 3D Rev.*, 5 *Bidirectional Soft*, 6 *Bidirectional Soft Rev.*
The soft modes exist from layout revision 0x2A. **Demag compensation** —
1 *Off*, 2 *Low*, 3 *High*, 4 *Very High*.

**Low voltage protection is the trap.** The stored byte is the user's setting
plus 24 whenever the setting is non-zero, and zero stays zero. Writing the
user's number straight into the byte sets a cut-off 24 units low, which on a
protection parameter is the wrong direction to be wrong in.

**Two parameters share offset 0x05.** "PWM frequency" and "PWM frequency low"
are the same byte under two names, because variable PWM frequency (from
firmware 32.8) reused the fixed-frequency field as the lower bound and added
0x22 as the upper. A parameter table that treats them as independent will
write one over the other.

### 8.3 Hardware capability bytes

Written by the ESC's manufacturer, read-only to the user, and they are what
bound the starred ranges above.

| Off | | |
| ---: | --- | --- |
| 0x30 | Voltage sense capable | |
| 0x31 | Current sense capable | |
| 0x32–0x35 | LED capable, 0–3 | One byte per LED |
| 0x36 | PWM frequency minimum | kHz |
| 0x37 | PWM frequency maximum | kHz |
| 0x3E | S.PORT capable | |
| 0x3F | Non-damped capable | |

---

## 9. The MCU table

Twelve ARM parts, keyed by the signature word from `cmd_DeviceInitFlash`. Flash
size is what selects the settings address of section 7.1, so this table is not
optional.

| Signature | MCU | Flash | Page | RAM |
| ---: | --- | ---: | ---: | ---: |
| 0x0D06 | AT32F413 | 64 K | 1 K | 8 K |
| 0x0F06 | MM32SPIN05 | 64 K | 1 K | 8 K |
| 0x1506 | AT32F421 | 32 K | 1 K | 8 K |
| 0x1F06 | STM32F031x6 | 32 K | 1 K | 8 K |
| 0x2B06 | STM32L431x6 | 64 K | 2 K | 8 K |
| 0x2F06 | GD32E230x6 | 32 K | 1 K | 8 K |
| 0x3306 | STM32F051x6 | 32 K | 1 K | 8 K |
| 0x3406 | GD32F150x6 | 32 K | 1 K | 8 K |
| 0x3506 | GD32F350x6 | 32 K | 1 K | 8 K |
| 0x3606 | STM32G071x6 | 32 K | 1 K | 8 K |
| 0x3706 | CKS32F051x6 | 32 K | 1 K | 8 K |
| 0x4706 | AT32F415 | 64 K | 2 K | 8 K |

Every one of them carries the signature `0x06` in its low byte, and the
distinguishing part is the high byte — so a device whose reply's low byte is
not 0x06 is not one of these, whatever else it may be.

---

## 10. What is uncertain, and what a bench session would settle

Nothing here has met an ESC. In rough order of how much a wrong answer costs:

1. **Low voltage protection's +24 offset.** Read from the write path; the read
   path was not traced far enough to confirm the inverse. Getting this backwards
   sets a protection threshold wrong, so it wants proving in both directions
   before anything is written.
2. **The 38400 baud on the 4-way path.** It is the tool's default, not a
   protocol constant. Path **A**'s 19200 is corroborated by the other
   firmwares; this one is not corroborated by anything.
3. **The eight zero bytes of boot-init.** Padding for byte alignment. Other
   tools have used twelve. A bench should send the larger number and find the
   floor experimentally if it ever matters.
4. **Whether the settings block is enciphered on every revision or only recent
   ones.** The tool applies the transform unconditionally to that address
   region and tries two keys, which is consistent with either "always, and the
   older key is a different key" or "always, and old firmware used a null one".
   The `FLAG_CryptE2` bit in the ACK reports it per-device and is the cheap
   answer: connect to one old ESC and one new one and read the ACK.
5. **`cmd_DeviceInitFlash` byte 3.** Stored and not obviously consumed.
6. **The maximum-acceleration sentinel.** 255 as "unlimited" is inferred from
   the manual's prose, not from the tool.
7. **The `TESCDefault` file.** `BLHeli32DefaultsX.cfg` ships beside the
   executable: an 18-byte header (a hash, then a 16-bit record count) followed
   by 222 records of `<len byte><name><5 × u16 LE>`, the words being default
   PWM frequency, PWM frequency low, PWM frequency high, motor timing and
   temperature protection for that ESC layout, with **0xFFFF meaning "no
   default"**. The observed values are consistent — 16/24/32/48/96/128 kHz for
   the frequencies, 120/125/140 for temperature — except that temperature also
   takes 0x0100, one past its byte range, which is ° read as a disabled
   sentinel. Interesting mostly as a source of per-ESC sanity limits.

---

## 11. Where this lands in rcbench

The README lists BLHeli_32 under *what is deliberately not built*, with the
reason "whose firmware and update servers are gone". **That reason was right
and is now the smaller half of the answer**, so it is worth restating
precisely:

- **The transport is not the obstacle.** Sections 4–6 are complete enough to
  implement from this page, and path **A** — a half-duplex UART at 19200 on the
  signal wire — is the same physical arrangement the bench already needs for
  BLHeli_S and AM32. The marginal cost of reaching a BLHeli_32 bootloader,
  given a BLHeli_S implementation, is one CRC routine and one command table.
- **The settings are the obstacle**, and section 7.3 says why. A parameter
  screen for BLHeli_32 needs a key this project should not be shipping.
- **So the useful subset is identification plus DShot.** Connect, read the MCU
  signature, name the ESC and its bootloader revision, and offer the DShot
  special commands for direction, 3D and beacon. That is a real feature, it is
  honest about what it cannot do, and it needs none of section 7. It also fits
  the programmer screen's existing shape: a protocol whose parameter list is
  short is still a protocol the screen can draw.
- **The `k_blheli32` table in `shared/ui/programmer_screen.c` is a placeholder
  and section 8.2 is the real thing.** Where they can be compared the
  placeholder does well — motor timing 0–31 and current limit 0–200 are exactly
  right, and startup power 25–150 is close to the true 3–150. Low voltage cut
  is the one that is actually wrong: it is drawn as 2.8–3.8 V per cell and the
  ESC's field is a 0–16 index with an offset, not volts. If the screen keeps a
  BLHeli_32 column, that row should change or go.

The licensing rule holds here as it does everywhere: **this page is the
specification, and any implementation is written from it.** Nothing was taken
from BLHeliSuite32 but facts — a command byte, a polynomial, a field offset —
and the binary itself stays out of the repository and out of its history.
