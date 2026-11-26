# Licences

[Deutsch](Licenses-de.md)

This page lists the licences of rcbench itself and every third-party
component compiled into the two firmware images the panel installs: the
panel firmware (ESP32-P4, ESP-IDF v6.1) and the IOmcu firmware (RP2350,
pico-sdk 2.3.0), which the panel image carries. The panel shows the same
list on SETUP > PANEL > About rcbench
([PANEL: About rcbench](Screens.md#panel-about-rcbench)).

## The project

Copyright 2026 Julian Wingert

Design: Julian Wingert. JuWi made.

| Part | Licence | File |
| --- | --- | --- |
| Firmware and software | MIT License (`MIT`) | `LICENSE` |
| Hardware designs in `hardware/`, `docs/Carrier.md`, `docs/Carrier-de.md` | Creative Commons Attribution-NonCommercial-ShareAlike 4.0 (`CC-BY-NC-SA-4.0`) | `hardware/LICENSE`, which also names the files under other terms |

Repository: [github.com/subtilitas/rcbench](https://github.com/subtilitas/rcbench).

## Third-party components

The list was read off the link maps of the two images
(`rcbench-panel.map`, `rcbench-iomcu.elf.map`): every archive member the
linker took, traced to its source file and the licence header of that file.
A component the build fetches but does not link is not listed. A licence
with no SPDX (Software Package Data Exchange) list identifier carries a
`LicenseRef-` one.

<!-- gen_licenses: begin -->
| Component | Version | Licence (SPDX) | Copyright | Firmware |
| --- | --- | --- | --- | --- |
| ESP-IDF (drivers, esp_lcd, HTTP server, esp-tls, NVS, VFS) | 6.1.0 | `Apache-2.0` | Copyright 2015-2026 Espressif Systems (Shanghai) CO LTD | panel |
| FreeRTOS Kernel (ESP-IDF SMP) | 10.5.1 | `MIT` | Copyright (C) 2021 Amazon.com, Inc. or its affiliates. | panel |
| Apache Mynewt NimBLE (host) | 1.6.0 | `Apache-2.0` | Copyright 2015-2022 The Apache Software Foundation<br>Modifications Copyright 2017-2022 Espressif Systems (Shanghai) CO., LTD.<br>os_mbuf.c: Copyright (c) 1982, 1986, 1988, 1991, 1993 The Regents of the University of California. | panel |
| lwIP | 2.2.0 | `BSD-3-Clause` | Copyright (c) 2001-2004 Swedish Institute of Computer Science.<br>Copyright (c) 2002-2003, Adam Dunkels.<br>Copyright (c) 2001-2004 Leon Woestenberg, Axon Digital Design B.V.<br>Copyright (c) 2002 CITEL Technologies Ltd.<br>Copyright (c) 2010 Inico Technologies Ltd.<br>Copyright (c) 2007-2009 Frédéric Bernon, Simon Goldschmidt | panel |
| FatFs | R0.16 | `LicenseRef-scancode-fatfs` | Copyright (C) 2025, ChaN, all right reserved. | panel |
| Mbed TLS and TF-PSA-Crypto | 4.1.0, 1.1.0 | `Apache-2.0 OR GPL-2.0-or-later` | Copyright The Mbed TLS Contributors | panel |
| http_parser | 2.7.0 | `MIT` | Copyright Joyent, Inc. and other Node contributors.<br>Based on src/http/ngx_http_parse.c from NGINX, copyright Igor Sysoev | panel |
| TLSF memory allocator (ESP-IDF heap) | ESP-IDF 6.1.0 | `BSD-3-Clause` | Copyright (c) 2006-2016 Matthew Conte | panel |
| picolibc (C library) | 1.8.10 | `LicenseRef-picolibc-COPYING` | Copyright 2019 Keith Packard; 2020 The Newlib Project; the further holders its COPYING.picolibc names | panel |
| esp_libc string routines (memcpy, strcmp, strcpy from SiFive; memmove, memcmp, strncmp, strncpy from newlib) | ESP-IDF 6.1.0 | `BSD-2-Clause-FreeBSD AND Apache-2.0` | Copyright (c) 2017 SiFive Inc.<br>Copyright (c) 1994-2009 Red Hat, Inc. | panel |
| SD/MMC driver (ESP-IDF sdmmc, from OpenBSD) | ESP-IDF 6.1.0 | `ISC` | Copyright (c) 2006 Uwe Stuehler | panel |
| UBSan handlers (ESP-IDF esp_system, from HelenOS) | ESP-IDF 6.1.0 | `BSD-2-Clause` | Copyright (c) 2016, Linaro Limited | panel |
| ESP-Hosted-MCU (link to the ESP32-C6) | 2.13.0 | `Apache-2.0` | Copyright 2015-2026 Espressif Systems (Shanghai) CO LTD | panel |
| protobuf-c (in ESP-Hosted-MCU) | 1.4.1 | `BSD-2-Clause` | Copyright (c) 2008-2022, Dave Benson and the protobuf-c authors. | panel |
| Mozilla CA root certificates (ESP-IDF certificate bundle, common authorities) | Mozilla data of 2026-02-11 | `MPL-2.0` | certdata.txt of Mozilla NSS, source: https://curl.se/docs/caextract.html | panel |
| GCC runtime libraries (libgcc, libstdc++) | 15.2.0 | `GPL-3.0-or-later WITH GCC-exception-3.1` | Copyright (C) Free Software Foundation, Inc. | panel |
| QR Code generator library (qrcodegen) | 1.8.0 | `MIT` | Copyright (c) Project Nayuki | panel |
| DejaVu Sans Mono (glyph bitmaps of tools/gen_font.py) | 2.37 | `Bitstream-Vera` | Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved.<br>DejaVu changes are in public domain. | panel |
| Raspberry Pi Pico SDK | 2.3.0 | `BSD-3-Clause` | Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd. | IOmcu |
| TinyUSB (in the Pico SDK) | 0.18.0 | `MIT` | Copyright (c) 2018, hathach (tinyusb.org)<br>Copyright (c) 2020 Raspberry Pi (Trading) Ltd. (the RP2040 port) | IOmcu |
| USB descriptors of pico_stdio_usb (from MicroPython) | Pico SDK 2.3.0 | `MIT` | Copyright (c) 2020 Raspberry Pi (Trading) Ltd.<br>Copyright (c) 2019 Damien P. George | IOmcu |
| printf of pico_printf | Pico SDK 2.3.0 | `MIT` | (c) Marco Paland (info@paland.com) 2014-2019, PALANDesign Hannover, Germany | IOmcu |
| uart_tx and uart_rx PIO programs of pico-examples (pd_uart.pio) | pico-examples | `BSD-3-Clause` | Copyright (c) 2020 Raspberry Pi (Trading) Ltd. | IOmcu |
| newlib (C library of the arm-none-eabi toolchain) | 4.4.0 | `LicenseRef-newlib-COPYING` | Copyright (c) 1994-2009 Red Hat, Inc.; the further holders its COPYING.NEWLIB names | IOmcu |
| GCC runtime library (libgcc) | 14.2.1 | `GPL-3.0-or-later WITH GCC-exception-3.1` | Copyright (C) Free Software Foundation, Inc. | IOmcu |

| Reference | Licence (SPDX) | Copyright | Used for |
| --- | --- | --- | --- |
| LZMA specification and LzmaEnc.c of the LZMA SDK | `LicenseRef-LZMA-SDK-public-domain` | Igor Pavlov | the literal-only encoder of protocols/supvan/lzlit.c and tools/lzlit.py |
| heeen/supvan-cups | `MIT` | Copyright (c) 2026 Florian Häne | the print protocol facts of protocols/supvan |
<!-- gen_licenses: end -->

`tools/gen_licenses.py` holds the list and writes both this table and the
screen's rows (`shared/ui/about_gen.c`). `tools/gen_licenses.py --check`,
run by CI (continuous integration), fails when either differs from the
list. `tools/gen_licenses.py --sources` compares each stored licence text
with the file it was copied from in the ESP-IDF tree, the toolchains and
the pico-sdk.

## Where the notice texts are

The MIT, BSD, ISC and Bitstream Vera licences ask that their copyright
notice and permission text go with every binary copy; the Apache License
2.0 asks for its text and the NOTICE file of a component that has one
(NimBLE). These texts are in the panel image and show on ABOUT, under
LICENCE TEXTS AND NOTICES, as 9,324 rows of at most 92 cells.

| Figure | Value |
| --- | --- |
| Texts in `third_party/notices/` and the two read in place | 23 |
| ABOUT's table and texts in the image (`about_gen.c`'s rodata) | 304,869 bytes |
| Panel image with them | 4,024,688 bytes |
| Application slot | 8,126,464 bytes, 4,101,776 bytes (50%) free |

The texts live in flash, not as a file on the SD (Secure Digital) card:

- every panel image carries the notices of the code it holds, and of the
  IOmcu image it holds, from the moment it is flashed;
- the card is optional and removable, and a file on it can be deleted or
  can belong to another release;
- the 304,869 bytes are 3.8% of the application slot; picolibc's
  COPYING.picolibc is 237,174 bytes of them.

The screen draws the faces' characters only (printable ASCII, °, Ω and the
German letters): another character shows as its ASCII decomposition, ©
as (c). The files in `third_party/notices/` keep the characters of their
origins.

## Trademarks

KST, CHASERVO and Hitec and their logos are trademarks or registered
trademarks of their respective manufacturers. Every other product and
company name in this repository is a name or trademark of its owner and is
used to describe compatibility only. `NOTICE` lists the names, by group.
