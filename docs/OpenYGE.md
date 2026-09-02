# The OpenYGE ESC telemetry and parameter protocol

<sub>**English** · [Deutsch](OpenYGE-de.md)</sub>

Specification of the protocol: frame format, telemetry payload, parameter
access and status encoding, with the arithmetic needed to implement it.

> The implementation is pursued in a separate repository. This page is the
> specification of record; it is going to YGE to be checked. The codec under
> `shared/openyge/` is built and tested but not wired into the firmware.

**° marks an inferred meaning.** Those entries come from a field name and
ordinary ESC (electronic speed controller) practice rather than from an
authoritative source. Section 8 lists what needs a measurement or an answer.
Together, ° and section 8 are everything on this page that is uncertain.

## 1. Physical layer

| | |
| --- | --- |
| Signalling | UART (universal asynchronous receiver-transmitter), 115200 baud, 8N1 |
| Byte order | little-endian throughout |
| Topology | half duplex, one master and up to 127 addressable ESCs |
| Rate | about 20 Hz telemetry |

The master hears its own transmission: a sender discards as many received bytes
as it sent. The connection is a single wire with TX and RX commoned.

The protocol needs a UART of its own on the coprocessor: a hardware UART with
TX and RX tied through a resistor and the echo discarded, or a PIO
(programmable input/output) soft UART. It cannot share the panel link, which is
CAN (Controller Area Network); see [The link](Link.md).

Timing at 115200 8N1, 86.81 µs per byte:

| | |
| --- | ---: |
| 34-byte telemetry reply | 2.95 ms |
| 12-byte request | 1.04 ms |
| One poll, both directions | 3.99 ms plus the ESC's turnaround |

That is 8% of the line at a 50 ms cycle.

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

`link_crc()` takes the seed as its first argument, so the same routine serves
both.

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
| 2 | 8 | u16 | `voltage` | 10 mV | `V = v / 100` |
| 4 | 10 | u16 | `current` | 10 mA | `A = v / 100` |
| 6 | 12 | u16 | `consumption` | mAh | as is |
| 8 | 14 | u16 | `rpm` | 10 eRPM (electrical revolutions per minute) | `eRPM = v × 10`, see below |
| 10 | 16 | i8 | `pwm` | % | output duty, signed |
| 11 | 17 | i8 | `throttle` | % | input setpoint, signed |
| 12 | 18 | u16 | `bec_voltage` | mV | as is |
| 14 | 20 | u16 | `bec_current` | mA | as is |
| 16 | 22 | u8 | `bec_temp` | °C + 40 | `°C = v − 40` |
| 17 | 23 | u8 | `status1` | | section 6 |
| 18 | 24 | u8 | `cap_temp` | °C + 40 | capacitor pack |
| 19 | 25 | u8 | `aux_temp` | °C + 40 | |
| 20 | 26 | u8 | `status2` | | undocumented; carry, do not interpret |
| 21 | 27 | u8 | `reserved1` | | possibly a consumption high byte above 65 Ah ° |
| 22 | 28 | u16 | `pidx` | | parameter index, section 5 |
| 24 | 30 | u16 | `pdata` | | parameter value, section 5 |

Every 16-bit field lands on an even offset in both header lengths. Do not rely
on it: parse byte by byte. Casting a buffer to a struct assumes little-endian,
assumes no padding, and performs unaligned reads that are undefined behaviour
on strict-alignment targets.

### `rpm` scale

The field is described as "0.1 eRPM" while the reference code multiplies by
ten. Multiplying is the assumption here: 65535 × 10 = 655,350 eRPM is a
plausible ceiling, 6,553 is not. Measure it (section 8).

Mechanical RPM (revolutions per minute) needs the pole count, parameter 20:

    motor RPM = eRPM / (poles / 2)
    head RPM  = motor RPM × pinion teeth / main gear teeth

---

## 5. Parameters

The protocol reads and writes the ESC's configuration.

### 5.1 Delivery: one parameter per frame

Every telemetry frame carries one `(pidx, pdata)` pair; the ESC walks its own
table and the master collects. A master builds the table by caching each pair,
reading parameter 0 (the parameter count), and treating the table as complete
only when every index `0 … count−1` has been seen.

At 20 Hz and 32 parameters that takes about 1.6 s. Show it as progress; never
present a partial table as the ESC's settings.

- Stop accepting cached values while writes are outstanding, or a frame already
  in flight overwrites the new value.
- Withdraw the whole table when a write is scheduled and republish only when
  every index has been re-read.

The cache in `shared/openyge/` is capped at 64 parameters by a 64-bit bitmap;
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

Grouped by function. Indices are as observed on the wire; ° marks a meaning
inferred from the name.

Confirm the indices before writing to a real ESC. Reading is safe: read every
index, change one setting in YGE's own tool, read again, and the index that
moved is the one that means it.

The reference comments are misnumbered around 26–28 (three consecutive entries
labelled 26), so at least one of those three is wrong in the reference.

#### Identity, read only

| # | Setting | Meaning |
| ---: | --- | --- |
| 0 | Parameter count | 32 from firmware v1.03503. Read first; defines the table |
| 11 | ESC type | model identifier, low word |
| 12–13 | Firmware version | low, high word |
| 14–15 | Serial number | low, high word |

#### Motor and gearing

