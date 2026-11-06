# Erster Lauf auf der Hardware

<sub>[English](FirstRun.md) · **Deutsch**</sub>

Dieses Verfahren prüft beide eingeschalteten Platinen mit angeschlossener
Heartbeat-Leitung. Miss jeden der folgenden Werte mit einem Messgerät.
Jeder Schritt nennt das erwartete Ergebnis und die Angaben, die du bei einer
Abweichung notieren sollst.

Arbeite die Liste von oben nach unten durch. Jeder Schritt setzt voraus,
dass der vorherige bestanden ist.

---

## 0. Vor dem Einschalten

**Bereitlegen:** ein Oszilloskop, ein Servo, einen ESC, der bidirektionales
DShot spricht, ein Labornetzteil mit Strombegrenzung und das USB-Kabel für
jede Platine.

**Schließe noch keinen Motor an den ESC an.** Schritt 5 prüft die
Verriegelung ohne angeschlossene Ausgänge. In Schritt 6 misst du erstmals
einen Ausgangspin. Prüfe ihn am Oszilloskop, bevor du eine Last anschließt.

**Strombegrenzung:** Stelle sie so niedrig ein, dass ein Kurzschluss am
Ausgang die Begrenzung auslöst, bevor eine Leiterbahn verbrennt.

> ### Die Konsole des Panels und seine USB-C-Buchsen
>
> Die Konsole des Panels ist UART0, mit USB-Serial-JTAG (der eingebauten
> USB-Seriell- und Debug-Brücke des ESP32-P4) als zweiter Konsole. Das
> 7-Zoll-Platine mit ESP32-P4 hat zwei USB-C-Buchsen. Welche davon UART0
> erreicht und welche USB-Serial-JTAG, ist nicht nachverfolgt
> ([Inbetriebnahme](Bringup-de.md#panel)). `esptool.py` flasht die Platine
> über jeden der seriellen Ports, die es zeigt.

---

## 1. Beide Platinen flashen

Das Panel verwendet die 7-Zoll-Displayplatine mit ESP32-P4 von Waveshare
(ESP32-P4-WIFI6-Touch-LCD-7B). Eine neue Platine enthält kein rcbench-Image.
Installiere es beim ersten Mal über USB (Universal Serial Bus), wie in
[Schritt 3](#aus-den-release-assets) beschrieben. Releases 0.22.1 und älter
laufen auf dem ESP32-S3-Panel und nicht auf dieser Platine
([README](../README-de.md#displaywechsel-esp32-p4-panel-ab-0230)). Der IOmcu
ist an beiden Panels dieselbe Platine. Was er lädt, und wie, hängt vom
Release ab, das er hat:

| IOmcu hat | IOmcu lädt |
| --- | --- |
| nichts (ein neues Modul), oder 0.18.0 und älter | über USB, [Schritt 2](#aus-den-release-assets) |
| 0.19.0 oder neuer | über CAN (Controller Area Network) vom Panel, [Der IOmcu über CAN](#der-iomcu-über-can) |

Läuft das Panel, installiert es ein späteres Release über WLAN
([Bildschirme: WLAN](Screens-de.md#wlan)) oder lädt es von GitHub
([Bildschirme: Updates](Screens-de.md#updates)). Beide nehmen das
Release-Asset `rcbench-panel-p4-vX.Y.Z.bin`; die Update-Prüfung sucht diesen
Namen in den `SHA256SUMS` des Releases.

Die SD-Karte (Secure Digital) trägt ein FAT32-, FAT16-, FAT12- oder
exFAT-Volume (extended FAT). Eine Karte bis 2TB lässt sich daher mit ihrem
Auslieferungsformat einbinden. Das Panel liest kein NTFS (New Technology File
System), keine Karte mit GPT (GUID-Partitionstabelle) und kein Volume über
2TiB, und es formatiert keine Karte ([Bildschirme: Das Format der
SD-Karte](Screens-de.md#das-format-der-sd-karte)). Eine Karte von einem
Prüfstand mit 0.21.0 oder älter enthält Dateien in Ordnern, die das Panel
nicht liest ([unten](#die-sd-karte-von-0210-und-älter)).

### Aus den Release-Assets

| Asset | Enthält | Für |
| --- | --- | --- |
| `rcbench-iomcu-vX.Y.Z.zip` | `rcbench-iomcu-install.uf2`, `rcbench-iomcu.uf2`, `rcbench-iomcu-pt.uf2`, `rcbench-iomcu.bin`, `rcbench-iomcu.elf` | das USB-Laden des IOmcu: `rcbench-iomcu-install.uf2` |
| `rcbench-iomcu-rbmod-vX.Y.Z.zip` | dieselben fünf Dateien, benannt `rcbench-iomcu-rbmod-*` | den IOmcu auf dem rbmod-Carrier ([Link: Boards mit verlöteten Pins](Link-de.md#boards-mit-verlöteten-pins)): `rcbench-iomcu-rbmod-install.uf2` |
| `rcbench-panel-p4-vX.Y.Z.zip` | `flash_args`, `bootloader/bootloader.bin`, `partition_table/partition-table.bin`, `ota_data_initial.bin`, `rcbench-panel.bin`, `rcbench-panel-merged.bin`, `rcbench-panel.elf`, `NOTICE`, `LICENSE` | den USB-Flash des Panels |
| `rcbench-panel-p4-vX.Y.Z.bin` | die Anwendung des Panels allein, mit den zwei IOmcu-Images, die es über CAN anbietet | ein Update über WLAN; nicht für USB |
| `SHA256SUMS` | die SHA-256 (Secure Hash Algorithm) der vier Dateien oben | die Prüfung in Schritt 1 |

Werkzeuge: das Laufwerk `RP2350` oder `picotool` für den IOmcu,
`esptool.py` für das Panel (`pip install esptool`; ESP-IDF v6.1 bringt
esptool 5.6.0 mit). Die Datei `flash_args` schreibt ihre Optionen wie
esptool 5 (`--flash-mode`); esptool 4.x, das ESP-IDF v5.5 mitbringt, lehnt
diese Schreibweise ab, sodass `@flash_args` esptool 5 oder neuer braucht.
`/dev/ttyACM0` unten heißt unter Windows `COMx`.

1. Führe im Download-Ordner `sha256sum -c SHA256SUMS` aus. Für jede der vier
   Dateien muss `OK` erscheinen.
2. **IOmcu.** Entpacke `rcbench-iomcu-vX.Y.Z.zip`. Verwende auf dem
   rbmod-Carrier stattdessen `rcbench-iomcu-rbmod-vX.Y.Z.zip` und dessen
   `rcbench-iomcu-rbmod-*`-Dateien anstelle der folgenden Namen. Halte
   BOOTSEL, während du den IOmcu über USB anschließt. Das Modul zeigt dann
   sein Laufwerk `RP2350`. Kopiere `rcbench-iomcu-install.uf2` auf das
   Laufwerk oder führe aus:

   ```bash
   picotool load -v -x rcbench-iomcu-install.uf2
   ```

   Das Modul startet neu in Slot A. Seine Konsole gibt alle 3s
   `rcbench-iomcu: running from slot A` aus
   ([unten](#die-partitionstabelle-des-iomcu)). Bei einem IOmcu mit 0.19.0 oder
   neuer überspringst du diesen Schritt.
3. **Panel.** Entpacke `rcbench-panel-p4-vX.Y.Z.zip` und schließe eine
   USB-C-Buchse des Panels an ([§0](#0-vor-dem-einschalten)). Führe im
   entpackten Ordner den folgenden Befehl aus, wenn die Platine neu ist
   oder du mit den Standardeinstellungen beginnen möchtest:

   ```bash
   esptool.py --chip esp32p4 -p /dev/ttyACM0 write_flash 0x0 rcbench-panel-merged.bin
   ```

   Um die Einstellungen des Panels zu behalten, verwende:

   ```bash
   esptool.py --chip esp32p4 -p /dev/ttyACM0 write_flash @flash_args
   ```

   Ein Browser-Flasher, der eine Datei an einen Offset schreibt, etwa das
   ESP Web Tool, nimmt `rcbench-panel-merged.bin` bei 0x0, mit derselben
   Wirkung wie der erste Befehl.
4. Schalte beide Platinen mit verdrahtetem Link ein. Ein in Schritt 2
   geladener IOmcu hat den Build, den das Panel trägt, und das Panel
   bietet kein Update an. Das Panel lädt die Zeichnung der Platine
   ([§4](#4-erster-link-aufbau-die-zeichnung-der-platine)).

| Was | Zusammengeführtes Image bei 0x0 | Einzelteile (`@flash_args`) |
| --- | --- | --- |
| Einstellungen und WLAN-Einstellungen des Panels, im NVS (non-volatile storage, 24KiB bei 0xD000) | gelöscht; das Panel startet mit den Standardwerten | bleiben |
| Gespeicherte Platinenzeichnungen des Panels, `boardart` bei 0x7E0000 | bleiben: das Image endet mit der Anwendung in `ota_0` | bleiben |

Der IOmcu behält seine Zuordnungen, die Netzteilverdrahtung und die
Konfiguration der Strommonitore. Die Ausnahme für einen RP2350 mit
Stepping A2 ist [unten](#die-partitionstabelle-des-iomcu) beschrieben.

Du kannst zuerst das Panel installieren. An einem IOmcu ohne Partitionstabelle zeigt
das Panel `IOmcu kann nicht über CAN aktualisiert werden. Installiere die Firmware einmal über USB.`, bis Schritt 2 erledigt ist; der Link läuft
solange.

### Der IOmcu über CAN

Ein IOmcu mit 0.19.0 oder neuer hat die Partitionstabelle von seinem
USB-Laden und bekommt das Image, das das Panel trägt, über CAN. Das
Link-Protokoll des Panels ist 4.37, und ein IOmcu mit 0.19.0 oder neuer
spricht 4.22 oder neuer: die Hauptversion, 4, ist gleich, also verbinden
sich beide Platinen vor und nach dem Update.

1. Schalte beide Platinen mit verdrahtetem Link ein. Beim Link-Aufbau
   vergleicht das Panel das laufende Image des IOmcu per SHA-256 mit dem
   Image, das es trägt, findet einen anderen Build, und das Band meldet
   `IOmcu nutzt einen anderen Build. Aktualisiere ihn auf UPDATES.`.
2. Die Frage öffnet sich auf dem Menü, solange der Prüfstand SAFE, der
   Netzteil-Ausgang ausgeschaltet und das WLAN des Panels aus ist. Sie nennt die
   Version auf dem IOmcu und die Version im Panel. Halte START 2s. Nach
   ABBRECHEN kannst du stattdessen `2s HALTEN ZUM IOMCU-UPDATE` auf
   SETUP > UPDATES halten. Der
   Fortschritt füllt den Bildschirm ([Bildschirme:
   IOmcu-Update](Screens-de.md#iomcu-update)), und zurück auf dem Menü
   meldet das Band `IOmcu auf X.Y.Z aktualisiert.`.

Das Update schreibt den Slot, aus dem der IOmcu nicht läuft. Seine
Zuordnungen, die Netzteilverdrahtung und die Konfiguration der
Strommonitore bleiben erhalten: sie liegen in der Store-Partition bei 0x3FC000, oder
vor deren erstem Speichern im alten Store bei 0x3FE000, und das Update
schreibt keins von beiden. Der Store-Datensatz wird beim ersten Speichern
Version 8; ein IOmcu-Image älter als 0.22.0 liest keinen Datensatz der
Version 8 und startet wie aus einem leeren Store. ABBRECHEN in der Rückfrage lässt
den IOmcu auf seinem Build; das Panel fragt nach seinem nächsten Start
wieder. Fällt während des Updates der Strom aus, läuft das alte Image
weiter, und der nächste Link-Aufbau bietet das Update wieder an. Ein IOmcu,
der das USB-Laden für 0.19.0 verpasst hat, hat keine Partitionstabelle: das
Band meldet `IOmcu kann nicht über CAN aktualisiert werden. Installiere die Firmware einmal über USB.`, und du führst [Schritt 2](#aus-den-release-assets) aus.
Die Dauer des Updates über CAN ist nicht gemessen.

### Der C6-Funk über SDIO

Der ESP32-C6 des Boards trägt Wi-Fi und Bluetooth und betreibt die Firmware
von esp_hosted. Ein Panel-Image bringt die C6-Firmware seiner
esp_hosted-Version mit, 2.13.0; der ausgelieferte C6 meldet keine (`nicht
gemeldet`).

1. Starte Wi-Fi einmal (WI-FI > ACCESS POINT), damit der C6 nach seiner
   Version gefragt wird, und schalte es wieder aus.
2. Auf SETUP > UPDATES zeigt die Zeile C6 RADIO `C6-Funk: nicht gemeldet.
   Auf diesem Panel: 2.13.0.`. Halte `2s HALTEN ZUM C6-FUNK-UPDATE` 2s lang,
   mit dem Prüfstand SAFE und dem Netzteilausgang aus. Die Taste zählt
   `C6-UPDATE 0%` bis `100%`.
3. Das Band meldet `C6-Funk auf 2.13.0 aktualisiert. Das Panel startet
   neu.`, und das Panel startet 3s später neu. Nach dem nächsten
   Wi-Fi-Start zeigt die Zeile `C6-Funk: 2.13.0. Auf diesem Panel: 2.13.0.`.

Ein C6, der das Update ablehnt, hinterlässt `Der C6-Funk nimmt kein Update
über SDIO an. Flashe ihn über Header H4.`; er behält seine Firmware, und
Wi-Fi arbeitet wie zuvor. Das Flashen über Header H4 steht in [Bauen:
Wiederherstellung über Header H4](Building-de.md#wiederherstellung-über-header-h4).
Ob der ausgelieferte C6 das Update über SDIO annimmt und wie lange es
dauert, ist nicht gemessen.

### Die SD-Karte von 0.21.0 und älter

Das Panel hält seine Dateien auf der SD-Karte (Secure Digital) in Ordnern
unter `BENCH` und `USER` ([Logs](Screens-de.md#logs)). Es verschiebt nichts,
was eine Karte von 0.21.0 oder älter hält, und liest dies nicht:

| 0.21.0 und älter | Gelesen aus | Am Computer |
| --- | --- | --- |
| `/ESC/*.json`, ESC-Profile | `/BENCH/ESC/CONF/` | die Dateien dorthin verschieben |
| `/KST/*.TXT`, Servodateien, der Vermerk `KST.TXT` und sein neuer Text `KST.NEW` | `/BENCH/KST/CONF/` | die Dateien dorthin verschieben, `KST.TXT` und `KST.NEW` mit |
| `/RCBENCH.LOG`, Fehlerprotokoll | nicht gelesen; neue Zeilen gehen nach `/BENCH/RCBENCH.LOG` | behalten oder löschen |
| Laufaufzeichnungen im Stammverzeichnis | gelistet im Tab STAMM von LOGS | nichts zu tun |

Ein Vermerk `KST.TXT` oder `KST.NEW` in `/KST` wird beim Start nicht
gefunden. Verschiebe die Datei zuerst, damit die Wiederherstellung nach
einem unterbrochenen Schreibvorgang angeboten werden kann.

### Aus einem Quellbaum

```bash
# IOmcu — erzeugt rcbench-iomcu-install.uf2 und rcbench-iomcu.uf2
cmake -S firmware/iomcu -B firmware/iomcu/build
cmake --build firmware/iomcu/build

# Panel
idf.py -C firmware/panel set-target esp32p4
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor
```

Der IOmcu-Build gibt seine eigene Größenprüfung aus:

    -- rcbench: image 279860 bytes, 13% of the 2084864 bytes of one slot

**Achtung:** das Modul für das Bring-up ist ein Waveshare RP2350-CAN mit
**4MB**, während `PICO_BOARD` auf eine Board-Datei zeigt, die **16MB**
angibt. Der Linker misst gegen 16MB und warnt nicht. `firmware/iomcu/cmake/image_fits.cmake`
prüft das Image gegen einen Slot der Partitionstabelle, 2.084.864 Bytes, gibt die Zeile oben aus
und lässt den Build fehlschlagen, wenn das Image größer ist.

### Die Partitionstabelle des IOmcu

Der Flash des IOmcu trägt eine Partitionstabelle: zwei Image-Slots
von je 2036KiB und eine eigene 8-KiB-Partition für den Ausgangsspeicher
([Link](Link-de.md#firmware-update-über-den-link) zeigt die Aufteilung).
Spätere IOmcu-Images schickt das Panel über CAN in den Slot, der nicht
läuft. Ein neues Modul und ein IOmcu mit 0.18.0 oder älter (einem Image des
Protokolls 4.21 oder älter) haben keine Tabelle und bekommen Tabelle und
erstes Image einmal über USB.

Schließe den IOmcu über USB an und halte BOOTSEL beim Einschalten. Sobald
das Laufwerk `RP2350` erscheint, kannst du das Image laden:

```bash
# die Datei auf das Laufwerk ziehen, oder:
picotool load -v -x firmware/iomcu/build/rcbench-iomcu-install.uf2

# prüfen: Partitionen 0 (A), 1 (B) und 2 (store, data)
picotool partition info
```

**Gut:** die Konsole des IOmcu gibt alle 3s `rcbench-iomcu: running
from slot A` aus. Die Zuordnungen, die Netzteilverdrahtung und die
Einrichtung der Strommonitore kommen aus dem alten Speicher bei 0x3FE000, bis
das erste Speichern den neuen schreibt. `no partition table -- firmware
updates over CAN need one USB load of rcbench-iomcu-install.uf2` heißt, das
Image läuft ohne die Tabelle. Lade `rcbench-iomcu-install.uf2` erneut.

Auf einem RP2350 der Stepping A2 schreibt das Laden auch den Block der
Umgehung für RP2350-E10, der im letzten 4-KiB-Sektor der 4MB landet,
0x3FF000: dem zweiten Sektor des alten Speichers. Zuordnungen, deren neuestes
Speichern dort liegt, gehen verloren, und das neueste Speichern in 0x3FE000
kommt stattdessen zurück. Prüfe nach dem Laden OUTPUTS. Das Stepping des
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

Der Flash des Panels, 32MB, hat zwei Anwendungs-Slots mit je 8 126 464
Bytes (7936KiB), `ota_0` und `ota_1`, damit sich das Panel über WLAN selbst
aktualisiert ([Bildschirme: WLAN](Screens-de.md#wlan)). Die Tabelle ist
`firmware/panel/partitions.csv`:

| Offset | Teil |
| --- | --- |
| 0x2000 | der Bootloader der zweiten Stufe, höchstens 40 960 Bytes |
| 0xC000 | die Partitionstabelle |
| 0xD000 | `nvs`, 24KiB: die Einstellungen |
| 0x13000 | `phy_init`, 4KiB |
| 0x14000 | `otadata`, 8KiB: welcher Slot startet |
| 0x20000 | `ota_0`, 7936KiB |
| 0x7E0000 | `boardart`, 128KiB: die gespeicherten Platinenzeichnungen |
| 0x800000 | `ota_1`, 7936KiB |
| 0xFC0000 | nicht vergeben, bis zum Ende des Flashs |

Die Einzelteile behalten die Einstellungen: Sie schreiben den Bootloader bei
0x2000, die Partitionstabelle bei 0xC000, `otadata` bei 0x14000 und die
Anwendung bei 0x20000 und lassen `nvs` in Ruhe. Aus einem Quellbaum:

```bash
idf.py -C firmware/panel -p /dev/ttyACM0 flash
```

Aus dem Release-Archiv `esptool.py ... write_flash @flash_args` im
entpackten Ordner, wie in [Schritt 3](#aus-den-release-assets).

Das zusammengeführte Image bei Offset 0 reicht von 0x0 bis zum Ende der
Anwendung in `ota_0` und füllt die Lücken zwischen den Teilen mit 0xFF.
`nvs` bei 0xD000 liegt in einer solchen Lücke, also löscht das
zusammengeführte Image die Einstellungen. `boardart` bei 0x7E0000 liegt
hinter seinem Ende, also bleiben die gespeicherten Platinenzeichnungen.

Das Panel installiert spätere Releases über WLAN aus dem Release-Asset
`rcbench-panel-p4-vX.Y.Z.bin`.

---

## 2. Der Link, vor allem anderen

Das Panel führt bei jedem Start einen CAN-Echo-Selbsttest aus. Er läuft
1200ms lang auf dem Startbildschirm und beginnt mit der ersten Antwort des
IOmcu, nach höchstens 5000ms Wartezeit. Er prüft, ob Frames unverändert über
den Bus gelangen. Das Page-Protokoll verwendet er nicht. Besteht der Test,
aber die Verbindung funktioniert trotzdem nicht, liegt der Fehler in einer
höheren Protokollschicht.

**Bestanden:** Der Startbildschirm zeigt `VERBINDUNG  OK  CAN 1Mbit/s` und
wechselt zum Menü.

**Fehlgeschlagen:** Vor dem Menü erscheint eine Fehlerseite mit dem
Ergebnis, einer geordneten Prüfliste und den Zählern beider Enden. Halte die
Taste zwei Sekunden, um sie zu verlassen. [Den Link in Betrieb
nehmen](Bringup-de.md#der-bus-fehler-bildschirm) hat die Urteile und was jedes
bedeutet.

**Fahre erst fort, wenn der Test bestanden ist.** Jeder folgende Schritt
setzt voraus, dass Frames ankommen.

Die Konsole zeigt weitere Einzelheiten. Sie liegt auf
UART0, mit USB-Serial-JTAG (der eingebauten USB-Seriell- und Debug-Bridge
des ESP32-P4) als Zweitkonsole; welche der zwei USB-C-Buchsen der Platine
zu welcher führt, ist nicht verfolgt:

    I (…) rcbench: CAN self-test: the IOmcu answered after 3ms
    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0

---

## 3. Der Heartbeat-Draht

| | |
|---|---|
| Panel-Seite | **GPIO28**, IO28 des 12-poligen Headers P3 des 7-Zoll-ESP32-P4-Boards (`PANEL_HEARTBEAT_PIN` in `board_pins.h`). |
| IOmcu-Seite | **GPIO 3** |
| Dazwischen | das retriggerbare Monoflop, sobald es eines gibt. Es ist auf keiner Platine, der Aufbau-Prüfstand fährt daher eine direkte Leitung und hat keine Hardware-Rückfallebene. Die Leitung deckt ein Panel ab, das keinen Heartbeat mehr sendet, solange der IOmcu fehlerfrei läuft: Er schaltet nach 150ms Stille auf SAFE. Unabgedeckt bleibt ein Panel, das keinen Heartbeat mehr sendet, während der IOmcu nicht reagieren kann: dann nimmt nichts die Ausgänge weg, und genau das täte das Monoflop ohne jede Firmware |

Ohne diesen Draht verweigert der IOmcu jedes Schalten auf ARMED, und das ist die
Verriegelung, die arbeitet — kein Fehler.

**Die Zahlen, an denen er gemessen wird:**

| | |
|---|---|
| Flankenperiode | 20ms |
| Akzeptierte Lücke | 4ms bis 150ms |
| Akzeptierte Abstände, bevor der Leitung vertraut wird | 4 |

Ab der ersten Flanke braucht die Leitung damit rund **80ms**, bis ihr
vertraut wird und ein Wechsel zu ARMED möglich ist.

**Am Oszilloskop, an GPIO 3:** ein Rechteck, Flanke zu Flanke 20ms.
Überschreitet die Lücke je 150ms, verwirft der IOmcu sie und das
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

- 26 Blöcke zu 62 Bytes über CAN, in einem Abschnitt von 15ms je Poll von
  50ms.
- **Flash-Operationen**, wenn sie geschrieben wird: 4 für diese Zeichnung
  (2 Sektoren von 4kB löschen, die Nutzdaten, der Kopf). Der DSI-Host
  (Display Serial Interface) gibt die Framebuffer per DMA (Direct Memory
  Access) ohne Bounce-Buffer aus dem PSRAM aus, und sein Interrupt läuft
  während einer Flash-Operation (`CONFIG_LCD_DSI_ISR_CACHE_SAFE`), also
  sollte das Bild jede überstehen. Was das Bild während des Schreibens tut,
  ist nicht gemessen. Das Speichern der Einstellungen schreibt den Flash auf dieselbe
  Weise.
- **Das Schreiben wartet auf SAFE.** Es nimmt einen Schritt pro
  5-ms-Durchlauf der Control-Task, und nur solange der Prüfstand SAFE ist und
  das Netzteil aus. In ARMED oder mit eingeschaltetem Netzteil erscheint
  einmal `keeping hardware 1's drawing waits for a bench in SAFE with the
  supply off` auf der Konsole, und `kept` folgt nach dem Schalten auf SAFE.
- **Der Heartbeat darf davon nicht gestört werden.** Die Übertragung ist in
  Zeitabschnitte aufgeteilt und das Flash-Schreiben wird in ARMED deswegen
  zurückgehalten. Fällt der Heartbeat während des Abrufs aus, halte an und
  notiere den Ausfall.

Beim nächsten Link-Aufbau liest das Panel Länge, CRC (zyklische
Redundanzprüfung) und Hash der Zeichnung vom IOmcu und zeigt die
gespeicherte Kopie, wenn alle drei übereinstimmen:

    I (…) rcbench: hardware 1's drawing is kept: 1560 bytes, hash 8f99cd48

Ein IOmcu, der mit einer anderen Zeichnung geflasht wurde, meldet einen
anderen Hash, und das Panel holt die Zeichnung neu. Ein IOmcu mit
Protokoll 4.19 oder älter liefert keine Zeichnung; das Panel schreibt
`hardware 1 serves no board drawing (protocol 4.19); OUTPUTS draws its
shape` ins Log und zeichnet die Platine aus ihrem Umriss.

---

## 5. Armen ohne angeschlossene Last

Noch ist keine Last an einem Ausgang angeschlossen. Dieser Schritt prüft die
Verriegelung, nicht einen Pin.

1. **Heartbeat-Draht abgezogen** → Das Schalten auf ARMED muss **verweigert** werden. Der
   IOmcu antwortet `NOT_ARMED`, weil `!beat.alive`. Das Panel zeigt die
   Verweigerung.
2. **Heartbeat-Draht gesteckt, Panel ARMED** → der IOmcu nimmt es an.
3. **Heartbeat-Draht in ARMED ziehen** → muss binnen **150ms**
   auf SAFE fallen.
4. **Drücke STOP** → STOP rastet ein. Halte STOP 2s und schließe das
   Zurücksetzen ab, bevor du ARM erneut hältst. Eine wiederhergestellte
   Verbindung darf STOP nicht aufheben.
5. **CAN-Draht in ARMED ziehen** → der IOmcu gibt nach
   **200ms** auf; das Panel eskaliert nach **1s**.
6. **Simuliere einen Ausfall des Touch-Controllers** → nach **500ms** ohne
   Antwort ist das Schalten auf ARMED gesperrt.

Jeder dieser Punkte ist host-getestet. **Keine der Zahlen unten wurde an einem Messgerät gesehen:** kein Oszilloskop und kein Logikanalysator hat irgendeine Impulsbreite, Rahmenperiode oder Antwortverzögerung in diesem Baum gelesen.

---

## 6. Erster Pin an einem Messgerät

Verwende für diesen Schritt ein **Servo**. Sein PWM-Signal lässt sich am
Oszilloskop leichter prüfen als das Signal für den ESC.

**Freie Pins für einen Ausgang:** GPIO 0, GPIO 1, GPIO 2, GPIO 4, GPIO 5, GPIO 6, GPIO 7, GPIO 13 bis
GPIO 22 und GPIO 26 bis GPIO 28. GPIO 13 ist der Pin des Programmierports an einem
IOmcu, der keinen gespeichert hat, und wird für einen Ausgang
abgelehnt, bis der Pin des Ports verlegt ist
([Link](Link-de.md#die-programmierports)).
**Reserviert und verweigert:** GPIO 3 (Heartbeat), GPIO 8–GPIO 12 (CAN), GPIO 23, GPIO 24, GPIO 25
und GPIO 29 (vom Modul belegt, nicht auf der Stiftleiste) und GPIO 30 aufwärts (nicht
im Bauteil).
**Als zweiter PWM-Pin verweigert:** ein Pin, dessen PWM-Compare-Register schon
von einem gebundenen Pin belegt ist. Auf dem RP2350 ist die Slice unterhalb
von GPIO 32 `(Pin / 2) modulo 8` und der Kanal das niedrigste Bit des Pins, also
teilen sich GPIO 0 und GPIO 16, GPIO 1 und GPIO 17, GPIO 2 und GPIO 18, GPIO 4 und GPIO 20, GPIO 5 und
GPIO 21, GPIO 6 und GPIO 22 je ein Compare-Register. Der zweite eines Paares wird
verweigert, statt auf die Pulsbreite des ersten gemuxt zu werden. Die
OUTPUTS-Seite führt kein Bit mit, das „gebunden“ sagt, der Bildschirm sieht
also weiter konfiguriert aus, während die Leitung keinen Impuls liefert.

Öffne am Panel **SETUP → OUTPUTS**, wähle `SERVO PWM` und markiere einen
Pin. Alternativ wählst du dort `SERVO PWM` in der Liste und tippst eine
Taste auf der Platinenzeichnung an. Masseanschlüsse sind mit `G` markiert,
Versorgungspins mit ihrer Spannung.

**Miss den Pin, bevor du das Servo anschließt.**

| | |
|---|---|
| Pulsrate | 40 bis 560Hz (voreingestellt 50Hz) |
| Puls | 400 bis 2500µs, außerhalb verweigert |
| Auflösung | 1µs |

**Miss und notiere:** die tatsächliche Pulsperiode, die Pulsbreite an
beiden Enden des Stellwegs und den Jitter. Das Bit-Timing gehört zu den
noch nicht auf Hardware bestätigten Eigenschaften.

**Danach:** Prüfe, dass SERVO die Stellung nur auf Anforderung freigibt.
Das Abheben des Fingers stoppt den Ausgang nicht: Die Seite hält die
vorgegebene Stellung und sendet sie erneut alle **100ms**
(`SERVO_HOLD_MS`) gegen die **500ms** des IOmcu
(`OUT_DEFAULT_TIMEOUT_MS`), ein Servo bleibt also stehen, wo es hingestellt
wurde. **FREIGEBEN** führt die Ruderflächen auf die Mitte zurück; es löscht den
Slot nicht, und der Pin pulst weiter. Beendet werden die Flanken durch
das Schalten auf SAFE, STOP oder das Verlassen des Bildschirms, was auf SAFE schaltet. Prüfe am
Oszilloskop, dass FREIGEBEN die Pulsbreite auf die Mitte des Kanalwegs setzt
und dass das Schalten auf SAFE die Pulse beendet.

Ein Kanal, den niemand auffrischt, geht nach 500ms weiterhin in seine
Ruhelage; das betrifft einen gebundenen Pin, den der Servo-Bildschirm nicht
hält.

---

## 7. Die Zahlen, die niemand gemessen hat

Die folgenden Eigenschaften sind anhand der Spezifikation implementiert
und nur mit Frames geprüft, die derselbe Code erzeugt. Notiere für jede
einen gemessenen Wert:

- **Jedes DShot-Bit-Timing**, gegen die Toleranz eines ESC statt gegen die
  Spezifikation.
- **Die Antwortrate** von fünf Vierteln der DShot-Rate.
- **Die Konvention des führenden Bits** des Gruppencodes.
- **Die Umschaltverzögerung** — ob 30µs das ist, was ein ESC wirklich
  wartet.
- **Die erweiterten Telemetrie-Frametypen** und ihre Einheiten.
- **Der PPM-DMA-Ring**, der einen Frame abspielt.
- **Das Flash-Löschfenster** auf dem IOmcu, und ob der Heartbeat über
  ein Speichern hinweg wieder greift.

---

## 8. Strommonitore (INA228, INA3221)

Core 1 des IOmcu liest einen INA228 im Leistungspfad des ESC und
einen INA3221 an der Servoversorgung über I2C (Inter-Integrated Circuit) mit
400kHz alle 1ms. Jeder folgende Schritt nennt das erwartete Ergebnis.
Notiere jede Abweichung.

**Bereitlegen:** das MATEK I2C-INA-BM (INA228, 200µΩ) auf der Adresse, mit
der es geliefert wird, das DAOKAI INA3221 (0,1Ω je Kanal, 1,638A
Vollausschlag) mit geschlossener A0-Lötbrücke nach GND für 0x40 (ab Werk
sind alle vier A0-Brücken offen, und die Adresse schwankt zwischen 0x40 und
0x41), zwei
Widerstände 2,2kΩ, ein Multimeter, ein Oszilloskop oder Logikanalysator mit
I2C-Dekodierung, ein Servo, und der ESC mit Motor oder eine ohmsche Last an
einem Netzteil mit Strombegrenzung.

**Am Panel einrichten.** SETUP → SENSORS: `INA228` auf ON; SETUP →
SENSORS: `0x40 bestückt` auf ON. Die übrigen Sensoreinstellungen passen in
der Vorgabe zu den Modulen oben: INA228 auf 0x45 mit 200µΩ, der INA3221 auf
0x40 mit 0,1Ω je Kanal und CH1, der AUSGANG 1 misst, SDA an GPIO 16 und SCL an
GPIO 17. Ohne Panel gibt die Build-Option `IOMCU_SENSE_BRINGUP=ON` beide
Bauteile mit den Vorgaben der Page frei, solange im Flash keine
Konfiguration liegt:

```bash
cmake -S firmware/iomcu -B firmware/iomcu/build-sense -DIOMCU_SENSE_BRINGUP=ON
cmake --build firmware/iomcu/build-sense
```

**Verdrahten** (alles Pads des IOmcu auf dem Modul RP2350-CAN):

| Draht | Von | Nach |
|---|---|---|
| SDA | GPIO 16 (Pad 21) | INA3221 SDA, MATEK SDA |
| SCL | GPIO 17 (Pad 22) | INA3221 SCL, MATEK SCL |
| Pull-ups | 2,2kΩ von SDA und von SCL | 3V3 (Pad 36) |
| Versorgung INA3221 | 3V3 (Pad 36), GND | INA3221 VS, GND |
| Versorgung MATEK | VBUS (Pad 40, 5V vom USB), GND | MATEK 5V, G |

Die Firmware schaltet die eigenen Pull-ups und Pull-downs der Pads an beiden
Pins ab: der Bus läuft auf den Pull-ups der Module und den beiden 2,2kΩ.
**Bevor du den IOmcu anschließt**, versorge die Module und miss SDA und
SCL gegen GND. Beide dürfen höchstens 3,3V zeigen. Auf welche
Spannung das MATEK hochzieht, ist nicht bekannt.

**Die Konsole.** Alle 3s, solange ein Bauteil freigegeben ist, druckt der
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
BENCH-Page: Schritte von 10mV und 10mA, Ladung in mAh, Energie in 0,1Wh,
und ihre Flags (0x01 Spannung, 0x02 Strom, 0x20 die des INA228, 0x40 die
Zähler des INA228).
Die dritte Zeile ist die INA3221-Map, SENSE_MAP: die bestückten Bauteile,
der FAST-Kanal, ihre Flags (0x0200 aus SENSE abgeleitet), der mittlere
Strom jedes Kanals und die Kanäle über ihrer Stromgrenze (SENSE_LIMIT) mit
ihrer Zahl
([Die INA3221-Map und ihre Fenster](LinkSensors-de.md#auf-der-konsole)).

### 8.1 Die Bauteile antworten

Schalte die Versorgung aller Geräte ein und lasse den Prüfstand SAFE.

**Gut:** `flags 0x0133`, `present 0x0021`, `ids 0x228x 0x3220`, `errors`
steigt zwischen zwei Zeilen nicht.

**Aufschreiben:** die beiden IDs wie gedruckt. Ein INA238 auf dem MATEK
antwortet 0x238x und wird abgewiesen (Flag 0x0004). Ein DAOKAI-Exemplar, das
0x1408 antwortet (die Lesung eines Käufers), wird ebenso abgewiesen (0x0040).

### 8.2 Der Bus am Oszilloskop

Greife SDA und SCL am IOmcu ab.

**Gut:** SCL mit 400kHz (2,5µs je Takt); Anstiegszeit, 30% bis 70%,
höchstens 300ns; eine Lesung von Register 0x01 des INA3221 alle 1,0ms;
die Register 0x07 und 0x05 des INA228 abwechselnd, eines je 1,0ms; kein
NACK in der Dekodierung.

**Aufschreiben:** die Anstiegszeit, den Takt, die Zeit vom STOP einer
Transaktion zum START der nächsten (die eigene Zeit des Controllers, nicht
gemessen), und die Periode der CH1-Lesung.

### 8.3 Messwerte gegen ein Messgerät

Lasse einen gleichmäßigen Strom durch den Shunt des MATEK fließen und
miss ihn sowie die Akkuspannung mit dem Multimeter. Schließe anschließend
ein Servo oder einen Widerstand mit weniger als 1,6A Stromaufnahme an
INA3221 CH1 an, mit dem Multimeter in Reihe.

**Gut:** Spannung und Strom in `bench` und mA und mV von `CH1` (die Spannung
von CH1 liegt auf der Lastseite des Shunts) stimmen mit dem Multimeter
überein, innerhalb seiner Genauigkeit und der Toleranz des Shunts, die für
keines der beiden Module bekannt ist. `temp` nahe Raumtemperatur, in
0,1°C. `ch_flags 0x01`; 0x10 kommt hinzu, wenn CH1 über 1,638A geht, und
der angezeigte Strom von CH1 ist dann nur eine Bereichsgrenze, kein genauer Messwert.

**Aufschreiben:** jedes Zahlenpaar, das des Multimeters und das der Konsole.

### 8.4 Ein Lauf: Spitzen und Zähler

Halte ARM auf MOTOR & ESC, um auf ARMED zu schalten. Halte den Strom 60s
lang gleichmäßig und schalte anschließend auf SAFE.

**Gut:** `bench flags` liest 0x63 (dazu 0x04 und 0x08, wenn ein
bidirektionaler ESC antwortet). `mAh` zählt Strom × Zeit: 2,00A über 60s
sind 33mAh. Ein neues Schalten auf ARMED beginnt Ladung und Energie bei 0. Der
Bildschirm MOTOR & ESC zeigt Spannung und Strom des INA228.

**Aufschreiben:** Strom, Zeit und erreichte Ladung, und die Spitzen, die das
Panel zeigt, gegen Oszilloskop oder Multimeter.

### 8.5 Eine Leitung im Lauf gezogen

Ziehe in ARMED die SDA-Leitung am MATEK für etwa 5s ab und stecke sie
anschließend wieder an.

**Gut:** innerhalb von etwa 6ms verliert `flags` 0x0001 und behält 0x0002;
`bench flags` liest 0x20: der INA228 bleibt die Quelle, seine Felder leer,
nicht die Werte des ESC; `errors` steigt; der Prüfstand bleibt ARMED. Etwa
1s nachdem die Leitung wieder steckt, kommt 0x0001 zurück, und 0x40 bleibt
bis zum nächsten Schalten auf ARMED gelöscht. In SAFE und mit gezogener
Leitung trägt `bench` wieder die Telemetrie des ESC.

### 8.6 Ein hängender Bus

Halte SDA für 2s mit einem Draht auf GND und löse danach die Verbindung. Die Leitungen
sind Open Drain: der Pin wird nie high getrieben, der Kurzschluss ist
ungefährlich.

**Gut:** `flags` bekommt 0x0200, solange gehalten wird; auf SCL zeigt das
Oszilloskop alle 100ms 9 Takte und ein STOP; nach dem Loslassen geht
0x0200 weg, und beide Bauteile sind innerhalb von etwa 1s online.

**Aufschreiben:** die Taktrate der 9 Takte (gedacht sind 100kHz), und wie
lange die Bauteile zum Zurückkommen brauchten.

### 8.7 Speichern während des Lesens

Lasse in SAFE beide Bauteile auslesen und markiere auf OUTPUTS einen Pin.
Die Zuordnung wird gespeichert.

**Gut:** die Konsole druckt `outputs saved, record n, program window N us`
und kein `window refused`; `errors` steigt nicht; `window` zählt weiter;
der Heartbeat fällt nicht aus.

**Aufschreiben:** das Programmierfenster, und das Löschfenster, wenn eines
gedruckt wird: Core 1 wird für jedes im RAM geparkt.

### 8.8 Noch nicht erreichbar

Die Messung der Bewegung auf SERVO_SENSE (der PWM-Frame auf 1µs
gestempelt und die Stellzeiten) löst der Servotest des Panels per ARM aus,
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
| ein PWM-Ausgang (Pulsweitenmodulation) gibt eine andere Pulsbreite aus als im Durchlauf davor, 50ms oder mehr nach der letzten Änderung dieses Ausgangs | 4s ab dem Frame, der den Puls trägt; Triggerzeile `$C` |
| `t` auf der Konsole getippt | 10s; Triggerzeile `$K` |
| die PWM-Flanke des Capture | 4s; Triggerzeile `$E`. Kein veröffentlichtes Panel löst das Capture per ARM aus, mit einem solchen kommt dieser Trigger nicht vor |

Jedes MOVE des automatischen Servotests ist eine geänderte Pulsbreite: ein
Test mit dem veröffentlichten Panel wird ohne Capture aufgezeichnet. Ein
Trigger während eines Trace fügt seine Zeile ein und verschiebt das Ende
auf 4s (bei `t` 10s) nach sich selbst, wenn das später liegt: Bewegungen
mit weniger als 4s Abstand sind ein Trace. Ein Kommando mit Slew ändert
die Pulsbreite in jedem Durchlauf: seine Zeile `$C` trägt die erste
Pulsbreite, und eine Zeile `$D` die, bei der es endete, sobald der Ausgang
50ms still gehalten hat. `cmd=` der Zeile `$D` ist die Zeit der Zeile `$C`
ihres Kommandos, und das Tool beendet dieses Kommando dort, auch wenn der
Ausgang losgelassen wurde und das nächste Kommando innerhalb von 30ms
kam. Ein Kommando, das auf den nächsten Trace wartet,
nimmt seine späteren Änderungen mit: dieser Trace endet 4s nach der
letzten davon und enthält die Zeile `$D`. Ein Ausgang, der losgelassen
wird, beendet dort ein Kommando mit Slew; der nächste Puls auf ihm ist ein
Kommando, wie bald er auch kommt. Die Bewegungen des automatischen Tests
sind Sprünge und haben keine Zeile `$D`. `x` auf der Konsole beendet
einen Trace; eine Triggerzeile, deren Zeit auf dem Stopp oder danach
liegt, entfällt. Jeder Trace enthält auch die Samples von bis zu 64ms vor
seinem Trigger.

**Was es kostet.** Core 1 liest CH1 jede 1ms wie im veröffentlichten
Image und kopiert das Sample in einen Ring aus 4096 Einträgen (49.152
Bytes; 3,9s bei 1000 Samples und 50 Busspannungen je Sekunde). Der Tick
bekommt keine Bus-Transaktion dazu. Eine Änderung der Konfiguration oder
des Zustands des Bauteils ist ein Eintrag im selben Ring und behält so
ihren Platz zwischen den Samples. Core 0 schreibt ganze Zeilen in den
Platz, den der 64-Byte-Sendepuffer der Konsole hat, und nichts, wenn kein
Terminal verbunden ist: seine Schleife wartet nicht auf den Host. Ein
voller Ring verwirft den neu hinzukommenden Eintrag: der Trace hat eine Zeile
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
Ausgangs 50ms stillgestanden hat; sie folgt Samples bis etwa 50ms nach
ihrem `t=`. Maßgeblich für den Zeitpunkt bleibt `t=`.
Core 0 schreibt eine Triggerzeile und die Endzeile, sobald Core 1 einen
Tick beendet hat, der zur Zeit der Zeile oder danach begann. `e=` in der
Endzeile ist der Grund für das Ende: `t` die Zeit, `s` ein geändertes
Set-up, `k` die Konsole. Ein Großbuchstabe (`T`, `S`, `K`) sagt, dass
Core 1 innerhalb von 100ms nicht hinter der Zeit einer Triggerzeile oder
dem Ende gesehen wurde und die Zeile trotzdem geschrieben ist: ein Sample
von vor dieser Zeit kann fehlen oder hinter der Zeile stehen. Das Tool
meldet einen solchen Trace als Problem und spielt keine seiner Bewegungen
nach.

Eine Sample-Zeile ist die Zeit seit dem Sample davor in 0,1ms und der
Shunt-Code, 40µV je Schritt: 0,4mA am 0,1-Ω-Shunt. Eine Sample-Zeile 1ms
nach dem Sample davor hat 8 Bytes bei einem dreistelligen Code und höchstens
10 Bytes bei einem vierstelligen. Die erste Sample-Zeile eines Traces zählt
ab dem Trigger-Zeitpunkt, ein Sample von vor dem Trigger trägt also eine
negative Zeit, und ein Sample nach verworfenen Records eine längere: Diese
Zeilen sind länger, bis zum vollen vorzeichenbehafteten 32-Bit-Bereich der
Zeit. Mit den Spannungszeilen (eine Zeile von 7 Bytes, eine je 20 Samples
in der Ausgabe von `sense_trace_gen`) sind es 8,35 Bytes je Sample,
8,35kB je Sekunde, beim simulierten Servo mit 0,12A, dessen Codes 3 Stellen
haben. Über etwa 0,4A am 0,1-Ω-Shunt haben die Codes 4 Stellen, und die Rate
liegt bei 9,35kB je Sekunde. Jede Zeile endet auf CR LF. Die Rate auf der
Hardware ist nicht gemessen.

**Bereitlegen:** den PD mini (WeAct PD Power Mini V1) bei einer Grenze von
2,00A, das INA3221-Modul mit dem 0,1-Ω-Shunt von CH1 in der Versorgung
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
wird nicht verwendet; jeder Wert funktioniert. Der IOmcu druckt nur, solange
das Terminal DTR (Data Terminal Ready) hält; PuTTY tut das, solange der
Port offen ist. Schalte zusätzliche Zeitstempel im Mitschnitt aus. Ein Präfix vor einer
Trace-Zeile gilt als beschädigtes Format; das Tool spielt dann keine
Bewegung dieses Trace nach. Eine Taste wird beim Drücken gesendet, ohne Enter.

**Teil A: ohne den Encoder.**

1. Baue das Image:

   ```bash
   export PICO_SDK_PATH=/path/to/pico-sdk
   cmake -S firmware/iomcu -B firmware/iomcu/build-trace -DSENSE_TRACE=ON
   cmake --build firmware/iomcu/build-trace
   ```

2. Flashe `firmware/iomcu/build-trace/rcbench-iomcu.uf2`: Halte BOOTSEL,
   stecke das Modul an den PC und kopiere die Datei auf sein Laufwerk.
3. Öffne den Port in PuTTY mit aktiviertem Mitschnitt. Innerhalb von 3s kommt eine
   Zeile, die mit `rcbench-iomcu:` beginnt. Am Panel SETUP → SENSORS:
   `0x40 bestückt` auf ON.
4. Rauschen ohne Servo: Lasse das Servo abgesteckt und schalte den
   Netzteil-Ausgang bei 6,00V ein. Drücke `t`. Nach 10s gibt die Konsole
   eine Zeile aus, die mit `$Z` beginnt. Schließe PuTTY und speichere den
   Mitschnitt als `noise.log`.
5. Servo in Ruhe: Beginne eine neue Mitschnittdatei. Schließe das Servo an,
   schalte auf ARMED und lasse es in der Mitte, bei eingeschaltetem
   Netzteil mit 4,80V. Drücke `t` und warte auf die Zeile `$Z`.
6. Bewegungen im selben Mitschnitt: Öffne auf SERVO die Aufgabe TEST und
   die OPTIONEN der Karte AUTOMATISCHER TEST. Setze TESTDAUER NACH auf
   BEWEGUNGEN, BEWEGUNGEN auf 20 und HALTEZEIT auf 1000ms. Schalte STUFE
   4.8V ein, alle anderen Stufen und BROWN-OUT aus. Tippe TEST STARTEN.
   Der Trace läuft von der ersten Bewegung bis 4s nach der letzten. Warte
   auf seine Zeile `$Z` und schließe dann PuTTY.
7. Wiederhole die Schritte 5 und 6 bei 6,00V mit STUFE 6.0V und anschließend
   beide Messungen für jedes Servo. Verwende eine Mitschnittdatei je Servo
   und Spannung.
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
Samples und dem Band bei 0,02, 0,05 und 0,10A: gesehen oder nicht, und
die Ankunft in ms ab dem Frame. Für den Mitschnitt als Ganzes: die
Einstellungen, die jede Bewegung sehen und ihre Ankunft messen, der Median
der Ankunft je Einstellung und sein Abstand zur Einstellung des Capture
(Filter 4, Band 0,05A), und wie weit die Einstellungen die Ankunft einer
Bewegung auseinanderlegen. Die Ausgabe endet mit `not compared with the
horn`: ohne Encoder ist eine Ankunft der Strom zurück auf seinem
Haltepegel, und das kann der Bewegung des Servohebels vorausgehen oder folgen. Je Trace eine
`<log>-trace-<n>.csv` mit vier Spalten, getrennt durch `;`: Zeit in ms,
Strom in A, die Busspannung in V auf den Zeilen, die eine haben, und
`clipped`. `clipped` ist 1 für ein Sample am oder über dem oberen Ende des
Bereichs, -1 am oder unter dem unteren Ende und 0 für einen Wert. Der
Strom einer geclippten Zeile ist das Ende des Bereichs, eine Grenze und
kein Wert: 1,6380A oder -1,6384A an einem Shunt von 0,1Ω. Eine Datei
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
späteste Kommando ihres Kanals, das höchstens 30ms nach ihrem `t=` liegt:
ein Kommando, das innerhalb von 30ms auf einen losgelassenen Ausgang
folgt, nimmt diese Zeile `$D` als seine eigene. Eine Zeile `$D` in der Form
der anderen Version ist ein Schaden. Eine Zeile endet mit
LF oder CR LF. Alles andere ist ein Schaden. Ebenso eine zweite Zeile
`$H`, eine andere Zeile als `$L` vor der Zeile `$H`, eine andere
Trace-Länge als 4000ms (10000ms bei `t`), eine andere Sample-Periode als
1000µs, ein Sample mit einer Zeit vor dem Sample davor, eine zweite
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
| die 50ms vor der Bewegung | ihr Pegel davor |
| die 200ms vor der Bewegung | ihre Schwelle; der Haltepegel jeder späteren Bewegung zurück zu der Pulsbreite, die diese Bewegung verlassen hat |
| von 50ms vor der Bewegung bis zur nächsten Bewegung oder zum Ende des Trace | die Samples, die das Nachspielen bekommt |

Ein Fenster ohne Sample, mit einer Zeile `$L` darin oder an einem seiner
Enden oder mit einer Zeile `$S` darin oder an einem seiner Enden gibt
keinen Pegel. Jede Bewegung, die diesen Pegel braucht, meldet `not
replayed` und nennt das Fenster. Ein Sample am Anschlag in einem Fenster
zählt als 4094 Schritte zu diesem Ende des Bereichs, wie beim Nachspielen,
und die Zeile der Bewegung sagt, wie viele ihrer Pegel-Samples am Anschlag
liegen.

**Mehr als ein Ausgang.** `ch=` in einer Zeile `$C` oder `$D` ist der
Ausgangskanal, nicht der Pin: die Kanäle zählen ab 0 über die gebundenen
Pins in aufsteigender Pin-Reihenfolge, 1 Kanal je PWM-Pin. Sind GPIO 0, GPIO 1,
GPIO 2 und GPIO 4 gebunden, ist GPIO 2 Kanal 2 und GPIO 4 Kanal 3. Der Shunt führt den
Strom jedes Servos an der Versorgung, und der Bildschirm SERVO kommandiert
jeden Servo-Ausgang im selben Frame. Das Tool spielt kein Kommando nach,
das ein anderes Kommando im Abstand von höchstens 30ms hat; mit 2
gebundenen Servo-Ausgängen spielt es also keine Bewegung nach.
`--loaded-channel N` nennt einen Kanal, an dessen Pin ein Servo
angeschlossen ist; du kannst die Option für jeden weiteren Kanal wiederholen, je 1 Kanal:

```bash
python3 tools/sense_trace.py mg90s-4v8.log --loaded-channel 2
```

Die Zeilen `$C` und `$D` aller anderen Kanäle werden dann weder als
Bewegungen noch bei der Prüfung des Abstands von 30ms berücksichtigt. Eine Zeile `$E` hat keinen Kanal: die
Kommandos der genannten Kanäle nehmen ihre Edge-Zeilen zuerst, und eine
übrige Edge-Zeile im Abstand von höchstens 30ms zum Kommando eines
anderen Kanals wird ausgelassen. Die zweite Zeile der Ausgabe nennt die
Kanäle und zählt, was ausgelassen ist:

```text
loaded channel(s) 2: 25 command line(s) of channel(s) 1, 3 left out
```

Sie endet mit `no command line of channel(s) N` für einen genannten Kanal,
den keine Zeile `$C` des Mitschnitts hat; der Exit-Code bleibt 0. Zwei
genannte Kanäle, die im Abstand von höchstens 30ms kommandiert werden,
werden nicht nachgespielt. Ohne die Option zählt jeder Kanal. Das Tool
prüft nicht, ob am Pin eines genannten Kanals ein Servo hängt: der Strom eines Servos
an einem nicht genannten Kanal bleibt im ausgewerteten Signal enthalten.

`--floor` nimmt 0,000001A bis 1000A, `--pair-ms` mehr als 0ms bis
60000ms, `--csv-offset` bis 1e9 s beiderseits von 0, `--loaded-channel`
eine ganze Zahl von 0 bis 65535 ohne Vorzeichen und ohne führende Null;
jeder andere Wert endet mit 2. Eine CSV-Datei des
Servo-Tests, die nicht gelesen werden kann, eine CSV-Datei, die nicht
geschrieben werden kann, und ein Nachspielen, das eine andere Zahl von
Bewegungen beantwortet als es bekam, enden mit 2.

**Aufschreiben:** die Ausgabe des Tools für jeden Mitschnitt. Teil A
beantwortet: das Rauschen der 1-ms-Samples von CH1 ohne Servo und mit
ruhendem Servo, gegen die Untergrenze der Schwelle (`SERVO_MOVE_MIN_A`,
0,020A); welche Filterlängen und Bänder alle 22 Bewegungen jedes Tests
sehen (2, die das Servo an die Enden stellen, und 20 gezählte); und wie
stark sich die Ankunft mit der Einstellung verschiebt. Ob die erkannte Ankunft mit der
Ankunft des Servohebels übereinstimmt, beantwortet er nicht.

**Teil B: mit dem AS5600.** Dieselben Bewegungen mit dem Encoder auf der
Welle.

1. Schalte bei einem Prüfstand in SAFE unter SETUP → SENSORS `AS5600` ein.
   Stelle das Servo auf Neutral und tippe auf der Seite PRÜFLING von SERVO
   auf SENSOR-MITTE.
2. Wiederhole die Schritte 5 bis 7 von Teil A.
3. Bewahre jeden Mitschnitt zusammen mit der `ATESTnnn.CSV` seines Tests
   aus dem Ordner `USER/LOGS` der SD-Karte auf.
4. Am Host, für jedes Paar:

   ```bash
   python3 tools/sense_trace.py mg90s-4v8.log --servo-csv ATEST012.CSV
   ```

Das Tool druckt dann zusätzlich je Bewegung die Ankunft abzüglich der
`travel angle (ms)` des Encoders und ihren Median je Einstellung. Teil B
beantwortet, was die Filterlänge des Capture (`SENSE_CAP_FILTER_N`, 4) und
das Ankunftsband (`SERVO_MOVE_BAND_A`, 0,05A) sein sollen: beide sind
gewählt, nicht gemessen.

**Nicht bekannt:**

- Teil B: die Stellzeit des Encoders zählt ab dem Kommando, wie das Panel
  es ausgibt, die Ankunft des Trace ab dem PWM-Frame am Pin. Die Differenz,
  die das Tool druckt, enthält die Zeit zwischen beiden: bis zu einem Poll
  und einem Frame, nicht gemessen.
- Die Frame-Zeit eines Kommandos wird aus dem PWM-Zähler berechnet, der
  nach dem Schreiben des Pulses gelesen wird. Ein Frame, der zwischen
  beidem endet, setzt die Zeit dieses einen Kommandos einen Frame (20ms
  bei 50Hz) zu spät. Wie oft: nicht gemessen.
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
- Kommandos im Abstand von höchstens 30ms, etwa zwei Ausgänge in einem
  Frame, ändern den Strom gemeinsam. Das Tool listet sie und spielt
  keines davon nach, außer `--loaded-channel` lässt alle bis auf eines
  aus. Ein Kommando, das später als das auf das Kommando
  eines anderen Ausgangs folgt, bevor dessen Servo angekommen ist, wird
  mit dem Strom des ersten Servos darin nachgespielt; das Tool
  unterscheidet beide nicht.
- Ob ein Terminal 8,35 bis 9,35kB je Sekunde ohne Verlust mitschneidet. Ein Trace,
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
| Schalten auf ARMED verweigert, Draht gesteckt | Heartbeat hat noch keine 4 akzeptierten Abstände, oder eine Lücke überschreitet 150ms. Miss GPIO 3. |
| Arm verweigert, kein offensichtlicher Grund | Ohne Antwort des Touch-Controllers für 500ms ist es gesperrt. Prüfe den Controller und seine Verbindung. |
| Link steht, Bildschirm bietet keine Pins an | Der Katalog der Platine kam nicht an. Die Konsole sagt, welche Page scheiterte. |
| Heartbeat fällt beim ersten Link-Aufbau aus | **Die Übertragung der Zeichnung oder der Flash-Keeper.** Siehe §4. |
| Display friert kurz ein | Prüfe, ob gerade die Platinenzeichnung im Flash gespeichert wird: 4 Vorgänge für die Zeichnung des RP2350-CAN. Das Verhalten der Anzeige dabei ist nicht gemessen. |
| IOmcu bootet nach dem Flashen nicht | Ursache nicht bestimmt. Lade `rcbench-iomcu-install.uf2` erneut ([Schritt 2](#aus-den-release-assets)) und lies die Größenzeile des Builds in §1: ein Image über einem Slot, 2.084.864 Bytes, baut nicht. |
| Panel bootet, aber keine `boardart`-Partition | Nur die App über eine alte Tabelle geflasht. Merge-bin bei Offset 0. |
| Output geht nach 500ms in die Mitte | Kein Fehler: in diesen Kanal hat nichts geschrieben, also ist er in seine Ruhelage gegangen: Mitte beim Servo, null beim Motor. Die Impulse laufen weiter, solange der Prüfstand ARMED ist |
| Impulse hören ganz auf | Nicht der Timeout. Etwas hat den Pin freigegeben, auf SAFE geschaltet oder den Prüfstand gestoppt |
| `sense flags` ohne 0x0100, ein Bauteil freigegeben | Der I2C-Block hat auf diesen Pins nicht geöffnet. Sie müssen SDA und SCL eines Blocks sein: die GPIO-Nummer von SDA mod 4 ist 0 oder 2, SCL die nächste |
| `sense flags` 0x0200, das bleibt | SDA oder SCL low gehalten: ein unversorgtes Modul klemmt den Bus, ein Pull-up fehlt, oder ein Kurzschluss |
| `sense present 0x0000` bei offenem Bus | Nichts antwortet: Pull-ups fehlen, Module unversorgt, oder SDA und SCL vertauscht |

**Scheint ein Fehler vom Panel auszugehen**, prüfe diese drei Abläufe.
Ihr Zeitverhalten auf Hardware ist nicht gemessen:

- das **Ablegen der Zeichnung** in der Control-Task, ein Flash-Schritt pro Durchlauf,
- die **abschnittsweise Übertragung der Zeichnung** in der Poll-Schleife der Control-Task und
- die **Run-Log-Task** (`runlog`), der jeder Schreibzugriff auf die SD-Karte
  (Secure Digital) gehört.

Die ersten beiden sind bei einem IOmcu, der keine Zeichnung meldet, wirkungslos —
das ist der schnellste Weg, sie auszuschließen. Die dritte ist ohne Karte im
Schacht wirkungslos. Mit Karte hängt sie bei jedem Fehler eine Zeile an
`BENCH/RCBENCH.LOG`, ob ARMED oder nicht; die Lauf-Datei wartet auf das Schalten auf
ARMED und auf die erste Zeile danach.

---

## 10. Was aufzuschreiben ist

Notiere für jeden Schritt den Messwert, den erwarteten Wert und das Bild
am Oszilloskop. Die Tabelle „Open items“ in `STATUS.md` enthält die offenen
Punkte zu Treibern auf Hardware, zur Control-Page und zum Flash-Speicher,
die dieser Lauf untersucht.

**Kennzeichne nicht gemessene Werte weiterhin als nicht gemessen.** Trage
keine Schätzung als Messwert in die Tabelle ein.
