# The sensor nodes

<sub>**English** · [Deutsch](Nodes-de.md)</sub>

A sensor node is an ESP32-C6 board on its own LiPo cell that sends finished
readings over Bluetooth LE (Low Energy, BLE). It has no wire to the bench.
The power node reads the INA228 of a MATEK I2C-INA-BM on the ESC's
(electronic speed controller's) XT60 power path. The balance node will
carry the vibration sensor and the index sensor; its firmware is a stub.

The power node is a measurement, not a protection. No limit, trip or error
path of the bench reads it, as none reads the wired INA228 today.

Waveshare, MATEK, GERUI and Texas Instruments are names of their owners, and
Bluetooth is a trademark of its owner. rcbench is independent: it is not
affiliated with, endorsed by or sponsored by them. The names say which
parts the node is built from.
[Legal notice and disclaimer](../README.md#legal-notice-and-disclaimer) holds
the project's notice.

| Part | Where |
| --- | --- |
| the packets, the UUIDs (universally unique identifiers), the control writes, the advertising, the 20ms windows | `protocols/rcnode`, no pin driver |
| start-up, the once-a-second step, sleep and wake | `firmware/node/main/node_main.c` |
| the battery and sleep decisions, under the host suite | `firmware/node/main/node_policy.c` |
| the GATT (generic attribute profile) server and the advertising on NimBLE | `firmware/node/main/node_ble.c` |
| the power role: the INA228, the windows, the accumulators | `firmware/node/main/node_pwr.c` |
| the INA228 driver, the IOmcu's own | `shared/sense/ina228.c`, `shared/sense/sense_bus.c` |
| the balance role, a stub | `firmware/node/main/node_bal.c` |
| the battery reading | `firmware/node/main/node_bat.c` |
| the console | `firmware/node/main/node_console.c` |

## The power node's parts

| Part | Use |
| --- | --- |
| Waveshare ESP32-C6-Zero | ESP32-C6FH4 with 4MB flash in the package, USB-C (USB Serial/JTAG), LDO (low-dropout regulator) from the 5V pin to 3.3V: ME6217C33M5G on Waveshare's schematic, ME6217C33M6G on the seller's listing; the part on the delivered boards is not confirmed, WS2812 RGB LED on GPIO8, BOOT key on GPIO9, RESET key |
| GERUI CD42 charge and boost module | charges the cell from VIN (4.5V to 5V) and boosts it to 5V on OUT |
| a 1S LiPo cell | on the module's BAT |
| MATEK I2C-INA-BM | INA228, 200µΩ shunt, address 0x45, XT60 in and out |
| 2 × 200kΩ, 1 × 100nF | the battery divider |
| 2 × 2.2kΩ | I2C (Inter-Integrated Circuit) pull-ups to 3.3V |

The board's USB VBUS reaches its 5V pin through a B5819WS Schottky diode
(the board's schematic). With the module's OUT on the 5V pin, nothing feeds
back into a USB cable.

## Wiring

| From | To | Note |
| --- | --- | --- |
| cell + | CD42 BAT+ | |
| cell − | CD42 BAT− | |
| CD42 OUT 5V | ESP32-C6-Zero 5V | the board's LDO makes 3.3V |
| CD42 OUT GND | ESP32-C6-Zero GND | |
| CD42 BAT+ | 200kΩ to GPIO3 | the top of the divider |
| GPIO3 | 200kΩ to GND, 100nF to GND | the bottom of the divider; GPIO3 is ADC1 channel 3 |
| ESP32-C6-Zero GPIO19 | MATEK SDA | 2.2kΩ from SDA to 3V3 |
| ESP32-C6-Zero GPIO18 | MATEK SCL | 2.2kΩ from SCL to 3V3 |
| ESP32-C6-Zero 5V | MATEK 5V | the INA228's supply |
| ESP32-C6-Zero GND | MATEK G | the node's only ground |

The lead to the MATEK is 10cm or shorter. The bus runs at 400kHz. The
firmware switches the pads' own pull-ups off; the bus runs on the 2.2kΩ
pull-ups and the module's own.

The divider draws 10.5µA from the cell at 4.2V, also while the node is
asleep or off.

**Before the first connect.** The rail the MATEK's I2C pull-ups go to is not
known. Power the MATEK alone from 5V, leave SDA and SCL open, and measure
each against G: both must read 3.3V or less. A higher reading needs a level
shifter; the ESP32-C6's pins take 3.3V.

### The ground rule

The node's GND is the MATEK's G, and G is the pack's negative. The cell's
negative is the node's GND too. The node has no other wire: the measured
path is separate from the bench.

- No USB cable on the node while a pack is on the XT60. The cable's ground
  is then the pack's negative, and a laptop or the panel on the same USB tree
  closes a loop through the ESC's return.
- Charge the node with the XT60 unplugged, or from a power bank with nothing
  else on it.
- The node's case insulates the board from the shunt side of the module.

## The CD42 module

The GERUI "5V 2A Laderegler" carries a chip marked CD42 and a 4-LED charge
indicator (25%, 50%, 75%, 100%).

| Terminal | Use |
| --- | --- |
| VIN | 4.5V to 5V from a source of 2A to 3A, to charge |
| BAT | the 3.7V cell |
| OUT | 5V, boosted from the cell |
| KEY | a button |

Not known, and measured on the first module:

- the light-load auto-shutdown threshold: the load current under which the
  module switches OUT off;
- the module's quiescent current at the cell;
- whether KEY or a load starts the boost.

**What the sleep policy assumes.** The module keeps OUT at 5V while the node
sleeps. The sleep load on OUT is the ESP32-C6 in deep sleep (7µA, the chip's
figure), the board's LDO and WS2812 at idle (not measured), the MATEK's own
regulator if it has one (not known) and the INA228 shut down (5µA). If the
module switches OUT off under this load, the node goes dark at its first
sleep: the 10s timer never runs, the RESET key does nothing, and only what
starts the module's boost again (KEY, or a charger on VIN, not known which)
brings the node back.

**How to measure it.**

1. A meter in series in OUT: the current with the node awake, advertising,
   and connected.
2. On the console, `sleep`. A meter on OUT for 2min: OUT stays at 5V, or it
   drops.
3. The LA2016 (logic analyser) on SCL: a bus voltage read every 10s shows the
   node waking on its timer.
4. The threshold: OUT loaded with resistors, the node off OUT. The lowest
   current that holds OUT on for 5min.
5. The quiescent current: a meter in the cell's lead, OUT without a load.

If OUT switches off in sleep, the choices are a bleed resistor on OUT that
holds the load above the threshold (it costs the cell that current), or a
slide switch in the cell's lead and no sleep. The choice waits for the
figures.

## Sleep and wake

| Condition | What the node does |
| --- | --- |
| no link, no pack on the XT60 and no `stay on` for 120s | deep sleep with a 10s timer; the balance node without a timer |
| SLEEP written to CONTROL, or `sleep` on the console | the same, at once |
| a timer wake | the INA228 converts the bus voltage once (540µs), the node reads it and shuts the part down; 5.0V or more starts the node in full, less sleeps again |
| a cell between 1.0V and 3.3V, 3 readings in a row, counted across timer wakes | off: deep sleep without a timer |
| OFF written to CONTROL, or `off` on the console | off, at once |
| the RESET key, a power cycle, a flash | a full start |

The count of low readings is kept in RTC (real-time clock) memory through
deep sleep, so one low reading at a timer wake switches nothing off; a
good reading clears the count. A divider reading under 1.0V is no cell,
the board on USB alone, and switches nothing off. Under 3.5V STATUS
reports the battery low. A failed battery read (the ADC or its
calibration fails) sets STATUS flag 0x0040 with a cell voltage of 0, prints
`bat error` on the console, and neither counts towards the switch-off nor
clears the count. Before a
sleep the node ends the link, waits 200ms, and writes the INA228's
ADC_CONFIG to shutdown.

The BOOT key is GPIO9, which cannot wake the ESP32-C6 from deep sleep (only
GPIO0 to GPIO7 can). The RESET key is the wake button. The balance node has
no sensor that tells it to wake: asleep, it sleeps until the RESET key.

A timer wake loads the image without checking it again
(`CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`). The time a timer wake
takes and the node's mean current asleep are not measured. The plan's
estimate is 100µA: 30ms awake at 30mA every 10s, plus the current asleep.

## The INA228

The node writes the bench's own set-up (`INA228_ADC_BENCH`): bus and shunt
voltage 540µs each, temperature 50µs, averaging 1, a result every 1130µs.
ADCRANGE 1 (±40.96mV) for 204.8A on the 200µΩ shunt: 390.625µA a step.
The driver is the IOmcu's.

| Every | Read | Into |
| --- | --- | --- |
| 1130µs | CURRENT and VBUS | the 20ms window |
| 20ms | | a window packet: mean and highest current, mean and lowest voltage, the sample count, clip flags |
| 40ms | ADC_CONFIG, against the value written | a part found reset is set up again 1000ms on and counted |
| 500ms | DIAG_ALRT, then CHARGE and ENERGY (raw, 40 bits; the reads clear their overflow flags), DIETEMP | an accumulator packet, and the run extremes since the last RUN write |

A window holds 17 or 18 samples. A current sample at either end of the
20-bit range is a clip: counted and flagged, never a value. The node never
clears the accumulators. The part clears them at its own reset and at
every set-up, and the accumulator packet's epoch counts the set-ups. After
CHARGE and ENERGY the node reads ADC_CONFIG back once more. When it does
not match, the part reset before or between the reads: both values are
dropped, their flags in the packet are clear, and the part is set up again.
The panel treats a fall in ENERGY under the same epoch as a restart too
(`rcnode_acc_restarted()`). The packets are in
[protocols/rcnode](../protocols/rcnode/README.md).

**What a set-up again loses.** Every set-up writes RSTACC, so CHARGE and
ENERGY counted since the last accumulator packet the panel took are gone:

| Cause | Lost charge and energy |
| --- | --- |
| the part reset itself | from the last good accumulator packet to the set-up: up to 500ms to the next read, 40ms to the read-back that catches the reset, and 1000ms to the set-up, 1.54s at most |
| 3 failed reads in a row with no reset (the bus, not the part) | from the last good accumulator packet to the set-up: up to 500ms, the time the reads fail, and 1000ms |
| the part does not answer the set-up | the set-up is tried again every 1000ms, and each try adds 1000ms |

The window packets carry current and voltage through such a gap; the
panel can count the gap from them.

## Bluetooth LE

The node advertises as `rcbench-pwr-XXXX`, the last two bytes of its address
in hex, every 100ms while no link stands. The manufacturer data carries the
role; the scan response carries the node service's UUID. One central at a
time, no pairing, no bonding. The node asks for an ATT (attribute protocol)
MTU (maximum transmission unit) of 247 and for no connection parameters of
its own.

| Characteristic | Properties | Content |
| --- | --- | --- |
| STREAM | NOTIFY | windows every 20ms, accumulators and run extremes every 500ms |
| STATUS | READ, NOTIFY | battery, flags, firmware version, counters, every 1000ms |
| CONTROL | WRITE, WRITE WITHOUT RESPONSE | RUN with a tag of 1 to 255, SLEEP, OFF; BLADES (1 to 12) and RPM on the balance node |
| INFO | READ | protocol, role, board, address, the INA228's set-up |

A write the role does not take, or of another length, is refused with ATT
error 0x13 or 0x0D.

STREAM packets wait in a queue of 32 for the sender. A full queue, a stack
without a buffer, or a packet longer than the link's MTU less 3 bytes
drops the packet and counts it in STATUS's `dropped`. The stack would cut
a longer notification without an error; the power node's packets are 20
bytes, which fit the smallest MTU of 23, and the balance report is 24
bytes, which needs 27.

## Building and flashing

The role is chosen at build time (`main/Kconfig.projbuild`). A board is
wired for one sensor, so the role is part of the image, not a stored
setting that a full flash erase loses.

```sh
. ~/esp/esp-idf-v6.1/export.sh
cd firmware/node
idf.py -B build-pwr -D SDKCONFIG=build-pwr/sdkconfig build
idf.py -B build-pwr merge-bin -o rcbench-node-pwr-merged.bin
```

The balance stub:

```sh
idf.py -B build-bal -D SDKCONFIG=build-bal/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.balance" build
```

A release carries `rcbench-node-pwr-<version>.bin`, the application, and
`rcbench-node-pwr-merged-<version>.bin`, which goes to 0x0. The CI
(continuous integration) job "Node image" builds them.

To flash, take the pack off the XT60 first (the ground rule), then connect
the node's USB-C:

```sh
python -m esptool --chip esp32c6 -p /dev/ttyACM0 -b 460800 \
  write-flash 0x0 rcbench-node-pwr-merged-<version>.bin
```

A node asleep does not answer on USB. Press RESET to start it, or hold BOOT
and press RESET for the ROM's download mode.

## The console

USB Serial/JTAG on the board's USB-C. A line every second:

```text
I (5123) node: pwr 50 win/s, n 18, 22.18V 14.300A, part online, epoch 1; link up -58dBm, sub 1, bat 3.92V, errors 0, resets 0, dropped 0
```

| Field | Meaning |
| --- | --- |
| `win/s` | windows closed in the last second, 50 when every window has a sample |
| `n` | samples in the last window |
| the volts and amperes | the last window's means |
| `part` | `online`, or `not answering` |
| `epoch` | set-ups of the INA228 since the start |
| `link` | `none`, `up` and the RSSI (received signal strength indication) |
| `sub` | 1 when STREAM is subscribed |
| `bat` | the cell; under 1.00V without a cell; `error` when the read failed |
| `errors`, `resets`, `dropped` | STATUS's counters |

| Command | Does |
| --- | --- |
| `status` | the name, the firmware, the role and the last status in full |
| `stay on`, `stay off` | holds the node awake, or lets it sleep again; until a restart |
| `sleep` | deep sleep at the next second, with the 10s check on the power node |
| `off` | off, at the next second |

The start prints `rcbench node <version>, role pwr, <n> timer wakes before
this start`.

## Current limitations

- The panel does not connect to a node. The BLE hub and the panel side are
  later slices; a phone's BLE scanner shows the service and the stream.
- No node has run on hardware: the boards are ordered, not delivered. The
  pins, the wake, the currents and the CD42's behaviour are not measured.
- The CD42's light-load auto-shutdown threshold is not known (above).
- The balance role is a stub: it advertises as `rcbench-bal-XXXX`, takes
  BLADES and RPM, sends no report, and sleeps until the RESET key.
- The WS2812 is not driven. Its current at idle is not measured.
- The firmware is updated over USB only.
- Windows without a sample between two windows are not sent: a stalled
  sampler shows as a jump in `t_node` with no jump in `seq`.
