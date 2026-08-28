# The OpenYGE ESC telemetry and parameter protocol

A written specification of the protocol: frame format, telemetry payload,
parameter access, and the status encoding — with the arithmetic needed to
implement it.

> **The implementation lives in a separate repository.** This page stays here
> because it is the specification of record, because it is going to YGE to be
> checked, and because the CRC seed and decoder shape it settles are shared
> with this project's own link. The codec under `shared/openyge/` is dormant.

**Marked ° = inferred, not stated.** Those meanings come from a field name and
ordinary ESC practice rather than from anything authoritative, and they are the
parts most worth correcting. Section 8 lists what still needs a measurement or
an answer; between them, ° and section 8 are the whole of what is uncertain
here.

## 1. Physical layer

| | |
| --- | --- |
| Signalling | UART, **115200 baud, 8N1** |
| Byte order | **Little-endian** throughout |
| Topology | Half duplex, one master and up to 127 addressable ESCs |
| Rate | ~20 Hz telemetry |

The master hears its own transmission — a sender discards exactly as many
received bytes as it just sent — so the connection is a **single wire** with TX
and RX commoned.

**This needs a UART of its own.** rcbench's panel link is
[CAN](Link.md) and carries nothing else; OpenYGE is an asynchronous byte
stream at 115200 and cannot share it. (When the panel link was RS485 there was
a second reason — its auto-direction circuit had a baud *floor* near 125 kbaud,
which 115200 sits under — but that transceiver is gone.) So: a dedicated
half-duplex UART on the coprocessor. Either a hardware
UART with TX and RX tied through a resistor and the echo discarded, or a PIO
soft UART. The coprocessor already plans PIO UARTs for the receiver-bus
analyser, so the second costs one state machine of twelve and gives cleaner
control of the turnaround.

Timing at 115200 8N1 — 86.81 µs per byte:

| | |
| --- | ---: |
| 34-byte telemetry reply | 2.95 ms |
| 12-byte request | 1.04 ms |
| One poll, both directions | **3.99 ms** plus the ESC's turnaround |

8% of the line against a 50 ms cycle. Room for a second ESC on its own UART, or
for parameter traffic interleaved with telemetry.

---

## 2. Frame structure

Every frame is `header · payload · CRC`. `frame_length` counts **the whole
frame, header and CRC included**.

### 2.1 Header, protocol version 3 and later — 6 bytes

| Off | Field | |
| ---: | --- | --- |
| 0 | `sync` | Always **0xA5** |
| 1 | `version` | 3 for the current protocol |
| 2 | `frame_type` | Section 3 |
| 3 | `frame_length` | Total, header and CRC included |
| 4 | `seq` | Master increments per request; the ESC **echoes it back** |
| 5 | `device` | Bit 7 (**0x80**) set = sender is the master. Bits 0–6 are the ESC address **0x01–0x7F**; **0x00 addresses all ESCs** |

### 2.2 Legacy header, version below 3 — 4 bytes

`sync`, `version`, `frame_type`, `frame_length` only. With no sequence number
and no address, **a pre-v3 ESC cannot be polled or addressed**: it broadcasts
telemetry and that is all. Parameter access does not exist below v3.

An implementation must therefore read `version` before it knows where the
payload starts.

### 2.3 CRC — 2 bytes, little-endian, at the end

Over **every byte from `sync` up to but not including the CRC**.

> **The algorithm is CRC-16/XMODEM, not CRC-16/CCITT-FALSE**, whatever it may
> be called in a given implementation. Same polynomial, different seed, and the
> difference is every byte of every frame:
>
> | | Poly | Init | Refl. | XorOut | Check over `"123456789"` |
> | --- | --- | --- | --- | --- | --- |
> | **OpenYGE** | 0x1021 | **0x0000** | none | none | **0x31C3** |
> | rcbench's own link | 0x1021 | 0xFFFF | none | none | 0x29B1 |
>
> Both check values were computed to confirm this.

rcbench's `link_crc()` takes the seed as its first argument, so the existing
tested routine serves both links unchanged — `0x0000` here, `LINK_CRC_INIT` for
the panel link. One new vector, no new code.

---

## 3. Frame types

| Value | Name | From | |
| ---: | --- | --- | --- |
| 0x00 | `TELE_AUTO` | ESC | Unsolicited telemetry. What a pre-v3 ESC does, and what a v3 ESC does until the master takes over |
| 0x02 | `TELE_RESP` | ESC | Telemetry, answering a request |
| 0x03 | `TELE_REQ` | Master | Ask for telemetry |
| 0x04 | `WRITE_PARAM_RESP` | ESC | Acknowledges a parameter write; carries telemetry too |
| 0x05 | `WRITE_PARAM_REQ` | Master | Write one parameter |

