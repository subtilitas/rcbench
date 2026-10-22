# Erster Lauf auf der Hardware

<sub>[English](FirstRun.md) · **Deutsch**</sub>

Beide Platinen mit gestecktem Heartbeat-Draht unter Strom, und jede Zahl unten
an einem Messgerät gelesen. Geschrieben für 0.8.1. Jeder Schritt sagt, wie „gut“ aussieht und was aufzuschreiben ist,
wenn es das nicht tut.

Die Liste von oben nach unten abarbeiten. Jeder Schritt setzt voraus, dass
der darüber bestanden hat.

---

## 0. Vor dem Einschalten

**Bereitlegen:** ein Oszilloskop, ein Servo, einen ESC, der bidirektionales
DShot spricht, ein Labornetzteil mit Strombegrenzung und das USB-Kabel für
jede Platine.

**Noch keinen Motor an den ESC.** Schritt 5 treibt überhaupt keinen Pin -- er
ist die Verriegelung, und kein Ausgang ist dort das bestandene Ergebnis.
Schritt 6 ist der erste Pin an einem Messgerät, und dort schaut man zuerst auf
das Oszilloskop und nicht auf einen Propeller.

**Strombegrenzung:** so niedrig, dass ein kurzgeschlossener Output sie
auslöst, statt eine Leiterbahn zu verbrennen.

> ### Den UART-Schalter des Panels vor allem anderen auf `UART1` stellen
>
> Neben den Tastern **BOOT** und **RESET** sitzt ein Schiebeschalter,
> beschriftet mit **`UART1`** und **`UART2`**. Er wählt aus, womit die
> serielle Seite der gebrückten USB-C-Buchse verbunden ist; die Konsole
> braucht ihn auf **`UART1`**.
>
> In der anderen Stellung meldet sich die Buchse trotzdem an: ein COM-Port
> erscheint, das Betriebssystem benennt den CH343. Es kommt in keine Richtung
> etwas durch — keine Konsolenausgabe, kein Flashen — also dasselbe Bild, das
> ein defektes Kabel oder eine defekte Platine liefert.
>
> Die andere USB-C-Buchse des Panels führt natives USB auf GPIO19 und GPIO20,
> die der Multiplexer etwa eine Sekunde nach jedem Start an CAN übergibt. Sie
> flasht die Platine und kann eine laufende nicht beobachten. Die gebrückte
> Buchse ist die einzige Konsole des Panels im Betrieb.

---

## 1. Beide Platinen flashen

Was eine Bench für 0.22.0 lädt, und wie, hängt vom Release ab, das sie hat:

