# KiCad library of the rcbench modules

Footprints and symbols for the modules the bench is built from, for a carrier
PCB (printed circuit board) that takes them. KiCad 10 format: footprint
version 20260206, symbol library version 20251024. KiCad 10.0.7 or newer
loads both files. KiCad 9 and older refuse both files.

| Path | Content |
| --- | --- |
| `rcbench_modules.pretty/` | 12 footprints, one `.kicad_mod` each |
| `rcbench_modules.kicad_sym` | 12 symbols; each Footprint field names `rcbench_modules:<footprint>` |
| `modules/*.py` | one spec per module: geometry, pads, drawings, symbol, and in its docstring the source, scale, accuracy and unknowns |
| `tools/kicad_gen.py` | writes footprints and symbols from a spec |
| `tools/gen_all.py` | writes the library from all specs; `--check` fails on drift |
| `tools/render.py` | PNG preview of a spec, or the footprint drawn over a photo |
| `tools/kicad_render.py` | loads the library with kicad-cli and renders every footprint and symbol to PNG |

The `.kicad_mod` and `.kicad_sym` files are generated. Edit the spec, then
run the generator:

```sh
python3 hardware/kicad/tools/gen_all.py          # write
python3 hardware/kicad/tools/gen_all.py --check  # CI: exit 1 on drift
KICAD_CLI=kicad-cli python3 hardware/kicad/tools/kicad_render.py /tmp/renders
```

## Use in KiCad

1. Preferences > Manage Symbol Libraries: add `rcbench_modules.kicad_sym`
   with the nickname `rcbench_modules`.
2. Preferences > Manage Footprint Libraries: add `rcbench_modules.pretty`
   with the nickname `rcbench_modules`.

Footprint origin is pad 1. Every footprint draws the board outline and its
main parts in top view on F.SilkS and F.Fab; the courtyard is the outline of
everything drawn plus 0.25mm.

## Modules

Positions come from maker drawings or 3D models where these exist, and from
photos scaled by a known pitch otherwise. No geometry is taken from another
KiCad library. The photos and drawings used for measuring stay outside the
repository; each spec names them.

| Footprint | Module | Pads | Dimension source | Accuracy |
| --- | --- | --- | --- | --- |
| `RP2350-CAN` | Waveshare RP2350-CAN, 21.00 x 51.00mm | 40, Pico order, 2.54mm | maker drawing | pad grid exact; parts about 0.3mm |
| `INA3221_Module` | INA3221 board, 3 channels, 38.0 x 31.8mm | 15 (header 1-8, channels 9-14, VPU 15; GND holes on 2) | owner photos, front and back | about 0.3mm |
| `INA228_Matek_I2C-INA-BM_27x20mm` | MATEK I2C-INA-BM | 10 (2 x JST-GH, BAT+, ESC+) | maker 3D model and product image | 0.05mm to the model; pads about 0.3mm |
| `INA228_BIFRC_XT90_80x48mm` | INA228 board with 2 XT90, print "BIFRC V1.1" | 10 (header L A V G 5, solder bars, test pads) | owner photo, rectified | header 0.2mm; outline 0.6mm; XT90 1.5mm |
| `AS5600_Encoder_22.5x22.5mm` | AS5600 angle encoder board | 7 (VCC OUT GND, DIR SCL SDA GPO) | owner photo, rectified | about 0.3mm |
| `PD_Buck_WeAct_PDPowerMiniV1_61x40mm` | WeAct PD Power Mini V1 Buck | 12 (terminal 1-4, header 5-12) | maker vector drawing | outline and pads 0.02mm; parts 0.2mm |
| `USBC_T-plug_17x30mm` | USB-C tap board with plug board | 4 (G D+ D- V) | owner photo, scaled by USB-C sizes | about 0.5mm |
| `ADS1115_Module_28x17mm` | ADS1115 16-bit ADC board | 10 | vendor photo and dimension drawing | about 0.2mm |
| `ADXL345_GY-291_14.8x20.6mm` | ADXL345 board GY-291 | 8 | one oblique vendor photo, fitted | about 0.3mm |
| `INA219_MCU-219_25.4x21.5mm` | INA219 board with screw terminal | 8 (header 1-6, terminal 7-8) | vendor photo, rectified | about 0.2mm |
| `USBC_PD_Trigger_DIP3_30x20mm` | USB-C PD trigger, 3-way DIP switch | 2 (+, -) | vendor photo | about 0.2mm |
| `DCDC_Buck_Mini_30x18mm_EN` | mini buck DC-DC, 4R7, input 6-20V | 5 (IN+ IN- EN OUT+ OUT-) | dimensions printed on the vendor photo | grid exact; EN pad 0.5mm |

The accuracy figures are estimates from the measuring, not checked against
boards with a caliper. Check a module against its footprint before ordering
a PCB; the ADXL345 board rests on a single photo, and listings of it give
four different sizes.

## Limitations

- The MATEK and XT90 INA228 boards are wired, not soldered to a carrier;
  their power and connector pads are SMD (surface-mount) copper at the
  module's contact positions, for reference.
- The PD Power Mini has no UART pads. Its UART runs on the USB-C data lines
  (DP = TX, DM = RX, 3.3V); the symbol notes it and has no pins for it.
- The XT90 board's print says its screw holes carry circuitry: the holes are
  non-plated in the footprint and must not be joined to anything.
- Pin 1 of the JST-GH connectors on the MATEK board follows the board print;
  which lead JST calls pin 1 is not checked.
- KiCad 10.0.3 shows the symbol library empty, without an error message;
  10.0.7 loads it. Versions between are not tested. Whether 10.0.3 loads the
  footprint library is not tested.

## Licence

MIT (Massachusetts Institute of Technology), as the rest of the tree:
[LICENSE](../../LICENSE).