Requests carry the 4-byte control payload of section 5.2; every ESC-to-master
frame carries the full telemetry payload of section 4, the parameter-write
acknowledgement included.

The material claims bit 7 of `frame_type` marks a write — 0x81 for a parameter
update. **No value above uses it and nothing tests it.** Treat `frame_type` as a
plain enumeration and reject anything unlisted. Worth asking YGE whether bit 7
is reserved for something not yet in use, since a decoder that rejects it today
will reject it tomorrow.

---

## 4. Telemetry payload — 26 bytes

Offsets within the payload and absolute in a version 3 frame. A v3 telemetry
frame is **34 bytes**; a legacy one is 32.

| Payload | v3 abs | Size | Field | Unit | To SI |
| ---: | ---: | ---: | --- | --- | --- |
| 0 | 6 | 1 | `reserved` | | |
| 1 | 7 | u8 | `temperature` | °C **+ 40** | `°C = v − 40`, −40…215 |
| 2 | 8 | u16 | `voltage` | 10 mV | `V = v / 100` |
| 4 | 10 | u16 | `current` | 10 mA | `A = v / 100` |
| 6 | 12 | u16 | `consumption` | mAh | as is |
| 8 | 14 | u16 | `rpm` | **10 eRPM** | `eRPM = v × 10` — see below |
| 10 | 16 | i8 | `pwm` | % | output duty, **signed** |
| 11 | 17 | i8 | `throttle` | % | input setpoint, **signed** |
| 12 | 18 | u16 | `bec_voltage` | mV | as is |
| 14 | 20 | u16 | `bec_current` | mA | as is |
| 16 | 22 | u8 | `bec_temp` | °C + 40 | `°C = v − 40` |
| 17 | 23 | u8 | `status1` | | Section 6 |
| 18 | 24 | u8 | `cap_temp` | °C + 40 | capacitor pack |
| 19 | 25 | u8 | `aux_temp` | °C + 40 | |
| 20 | 26 | u8 | `status2` | | undocumented; carry it, do not interpret it |
| 21 | 27 | u8 | `reserved1` | | possibly a consumption high byte above 65 Ah ° |
| 22 | 28 | u16 | `pidx` | | parameter index — section 5 |
| 24 | 30 | u16 | `pdata` | | parameter value — section 5 |

Every 16-bit field happens to land on an even offset in both header lengths, so
an ordinary ABI lays this out with no padding. **Do not rely on it.** Parse byte
by byte: casting a buffer to a struct assumes little-endian, assumes no
padding, and performs unaligned reads that are undefined behaviour on
strict-alignment targets. rcbench's link decoder already parses byte-wise.

### `rpm` — the one genuine contradiction

The field is described as "0.1 eRPM" while the code multiplies by ten. Both
cannot be true. Multiplying is right: it is what most ESC telemetry sends, and
65535 × 10 = 655,350 eRPM is a plausible ceiling where 6,553 would not be.

**Measure it anyway** — a known motor at a known RPM, one reading. It is the
difference between a tachometer and a decoration.

Mechanical RPM needs the pole count, parameter 20:

    motor RPM = eRPM / (poles / 2)
    head RPM  = motor RPM × pinion teeth / main gear teeth

---

## 5. Parameters

The half of this protocol that makes it more than a telemetry feed: **it reads
and writes the ESC's configuration**, which is ESC programming over a
documented, non-proprietary interface.

### 5.1 How they arrive: drip fed, one per frame

Every telemetry frame carries **one** `(pidx, pdata)` pair; the ESC walks its
own table and the master collects. A master builds the table by caching each
pair, reading **parameter 0, the parameter count**, and treating the table as
complete only when every index `0 … count−1` has been seen.

At 20 Hz and 32 parameters that is about **1.6 seconds**. Show it as progress;
never present a half-read table as the ESC's settings.

Two rules worth keeping:

- **Stop accepting cached values while writes are outstanding**, or a frame
  already in flight lands on top of a new value and the ESC looks like it
  ignored the write.
- **Withdraw the whole table when a write is scheduled** and republish only
  when every index has been re-read. Partly-old, partly-new is worse than none.

The cache is capped at **64 parameters** by a 64-bit bitmap. 32 is today's
reality.

### 5.2 Control payload — 4 bytes

Carried by `TELE_REQ` and `WRITE_PARAM_REQ`:

