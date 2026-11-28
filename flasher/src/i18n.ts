// Strings of the page in English and German. The start language follows the
// browser; the buttons EN and DE switch it.

export type Lang = 'en' | 'de';

const S = {
  title: ['rcbench web flasher', 'rcbench-Web-Flasher'],
  sub: [
    'Install rcbench firmware on the panel, the IOmcu or a wireless device from your browser.',
    'Installiere rcbench-Firmware direkt im Browser auf dem Panel, der IOmcu oder einem Funkgerät.',
  ],
  langGroup: ['Language / Sprache', 'Language / Sprache'],
  sTask: ['What do you want to do?', 'Was möchtest du tun?'],
  sBoard: ['Which board?', 'Welches Board?'],
  sPrepare: ['Prepare the panel', 'Panel vorbereiten'],
  sConnect: ['Connect the board', 'Board verbinden'],
  sCheck: ['Check before writing', 'Vor dem Schreiben prüfen'],
  sWrite: ['Writing', 'Schreiben'],
  sDone: ['Done', 'Fertig'],
  stepOf: ['Step {n} of {m}: {title}', 'Schritt {n} von {m}: {title}'],
  task_panel: ['Install or update the panel', 'Panel installieren oder aktualisieren'],
  task_iomcu: ['Install the IOmcu', 'IOmcu installieren'],
  task_wireless: ['Set up a wireless device', 'Funkgerät einrichten'],
  tsub_wireless: ['M5Stack Dial, display node, power node, balance node', 'M5Stack Dial, Anzeigeknoten, Leistungsknoten, Auswuchtknoten'],
  task_c6: ['Repair the panel\'s C6 radio', 'C6-Funk des Panels reparieren'],
  tsub_c6: ['over header H4 and a USB-to-UART adapter', 'über Stiftleiste H4 und einen USB-UART-Adapter'],
  tasksNote: [
    'A running bench updates the IOmcu, the C6 radio, the Dial and the nodes from the panel. This page is for a first install and for a board that does not start.',
    'Ein laufender Prüfstand aktualisiert IOmcu, C6-Funk, Dial und Knoten vom Panel aus. Diese Seite ist für eine Erstinstallation und für ein Board, das nicht startet.',
  ],
  task_file: ['Write a local firmware file', 'Lokale Firmware-Datei schreiben'],
  tsub_file: ['any of the 9 boards, a .bin or .uf2 file from this computer', 'eines der 9 Boards, eine .bin- oder .uf2-Datei von diesem Computer'],
  advSwitch: ['Advanced: local firmware, older releases, or erase settings during installation', 'Erweitert: lokale Firmware, ältere Releases oder Einstellungen bei der Installation löschen'],
  hintFile: ['Advanced mode on the first screen writes a local file.', 'Der erweiterte Modus auf der ersten Seite schreibt eine lokale Datei.'],
  hintErase: [
    'Advanced mode on the first screen writes the whole image; that erases the settings.',
    'Der erweiterte Modus auf der ersten Seite schreibt das ganze Image; das löscht die Einstellungen.',
  ],
  back: ['BACK', 'ZURÜCK'],
  cancel: ['CANCEL', 'ABBRECHEN'],
  next: ['NEXT', 'WEITER'],
  retry: ['RETRY', 'ERNEUT LADEN'],
  connectAgain: ['CONNECT AGAIN', 'NEU VERBINDEN'],
  writeAnother: ['WRITE ANOTHER: {board}', 'NOCH EINES SCHREIBEN: {board}'],
  startOver: ['START OVER', 'VON VORN'],
  chooseOther: ['CHOOSE ANOTHER BOARD', 'ANDERES BOARD WÄHLEN'],
  c6_1: ['Switch the panel off.', 'Schalte das Panel aus.'],
  c6_2: [
    'Wire a 3.3V USB-to-UART adapter: its RX to H4 TXD0, its TX to H4 RXD0, its GND to H4 GND. Tie H4 IO9 to GND.',
    'Verdrahte einen 3,3V-USB-UART-Adapter: sein RX an H4 TXD0, sein TX an H4 RXD0, sein GND an H4 GND. Verbinde H4 IO9 mit GND.',
  ],
  c6_3: [
    'Switch the panel on. Open neither WI-FI nor the C6 RADIO hold: a Wi-Fi start resets the C6 and ends the write.',
    'Schalte das Panel ein. Öffne weder WI-FI noch das Halten von C6 RADIO: Ein Wi-Fi-Start setzt den C6 zurück und beendet das Schreiben.',
  ],
  c6Pins: [
    'The drawing names the signals of H4; the order of its pins is on the board\'s silkscreen. The dashed line ties IO9 to GND.',
    'Die Zeichnung nennt die Signale von H4; die Reihenfolge der Pins steht im Bestückungsdruck des Boards. Die gestrichelte Linie verbindet IO9 mit GND.',
  ],
  cn_panel: ['Use either USB-C port on the panel board.', 'Verwende eine der beiden USB-C-Buchsen am Panel-Board.'],
  cn_dial: [
    'A Dial without rcbench firmware: hold the Stamp button (GPIO0) while you plug in the cable.',
    'Ein Dial ohne rcbench-Firmware: Halte die Stamp-Taste (GPIO0), während du das Kabel einsteckst.',
  ],
  'cn_node-display': [
    'Disconnect the battery from the XT60 connector first. A board without rcbench firmware: hold BOOT (IO0) while you plug in the cable.',
    'Trenne zuerst den Akku vom XT60-Stecker. Ein Board ohne rcbench-Firmware: Halte BOOT (IO0), während du das Kabel einsteckst.',
  ],
  'cn_node-pwr': [
    'Disconnect the battery from the XT60 connector first. A board without rcbench firmware: hold BOOT (GPIO9) while you plug in the cable.',
    'Trenne zuerst den Akku vom XT60-Stecker. Ein Board ohne rcbench-Firmware: Halte BOOT (GPIO9), während du das Kabel einsteckst.',
  ],
  'cn_node-bal': [
    'Disconnect the battery from the XT60 connector first. A board without rcbench firmware: hold BOOT (GPIO9) while you plug in the cable.',
    'Trenne zuerst den Akku vom XT60-Stecker. Ein Board ohne rcbench-Firmware: Halte BOOT (GPIO9), während du das Kabel einsteckst.',
  ],
  cn_c6: [
    'Plug the USB-to-UART adapter into this computer. The browser lists the serial ports; choose the adapter\'s port.',
    'Stecke den USB-UART-Adapter an diesen Computer. Der Browser listet die seriellen Ports; wähle den Port des Adapters.',
  ],
  nx_panel: [
    'Next: power the panel and the IOmcu as the configurator\'s wiring shows. The splash shows LINK OK CAN 1Mbit/s.',
    'Weiter: Versorge Panel und IOmcu, wie die Verdrahtung im Konfigurator zeigt. Der Startbildschirm zeigt LINK OK CAN 1Mbit/s.',
  ],
  nx_iomcu: [
    'Next: power the panel and the IOmcu as the configurator\'s wiring shows. SETUP > OUTPUTS on the panel shows the IOmcu\'s pins.',
    'Weiter: Versorge Panel und IOmcu, wie die Verdrahtung im Konfigurator zeigt. SETUP > OUTPUTS auf dem Panel zeigt die Pins der IOmcu.',
  ],
  'nx_iomcu-rbmod': [
    'Next: power the panel and the IOmcu as the configurator\'s wiring shows. SETUP > OUTPUTS on the panel shows the grid of the rbmod carrier.',
    'Weiter: Versorge Panel und IOmcu, wie die Verdrahtung im Konfigurator zeigt. SETUP > OUTPUTS auf dem Panel zeigt das Raster des rbmod-Trägers.',
  ],
  nx_dial: [
    'Next: the Dial shows NOT BOUND and its name, DIAL- and 4 hex digits. Bind it on the panel: SETUP > WIRELESS > DIAL.',
    'Weiter: Das Dial zeigt NOT BOUND und seinen Namen, DIAL- und 4 Hex-Ziffern. Binde es am Panel: SETUP > WIRELESS > DIAL.',
  ],
  nk_dial: [
    'Next: the Dial keeps its settings and its binding to the panel. A Dial never bound shows NOT BOUND: bind it on the panel: SETUP > WIRELESS > DIAL.',
    'Weiter: Das Dial behält seine Einstellungen und seine Bindung an das Panel. Ein nie gebundenes Dial zeigt NOT BOUND: Binde es am Panel: SETUP > WIRELESS > DIAL.',
  ],
  'nx_node-display': [
    'Next: a node bound before stays bound: the panel binds it by its address. A node never bound: bind it on the panel: SETUP > WIRELESS > POWER NODE.',
    'Weiter: Ein schon gebundener Knoten bleibt gebunden: Das Panel bindet ihn über seine Adresse. Ein nie gebundener Knoten: Binde ihn am Panel: SETUP > WIRELESS > POWER NODE.',
  ],
  'nx_node-pwr': [
    'Next: a node bound before stays bound: the panel binds it by its address. A node never bound: bind it on the panel: SETUP > WIRELESS > POWER NODE.',
    'Weiter: Ein schon gebundener Knoten bleibt gebunden: Das Panel bindet ihn über seine Adresse. Ein nie gebundener Knoten: Binde ihn am Panel: SETUP > WIRELESS > POWER NODE.',
  ],
  'nx_node-bal': [
    'Next: a node bound before stays bound: the panel binds it by its address. A node never bound: bind it on the panel: SETUP > WIRELESS > BALANCE NODE.',
    'Weiter: Ein schon gebundener Knoten bleibt gebunden: Das Panel bindet ihn über seine Adresse. Ein nie gebundener Knoten: Binde ihn am Panel: SETUP > WIRELESS > BALANCE NODE.',
  ],
  nx_c6: [
    'Next: switch the panel off, take IO9 off GND, switch the panel on and start Wi-Fi once. The console shows the C6\'s esp_hosted firmware version.',
    'Weiter: Schalte das Panel aus, nimm IO9 von GND, schalte das Panel ein und starte Wi-Fi einmal. Die Konsole zeigt die esp_hosted-Firmwareversion des C6.',
  ],
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
  b_panel: ['Panel: Waveshare ESP32-P4 7-inch board', 'Panel: Waveshare-ESP32-P4-Board, 7 Zoll'],
  b_iomcu: ['IOmcu on the Waveshare RP2350-CAN', 'IOmcu auf dem Waveshare RP2350-CAN'],
  'b_iomcu-rbmod': ['IOmcu on the rbmod carrier', 'IOmcu auf dem rbmod-Träger'],
  b_core2350b: ['Remote servo node: Waveshare Core2350B (RP2350B)', 'Servo-Fernknoten: Waveshare Core2350B (RP2350B)'],
  'b_node-display': ['Display node: ES3C28P', 'Anzeigeknoten: ES3C28P'],
  b_dial: ['M5Stack Dial', 'M5Stack Dial'],
  'b_node-pwr': ['Power node', 'Leistungsknoten'],
  'b_node-bal': ['Balance node', 'Auswuchtknoten'],
  b_c6: ['C6 radio of the panel, over header H4 and a USB-to-UART adapter', 'C6-Funk des Panels, über Stiftleiste H4 und einen USB-UART-Adapter'],
  unsupported: ['needs {api}, which this browser lacks', 'braucht {api}, das diesem Browser fehlt'],
  noReleases: ['The list of releases is not available ({x}).', 'Die Liste der Releases ist nicht verfügbar ({x}).'],
  noRelease: ['No release on this page carries an image for this board.', 'Kein Release auf dieser Seite enthält ein Image für dieses Board.'],
  rowBoard: ['Board', 'Board'],
  rowConn: ['Connected', 'Verbunden'],
  rowSig: ['Signature', 'Signatur'],
  sigSigned: ['signed by the rcbench release key ({file})', 'signiert mit dem rcbench-Release-Schlüssel ({file})'],
  sigNone: [
    'Signature: no signature ({file} is missing or empty). Releases before the signed ones carry none.',
    'Signatur: keine Signatur ({file} fehlt oder ist leer). Releases vor den signierten tragen keine.',
  ],
  sigMismatch: [
    'Signature: the signature does not match this image ({file}). The rcbench release key did not sign it. Nothing is written.',
    'Signatur: Die Signatur passt nicht zu diesem Image ({file}). Der rcbench-Release-Schlüssel hat es nicht signiert. Es wird nichts geschrieben.',
  ],
  keepRefused: [
    'This page keeps the settings on the board and finds no safe place for them in this image. Nothing is written.',
    'Diese Seite behält die Einstellungen auf dem Board und findet in diesem Image keinen sicheren Platz dafür. Es wird nichts geschrieben.',
  ],
  chipBad: [
    'The connected chip is {got}; this board needs {want}. Nothing is written.',
    'Der angeschlossene Chip ist {got}; dieses Board braucht {want}. Es wird nichts geschrieben.',
  ],
  connectEsp: ['CONNECT', 'VERBINDEN'],
  connectPico: ['CONNECT', 'VERBINDEN'],
  disconnect: ['DISCONNECT', 'TRENNEN'],
  writeKeep: ['Keep the cable connected until the page reports that writing has finished.', 'Lass das Kabel stecken, bis die Seite meldet, dass der Schreibvorgang beendet ist.'],
  testing: ['testing', 'Testing'],
  stable: ['stable', 'Stable'],
  loading: ['Loading the list of releases …', 'Lade die Liste der Releases …'],
  noImage: ['Release {tag} carries no image for this board.', 'Release {tag} enthält kein Image für dieses Board.'],
  fetching: ['Downloading {file} …', 'Lade {file} …'],
  imgTag: ['Release', 'Release'],
  imgFile: ['File', 'Datei'],
  imgSize: ['Size', 'Größe'],
  imgSha: ['SHA-256', 'SHA-256'],
  imgCheck: ['Check', 'Prüfung'],
  imgOk: ['matches the manifest of the release', 'stimmt mit dem Manifest des Releases überein'],
  imgErase: ['Also erases', 'Löscht außerdem'],
  imgEraseB: ['slot B, {x}: the board boots from the image you write', 'Slot B, {x}: Das Board startet mit dem Image, das du schreibst'],
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
  connectEspHint: [
    'Connect the board by USB. The browser lists the serial ports; choose the board. The page resets the board into its boot ROM.',
    'Schließe das Board per USB an. Der Browser listet die seriellen Ports; wähle das Board. Die Seite setzt das Board in sein Boot-ROM zurück.',
  ],
  bootselBtn: ['Restart the IOmcu into BOOTSEL', 'IOmcu in BOOTSEL neu starten'],
  bootselHint: [
    'An IOmcu running rcbench firmware restarts into BOOTSEL when its USB serial port is opened at 1200 baud. Choose its port (USB ID 2E8A).',
    'Eine IOmcu mit rcbench-Firmware startet in BOOTSEL neu, wenn ihr USB-Serial-Port mit 1200 Baud geöffnet wird. Wähle ihren Port (USB-ID 2E8A).',
  ],
  bootselDone: ['The IOmcu restarts into BOOTSEL. Press CONNECT.', 'Die IOmcu startet in BOOTSEL neu. Drücke VERBINDEN.'],
  bootselManual: [
    'Without a running IOmcu: hold the BOOT button, plug in the USB cable, release BOOT. The board shows a drive named RP2350.',
    'Ohne laufende IOmcu: Halte die Taste BOOT, stecke das USB-Kabel ein, lass BOOT los. Das Board zeigt ein Laufwerk namens RP2350.',
  ],
  noBootrom: ['no RP2350 in BOOTSEL was chosen or found', 'kein RP2350 im BOOTSEL gewählt oder gefunden'],
  connected: ['Connected: {x}', 'Verbunden: {x}'],
  picoName: ['RP2350 in BOOTSEL (PICOBOOT)', 'RP2350 im BOOTSEL (PICOBOOT)'],
  chipUnknown: ['a chip the page does not flash', 'ein Chip, den die Seite nicht beschreibt'],
  write: ['WRITE', 'SCHREIBEN'],
  writeHint: [
    'WRITE erases, writes, reads back and restarts the board. Keep the cable connected until the page reports that writing has finished.',
    'SCHREIBEN löscht, schreibt, liest zurück und startet das Board neu. Lass das Kabel stecken, bis die Seite meldet, dass der Schreibvorgang beendet ist.',
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

/**
 * The key of the next-step line on Done. `keptNvs`: the write left an NVS
 * partition on the board. The Dial keeps its bound panel in its own NVS, so
 * it has an `nk_` text for a kept NVS; a node is bound by its address on the
 * panel, so its `nx_` text holds either way.
 */
export function nextKey(boardId: string, keptNvs: boolean): Key | null {
  if (keptNvs && hasKey(`nk_${boardId}`)) return `nk_${boardId}` as Key;
  return hasKey(`nx_${boardId}`) ? (`nx_${boardId}` as Key) : null;
}

/** Every key with both strings, for the tests. */
export const ALL_KEYS = Object.keys(S) as Key[];
export const strings = (key: Key): readonly [string, string] => S[key];
