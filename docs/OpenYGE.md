# The OpenYGE ESC telemetry and parameter protocol

<sub>**English** · [Deutsch](OpenYGE-de.md)</sub>

This specification describes the frame format, telemetry payload,
parameter access and status encoding, including the calculations
needed for an implementation.

> Implementation work takes place in a separate repository. This page is
> the reference specification intended for review by YGE. The codec under
> `protocols/openyge/` is built and tested but not connected to the firmware.

**° marks an inferred meaning.** Those entries come from a field name and
ordinary ESC (electronic speed controller) practice rather than from an
authoritative source. The Conflict column of section 5.3 names where two
sources disagree. Section 8 lists what needs a measurement or an answer.
Together, °, the conflicts and section 8 are everything on this page that
is uncertain.

## 1. Physical layer

| | |
| --- | --- |
| Signalling | UART (universal asynchronous receiver-transmitter), 115200 baud, 8N1 |
| Byte order | little-endian throughout |
| Topology | half duplex, one master and up to 127 addressable ESCs |
| Rate | about 20Hz telemetry |

The master receives an echo of its own transmission. The sender discards
as many received bytes as it sent. TX and RX share a single wire.

The protocol needs a UART of its own on the IOmcu: a hardware UART with
TX and RX tied through a resistor and the echo discarded, or a PIO
(programmable input/output) soft UART. It cannot share the panel link, which is
CAN (Controller Area Network); see [The link](Link.md).

Timing at 115200 8N1, 86.81µs per byte:

| | |
| --- | ---: |
| 34-byte telemetry reply | 2.95ms |
| 12-byte request | 1.04ms |
| One poll, both directions | 3.99ms plus the ESC's turnaround |

That is 8% of the line at a 50ms cycle.

---

## 2. Frame structure

Every frame is `header · payload · CRC`. `frame_length` counts the whole frame,
header and CRC included.

### 2.1 Header, protocol version 3 and later: 6 bytes

| Off | Field | |
| ---: | --- | --- |
| 0 | `sync` | always 0xA5 |
| 1 | `version` | 3 for the current protocol |
| 2 | `frame_type` | section 3 |
| 3 | `frame_length` | total, header and CRC included |
| 4 | `seq` | the master increments per request; the ESC echoes it back |
| 5 | `device` | bit 7 (0x80) set: sender is the master. Bits 0–6: ESC address 0x01–0x7F; 0x00 addresses all ESCs |

### 2.2 Legacy header, version below 3: 4 bytes

`sync`, `version`, `frame_type`, `frame_length` only. With no sequence number
and no address, a pre-v3 ESC cannot be polled or addressed: it broadcasts
telemetry. Parameter access does not exist below v3.

An implementation must read `version` before it knows where the payload starts.

### 2.3 CRC: 2 bytes, little-endian, at the end

Over every byte from `sync` up to but not including the CRC (cyclic
redundancy check).

The algorithm is CRC-16/XMODEM, not CRC-16/CCITT-FALSE. Same polynomial,
different seed:

| | Poly | Init | Reflect | XorOut | Check over `"123456789"` |
| --- | --- | --- | --- | --- | --- |
| OpenYGE | 0x1021 | 0x0000 | none | none | 0x31C3 |
| rcbench link CRC (`link_crc`) | 0x1021 | 0xFFFF | none | none | 0x29B1 |

`protocols/openyge` provides its own routine, `openyge_crc()`, without
depending on the link module. The host suite checks it against the
check value and against `link_crc()` with seed 0 for every frame length
from 0 to 140 bytes.

---

## 3. Frame types

| Value | Name | From | |
| ---: | --- | --- | --- |
| 0x00 | `TELE_AUTO` | ESC | unsolicited telemetry: a pre-v3 ESC always, a v3 ESC until the master takes over |
| 0x02 | `TELE_RESP` | ESC | telemetry answering a request |
| 0x03 | `TELE_REQ` | master | request telemetry |
| 0x04 | `WRITE_PARAM_RESP` | ESC | acknowledges a parameter write; carries telemetry too |
| 0x05 | `WRITE_PARAM_REQ` | master | write one parameter |