| Off | Size | Field |
| ---: | ---: | --- |
| 0 | u16 | `index` |
| 2 | u16 | `param` — value to write |

A plain telemetry request sends both zero. A request frame is **12 bytes**.
Writes are **one parameter per frame**, each with its own sequence number.
There is no page write; a "save" is a queue of single writes.

### 5.3 The parameter table

Grouped by what they do. Indices are what goes on the wire and are as observed;
**°** marks a meaning inferred from the name rather than stated.

**Confirm these before writing any of them to a real ESC.** A wrong index into
an ESC's configuration is not a recoverable experiment. Reading is safe and
answers most of it on its own: read every index, change one setting in YGE's
own tool, read again, and the index that moved is the index that means it.

The reference comments are misnumbered around 26–28 — three consecutive
entries all labelled 26 — so at least one of those three is wrong on the page
even if the values are right.

#### Identity — read only

| # | Setting | What it does |
| ---: | --- | --- |
| 0 | **Parameter count** | 32 from firmware v1.03503. Read this first; it defines the table |
| 11 | ESC type | Model identifier, low word |
| 12–13 | Firmware version | Low, high word |
| 14–15 | Serial number | Low, high word |

#### Motor and gearing

| # | Setting | What it does |
| ---: | --- | --- |
| 20 | **Motor pole count** | Magnet poles. Needed for mechanical RPM: `motor RPM = eRPM / (poles / 2)` |
| 21 | Pinion teeth | Motor pinion |
| 22 | Main gear teeth | With 21, gives head speed. A bench that reads these reports head RPM without being told anything |
| 3 | **Motor timing** | Commutation advance. `0` = automatic; otherwise `1` = 0° through `6` = 30°, i.e. **6° per step** ° . More advance buys RPM and power at the cost of heat, efficiency, and margin against desync. Automatic adapts to the motor and is the sane default |

#### Governor — head speed regulation

| # | Setting | What it does |
| ---: | --- | --- |
| 1 | **Device mode** | Which job the ESC is doing: free-running throttle for a fixed-wing model, or a governor holding constant head speed for a helicopter, either from its own setpoint or from an external one ° . This decides whether parameters 5, 6 and 31 do anything at all |
| 5 | Governor P gain | Proportional term, 0–9. How hard the governor corrects a head-speed error. Too low and the head sags when the collective loads it; too high and it hunts audibly |
| 6 | Governor I gain | Integral term, 0–9. Removes the steady droop that P alone leaves. Too high gives a slow oscillation that P gain will not cure |
| 31 | RPM setpoint | Target head speed for the governor ° |

#### Starting and ramping

| # | Setting | What it does |
| ---: | --- | --- |
| 4 | Initial torque | How forcefully the motor is driven out of standstill ° . A high-inertia head wants more; too much risks losing sync at the moment there is least back-EMF to sync to |
| 23 | Minimum start power | Lower bound of the start ramp ° |
| 24 | Maximum start power | Upper bound of the start ramp ° . The two bracket how hard the ESC is allowed to push while spooling |
| 28 | Soft start | Spool-up rate from rest ° . The parameter that stops a helicopter snatching its drivetrain on every start |
| 29 | Soft run | Rate limit on throttle changes while running ° |
| 30 | Soft blend | Crossover between the soft-start ramp and normal running ° . One of the three most worth confirming, because a wrong value here is felt on every spool-up |
| 7 | Throttle response | Slow / medium / high / custom. How briskly the output follows the input. Slow protects the drivetrain; high is snappier and harder on it |
| 10 | Freewheel demand | Whether the ESC actively drives the low side during the PWM off-time — "active freewheeling" or "complementary PWM" ° . On gives better part-throttle efficiency and linearity and real braking; off lets the motor coast. On a helicopter it changes how the head behaves in autorotation, which is why it is a setting and not a constant |

#### Protection and limits

| # | Setting | What it does |
| ---: | --- | --- |
| 8 | **Cut-off type** | What happens at the low-voltage limit. `0` = nothing, the pilot is in charge; `1` = slow down, power reduced gradually so control is kept — the only sane choice in the air; `2` = cut off, power stops. `2` belongs on a boat or a car |
| 9 | Cut-off voltage per cell | Threshold that triggers the above, encoded as an offset where `0` = 2.9 V. **The step is not stated** — 0.1 V per count is the obvious guess ° and is worth one measurement, since being one step out is a pack flown flat or a model landed early |
| 27 | Current limit | The ESC's own current ceiling ° |
| 16 | mAh alarm limit | Consumption at which the ESC raises a telemetry warning |

#### BEC

