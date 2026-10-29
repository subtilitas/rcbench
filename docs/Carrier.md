**English** | [Deutsch](Carrier-de.md)

# The rbmod carrier

The rbmod carrier is a board that holds the Waveshare RP2350-CAN module and
wires every lead to a fixed GPIO (general-purpose input/output). It is a
fixed configuration: no pin is chosen on the panel. The IOmcu on the carrier
runs an image of its own, `rcbench-iomcu-rbmod`, and reports board 2 on
IDENTITY register 5 ([Link: Boards with soldered
pins](Link.md#boards-with-soldered-pins)).

## Pins

| Signal | GPIO | Connector |
| --- | --- | --- |
| PROGP01, PROGP02, PROGP03, PROGP04 | 13, 14, 15, 0 | J1 |
| PWM01, PWM02, PWM03, PWM04 | 2, 4, 5, 1 | J2 |
| SDA01 / SCL01, the sensor bus (I2C0) | 16 / 17 | J5 |
| SDA02 / SCL02 (I2C1) | 18 / 19 | J4 |
| PDMINI TX / RX | 7 / 6 | J6 |
| TAP | 22 | J9 |
| PROGP_5V_EN, the enable of the programming ports' 5V | 21 | none |
| HEARTBEAT | 3 | J3 |
| CAN (Controller Area Network) | 8 to 12 | on the module |
| unused, reserved | 20, 26, 27, 28 | none |

GPIO 26 to GPIO 28 are ADC (analog-to-digital converter) pins and not 5V
tolerant. No servo line is on them.

The second I2C (Inter-Integrated Circuit) bus on J4 is not driven in this
release.

## Connectors J1 and J2

Each position is signal, + and GND. Every signal has a 120Ω series
resistor.

| J1 position | Signal | GPIO | + rail |
| --- | --- | --- | --- |
| 1 | PROGP04 | 0 | VCC_PROGPORT |
| 2 | PROGP01 | 13 | VCC_PROGPORT |
| 3 | PROGP02 | 14 | VCC_PROGPORT |
| 4 | PROGP03 | 15 | VCC_PROGPORT |

| J2 position | Signal | GPIO | + rail |
| --- | --- | --- | --- |
| 1 | PWM03 | 5 | VCC_SERVOPORT_B |
| 2 | PWM02 | 4 | VCC_SERVOPORT_B |
| 3 | PWM01 | 2 | VCC_SERVOPORT_A |
| 4 | PWM04 | 1 | VCC_SERVOPORT_A |

The position order is set for the layout.

PWM02 (GPIO 4) and PWM03 (GPIO 5) are one PWM (pulse-width modulation)
slice and run at one frame rate. PWM04 (GPIO 1) shares slice 0 with
PROGP04 (GPIO 0). A programming port carries no PWM signal on the carrier,
so the shared slice limits nothing.

## Power

| Part | Input | Output | Switched by |
| --- | --- | --- | --- |
| M6, USB-C PD (Power Delivery) trigger | USB-C | the +15V net | its DIP switch sets 5V, 9V, 12V, 15V or 20V |
| M7, buck | the +15V net | 5V, VCC_PROGPORT_DCDC: the programming ports on J1 | EN on GPIO 21, with a pull-down on EN |
| M8, buck | the +15V net | +5V: the module's VBUS and J7 | always on |
| PD mini, on J8 | its own input | VCC_PDMINI: the servo rails A and B on J2 | the SUPPLY page |

The programming ports run from M7's fixed 5V, not from the PD mini. KST and
Chaservo servos program at 5V. On the carrier the programmers do not set the
programming ports' voltage.

The IOmcu drives GPIO 21 low from the first statement of `main()` and keeps
it low. While the IOmcu does not drive GPIO 21 (in reset, in BOOTSEL,
unpowered), the pull-down on EN holds M7 off.

Current limitation: this release never switches GPIO 21 on, so the
programming ports have no supply from the carrier.

## The current monitor

An INA3221 (M5) on the sensor bus at address 0x40 watches the three
supplies. Its VS is +3.3V, and the carrier ties its VPU to VCC. The module's I2C
pull-ups hang on VPU, so they are bus 1's pull-ups: the firmware runs bus 1
(GPIO 16 SDA, GPIO 17 SCL) with the RP2350's pad pulls off.

| Channel | From | To |
| --- | --- | --- |
| CH1 | VCC_PDMINI | VCC_SERVOPORT_B, J2 positions 1 and 2 |
| CH2 | VCC_PDMINI | VCC_SERVOPORT_A, J2 positions 3 and 4 |
| CH3 | VCC_PROGPORT_DCDC (M7) | VCC_PROGPORT, J1 |

The channels are set up on SETUP > SENSORS as on any bench in this release.

## The image

| Build | Files | Release asset |
| --- | --- | --- |
| `cmake -S firmware/iomcu -B firmware/iomcu/build-rbmod -DIOMCU_BOARD=rbmod` | `rcbench-iomcu-rbmod-install.uf2`, `rcbench-iomcu-rbmod.uf2`, `rcbench-iomcu-rbmod-pt.uf2`, `rcbench-iomcu-rbmod.bin`, `rcbench-iomcu-rbmod.elf` | `rcbench-iomcu-rbmod-vX.Y.Z.zip` |

The first load is the install UF2 (USB Flashing Format) over USB, as in
[First run](FirstRun.md#from-the-release-assets). After that the panel
offers each IOmcu the image of the board it reports.

## Rev 1 rework

Every rev 1 board needs two changes by hand. The carrier's image assumes
both are done. A rev 2 board carries the two changes in copper.

| # | Change | Reason | The firmware relies on it |
| --- | --- | --- | --- |
| 1 | PROGP03 (J1 position 4, through R4) moves from GPIO 26 to GPIO 15: the trace is cut under the series resistor R4, and a bodge wire runs from R4 to GPIO 15. | GPIO 26 is an ADC pin and not 5V tolerant. A Hitec or KST pull-up puts the servo supply on the programming line. | PROG PORT 3 is GPIO 15 on every carrier. GPIO 26 stays unused. |
| 2 | A pull-down of 2kΩ to 5kΩ from M7's EN (the GPIO 21 net) to GND. | The buck module's EN reads open as on. Without the pull-down the programming ports' 5V is on while the IOmcu is in reset, in BOOTSEL or unpowered. | The programming ports' supply is off whenever GPIO 21 is not driven high. |