Requests carry the 4-byte control payload of section 5.2. Every ESC-to-master
frame carries the full telemetry payload of section 4, the parameter-write
acknowledgement included.

The source material states that bit 7 of `frame_type` marks a write (0x81 for a
parameter update). No listed value uses it. Treat `frame_type` as a plain
enumeration and reject anything unlisted; whether bit 7 is reserved is a
question for YGE.

---

## 4. Telemetry payload: 26 bytes

Offsets within the payload and absolute in a version 3 frame. A v3 telemetry
frame is 34 bytes; a legacy one is 32.

| Payload | v3 abs | Size | Field | Unit | To SI |
| ---: | ---: | ---: | --- | --- | --- |
| 0 | 6 | 1 | `reserved` | | |
| 1 | 7 | u8 | `temperature` | °C + 40 | `°C = v − 40`, −40…215 |
| 2 | 8 | u16 | `voltage` | 10mV | `V = v / 100` |
| 4 | 10 | u16 | `current` | 10mA | `A = v / 100` |
| 6 | 12 | u16 | `consumption` | mAh | as is |
| 8 | 14 | u16 | `rpm` | 10eRPM (electrical revolutions per minute) | `eRPM = v × 10`, see below |
| 10 | 16 | i8 | `pwm` | % | output duty, signed |
| 11 | 17 | i8 | `throttle` | % | input setpoint, signed |
| 12 | 18 | u16 | `bec_voltage` | mV | as is |
| 14 | 20 | u16 | `bec_current` | mA | as is |
| 16 | 22 | u8 | `bec_temp` | °C + 40 | `°C = v − 40` |
| 17 | 23 | u8 | `status1` | | section 6 |
| 18 | 24 | u8 | `cap_temp` | °C + 40 | capacitor pack |
| 19 | 25 | u8 | `aux_temp` | °C + 40 | |
| 20 | 26 | u8 | `status2` | | undocumented; carry, do not interpret |
| 21 | 27 | u8 | `reserved1` | | possibly a consumption high byte above 65Ah ° |
| 22 | 28 | u16 | `pidx` | | parameter index, section 5 |
| 24 | 30 | u16 | `pdata` | | parameter value, section 5 |

Every 16-bit field lands on an even offset in both header lengths. Do not rely
on it: parse byte by byte. Casting a buffer to a struct assumes little-endian,
assumes no padding, and performs unaligned reads that are undefined behaviour
on strict-alignment targets.

### `rpm` scale

The field is described as "0.1eRPM" while the reference code multiplies by
ten. Multiplying is the assumption here: 65535 × 10 = 655,350eRPM is a
plausible ceiling, 6,553 is not. Measure it (T1, section 8).

Mechanical RPM (revolutions per minute) needs the motor's poles, parameter
20. One source reads index 20 as pole pairs, the other as poles (section
5.3, conflict at index 20):

    index 20 = pole pairs:  motor RPM = eRPM / pole pairs
    index 20 = poles:       motor RPM = eRPM / (poles / 2)
    head RPM = motor RPM × pinion teeth / main gear teeth

**Current limitation.** `openyge_motor_rpm()` and `openyge_head_rpm()` in
`protocols/openyge/` read index 20 as poles. On an ESC whose index 20 holds
pole pairs, both return twice the true RPM for 2 to 100 pole pairs, and
return false for 1 pole pair, because a value below 2 is refused. Step H5
in section 8 settles which reading holds.

---

## 5. Parameters

The protocol reads and writes the ESC's configuration.

### 5.1 Delivery: one parameter per frame

Every telemetry frame carries one `(pidx, pdata)` pair. The ESC cycles
through its table, and the master caches each pair. Parameter 0 gives
the parameter count. The table is complete only after the master has
received every index `0 … count−1`.

At 20Hz and 32 parameters that takes about 1.6s. Show it as progress; never
present a partial table as the ESC's settings.

