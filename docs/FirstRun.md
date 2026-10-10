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

**Have to hand:** the MATEK I2C-INA-BM (INA228, 200 µΩ) at the address it
ships with, the DAOKAI INA3221 (0.1 Ω per channel, 1.638 A full scale) with
its A0 solder bridge closed to GND for 0x40 (as shipped all four A0 bridges
are open and the address floats between 0x40 and 0x41), two
2.2 kΩ resistors, a multimeter, a scope or logic analyser with I2C decode,
a servo, and the ESC and motor or a resistive load on a current-limited
supply.

**Set it up on the panel.** SETUP → INTERFACES: `INA228` and `INA3221` ON.
The other sensor settings default to the modules above: INA228
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

### 8.9 CH1's 1 ms samples on the console

A coprocessor image built with `-DSENSE_TRACE=ON` prints INA3221 CH1's
1 ms samples on its USB (Universal Serial Bus) console as text. The link
carries 50 ms windows only, so this build is the one way to see the single
samples. It is a debug build: a released image is built without the option
and holds none of its code. `tools/sense_trace.py` reads the captured
console. None of this has run on hardware. The bench session has two
parts: part A needs no encoder, part B repeats the moves with an AS5600
on the servo's shaft.

**What starts a trace.**

| Trigger | Trace |
| --- | --- |
| a PWM (pulse-width modulation) output renders another pulse width than in the pass before, 50 ms or more after that output's last change | 4 s from the frame that carries the pulse; trigger line `$C` |
| `t` typed on the console | 10 s; trigger line `$K` |
| the capture's PWM edge | 4 s; trigger line `$E`. No released panel arms the capture, so this trigger does not occur with one |

Every MOVE of the automatic servo test is a changed pulse, so a test with
the released panel is traced without a capture. A trigger during a trace
adds its line and moves the end to 4 s (10 s for `t`) after itself when
that is later: moves less than 4 s apart are one trace. `x` on the console
ends a trace. Each trace also holds the samples of up to 64 ms before its
trigger.

**What it costs.** Core 1 reads CH1 every 1 ms as in the released image
and copies the sample into a ring of 4096 records (49,152 bytes; 3.9 s of
1000 samples and 50 bus voltages a second). No bus transaction is added
to the tick. A change of the part's set-up or state is a record in the
same ring, so it keeps its place among the samples. Core 0 writes whole lines into the room the console's 64-byte
transmit buffer has, and nothing when no terminal is connected, so its
loop does not wait for the host. A full ring drops the newest record: the
trace has a `$L n=` line where records are missing and their sum in its
end line. The time a pass of core 0 gains from writing the lines is not
measured.

**The lines** (`shared/sense/sense_trace.h` has every field):

```text
$T v=1 n=2 trig=cmd t=220200 ms=22020 len=4000
$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=0
$C t=220200 ch=0 us=1800
-810,289
10,277
v5952
10,289
$Z n=2 s=8881 v=444 l=0 m=4 ml=0 e=t
```

A sample line is the time since the sample before it in 0.1 ms and the
shunt code, 40 µV a step: 0.4 mA on the 0.1 Ω shunt. A sample line is 8
bytes with a 3-digit code; with the voltage lines a trace is about 8.6
bytes a sample, 8.6 kB a second.

**Have to hand:** the PD mini (WeAct PD Power Mini V1) at a limit of
2.00 A, the INA3221 module with CH1's 0.1 Ω shunt in the servo's supply,
the servos to measure, and a Windows PC with PuTTY. Part A needs no
encoder. Part B needs an AS5600 on the servo's shaft.

**The terminal.** The console is the coprocessor module's own USB-C
socket, the one the image is flashed through, not the panel's. In the
Windows Device Manager it is under "Ports (COM & LPT)" as "USB Serial
Device (COMn)", hardware ID `VID_2E8A`. PuTTY settings:

| Where | Setting |
| --- | --- |
| Session | Connection type Serial, Serial line `COMn`, Speed 115200 |
| Connection, Serial | Data bits 8, Stop bits 1, Parity None, Flow control None |
| Session, Logging | "All session output", a new log file name for each run |

The port is USB CDC (communications device class): the speed is not used,
any value works. The coprocessor prints only while the terminal holds DTR
(data terminal ready), which PuTTY does while the port is open. No
timestamps in the log: a line with anything in front of it is not read. A
key is sent when it is pressed, with no Enter.

**Part A: without the encoder.**

