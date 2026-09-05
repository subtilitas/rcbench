# First run on hardware

<sub>**English** · [Deutsch](FirstRun-de.md)</sub>

For the first time both boards are powered with the heartbeat wire fitted.
Written for 0.4.0. Nothing below has been done before, so
every step says what "good" looks like and what to write down when it is not.

Work down the list. Each step assumes the one above it passed.

---

## 0. Before power

**Have to hand:** an oscilloscope, one servo, one ESC that speaks
bidirectional DShot, a bench supply with current limit, and the USB cable for
each board.

**Do not connect a motor to the ESC yet.** Step 5 is the first time a pin has
ever been driven by this firmware; the first thing to look at is the scope,
not a propeller.

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

    -- rcbench: image 269828 bytes, 6% of the 4190208 bytes a four-megabyte
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

Follow **[Bringing up the link](Bringup.md)** — the CAN echo self-test. It
answers one question: do frames cross the bus intact? It uses no page
protocol, so if it passes and the link still does not work, the fault is
above the wire.

```bash
idf.py -C firmware/panel -DRCBENCH_CAN_SELFTEST=1 build flash
```

Watch the **UART socket**, not native USB: GPIO19 and GPIO20 carry both, and
the multiplexer selects one.

Good looks like:

    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0

**Do not go on until this passes.** Every step below assumes frames cross.

Then reflash the panel **without** `-DRCBENCH_CAN_SELFTEST=1`.

---

## 3. The heartbeat wire — the thing that has blocked everything

| | |
|---|---|
| Panel end | **GPIO6**, on **J8** (a three-pin header carrying 3V3, GND, GPIO6) |
| Coprocessor end | **GP3** |
| Through | the retriggerable monostable on the daughterboard |

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

Every one of these is host-tested. **None has been seen on hardware.**

---

## 6. First pin driven — ever

Use a **servo**, not the ESC. A servo is the forgiving case and the one the
scope reads most easily.

**Pins free for an output:** GP0, GP1, GP2, GP4, GP5, GP6, GP7, GP13, GP14,
GP15 and up.
**Reserved and refused:** GP3 (heartbeat), GP8–GP12 (CAN).

On the panel: **Setup → OUTPUTS**, choose `SERVO PWM`, tick one pin. Or
**Setup → PICK A PIN** for the board picture — grounds are marked `G`, rails
carry their voltage.

**Scope the pin before connecting the servo.**

| | |
|---|---|
| Frame rate | 40 to 400 Hz (50 Hz by default) |
| Pulse | 500 to 2500 µs, refused outside |
| Resolution | 1 µs |

**Measure and record:** the actual frame period, the pulse at both ends of
travel, and the jitter. Bit timings are the largest unverified surface in the
tree.

**Then:** stop commanding it and confirm the output stops driving after
**500 ms** (`OUT_DEFAULT_TIMEOUT_MS`).

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

## 8. When something goes wrong

| Symptom | Look here first |
|---|---|
| Arm refused, wire fitted | Heartbeat not edging 4 times yet, or gap over 150 ms. Scope GP3. |
| Arm refused, no obvious reason | Touch dead for 500 ms blocks it. Touch the panel. |
| Link up, screen offers no pins | The board's catalogue did not arrive. Console says which page failed. |
| Heartbeat drops in the first minute | **The artwork transfer or the flash keeper.** Newest code, never run. See §4. |
| Display freezes briefly | A flash write. Expected once, when the photograph is kept. |
| Coprocessor does not boot after flashing | Image past 4 MB. Check the size line from §1. |
| Panel boots but no `boardart` partition | Flashed app-only over an old table. Merge-bin at offset 0. |
| Output stops on its own after ½ s | Working as intended — nothing wrote to it. |

**If the bench misbehaves in a way that points at the panel**, the two newest
and least proven things are both mine and both only compiler-checked:

- the **flash keeper task** (`artkeep`), and
- the **artwork slice** inside the control task's poll loop.

Both are inert on a coprocessor that reports no photograph, which is the
quickest way to rule them out.

---

## 9. What to write down

For each step: what was measured, against what it was expected to be, and
what the scope showed. `STATUS.md` carries an "Open items" table — the rows
about drivers on hardware, the control page and the flash store are the ones
this run answers.

**Anything that is not measured stays written as not measured.** A number
guessed into that table is worse than an empty cell.
