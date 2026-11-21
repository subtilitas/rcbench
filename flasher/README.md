# rcbench web flasher

This static page writes rcbench firmware to the boards of a bench from the
browser. It is served at `/rcbench/flash/` on the repository's GitHub Pages
site, beside the configurator at `/rcbench/`.

- **Panel**, an ESP32-P4, over Web Serial: the merged image of the chosen
  release at 0x0. The page connects, detects the chip, writes, verifies and
  resets. No offset and no file to pick.
- **IOmcu**, an RP2350, over WebUSB: the install image of the chosen release,
  through the boot ROM's PICOBOOT interface.
- **Wireless devices** over Web Serial: the display node and the M5Stack
  Dial (ESP32-S3), the power node, the balance node and the panel's C6
  radio (ESP32-C6). The merged image of the chosen release at 0x0.
- **A local file** in place of a release image, for any of these boards.

The page writes nothing before a press of WRITE. Before that it shows the
image's release tag, file name, size in bytes and SHA-256 (Secure Hash
Algorithm). Every write ends with a verify step. Progress shows the step,
the percentage, the bytes and the elapsed time.

The page sends no data and loads nothing from another origin: esptool-js
and its dependencies are bundled, and the firmware images are copies on the
same site (see "How releases reach the page").

## Boards and images

| Board | Chip | Link | Image of a release | Written at |
| --- | --- | --- | --- | --- |
| Panel, Waveshare ESP32-P4 7-inch board | ESP32-P4 | Web Serial | `rcbench-panel-merged.bin` from `rcbench-panel-p4-<tag>.zip` | 0x0 |
| IOmcu, Waveshare RP2350-CAN | RP2350 | WebUSB | `rcbench-iomcu-install.uf2` from `rcbench-iomcu-<tag>.zip` | its UF2 (USB Flashing Format) addresses |
| IOmcu, rbmod carrier | RP2350 | WebUSB | `rcbench-iomcu-rbmod-install.uf2` from `rcbench-iomcu-rbmod-<tag>.zip` | its UF2 addresses |
| Remote servo node, Waveshare Core2350B | RP2350B | WebUSB | not in a release yet; listed and greyed out | — |
| Display node, ES3C28P | ESP32-S3 | Web Serial | `rcbench-node-display-merged-<version>.bin` | 0x0 |
| M5Stack Dial | ESP32-S3 | Web Serial | `rcbench-dial-merged-<version>.bin` | 0x0 |
| Power node | ESP32-C6 | Web Serial | `rcbench-node-pwr-merged-<version>.bin` | 0x0 |
| Balance node | ESP32-C6 | Web Serial | `rcbench-node-bal-merged-<version>.bin` | 0x0 |
| C6 radio of the panel, over header H4 and a USB-to-UART adapter ([C6](../docs/C6.md)) | ESP32-C6 | Web Serial | `rcbench-c6-merged-<esp_hosted version>.bin` | 0x0 |

Every ESP image is a merged image: the bootloader, the partition table and
the application, for 0x0. A merged image erases the settings in NVS
(non-volatile storage) on the panel, as `esptool.py write_flash 0x0`
does ([First run](../docs/FirstRun.md)).

The checks before WRITE:

- An ESP image carries the chip ID of its target in byte 12 of its image
  header: 18 for the ESP32-P4 of the panel, 9 for the ESP32-S3 of the Dial
  and the display node, 13 for the ESP32-C6. The
  header stands at 0x0, for the ESP32-P4 at 0x2000. An image of another chip
  is refused, and so is an application image alone (its description word
  0xABCD5432 at byte 32 of the header).
- An image of a release must match the SHA-256 and size of its manifest.
- After Connect, the chip esptool-js detects must be the chip of the image.
  Another chip is refused before anything is written.