| # | Setting | What it does |
| ---: | --- | --- |
| 2 | **BEC voltage** | Regulated output to the receiver and servos, in **0.1 V**. Set above what the servos tolerate and it destroys them quietly, which makes this the single most dangerous number in the table to write wrong |

#### Input signal calibration

| # | Setting | What it does |
| ---: | --- | --- |
| 17 | `STK_ZERO` | Input pulse width the ESC treats as zero throttle ° |
| 18 | `STK_RANGE` | Span from zero to full throttle ° |
| 19 | `STK_PERIOD` | Expected input frame period, i.e. the signal rate ° |

Together these three are the throttle-signal calibration — the equivalent of
the stick-range learning most ESCs do by beeping at you on power-up. Units are
presumed microseconds ° .

#### Telemetry and flags

| # | Setting | What it does |
| ---: | --- | --- |
| 25 | Telemetry type | Which telemetry format the ESC emits ° . An ESC set to something other than OpenYGE will not answer this protocol at all, so it is the first thing to read when a supposedly supported ESC stays silent |
| 26 | Flags | Bit field, contents unknown. Carry it, do not interpret it |

---

## 6. `status1`

One byte carrying two unrelated things.

### Low nibble — motor state

| | | |
| ---: | --- | --- |
| 0x0 | `DISARMED` | stopped |
| 0x1 | `POWER_CUT` | power cut; the warning bits say why |
| 0x2 | `FAST_START` | "bailout" |
| 0x4 | `ALIGN_FOR_POS` | positioning |
| 0x6 | `BRAKING_NORM_FINI` | |
| 0x7 | `BRAKING_SYNC_FINI` | |
| 0x8 | `STARTING` | |
| 0x9 | `BRAKING_NORM` | |
| 0xA | `BRAKING_SYNC` | |
| 0xC | `WINDMILLING` | turning, not driven — idle |
| 0xE | `RUNNING_NORM` | running normally |

0x3, 0x5, 0xB, 0xD and 0xF are reserved. Show an unknown state as its number;
do not map it onto a neighbour.

### High nibble — warnings, and the trap in it

| Mask | |
| --- | --- |
| 0x10 | Undervoltage |
| 0x20 | Over-temperature |
| 0x40 | Over-current |
| 0x80 | These warnings refer to the **BEC**, not the ESC |

**The encoding is overloaded.** `0x80 | 0x40` — "BEC over-current" — cannot
occur, so that exact combination is reused to mean **setpoint noise**: the ESC
reporting that its *input signal* is dirty. Decode in this order:

1. High nibble exactly `0xC0` → **setpoint noise**. Stop.
2. Otherwise bit 0x80 selects the subject: set → BEC, clear → ESC.
3. Bits 0x10 / 0x20 / 0x40 are that subject's warnings.

Get step 1 wrong and a noisy servo lead reads as a BEC over-current fault,
sending somebody after a short that is not there.

### The warnings are qualified by the state

A warning bit alone is not a fault:

| | Is a fault when |
| --- | --- |
| No warning bits set | state is `POWER_CUT` → **over-voltage** |
| Undervoltage | state is **below** `STARTING` (< 0x08) |
| Over-temperature | state is `POWER_CUT` |
| Over-current | state is `POWER_CUT` |

The same bit is a caution while running and a fault once power is cut — and
over-voltage has no bit of its own at all, it is *the absence* of warnings while
the power is cut. A decoder that reports bits without the state will both cry
wolf and miss the one condition with no flag.

---

## 7. Points to check in the reference code

Six faults found while reading the reference implementation. Each is a place
where *what the code does* and *what the protocol means* have to be told
apart — and an implementation transcribed from it would inherit all six.

| | |
| --- | --- |
| **1. The init entry point returns nothing** | Declared as returning a success flag, then falls off the end without a `return`. Undefined behaviour: initialisation reports success or failure at random |
| **2. An oversized request corrupts the send** | The builder sets the length field, *then* returns early if that length exceeds the buffer — leaving a length that claims more than was written. The next transmission sends stale bytes past the end of the frame |
| **3. Timing arguments are ignored** | The builder takes a frame period and a timeout and assigns neither; both assignments are commented out. Possibly an artefact of pulling one protocol out of a driver that handled a dozen |
| **4. The sequence number lives in the send buffer** | Incremented in place rather than kept as a counter, so a builder that returns early (2) advances the sequence without a frame going out |
| **5. Struct casting over the wire buffer** | Assumes little-endian, assumes no padding, performs unaligned 16-bit reads. It happens to work on the ABIs it has run on |
| **6. Parameter comments misnumbered** | Three consecutive entries labelled 26. Whether the *values* are off by one or only the comments is exactly what must be answered before writing to an ESC |

