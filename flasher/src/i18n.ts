// Strings of the page in English and German. The start language follows the
// browser; the buttons EN and DE switch it.

export type Lang = 'en' | 'de';

const S = {
  title: ['rcbench web flasher', 'rcbench-Web-Flasher'],
  sub: [
    'Writes rcbench firmware to the panel, the IOmcu and the wireless devices, from the browser.',
    'Schreibt rcbench-Firmware auf das Panel, die IOmcu und die Funkgeräte, aus dem Browser.',
  ],
  langGroup: ['Language / Sprache', 'Language / Sprache'],
  noBrowser: [
    'This browser has neither Web Serial nor WebUSB. The page writes firmware only in Chrome or Edge on a desktop computer (Windows, macOS, Linux, ChromeOS). Firefox and Safari offer neither interface.',
    'Dieser Browser hat weder Web Serial noch WebUSB. Die Seite schreibt Firmware nur in Chrome oder Edge auf einem Desktop-Computer (Windows, macOS, Linux, ChromeOS). Firefox und Safari bieten keine der beiden Schnittstellen.',
  ],
  noSerial: [
    'This browser has no Web Serial: the panel and the wireless devices need Chrome or Edge on a desktop computer.',
    'Dieser Browser hat kein Web Serial: Panel und Funkgeräte brauchen Chrome oder Edge auf einem Desktop-Computer.',
  ],
  noUsb: [
    'This browser has no WebUSB: the IOmcu needs Chrome or Edge on a desktop computer.',
    'Dieser Browser hat kein WebUSB: Die IOmcu braucht Chrome oder Edge auf einem Desktop-Computer.',
  ],
  hBoard: ['1. Board', '1. Board'],
  gPanel: ['Panel', 'Panel'],
  gIomcu: ['IOmcu, RP2350 over USB in BOOTSEL', 'IOmcu, RP2350 über USB im BOOTSEL'],
  gWireless: ['Wireless devices, ESP32-S3 and ESP32-C6', 'Funkgeräte, ESP32-S3 und ESP32-C6'],
  b_panel: ['Panel: Waveshare ESP32-P4 7-inch board', 'Panel: Waveshare-ESP32-P4-Board, 7 Zoll'],
  b_iomcu: ['IOmcu on the Waveshare RP2350-CAN', 'IOmcu auf dem Waveshare RP2350-CAN'],
  'b_iomcu-rbmod': ['IOmcu on the rbmod carrier', 'IOmcu auf dem rbmod-Träger'],
  b_core2350b: ['Remote servo node: Waveshare Core2350B (RP2350B)', 'Servo-Fernknoten: Waveshare Core2350B (RP2350B)'],
  'b_node-display': ['Display node: ES3C28P', 'Anzeigeknoten: ES3C28P'],
  b_dial: ['M5Stack Dial', 'M5Stack Dial'],
  'b_node-pwr': ['Power node', 'Leistungsknoten'],
  'b_node-bal': ['Balance node', 'Balance-Knoten'],
  b_c6: ['C6 radio of the panel, over header H4 and a USB-to-UART adapter', 'C6-Funkchip des Panels, über Stiftleiste H4 und einen USB-UART-Adapter'],
  planned: ['not in a release yet', 'noch in keinem Release'],
  unsupported: ['needs {api}, which this browser lacks', 'braucht {api}, das diesem Browser fehlt'],
  hFirmware: ['2. Firmware', '2. Firmware'],
  srcRelease: ['Release', 'Release'],
  srcFile: ['Local file', 'Lokale Datei'],
  testing: ['testing', 'Testing'],
  stable: ['stable', 'Stable'],
  loading: ['Loading the list of releases …', 'Lade die Liste der Releases …'],
  noReleases: [
    'The list of releases is not available ({x}). A local file still works.',
    'Die Liste der Releases ist nicht verfügbar ({x}). Eine lokale Datei geht trotzdem.',
  ],
  noRelease: ['No release on this page carries an image for this board. A local file still works.', 'Kein Release auf dieser Seite enthält ein Image für dieses Board. Eine lokale Datei geht trotzdem.'],
  noImage: ['Release {tag} carries no image for this board.', 'Release {tag} enthält kein Image für dieses Board.'],
  fetching: ['Downloading {file} …', 'Lade {file} …'],
  fileHint: [
    'A merged image for 0x0 (.bin) for an ESP32 board; a UF2 file or a .bin for the flash start for an RP2350.',
    'Ein zusammengeführtes Image für 0x0 (.bin) für ein ESP32-Board; eine UF2-Datei oder eine .bin für den Flash-Anfang für einen RP2350.',
  ],
  binWarn: [
    'A .bin goes to 0x10000000, the start of the flash, over the partition table of an IOmcu. A UF2 file keeps the layout.',
    'Eine .bin landet bei 0x10000000, am Anfang des Flashs, über der Partitionstabelle einer IOmcu. Eine UF2-Datei behält die Aufteilung.',
  ],
  imgTag: ['Release', 'Release'],
  imgFile: ['File', 'Datei'],
  imgSize: ['Size', 'Größe'],
  imgSha: ['SHA-256', 'SHA-256'],
  imgCheck: ['Check', 'Prüfung'],
  imgOk: ['matches the manifest of the release', 'stimmt mit dem Manifest des Releases überein'],
  imgLocal: ['local file, no manifest to compare', 'lokale Datei, kein Manifest zum Vergleich'],
  imgErase: ['Also erases', 'Löscht außerdem'],
  imgEraseB: ['slot B, {x}: the written image is the one that starts', 'Slot B, {x}: das geschriebene Image ist das, das startet'],
  bytes: ['{n} bytes', '{n} Bytes'],
  hashBad: [
    'The download does not match the manifest: SHA-256 {got}, {want} expected. Nothing is written. Reload the page and try again.',
    'Der Download stimmt nicht mit dem Manifest überein: SHA-256 {got}, erwartet {want}. Es wird nichts geschrieben. Lade die Seite neu und versuche es noch einmal.',
  ],
  fetchBad: ['The download of {file} failed: {x}.', 'Der Download von {file} ist fehlgeschlagen: {x}.'],
  'bad_not-esp': ['{x} is no ESP-IDF image.', '{x} ist kein ESP-IDF-Image.'],
  'bad_app-only': [
    '{x} is an application image alone. The page writes merged images at 0x0: take the file with "merged" in its name.',
    '{x} ist nur ein Anwendungs-Image. Die Seite schreibt zusammengeführte Images bei 0x0: Nimm die Datei mit „merged“ im Namen.',
  ],
  bad_chip: ['This image is for an {x}, not for this board.', 'Dieses Image ist für einen {x}, nicht für dieses Board.'],
  bad_offset: ['The bootloader of this image sits at {x}, not where this chip starts it.', 'Der Bootloader dieses Images liegt bei {x}, nicht dort, wo dieser Chip ihn startet.'],
  'bad_not-rp2350': ['{x} is an ESP-IDF image, not an RP2350 image.', '{x} ist ein ESP-IDF-Image, kein RP2350-Image.'],
  bad_family: ['This UF2 file is for family {x}, which an RP2350 does not take.', 'Diese UF2-Datei ist für die Familie {x}, die ein RP2350 nicht annimmt.'],
  'bad_uf2-as-bin': ['{x} holds a UF2 file under the name .bin. Rename it to .uf2.', '{x} enthält eine UF2-Datei unter dem Namen .bin. Benenne sie in .uf2 um.'],
  bad_ext: ['{x}: an RP2350 takes a .uf2 or a .bin file.', '{x}: Ein RP2350 nimmt eine .uf2- oder eine .bin-Datei.'],
  bad_uf2: ['The UF2 file is damaged: {x}.', 'Die UF2-Datei ist beschädigt: {x}.'],
  hConnect: ['3. Connect', '3. Verbinden'],
  connectEsp: ['Connect', 'Verbinden'],
  connectEspHint: [
    'Connect the board by USB. The browser lists the serial ports; choose the board. The page resets the board into its boot ROM.',
    'Schließe das Board per USB an. Der Browser listet die seriellen Ports; wähle das Board. Die Seite setzt das Board in sein Boot-ROM zurück.',
  ],
  bootselBtn: ['Restart the IOmcu into BOOTSEL', 'IOmcu in BOOTSEL neu starten'],
  bootselHint: [
    'An IOmcu running rcbench firmware restarts into BOOTSEL when its USB serial port is opened at 1200 baud. Choose its port (USB ID 2E8A).',
    'Eine IOmcu mit rcbench-Firmware startet in BOOTSEL neu, wenn ihr USB-Serial-Port mit 1200 Baud geöffnet wird. Wähle ihren Port (USB-ID 2E8A).',
  ],
  bootselDone: ['The IOmcu restarts into BOOTSEL. Press Connect.', 'Die IOmcu startet in BOOTSEL neu. Drücke Verbinden.'],
  bootselManual: [
    'Without a running IOmcu: hold the BOOT button, plug in the USB cable, release BOOT. The board shows a drive named RP2350.',
    'Ohne laufende IOmcu: Halte die Taste BOOT, stecke das USB-Kabel ein, lass BOOT los. Das Board zeigt ein Laufwerk namens RP2350.',
  ],
  connectPico: ['Connect', 'Verbinden'],
  noBootrom: ['no RP2350 in BOOTSEL was chosen or found', 'kein RP2350 im BOOTSEL gewählt oder gefunden'],
  connected: ['Connected: {x}', 'Verbunden: {x}'],
  picoName: ['RP2350 in BOOTSEL (PICOBOOT)', 'RP2350 im BOOTSEL (PICOBOOT)'],
  chipBad: [
    'This image is for {want}; the connected chip is {got}. Nothing is written. Choose the board that matches, or connect the other board.',
    'Dieses Image ist für {want}; der angeschlossene Chip ist {got}. Es wird nichts geschrieben. Wähle das passende Board oder schließe das andere Board an.',
  ],
  chipUnknown: ['a chip the page does not flash', 'ein Chip, den die Seite nicht beschreibt'],
  disconnect: ['Disconnect', 'Trennen'],
  hWrite: ['4. Write', '4. Schreiben'],
  write: ['WRITE', 'SCHREIBEN'],
  writeHint: [
    'WRITE erases, writes, reads back and restarts the board. Keep the cable in until the page reports the end.',
    'SCHREIBEN löscht, schreibt, liest zurück und startet das Board neu. Lass das Kabel stecken, bis die Seite das Ende meldet.',
  ],
  needImage: ['Choose an image first.', 'Wähle zuerst ein Image.'],
  keep: ['Keep settings', 'Einstellungen behalten'],
  keepSub: [
    'the NVS (non-volatile storage) partition with the settings and the bindings is not written',
    'die NVS-Partition (non-volatile storage) mit den Einstellungen und den Kopplungen wird nicht geschrieben',
  ],
  keepList: ['Not written, kept on the board (partition table at {x}):', 'Nicht geschrieben, bleibt auf dem Board (Partitionstabelle bei {x}):'],
  keepItem: ['{name} ({sub}) at {offset}, {n} bytes', '{name} ({sub}) bei {offset}, {n} Bytes'],
  keepOta: [
    'otadata is written blank: the board starts ota_0, the slot with the written application.',
    'otadata wird leer geschrieben: Das Board startet ota_0, den Slot mit der geschriebenen Anwendung.',
  ],
  keepNone: [
    'The partition table at {x} has no settings partition: the whole image is written.',
    'Die Partitionstabelle bei {x} hat keine Einstellungs-Partition: Das ganze Image wird geschrieben.',
  ],
  keepOff: [
    'The whole image is written from 0x0, over the NVS partition: the settings and the bindings on the board are erased.',
    'Das ganze Image wird ab 0x0 geschrieben, über die NVS-Partition: Die Einstellungen und die Kopplungen auf dem Board werden gelöscht.',
  ],
  keepRefused: [
    'With "Keep settings" on, nothing is written. Untick it to write the whole image; that erases the settings.',
    'Mit „Einstellungen behalten“ wird nichts geschrieben. Nimm den Haken weg, um das ganze Image zu schreiben; das löscht die Einstellungen.',
  ],
  keep_bootloader: [
    'The bootloader header at {x} is damaged or runs past the end of the image, so the partition table cannot be found.',
    'Der Bootloader-Header bei {x} ist beschädigt oder reicht über das Ende des Images, daher ist die Partitionstabelle nicht auffindbar.',
  ],
  'keep_no-table': [
    'The image has no partition table between {x}, at steps of 4096 bytes.',
    'Das Image hat keine Partitionstabelle zwischen {x}, in Schritten von 4096 Bytes.',
  ],
  'keep_two-tables': ['The image holds two partition tables, at {x}.', 'Das Image enthält zwei Partitionstabellen, bei {x}.'],
  keep_end: ['The partition table at {x} has no end marker.', 'Die Partitionstabelle bei {x} hat keine Endmarke.'],
  'keep_md5-missing': ['The partition table at {x} has no MD5 entry.', 'Die Partitionstabelle bei {x} hat keinen MD5-Eintrag.'],
  keep_md5: [
    'The MD5 entry of the partition table does not match its entries: {x} (read, computed).',
    'Der MD5-Eintrag der Partitionstabelle passt nicht zu ihren Einträgen: {x} (gelesen, berechnet).',
  ],
  keep_magic: ['The partition table holds an entry without the magic bytes 0xAA50, at {x}.', 'Die Partitionstabelle enthält einen Eintrag ohne die Magic-Bytes 0xAA50, bei {x}.'],
  keep_layout: [
    'The partitions overlap or start inside the sector of the table: {x}.',
    'Die Partitionen überlappen sich oder beginnen im Sektor der Tabelle: {x}.',
  ],
  keep_overlap: [
    'The settings partition {x} overlaps the bootloader, the partition table or another partition.',
    'Die Einstellungs-Partition {x} überlappt den Bootloader, die Partitionstabelle oder eine andere Partition.',
  ],
  keep_align: [
    'The settings partition {x} does not start and end on a 4096-byte sector.',
    'Die Einstellungs-Partition {x} beginnt und endet nicht auf einem 4096-Byte-Sektor.',
  ],
  needConnect: ['Connect the board first.', 'Verbinde zuerst das Board.'],
  step_connect: ['Connect', 'Verbinden'],
  step_exclusive: ['Exclusive access', 'Exklusiver Zugriff'],
  step_partition: ['Partition', 'Partition'],
  step_erase: ['Erase', 'Löschen'],
  step_write: ['Write', 'Schreiben'],
  step_verify: ['Verify', 'Prüfen'],
  step_reset: ['Reset', 'Neustart'],
  step_reboot: ['Reboot', 'Neustart'],
  step_bootsel: ['BOOTSEL', 'BOOTSEL'],
  progress: ['{step}: {pct}%, {done} of {total} bytes, {s}s', '{step}: {pct} %, {done} von {total} Bytes, {s} s'],
  done: ['Done: {file} is written and verified, and the board restarts. {s}s in all.', 'Fertig: {file} ist geschrieben und geprüft, und das Board startet neu. {s} s insgesamt.'],
  doneKept: ['Kept on the board: {x}.', 'Auf dem Board behalten: {x}.'],
  failed: ['Failed in step {step}: {x}', 'Fehlgeschlagen im Schritt {step}: {x}'],
  try_connect_serial: [
    'Try: a USB data cable, not a charge-only one. Hold BOOT, press and release RESET (or plug the cable in), release BOOT, connect again. On Windows, a board whose USB socket goes through a USB-to-UART bridge chip needs that chip\'s driver (CH343, CP210x). The 7-inch panel board has 2 USB-C sockets and either one works; its bridge chip is not known.',
    'Versuche: ein USB-Datenkabel, kein reines Ladekabel. Halte BOOT, drücke und lass RESET los (oder stecke das Kabel ein), lass BOOT los, verbinde noch einmal. Unter Windows braucht ein Board, dessen USB-Buchse über einen USB-UART-Brückenchip läuft, den Treiber dieses Chips (CH343, CP210x). Das 7-Zoll-Panel-Board hat 2 USB-C-Buchsen, beide gehen; sein Brückenchip ist nicht bekannt.',
  ],
  try_connect_pico: [
    'Try: hold BOOT while you plug in the USB cable, then connect. Linux: the browser needs access to the USB device, for example through picotool\'s udev rule 99-picotool.rules. Windows: the boot ROM asks Windows for its WinUSB driver.',
    'Versuche: Halte BOOT, während du das USB-Kabel einsteckst, dann verbinde. Linux: Der Browser braucht Zugriff auf das USB-Gerät, zum Beispiel über die udev-Regel 99-picotool.rules von picotool. Windows: Das Boot-ROM fordert von Windows seinen WinUSB-Treiber an.',
  ],
  try_bootsel: [
    'Try: hold BOOT while you plug in the USB cable instead. An IOmcu without rcbench firmware does not answer the 1200 baud request.',
    'Versuche stattdessen: Halte BOOT, während du das USB-Kabel einsteckst. Eine IOmcu ohne rcbench-Firmware reagiert nicht auf die 1200-Baud-Anforderung.',
  ],
  try_transfer: [
    'Try: another USB cable, a USB port on the computer itself rather than a hub, then connect again and press WRITE. An interrupted write leaves the board in its boot ROM, ready for the next one.',
    'Versuche: ein anderes USB-Kabel, einen USB-Port direkt am Computer statt eines Hubs, dann verbinde noch einmal und drücke SCHREIBEN. Ein abgebrochener Schreibvorgang lässt das Board in seinem Boot-ROM, bereit für den nächsten.',
  ],
  try_verify: [
    'Try: write again. A second failure points at the cable or the flash chip.',
    'Versuche: noch einmal schreiben. Ein zweiter Fehler deutet auf das Kabel oder den Flash-Chip.',
  ],
  try_reset: [
    'The image is written and verified. Press RESET on the board, or unplug it and plug it in.',
    'Das Image ist geschrieben und geprüft. Drücke RESET am Board oder zieh es ab und steck es wieder ein.',
  ],
  try_rom: [
    'Try: unplug the board, enter BOOTSEL again and connect.',
    'Versuche: Zieh das Board ab, geh noch einmal in BOOTSEL und verbinde.',
  ],
  log: ['Details', 'Details'],
  footNoData: [
    'This page sends no data and loads nothing from other servers. The firmware images are copies of the release assets on this site.',
    'Diese Seite sendet keine Daten und lädt nichts von anderen Servern. Die Firmware-Images sind Kopien der Release-Assets auf dieser Seite.',
  ],
  footDev: [
    'rcbench is under development, and its functions are not verified on hardware.',
    'rcbench ist in Entwicklung, und seine Funktionen sind nicht auf Hardware verifiziert.',
  ],
  footConfigurator: ['Configurator', 'Konfigurator'],
  footLicences: ['Licences of the bundled libraries', 'Lizenzen der eingebundenen Bibliotheken'],
  footNotice: ['Trademark notice', 'Markenhinweis'],
  footNames: [
    'Product and company names belong to their respective owners. They are used to describe compatibility. There is no affiliation.',
    'Produkt- und Firmennamen gehören ihren jeweiligen Inhabern. Sie dienen der Beschreibung der Kompatibilität. Es besteht keine Verbindung zu den Herstellern.',
  ],
} as const satisfies Record<string, readonly [string, string]>;

export type Key = keyof typeof S;

/** The string of a key in a language, with each {name} replaced. */
export function t(lang: Lang, key: Key, vars: Record<string, string | number> = {}): string {
  const s: string = S[key][lang === 'de' ? 1 : 0];
  return s.replace(/\{(\w+)\}/g, (m, name: string) => (name in vars ? String(vars[name]) : m));
}

export const hasKey = (key: string): key is Key => key in S;

/** The start language: German for a browser that prefers German, else English. */
export function browserLang(languages: readonly string[]): Lang {
  const first = languages[0]?.toLowerCase() ?? 'en';
  return first.startsWith('de') ? 'de' : 'en';
}

/** Every key with both strings, for the tests. */
export const ALL_KEYS = Object.keys(S) as Key[];
export const strings = (key: Key): readonly [string, string] => S[key];