- An RP2350 image is a file named `.uf2` that the UF2 reader takes, of the
  families absolute, rp2350-arm-s, rp2350-riscv or rp2350-arm-ns; a damaged
  or short `.uf2` is refused. A file named `.bin` goes to the flash start
  as raw bytes; one that starts with the UF2 magic word 0x0A324655 is
  refused as a UF2 file under the wrong name. Any other name is refused.

## Browsers

Chrome or Edge on a desktop computer (Windows, macOS, Linux, ChromeOS).
Firefox and Safari have neither Web Serial nor WebUSB: the page shows a
notice that names Chrome and Edge and hides the steps. A browser with one of
the two interfaces offers the boards of that interface and marks the others.

- **Windows:** the RP2350 boot ROM serves a Microsoft OS 2.0 descriptor set
  that names WinUSB as the driver of its PICOBOOT interface
  (`nsboot_usb_client.c` of the boot ROM sources,
  github.com/raspberrypi/pico-bootrom-rp2350), so Windows 8.1 and later
  bind WinUSB without an installer. Chrome and Edge reach a WinUSB
  interface of a composite device through WebUSB. A board whose USB socket goes through a USB-to-UART bridge chip needs that
  chip's driver (CH343, CP210x). The 7-inch panel board has 2 USB-C
  sockets; which one reaches UART0 and which the ESP32-P4's built-in
  USB-Serial-JTAG is not traced, and its bridge chip is not known.
- **Linux:** the browser opens the boot ROM's USB device only with access
  rights, for example through picotool's udev rule `99-picotool.rules`. A
  serial port needs the user in the group of the port (`dialout` on most
  distributions).

## ESP32: esptool-js 0.7.1

esptool-js 0.7.1 (Apache-2.0, Espressif) is bundled at that exact version.
esptool-js added the ESP32-P4 in 0.4.4 (18 July 2024). 0.6.0 (26 March
2026) is the first version that picks the flasher stub and the UART buffer
address by the chip revision of an ESP32-P4, which a P4 of revision 3.0 or
later needs; the panel build requires revision 3.1
(`CONFIG_ESP32P4_REV_MIN_301` in `firmware/panel/sdkconfig.defaults`).
0.7.1 is the newest release (2 November 2026). It loads on the first
Connect: the first load of the page is 50.3kB.

The flow: open the port at 115200 baud, reset into the boot ROM, sync,
detect the chip, start the flasher stub, change to 460800 baud (the
USB-Serial-JTAG of the ESP32-P4, -S3 and -C6 ignores the rate), write the
image compressed at 0x0 with flash mode, frequency and size kept as in the
image, read the MD5 (Message-Digest 5) of the written range from the stub
and compare it with the MD5 of the image, hard reset.

## RP2350: PICOBOOT over WebUSB

The RP2350 cannot be flashed over Web Serial. In BOOTSEL it is a USB drive
and the boot ROM's PICOBOOT interface: vendor class 0xFF, bulk OUT
endpoint 3, bulk IN endpoint 4, USB ID 2E8A:000F. The page speaks PICOBOOT
itself (`src/picoboot.ts`):

- A command is 32 bytes: magic 0x431FD10B, token, command ID, argument
  length, transfer length, up to 16 argument bytes. Bit 7 of the ID marks
  an IN command. The data follows in its direction; a zero-length packet in
  the opposite direction acknowledges it.
- On a stall the page reads the command status (control request 0x42) to
  name the error and resets the interface (control request 0x41).
- The flow: EXCLUSIVE_ACCESS, EXIT_XIP, FLASH_ERASE of the ranges the
  manifest names and of every 4096-byte sector the image writes, WRITE,
  READ of every written byte and every erased range and a comparison,
  REBOOT2 of type NORMAL after 500ms. Erase, write and read go in chunks of
  64KiB.
- A UF2 file of the absolute family is written at its addresses. A UF2
  file of another family goes into the partition the boot ROM names for
  that family (GET_INFO, type UF2_TARGET_PARTITION), as a drag-and-drop
  places it. The RP2350-E10 workaround block, a lone absolute block at
  0x10FFFF00, is dropped, as picotool drops it.
