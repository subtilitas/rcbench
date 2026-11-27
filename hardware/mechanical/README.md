# Mechanical models

STEP (ISO 10303-21) and STL models of the bench's purchased parts, its
boards and its printed cases, for designing enclosures and panels and for
the video. Units are mm. STL files are for printing; each comes with the
STEP file of the same name.

The models are written by generators that are maintained outside this
repository. The files here are the released output: edit them only
through a new release of the generators.

![Mechanical models](renders/all.png)

Each model has a render under `renders/<folder>/<stem>.png`; the cases
and the bench have further views beside it. "Box" below is the model's
bounding box, x by y by z.

## Boards (`models/pcb/`)

| Files | Content | Box, mm |
| --- | --- | --- |
| `boost_5v_en.step` | boost board: 4 plated corner holes, enable pad, chip (SOIC-8), inductor, 2 diodes (SMA), 3 capacitors, 3 resistors, print, underside's white field; 69 solids, 0.6MB | 13.7 x 21.7 x 4.6 |
| `charger_boost_2s4s.step` | 2S to 4S USB-C boost charger, variant 4S 1A: USB-C socket, inductor, diode, 2 SOP-8 chips, shunt R100, capacitors, 2 LEDs, BAT and GND slotted pads, 2 holes of 3.1mm, print of both sides with the 2S/3S/4S and 1A/2A/4A jumpers; 225 solids | 17.9 x 38.2 x 5.2 |
| `core2350b.step` | Waveshare Core2350B (RP2350B on a 25.4mm square board) | 25.4 x 25.4 x 5.4 |
| `core2350b_usb_adapter.step` | the Core2350B's USB adapter board: USB Type-C socket, 6-way flat cable socket, BOOT and RESET buttons, LED, 5 chip parts, 4 holes of 2.0mm at 14.0mm, the slots of the socket's legs, print BOOT, RESET, PWR; 70 solids | 18.0 x 18.0 x 4.3 |
| `dcdc_buck_mini.step` | Mini560 buck converter, blue: 4 corner pads with 2 holes each, enable hole, inductor 4R7, 6 capacitors 1206, controller, 10 chip parts, LED; 137 solids, 1.3MB | 30.0 x 18.0 x 5.5 |
| `es3c28p.step` | LCDWIKI ES3C28P, the display node board: the ES3C28P: board with 4 holes, window and slot of the touch cable; touch panel with its black border, optical glue, display module, 2 strips of tape, both cables' folds round the display's end and the touch cable's run to its socket; on the back the ESP32-S3, flash, codec, amplifier, RGB LED, crystal, microphone, 6 SOT-23 packages, a diode, 103 chip parts, USB Type-C, microSD, 6-way flat cable socket, 5 wire sockets of 1.25mm pitch, BOOT and RESET, the touch controller and 6 chip parts on its cable; print of both sides; 767 solids, 5.8MB | 50.0 x 86.4 x 10.6 |
| `esp32_c6_zero.step` | Waveshare ESP32-C6-Zero: 18 edge pads with holes and half holes, 7 underside pads, USB Type-C socket, chip (QFN, 40 leads), antenna, 2 buttons, LED, regulator, diode, crystal, 21 chip parts, print; 263 solids, 2.6MB | 18.0 x 24.7 x 4.9 |
| `esp32_s3_lcd_185.step` | Waveshare ESP32-S3-Touch-LCD-1.85: round 1.85" display, ESP32-S3, sockets; measured from Waveshare's STEP file | 55.0 x 55.0 x 9.7 |
| `ina219_cjmcu219.step` | INA219 board, print CJMCU: 12 plated holes, shunt R100 (2512), INA219 (SOT-23-8), 5 chip parts (0805), 4 bridge pads, print of both sides; 225 solids, 3.5MB | 24.2 x 21.9 x 2.7 |
| `ina228_node_layout.step` | remote INA228 sensor node layout: INA228 module, buck converter, display board; 1139 solids, 10.3MB | 86.4 x 50.0 x 23.7 |
| `ina228_xt90.step` | INA228 module with 2 XT90 connectors: board with 59 holes, XT90PW-M, XT90PW-F, solder on the bars of both sides and round the tails, shunt, INA228, 13 chip parts, 4-pin header, bare pads, print; 225 solids, 1.7MB | 83.4 x 48.6 x 12.6 |
| `ina3221_module.step` | INA3221 board, black: 20 plated holes, INA3221, 3 shunts R100, 5 LEDs, 28 chip parts, 4 pairs of bridge pads, hole names on the underside; 299 solids, 3.3MB | 38.0 x 31.8 x 2.5 |
| `lipo_charger_boost_5v.step` | charger board: 6 plated holes, chip (SOIC-8), inductor, 6 capacitors, 4 resistors, 4 LEDs, side push button, underside's white field with black names; 121 solids, 1.3MB | 16.6 x 24.0 x 4.6 |
| `matek_mbec12s.step` | Matek mBEC12S, black: 4 edge pads OUT, G, G, IN 2.54 apart, inductor with the print 100, diode SS56, converter chip, 3 capacitors, LED, 13 chip parts, 2 voltage pads, the print of both sides; 147 solids, 1.8MB | 18.0 x 15.0 x 4.6 |
| `panel_7b.step` | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, the panel board: glass, display, board, 4 standoffs, 2 flat cables and every part on the board; 1497 solids, 7.5MB | 166.0 x 99.2 x 15.1 |
| `pd_trigger_pd31.step` | purple USB-C PD3.1 trigger, low-voltage model: USB-C socket, 2 x 2mm chip, chip parts, jumper pads 1 to 3, + and - holes 7.9mm apart, the underside's voltage table (A: 5 to 20V, B: 5 to 48V); 110 solids | 14.5 x 10.0 x 4.3 |
| `rbmod_v2.step` | rbmod carrier board, version 2, assembled: the same with 3 JST PH headers of 4 pins and 2 of 2 pins, 2 angled pin headers of 3 x 4 pins and the 5 modules, each part by part; 1920 solids, 27.3MB | 85.3 x 89.0 x 19.0 |
| `rbmod_v2_smd.step` | rbmod carrier board, version 2, bare with its surface-mounted parts: the board with 129 holes, the copper of its 2 outer layers (280 pads, 50 areas of tracks and vias) and its 17 surface-mounted parts: 9 resistors 1206, 3 resistors 0402, 2 capacitors 0603, 1 capacitor 1210, 1 SMB diode, 1 SMC diode; 382 solids, 10.1MB | 80.0 x 89.0 x 4.1 |
| `rp2350_can.step` | Waveshare RP2350-CAN, blue: 40 edge pads, USB-C socket, 2 buttons, RP2350A, XL2515, SIT65HVD230, flash, buck converter, 2 crystals, slide switch, 3-way screw terminal, 52 chip parts, 3 wire openings, pad names on the underside; 632 solids, 7.3MB | 21.0 x 52.6 x 11.9 |
| `usbc_pd_trigger.step` | USB-C PD trigger board, purple: USB-C socket, 3-way switch, 10-lead chip, regulator, 2 capacitors 1206, 9 chip parts, LED, the voltage table on the underside; 158 solids, 1.9MB | 31.1 x 20.0 x 4.9 |
| `usbc_plug_breakout.step` | USB-C plug on a green breakout board: steel shell with the black insulator, black collar with steel bands, 24 tails on 12 pads each side, 4 wire pads (G, D+, D-, one unlabelled), R1 (0603); 76 solids | 15.3 x 8.4 x 2.8 |

