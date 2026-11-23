# rcbench web flasher

This static page writes rcbench firmware to the boards of a bench from the
browser. It is served at `/rcbench/flash/` on the repository's GitHub Pages
site, beside the configurator at `/rcbench/`.

The page is a wizard: the first screen lists tasks, then one screen per
step. It writes release images only, with "Keep settings" always on.

## Tasks and steps

| Task | Boards | Chip, link | Steps |
| --- | --- | --- | --- |
| Install or update the panel | panel | ESP32-P4, Web Serial | Task, Connect, Check, Write, Done |
| Install the IOmcu | IOmcu on the Waveshare RP2350-CAN, IOmcu on the rbmod carrier | RP2350, WebUSB | Task, Board, Connect, Check, Write, Done |
| Set up a wireless device | display node, M5Stack Dial, power node, balance node | ESP32-S3, ESP32-C6, Web Serial | Task, Board, Connect, Check, Write, Done |
| Repair the panel's C6 radio | C6 radio over header H4 and a USB-to-UART adapter | ESP32-C6, Web Serial | Task, Prepare, Connect, Check, Write, Done |

Below the tasks the page says that a running bench updates the IOmcu, the
C6 radio, the Dial and the nodes from the panel, and that this page is for
a first install and for a board that does not start.

| Screen | Shows | Moves on |
| --- | --- | --- |
| Task | 4 tiles; a tile whose link the browser lacks is disabled and names the missing interface | a tap on a tile |
| Board | the task's boards with their chip; a board no release on the page carries is disabled | a tap on a board; the download of its image starts here |
| Prepare (C6 radio only) | 3 steps from [The ESP32-C6 radio](../docs/C6.md#recovery-over-header-h4) and a drawing of H4: TXD0, RXD0, GND, IO9 | NEXT |
| Connect | lines per board (sockets, BOOT key, the pack off the XT60), CONNECT; for the IOmcu the 1200 baud restart into BOOTSEL and the BOOT drawing | a connection whose chip matches the board |
| Check | board, connected chip, release with kind and date, file, size in bytes, SHA-256, the manifest check, the signature, the IOmcu's slot B erase, the kept NVS ranges, WRITE | WRITE |
| Write | the phases of the board's flow (ESP: Write, Verify, Reset; RP2350: Exclusive access, Erase, Write, Verify, Reboot), progress bar and text | the end of the write |
| Done | file, time, kept ranges, the next step on the bench, WRITE ANOTHER, START OVER | a button |

Each step screen carries "Step n of m", BACK and CANCEL. CANCEL drops the
connection and returns to Task. The page writes nothing before a press of
WRITE, and every write ends with a verify.

The error states:

| Screen | State | What the page offers |
| --- | --- | --- |
| Connect | the browser's chooser closed | stays, no message |
| Connect | the connect fails | the step and a hint, CONNECT again |
| Connect | the chip is another than the board's | both chips named, nothing written, CHOOSE ANOTHER BOARD (Board, or Task for a one-board task) |
| Check | the download runs | "Downloading <file>", WRITE disabled |
| Check | the download fails, or its size or SHA-256 differs from the manifest | the reason, RETRY, WRITE disabled |
| Check | the release list is not available | the reason, WRITE disabled |
| Check | "Keep settings" refuses the image (see below) | the reason, WRITE disabled |
| Check | the signature does not match | a red note, WRITE disabled |
| Check | the release has no signature | an information note, WRITE enabled |
| Write | the write or the verify fails | the failed phase, the reason and a hint, CONNECT AGAIN (Connect, the image kept), CANCEL |
| Write | the reset after a verify that passed fails | the port is closed; goes to Done with "press RESET" |

### Release of the visit

The page writes one release for the whole visit: the newest stable
release with a panel image, else the newest release with one. A board that
release does not carry gets the newest stable release with an image for
it, else the newest with one (`src/release.ts`). Check shows the release
with its kind (testing or stable) and date.

### History and URL

The URL hash holds the state: `#t=<task>&b=<board>&s=<step>`, no hash on
Task. Every forward step adds a browser history entry, so the browser's
back and the page's BACK are the same move.

- Back from Check to Connect keeps the connection; Connect then shows it
  with NEXT and DISCONNECT. Back from Connect drops it; back to Board or
  Task drops the image too.
- A reload or a pasted link lands at most on Connect: connection and image
  are not in the URL.
- While a write runs, BACK and CANCEL are disabled and the browser's back
  leaves the Write screen as it is; leaving the page asks first.
- The browser's back from Done leads to Task.

The page sends no data and loads nothing from another origin: esptool-js
and its dependencies are bundled, and the firmware images are copies on the
same site (see "How releases reach the page").

## Boards and images

| Board | Chip | Link | Image of a release | Written at |
| --- | --- | --- | --- | --- |
| Panel, Waveshare ESP32-P4 7-inch board | ESP32-P4 | Web Serial | `rcbench-panel-merged.bin` from `rcbench-panel-p4-<tag>.zip` | 0x0 |
| IOmcu, Waveshare RP2350-CAN | RP2350 | WebUSB | `rcbench-iomcu-install.uf2` from `rcbench-iomcu-<tag>.zip` | its UF2 (USB Flashing Format) addresses |
| IOmcu, rbmod carrier | RP2350 | WebUSB | `rcbench-iomcu-rbmod-install.uf2` from `rcbench-iomcu-rbmod-<tag>.zip` | its UF2 addresses |
| Remote servo node, Waveshare Core2350B | RP2350B | WebUSB | not in a release yet; not listed | — |
| Display node, ES3C28P | ESP32-S3 | Web Serial | `rcbench-node-display-merged-<version>.bin` | 0x0 |
| M5Stack Dial | ESP32-S3 | Web Serial | `rcbench-dial-merged-<version>.bin` | 0x0 |
| Power node | ESP32-C6 | Web Serial | `rcbench-node-pwr-merged-<version>.bin` | 0x0 |
| Balance node | ESP32-C6 | Web Serial | `rcbench-node-bal-merged-<version>.bin` | 0x0 |
| C6 radio of the panel, over header H4 and a USB-to-UART adapter ([C6](../docs/C6.md)) | ESP32-C6 | Web Serial | `rcbench-c6-merged-<esp_hosted version>.bin` | 0x0 |

Every ESP image is a merged image: the bootloader, the partition table and
the application, for 0x0, with 0xFF in every gap. Written whole, as
`esptool.py write_flash 0x0` writes it, it fills the NVS partition with
0xFF and so erases the settings ([First run](../docs/FirstRun.md)).

## Release signature

Every image of a release has a detached signature, `<image>.sig`: ECDSA
(elliptic curve digital signature algorithm) on curve P-256 over the
SHA-256 of the whole file, in DER (distinguished encoding rules) form, 8 to
72 bytes ([SECURITY.md](../SECURITY.md#release-signatures)). For a release
image the page downloads `fw/<tag>/<image>.sig` beside the image and
verifies it with Web Crypto against the release public key it embeds
(`src/release_key.ts`, a copy of
`firmware/panel/main/keys/release_signing_pub.pem`; a unit test holds the
two equal). The acceptance rules are the panel's (`shared/fwupdate/fw_sign.c`):
a SEQUENCE of two INTEGERs of 1 to 33 bytes each with short lengths and no
byte after it; leading zero bytes in an integer are accepted.

Check shows one of these states:

| State | Shown when | On Check | WRITE |
| --- | --- | --- | --- |
| signed by the rcbench release key | the `.sig` verifies | a row "Signature" | enabled |
| no signature | no `.sig` (HTTP 404 or a failed download), or a `.sig` of 0 bytes | a blue information note: releases before the signed ones carry none | enabled |
| signature does not match | a `.sig` under 8 or over 72 bytes, out of form, or one that does not verify for this image | a red note | disabled |

The log lists the state of each release image.

## Keep settings

"Keep settings" (German "Einstellungen behalten") is always on: Check
lists what stays on the board for every ESP board.

- The page reads the partition table from the image and writes
  every byte of the image except the ranges of its data partitions of the
  subtypes nvs (0x02), nvs_keys (0x04), fat (0x81), spiffs (0x82) and
  littlefs (0x83). Before WRITE it lists each skipped range with name,
  subtype, offset and size in bytes, and the offset of the table.

Each written range gets its own MD5 (Message-Digest 5) check, over exactly
the bytes written there. A skipped range is neither written nor read.

What each board keeps, from the tables of the v0.23.15 images:

| Board | Table at | Kept | Holds | Written ranges |
| --- | --- | --- | --- | --- |
| Panel | 0xC000 | nvs 0xD000, 24,576 bytes | panel settings, Wi-Fi settings, the bound devices | 0x0 to 0xD000, 0x13000 to the end |
| M5Stack Dial | 0x8000 | nvs 0x9000, 24,576 bytes | the bound panel (namespace `dial`, key `panel`) | 0x0 to 0x9000, 0xF000 to the end |
| Display node, power node, balance node | 0x8000 | nvs 0x9000, 24,576 bytes | ESP-IDF's PHY calibration; the node firmware stores no setting of its own (`firmware/node`) | 0x0 to 0x9000, 0xF000 to the end |
| C6 radio | 0x8000 | nvs 0x9000, 16,384 bytes | the entries of ESP-IDF's Wi-Fi driver and PHY calibration | 0x0 to 0x9000, 0xD000 to the end |

The panel's `boardart` partition (0x7E0000, subtype 0x40) lies past the
end of its image (7,572,864 bytes in v0.23.15): neither write touches it.

**otadata is written.** Its bytes in a merged image are 0xFF, ESP-IDF's
`ota_data_initial.bin`. With a blank otadata the bootloader starts ota_0,
and ota_0 is where the merged image puts the application. A board that
had taken an update over the air runs from ota_1 with an otadata that
names ota_1; skipping otadata would leave that board on the old firmware
in ota_1 after a write that verifies. otadata holds only the choice of
slot, no setting.

**Where the table is.** ESP-IDF puts the table at 0x8000 unless
`CONFIG_PARTITION_TABLE_OFFSET` moves it; the panel moves it to 0xC000
(`firmware/panel/sdkconfig.defaults`) to make room for its bootloader at
0x2000. The page computes the end of the bootloader from its image header
(segment lengths, checksum, appended SHA-256) and looks for the table at
every 4096-byte step from there up to 0xF000. The table format follows
`gen_esp32part.py` of ESP-IDF v6.1: entries of 32 bytes with the magic
bytes 0xAA 0x50, an MD5 entry (0xEB 0xEB, 14 bytes 0xFF, the MD5 of the
entries), an end marker of 32 bytes 0xFF, at most 0xC00 bytes.

"Keep settings" refuses, with the reason, and WRITE stays disabled, when:

- the bootloader header is damaged or runs past the end of the image;
- no table is found, the table has no MD5 entry, its MD5 does not match,
  an entry lacks the magic bytes, or the end marker is missing;
- the image holds two valid tables;
- partitions overlap or start inside the sector of the table;
- a kept range overlaps the bootloader, the table sector, an application
  or any other partition (an ESP-IDF v6 bootloader entry included);
- a kept range does not start and end on a 4096-byte sector: the flasher
  stub erases whole sectors, so the written range next to it would erase
  part of it.

An image whose table has no such partition is written whole, and the page
says so.

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
notice that names Chrome and Edge, and every task is disabled. A browser
with one of the two interfaces disables the tasks and boards of the other
and names the missing interface on them.

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
Connect.

The flow: open the port at 115200 baud, reset into the boot ROM, sync,
detect the chip, start the flasher stub, change to 460800 baud behind a
USB-to-UART bridge, write each range of the write plan compressed at its
offset with flash mode, frequency and size kept as in the image, read the
MD5 of each written range from the stub and compare it with the MD5 of
the same bytes of the image, hard reset.

On the USB Serial/JTAG built into the ESP32-P4, -S3 and -C6 (USB ID
303A:1001) the page stays at 115200 baud and sends no baud change. The
rate has no effect there, and the change makes esptool-js reopen the
port, which resets the chip: esptool-js then connects again and goes on
at 115200 baud anyway. A panel connected that way writes 4,216,445
compressed bytes of v0.23.15 in about 40s.

On 21 November 2026 the page wrote v0.23.15 to a panel over the ESP32-P4's
own USB-Serial-JTAG (USB ID 303A:1001), chip revision 3.2: 7,572,864 bytes
in 40s.

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
   its merged `.bin` images with their `.sig` files and `SHA256SUMS`; `git show <tag>:firmware/iomcu/partitions.json`
   gives the IOmcu's partition table at that tag. A cache keyed on the list
   of tags keeps the downloads, about 175MB for 7 releases.
3. `tools/flash_manifest.py build` holds every asset to `SHA256SUMS`, takes
   the images out of the archives, holds every ESP image to its chip ID and
   to the merged kind, and writes:

```text
flash/fw/index.json               {"format":1,"keep":6,"releases":[{"tag","kind","published"}]}
flash/fw/<tag>/manifest.json      {"format":1,"tag","kind","published","images":[...]}
flash/fw/<tag>/<image>            the images
flash/fw/<tag>/<image>.sig        their release signatures, where the release has them
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

The page writes the release of the visit (see "Release of the visit").
Releases up to 0.22.1 carry a panel image for an older panel chip and no
rbmod image: they offer the IOmcu image alone.

## Layout

| Path | Content |
| --- | --- |
| `src/main.ts` | Wiring: events, the render of the current step, the browser history and the URL hash. |
| `src/state.ts` | The page's state, the facts the wizard's guards read, strings, the log, the stored language. |
| `src/actions.ts` | The release list, the release image with its checks and signature, the connection, the write with its verify. |
| `src/views.ts` | One render function per step: Task, Board, Prepare, Connect, Check, Write, Done. |
| `src/i18n.ts` | Strings in English and German. |
| `src/boards.ts` | The boards, their chips, the image and chip checks. |
| `src/manifest.ts` | Reader and checks of `index.json` and `manifest.json`. |
| `src/esp.ts` | ESP32 flow on esptool-js, the baud rule. |
| `src/partitions.ts` | The partition table of a merged image, the bootloader's end, the write plan of "Keep settings". |
| `src/picoboot.ts` | PICOBOOT: command encoding, transfers, errors. |
| `src/rp2350.ts` | RP2350 flow: plan, erase, write, read back, reboot. |
| `src/uf2.ts` | UF2 reader. |
| `src/bootsel.ts` | The 1200 baud request, the boot ROM device chooser. |
| `src/digest.ts` | SHA-256 by Web Crypto, MD5. |
| `src/tasks.ts` | Wizard model: the tasks of the first screen, the boards and links of each task, the image sources and "Keep settings" per mode. |
| `src/release.ts` | Wizard model: the release of the visit and the release per board. |
| `src/wizard.ts` | Wizard model: the steps of each task, the moves between them, their entry guards, the URL hash, the gate of WRITE. |
| `src/signature.ts` | Release signature: DER form check, conversion to the 64-byte form of Web Crypto, verification. |
| `src/release_key.ts` | The release public key, a copy of the panel's. |
| `public/licenses.txt` | Licences of the bundled code, written by `tools/licenses.ts`. |
| `test/` | Unit tests, run by `node --test`. `pages-fw/` holds `index.json` and the 7 manifests of the Pages site on 2026-11-23. `fixtures.ts` builds synthetic UF2 images, ESP images, partition tables and merged images, and holds the panel's partition table and bootloader header of v0.23.15. |
| `e2e/helpers.ts` | Set-up of the browser tests: synthetic release images, the `fw/` folder served from memory, a test key put in place of the release key in the served script, the walk to Connect. |
| `e2e/tasks.spec.ts` | Each task to Done: panel with the NVS bytes kept, Dial, power node, IOmcu at 1200 baud and over PICOBOOT, C6 radio at 460800 baud behind a USB-to-UART bridge; a language switch during a write; WRITE ANOTHER. |
| `e2e/errors.spec.ts` | A chip that does not match, a download that fails or differs, a release list that fails, "Keep settings" refused, a board no release carries, the 3 signature states, a write and a verify that fail. |
| `e2e/history.spec.ts` | BACK, CANCEL, the browser's back from Check, Connect, Write and Done, a pasted hash, a reload. |
| `e2e/browser.spec.ts` | No Web Serial and no WebUSB, no WebUSB alone, the task list, the language, the footer links, the width of a phone. |
| `e2e/fake-devices.js` | Fake Web Serial with an ESP32 boot ROM and flasher stub that erases the 4096-byte sectors it writes, an IOmcu port that leaves for BOOTSEL at 1200 baud, and a fake WebUSB RP2350 boot ROM; options for a write, an MD5 and a read back that fail, and for a browser without WebUSB. |
| `tools/size.ts` | Size check of the build. |
| `tools/licenses.ts` | Writes and checks `public/licenses.txt`. |

## Wizard model

`src/tasks.ts`, `src/release.ts` and `src/wizard.ts` hold the step model of
a wizard: no browser code, unit tests in `test/tasks.test.ts`,
`test/release.test.ts` and `test/wizard.test.ts`. The page runs in simple
mode. It has no Advanced switch, so the model's Advanced parts ("Write a
file of my own", a picked release, a local file, "Keep settings" off, the
Core2350B, WRITE on a signature that does not match) have no control on the
page.

- First screen: 4 tasks, panel, IOmcu, wireless device, and the C6 radio
  repair last. The Advanced switch adds "Write a file of my own" with all 9
  boards.
- Steps: Task, Board (tasks of several boards) or Prepare (C6 repair),
  Connect, Check, Write, Done.
- Release: one release per visit, the default release of the panel image
  (newest stable with a panel image, else the newest); a board that release
  does not carry takes the newest stable release with an image for it, else
  the newest. Advanced writes the picked release while it carries the board.
- Signature on Check: "does not match" disables WRITE in simple mode and is
  a warning in Advanced; "no signature" is an information note in both. A
  local file without a picked .sig file (Advanced only) shows "signature not
  checked" as an information note and allows WRITE.
- Core2350B: Advanced only, local file only.
- Simple mode: release images only, "Keep settings" always on.

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
it. The build measures 75.2kB at the first load and 401.8kB in all, with
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
- A board behind a USB-to-UART bridge that stops answering after the
  change to 460800 baud is written at 115200 baud: esptool-js connects
  again at the lower rate.
- "Keep settings" keeps the NVS bytes whatever firmware wrote them. The
  rcbench firmware erases the partition and starts on defaults when
  `nvs_flash_init` returns `ESP_ERR_NVS_NO_FREE_PAGES` or
  `ESP_ERR_NVS_NEW_VERSION_FOUND`; the panel runs on defaults when NVS
  stays unusable after that.
- The page looks for the partition table up to 0xF000 only. An image with
  the table above 0xF000 gets the refusal "no partition table" and a full
  write only.
- The RP2350 boot ROM's limit for the length of one PICOBOOT transfer is not
  known; the page sends 64KiB, which picotool exceeds for an image above
  6.4MB.
- An ESP32 in secure download mode or with flash encryption is not handled
  beyond what esptool-js does.
- The Core2350B has no image in a release.