- A `.bin` goes to 0x10000000, the flash start: on an IOmcu that is the
  partition table. The page says so beside a `.bin`.

The release image is the install image: the partition table at 0x0 and the
image in slot A. Its manifest entry names slot B, 2044KiB to 4080KiB, as a
range to erase. Between two valid slots the boot ROM starts the higher
version; with slot B blank, the written release is the one that starts,
also when it is older than the one before. The output store at 4080KiB and
the store of an older layout at 0x3FE000 are not touched.

Sources: the command values, the command layout, the status codes and the
family IDs follow `boot/picoboot.h`, `boot/picoboot_constants.h` and
`boot/uf2.h` of pico-sdk 2.3.0; the order of a transfer and the recovery
follow `picoboot_connection.c` of picotool 2.3.0; the E10 rule, the target
partition and the reboot parameters follow picotool's `main.cpp`. No code is
copied. Both are BSD-3-Clause, Copyright (c) 2020 Raspberry Pi (Trading)
Ltd.; the terms are in `public/licenses.txt` and in the repository's
`NOTICE`. The RP2350 datasheet (CC BY-ND 4.0) is used for facts only.

### The IOmcu into BOOTSEL

The IOmcu firmware links pico-sdk's USB stdio with its default reset
interface (`pico_enable_stdio_usb` in `firmware/iomcu/CMakeLists.txt`,
`PICO_USB_RESET_MAGIC_BAUD_RATE` 1200 in pico-sdk). "Restart the IOmcu into
BOOTSEL" opens the IOmcu's serial port (USB vendor ID 2E8A) at 1200 baud;
the firmware then calls the boot ROM's reset into BOOTSEL. The port
vanishes, the boot ROM appears, and Connect finds it. An IOmcu without
rcbench firmware, or one that does not run, needs the button: hold BOOT,
plug in the USB cable, release BOOT. The page shows that with a drawing.

## How releases reach the page

A release download from github.com sends no CORS (cross-origin resource
sharing) header, so the page cannot fetch release assets. The Pages
workflow (`.github/workflows/pages.yml`, job `flash-fw`) copies them to the
site:

1. `gh release list` and `tools/flash_manifest.py select` pick the 6 newest
   published releases by version, and the newest stable release when it is
   older than those 6. Odd minor numbers are testing releases, even minor
   numbers stable ones.
2. `gh release download` fetches each release's panel and IOmcu archives,
   its merged `.bin` images and `SHA256SUMS`; `git show <tag>:firmware/iomcu/partitions.json`
   gives the IOmcu's partition table at that tag. A cache keyed on the list
   of tags keeps the downloads, about 175MB for 7 releases.
3. `tools/flash_manifest.py build` holds every asset to `SHA256SUMS`, takes
   the images out of the archives, holds every ESP image to its chip ID and
   to the merged kind, and writes:

```text
flash/fw/index.json               {"format":1,"keep":6,"releases":[{"tag","kind","published"}]}
flash/fw/<tag>/manifest.json      {"format":1,"tag","kind","published","images":[...]}
flash/fw/<tag>/<image>            the images
```

An image entry: `board`, `chip`, `file`, `size` in bytes, `sha256`, `offset`
(always 0), `format` (`bin` or `uf2`) and for an IOmcu `erase`, a list of
`{offset, size}` in bytes from the flash start.

The workflow runs on a push to `main` and on a manual run on `main`.
`release.yml` starts that manual run once it publishes a release (job
`pages`): the github-pages environment deploys from `main` only, so a run
on the release event, whose ref is the tag, could not deploy. No binary is
committed to git.

Size on Pages: 63,506,153 bytes, about 64MB, for the 7 releases on
21 November 2026 (v0.23.10 to v0.23.15 and v0.22.1). A release with all 8
images takes about 13.4MB, so the folder reaches about 94MB once every kept
release carries all of them. GitHub Pages allows a site of 1GB.