- Stop accepting cached values while writes are outstanding, or a frame already
  in flight overwrites the new value.
- Withdraw the whole table when a write is scheduled and republish only when
  every index has been re-read.

The cache in `protocols/openyge/` is capped at 64 parameters by a 64-bit bitmap;
current firmware has 32.

### 5.2 Control payload: 4 bytes

Carried by `TELE_REQ` and `WRITE_PARAM_REQ`:

| Off | Size | Field |
| ---: | ---: | --- |
| 0 | u16 | `index` |
| 2 | u16 | `param`: value to write |

A plain telemetry request sends both as zero. A request frame is 12 bytes.
Writes are one parameter per frame, each with its own sequence number. There is
no page write; a "save" is a queue of single writes.

### 5.3 Parameter table

Index i of the table is parameter i on the wire. The count, index 0, is
32 from firmware v1.03503. Rotorflight documents parameter access for ESC
firmware V1.03547 and later.

Sources, by their key in the Source column:

| Key | Source |
| --- | --- |
| OY | the OpenYGE material this page is written from; the reference of section 7 |
| RF-L | Rotorflight Lua scripts (rotorflight-lua-scripts): `mspEscYge.lua` and `esc_yge.lua` |
| RF-F | Rotorflight firmware (rotorflight-firmware): `src/main/sensors/esc_sensor.c` |
| RF-D | Rotorflight documentation, ESC Telemetry |
| M | YGE manuals: Saphir 125/155 V2, Aureus 105/135 V2, 35–135 LVT, Opto 255, ProgCard III |

The Rotorflight sources are under the GPL-3.0 (GNU General Public License
version 3.0). This page takes numbers, names and meanings from them, no
code.

RF-F hands the cached table to the Lua scripts as a 2-byte header and then
parameter i as a little-endian u16 in slot i. RF-L reads the fields in that
order, so its field order gives the index. RF-L reads 12–13, 14–15, 28–29
and 30–31 as one u32 each, and splits 26 into a low and a high byte.

The Access column:

| Access | Meaning |
| --- | --- |
| identity | read-only, never written |
| telemetry | read-only; a change switches the ESC to another telemetry format and ends this wire |
| raw | meaning unknown or named by one source only; shown as a number, read-only, written back as read |
| CHECK | editable within the Writable values; a wrong value can damage the model, the receiver or the servos, so the field is marked |
| edit | editable within the Writable values |

A value that is read but lies outside the Writable values is shown and
written back as read. Values without a unit are plain numbers.

