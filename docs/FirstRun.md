# First run on hardware

<sub>**English** · [Deutsch](FirstRun-de.md)</sub>

Both boards powered with the heartbeat wire fitted, and every number below
read on an instrument. Written for 0.8.1. A servo and a motor have run from
the panel on the bring-up bench; what nothing here has done is put a scope or
an analyser on a pin, so every step says what "good" looks like and what to
write down when it is not.

Work down the list. Each step assumes the one above it passed.

---

## 0. Before power

> ### Set the panel's UART switch to `UART1` before anything else
>
> A slide switch beside the **BOOT** and **RESET** buttons is marked
> **`UART1`** and **`UART2`**. It selects what the bridged USB-C socket's
> serial side is connected to, and the console needs it on **`UART1`**.
>
> On the other setting the socket still enumerates: a COM port appears and
> the operating system names the CH343. Nothing crosses it in either
> direction — no console output and no flashing — which is the same symptom
> a broken cable or a broken board gives.
>
> The panel's other USB-C socket carries native USB on GPIO19 and GPIO20,
> which the multiplexer hands to CAN about a second into every boot. It
> flashes the board and it cannot watch a running one. The bridged socket is
> the panel's only console while the bench runs.

**Have to hand:** an oscilloscope, one servo, one ESC that speaks
bidirectional DShot, a bench supply with current limit, and the USB cable for
each board.

**Do not connect a motor to the ESC yet.** Step 5 drives no pin at all -- it
is the interlock, and no output is the passing result. Step 6 is the first pin
on an instrument, and the first thing to look at there is the scope, not a
propeller.

**Current limit:** set it low enough that a shorted output trips it rather
than burning a track.

---

## 1. Flash both boards

```bash
# coprocessor — produces rcbench-iomcu.uf2, copy it while holding BOOTSEL
cmake -S firmware/iomcu -B firmware/iomcu/build
cmake --build firmware/iomcu/build

# panel
idf.py -C firmware/panel set-target esp32s3
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor
```

The coprocessor build prints its own size check:

    -- rcbench: image 272040 bytes, 6% of the 4186112 bytes a four-megabyte
       module leaves below the store

**Watch for:** the module used for bring-up is a Waveshare RP2350-CAN with
**4 MB**, while `PICO_BOARD` defaults to a board file claiming **16 MB**. The
linker measures against 16 MB and will not warn. The line above is the only
thing that will, so read it.

**The panel's partition table changed.** It now carries a 2 MB `boardart`
partition. If the panel was flashed before that, flash the merged image at
offset 0 rather than only the app:

```bash
idf.py -C firmware/panel merge-bin -o rcbench-panel-merged.bin
esptool.py -p /dev/ttyACM0 write_flash 0x0 firmware/panel/build/rcbench-panel-merged.bin
```

---

## 2. The link, before anything else

The panel runs the CAN echo self-test itself, at every start-up, for 1200 ms
inside the splash. It answers one question: do frames cross the bus intact? It
uses no page protocol, so if it passes and the link still does not work, the
fault is above the wire.

**Good looks like nothing:** the splash shows `LINK  OK  CAN 1 Mbit/s` and
hands over to the menu.