1. Build the image:

   ```bash
   export PICO_SDK_PATH=/path/to/pico-sdk
   cmake -S firmware/iomcu -B firmware/iomcu/build-trace -DSENSE_TRACE=ON
   cmake --build firmware/iomcu/build-trace
   ```

2. Flash `firmware/iomcu/build-trace/rcbench-iomcu.uf2`: hold BOOTSEL,
   plug the module into the PC, copy the file to its drive.
3. Open the port in PuTTY with logging on. Within 3 s it prints a line
   starting `rcbench-iomcu:`. On the panel, SETUP → INTERFACES: `INA3221`
   ON.
4. Noise, no servo: with no servo connected and the supply's output on at
   6.00 V, press `t`. 10 s later the console prints a line starting `$Z`.
   Close PuTTY; this log is `noise.log`.
5. Servo at rest: new log file. Connect the servo, arm, leave it at the
   centre, supply on at 4.80 V. Press `t` and wait for the `$Z` line.
6. Moves, in the same log: on the SERVO screen's TEST page set LENGTH BY
   to MOVEMENTS, MOVEMENTS 20, DWELL 1000 ms, STEP 4.8 V on and every
   other step and BROWN-OUT off. START TEST. The trace runs from the first
   move to 4 s after the last; wait for its `$Z` line, then close PuTTY.
7. Repeat steps 5 and 6 at 6.00 V with STEP 6.0 V, and both for each
   servo: one log file per servo and voltage.
8. On the host, for each log:

   ```bash
   python3 tools/sense_trace.py noise.log
   python3 tools/sense_trace.py mg90s-4v8.log
   ```

**Good:** every trace reads `counts match the end line` and `0 records
missing`; the exit code is 0.

**What the tool prints.** Per trace: the samples and voltages read against
the end line; the mean, the standard deviation and the largest distance
from the mean of the samples before the first command, as read and through
a moving mean of 4 and of 8 samples; then every move replayed through
`shared/servo/servo_move.c` with the filter at 1, 4 and 8 samples and the
band at 0.02, 0.05 and 0.10 A: seen or not, and the arrival in ms from the
frame. For the log as a whole: the settings that see every move and time
its arrival, each setting's median arrival and its distance from the
capture's setting (filter 4, band 0.05 A), and how far apart the settings
put one move's arrival. It ends `not compared with the horn`: without an
encoder an arrival is the current back at its holding level, which can
lead or lag the horn. One `<log>-trace-<n>.csv` per trace holds time in
ms, current in A and the bus voltage in V.

**Write down:** the tool's output for each log. Part A answers: the noise
of CH1's 1 ms samples with no servo and with a servo at rest, against the
threshold's floor (`SERVO_MOVE_MIN_A`, 0.020 A); which filter lengths and
bands see all 22 moves of each test (2 that place the servo and 20
counted); and how much the arrival moves with the setting. It does not
answer whether an arrival is the horn's.

**Part B: with the AS5600.** The same moves with the encoder on the shaft.

1. SETUP → INTERFACES: `AS5600` ON, with the bank disarmed. On the SERVO
   screen's DUT page, with the servo at its neutral, tap ENC CENTRE.
2. Repeat part A's steps 5 to 7.
3. Keep each log together with its test's `BENCHnnn.CSV` from the SD card.
4. On the host, for each pair:

   ```bash
   python3 tools/sense_trace.py mg90s-4v8.log --servo-csv BENCH012.CSV
   ```

The tool then also prints, per move, the arrival less the encoder's
`travel angle (ms)`, and its median per setting. Part B answers what the
capture's filter length (`SENSE_CAP_FILTER_N`, 4) and arrival band
(`SERVO_MOVE_BAND_A`, 0.05 A) are to be: both are chosen, not measured.

**Not known:**

- Part B: the encoder's travel time counts from the command as the panel
  issues it, the trace's arrival from the PWM frame at the pin. The
  difference the tool prints contains the time between the two: up to one
  poll and one frame, not measured.
- A command's frame time is computed from the PWM counter read after the
  pulse is written. A frame that ends between the two puts that one
  command's time one frame (20 ms at 50 Hz) late. How often: not measured.
- Part B: the two files have different clocks. The tool pairs rows and commands by
  the spacing of the moves; when two offsets pair equally many it says so,
  exits 1 and takes `--csv-offset`.
- Whether a terminal keeps up with 8.6 kB a second without loss. A trace
  whose line counts do not match its end line is reported and exits 1.

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