The page lists for each board the releases whose manifest carries an image
for it, newest first, with their kind and date. It selects the newest stable
release with an image for the board, else the newest. A release the visitor
picks stays selected while the next board has an image in it. Releases up to
0.22.1 carry a panel image for an older panel chip and no rbmod image: they
offer the IOmcu image alone.

## Layout

| Path | Content |
| --- | --- |
| `src/main.ts` | State, events and rendering. |
| `src/i18n.ts` | Strings in English and German. |
| `src/boards.ts` | The boards, their chips, the image and chip checks. |
| `src/manifest.ts` | Reader and checks of `index.json` and `manifest.json`. |
| `src/esp.ts` | ESP32 flow on esptool-js. |
| `src/picoboot.ts` | PICOBOOT: command encoding, transfers, errors. |
| `src/rp2350.ts` | RP2350 flow: plan, erase, write, read back, reboot. |
| `src/uf2.ts` | UF2 reader. |
| `src/bootsel.ts` | The 1200 baud request, the boot ROM device chooser. |
| `src/digest.ts` | SHA-256 by Web Crypto, MD5. |
| `public/licenses.txt` | Licences of the bundled code, written by `tools/licenses.ts`. |
| `test/` | Unit tests, run by `node --test`. `fixtures.ts` builds synthetic UF2 and ESP images. |
| `e2e/flow.spec.ts` | Playwright test of the whole flow to "done" for the panel, the Dial, the power node and the IOmcu, and the refusals. |
| `e2e/fake-devices.js` | Fake Web Serial with an ESP32 boot ROM and flasher stub, an IOmcu port that leaves for BOOTSEL at 1200 baud, and a fake WebUSB RP2350 boot ROM. |
| `tools/size.ts` | Size check of the build. |
| `tools/licenses.ts` | Writes and checks `public/licenses.txt`. |

The manifest builder is `tools/flash_manifest.py` at the repository root,
with its tests in `test/tools/test_flash_manifest.py`.

## Commands

Node.js 24 is required.

```bash
cd flasher
npm ci
npm run typecheck   # tsc --noEmit
npm test            # unit tests
npm run licenses    # writes public/licenses.txt
npm run build       # checks public/licenses.txt, then writes dist/
npm run size        # initial load at most 80kB, dist/ at most 600kB
npx playwright install chromium
npm run e2e         # browser test at 1280px in English, 360px in German
npm run dev         # http://localhost:5173/rcbench/flash/, without fw/
python3 -m pytest ../test/tools/test_flash_manifest.py
```

The size budget is the page's own; the configurator's budget does not count
it. The build measures 50.3kB at the first load and 376.9kB in all, with
the flasher stubs of every chip esptool-js knows; the page loads the stub of
the detected chip only.

## Language and stored state

English and German; the start language follows the browser, the buttons EN
and DE switch it. A switch during a write keeps the progress: the page
renders the last step, percentage and bytes in the new language. The page keeps the language picked with the buttons in
`localStorage` under `rcbench.flasher` (`{"v":1,"lang":"de"}`). A browser
that blocks the store keeps the page at work. Light and dark theme follow
the system setting.

## Limits

- The browser test runs against fakes of the boot ROMs (`e2e/fake-devices.js`).
- A board that stops answering after the change to 460800 baud is written
  at 115200 baud: esptool-js connects again at the lower rate.
- The RP2350 boot ROM's limit for the length of one PICOBOOT transfer is not
  known; the page sends 64KiB, which picotool exceeds for an image above
  6.4MB.
- A `.bin` for an RP2350 goes to the flash start whatever the board's
  partition table.
- An ESP32 in secure download mode or with flash encryption is not handled
  beyond what esptool-js does.
- The Core2350B has no image in a release.