One difference that is not a defect: that code resynchronises by discarding its
buffer a byte at a time. **rcbench's decoder already scans every sync candidate
and prefers the earliest complete frame**, recovering a good frame that arrives
behind noise containing a plausible sync — a case the byte-at-a-time approach
drops. See [the link](Link.md); reuse that shape.

---

## 8. What to measure before trusting this

Each converts an inference into a fact.

1. **`rpm` scale.** Known motor, known pole count, known mechanical RPM.
   Settles ×10 against ÷10 (section 4).
2. **CRC seed.** Capture one frame; confirm it verifies with `0x0000` and fails
   with `0xFFFF`.
3. **Frame length.** Confirm a v3 telemetry frame is 34 bytes and that
   `frame_length` counts the CRC.
4. **Legacy header.** Confirm a pre-v3 ESC omits `seq` and `device` and that
   the payload starts at offset 4.
5. **Turnaround.** The gap between the last byte of a request and the first of
   the reply. Sets the coprocessor's timing as `LINK_TURNAROUND_US` does for
   the panel link.
6. **Parameter indices**, with YGE or by read-modify-read against their own
   tool — **before any write**. Section 5.3, and the cut-off voltage step
   with them.
7. **`status2` and `reserved1`.** Log both across a session; if `reserved1` is
   a consumption high byte it will move on a long run.

---

## 9. Where this lands in rcbench

**On the coprocessor.** A 20 Hz request/response loop with a turnaround in
microseconds and a timeout that must fire whether or not bytes arrive — the
panel has no business near it. The panel sees the result as a `bench_state`
through the existing page protocol; **no OpenYGE frame crosses the panel
link**, which is the rule [the two-processor split](Link.md) rests on. Nor does
it change the safety arrangement: a throttle command still travels as a
register write behind the heartbeat and the coprocessor's failsafe
([safety](Safety.md)).

**What it changes.** This is step 3 — *make the numbers real* — arriving
without the sensor step 3 assumed. A YGE ESC reports voltage, current,
consumption, eRPM and four temperatures itself, so every modelled field of
`bench_state` gets a real source and the SIMULATION watermark can come off
before the INA228 back-ordered into 2027. It does not replace that shunt: the
ESC reports what it believes about itself, an independent shunt is what the
bench knows, and the disagreement is its own measurement.

**What it adds that was not planned.** ESC programming over a documented
interface with the vendor's help, which is arguably ahead of BLHeli_S and AM32
on the same list. Head speed free from parameters 20–22. And a fault display
with real content, since section 6 is a diagnostic rather than a status LED.

**What is built:**

    shared/openyge/          pure C, host-tested, no SDK
      openyge_frame.c        encode and decode, reusing link_crc with seed 0
      openyge_status.c       status1 -> state, subject, warnings, faults
      openyge_params.c       the drip-fed cache and its completeness rule

The decoder is the panel link's shape: every sync byte in the buffer is a
candidate and the earliest complete one wins, so a real frame arriving behind
noise that happens to contain a plausible sync is still recovered. It counts
frames, CRC errors, resyncs, and separately the frames whose CRC held but whose
shape was impossible — the last of those is a version mismatch or a bug at the
far end rather than a noisy line, and the two want different things done about
them. The analyser screen promises *"a raw view: bytes, gaps, errors,
framing"*, and those counters are it.

Two decisions in the codec worth knowing:

- **Nothing is cast over the receive buffer.** Every field is assembled byte by
  byte, because the wire is packed little-endian and a host need be neither.
- **A half-read parameter table refuses to be read at all.** `openyge_params_get`
  returns false until every index has arrived, and a pending write withdraws
  the whole table rather than the indices being written. Partly-old and
  partly-new reads as the ESC's settings and is not.

**What is not built:** `openyge_session.c`, the request/response state machine —
polling at the frame period, matching the sequence number, the read and write
timeouts, and falling back to listening when the ESC turns out to be pre-v3. It
wants the turnaround measurement from section 8 before its timing means
anything, so it is next rather than now.

**One thing to decide first: which ESCs are supported.** OpenYGE is YGE's. The
material sits beside Hobbywing, Kontronik, Scorpion, OMP, ZTW, APD, FLY,
Graupner and XDFly — several sharing a state machine, none sharing a frame
format. Building the session layer so a second protocol can be added is nearly
free now and expensive later, but building two before either has met hardware
is inventing requirements. One protocol, proven, then the second.