## Cases and printed parts (`models/casing/`)

| Files | Content | Box, mm |
| --- | --- | --- |
| `dial_base_bottom.step`, `dial_base_bottom.stl` | hand-held base for the M5Stack Dial, bottom part: floor with the cell's pocket, centring rim with a notch at each boss, 4 screw columns; 15.4cm3 | 77.9 x 77.9 x 15.5 |
| `dial_base_top.step`, `dial_base_top.stl` | hand-held base for the M5Stack Dial, top part: the dial's seat: top plate with the 45.2mm hole, wall, 4 bosses for threaded inserts, USB opening with its guide tube; 14.2cm3 | 71.9 x 71.9 x 19.0 |
| `display_node_box.step`, `display_node_box.stl` | display node case, box | 59.8 x 101.2 x 16.0 |
| `display_node_lid.step`, `display_node_lid.stl` | display node case, lid | 59.8 x 101.2 x 6.0 |
| `display_node_xt60_box.step`, `display_node_xt60_box.stl` | display node case with XT60 seat, box | 59.8 x 105.6 x 22.6 |
| `display_node_xt60_lid.step`, `display_node_xt60_lid.stl` | display node case with XT60 seat, lid | 59.8 x 105.6 x 6.0 |
| `flap_drive.step` | KST X10 flap drive: plate, 3 inserts, 3 washers, 3 screws, 2 blocks, strut, bush and its screw, double arm, pin, rod end, rod, fork, 2 horn plates, horn pin; 23 solids | 124.8 x 60.9 x 17.5 |
| `ina228_node_bezel.step`, `ina228_node_bezel.stl` | INA228 sensor node case, bezel round the display | 92.4 x 61.4 x 8.5 |
| `ina228_node_frame.step`, `ina228_node_frame.stl` | INA228 sensor node case, frame with the I2C pocket | 91.4 x 58.4 x 20.6 |
| `ina228_node_plate.step`, `ina228_node_plate.stl` | INA228 sensor node case, bottom plate | 92.4 x 61.4 x 4.0 |
| `ina228_xt90_case.step` | case of the INA228 XT90 module: printed shell with 4 standoffs, 4 M3 screws; 17 solids | 85.1 x 53.6 x 11.6 |
| `knob_bezel.step`, `knob_bezel.stl` | rotary knob, bezel: the skirt's top, 2.7 high: a lip with an opening of 47 under the window, the window's seat 49.7 across, a spigot inside the skirt with 0.1 play; the finger, a tab under the lip 6 wide from r 18.5, its underside 0.15 over the display's glass; 3.9cm3 | 64.0 x 64.0 x 4.2 |
| `knob_carrier.step`, `knob_carrier.stl` | rotary knob, display carrier: plate 54 across, 2.0 thick, with 3 countersunk holes for M2 into the display's inserts; a spigot 13.6 / 10.0 under it, 4.5 long, with the bayonet's 3 lugs; 4.7cm3 | 54.0 x 54.0 x 6.5 |
| `knob_case.step`, `knob_case.stl` | rotary knob, case: the base's wall from the joint to the case's rim, open at the top; floor with a ring over the bottom part's rim, 4 bosses for threaded inserts, a cable hole of 14 and a wire hole of 5; the shaft, a tube 20 / 14 with a collar 21.6 under the lower bearing, up to 9.0 over the rim, with the bayonet's 3 L-shaped slots in its bore at the top; the wall the AS5600 module is screwed to; 22.1cm3 | 71.9 x 71.9 x 28.0 |
| `knob_clamp.step`, `knob_clamp.stl` | rotary knob, clamp sleeve: sleeve 21.6 / 20, 8.0 long, from the upper bearing to the carrier; 0.4cm3 | 21.6 x 21.6 x 8.0 |
| `knob_rotor.step`, `knob_rotor.stl` | rotary knob, rotor: hub 38 / 27 with a shoulder of 24 between the bearings' outer rings and 22 magnet pockets; a web over the sensor; a skirt 64 / 60 up to the bezel; 25.0cm3 | 64.0 x 64.0 x 37.0 |
| `knob_spacer.step`, `knob_spacer.stl` | rotary knob, spacer sleeve: sleeve 21.6 / 20, 6.0 long, between the bearings' inner rings; 0.3cm3 | 21.6 x 21.6 x 6.0 |
| `rcbench_case.step` | bench enclosure, the owner's Onshape export: assembly `rcbench_case` of 4 solids in 2 levels, 989610 bytes; each solid is valid by OpenCASCADE's check | 172.0 x 109.8 x 37.0 |