| # | Setting | Meaning |
| ---: | --- | --- |
| 20 | Motor pole count | magnet poles; `motor RPM = eRPM / (poles / 2)` |
| 21 | Pinion teeth | |
| 22 | Main gear teeth | with 21 gives head speed |
| 3 | Motor timing | commutation advance. `0` automatic; otherwise `1` = 0° to `6` = 30°, 6° per step ° |

#### Governor

| # | Setting | Meaning |
| ---: | --- | --- |
| 1 | Device mode | free-running throttle, or governor from an internal or external setpoint ° ; decides whether 5, 6 and 31 have any effect |
| 5 | Governor P gain | 0–9 |
| 6 | Governor I gain | 0–9 |
| 31 | RPM setpoint | governor target ° |

#### Starting and ramping

| # | Setting | Meaning |
| ---: | --- | --- |
| 4 | Initial torque | drive out of standstill ° |
| 23 | Minimum start power | lower bound of the start ramp ° |
| 24 | Maximum start power | upper bound of the start ramp ° |
| 28 | Soft start | spool-up rate from rest ° |
| 29 | Soft run | rate limit on throttle changes while running ° |
| 30 | Soft blend | crossover between the soft-start ramp and normal running ° |
| 7 | Throttle response | slow / medium / high / custom |
| 10 | Freewheel demand | active freewheeling (complementary PWM (pulse-width modulation)) on or off ° |

#### Protection and limits

| # | Setting | Meaning |
| ---: | --- | --- |
| 8 | Cut-off type | at the low-voltage limit: `0` none, `1` reduce power gradually, `2` cut power |
| 9 | Cut-off voltage per cell | offset where `0` = 2.9 V; the step is not stated, 0.1 V per count is the assumption ° |
| 27 | Current limit | ° |
| 16 | mAh alarm limit | consumption at which the ESC raises a telemetry warning |

#### BEC

| # | Setting | Meaning |
| ---: | --- | --- |
| 2 | BEC (battery eliminator circuit) voltage | regulated output to receiver and servos, in 0.1 V. A value above the servos' rating damages them |

#### Input signal calibration

| # | Setting | Meaning |
| ---: | --- | --- |
| 17 | `STK_ZERO` | input pulse width treated as zero throttle ° |
| 18 | `STK_RANGE` | span from zero to full throttle ° |
| 19 | `STK_PERIOD` | expected input frame period ° |

Units are presumed microseconds ° .

#### Telemetry and flags

| # | Setting | Meaning |
| ---: | --- | --- |
| 25 | Telemetry type | which telemetry format the ESC emits ° ; an ESC set to another format does not answer this protocol |
| 26 | Flags | bit field, contents unknown; carry, do not interpret |

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

The encoding is overloaded: `0x80 | 0x40` (BEC over-current) cannot occur, and
that combination means setpoint noise, a dirty input signal. Decode in this
order:

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

The reference resynchronises by discarding its buffer a byte at a time. The
decoder in `shared/openyge/` scans every sync candidate and takes the earliest
complete frame, which recovers a frame that arrives behind noise containing a
plausible sync.

---

## 8. What to measure before trusting this page

1. `rpm` scale: known motor, known pole count, known mechanical RPM. Settles
   ×10 against ÷10.
2. CRC seed: capture one frame; confirm it verifies with 0x0000 and fails with
   0xFFFF.
3. Frame length: confirm a v3 telemetry frame is 34 bytes and that
   `frame_length` counts the CRC.
4. Legacy header: confirm a pre-v3 ESC omits `seq` and `device` and that the
   payload starts at offset 4.
5. Turnaround: the gap between the last byte of a request and the first byte of
   the reply. Sets the session layer's timing.
6. Parameter indices, with YGE or by read-modify-read against their own tool,
   before any write. Section 5.3, and the cut-off voltage step with them.
7. `status2` and `reserved1`: log both across a session; if `reserved1` is a
   consumption high byte it moves on a long run.

---

## 9. Status in rcbench

The protocol runs on the coprocessor: a 20 Hz request and response loop with a
turnaround in microseconds and a timeout that fires whether or not bytes
arrive. The panel sees the result as a `bench_state` through the page protocol;
no OpenYGE frame crosses the panel link. A throttle command travels as a
control-page write behind the heartbeat and the coprocessor's failsafe
([Safety](Safety.md)).

A YGE ESC reports voltage, current, consumption, eRPM and four temperatures
itself, so every modelled field of `bench_state` has a source before the
current sensor is fitted. The ESC's own figures and the bench's shunt are
independent measurements.

Built, host-tested, not wired in:

    shared/openyge/
      openyge_frame.c        encode and decode, reusing link_crc with seed 0
      openyge_status.c       status1 -> state, subject, warnings, faults
      openyge_params.c       the parameter cache and its completeness rule

The decoder counts frames, CRC errors, resyncs, and separately frames whose CRC
held but whose shape was impossible. Nothing is cast over the receive buffer.
`openyge_params_get` returns false until every index has arrived, and a pending
write withdraws the whole table.

Not built: `openyge_session.c`, the request and response state machine (polling
at the frame period, sequence matching, read and write timeouts, fallback to
listening for a pre-v3 ESC). It needs the turnaround measurement from section 8
first. The implementation is pursued in a separate repository.

Open decision: which ESC protocols beyond OpenYGE are supported. The source
material sits beside Hobbywing, Kontronik, Scorpion, OMP, ZTW, APD, FLY,
Graupner and XDFly, several sharing a state machine and none sharing a frame
format. One protocol proven on hardware first, then a second.