**A failure is a screen**, before the menu, with the verdict, what to check in
order, and both ends' counters. It takes a two-second hold to leave.
[Bringing up the link](Bringup.md#the-bus-fault-screen) has the verdicts and
what each one means.

**Do not go on until it passes.** Every step below assumes frames cross.

The console carries the detail if you want it — the **UART socket**, not
native USB, because GPIO19 and GPIO20 carry both and the multiplexer selects
one:

    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0

---

## 3. The heartbeat wire — the thing that has blocked everything

| | |
|---|---|
| Panel end | **GPIO6**, on **J8** (a three-pin header carrying 3V3, GND, GPIO6) |
| Coprocessor end | **GP3** |
| Through | the retriggerable monostable, once one exists. It is on no board, so the bring-up bench runs a direct wire and has no hardware backstop. The wire covers a panel that stops beating while the coprocessor is healthy: it disarms after 150 ms of silence. What is uncovered is a panel that stops beating while the coprocessor cannot act -- nothing then removes the outputs, and that is what the monostable would do without any firmware |

Without this wire the coprocessor refuses every arm, and that is the interlock
working, not a fault.

**Numbers it is judged against:**

| | |
|---|---|
| Edge period | 20 ms |
| Gap accepted | 4 ms to 150 ms |
| Edges before it counts as alive | 4 |

So the line must be edging for roughly **80 ms** before an arm can succeed.

**On the scope, at GP3:** a square wave, edge to edge 20 ms. If the gap ever
exceeds 150 ms the coprocessor drops it and the next arm is refused.

**Where the edges come from:** the panel's control task, inside its poll loop.
Anything that stalls that task stops the heartbeat — which is why nothing
long is allowed to run there.

---

## 4. First link-up — expect about a minute of unusual traffic

**This is new and will happen on your first ever link-up.** The coprocessor
now carries a 201 kB photograph of itself, and the panel fetches it once and
keeps it in flash.

On the panel console, in this order:

    I (…) rcbench: coprocessor answered
    I (…) rcbench: hardware 1 says where its pads are
    I (…) rcbench: hardware 1 says which pads are grounds and rails
    I (…) rcbench: fetching hardware 1's photograph: 500 x 206, 206000 bytes
    …
    I (…) rcbench: hardware 1's photograph kept

**What to expect while it runs:**

- Extra CAN traffic for tens of seconds. It takes 15 ms of each 50 ms poll,
  so it is slower in wall clock than the bus alone would need.
- **A visible display stall** when it finishes and writes to flash. A flash
  operation closes the cache the panel's bounce-buffer refill reads PSRAM
  through, so the panel stops for the length of the write. Settings saves
  already do this.
- **It must not disturb the heartbeat.** The transfer is sliced and the flash
  write runs on its own task for exactly that reason. **If the heartbeat
  drops during this minute, stop and write it down** — that is the most
  important thing this first run can find, and it is code that has never run.

On the **second** link-up the photograph is already kept and none of this
happens. If you want to skip it entirely, a coprocessor built with the
artwork removed reports zero blocks and the panel draws the board from its
outline instead.

---

## 5. Arm with nothing connected

Nothing is wired to an output yet. This step tests the interlock, not a pin.

1. **Heartbeat wire removed** → arming must be **refused**. The coprocessor
   answers `NOT_ARMED` because `!beat.alive`. The panel shows the refusal.
2. **Heartbeat wire fitted, panel armed** → the coprocessor accepts.
3. **Pull the heartbeat wire while armed** → it must fail safe within
   **150 ms**.
4. **Press STOP** → latches. It must take an explicit arm to leave, not a
   link that recovers.
5. **Pull the CAN wire while armed** → the coprocessor gives up after
   **200 ms**; the panel escalates after **1 s**.
6. **Cover the touch panel / let touch die** → after **500 ms** of silence
   arming is blocked.

Every one of these is host-tested. **None of the numbers below has been seen
on an instrument.** A servo and a motor have since been run from the panel on
a bring-up bench, so a pin does drive; what no scope or analyser has read is
any pulse width, frame period or reply delay in this tree.

---

## 6. First pin on an instrument

Use a **servo**, not the ESC. A servo is the forgiving case and the one the
scope reads most easily.

**Pins free for an output:** GP0, GP1, GP2, GP4, GP5, GP6, GP7, GP13 to GP22
and GP26 to GP28.
**Reserved and refused:** GP3 (heartbeat), GP8–GP12 (CAN), GP23, GP24, GP25 and
GP29 (used by the module, not on the header) and GP30 and up (not on the part).
**Refused as a second PWM pin:** a pin whose PWM compare register is already
taken by a bound pin. On the RP2350 the slice is `(pin / 2) modulo 8` below
GP32 and the channel is the pin's low bit, so GP0 and GP16, GP1 and GP17, GP2
and GP18, GP4 and GP20, GP5 and GP21, GP6 and GP22 are pairs that share one
compare register. The second of a pair is refused rather than muxed onto the
first one's pulse width. The OUTPUTS page carries no bit saying "bound", so
the screen goes on looking configured while the lead produces no pulse.

On the panel: **Setup → OUTPUTS**, choose `SERVO PWM`, tick one pin. Or
**Setup → PICK A PIN** for the board picture — grounds are marked `G`, rails
carry their voltage.

**Scope the pin before connecting the servo.**

| | |
|---|---|
| Frame rate | 40 to 560 Hz (50 Hz by default) |
| Pulse | 400 to 2500 µs, refused outside |
| Resolution | 1 µs |

**Measure and record:** the actual frame period, the pulse at both ends of
travel, and the jitter. Bit timings are the largest unverified surface in the
tree.

**Then:** confirm the servo screen lets go when it is told to, and only
then. Lifting the finger does not stop the output: the screen holds the
position it was given and says it again every **100 ms** (`SERVO_HOLD_MS`),
against the coprocessor's **500 ms** (`OUT_DEFAULT_TIMEOUT_MS`), so a servo
stays where it was put. **RELEASE** returns the surfaces to centre; it does
not clear the slot and the pin keeps pulsing. What stops the edges is a
disarm, a STOP, or leaving the screen, which disarms. Check on the scope that
RELEASE moves the pulse to the middle of the channel's travel and that a
disarm is what stops it.

A channel nobody is refreshing does still go to rest after 500 ms, which is
what happens to a bound pin the servo screen is not holding.

---

## 7. The numbers nobody has measured

Written from the specification, exercised only against frames the same code
builds. Record real values for each:

- **Every DShot bit timing**, against an ESC's tolerance rather than the spec.
- **The reply rate** of five quarters of the DShot rate.
- **The leading-bit convention** of the group code.
- **The turnaround delay** — whether 30 µs is what an ESC actually waits.
- **The extended-telemetry frame types** and their units.
- **The PPM DMA ring** playing a frame.
- **The flash erase window** on the coprocessor, and whether the heartbeat
  re-acquires across a save.

---

## 8. Current monitors (INA228, INA3221)

The coprocessor's core 1 reads an INA228 in the ESC's power path and an
INA3221 on the servo rail over I2C (Inter-Integrated Circuit) at 400 kHz,
every 1 ms. This has not run on hardware. Every step below says what good
looks like; anything else is a finding to write down.

**Have to hand:** the MATEK I2C-INA-BM (INA228, 200 µΩ) and the DAOKAI
INA3221 (0.1 Ω per channel, 1.638 A full scale), each at the address it
ships with, two
2.2 kΩ resistors, a multimeter, a scope or logic analyser with I2C decode,
a servo, and the ESC and motor or a resistive load on a current-limited
supply.

**Set it up on the panel.** SETUP → INTERFACES: `INA228` and `INA3221` ON.
The other sensor settings default to the modules above as shipped: INA228
at 0x45 with 200 µΩ, INA3221 at 0x40 with 0.1 Ω, SDA on GP16 and SCL on
GP17. Without a panel, the build option `IOMCU_SENSE_BRINGUP=ON` enables both
parts at the page's defaults while no set-up is stored in flash:

```bash
cmake -S firmware/iomcu -B firmware/iomcu/build-sense -DIOMCU_SENSE_BRINGUP=ON
cmake --build firmware/iomcu/build-sense
```

**Wire it** (all coprocessor pads on the RP2350-CAN module):

| Wire | From | To |
|---|---|---|
| SDA | GP16 (pad 21) | INA3221 SDA, MATEK SDA |
| SCL | GP17 (pad 22) | INA3221 SCL, MATEK SCL |
| Pull-ups | 2.2 kΩ from SDA and from SCL | 3V3 (pad 36) |
| INA3221 power | 3V3 (pad 36), GND | INA3221 VS, GND |
| MATEK power | VBUS (pad 40, 5 V from USB), GND | MATEK 5V, G |

The firmware switches the pads' own pull-ups and pull-downs off on both
pins: the bus runs on the modules' pull-ups and the two 2.2 kΩ. **Before
connecting the coprocessor**, power the modules and measure SDA and SCL to
GND: both must read 3.3 V or less. The MATEK's pull-up rail is not known.

**The console.** Every 3 s, while a part is enabled, the coprocessor prints
two lines:

```
rcbench-iomcu: sense flags 0x0133 present 0x0021 ids 0x2281 0x3220 errors 0 | bench 1680 cV 1200 cA 33 mAh 6 dWh flags 0x63 | temp 254 dC
rcbench-iomcu: sense window 4711 CH1 120 mA 6000 mV ch_flags 0x01 | capture state 0 seq 0 move 0 arrive 0
```

`flags` is the SENSE page's FLAGS: 0x0001 INA228 online, 0x0002 its
identity read was an INA228's, 0x0004 something else answers at its
address, 0x0008 a current clipped, 0x0010, 0x0020 and 0x0040 the same for
the INA3221, 0x0100 the bus is open, 0x0200 the bus is stuck. `present`
has bit n for address 0x40 + n. `bench` is the BENCH page: 10 mV and
10 mA steps, charge in mAh, energy in 0.1 Wh, and its flags (0x01
voltage, 0x02 current, 0x20 the INA228's, 0x40 the INA228's totals).

### 8.1 The parts answer

Power everything, nothing armed.

**Good:** `flags 0x0133`, `present 0x0021`, `ids 0x228x 0x3220`, `errors`
not rising between two lines.

**Write down:** the two IDs as printed. An INA238 on the MATEK answers
0x238x and is refused (flag 0x0004). A DAOKAI unit that answers 0x1408
(one buyer's reading) is refused the same way (0x0040).

### 8.2 The bus on a scope

Probe SDA and SCL at the coprocessor.

**Good:** SCL at 400 kHz (2.5 µs a clock); rise time, 30 % to 70 %, at most
300 ns; a read of the INA3221's register 0x01 every 1.0 ms; the INA228's
registers 0x07 and 0x05 in turn, one each 1.0 ms; no NACK in the decode.

**Write down:** the rise time, the clock, the time from one transaction's
STOP to the next START (the controller's own time, not measured), and the
period of the CH1 read.

### 8.3 Readings against a meter

Run a steady current through the MATEK's shunt and measure it and the
pack voltage with the meter. Then a servo, or a resistor drawing under
1.6 A, on INA3221 CH1, with the meter in series.

**Good:** `bench` voltage and current, and `CH1` mA and mV (CH1's voltage
is at the load side of the shunt), agree with the meter within the meter's
accuracy and the shunt's tolerance, which is not known for either module.
`temp` near room temperature, in 0.1 °C. `ch_flags 0x01`; 0x10 appears
when CH1 passes 1.638 A, and CH1's current is then a bound, not a value.

**Write down:** each pair of numbers, the meter's and the console's.

### 8.4 A run: peaks and totals

Arm on MOTOR & ESC and hold a steady current for 60 s, then disarm.

**Good:** `bench flags` reads 0x63 (with 0x04 and 0x08 too when a
bidirectional ESC answers). `mAh` counts current × time: 2.00 A for 60 s
is 33 mAh. A new arm starts charge and energy from 0.
The MOTOR & ESC screen shows the INA228's voltage and current.

**Write down:** current, time and the charge reached, and the peaks the
panel shows against the scope or meter.

### 8.5 A lead pulled mid-run

Armed, pull the MATEK's SDA for about 5 s, then plug it back.

**Good:** within about 6 ms `flags` loses 0x0001 and keeps 0x0002; `bench
flags` reads 0x20: the INA228 is still the source, its fields empty, not
the ESC's numbers; `errors` rises; the bench stays armed. About 1 s after
the lead is back, 0x0001 returns, and 0x40 stays clear until the next arm.
Disarmed with the lead out, `bench` carries the ESC's telemetry again.

### 8.6 A stuck bus

Hold SDA to GND with a wire for 2 s, then let go. The lines are open drain:
the pin is never driven high, so the short is safe.

**Good:** `flags` gains 0x0200 while it is held; on SCL the scope shows 9
clocks and a STOP every 100 ms; after release, 0x0200 clears and both
parts are online within about 1 s.

**Write down:** the clock rate of the 9 clocks (meant to be 100 kHz), and
how long the parts took to come back.

### 8.7 A save while reading

Disarmed, with both parts reading, tick a pin on the OUTPUTS screen: the
binding is saved.

**Good:** the console prints `outputs saved, record n, program window N
us` and no `window refused`; `errors` does not rise; `window` goes on
counting; the heartbeat does not drop.

**Write down:** the program window, and the erase window when one is
printed: core 1 is parked in RAM for each.

### 8.8 Not reachable yet

The move capture on SERVO_SENSE -- the PWM frame stamped to 1 µs and the
travel times -- is armed by the panel's servo test, which does not use it
yet. A travel time against a scope on the PWM pin and on the shunt waits
for that.

---

## 9. When something goes wrong

| Symptom | Look here first |
|---|---|
| Arm refused, wire fitted | Heartbeat not edging 4 times yet, or gap over 150 ms. Scope GP3. |
| Arm refused, no obvious reason | Touch dead for 500 ms blocks it. Touch the panel. |
| Link up, screen offers no pins | The board's catalogue did not arrive. Console says which page failed. |
| Heartbeat drops in the first minute | **The artwork transfer or the flash keeper.** Newest code, never run. See §4. |
| Display freezes briefly | A flash write. Expected once, when the photograph is kept. |
| Coprocessor does not boot after flashing | Image past 4 MB. Check the size line from §1. |
| Panel boots but no `boardart` partition | Flashed app-only over an old table. Merge-bin at offset 0. |
| Output goes to mid-travel after ½ s | Working as intended — nothing wrote to that channel, so it went to its rest: mid-travel for a servo, zero for a motor. The pulses continue while the bench is armed |
| Pulses stop altogether | Not the timeout. Something released the pin, disarmed, or stopped the bench |
| `sense flags` without 0x0100, a part enabled | The I2C block did not open on those pins. They must be one block's SDA and SCL: SDA's GPIO number mod 4 is 0 or 2, SCL the next |
| `sense flags` 0x0200 that stays | SDA or SCL held low: an unpowered module clamping the bus, a missing pull-up, or a short |
| `sense present 0x0000` with the bus open | Nothing answers: pull-ups missing, modules unpowered, or SDA and SCL swapped |

**If the bench misbehaves in a way that points at the panel**, the three
newest and least proven things are all mine and all only compiler-checked:

- the **flash keeper task** (`artkeep`),
- the **artwork slice** inside the control task's poll loop, and
- the **run log task** (`runlog`), which owns every write to the SD (Secure
  Digital) card.

The first two are inert on a coprocessor that reports no photograph, which is
the quickest way to rule them out. The third is inert with no card in the
slot. With a card it appends a line to `RCBENCH.LOG` whenever a fault is
raised, armed or not; the run file is what waits for an arm, and for the
first row after it.

---

## 10. What to write down

For each step: what was measured, against what it was expected to be, and
what the scope showed. `STATUS.md` carries an "Open items" table — the rows
about drivers on hardware, the control page and the flash store are the ones
this run answers.

**Anything that is not measured stays written as not measured.** A number
guessed into that table is worse than an empty cell.