| Bench hat | IOmcu | Panel |
| --- | --- | --- |
| nichts (eine neue Bench), oder 0.18.0 und älter | über USB (Universal Serial Bus), [Schritt 2](#aus-den-release-assets) | über USB, [Schritt 3](#aus-den-release-assets) |
| 0.19.0 | über CAN (Controller Area Network) vom Panel ([Ab 0.19.0](#ab-0190)) | über USB, [Schritt 3](#aus-den-release-assets) |
| 0.20.0 | über CAN vom Panel ([Ab 0.20.0](#ab-0200)) | über WLAN: NACH UPDATES SUCHEN, oder die deutsche Browser-Seite; über USB mit gespeichertem Drehknopf EIN |
| 0.21.0 | über CAN vom Panel ([Ab 0.21.0](#ab-0210)) | über WLAN: NACH UPDATES SUCHEN, oder der Upload auf der Browser-Seite |
| 0.21.1 | über CAN vom Panel ([Ab 0.21.1](#ab-0211)) | über WLAN: NACH UPDATES SUCHEN, oder der Upload auf der Browser-Seite |
| 0.21.2 | über CAN vom Panel ([Ab 0.21.2](#ab-0212)) | über WLAN: NACH UPDATES SUCHEN, oder der Upload auf der Browser-Seite |
| 0.22.0 und spätere 0.22.x | über CAN vom Panel | über WLAN |

Das USB-Laden einer neuen Bench, oder einer mit 0.18.0 und älter, bringt
die Partitionstabelle des IOmcu und die zwei Anwendungs-Slots des
Panels an ihren Platz, und keins von beiden kommt über den Link. Ein Panel
mit 0.19.0 nimmt kein Image über WLAN: WLAN-HOTSPOT endet beim Drücken mit
`WLAN konnte nicht starten.`, und nach der Schätzung des internen RAM
(Random-Access Memory) in [Performance](Performance-de.md#interner-ram)
endet MIT HEIMNETZ VERBINDEN genauso. Ab 0.20.0 installiert das Panel ein
Release über WLAN ([Bildschirme:
WLAN](Screens-de.md#wlan)) oder lädt es von GitHub
([Bildschirme: Updates](Screens-de.md#updates)), und der IOmcu bekommt
sein Image vom Panel über CAN.

0.22.0 ist ein Stable-Release ([Bauen: Releases](Building-de.md#releases))
und das letzte Release für das ESP32-S3-Panel. Spätere Releases sind für
das 7-Zoll-Display-Board mit ESP32-P4 von Waveshare. NACH UPDATES SUCHEN
mit `RELEASES: STABLE` liest `releases/latest`, das das neueste
Stable-Release nennt, v0.22.0; ein Panel mit 0.20.0, das keine Einstellung
RELEASES hat, liest dasselbe. Einem Panel mit 0.22.0 werden nur
Patch-Releases 0.22.x angeboten
([Bildschirme: Die Release-Linie](Screens-de.md#die-release-linie)). Ein
Panel älter als 0.22.0 hat keine Release-Linie und bekommt jedes neuere
Release angeboten, das GitHub nennt, auch eines für das ESP32-P4-Panel; die
Prüfungen des Panels lehnen ein Image für einen anderen Chip mit `Die
Firmware hat die Prüfungen des Panels nicht bestanden.` ab (0.20.0: `Das
Image fiel durch die Prüfungen.`), und das Panel behält seinen Build. Ein
solches Panel lädt 0.22.0 per Upload auf seiner Browser-Seite oder über
USB.

Die SD-Karte (Secure Digital) trägt ein FAT32-, FAT16-, FAT12- oder
exFAT-Volume (extended FAT), also mountet eine Karte bis 2 TB in dem Format,
mit dem sie verkauft wird. 0.21.1 und älter lesen kein exFAT. 0.22.0 liest
kein NTFS (New Technology File System), keine Karte mit GPT
(GUID-Partitionstabelle) und kein Volume über 2 TiB, und das Panel
formatiert keine Karte ([Bildschirme: Das Format der
SD-Karte](Screens-de.md#das-format-der-sd-karte)).

### Aus den Release-Assets

| Asset | Enthält | Für |
| --- | --- | --- |
| `rcbench-iomcu-vX.Y.Z.zip` | `rcbench-iomcu-install.uf2`, `rcbench-iomcu.uf2`, `rcbench-iomcu-pt.uf2`, `rcbench-iomcu.bin`, `rcbench-iomcu.elf` | das USB-Laden des IOmcu: `rcbench-iomcu-install.uf2` |
| `rcbench-panel-vX.Y.Z.zip` | `flash_args`, `bootloader/bootloader.bin`, `partition_table/partition-table.bin`, `ota_data_initial.bin`, `rcbench-panel.bin`, `rcbench-panel-merged.bin`, `rcbench-panel.elf` | den USB-Flash des Panels |
| `rcbench-panel-vX.Y.Z.bin` | die Anwendung des Panels allein, mit dem IOmcu-Image, das es über CAN anbietet | ein Update über WLAN auf einem Panel mit 0.20.0 oder neuer; nicht für USB |
| `SHA256SUMS` | die SHA-256 (Secure Hash Algorithm) der drei Dateien oben | die Prüfung in Schritt 1 |

Werkzeuge: das Laufwerk `RP2350` oder `picotool` für den IOmcu,
`esptool.py` für das Panel (`pip install esptool`; ESP-IDF bringt es mit).
`/dev/ttyACM0` unten heißt unter Windows `COMx`.

1. Im Download-Ordner gibt `sha256sum -c SHA256SUMS` für jede der drei
   Dateien `OK` aus.
2. **IOmcu.** `rcbench-iomcu-vX.Y.Z.zip` entpacken. BOOTSEL halten,
   während die USB-Buchse des IOmcu angeschlossen wird, sodass das
   Modul sein Laufwerk `RP2350` zeigt. `rcbench-iomcu-install.uf2` auf das
   Laufwerk kopieren, oder:

   ```bash
   picotool load -v -x rcbench-iomcu-install.uf2
   ```

   Das Modul startet neu in Slot A. Seine Konsole gibt alle 3 s
   `rcbench-iomcu: running from slot A` aus
   ([unten](#die-partitionstabelle-des-iomcu)).
3. **Panel.** `rcbench-panel-vX.Y.Z.zip` entpacken und die USB-C-Buchse des
   Panels anschließen (gebrückte Buchse: Schalter auf `UART1`,
   [§0](#0-vor-dem-einschalten)). Im entpackten Ordner, auf einer Bench,
   deren Einstellungen bleiben:

   ```bash
   esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash @flash_args
   ```

   Auf einer neuen Bench, oder um mit den Standardeinstellungen zu beginnen:

   ```bash
   esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 rcbench-panel-merged.bin
   ```

   Ein Browser-Flasher, der eine Datei an einen Offset schreibt, etwa das
   ESP Web Tool, nimmt `rcbench-panel-merged.bin` bei 0x0, mit derselben
   Wirkung wie der zweite Befehl.
4. Beide Platinen mit verdrahtetem Link einschalten. Das Panel findet auf
   dem IOmcu den Build, den es trägt, und bietet kein Update an. Es
   lädt die Zeichnung der Platine neu
   ([§4](#4-erster-link-aufbau-die-zeichnung-der-platine)).

| Was | Einzelteile (`@flash_args`) | Zusammengeführtes Image bei 0x0 |
| --- | --- | --- |
| Einstellungen und WLAN-Einstellungen des Panels, im NVS (non-volatile storage, 24 KiB bei 0x9000) | bleiben | gelöscht; das Panel startet mit den Standardwerten |
| Gespeicherte Platinenzeichnung des Panels | einmal verloren; beim Link-Aufbau neu geladen | einmal verloren; beim Link-Aufbau neu geladen |

Der IOmcu behält seine Bindungen, die Verdrahtung der Versorgung und
die Einrichtung der Strommonitore, mit der Ausnahme für einen RP2350 der
Stepping A2, die [unten](#die-partitionstabelle-des-iomcu) steht.

Das Panel darf zuerst kommen. Ein Panel mit 0.19.0 oder neuer an einem
IOmcu ohne Partitionstabelle zeigt `IOmcu kann nicht über CAN aktualisiert werden. Firmware einmal über USB installieren.`, bis Schritt 2 erledigt ist; der Link läuft
solange.

### Ab 0.19.0

Eine Bench mit 0.19.0 lädt das Panel über USB, und nur das Panel. Der
IOmcu hat die Partitionstabelle vom USB-Laden für 0.19.0 und bekommt
0.22.0 über CAN aus dem Image, das das neue Panel mitbringt. Das
Link-Protokoll von 0.22.0 ist 4.28, das von 0.19.0 ist 4.22; die
Hauptversion, 4, ist gleich, also verbinden sich beide Platinen vor und
nach dem Update.

1. Im Download-Ordner gibt `sha256sum -c SHA256SUMS` für jede der drei
   Dateien `OK` aus.
2. **Panel.** `rcbench-panel-v0.22.0.zip` wie in
   [Schritt 3](#aus-den-release-assets) oben flashen: `write_flash
   @flash_args`, um die Einstellungen zu behalten, oder
   `rcbench-panel-merged.bin` bei 0x0, mit `esptool.py` oder dem
   Browser-Flasher, um mit den Standardeinstellungen zu beginnen.
3. Beide Platinen mit verdrahtetem Link einschalten. Beim Link-Aufbau
   vergleicht das Panel das laufende Image des IOmcu per SHA-256 mit
   dem Image, das es mitbringt, findet einen anderen Build, und das Band
   meldet `IOmcu nutzt einen anderen Build. Update unter SETUP > UPDATES.`.
4. **IOmcu.** Die Frage öffnet sich auf dem Menü, solange die Bench
   SAFE ist, der Ausgang der Versorgung aus und der Funk des Panels aus. Sie
   nennt 0.19.0 auf dem IOmcu und 0.22.0 im Panel. START 2 s halten.
   Der Fortschritt füllt den Bildschirm ([Bildschirme:
   IOmcu-Update](Screens-de.md#iomcu-update)), und zurück auf
   dem Menü meldet das Band `IOmcu auf 0.22.0 aktualisiert.`.

| Was | Einzelteile (`@flash_args`) | Zusammengeführtes Image bei 0x0 |
| --- | --- | --- |
| Einstellungen und WLAN-Einstellungen des Panels, im NVS (24 KiB bei 0x9000) | bleiben | gelöscht; das Panel startet mit den Standardwerten |
| Gespeicherte Platinenzeichnung des Panels, `boardart` bei 0x7E0000 | bleibt: das letzte Teil endet mit der Anwendung, etwa 2,1 MB ab 0x0 | bleibt: das Image endet mit der Anwendung, etwa 2,1 MB ab 0x0 |

Das Panel zeigt die gespeicherte Zeichnung wieder, wenn die ARTWORK-Seite
des IOmcu dieselbe Zeichnung nennt, und lädt sie sonst neu
([§4](#4-erster-link-aufbau-die-zeichnung-der-platine)). Die Einstellung
ERINNERUNG BEIM START ist neu in 0.20.0 und steht auf EIN.

Das Update des IOmcu schreibt den Slot, aus dem er nicht läuft.
Seine Bindungen, die Verdrahtung der Versorgung und die Einrichtung der
Strommonitore bleiben: sie liegen in der Store-Partition bei 0x3FC000, oder
vor deren erstem Speichern im alten Store bei 0x3FE000, und das Update
schreibt keins von beiden. CANCEL auf der Frage lässt den IOmcu auf
0.19.0; das Panel fragt nach seinem nächsten Start wieder. Fällt während des
Updates der Strom aus, läuft das alte Image weiter, und der nächste
Link-Aufbau bietet das Update wieder an. Ein IOmcu, der das USB-Laden
für 0.19.0 verpasst hat, hat keine Partitionstabelle: das Band meldet
`IOmcu kann nicht über CAN aktualisiert werden. Firmware einmal über USB installieren.`, und er
bekommt [Schritt 2](#aus-den-release-assets). Die Dauer des Updates über CAN
ist nicht gemessen.

### Ab 0.20.0

Eine Bench mit 0.20.0 lädt das Panel über WLAN, von GitHub oder mit der
deutschen Fassung der Browser-Seite des Panels, und den IOmcu über CAN aus
dem Image, das das neue Panel mitbringt. Drei Fehler von 0.20.0 bestimmen
den Weg:

| Fehler von 0.20.0 | Wirkung | Weg darum herum |
| --- | --- | --- |
| Das Skript der englischen Browser-Seite lässt sich nicht parsen: der Text `The file is larger than the panel's slot.` beendet seinen String am Apostroph. | Upload schickt das Formular ohne die Datei, an `/?`. | Die Seite folgt der Sprache des Panels; die deutsche Seite nimmt den Upload. |
| Ein Abbruch der Auth-Klasse beendet ein Verbinden. | HEIMNETZ VERBINDEN, NACH UPDATES SUCHEN und der Download können mit `Verbinden scheiterte: Passwort abgelehnt.` enden, auch bei richtigem Passwort. | Noch einmal drücken; ACCESS POINT verbindet sich mit keinem Netz. |
| Drehknopf EIN fragt einen AS5600 bei 0x36 ab, eine der Befehlsadressen des I/O-Expanders CH422G. | Das Panel wird bei der ersten Abfrage des Drehknopfs nach jedem Start dunkel, CAN fällt vom Link, und der Touch-Controller kann im Reset bleiben. | Ein Panel mit gespeichertem Drehknopf EIN lädt über USB. |

Von GitHub, mit gespeichertem Heimnetz, der Bench SAFE, dem Ausgang der
Versorgung aus und keinem offenen Log-Lauf: SETUP > UPDATES, `NACH UPDATES
SUCHEN`. Das Panel liest `releases/latest`, das v0.22.0 nennt, und meldet
`Version 0.22.0 ist verfügbar.`. `2 s HALTEN: DOWNLOAD & INSTALL` halten;
das Panel lädt das Image, prüft es, schreibt den Slot, aus dem es nicht
läuft, und startet neu in ihn, und Schritt 5 unten folgt. Mit der
Browser-Seite:

1. Im Download-Ordner gibt `sha256sum -c SHA256SUMS` für jede der drei
   Dateien `OK` aus.
2. **Sprache.** Ein Panel auf Englisch: SETUP > APPLICATION, Language
   `DEUTSCH`. Sie gilt sofort; nicht speichern, dann startet der Neustart
   in Schritt 4 0.22.0 in der gespeicherten Sprache. Ein Panel auf Deutsch
   lässt den Schritt aus.
3. **WLAN.** Mit der Bench SAFE, dem Ausgang der Versorgung aus und keinem
   offenen Log-Lauf: LOGS > WLAN > ACCESS POINT. Auf einem Telefon oder
   einem Computer das Netz verbinden, das der Bildschirm nennt, `rcbench-`
   und 4 Hex-Ziffern, mit dem 8-stelligen Passwort, das er zeigt.
   `http://192.168.4.1/` öffnen und den Zugangscode eingeben, den der
   Bildschirm zeigt.
4. **Panel.** Unter `Panel-Firmware` `rcbench-panel-v0.22.0.bin` wählen
   und `Hochladen` drücken. Das Panel prüft das ganze Image und fragt
   `PANEL-FIRMWARE INSTALLIEREN?` mit beiden Versionen. `2 s HALTEN:
   INSTALL` halten. Das Panel schaltet den Funk aus, schreibt den Slot, aus
   dem es nicht läuft, prüft ihn und startet neu in ihn. 0.22.0 startet auf
   Probe und bleibt, sobald Anzeige und Touch laufen; ein Start, der nicht
   so weit kommt, geht zurück zu 0.20.0.
5. **IOmcu.** Beim Link-Aufbau vergleicht das Panel das laufende Image des
   IOmcu per SHA-256 (Secure Hash Algorithm) mit dem Image, das es
   mitbringt, findet einen anderen Build, und das Band meldet `IOmcu nutzt
   einen anderen Build. Update unter SETUP > UPDATES.`. Die Frage öffnet
   sich auf dem Menü, solange die Bench SAFE ist, der Ausgang der
   Versorgung aus und der Funk des Panels aus. Sie nennt 0.20.0 auf dem
   IOmcu und 0.22.0 im Panel. START 2 s halten, oder nach CANCEL `2 s
   HALTEN: IOMCU AKTUALISIEREN` auf SETUP > UPDATES. Der Fortschritt füllt
   den Bildschirm ([Bildschirme: IOmcu-Update](Screens-de.md#iomcu-update)),
   und danach meldet das Band `IOmcu auf 0.22.0 aktualisiert.`.

Ein Panel mit gespeichertem Drehknopf EIN, oder eine Bench ohne Telefon
oder Computer mit WLAN, flasht `rcbench-panel-v0.22.0.zip` über USB wie in
[Schritt 3](#aus-den-release-assets) oben, und Schritt 5 folgt. Die
Einzelteile, `write_flash @flash_args`, behalten die Einstellungen mit
Drehknopf EIN: 0.22.0 liest den Drehknopf als MT6701 bei 0x06 und setzt
kein Gerät auf 0x36. Das zusammengeführte Image bei 0x0 löscht die
Einstellungen, und der Drehknopf startet AUS.

| Was | Installation über WLAN | Einzelteile (`@flash_args`) | Zusammengeführtes Image bei 0x0 |
| --- | --- | --- | --- |
| Einstellungen und WLAN-Einstellungen des Panels, im NVS (non-volatile storage, 24 KiB bei 0x9000) | bleiben | bleiben | gelöscht; das Panel startet mit den Standardwerten |
| Gespeicherte Platinenzeichnung des Panels, `boardart` bei 0x7E0000 | bleibt: die Installation schreibt nur den Anwendungs-Slot | bleibt | bleibt |
| Bindungen des IOmcu, Verdrahtung der Versorgung, Einrichtung der Strommonitore | bleiben: das Update schreibt den Slot, aus dem der IOmcu nicht läuft | bleiben | bleiben |

Das Link-Protokoll von 0.22.0 ist 4.28, das von 0.20.0 ist 4.22; die
Hauptversion, 4, ist gleich, also verbinden sich beide Platinen vor und
nach dem Update des IOmcu. CANCEL auf der Frage lässt den IOmcu auf
0.20.0; das Panel fragt nach seinem nächsten Start wieder, und SETUP >
UPDATES startet das Update solange. Ein IOmcu, der das USB-Laden für
0.19.0 verpasst hat, hat keine Partitionstabelle: das Band meldet `IOmcu
kann nicht über CAN aktualisiert werden. Firmware einmal über USB
installieren.`, und er bekommt [Schritt 2](#aus-den-release-assets). Die
Dateien der SD-Karte werden von Hand verschoben wie für 0.21.0
([unten](#die-sd-karte-von-0210-und-älter)). Die Dauer des Downloads, des
Uploads und des Updates über CAN ist nicht gemessen.

### Ab 0.21.0

Eine Bench mit 0.21.0 lädt das Panel über WLAN und den IOmcu über CAN aus
dem Image, das das neue Panel mitbringt. Ab 0.21.1 liest das Panel seine
eigenen Dateien aus neuen Ordnern der SD-Karte (Secure Digital) und verschiebt keine dorthin
([unten](#die-sd-karte-von-0210-und-älter)).

1. **SD-Karte.** Mit dem Panel aus die Karte herausnehmen. Am Computer die
   ESC-Profile von `/ESC/` nach `/BENCH/ESC/CONF/` verschieben, und die
   Servodateien mit `KST.TXT` und `KST.NEW` von `/KST/` nach
   `/BENCH/KST/CONF/`. Jeden fehlenden Ordner anlegen. Die Karte wieder
   einsetzen.
2. **Prüfen.** Mit gespeichertem Heimnetz, der Bench SAFE, dem Ausgang der
   Versorgung aus und keinem offenen Log-Lauf: SETUP > UPDATES, `NACH
   UPDATES SUCHEN`. Das Panel meldet `Version 0.22.0 ist verfügbar.`.
   `RELEASES: USABLE` liest `releases/latest`, das v0.22.0 nennt. Mit
   `RELEASES: TESTING` ist ein Panel, das `Unerwartete Antwort von GitHub.`
   meldet, auf den Fehler von 0.21.0 und 0.21.1 getroffen
   ([unten](#die-prüfung-auf-0210-und-0211)): `NACH UPDATES SUCHEN` sofort
   noch einmal drücken.
3. **Panel.** `2 s HALTEN: LADEN & INSTALLIEREN` halten. Das Panel lädt das
   Image, prüft es, schreibt den Slot, aus dem es nicht läuft, und startet
   neu in ihn. 0.22.0 startet auf Probe und bleibt, sobald Anzeige und
   Touch laufen; ein Start, der nicht so weit kommt, geht zurück zu 0.21.0.
4. **IOmcu.** Beim Link-Aufbau meldet das Band `IOmcu nutzt einen anderen
   Build. Update unter SETUP > UPDATES.`. Die Frage öffnet sich auf dem
   Menü, solange die Bench SAFE ist, der Ausgang der Versorgung aus und der
   Funk des Panels aus. Sie nennt 0.21.0 auf dem IOmcu und 0.22.0 im Panel.
   START 2 s halten, oder nach CANCEL `2 s HALTEN: IOMCU AKTUALISIEREN` auf
   SETUP > UPDATES. Danach meldet das Band `IOmcu auf 0.22.0 aktualisiert.`.

`RELEASES` gilt sofort; SPEICHERN auf SETUP behält es. Eine Bench ohne
Heimnetz nimmt den Upload auf der Browser-Seite des Panels, in beiden
Sprachen: SETUP > WI-FI > WLAN-HOTSPOT, das Netz verbinden, das der
Bildschirm nennt, `http://192.168.4.1/` öffnen, den Zugangscode eingeben,
unter `Panel-Firmware` `rcbench-panel-v0.22.0.bin` wählen und `Hochladen`
drücken, dann am Panel `2 s HALTEN: INSTALLIEREN` halten. Schritt 4 oben
folgt. Eine Bench ohne Telefon oder Computer mit WLAN flasht
`rcbench-panel-v0.22.0.zip` über USB wie in
[Schritt 3](#aus-den-release-assets) oben, und Schritt 4 folgt.

Die Installation behält die Einstellungen des Panels, seine
WLAN-Einstellungen und seine Platinenzeichnung; das Update des IOmcu
behält seine Bindungen, die Verdrahtung der Versorgung und die Einrichtung
der Strommonitore. Das Link-Protokoll von 0.22.0 ist 4.28, das von 0.21.0
ist 4.22; die Hauptversion, 4, ist gleich, also verbinden sich beide
Platinen vor und nach dem Update des IOmcu. Die Dauer des Downloads und des
Updates über CAN ist nicht gemessen.

### Ab 0.21.1

Eine Bench mit 0.21.1 lädt das Panel über WLAN und den IOmcu über CAN aus
dem Image, das das neue Panel mitbringt. 0.22.0 liest die Ordner der
SD-Karte wie 0.21.1; auf der Karte wird nichts verschoben.

1. **Prüfen.** Mit gespeichertem Heimnetz, der Bench SAFE, dem Ausgang der
   Versorgung aus und keinem offenen Log-Lauf: SETUP > UPDATES, `NACH
   UPDATES SUCHEN`. Das Panel meldet `Version 0.22.0 ist verfügbar.`.
   `RELEASES: STABLE` liest `releases/latest`, das v0.22.0 nennt. Mit
   `RELEASES: TESTING` ist ein Panel, das `Unerwartete Antwort von GitHub.`
   meldet, auf den Fehler von 0.21.1 getroffen
   ([unten](#die-prüfung-auf-0210-und-0211)): `NACH UPDATES SUCHEN` sofort
   noch einmal drücken.
2. **Panel.** `2 s HALTEN: LADEN & INSTALLIEREN` halten. Das Panel lädt das
   Image, prüft es, schreibt den Slot, aus dem es nicht läuft, und startet
   neu in ihn. 0.22.0 startet auf Probe und bleibt, sobald Anzeige und
   Touch laufen; ein Start, der nicht so weit kommt, geht zurück zu 0.21.1.
3. **IOmcu.** Beim Link-Aufbau meldet das Band `IOmcu nutzt einen anderen
   Build. Update unter SETUP > UPDATES.`. Die Frage öffnet sich auf dem
   Menü, solange die Bench SAFE ist, der Ausgang der Versorgung aus und der
   Funk des Panels aus. Sie nennt 0.21.1 auf dem IOmcu und 0.22.0 im Panel.
   START 2 s halten, oder nach CANCEL `2 s HALTEN: IOMCU AKTUALISIEREN` auf
   SETUP > UPDATES. Danach meldet das Band `IOmcu auf 0.22.0 aktualisiert.`.

Eine Bench ohne Heimnetz nimmt den Upload auf der Browser-Seite des Panels,
eine Bench ohne Telefon oder Computer mit WLAN das Flashen über USB, wie
[Ab 0.21.0](#ab-0210) es beschreibt; Schritt 3 oben folgt beidem. Die
Installation behält die Einstellungen des Panels, seine WLAN-Einstellungen
und seine Platinenzeichnung; das Update des IOmcu behält seine Bindungen,
die Verdrahtung der Versorgung und die Einrichtung der Strommonitore.
Das Link-Protokoll von 0.22.0 ist 4.28, das von 0.21.1 ist 4.23; die
Hauptversion, 4, ist gleich, also verbinden sich beide Platinen vor und
nach dem Update des IOmcu. Die Dauer des Downloads und des Updates über CAN
ist nicht gemessen. Eine Einrichtung von 0.21.1 findet die Änderungen von
0.21.2 unten vor und die von [Ab 0.21.2](#ab-0212).

Was eine Einrichtung von 0.21.1 auf 0.22.0 vorfindet:

| Auf 0.21.1 | Auf 0.22.0 |
| --- | --- |
| PD mini TX und PD mini RX auf einem GPIO gespeichert | lädt wie gespeichert; der PD mini bleibt aus, der Kopf von SUPPLY meldet `PD MINI OHNE PINS` und beide Zeilen auf SETUP > ANSCHLÜSSE melden `TX und RX auf einem Pin. Der PD mini bleibt aus.`; eine Zeile auf einen anderen GPIO stellen und SPEICHERN |
| Beleuchtung auf SETUP > PANEL | keine Zeile; die Hintergrundbeleuchtung ist an, solange das Panel läuft; der gespeicherte Schlüssel `backlight` bleibt im Einstellungsspeicher und wird nicht gelesen |
| Sensor SDA und SCL ohne Verdrahtung: `Sensorbus blockiert: SDA auf Low. Freitakten wird versucht.` | die Monitore gelten als nicht antwortend; die Prüfung läuft auf dem IOmcu und kommt mit Schritt 3 |
| INA228-Adresse und INA3221-Adresse springen an ihren Enden um | bleiben bei 0x40 und 0x4F (INA228), 0x40 und 0x43 (INA3221) stehen |
| eine exFAT-Karte, das Format der SDXC-Karten von 64 GB bis 2 TB: `keine Karte` auf dem Splash, `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.` auf LOGS | wird gemountet und gelistet; Dateien ab 4 GiB stehen mit ihrer Größe in der Liste |
| eine Karte ohne FAT- oder exFAT-Volume (NTFS, GPT, nicht formatiert): `keine Karte` auf dem Splash, `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.` auf LOGS | `nicht FAT32/exFAT` auf dem Splash, `Karte ist weder FAT32 noch exFAT. Am Computer formatieren.` auf LOGS, `Karte ist weder FAT32 noch exFAT. Lauf nicht aufgezeichnet.` beim Start eines Laufs; die Karte am Computer als FAT32 oder exFAT formatieren ([Bildschirme: Das Format der SD-Karte](Screens-de.md#das-format-der-sd-karte)) |
| eine Karte, die aus anderem Grund nicht eingebunden wird: `keine Karte` | `nicht lesbar` auf dem Splash, `Karte nicht lesbar. Am Computer prüfen, dann AKTUALISIEREN.` auf LOGS |
| ein Tipp auf DISARM, AUSGANG AUS, ABBRECHEN, TEST BEENDEN oder LAUF BEENDEN, während das Alert-Band ihn verdeckt oder der Hilfe-Kasten eines Chips offen ist: räumt nur das Band oder schließt nur den Kasten; das Alert-Band verdeckt die unteren 22 px von DISARM auf SERVO | wirkt beim ersten Druck und räumt das Band oder schließt den Kasten; ARM und AUSGANG EIN unter dem Band oder dem Kasten starten weiterhin kein Halten |

### Ab 0.21.2

Eine Bench mit 0.21.2 lädt das Panel über WLAN und den IOmcu über CAN aus
dem Image, das das neue Panel mitbringt. 0.22.0 liest die Ordner der
SD-Karte wie 0.21.2; auf der Karte wird nichts verschoben.

1. **Prüfen.** Mit gespeichertem Heimnetz, der Bench SAFE, dem Ausgang der
   Versorgung aus und keinem offenen Log-Lauf: SETUP > UPDATES, `NACH
   UPDATES SUCHEN`, mit beiden Werten von RELEASES. Das Panel meldet
   `Version 0.22.0 ist verfügbar.`.
2. **Panel.** `2 s HALTEN: LADEN & INSTALLIEREN` halten. Das Panel lädt das
   Image, prüft es, schreibt den Slot, aus dem es nicht läuft, und startet
   neu in ihn. 0.22.0 startet auf Probe und bleibt, sobald Anzeige und
   Touch laufen; ein Start, der nicht so weit kommt, geht zurück zu 0.21.2.
3. **IOmcu.** Beim Link-Aufbau meldet das Band `IOmcu nutzt einen anderen
   Build. Update unter SETUP > UPDATES.`. Die Frage öffnet sich auf dem
   Menü, solange die Bench SAFE ist, der Ausgang der Versorgung aus und der
   Funk des Panels aus. Sie nennt 0.21.2 auf dem IOmcu und 0.22.0 im Panel.
   START 2 s halten, oder nach CANCEL `2 s HALTEN: IOMCU AKTUALISIEREN` auf
   SETUP > UPDATES. Danach meldet das Band `IOmcu auf 0.22.0 aktualisiert.`.

Eine Bench ohne Heimnetz nimmt den Upload auf der Browser-Seite des Panels,
eine Bench ohne Telefon oder Computer mit WLAN das Flashen über USB, wie
[Ab 0.21.0](#ab-0210) es beschreibt; Schritt 3 oben folgt beidem. Die
Installation behält die Einstellungen des Panels, seine WLAN-Einstellungen
und seine Platinenzeichnung. Das Update des IOmcu behält seine Bindungen,
die Verdrahtung der Versorgung und die Einrichtung der Strommonitore: sein
Store-Datensatz wird beim ersten Speichern Version 8, und ein IOmcu-Image
älter als 0.22.0 liest keinen Datensatz der Version 8 und startet wie mit
leerem Store. Das Link-Protokoll von 0.22.0 ist 4.28, das von 0.21.2 ist
4.23; die Hauptversion, 4, ist gleich, also verbinden sich beide Platinen
vor und nach dem Update des IOmcu. Die Dauer des Downloads und des Updates
über CAN ist nicht gemessen.

Was eine Einrichtung von 0.21.2 auf 0.22.0 vorfindet:

| Auf 0.21.2 | Auf 0.22.0 |
| --- | --- |
| Die INA3221-Zeilen auf SETUP > ANSCHLÜSSE: INA3221, Adresse, Shunt, Kanäle | SETUP > SENSOREN, beim ersten Start aus den gespeicherten Zeilen belegt: der Baustein an der gespeicherten Adresse bestückt, wenn der INA3221 EIN war, sein CH1 auf dem gespeicherten Shunt, CH2 und CH3 darauf mit CH1+2+3 und AUS mit CH1, CH1 überwacht PORT 1, wenn das Ziel des Servotesters der Programmierport war, sonst OUT 1 ([Bildschirme: SENSOREN](Screens-de.md#sensoren-die-ina3221-bausteine)); die gespeicherten Schlüssel bleiben unverändert für ein Panel, das zurück auf 0.21 geht |
| Ein Programmierport, PROG PORT | PROG PORT 1, PROG PORT 2, PROG PORT 3 und PROG PORT 4 auf SETUP > OUTPUTS; ein ZIEL des Servotesters PROG PORT liest sich als PORT 1; die Ports 2 bis 4 starten mit FUNKTION NONE ohne Pin |
| Die INA3221-Spalten des Lauf-Logs `ch1` bis `ch3` | eine Gruppe aus Strom, Maximum und Spannung je überwachendem Kanal, nach seinem Ziel benannt (`port 1 current (A)`), höchstens 3 Gruppen |
| Die Tabs von LOGS in der Reihenfolge USER, BENCH, STAMM | STAMM, USER, BENCH; der gemerkte Tab und die Ordner bleiben |

### Die Prüfung auf 0.21.0 und 0.21.1

`RELEASES: TESTING` liest die Release-Liste von GitHub
(`api.github.com/repos/subtilitas/rcbench/releases?per_page=10`). GitHub
schickt sie in einer von zwei Formen. Eine Anfrage, die sein Cache nicht
hält, kommt in Stücken (`Transfer-Encoding: chunked`) ohne
`Content-Length`; dieselbe Anfrage aus dem Cache kommt mit
`Content-Length`. GitHub hält die Antwort 60 s (`Cache-Control:
s-maxage=60`). 0.21.0 und 0.21.1 lesen nur die Form mit Länge: eine
Antwort in Stücken beendet die Prüfung mit `Unerwartete Antwort von
GitHub.` und `Die Antwort enthält keine Größenangabe.`, die Prüfung beim
Start eingeschlossen. Ein zweites `NACH UPDATES SUCHEN` sofort danach
bekommt die Form aus dem Cache. Gemessen am 2026-10-19: 4 Anfragen mit
mehr als 60 s Abstand kamen in Stücken, und eine Wiederholung jeder
innerhalb 1 s kam mit `Content-Length`, 4 von 4. Jede Prüfung zählt gegen
GitHubs Grenze von 60 API-Anfragen (Application Programming Interface) pro
Stunde und Adresse. 0.21.2 und neuer lesen beide Formen.

### Die SD-Karte von 0.21.0 und älter

Das Panel hält seine Dateien auf der SD-Karte (Secure Digital) in Ordnern
unter `BENCH` und `USER` ([Logs](Screens-de.md#logs)). Es verschiebt nichts,
was eine Karte von 0.21.0 oder älter hält, und liest dies nicht:

| 0.21.0 und älter | Gelesen aus | Am Computer |
| --- | --- | --- |
| `/ESC/*.json`, ESC-Profile | `/BENCH/ESC/CONF/` | die Dateien dorthin verschieben |
| `/KST/*.TXT`, Servodateien, der Vermerk `KST.TXT` und sein neuer Text `KST.NEW` | `/BENCH/KST/CONF/` | die Dateien dorthin verschieben, `KST.TXT` und `KST.NEW` mit |
| `/RCBENCH.LOG`, Fehlerprotokoll | nicht gelesen; neue Zeilen gehen nach `/BENCH/RCBENCH.LOG` | behalten oder löschen |
| Lauf-Logs im Stammverzeichnis | gelistet im Tab STAMM von LOGS | nichts zu tun |

Ein Vermerk `KST.TXT` oder `KST.NEW`, der in `/KST` liegt, wird beim Start nicht gesehen:
Die Wiederherstellung, die er für ein unterbrochenes Servo-Schreiben
anbietet, braucht die verschobene Datei. Firmware 0.21.0 und älter liest die
Ordner unter `BENCH` nicht.

### Aus einem Quellbaum

```bash
# IOmcu — erzeugt rcbench-iomcu-install.uf2 und rcbench-iomcu.uf2
cmake -S firmware/iomcu -B firmware/iomcu/build
cmake --build firmware/iomcu/build

# Panel
idf.py -C firmware/panel set-target esp32s3
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor
```

Der IOmcu-Build gibt seine eigene Größenprüfung aus:

    -- rcbench: image 191808 bytes, 9% of the 2084864 bytes of one slot

**Achtung:** das Modul für das Bring-up ist ein Waveshare RP2350-CAN mit
**4 MB**, während `PICO_BOARD` auf eine Board-Datei zeigt, die **16 MB**
angibt. Der Linker misst gegen 16 MB und warnt nicht. Die Zeile oben ist das
Einzige, was es tut — also lesen.

### Die Partitionstabelle des IOmcu

Der Flash des IOmcu trägt eine Partitionstabelle: zwei Image-Slots
von je 2036 KiB und eine eigene 8-KiB-Partition für den Ausgangsspeicher
([Link](Link-de.md#firmware-update-über-den-link) zeigt die Aufteilung).
Spätere IOmcu-Images schickt das Panel über CAN in den Slot, der nicht
läuft. Ein neues Modul und ein IOmcu mit 0.18.0 oder älter (einem Image des
Protokolls 4.21 oder älter) haben keine Tabelle und bekommen Tabelle und
erstes Image einmal über USB.

Die USB-Buchse des IOmcu angeschlossen und BOOTSEL beim Einschalten
gehalten, sodass das Modul sein Laufwerk `RP2350` zeigt:

```bash
# die Datei auf das Laufwerk ziehen, oder:
picotool load -v -x firmware/iomcu/build/rcbench-iomcu-install.uf2

# prüfen: Partitionen 0 (A), 1 (B) und 2 (store, data)
picotool partition info
```

**Gut:** die Konsole des IOmcu gibt alle 3 s `rcbench-iomcu: running
from slot A` aus. Die Bindungen, die Verdrahtung der Versorgung und die
Einrichtung der Strommonitore kommen aus dem alten Speicher bei 0x3FE000, bis
das erste Speichern den neuen schreibt. `no partition table -- firmware
updates over CAN need one USB load of rcbench-iomcu-install.uf2` heißt, das
Image läuft ohne die Tabelle: `rcbench-iomcu-install.uf2` noch einmal laden.

Auf einem RP2350 der Stepping A2 schreibt das Laden auch den Block der
Umgehung für RP2350-E10, der im letzten 4-KiB-Sektor der 4 MB landet,
0x3FF000: dem zweiten Sektor des alten Speichers. Bindungen, deren neuestes
Speichern dort liegt, gehen verloren, und das neueste Speichern in 0x3FE000
kommt stattdessen zurück. Nach dem Laden OUTPUTS prüfen. Das Stepping des
Bring-up-Moduls ist nicht bekannt.

Mit der Tabelle geht `rcbench-iomcu.uf2` allein in den Slot, der nicht läuft.
Welchen Slot ein Einschalten startet, wenn beide Slots ein Image derselben
Version halten, sagt das Datenblatt des RP2350 nicht. Das Image, das
startet, löscht den ersten Sektor des anderen Slots, die Wahl bleibt also,
und der andere Build ist weg. Um nach einem Entwicklungs-Laden sicher zu
wissen, welcher Build läuft, zuerst den anderen Slot löschen:

```bash
picotool erase -p 1
picotool load -v -x -p 0 firmware/iomcu/build/rcbench-iomcu.uf2
```

### Die Anwendungs-Slots des Panels

Der Flash des Panels hat zwei Anwendungs-Slots mit je 4 063 232 Bytes,
`ota_0` und `ota_1`, damit es sich über WLAN selbst aktualisieren kann
([Bildschirme: WLAN](Screens-de.md#wlan)). Ein Panel,
das mit 0.18.0 oder älter geflasht ist, hat einen 3 MiB großen
`factory`-Slot und bekommt das neue Layout mit einem Flash über USB:

| Offset | 0.18.0 und älter | 0.19.0 und neuer |
| --- | --- | --- |
| 0x9000 | `nvs`, 24 KiB | `nvs`, 24 KiB |
| 0xF000 | `phy_init`, 4 KiB | `phy_init`, 4 KiB |
| 0x10000 | `factory`, 3 MiB | `otadata`, 8 KiB |
| 0x20000 | | `ota_0`, 3968 KiB |
| 0x310000 | `storage`, 1 MiB, unbenutzt | |
| 0x400000 | | `ota_1`, 3968 KiB |
| 0x410000 | `boardart`, 2 MiB | |
| 0x7E0000 | | `boardart`, 128 KiB |

Die Einzelteile behalten die Einstellungen: Sie schreiben den Bootloader bei
0x0, die Partitionstabelle bei 0x8000, `otadata` bei 0x10000 und die
Anwendung bei 0x20000 und lassen `nvs` in Ruhe. Aus einem Quellbaum:

```bash
idf.py -C firmware/panel -p /dev/ttyACM0 flash
```

Aus dem Release-Archiv `esptool.py ... write_flash @flash_args` im
entpackten Ordner, wie in [Schritt 3](#aus-den-release-assets).

Das zusammengeführte Image bei Offset 0 reicht von 0x0 bis zum Ende der
Anwendung in `ota_0`, etwa 2 MB, und füllt die Lücken zwischen den Teilen
mit 0xFF. `nvs` bei 0x9000 liegt in einer solchen Lücke, also löscht das
zusammengeführte Image die Einstellungen.

In beiden Fällen ist die gespeicherte Platinenzeichnung einmal verloren:
`boardart` ist von 0x410000 nach 0x7E0000 umgezogen. Das Panel lädt sie beim
nächsten Link-Aufbau neu ([§4](#4-erster-link-aufbau-die-zeichnung-der-platine)).
Ab 0.20.0 installiert das Panel spätere Releases über WLAN aus dem
Release-Asset `rcbench-panel-vX.Y.Z.bin`.

---

## 2. Der Link, vor allem anderen

Das Panel führt den CAN-Echo-Selbsttest selbst aus, bei jedem Start, 1200 ms
lang innerhalb des Splash. Er beantwortet eine Frage: kommen Frames unversehrt
über den Bus? Er benutzt kein Page-Protokoll; besteht er und der Link arbeitet
trotzdem nicht, liegt der Fehler oberhalb des Drahts.

**Gut sieht nach nichts aus:** der Splash zeigt `VERBINDUNG  OK  CAN 1 Mbit/s` und
übergibt ans Menü.

**Ein Fehlschlag ist ein Bildschirm**, vor dem Menü, mit dem Urteil, der Liste
dessen, was der Reihe nach zu prüfen ist, und den Zählern beider Enden. Ihn zu
verlassen kostet zwei Sekunden Halten. [Den Link in Betrieb
nehmen](Bringup-de.md#der-bus-fehler-bildschirm) hat die Urteile und was jedes
bedeutet.

**Vorher nicht weitergehen.** Jeder Schritt darunter setzt voraus, dass Frames
ankommen.

Die Einzelheiten stehen auf der Konsole, wenn man sie will — auf der
**UART-Buchse**, nicht auf nativem USB, denn GPIO19 und GPIO20 tragen beides
und der Multiplexer wählt eines aus:

    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0

---

## 3. Der Heartbeat-Draht — das, was alles blockiert hat

| | |
|---|---|
| Panel-Seite | **GPIO28**, ein freier Pin der GPIO-Stiftleiste des 7-Zoll-ESP32-P4-Boards (`PANEL_HEARTBEAT_PIN` in `board_pins.h`). GPIO28 ist vorläufig, bis der Pin am ersten Muster festgelegt ist. |
| IOmcu-Seite | **GP3** |
| Dazwischen | das retriggerbare Monoflop, sobald es eines gibt. Es ist auf keiner Platine, der Aufbau-Prüfstand fährt daher eine direkte Leitung und hat keine Hardware-Rückfallebene. Die Leitung deckt ein Panel ab, das aufhört zu schlagen, solange der IOmcu gesund ist: Er schaltet nach 150 ms Stille auf SAFE. Unabgedeckt bleibt ein Panel, das aufhört zu schlagen, während der IOmcu nicht handeln kann -- dann nimmt nichts die Ausgänge weg, und genau das täte das Monoflop ohne jede Firmware |

Ohne diesen Draht verweigert der IOmcu jedes Schalten auf ARMED, und das ist die
Verriegelung, die arbeitet — kein Fehler.

**Die Zahlen, an denen er gemessen wird:**

| | |
|---|---|
| Flankenperiode | 20 ms |
| Akzeptierte Lücke | 4 ms bis 150 ms |
| Flanken, bis er als lebendig gilt | 4 |

Die Leitung muss also rund **80 ms** flanken, bevor ein Arm gelingen kann.

**Am Oszilloskop, an GP3:** ein Rechteck, Flanke zu Flanke 20 ms.
Überschreitet die Lücke je 150 ms, verwirft der IOmcu sie und das
nächste Schalten auf ARMED wird verweigert.

**Woher die Flanken kommen:** aus der Control-Task des Panels, innerhalb
ihrer Poll-Schleife. Alles, was diese Task anhält, hält den Heartbeat an —
deshalb darf dort nichts Langes laufen.

---

## 4. Erster Link-Aufbau: die Zeichnung der Platine

Der IOmcu trägt eine 1 560 Bytes große Zeichnung seiner eigenen
Platine. Das Panel holt sie beim ersten Link-Aufbau und behält sie im Flash.

Auf der Panel-Konsole, in dieser Reihenfolge:

    I (…) rcbench: IOmcu answered
    I (…) rcbench: hardware 1 says where its pads are
    I (…) rcbench: hardware 1 says which pads are grounds and rails
    I (…) rcbench: fetching hardware 1's drawing: 1560 bytes, hash 8f99cd48
    I (…) rcbench: hardware 1's drawing kept

**Was währenddessen zu erwarten ist:**

- 26 Blöcke zu 62 Bytes über CAN, in einem Abschnitt von 15 ms je Poll von
  50 ms.
- **Ein Stillstand des Displays**, wenn sie in den Flash geschrieben wird:
  einer pro Flash-Operation, 4 für diese Zeichnung (2 Sektoren von 4 kB
  löschen, die Nutzdaten, der Kopf). Eine Flash-Operation schließt den
  Cache, durch den das Nachladen des Bounce-Buffers PSRAM liest, also steht
  das Panel für die Dauer jeder einzelnen. Eine Operation, die das Nachladen
  länger als 512 us aufhält, startet die Ausgabe beim ersten
  VBlank-Interrupt (Vertical Blanking Interval) nach ihrem Ende neu, der
  mitten im Bild liegen kann, und die Konsole gibt
  `display: scan restarted` aus; das erste ganze Bild danach sitzt an
  seinem Platz. Settings-Speichern tut dasselbe.
- **Das Schreiben wartet auf SAFE.** Es nimmt einen Schritt pro
  5-ms-Durchlauf der Control-Task, und nur solange der Prüfstand SAFE ist und
  das Netzteil aus. In ARMED oder mit eingeschaltetem Netzteil erscheint
  einmal `keeping hardware 1's drawing waits for a bench in SAFE with the
  supply off` auf der Konsole, und `kept` folgt nach dem Schalten auf SAFE.
- **Der Heartbeat darf davon nicht gestört werden.** Die Übertragung ist in
  Scheiben geteilt und das Flash-Schreiben wird in ARMED deswegen
  zurückgehalten. Fällt der Heartbeat während des Abrufs aus, anhalten und
  aufschreiben.

Beim nächsten Link-Aufbau liest das Panel Länge, CRC (zyklische
Redundanzprüfung) und Hash der Zeichnung vom IOmcu und zeigt die
behaltene Kopie, wenn alle drei übereinstimmen:

    I (…) rcbench: hardware 1's drawing is kept: 1560 bytes, hash 8f99cd48

Ein IOmcu, der mit einer anderen Zeichnung geflasht wurde, meldet einen
anderen Hash, und das Panel holt die Zeichnung neu. Ein IOmcu mit
Protokoll 4.19 oder älter liefert keine Zeichnung; das Panel schreibt
`hardware 1 serves no board drawing (protocol 4.19); PICK A PIN draws its
shape` ins Log und zeichnet die Platine aus ihrem Umriss.

---

## 5. Armen ohne angeschlossene Last

Noch ist nichts an einem Output verdrahtet. Dieser Schritt prüft die
Verriegelung, nicht einen Pin.

1. **Heartbeat-Draht abgezogen** → Das Schalten auf ARMED muss **verweigert** werden. Der
   IOmcu antwortet `NOT_ARMED`, weil `!beat.alive`. Das Panel zeigt die
   Verweigerung.
2. **Heartbeat-Draht gesteckt, Panel ARMED** → der IOmcu nimmt es an.
3. **Heartbeat-Draht in ARMED ziehen** → muss binnen **150 ms**
   auf SAFE fallen.
4. **STOP drücken** → rastet ein. Es zu verlassen braucht ein ausdrückliches
   Arm, keinen Link, der sich erholt.
5. **CAN-Draht in ARMED ziehen** → der IOmcu gibt nach
   **200 ms** auf; das Panel eskaliert nach **1 s**.
6. **Touch abdecken / sterben lassen** → nach **500 ms** Stille ist Armen
   gesperrt.

Jeder dieser Punkte ist host-getestet. **Keine der Zahlen unten wurde an einem Messgerät gesehen:** kein Oszilloskop und kein Logikanalysator hat irgendeine Impulsbreite, Rahmenperiode oder Antwortverzögerung in diesem Baum gelesen.

---

## 6. Erster Pin an einem Messgerät

Ein **Servo** nehmen, nicht den ESC. Ein Servo ist der gutmütige Fall und der,
den das Oszilloskop am leichtesten liest.

**Freie Pins für einen Output:** GP0, GP1, GP2, GP4, GP5, GP6, GP7, GP13 bis
GP22 und GP26 bis GP28. GP13 ist der Pin des Programmierports an einem
IOmcu, der keinen gespeichert hat, und wird für einen Output
verweigert, bis der Pin des Ports verlegt ist
([Link](Link-de.md#die-programmierports)).
**Reserviert und verweigert:** GP3 (Heartbeat), GP8–GP12 (CAN), GP23, GP24, GP25
und GP29 (vom Modul belegt, nicht auf der Stiftleiste) und GP30 aufwärts (nicht
im Bauteil).
**Als zweiter PWM-Pin verweigert:** ein Pin, dessen PWM-Compare-Register schon
von einem gebundenen Pin belegt ist. Auf dem RP2350 ist die Slice unterhalb
von GP32 `(Pin / 2) modulo 8` und der Kanal das niedrigste Bit des Pins, also
teilen sich GP0 und GP16, GP1 und GP17, GP2 und GP18, GP4 und GP20, GP5 und
GP21, GP6 und GP22 je ein Compare-Register. Der zweite eines Paares wird
verweigert, statt auf die Pulsbreite des ersten gemuxt zu werden. Die
OUTPUTS-Seite führt kein Bit mit, das „gebunden“ sagt, der Bildschirm sieht
also weiter konfiguriert aus, während die Leitung keinen Impuls liefert.

Am Panel: **Setup → OUTPUTS**, `SERVO PWM` wählen, einen Pin anhaken. Oder
**Setup → PICK A PIN**, in dessen Liste `SERVO PWM` wählen und eine Taste auf
der Platinenzeichnung antippen — Massen sind mit `G` markiert,
Versorgungen tragen ihre Spannung.

**Den Pin vor dem Anschließen des Servos messen.**

| | |
|---|---|
| Framerate | 40 bis 560 Hz (voreingestellt 50 Hz) |
| Puls | 400 bis 2500 µs, außerhalb verweigert |
| Auflösung | 1 µs |

**Messen und aufschreiben:** die tatsächliche Frameperiode, den Puls an
beiden Enden des Wegs und den Jitter. Bit-Timings sind die größte
unbestätigte Fläche im Baum.

**Danach:** bestätigen, dass der Servo-Bildschirm loslässt, wenn man es ihm
sagt, und nur dann. Den Finger zu heben stoppt den Ausgang nicht: der
Bildschirm hält die gegebene Stellung und wiederholt sie alle **100 ms**
(`SERVO_HOLD_MS`) gegen die **500 ms** des IOmcu
(`OUT_DEFAULT_TIMEOUT_MS`), ein Servo bleibt also stehen, wo es hingestellt
wurde. **FREIGEBEN** führt die Ruderflächen auf die Mitte zurück; es löscht den
Slot nicht, und der Pin pulst weiter. Beendet werden die Flanken durch
das Schalten auf SAFE, STOP oder das Verlassen des Bildschirms, was auf SAFE schaltet. Am
Oszilloskop prüfen, dass FREIGEBEN den Impuls in die Mitte des Kanalwegs führt
und dass das Schalten auf SAFE ihn beendet.

Ein Kanal, den niemand auffrischt, geht nach 500 ms weiterhin in seine
Ruhelage; das betrifft einen gebundenen Pin, den der Servo-Bildschirm nicht
hält.

---

## 7. Die Zahlen, die niemand gemessen hat

Aus der Spezifikation geschrieben, nur gegen Frames geprüft, die derselbe
Code baut. Für jede einen echten Wert aufschreiben:

- **Jedes DShot-Bit-Timing**, gegen die Toleranz eines ESC statt gegen die
  Spezifikation.
- **Die Antwortrate** von fünf Vierteln der DShot-Rate.
- **Die Konvention des führenden Bits** des Gruppencodes.
- **Die Umschaltverzögerung** — ob 30 µs das ist, was ein ESC wirklich
  wartet.
- **Die erweiterten Telemetrie-Frametypen** und ihre Einheiten.
- **Der PPM-DMA-Ring**, der einen Frame abspielt.
- **Das Flash-Löschfenster** auf dem IOmcu, und ob der Heartbeat über
  ein Speichern hinweg wieder greift.

---

## 8. Strommonitore (INA228, INA3221)

Core 1 des IOmcu liest einen INA228 im Leistungspfad des ESC und
einen INA3221 an der Servoversorgung über I2C (Inter-Integrated Circuit) mit
400 kHz, jede 1 ms. Jeder Schritt unten
sagt, wie gut aussieht; alles andere ist ein Befund zum Aufschreiben.

**Bereitlegen:** das MATEK I2C-INA-BM (INA228, 200 µΩ) auf der Adresse, mit
der es geliefert wird, das DAOKAI INA3221 (0,1 Ω je Kanal, 1,638 A
Vollausschlag) mit geschlossener A0-Lötbrücke nach GND für 0x40 (ab Werk
sind alle vier A0-Brücken offen, und die Adresse schwankt zwischen 0x40 und
0x41), zwei
Widerstände 2,2 kΩ, ein Multimeter, ein Oszilloskop oder Logikanalysator mit
I2C-Dekodierung, ein Servo, und der ESC mit Motor oder eine ohmsche Last an
einem Netzteil mit Strombegrenzung.

**Am Panel einrichten.** SETUP → ANSCHLÜSSE: `INA228` auf ON; SETUP →
SENSOREN: `0x40 bestückt` auf ON. Die übrigen Sensoreinstellungen passen in
der Vorgabe zu den Modulen oben: INA228 auf 0x45 mit 200 µΩ, der INA3221 auf
0x40 mit 0,1 Ω je Kanal und CH1, der AUSGANG 1 misst, SDA an GP16 und SCL an
GP17. Ohne Panel gibt die Build-Option `IOMCU_SENSE_BRINGUP=ON` beide
Bauteile mit den Vorgaben der Page frei, solange im Flash keine
Konfiguration liegt:

```bash
cmake -S firmware/iomcu -B firmware/iomcu/build-sense -DIOMCU_SENSE_BRINGUP=ON
cmake --build firmware/iomcu/build-sense
```

**Verdrahten** (alles Pads des IOmcu auf dem Modul RP2350-CAN):

| Draht | Von | Nach |
|---|---|---|
| SDA | GP16 (Pad 21) | INA3221 SDA, MATEK SDA |
| SCL | GP17 (Pad 22) | INA3221 SCL, MATEK SCL |
| Pull-ups | 2,2 kΩ von SDA und von SCL | 3V3 (Pad 36) |
| Versorgung INA3221 | 3V3 (Pad 36), GND | INA3221 VS, GND |
| Versorgung MATEK | VBUS (Pad 40, 5 V vom USB), GND | MATEK 5V, G |

Die Firmware schaltet die eigenen Pull-ups und Pull-downs der Pads an beiden
Pins ab: der Bus läuft auf den Pull-ups der Module und den beiden 2,2 kΩ.
**Bevor der IOmcu angeschlossen wird**, die Module versorgen und SDA
und SCL gegen GND messen: beide dürfen höchstens 3,3 V zeigen. Auf welche
Spannung das MATEK hochzieht, ist nicht bekannt.

**Die Konsole.** Alle 3 s, solange ein Bauteil freigegeben ist, druckt der
IOmcu drei Zeilen:

```
rcbench-iomcu: sense flags 0x0133 present 0x0021 ids 0x2281 0x3220 errors 0 | bench 1680 cV 1200 cA 33 mAh 6 dWh flags 0x63 | temp 254 dC
rcbench-iomcu: sense window 4711 CH1 120 mA 6000 mV ch_flags 0x01 | capture state 0 seq 0 move 0 arrive 0
rcbench-iomcu: sense map fit 0x1 fast 0 flags 0x0201 | mA 120 0 0 0 0 0 0 0 0 0 0 0 | over 0x000 count 0
```

`flags` sind die FLAGS der SENSE-Page: 0x0001 INA228 online, 0x0002 seine
Identitätslesung war die eines INA228, 0x0004 an seiner Adresse antwortet
etwas anderes, 0x0008 ein Strom lag am Bereichsende, 0x0010, 0x0020 und
0x0040 dasselbe für den INA3221, 0x0100 der Bus ist offen, 0x0200 der Bus
hängt. `present` hat Bit n für Adresse 0x40 + n. `bench` ist die
BENCH-Page: Schritte von 10 mV und 10 mA, Ladung in mAh, Energie in 0,1 Wh,
und ihre Flags (0x01 Spannung, 0x02 Strom, 0x20 die des INA228, 0x40 die
Zähler des INA228).
Die dritte Zeile ist die INA3221-Map, SENSE_MAP: die bestückten Bauteile,
der FAST-Kanal, ihre Flags (0x0200 aus SENSE abgeleitet), der mittlere
Strom jedes Kanals und die Kanäle über ihrer Stromgrenze (SENSE_LIMIT) mit
ihrer Zahl
([Die INA3221-Map und ihre Fenster](LinkSensors-de.md#auf-der-konsole)).

### 8.1 Die Bauteile antworten

Alles versorgen, nichts auf ARMED.

**Gut:** `flags 0x0133`, `present 0x0021`, `ids 0x228x 0x3220`, `errors`
steigt zwischen zwei Zeilen nicht.

**Aufschreiben:** die beiden IDs wie gedruckt. Ein INA238 auf dem MATEK
antwortet 0x238x und wird abgewiesen (Flag 0x0004). Ein DAOKAI-Exemplar, das
0x1408 antwortet (die Lesung eines Käufers), wird ebenso abgewiesen (0x0040).

### 8.2 Der Bus am Oszilloskop

SDA und SCL am IOmcu abgreifen.

**Gut:** SCL mit 400 kHz (2,5 µs je Takt); Anstiegszeit, 30 % bis 70 %,
höchstens 300 ns; eine Lesung von Register 0x01 des INA3221 alle 1,0 ms;
die Register 0x07 und 0x05 des INA228 abwechselnd, eines je 1,0 ms; kein
NACK in der Dekodierung.

**Aufschreiben:** die Anstiegszeit, den Takt, die Zeit vom STOP einer
Transaktion zum START der nächsten (die eigene Zeit des Controllers, nicht
gemessen), und die Periode der CH1-Lesung.

### 8.3 Messwerte gegen ein Messgerät

Einen gleichmäßigen Strom durch den Shunt des MATEK schicken und ihn und die
Akkuspannung mit dem Multimeter messen. Dann ein Servo, oder einen
Widerstand unter 1,6 A, an INA3221 CH1, das Multimeter in Reihe.

**Gut:** Spannung und Strom in `bench` und mA und mV von `CH1` (die Spannung
von CH1 liegt auf der Lastseite des Shunts) stimmen mit dem Multimeter
überein, innerhalb seiner Genauigkeit und der Toleranz des Shunts, die für
keines der beiden Module bekannt ist. `temp` nahe Raumtemperatur, in
0,1 °C. `ch_flags 0x01`; 0x10 kommt hinzu, wenn CH1 über 1,638 A geht, und
der Strom von CH1 ist dann eine Grenze, kein Wert.

**Aufschreiben:** jedes Zahlenpaar, das des Multimeters und das der Konsole.

### 8.4 Ein Lauf: Spitzen und Zähler

Auf MOTOR & ESC auf ARMED schalten, 60 s einen gleichmäßigen Strom halten, dann
auf SAFE schalten.

**Gut:** `bench flags` liest 0x63 (dazu 0x04 und 0x08, wenn ein
bidirektionaler ESC antwortet). `mAh` zählt Strom × Zeit: 2,00 A über 60 s
sind 33 mAh. Ein neues Schalten auf ARMED beginnt Ladung und Energie bei 0. Der
Bildschirm MOTOR & ESC zeigt Spannung und Strom des INA228.

**Aufschreiben:** Strom, Zeit und erreichte Ladung, und die Spitzen, die das
Panel zeigt, gegen Oszilloskop oder Multimeter.

### 8.5 Eine Leitung im Lauf gezogen

In ARMED die SDA am MATEK für etwa 5 s abziehen, dann wieder stecken.

**Gut:** innerhalb von etwa 6 ms verliert `flags` 0x0001 und behält 0x0002;
`bench flags` liest 0x20: der INA228 bleibt die Quelle, seine Felder leer,
nicht die Werte des ESC; `errors` steigt; der Prüfstand bleibt ARMED. Etwa
1 s nachdem die Leitung wieder steckt, kommt 0x0001 zurück, und 0x40 bleibt
bis zum nächsten Schalten auf ARMED gelöscht. In SAFE und mit gezogener
Leitung trägt `bench` wieder die Telemetrie des ESC.

### 8.6 Ein hängender Bus

SDA für 2 s mit einem Draht auf GND halten, dann loslassen. Die Leitungen
sind Open Drain: der Pin wird nie high getrieben, der Kurzschluss ist
ungefährlich.

**Gut:** `flags` bekommt 0x0200, solange gehalten wird; auf SCL zeigt das
Oszilloskop alle 100 ms 9 Takte und ein STOP; nach dem Loslassen geht
0x0200 weg, und beide Bauteile sind innerhalb von etwa 1 s online.

**Aufschreiben:** die Taktrate der 9 Takte (gedacht sind 100 kHz), und wie
lange die Bauteile zum Zurückkommen brauchten.

### 8.7 Speichern während des Lesens

In SAFE beide Bauteile lesen, auf dem Bildschirm OUTPUTS einen Pin
ankreuzen: die Belegung wird gespeichert.

**Gut:** die Konsole druckt `outputs saved, record n, program window N us`
und kein `window refused`; `errors` steigt nicht; `window` zählt weiter;
der Heartbeat fällt nicht aus.

**Aufschreiben:** das Programmierfenster, und das Löschfenster, wenn eines
gedruckt wird: Core 1 wird für jedes im RAM geparkt.

### 8.8 Noch nicht erreichbar

Die Messung der Bewegung auf SERVO_SENSE -- der PWM-Frame auf 1 µs
gestempelt und die Stellzeiten -- schaltet der Servotest des Panels scharf,
der sie noch nicht nutzt. Eine Stellzeit gegen ein Oszilloskop am PWM-Pin
und am Shunt wartet darauf.

### 8.9 Die 1-ms-Samples von CH1 auf der Konsole

Ein IOmcu-Image, das mit `-DSENSE_TRACE=ON` gebaut ist, druckt die
1-ms-Samples von INA3221 CH1 als Text auf seine USB-Konsole (Universal
Serial Bus). Der Link überträgt nur 50-ms-Fenster; dieser Build ist der
einzige Weg zu den einzelnen Samples. Er ist ein Debug-Build: ein
veröffentlichtes Image wird ohne die Option gebaut und enthält nichts von
ihrem Code. `tools/sense_trace.py` liest die mitgeschnittene Konsole.
Die Messung am Prüfstand hat zwei
Teile: Teil A braucht keinen Encoder, Teil B wiederholt die Bewegungen mit
einem AS5600 auf der Welle des Servos.

**Was einen Trace startet.**

| Trigger | Trace |
| --- | --- |
| ein PWM-Ausgang (Pulsweitenmodulation) gibt eine andere Pulsbreite aus als im Durchlauf davor, 50 ms oder mehr nach der letzten Änderung dieses Ausgangs | 4 s ab dem Frame, der den Puls trägt; Triggerzeile `$C` |
| `t` auf der Konsole getippt | 10 s; Triggerzeile `$K` |
| die PWM-Flanke des Capture | 4 s; Triggerzeile `$E`. Kein veröffentlichtes Panel schaltet das Capture scharf, mit einem solchen kommt dieser Trigger nicht vor |

Jedes MOVE des automatischen Servotests ist eine geänderte Pulsbreite: ein
Test mit dem veröffentlichten Panel wird ohne Capture aufgezeichnet. Ein
Trigger während eines Trace fügt seine Zeile ein und verschiebt das Ende
auf 4 s (bei `t` 10 s) nach sich selbst, wenn das später liegt: Bewegungen
mit weniger als 4 s Abstand sind ein Trace. Ein Kommando mit Slew ändert
die Pulsbreite in jedem Durchlauf: seine Zeile `$C` trägt die erste
Pulsbreite, und eine Zeile `$D` die, bei der es endete, sobald der Ausgang
50 ms still gehalten hat. `cmd=` der Zeile `$D` ist die Zeit der Zeile `$C`
ihres Kommandos, und das Tool beendet dieses Kommando dort, auch wenn der
Ausgang losgelassen wurde und das nächste Kommando innerhalb von 30 ms
kam. Ein Kommando, das auf den nächsten Trace wartet,
nimmt seine späteren Änderungen mit: dieser Trace endet 4 s nach der
letzten davon und enthält die Zeile `$D`. Ein Ausgang, der losgelassen
wird, beendet dort ein Kommando mit Slew; der nächste Puls auf ihm ist ein
Kommando, wie bald er auch kommt. Die Bewegungen des automatischen Tests
sind Sprünge und haben keine Zeile `$D`. `x` auf der Konsole beendet
einen Trace; eine Triggerzeile, deren Zeit auf dem Stopp oder danach
liegt, entfällt. Jeder Trace enthält auch die Samples von bis zu 64 ms vor
seinem Trigger.

**Was es kostet.** Core 1 liest CH1 jede 1 ms wie im veröffentlichten
Image und kopiert das Sample in einen Ring aus 4096 Einträgen (49.152
Bytes; 3,9 s bei 1000 Samples und 50 Busspannungen je Sekunde). Der Tick
bekommt keine Bus-Transaktion dazu. Eine Änderung der Konfiguration oder
des Zustands des Bauteils ist ein Eintrag im selben Ring und behält so
ihren Platz zwischen den Samples. Core 0 schreibt ganze Zeilen in den
Platz, den der 64-Byte-Sendepuffer der Konsole hat, und nichts, wenn kein
Terminal verbunden ist: seine Schleife wartet nicht auf den Host. Ein
voller Ring verwirft den neuesten Eintrag: der Trace hat eine Zeile
`$L n=`, wo Einträge fehlen, und ihre Summe in seiner Endzeile. Die
Endzeile zählt jeden verworfenen Eintrag von vor dem Ende des Trace, ob
danach noch ein Eintrag in den Ring kam oder nicht. Ein Sample, das
verworfen wird, während ein Konfigurationseintrag auf Platz wartet, zählt
nach diesem Eintrag; ein Trace, der an der Änderung der Konfiguration
endet, zählt es nicht. Um wie
viel ein Durchlauf von Core 0 durch das Schreiben der Zeilen länger wird,
ist nicht gemessen.

**Die Zeilen** (`shared/sense/sense_trace.h` nennt jedes Feld):

```text
$T v=3 n=2 trig=cmd t=220200 ms=22020 len=4000
$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=0
-810,289
10,277
v5952
10,289
$C t=220200 ch=0 us=1800
10,2378
$Z n=2 s=8881 v=444 l=0 m=4 ml=0 e=t
```

Die Zeilen nach den ersten beiden stehen in der Reihenfolge ihrer Zeiten,
außer einer `$D`-Zeile: eine Triggerzeile folgt auf die Samples, die vor ihr
genommen wurden. Eine `$D`-Zeile wird geschrieben, sobald der Puls des
Ausgangs 50 ms stillgestanden hat; sie folgt Samples bis etwa 50 ms nach
ihrem `t=`, und ihr `t=` ist ihre Zeit.
Core 0 schreibt eine Triggerzeile und die Endzeile, sobald Core 1 einen
Tick beendet hat, der zur Zeit der Zeile oder danach begann. `e=` in der
Endzeile ist der Grund für das Ende: `t` die Zeit, `s` ein geändertes
Set-up, `k` die Konsole. Ein Großbuchstabe (`T`, `S`, `K`) sagt, dass
Core 1 innerhalb von 100 ms nicht hinter der Zeit einer Triggerzeile oder
dem Ende gesehen wurde und die Zeile trotzdem geschrieben ist: ein Sample
von vor dieser Zeit kann fehlen oder hinter der Zeile stehen. Das Tool
meldet einen solchen Trace als Problem und spielt keine seiner Bewegungen
nach.

Eine Sample-Zeile ist die Zeit seit dem Sample davor in 0,1 ms und der
Shunt-Code, 40 µV je Schritt: 0,4 mA am 0,1-Ω-Shunt. Eine Sample-Zeile 1 ms
nach dem Sample davor hat 8 Bytes bei einem dreistelligen Code und höchstens
10 Bytes bei einem vierstelligen. Die erste Sample-Zeile eines Traces zählt
ab dem Trigger-Zeitpunkt, ein Sample von vor dem Trigger trägt also eine
negative Zeit, und ein Sample nach verworfenen Records eine längere: Diese
Zeilen sind länger, bis zum vollen vorzeichenbehafteten 32-Bit-Bereich der
Zeit. Mit den Spannungszeilen (eine Zeile von 7 Bytes, eine je 20 Samples
in der Ausgabe von `sense_trace_gen`) sind es 8,35 Bytes je Sample,
8,35 kB je Sekunde, beim simulierten Servo mit 0,12 A, dessen Codes 3 Stellen
haben. Über etwa 0,4 A am 0,1-Ω-Shunt haben die Codes 4 Stellen, und die Rate
liegt bei 9,35 kB je Sekunde. Jede Zeile endet auf CR LF. Die Rate auf der
Hardware ist nicht gemessen.

**Bereitlegen:** den PD mini (WeAct PD Power Mini V1) bei einer Grenze von
2,00 A, das INA3221-Modul mit dem 0,1-Ω-Shunt von CH1 in der Versorgung
des Servos, die zu messenden Servos und einen Windows-PC mit PuTTY. Teil A
braucht keinen Encoder. Teil B braucht einen AS5600 auf der Welle des
Servos.

**Das Terminal.** Die Konsole ist die USB-C-Buchse des IOmcu-Moduls
selbst, über die auch das Image geflasht wird, nicht die des Panels. Im
Windows-Geräte-Manager steht sie unter "Anschlüsse (COM & LPT)" als
"Serielles USB-Gerät (COMn)", Hardware-ID `VID_2E8A`. Einstellungen in
PuTTY:

| Wo | Einstellung |
| --- | --- |
| Session | Connection type Serial, Serial line `COMn`, Speed 115200 |
| Connection, Serial | Data bits 8, Stop bits 1, Parity None, Flow control None |
| Session, Logging | "All session output", für jeden Lauf ein neuer Dateiname |

Der Port ist USB CDC (Communications Device Class): die Geschwindigkeit
wird nicht benutzt, jeder Wert geht. Der IOmcu druckt nur, solange
das Terminal DTR (Data Terminal Ready) hält; PuTTY tut das, solange der
Port offen ist. Keine Zeitstempel im Mitschnitt: in einem Trace ist eine
Zeile, vor der etwas steht, ein Schaden, und keine Bewegung dieses Trace
wird nachgespielt. Eine Taste wird beim Drücken gesendet, ohne Enter.

**Teil A: ohne den Encoder.**

1. Das Image bauen:

   ```bash
   export PICO_SDK_PATH=/path/to/pico-sdk
   cmake -S firmware/iomcu -B firmware/iomcu/build-trace -DSENSE_TRACE=ON
   cmake --build firmware/iomcu/build-trace
   ```

2. `firmware/iomcu/build-trace/rcbench-iomcu.uf2` flashen: BOOTSEL halten,
   das Modul an den PC stecken, die Datei auf sein Laufwerk kopieren.
3. Den Port in PuTTY mit Mitschnitt öffnen. Innerhalb von 3 s kommt eine
   Zeile, die mit `rcbench-iomcu:` beginnt. Am Panel SETUP → SENSOREN:
   `0x40 bestückt` auf ON.
4. Rauschen ohne Servo: kein Servo angeschlossen, der Ausgang des Netzteils
   an bei 6,00 V, `t` drücken. 10 s später druckt die Konsole eine Zeile,
   die mit `$Z` beginnt. PuTTY schließen; dieser Mitschnitt ist
   `noise.log`.
5. Servo in Ruhe: neue Mitschnittdatei. Das Servo anschließen, auf ARMED
   schalten, in der Mitte lassen, Netzteil an bei 4,80 V. `t` drücken und
   auf die Zeile `$Z` warten.
6. Bewegungen, in denselben Mitschnitt: auf der Aufgabe TEST des
   Bildschirms SERVO, in den OPTIONEN der Karte AUTOMATISCHER TEST,
   TESTDAUER NACH auf BEWEGUNGEN, BEWEGUNGEN 20, HALTEZEIT 1000 ms,
   STUFE 4.8 V an, jede andere Stufe und BROWN-OUT aus. TEST STARTEN. Der
   Trace läuft von der ersten Bewegung bis 4 s nach der letzten; auf seine
   Zeile `$Z` warten, dann PuTTY schließen.
7. Die Schritte 5 und 6 bei 6,00 V mit STUFE 6.0 V wiederholen, und beides
   für jedes Servo: eine Mitschnittdatei je Servo und Spannung.
8. Am Host, für jeden Mitschnitt:

   ```bash
   python3 tools/sense_trace.py noise.log
   python3 tools/sense_trace.py mg90s-4v8.log
   ```

**Gut:** jeder Trace meldet `counts match the end line` und `0 records
missing`; der Exit-Code ist 0.

**Was das Tool druckt.** Je Trace: die gelesenen Samples und Spannungen
gegen die Endzeile; Mittelwert, Standardabweichung und größter Abstand vom
Mittelwert der Samples vor dem ersten Kommando, wie gelesen und durch
einen gleitenden Mittelwert über 4 und über 8 Samples; dann jede Bewegung
durch `shared/servo/servo_move.c` gespielt, mit dem Filter bei 1, 4 und 8
Samples und dem Band bei 0,02, 0,05 und 0,10 A: gesehen oder nicht, und
die Ankunft in ms ab dem Frame. Für den Mitschnitt als Ganzes: die
Einstellungen, die jede Bewegung sehen und ihre Ankunft messen, der Median
der Ankunft je Einstellung und sein Abstand zur Einstellung des Capture
(Filter 4, Band 0,05 A), und wie weit die Einstellungen die Ankunft einer
Bewegung auseinanderlegen. Die Ausgabe endet mit `not compared with the
horn`: ohne Encoder ist eine Ankunft der Strom zurück auf seinem
Haltepegel, und das kann dem Arm vor- oder nachlaufen. Je Trace eine
`<log>-trace-<n>.csv` mit vier Spalten, getrennt durch `;`: Zeit in ms,
Strom in A, die Busspannung in V auf den Zeilen, die eine haben, und
`clipped`. `clipped` ist 1 für ein Sample am oder über dem oberen Ende des
Bereichs, -1 am oder unter dem unteren Ende und 0 für einen Wert. Der
Strom einer geclippten Zeile ist das Ende des Bereichs, eine Grenze und
kein Wert: 1,6380 A oder -1,6384 A an einem Shunt von 0,1 Ω. Eine Datei
mit drei Spalten hat keine Clip-Markierung, und ihre geclippten Zeilen
lesen sich als Werte.

**Was das Tool ablehnt.** Zwischen der Zeile `$T` und der Zeile `$Z` eines
Trace ist jede Zeile eine ganze Zeile des Formats oder eine eigene
Konsolenzeile des IOmcu, die mit `rcbench-iomcu:` beginnt. Ganz ist
eine Zeile mit jedem Feld ihres Typs in seiner Reihenfolge, mit nichts
vor, zwischen oder hinter den Feldern und mit jeder Zahl in dem Bereich,
in dem der IOmcu sie hält: eine Zeile `$C` oder `$D` hat `ch=` und
`us=`, eine Zeile `$D` danach `cmd=`, eine Zeile `$E` oder `$K` keines von
beiden. Das Tool liest die Formatversionen 3 und 2 (`$T v=`). Eine Zeile
`$D` der Version 2 hat kein `cmd=`, und das Tool beendet mit ihr das
späteste Kommando ihres Kanals, das höchstens 30 ms nach ihrem `t=` liegt:
ein Kommando, das innerhalb von 30 ms auf einen losgelassenen Ausgang
folgt, nimmt diese Zeile `$D` als seine eigene. Eine Zeile `$D` in der Form
der anderen Version ist ein Schaden. Eine Zeile endet mit
LF oder CR LF. Alles andere ist ein Schaden. Ebenso eine zweite Zeile
`$H`, eine andere Zeile als `$L` vor der Zeile `$H`, eine andere
Trace-Länge als 4000 ms (10000 ms bei `t`), eine andere Sample-Periode als
1000 µs, ein Sample mit einer Zeit vor dem Sample davor, eine zweite
Spannung zu einem Sample, eine Zeile `$S`, die den Zustand davor
wiederholt, und eine Zeile eines Trace außerhalb eines Trace. Eine Zeile
`$L` vor der Zeile `$H` zählt Records, die vor dem ersten Sample des Trace
fehlen, und keine Bewegung dieses Trace wird nachgespielt. Das Tool druckt eine Zeile `PROBLEM:` mit der
Anzahl und der Nummer der ersten solchen Zeile, spielt keine Bewegung
dieses Trace nach und endet mit 1.

Die Pegel einer Bewegung kommen aus vier Fenstern von Samples:

| Fenster | Was daraus genommen wird |
| --- | --- |
| die Samples vor dem ersten Kommando des Trace | die Schwelle jeder Bewegung; der Haltepegel einer Bewegung zu einer Pulsbreite, die keine frühere Bewegung verlassen hat |
| die 50 ms vor der Bewegung | ihr Pegel davor |
| die 200 ms vor der Bewegung | ihre Schwelle; der Haltepegel jeder späteren Bewegung zurück zu der Pulsbreite, die diese Bewegung verlassen hat |
| von 50 ms vor der Bewegung bis zur nächsten Bewegung oder zum Ende des Trace | die Samples, die das Nachspielen bekommt |

Ein Fenster ohne Sample, mit einer Zeile `$L` darin oder an einem seiner
Enden oder mit einer Zeile `$S` darin oder an einem seiner Enden gibt
keinen Pegel. Jede Bewegung, die diesen Pegel braucht, meldet `not
replayed` und nennt das Fenster. Ein Sample am Anschlag in einem Fenster
zählt als 4094 Schritte zu diesem Ende des Bereichs, wie beim Nachspielen,
und die Zeile der Bewegung sagt, wie viele ihrer Pegel-Samples am Anschlag
liegen.

**Mehr als ein Ausgang.** `ch=` in einer Zeile `$C` oder `$D` ist der
Ausgangskanal, nicht der Pin: die Kanäle zählen ab 0 über die gebundenen
Pins in aufsteigender Pin-Reihenfolge, 1 Kanal je PWM-Pin. Sind GP0, GP1,
GP2 und GP4 gebunden, ist GP2 Kanal 2 und GP4 Kanal 3. Der Shunt führt den
Strom jedes Servos an der Versorgung, und der Bildschirm SERVO kommandiert
jeden Servo-Ausgang im selben Frame. Das Tool spielt kein Kommando nach,
das ein anderes Kommando im Abstand von höchstens 30 ms hat; mit 2
gebundenen Servo-Ausgängen spielt es also keine Bewegung nach.
`--loaded-channel N` nennt einen Kanal, an dessen Pin ein Servo
angeschlossen ist; die Option wiederholt sich, je 1 Kanal:

```bash
python3 tools/sense_trace.py mg90s-4v8.log --loaded-channel 2
```

Die Zeilen `$C` und `$D` jedes anderen Kanals sind dann keine Bewegungen
und zählen für keine 30 ms. Eine Zeile `$E` hat keinen Kanal: die
Kommandos der genannten Kanäle nehmen ihre Edge-Zeilen zuerst, und eine
übrige Edge-Zeile im Abstand von höchstens 30 ms zum Kommando eines
anderen Kanals wird ausgelassen. Die zweite Zeile der Ausgabe nennt die
Kanäle und zählt, was ausgelassen ist:

```text
loaded channel(s) 2: 25 command line(s) of channel(s) 1, 3 left out
```

Sie endet mit `no command line of channel(s) N` für einen genannten Kanal,
den keine Zeile `$C` des Mitschnitts hat; der Exit-Code bleibt 0. Zwei
genannte Kanäle, die im Abstand von höchstens 30 ms kommandiert werden,
werden nicht nachgespielt. Ohne die Option zählt jeder Kanal. Das Tool
prüft nicht, ob am Pin eines genannten Kanals ein Servo hängt: ein Servo
an einem nicht genannten Kanal bewegt sich im nachgespielten Strom.

`--floor` nimmt 0,000001 A bis 1000 A, `--pair-ms` mehr als 0 ms bis
60000 ms, `--csv-offset` bis 1e9 s beiderseits von 0, `--loaded-channel`
eine ganze Zahl von 0 bis 65535 ohne Vorzeichen und ohne führende Null;
jeder andere Wert endet mit 2. Eine CSV-Datei des
Servo-Tests, die nicht gelesen werden kann, eine CSV-Datei, die nicht
geschrieben werden kann, und ein Nachspielen, das eine andere Zahl von
Bewegungen beantwortet als es bekam, enden mit 2.

**Aufschreiben:** die Ausgabe des Tools für jeden Mitschnitt. Teil A
beantwortet: das Rauschen der 1-ms-Samples von CH1 ohne Servo und mit
ruhendem Servo, gegen die Untergrenze der Schwelle (`SERVO_MOVE_MIN_A`,
0,020 A); welche Filterlängen und Bänder alle 22 Bewegungen jedes Tests
sehen (2, die das Servo an die Enden stellen, und 20 gezählte); und wie
stark sich die Ankunft mit der Einstellung verschiebt. Ob eine Ankunft die
des Arms ist, beantwortet er nicht.

**Teil B: mit dem AS5600.** Dieselben Bewegungen mit dem Encoder auf der
Welle.

1. SETUP → ANSCHLÜSSE: `AS5600` auf ON, bei einem Prüfstand in SAFE. Auf der Seite
   PRÜFLING des Bildschirms SERVO, Servo in Neutralstellung, SENSOR-MITTE
   antippen.
2. Die Schritte 5 bis 7 von Teil A wiederholen.
3. Jeden Mitschnitt zusammen mit der `ATESTnnn.CSV` seines Tests aus dem
   Ordner `USER/LOGS` der SD-Karte aufheben.
4. Am Host, für jedes Paar:

   ```bash
   python3 tools/sense_trace.py mg90s-4v8.log --servo-csv ATEST012.CSV
   ```

Das Tool druckt dann zusätzlich je Bewegung die Ankunft abzüglich der
`travel angle (ms)` des Encoders und ihren Median je Einstellung. Teil B
beantwortet, was die Filterlänge des Capture (`SENSE_CAP_FILTER_N`, 4) und
das Ankunftsband (`SERVO_MOVE_BAND_A`, 0,05 A) sein sollen: beide sind
gewählt, nicht gemessen.

**Nicht bekannt:**

- Teil B: die Stellzeit des Encoders zählt ab dem Kommando, wie das Panel
  es ausgibt, die Ankunft des Trace ab dem PWM-Frame am Pin. Die Differenz,
  die das Tool druckt, enthält die Zeit zwischen beiden: bis zu einem Poll
  und einem Frame, nicht gemessen.
- Die Frame-Zeit eines Kommandos wird aus dem PWM-Zähler berechnet, der
  nach dem Schreiben des Pulses gelesen wird. Ein Frame, der zwischen
  beidem endet, setzt die Zeit dieses einen Kommandos einen Frame (20 ms
  bei 50 Hz) zu spät. Wie oft: nicht gemessen.
- Teil B: die beiden Dateien haben verschiedene Uhren. Das Tool ordnet Zeilen und
  Kommandos über die Abstände der Bewegungen zu; ordnen zwei Versätze
  gleich viele Zeilen zu, aber nicht dieselben Zeilen denselben
  Bewegungen, sagt es das, endet mit 1 und nimmt `--csv-offset`.
- Ein Mitschnitt mit einem Neustart des IOmcu darin hat eine Uhr je
  Boot. Das Tool nimmt eine Zeile `$T` als Neustart, wenn ihr
  Millisekunden-Tick vor dem der letzten liegt oder ihre Trace-Nummer
  nicht hinter der letzten; fehlende Nummern sind Traces, die keine
  Konsole abnahm, kein Neustart. Ein Neustart, nach dem keines von beiden
  zurückgeht, wird nicht erkannt.
  Das Tool ordnet die Bewegungen jedes Boots für sich zu. Liegen die
  Zeilen eines späteren Boots nicht hinter denen des Boots davor, ordnet
  es keine Zeile zu, sagt das und endet mit 1: den Teil des Mitschnitts
  jedes Boots mit seinem Teil der CSV-Datei auswerten.
- Kommandos im Abstand von höchstens 30 ms, etwa zwei Ausgänge in einem
  Frame, ändern den Strom gemeinsam. Das Tool listet sie und spielt
  keines davon nach, außer `--loaded-channel` lässt alle bis auf eines
  aus. Ein Kommando, das später als das auf das Kommando
  eines anderen Ausgangs folgt, bevor dessen Servo angekommen ist, wird
  mit dem Strom des ersten Servos darin nachgespielt; das Tool
  unterscheidet beide nicht.
- Ob ein Terminal 8,35 bis 9,35 kB je Sekunde ohne Verlust mitschneidet. Ein Trace,
  dessen Zeilenzahlen nicht zu seiner Endzeile passen, wird gemeldet und
  endet mit 1.
- Das Zeilenformat hat keinen Prüfwert. Eine Zeile, die in ihrer letzten
  Zahl abgeschnitten ist, ist eine ganze Zeile mit einer anderen Zahl, und
  das Tool liest sie so: der Code eines Samples, eine Spannung, die
  Pulsbreite einer Zeile `$C`, die Kommandozeit einer Zeile `$D`, die
  Zeit einer Zeile `$E` oder
  `$K`, der Reset-Zähler einer Zeile `$H` oder `$S`. Ein Schnitt an jeder
  anderen Stelle einer Zeile wird gemeldet, ebenso eine verlorene Sample-,
  Spannungs- oder Trigger-Zeile. Eine verlorene Zeile `$S` wird gemeldet,
  wenn die nächste den Zustand wiederholt. Ein Mitschnitt, der zwischen
  zwei Traces endet, liest sich wie ein Mitschnitt mit weniger Traces.

---

## 9. Wenn etwas schiefgeht

| Symptom | Zuerst hier nachsehen |
|---|---|
| Schalten auf ARMED verweigert, Draht gesteckt | Heartbeat hat noch keine 4 Flanken, oder Lücke über 150 ms. GP3 messen. |
| Arm verweigert, kein offensichtlicher Grund | Touch 500 ms tot sperrt es. Das Panel berühren. |
| Link steht, Bildschirm bietet keine Pins an | Der Katalog der Platine kam nicht an. Die Konsole sagt, welche Page scheiterte. |
| Heartbeat fällt beim ersten Link-Aufbau aus | **Die Übertragung der Zeichnung oder der Flash-Keeper.** Siehe §4. |
| Display friert kurz ein | Ein Flash-Schreiben. Erwartet, solange die Zeichnung der Platine abgelegt wird, einmal pro Flash-Operation, 4 für die Zeichnung des RP2350-CAN. |
| IOmcu bootet nach dem Flashen nicht | Image über 4 MB. Die Größenzeile aus §1 prüfen. |
| Panel bootet, aber keine `boardart`-Partition | Nur die App über eine alte Tabelle geflasht. Merge-bin bei Offset 0. |
| Output geht nach ½ s in die Mitte | Arbeitet wie vorgesehen — in diesen Kanal hat nichts geschrieben, also ist er in seine Ruhelage gegangen: Mitte beim Servo, null beim Motor. Die Impulse laufen weiter, solange der Prüfstand ARMED ist |
| Impulse hören ganz auf | Nicht der Timeout. Etwas hat den Pin freigegeben, auf SAFE geschaltet oder den Prüfstand gestoppt |
| `sense flags` ohne 0x0100, ein Bauteil freigegeben | Der I2C-Block hat auf diesen Pins nicht geöffnet. Sie müssen SDA und SCL eines Blocks sein: die GPIO-Nummer von SDA mod 4 ist 0 oder 2, SCL die nächste |
| `sense flags` 0x0200, das bleibt | SDA oder SCL low gehalten: ein unversorgtes Modul klemmt den Bus, ein Pull-up fehlt, oder ein Kurzschluss |
| `sense present 0x0000` bei offenem Bus | Nichts antwortet: Pull-ups fehlen, Module unversorgt, oder SDA und SCL vertauscht |

**Verhält sich der Prüfstand so, dass es aufs Panel zeigt**, sind die drei
neuesten und am wenigsten bewährten Dinge nur vom Compiler geprüft:

- das **Ablegen der Zeichnung** in der Control-Task, ein Flash-Schritt pro Durchlauf,
- die **Scheibe der Zeichnung** in der Poll-Schleife der Control-Task und
- die **Run-Log-Task** (`runlog`), der jeder Schreibzugriff auf die SD-Karte
  (Secure Digital) gehört.

Die ersten beiden sind bei einem IOmcu, der keine Zeichnung meldet, wirkungslos —
das ist der schnellste Weg, sie auszuschließen. Die dritte ist ohne Karte im
Schacht wirkungslos. Mit Karte hängt sie bei jedem Fehler eine Zeile an
`BENCH/RCBENCH.LOG`, ob ARMED oder nicht; die Lauf-Datei wartet auf das Schalten auf
ARMED und auf die erste Zeile danach.

---

## 10. Was aufzuschreiben ist

Für jeden Schritt: was gemessen wurde, wogegen es erwartet wurde und was das
Oszilloskop zeigte. `STATUS.md` trägt eine Tabelle „Open items“ — die Zeilen
über Treiber auf Hardware, die Control-Page und den Flash-Speicher sind die,
die dieser Lauf beantwortet.

**Was nicht gemessen ist, bleibt als nicht gemessen geschrieben.** Eine in
diese Tabelle geratene Zahl ist schlimmer als eine leere Zelle.
