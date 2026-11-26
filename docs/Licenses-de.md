# Lizenzen

[English](Licenses.md)

Diese Seite listet die Lizenzen von rcbench selbst und jeder
Fremdkomponente, die in die beiden Firmware-Images kompiliert ist, die das
Panel installiert: die Panel-Firmware (ESP32-P4, ESP-IDF v6.1) und die
IOmcu-Firmware (RP2350, pico-sdk 2.3.0), die das Panel-Image enthält. Das
Panel zeigt dieselbe Liste unter SETUP > PANEL > Über rcbench
([PANEL: Über rcbench](Screens-de.md#panel-über-rcbench)).

## Das Projekt

Copyright 2026 Julian Wingert

Design: Julian Wingert. Implemented by Claude and Julian.

| Teil | Lizenz | Datei |
| --- | --- | --- |
| Firmware und Software | MIT License (`MIT`) | `LICENSE` |
| Hardware-Entwürfe in `hardware/`, `docs/Carrier.md`, `docs/Carrier-de.md` | Creative Commons Attribution-NonCommercial-ShareAlike 4.0 (`CC-BY-NC-SA-4.0`) | `hardware/LICENSE`, die auch die Dateien unter anderen Bedingungen nennt |

Repository: [github.com/subtilitas/rcbench](https://github.com/subtilitas/rcbench).

## Fremdkomponenten

Die Liste stammt aus den Link-Maps der beiden Images
(`rcbench-panel.map`, `rcbench-iomcu.elf.map`): jedes Archivmitglied, das
der Linker genommen hat, zurückverfolgt bis zu seiner Quelldatei und deren
Lizenzkopf. Eine Komponente, die der Build holt, aber nicht linkt, steht
nicht in der Liste. Eine Lizenz ohne Kennung der SPDX-Liste (Software
Package Data Exchange) trägt eine `LicenseRef-`-Kennung.

<!-- gen_licenses: begin -->
| Komponente | Version | Lizenz (SPDX) | Copyright | Firmware |
| --- | --- | --- | --- | --- |
| ESP-IDF (drivers, esp_lcd, HTTP server, esp-tls, NVS, VFS) | 6.1.0 | `Apache-2.0` | Copyright 2015-2026 Espressif Systems (Shanghai) CO LTD | Panel |
| FreeRTOS Kernel (ESP-IDF SMP) | 10.5.1 | `MIT` | Copyright (C) 2021 Amazon.com, Inc. or its affiliates. | Panel |
| Apache Mynewt NimBLE (host) | 1.6.0 | `Apache-2.0` | Copyright 2015-2022 The Apache Software Foundation<br>Modifications Copyright 2017-2022 Espressif Systems (Shanghai) CO., LTD.<br>os_mbuf.c: Copyright (c) 1982, 1986, 1988, 1991, 1993 The Regents of the University of California. | Panel |
| lwIP | 2.2.0 | `BSD-3-Clause` | Copyright (c) 2001-2004 Swedish Institute of Computer Science.<br>Copyright (c) 2002-2003, Adam Dunkels.<br>Copyright (c) 2001-2004 Leon Woestenberg, Axon Digital Design B.V.<br>Copyright (c) 2002 CITEL Technologies Ltd.<br>Copyright (c) 2010 Inico Technologies Ltd.<br>Copyright (c) 2007-2009 Frédéric Bernon, Simon Goldschmidt | Panel |
| FatFs | R0.16 | `LicenseRef-scancode-fatfs` | Copyright (C) 2025, ChaN, all right reserved. | Panel |
| Mbed TLS and TF-PSA-Crypto | 4.1.0, 1.1.0 | `Apache-2.0 OR GPL-2.0-or-later` | Copyright The Mbed TLS Contributors | Panel |
| http_parser | 2.7.0 | `MIT` | Copyright Joyent, Inc. and other Node contributors.<br>Based on src/http/ngx_http_parse.c from NGINX, copyright Igor Sysoev | Panel |
| TLSF memory allocator (ESP-IDF heap) | ESP-IDF 6.1.0 | `BSD-3-Clause` | Copyright (c) 2006-2016 Matthew Conte | Panel |
| picolibc (C library) | 1.8.10 | `LicenseRef-picolibc-COPYING` | Copyright 2019 Keith Packard; 2020 The Newlib Project; the further holders its COPYING.picolibc names | Panel |
| esp_libc string routines (memcpy, strcmp, strcpy from SiFive; memmove, memcmp, strncmp, strncpy from newlib) | ESP-IDF 6.1.0 | `BSD-2-Clause-FreeBSD AND Apache-2.0` | Copyright (c) 2017 SiFive Inc.<br>Copyright (c) 1994-2009 Red Hat, Inc. | Panel |
| SD/MMC driver (ESP-IDF sdmmc, from OpenBSD) | ESP-IDF 6.1.0 | `ISC` | Copyright (c) 2006 Uwe Stuehler | Panel |
| UBSan handlers (ESP-IDF esp_system, from HelenOS) | ESP-IDF 6.1.0 | `BSD-2-Clause` | Copyright (c) 2016, Linaro Limited | Panel |
| ESP-Hosted-MCU (link to the ESP32-C6) | 2.13.0 | `Apache-2.0` | Copyright 2015-2026 Espressif Systems (Shanghai) CO LTD | Panel |
| protobuf-c (in ESP-Hosted-MCU) | 1.4.1 | `BSD-2-Clause` | Copyright (c) 2008-2022, Dave Benson and the protobuf-c authors. | Panel |
| Mozilla CA root certificates (ESP-IDF certificate bundle, common authorities) | Mozilla data of 2026-02-11 | `MPL-2.0` | certdata.txt of Mozilla NSS, source: https://curl.se/docs/caextract.html | Panel |
| GCC runtime libraries (libgcc, libstdc++) | 15.2.0 | `GPL-3.0-or-later WITH GCC-exception-3.1` | Copyright (C) Free Software Foundation, Inc. | Panel |
| QR Code generator library (qrcodegen) | 1.8.0 | `MIT` | Copyright (c) Project Nayuki | Panel |
| DejaVu Sans Mono (glyph bitmaps of tools/gen_font.py) | 2.37 | `Bitstream-Vera` | Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved.<br>DejaVu changes are in public domain. | Panel |
| Raspberry Pi Pico SDK | 2.3.0 | `BSD-3-Clause` | Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd. | IOmcu |
| TinyUSB (in the Pico SDK) | 0.18.0 | `MIT` | Copyright (c) 2018, hathach (tinyusb.org)<br>Copyright (c) 2020 Raspberry Pi (Trading) Ltd. (the RP2040 port) | IOmcu |
| USB descriptors of pico_stdio_usb (from MicroPython) | Pico SDK 2.3.0 | `MIT` | Copyright (c) 2020 Raspberry Pi (Trading) Ltd.<br>Copyright (c) 2019 Damien P. George | IOmcu |
| printf of pico_printf | Pico SDK 2.3.0 | `MIT` | (c) Marco Paland (info@paland.com) 2014-2019, PALANDesign Hannover, Germany | IOmcu |
| uart_tx and uart_rx PIO programs of pico-examples (pd_uart.pio) | pico-examples | `BSD-3-Clause` | Copyright (c) 2020 Raspberry Pi (Trading) Ltd. | IOmcu |
| newlib (C library of the arm-none-eabi toolchain) | 4.4.0 | `LicenseRef-newlib-COPYING` | Copyright (c) 1994-2009 Red Hat, Inc.; the further holders its COPYING.NEWLIB names | IOmcu |
| GCC runtime library (libgcc) | 14.2.1 | `GPL-3.0-or-later WITH GCC-exception-3.1` | Copyright (C) Free Software Foundation, Inc. | IOmcu |

| Referenz | Lizenz (SPDX) | Copyright | Verwendet für |
| --- | --- | --- | --- |
| LZMA specification and LzmaEnc.c of the LZMA SDK | `LicenseRef-LZMA-SDK-public-domain` | Igor Pavlov | der Literal-Encoder von protocols/supvan/lzlit.c und tools/lzlit.py |
| heeen/supvan-cups | `MIT` | Copyright (c) 2026 Florian Häne | die Fakten des Druckprotokolls von protocols/supvan |
<!-- gen_licenses: end -->

`tools/gen_licenses.py` enthält die Liste und schreibt diese Tabelle und
die Zeilen des Bildschirms (`shared/ui/about_gen.c`).
`tools/gen_licenses.py --check`, von der CI (Continuous Integration)
ausgeführt, schlägt fehl, wenn eines von beiden von der Liste abweicht.
`tools/gen_licenses.py --sources` vergleicht jeden gespeicherten Lizenztext
mit der Datei, aus der er im ESP-IDF-Baum, in den Toolchains und im
pico-sdk kopiert wurde.

## Wo die Lizenztexte stehen

Die Lizenzen MIT, BSD, ISC und Bitstream Vera verlangen, dass ihr
Copyright-Vermerk und Erlaubnistext jede Binärkopie begleiten; die Apache
License 2.0 verlangt ihren Text und die NOTICE-Datei einer Komponente, die
eine hat (NimBLE). Diese Texte stehen im Panel-Image und erscheinen auf
ABOUT unter LIZENZTEXTE UND HINWEISE, als 9.324 Zeilen zu höchstens 92
Zeichen.

| Größe | Wert |
| --- | --- |
| Texte in `third_party/notices/` und die zwei an Ort und Stelle gelesenen | 23 |
| Tabelle und Texte von ABOUT im Image (Rodata von `about_gen.c`) | 302.913 Byte |
| Panel-Image damit | 4.024.688 Byte |
| Anwendungs-Slot | 8.126.464 Byte, 4.101.776 Byte (50%) frei |

Die Texte liegen im Flash, nicht als Datei auf der SD-Karte (Secure
Digital):

- jedes Panel-Image trägt die Hinweise des Codes, den es enthält, und des
  IOmcu-Images, das es enthält, vom Flashen an;
- die Karte ist optional und entnehmbar, und eine Datei darauf kann
  gelöscht sein oder zu einem anderen Release gehören;
- die 302.913 Byte sind 3,7% des Anwendungs-Slots; COPYING.picolibc von
  picolibc macht 237.174 Byte davon aus.

Der Bildschirm zeichnet nur die Zeichen der Schriften (druckbares ASCII,
°, Ω und die deutschen Buchstaben): ein anderes Zeichen erscheint als seine
ASCII-Zerlegung, © als (c). Die Dateien in `third_party/notices/` behalten
die Zeichen ihrer Herkunft.

## Marken

KST, CHASERVO und Hitec sowie ihre Logos sind Marken oder eingetragene
Marken ihrer jeweiligen Hersteller. Jeder andere Produkt- und Firmenname in
diesem Repository ist ein Name oder eine Marke seines Inhabers und wird nur
genannt, um die Kompatibilität zu beschreiben. `NOTICE` listet die Namen
nach Gruppen.