## Parts and assemblies (`models/parts/`)

| Files | Content | Box, mm |
| --- | --- | --- |
| `acrylic_disc_49_5.step` | clear acrylic disc, 49.5mm across, 2mm thick | 49.5 x 49.5 x 2.0 |
| `bearing_6704_2rs.step` | ball bearing 6704-2RS, 20 x 27 x 4mm | 27.0 x 27.0 x 4.0 |
| `esc_motor_set.step` | the ESC (electronic speed controller) motor bench: ESC, motor and motor simulator | 398.2 x 35.2 x 54.5 |
| `estop_m16_assembly.step` | M16 emergency stop button with nut and shim: all three on a 1.6mm panel | 24.9 x 24.9 x 50.1 |
| `estop_m16_button.step` | M16 emergency stop button: button without nut and shim | 24.9 x 24.9 x 50.1 |
| `estop_m16_nut.step` | M16 emergency stop button, nut: hex nut, 19.0mm across flats, 3.0mm | 19.0 x 21.9 x 3.0 |
| `estop_m16_shim.step` | M16 emergency stop button, shim: square anti-rotation shim, 20.4mm side, bore 16.2mm, 0.8mm, tab 1.4mm tall | 20.4 x 20.4 x 2.2 |
| `flap_drive_assembly.step` | KST X10 flap drive with wing: the two files above and the servo in its place; 55 solids, 4.0MB | 230.0 x 88.0 x 56.4 |
| `flap_drive_wing.step` | wing section for the flap drive: wing shell, flap shell, cover of the servo's opening, spar web, rear web, the flap's nose wall, fairing over the rod's slot; 7 solids | 230.0 x 88.0 x 56.4 |
| `flycolor_flydragon_lite_40a.step` | Flycolor FlyDragon Lite 40A ESC | 263.0 x 26.9 x 15.5 |
| `hitec_d485hw.step` | Hitec D485HW servo: case of 3 shells, 2 ears, boss, shaft, horn HD-X25 with 16 holes, type plate, print "D485HW" on the plate and on 2 sides, lead bushing, 3 wires, plug; 37 solids, 0.6MB | 126.4 x 36.0 x 46.4 |
| `knob_as5600.step` | rotary knob, AS5600 encoder module: the AS5600 module where it stands | 3.1 x 22.5 x 22.5 |
| `knob_magnets.step` | rotary knob, magnet ring: the 22 magnets where they sit, N red and S blue | 38.0 x 37.7 x 1.5 |
| `kst_x10.step` | KST X10 servo: the servo: case with red band, 2 ears, tab, shaft, print, 3 wires 15mm long; 25 solids | 51.0 x 45.2 x 10.0 |
| `lipo_102540_1000mah.step` | lithium polymer cell 1000mAh: cell, print 102540 3.7V 1000mAh: pouch, head under black tape, print, 2 wires, plug; 28 solids | 89.4 x 41.1 x 10.0 |
| `lipo_702030_380mah.step` | lithium polymer cell 380mAh: cell, print 702030 3.7V 380mAh 1.406Wh: pouch with folded side seams, head under amber tape, print, 2 wires, plug; 40 solids | 84.4 x 31.6 x 7.0 |
| `m5dial.step` | M5Stack Dial: dial without the nut, 37 solids, 4.4MB | 50.1 x 50.1 x 32.7 |
| `m5dial_nut.step` | M5Stack Dial, ring nut: ring nut, 50.82mm across, 5.0mm, 20 notches, 0.6MB | 51.1 x 51.1 x 5.2 |
| `motor_simulator.step` | motor simulator: 3 resistors in a triangle | 51.1 x 20.0 x 62.7 |
| `servo_standard.step` | standard-size servo: case of 3 shells, 2 ears, boss, shaft hub, horn with 13 holes, print on 2 sides; 34 solids, 0.7MB | 54.9 x 21.0 x 44.1 |
| `supvan_t50m_pro.step` | Supvan KATASYMBOL T50M Pro label printer: lid and base with the joint, sloping top, ribbed grip, orange lid lever, USB-C socket, label slit, control panel with power and print buttons and the LED bar, 4 feet, end slot; 87 solids | 88.8 x 120.8 x 56.0 |
| `tmotor_at2814.step` | T-Motor AT2814 brushless motor | 135.2 x 35.2 x 54.5 |
| `wit_imu.step` | WitMotion BWT901CL inertial sensor: base with 2 slotted ears, body, label with its print; 51 solids | 36.1 x 51.5 x 15.0 |
## Licence

The models and renders are under CC BY-NC-SA 4.0
([LICENSE](LICENSE)). M5Stack's Dial mesh and the two Dial STEP files
fitted to it keep M5Stack's MIT licence
([vendor/m5stack_dial/LICENSE](vendor/m5stack_dial/LICENSE)).
