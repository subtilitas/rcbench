---
screen: none
title: Interfaces
order: 61
---

# Interfaces

INTERFACES, the second category on [SETUP](setup.md), wires the PD mini supply and says how the programming port receives a Hitec D series servo. Its 5 settings stand in two sections under the headings SUPPLY and PROG PORT; a heading is a row of its own with no keys. The settings are kept and saved like every other setting on SETUP. The panel writes each set-up to the IOmcu 500ms after the last edit, and only what differs from what the IOmcu holds. The devices the IOmcu reads are on [Sensors](setup-sensors.md).

## The PD mini

| Setting | Range | Default | What it does |
|---|---|---|---|
| PD mini | ON, OFF | OFF | the programmable supply on [SUPPLY](supply.md) and SERVO; OFF runs the panel's supply model |
| PD mini TX | -1 to 47 | -1 | the IOmcu GPIO to the module's DM |
| PD mini RX | -1 to 47 | -1 | the IOmcu GPIO from the module's DP |
| PD mini baud | 9600, 19200, 38400, 57600, 115200, 230400, 460800, AUTO | AUTO | the module's UART (universal asynchronous receiver-transmitter) rate; AUTO tries each of the 7 rates, one a second |

The `-` and `+` keys of PD mini TX and PD mini RX step over the GPIO the other row holds: with RX on GPIO 18, TX steps from GPIO 19 to GPIO 17. At either end of the range the key then does nothing: with RX on GPIO 47, TX stops at GPIO 46. The keypad refuses the other row's GPIO and shows `The other PD mini pin holds this GPIO` in place of the range. A set-up stored with both rows on one GPIO loads as stored; the PD mini stays off, the SUPPLY header says `PD MINI NO PINS`, and both rows say `TX and RX on one pin. The PD mini stays off.` until one of them is stepped. The PD mini switch turns the module off. The IOmcu refuses a pin that is reserved, bound to an output, held by the sensor bus or the phase tap, or the other pin: RX on GPIO 17 is refused while Sensor SCL is GPIO 17. The message leaves the band once the pin is set back to one the IOmcu takes. A change to the pins, the baud rate or the enable switch switches the supply output off.

![RX's GPIO 9 refused for PD mini TX](view:setup-pdmini-why)

## The programming port

| Setting | Range | Default | What it does |
|---|---|---|---|
| Hitec D receive | PULL-UP, PRECHARGE | PULL-UP | how the IOmcu receives a Hitec D series servo's answer on the programming port. PULL-UP: a 2.2kΩ resistor from the signal wire to the servo supply. PRECHARGE: no resistor; the IOmcu drives the pad high for 100ns every 1.09us and samples it 113ns later |

The IOmcu takes the value at its next OPEN of a D series line. Classic servos (HS-5xxx, HS-7xxx) need neither and ignore it. An IOmcu of 0.21.1 or 0.21.2 (protocol 4.23) receives with PULL-UP; with PRECHARGE set the row then reads `IOmcu older than 0.22.0: receives with PULL-UP.` An IOmcu older than 0.21.1 has no Hitec line; with PRECHARGE set the row reads `IOmcu older than 0.21.1: no Hitec line.`

## On the rbmod carrier

![PD mini TX and RX soldered](view:setup-rbmod-iface)

On the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier) PD mini TX reads GPIO 7 and PD mini RX GPIO 6. These rows have no keys and open no keypad, and their help line reads `Soldered on this board. Not a setting here.`. The panel writes these pins to the IOmcu. The values stored for them stay as they are and are in force again on a module wired by hand.

## Related pages

- [Setup](setup.md): keys, saving and the other categories.
- [Sensors](setup-sensors.md): the sensor bus, the current monitors, the output encoder and the phase tap.
- [Outputs](outputs.md): which pins are bound, and so not free for the parts here.