| # | Setting | Values, scale, unit | Writable | Access | Source | Conflict |
| ---: | --- | --- | --- | --- | --- | --- |
| 0 | Parameter count | 32; defines the table length | – | identity | OY, RF-F | – |
| 1 | ESC mode | 0 free, no governor; 1 helicopter, external governor; 2 helicopter governor; 3 helicopter governor store; 4 glider with brake; 5 airplane without brake; 6 airplane with F3A brake | 0–6 | CHECK | RF-L, M | OY: free throttle or governor from an internal or external setpoint °, no values |
| 2 | BEC voltage | raw × 0.1V; 55–84 = 5.5–8.4V; up to 123 = 12.3V while bit 3 of index 26 is set | 55–84; 55–120 while 26 bit 3 is set | CHECK | RF-L, OY, M | upper end: RF-L 123 = 12.3V, M 12.0V (Saphir V2, Aureus V2); M 8.4V on the LVT models |
| 3 | Motor timing | 0 auto normal; 1–6 = 0°, 6°, 12°, 18°, 24°, 30°; 16 auto normal; 17 auto efficient; 18 auto power; 19 auto extreme | 0–6, 17–19 | edit | RF-L, OY, M | OY: 0–6 only °; 16–19 from RF-L only; RF-L shows 0 and 16 as one setting and writes 0 for it |
| 4 | Startup response | RF-L: 0 normal, 1 smooth | – | raw | RF-L, OY | OY: initial torque, drive out of standstill ° |
| 5 | Governor P gain | number | 1–9 | edit | RF-L, OY | RF-L 1–10, OY 0–9 |
| 6 | Governor I gain | number | 1–9 | edit | RF-L, OY | RF-L 1–10, OY 0–9 |
| 7 | Throttle response | 0 slow, 1 medium, 2 fast, 3 custom (curve set in YGE's PC tool) | 0–3 | edit | RF-L, OY | OY calls 2 "high"; same 4 values |
| 8 | Cut-off handling | at the low-voltage limit: 0 off; 1 slow down, power reduced gradually; 2 cut off | 0–2 | CHECK | RF-L, OY | – |
| 9 | Cut-off voltage per cell | 0–5 = 2.9–3.4V, 0.1V a step, LiPo | 0–5 | edit | RF-L, OY, M | – |
| 10 | Active freewheel | 0 off, 1 auto, 2 unused, 3 always on | 0, 1, 3 | edit | RF-L, OY, M | OY: on or off °; RF-L: 4 codes, 2 unused |
| 11 | ESC type | model code, table below | – | identity | RF-L, OY | – |
| 12–13 | Firmware version | u32, 12 the low word; version = value / 100000, 103555 = 1.03555 | – | identity | RF-L, OY | – |
| 14–15 | Serial number | u32, 14 the low word | – | identity | RF-L, OY | – |
| 16 | mAh alarm limit | OY: consumption at which the ESC raises a telemetry warning | – | raw | OY | RF-L: unnamed |
| 17 | Stick zero | 900–1900µs, input pulse width taken as zero throttle | 900–1900 | edit | RF-L, OY | – |
| 18 | Stick range | 600–1500µs, span from zero to full throttle | 600–1500 | edit | RF-L, OY | – |
| 19 | Stick period | OY: expected input frame period ° | – | raw | OY | RF-L: unnamed |
| 20 | Motor pole pairs | RF-L: 1–100 pole pairs | – | raw | RF-L, OY | RF-L pole pairs, OY poles: a factor of 2 apart |
| 21 | Pinion teeth | 1–255 | 1–255 | edit | RF-L, OY | – |
| 22 | Main gear teeth | 0–1800 | 0–1800 | edit | RF-L, OY | – |
| 23 | Minimum start power | 0–26% | 0–26 | edit | RF-L, OY | – |
| 24 | Maximum start power | 0–31% | 0–31 | edit | RF-L, OY | – |
| 25 | Telemetry type | OY: which telemetry format the ESC sends °; M: YGE, Jeti EX Bus, HoTT V4 or MSBv2, chosen in YGE's PC tool | – | telemetry | OY, M | RF-L: unnamed; the codes are not known |
| 26 | Flags | low byte: bit 0 direction (0 normal, 1 reverse), bit 1 F3C autorotation (0 off, 1 on), bit 2 keep mAh, bit 3 BEC up to 12V; bits 4–7 and the high byte unnamed | bit 0 and bit 1; every other bit as read | bit 0 CHECK, bit 1 edit, the rest raw | RF-L, OY | OY: bit field, contents unknown |
| 27 | Current limit | raw / 100 = A; 1–65500 = 0.01–655.00A | 1–65500 | CHECK | RF-L, OY, M | – |
| 28–29 | Soft start, soft run | OY: 28 spool-up rate from rest °, 29 rate limit on throttle changes while running ° | – | raw | OY | RF-L: one unnamed u32 |
| 30–31 | Soft blend, RPM setpoint | OY: 30 crossover from the soft-start ramp to normal running °, 31 governor target ° | – | raw | OY | RF-L: one unnamed u32 |

Notes per index:

- 0: RF-F never writes index 0.
- 1: RF-L marks mode 0 for attention. M lists modes 1 to 6 for the stick
  mode setup and names mode 1 for an external governor such as VBar.
- 2: M (Saphir V2, Aureus V2, 35–135 LVT): the BEC (battery eliminator
  circuit) voltage is set in 0.1V steps in YGE's PC tool. A value above
  the servos' rating damages them.
- 3: M: automatic timing or 6 fixed steps.
- 7: value 3 selects a curve that only YGE's PC tool sets.
- 9: M (ProgCard III): 2.9 to 3.4V per cell for LiPo.
- 26: RF-L writes only bits 0 and 1 and writes every other bit back as
  read. Bit 3 raises the upper end of index 2 from 84 to 123.
- 27: RF-L edits it in steps of 100, 1.00A. M (Opto 255): current limit
  up to 400A.
- 21 and 22 give the head speed with the motor RPM (section 4).

The reference comments are misnumbered around 26–28 (three consecutive
entries labelled 26). RF-L places the flags at 26 and the current limit at
27.

Confirm the indices before writing to a real ESC. To identify an index,
read every value, change one setting with YGE's own tool, and read the
values again. The changed index identifies that setting (step H5, section
8).

#### ESC type codes, index 11

| Code | Model |
| ---: | --- |
| 848 | 35 LVT BEC |
| 1616 | 65 LVT BEC |
| 2128 | 85 LVT BEC |
| 2384 | 95 LVT BEC |
| 4944 | 135 LVT BEC |
| 2304 | 90 HVT Opto |
| 4608 | 120 HVT Opto |
| 5712 | 165 HVT |
| 8272 | 205 HVT |
| 8273 | 205 HVT BEC |
| 4177 | Aureus 105 |
| 4179 | Aureus 105 v2 |
| 5025 | Aureus 135 |
| 5027 | Aureus 135 v2 |
| 4689 | Saphir 125 |
| 5457 | Saphir 155 |
| 5459 | Saphir 155 v2 |
| 4928 | Opto 135 |
| 9552 | Opto 255 |
| 16464 | Opto 405 |

Source: RF-L. Another code is shown as its number. In hexadecimal the
upper digits of 17 of the 20 codes read as the model's current rating,
848 = 0x350 for the 35 LVT BEC °.

---

## 6. `status1`

One byte carrying a motor state and a set of warnings.

### Low nibble: motor state

| | | |
| ---: | --- | --- |
| 0x0 | `DISARMED` | stopped |
| 0x1 | `POWER_CUT` | power cut; the warning bits say why |
| 0x2 | `FAST_START` | bailout |
| 0x4 | `ALIGN_FOR_POS` | positioning |
| 0x6 | `BRAKING_NORM_FINI` | |
| 0x7 | `BRAKING_SYNC_FINI` | |
| 0x8 | `STARTING` | |
| 0x9 | `BRAKING_NORM` | |
| 0xA | `BRAKING_SYNC` | |
| 0xC | `WINDMILLING` | turning, not driven |
| 0xE | `RUNNING_NORM` | running normally |

0x3, 0x5, 0xB, 0xD and 0xF are reserved. Show an unknown state as its number.

### High nibble: warnings

| Mask | |
| --- | --- |
| 0x10 | undervoltage |
| 0x20 | over-temperature |
| 0x40 | over-current |
| 0x80 | the warnings refer to the BEC, not the ESC |

The encoding reuses one combination: `0x80 | 0x40` cannot represent BEC
over-current and instead means setpoint noise, or a disturbed input
signal. Decode it in this order:

1. High nibble exactly `0xC0`: setpoint noise. Stop.
2. Otherwise bit 0x80 selects the subject: set BEC, clear ESC.
3. Bits 0x10 / 0x20 / 0x40 are that subject's warnings.

### Warnings qualified by state

A warning bit alone is not a fault:

| Warning | Is a fault when |
| --- | --- |
| none set | state is `POWER_CUT`: over-voltage |
| undervoltage | state is below `STARTING` (< 0x08) |
| over-temperature | state is `POWER_CUT` |
| over-current | state is `POWER_CUT` |

Over-voltage has no bit of its own; it is the absence of warnings while the
power is cut.

---

## 7. Defects in the reference code

Six places where the reference implementation and the protocol differ. An
implementation transcribed from it inherits all six.

| | |
| --- | --- |
| 1. Init returns nothing | declared as returning a success flag, falls off the end without a `return` |
| 2. An oversized request corrupts the send | the length field is set before the size check that returns early, so the next transmission sends stale bytes past the end of the frame |
| 3. Timing arguments are ignored | frame period and timeout are accepted and never assigned |
| 4. The sequence number lives in the send buffer | incremented in place, so an early return (2) advances it without a frame going out |
| 5. Struct casting over the wire buffer | assumes little-endian, no padding and aligned access |
| 6. Parameter comments misnumbered | three consecutive entries labelled 26 |

The reference resynchronises by discarding buffered bytes one at a
time. The decoder in `protocols/openyge/` checks every sync candidate
and accepts the earliest complete frame. It can therefore recover a
frame preceded by noise that contains a plausible sync.

---

## 8. Open measurements

Each unknown names the hardware step that settles it. Steps H3 to H6 need
a YGE ESC with OpenYGE firmware V1.03547 or later, its telemetry type set
to YGE, a battery and no propeller. H5 and H6 also need YGE's USB adapter
and PC tool.

| Step | What it does |
| --- | --- |
| H3 | read only: time from power-on to the first frame, auto frames, 100 requests, logic analyser threshold sweep from 1.0 to 3.0V, address, sequence echo; supply current over 10s from the PD mini; throttle lead connected and unplugged |
| H4 | write index 21 (pinion, no effect on the drive) plus 1, read, power cycle, read, 20 times; each writable field once at its minimum and maximum; writes 4ms, 20ms and 50ms apart |
| H5 | in YGE's PC tool, change one setting, read the table on the bench, compare; run a stick mode setup, compare |
| H6 | logic analyser on the telemetry wire while YGE's PC tool connects, reads and writes; on the throttle lead while a ProgCard reads and writes |

| # | Unknown | Settled by |
| --- | --- | --- |
| U1 | the ESC's transmit level (3.3V or 5V), a pull-up on the ESC side, the idle level | H3, threshold sweep and oscilloscope |
| U2 | turnaround from the end of a request to the first reply byte, and the gaps between reply bytes | H3, 100 requests |
| U3 | the ESC's address; whether device 1 and broadcast 0 are answered | H3 |
| U4 | whether `WRITE_PARAM_RESP` carries the written index and value in `pidx` and `pdata` | H4 |
| U5 | when a write is stored, and whether it survives a power cycle | H4, 20 cycles |
| U6 | what the ESC does with a value outside the range | not measured: the write guard refuses such a value, so it is never sent |
| U7 | the meanings of 4, 16, 19, 25, 26 high byte and 28 to 31, and the conflicts of section 5.3: index 20 pole pairs or poles, gains 1–10 or 0–9, timing codes 16–19, freewheel values | H5, or an answer from YGE |
| U8 | whether a stick mode setup resets the telemetry type and other indices | H5 |
| U9 | whether a connected throttle lead or a throttle signal changes whether the ESC answers | H3, throttle lead variant |
| U10 | time from power-on to the first frame; whether `TELE_AUTO` frames come at all | H3 |
| U11 | inrush and start-up current on the PD mini; the lowest voltage at which each model answers | H3, current log |
| U12 | opto models: whether the telemetry port needs power from the receiver side | H3 with an opto model |
| U13 | the ProgCard wire protocol on the throttle lead | H6 |
| U14 | whether YGE's PC tool speaks OpenYGE frames or a protocol of its own | H6 |
| U15 | the shortest spacing for consecutive writes; RF-F uses 4ms | H4 |
| U16 | whether the ESC echoes `seq` as section 2.1 states | H3 |

Telemetry frame checks, from the same captures:

| # | Check | Settled by |
| --- | --- | --- |
| T1 | `rpm` scale, ×10 against ÷10 | a run with a known motor, known poles and a known mechanical RPM |
| T2 | CRC seed: a captured frame verifies with 0x0000 and fails with 0xFFFF; RF-F uses seed 0 | H3 capture |
| T3 | a v3 telemetry frame is 34 bytes and `frame_length` counts the CRC; RF-F counts it | H3 capture |
| T4 | a pre-v3 ESC omits `seq` and `device`, and its payload starts at offset 4 | a capture from a pre-v3 ESC |
| T5 | `status2` and `reserved1`; if `reserved1` is a consumption high byte it moves on a long run | a log across a run |

---

## 9. Status in rcbench

Neither board runs the protocol. The IOmcu image links the module, the
panel image does not, and neither calls it. The design assigns the
protocol to the IOmcu: a 20Hz request-response loop with turnaround
timing in microseconds and a timeout that expires even when no bytes
arrive. The panel would receive the result as `bench_state` through
the page protocol; OpenYGE frames would remain on the IOmcu side.
Throttle commands would use control-page writes subject to the
heartbeat and the IOmcu's failsafe ([Safety](Safety.md)).

A YGE ESC reports voltage, current, consumption, eRPM and four temperatures
itself, so every modelled field of `bench_state` has a source before the
current sensor is fitted. The ESC's own figures and the bench's shunt are
independent measurements.

Built, host-tested, not wired in:

    protocols/openyge/
      openyge_frame.c        encode, decode and the CRC, openyge_crc()
      openyge_status.c       status1 -> state, subject, warnings, faults
      openyge_params.c       the parameter cache and its completeness rule

The decoder counts frames, CRC errors, resyncs, and separately frames whose CRC
held but whose shape was impossible. Nothing is cast over the receive buffer.
`openyge_params_get` returns false until every index has arrived, and a pending
write withdraws the whole table.

`openyge_session.c` is not built. This request-response state machine
is intended to handle periodic polling, sequence matching, read and
write timeouts, and fallback to listening for a pre-v3 ESC. It first
needs the turnaround measurement, U2 in section 8, and the timing of
section 10. Implementation work
takes place in a separate repository.

Open decision: which ESC protocols beyond OpenYGE are supported. The source
material sits beside Hobbywing, Kontronik, Scorpion, OMP, ZTW, APD, FLY,
Graupner and XDFly, several sharing a state machine and none sharing a frame
format. One protocol proven on hardware first, then a second.

---

## 10. Session timing

The times a master keeps while it polls and writes. The values are RF-F's
constants. RF-F marks both timeouts "to be confirmed" and the write period
as provisional; U2, U10 and U15 in section 8 settle them. The Session
column holds the values for `openyge_session.c`, which is not built; a
value marked chosen has no source.

| Time | RF-F | Counted from | Session |
| --- | ---: | --- | --- |
| Boot delay | 5000ms | the flight controller's start; RF-F does not see the ESC's power-on | 5000ms after the supply reads on; setting 1000–20000ms |
| Listen for the first frame | – | – | up to 3000ms, chosen |
| First request after an auto frame | 20ms | the end of that frame | – |
| Request period | 38ms | the end of the previous reply to the next request; about 50ms a cycle, 20Hz | 40ms, chosen |
| Write period | 4ms | the end of the previous reply to the write request | one write after each reply, U15 |
| Read timeout | 200ms | the request's send; "to be confirmed" | 200ms |
| Write timeout | 400ms | the request's send; "to be confirmed" | 400ms, 3 tries |
| Frame timeout in auto mode | 900ms down to 60ms | the previous frame; it shrinks to the measured frame gap plus 10ms, never below 60ms | – |
| Table read after a write | – | – | up to 10000ms, chosen |

How RF-F runs the session:

- It addresses device 1 with the master bit set, 0x81, in every request.
- It increments `seq` once per new request. A timeout sends the same
  request again with the same `seq`.
- It drops a reply whose `seq` differs from the request's. `TELE_AUTO`
  frames are exempt.
- It writes in request/response mode only. Wired receive-only, it listens
  to `TELE_AUTO` frames and writes nothing.
- A save writes every index from 1 up whose value differs, one per request
  slot in rising order, then goes back to telemetry requests. It never
  writes index 0.
- From the save until the last write is sent it caches no value. It
  publishes the table again once every index has been read again.
- An ESC with a header below version 3 switches it to listening.
