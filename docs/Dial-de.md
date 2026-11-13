# Das M5Stack Dial

<sub>[English](Dial.md) · **Deutsch**</sub>

Das M5Stack Dial v1.1 ist ein runder Drehknopf mit einem Display von
240 x 240 Pixeln. In rcbench ist es ein kabelloser Knopf für das Panel über
Bluetooth LE (Low Energy, BLE). Sein Display zeigt, worauf der Knopf wirkt.
Diese Seite beschreibt seine Firmware (`firmware/dial`) und das Protokoll
zwischen ihm und dem Panel (`protocols/dialknob`).

Was es gibt: das Protokollmodul mit seinen Host-Tests und die Firmware des
Dial: die Selbsthaltung, den Encoder, die Tasten, das Touch-Glas, den Klick
pro Rastung, das Display mit einem Platzhalter-Face, das BLE-Peripheral mit
seinem gebundenen Panel und die USB-Konsole. Was es noch nicht gibt: die
Seite des Panels. Kein Panel-Build verbindet sich mit einem Dial, ein Dial
bewegt also nichts am Prüfstand. Die endgültigen Faces, das BLE-Update und
das Pairing folgen in späteren Schritten.

M5Stack, Dial und Stamp-S3A sind Namen ihres Inhabers und seiner Produkte,
Bluetooth ist eine Marke ihres Inhabers. rcbench ist unabhängig: Es ist
nicht mit ihnen verbunden, nicht von ihnen empfohlen und nicht von ihnen
gefördert. Die Namen sagen, mit welchem Gerät das Panel spricht.
[Rechtliche Hinweise und Haftungsausschluss](../README-de.md#rechtliche-hinweise-und-haftungsausschluss)
enthält den Hinweis des Projekts.

## Das Gerät

| Teil | Was es ist | Pins |
| --- | --- | --- |
| Modul | Stamp-S3A, das Modul des Dial: ESP32-S3FN8, 8MB Flash im Gehäuse, kein PSRAM (Pseudo-Static RAM) | |
| Display | GC9A01, 1,28 Zoll, 240 x 240, SPI (Serial Peripheral Interface) mit 40MHz | DC GPIO4, MOSI GPIO5, SCLK GPIO6, CS GPIO7, Reset GPIO8, Hintergrundlicht GPIO9 |
| Touch | FT3267 an I2C-Adresse 0x38, 400kHz, alle 20ms abgefragt | SDA GPIO11, SCL GPIO12, Interrupt GPIO14 (nicht benutzt) |
| Encoder | 16 Rastungen pro Umdrehung, 4 Quadratur-Zählschritte pro Rastung, am PCNT (dem Pulszähler) | A GPIO41, B GPIO40 |
| Bildschirmtaste | unter dem Glas, alle 5ms abgefragt, 20ms entprellt | GPIO42 |
| Stamp-Taste | die Taste des Moduls; beim Reset gehalten startet sie den ROM-Download-Modus | GPIO0 |
| Summer | ein Klick von 3ms mit 4.000Hz pro Rastung | GPIO3 |
| Selbsthaltung | high hält das Dial an, nachdem WAKE losgelassen ist | GPIO46 |
| Testmarke | 1ms high pro eingespeister Rastung, nur im Test-Build | GPIO1 (PORT.B G1) |
| Akku | eine LiPo-Zelle mit 1.000mAh oder weniger an der MX1.25-Buchse, auf der Platine geladen | der Messpin ist unbekannt |

Die Zelle bleibt bei 1.000mAh oder weniger, bis der Ladestrom gemessen ist:
Der Lader (TP4057 im Text des Schaltplans) und sein Strom sind nicht
bestätigt.

## Sicherheit

Das Dial ist kein Sicherheitsgerät. Diese Regeln gehören dem Panel; die
Seite des Panels, die sie anwendet, folgt in einem späteren Schritt:

- Ein Ereignis des Dial schaltet nie Spannung oder Leistung ein. ARM,
  SUPPLY ON, PROG 5V und WRITE bleiben die 2s-Holds des Panels. STOP gibt es
  nur am Panel.
- Der Link ist relativ: Jedes EVENT trägt den Rastungszähler des Dial, nie
  einen Wert. Das erste Paket nach einem Connect, ein Paket mehr als 300ms
  nach dem vorigen und ein Sprung um mehr als 16 Rastungen bewegen nichts.
- Ein Dial, das im Zustand ARMED verloren geht, ist ein Fehler: 1.000ms ohne
  Paket oder der Disconnect schalten den Prüfstand SAFE und alle Ausgänge
  ab. 500ms ohne Paket sind DEGRADED, ein Hinweis.
- Die Bildschirmtaste im Zustand ARMED: Ein Tippen setzt das Gas auf MOTOR
  auf 0% und auf SERVO auf CENTER; ein Halten von 1s macht DISARM.

Die Regeln des Links und ihre Zeiten stehen im Protokollmodul, jede mit
einem Host-Test
([protocols/dialknob](https://github.com/subtilitas/rcbench/blob/main/protocols/dialknob/README.md)).

## Flashen über USB

Das Release enthält `rcbench-dial-merged-<version>.bin` für Offset 0x0 und
`rcbench-dial-<version>.bin`, die Anwendung allein, mit ihren
SHA-256-Summen in `SHA256SUMS`.

1. Das Image prüfen: `sha256sum -c SHA256SUMS --ignore-missing` im Ordner
   mit dem Image und `SHA256SUMS`.
2. Die USB-C-Buchse des Dial mit dem Rechner verbinden. Ein Dial, auf dem
   diese Firmware läuft, setzt esptool über den USB-Serial/JTAG-Port in den
   ROM-Download-Modus. Eines, auf dem sie nicht läuft: die Stamp-Taste
   (GPIO0) beim Einstecken des Kabels halten.
3. Flashen:

   ```bash
   python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
     write-flash 0x0 rcbench-dial-merged-0.23.12.bin
   ```

   esptool 5 schreibt den Befehl `write-flash`, esptool 4 `write_flash`.
4. Das Dial startet neu und zeigt `NICHT GEBUNDEN` und seinen Namen, `DIAL-`
   und die letzten 4 Hex-Ziffern seiner Adresse.

Das zusammengeführte Image schreibt die Partitionstabelle, also wird der
NVS (Non-Volatile Storage) bei 0x9000 neu geschrieben, und das gebundene
Panel ist vergessen.

Aus den Quellen, mit ESP-IDF (Espressif Internet-of-Things Development
Framework) v6.1:

```bash
cd firmware/dial
idf.py -B build -D SDKCONFIG=build/sdkconfig build
idf.py -B build -p /dev/ttyACM0 flash monitor
# der Test-Build mit den Hooks der Konsole
idf.py -B build-hooks -D SDKCONFIG=build-hooks/sdkconfig -D RCBENCH_DIAL_HOOKS=ON build
```

Die Partitionstabelle (`firmware/dial/partitions.csv`) enthält den NVS, zwei
Anwendungsslots von 3.072kB und ihre otadata. Das veröffentlichte Image hat
600.960 Bytes, 19,1% eines Slots. Sein statischer RAM: 211.478 von 341.760
Bytes internem DRAM und IRAM, darunter der Frame von 115.200 Bytes; den
Heap, den NimBLE zur Laufzeit nimmt, hat niemand gemessen.

## Binden

Das Dial sendet im Advertising die UUID (Universally Unique Identifier) des
dialknob-Dienstes und seinen Namen in der Scan Response. Ungebunden sendet
es alle 100ms, und jedes Central darf sich verbinden. Das erste Central, das
EVENT abonniert, ist sein gebundenes Panel: Das Dial behält die Adresse des
Panels im NVS, nimmt ab dann über die Accept List des BLE-Controllers nur
noch eine Verbindung von dieser Adresse an und sendet alle 500ms. Ein Scan
listet ein gebundenes Dial weiterhin.

Ein Halten der Bildschirmtaste von 10s vergisst das gebundene Panel, ebenso
`forget` auf der Konsole. Das Dial piept, beendet einen Link und sendet
wieder für jeden. Die HOLD-Geste nach 1s geht dabei an das Panel.

Protokollversion 1 hat kein Pairing und keine Verschlüsselung: Ein Gerät in
Reichweite, das sich zuerst verbindet oder die gebundene Adresse fälscht,
wird angenommen.

## Das Face

Das Face dieser Firmware ist ein Platzhalter: ein schlichter Ring, der Titel
des Panel-Bildschirms und der Wert, auf den der Knopf wirkt, als Text (der
Wert einer Einstellung mit Einheit, das Gas in %, der Puls des Servos in us,
die fokussierte Listenzeile), dazu SAFE oder ARMED. Ohne gebundenes Panel
zeigt es `NICHT GEBUNDEN`, seinen Namen und `AM PANEL BINDEN`. 500ms ohne
Paket vom Panel zeigt es `LINK VERLOREN` und `DREHEN BEWEGT NICHTS`. Seine
eigenen Worte folgen der Sprache des Panels, Englisch oder Deutsch.

## Die Konsole

Die Konsole ist der USB-Serial/JTAG-Port der USB-C-Buchse, mit dem Prompt
`dial>` und dem Log auf demselben Port.

| Befehl | Was er tut |
| --- | --- |
| `status` | Name, Adresse, Firmware- und Protokollversion, das gebundene Panel, der Link und sein RSSI (Received Signal Strength Indication), die EVENT-Zähler, die Klick-Einstellung, der Akkuwert |
| `sound on` / `sound off` | der Klick pro Rastung; das SCREEN-Paket des Panels setzt ihn ebenfalls, Standard ON |
| `forget` | das gebundene Panel vergessen, wie das Halten von 10s |
| `help` | die Befehle |

Der Test-Build (`RCBENCH_DIAL_HOOKS=ON`) fügt die Hooks hinzu. Jeder speist
ein, als hätte die Hardware es getan. Das veröffentlichte Image enthält
keinen Code von ihnen; CI schlägt fehl, wenn doch.

| Befehl | Was er tut |
| --- | --- |
| `turn <n>` | n Rastungen, -1.000 bis 1.000, eine alle 20ms, jede in einem eigenen EVENT; GPIO1 (PORT.B G1) geht für 1ms high, wenn dieses EVENT an den BLE-Stack übergeben ist. Ohne Link geht kein EVENT, und GPIO1 bleibt low |
| `press` | eine PRESS-Geste |
| `hold` | eine HOLD-Geste |
| `tap <x> <y>` | ein TAP auf das Glas bei x, y, 0 bis 239 |
| `batt <mV>` | der Akkuwert, den STATUS trägt, 2.000 bis 5.000mV; 0 setzt ihn auf unbekannt zurück |
| `drop` | den Link beenden; das Dial sendet wieder Advertising |
| `ping` | sofort ein EVENT, mit dem Echo der Uhr des letzten STATE |

Die Dokumentation des Dial und M5Unified nennen die zwei Pins von PORT.B in
verschiedener Reihenfolge; GPIO1 ist in beiden Fällen der Pin dieser Nummer.

## Grenzen

- Noch kein Panel-Build verbindet sich mit einem Dial: Die Seite des Panels
  folgt in einem späteren Schritt.
- Die Spannung des Akkus ist unbekannt: Keine Quelle nennt den Pin, den sie
  erreicht. STATUS trägt 0 und das Flag "Akku unbekannt". Der Zustand des
  Laders ist ebenfalls unbekannt.
- Welche Drehrichtung des Encoders hochzählt, ist nicht gemessen, ebenso
  wenig, wo in seinen 4 Zählschritten der Encoder einrastet. Im Uhrzeigersinn
  positiv ist die Regel des Protokolls.
- Die Startzeit des Touch-Controllers nach dem gemeinsamen Reset ist mit
  300ms angenommen, der Wert der FT5x06-Familie; an diesem Teil nicht
  gemessen.
- Ob GPIO46 eine Rolle spielt, solange USB das Dial versorgt, ist nicht
  bestätigt.
- Das Face ist ein Platzhalter; Zeichen außerhalb von ASCII und der
  deutschen Buchstaben erscheinen als `?`.
- Das Senden eines ganzen Frames dauert bei 40MHz etwa 23ms (gerechnet,
  nicht gemessen).
- Nichts in dieser Firmware ist auf dem Gerät gelaufen: Das Dial ist
  bestellt, nicht geliefert. Die Station misst die Latenz von der Drehung
  zum Ausgang mit dem Test-Build, sobald das Dial an ihrem USB hängt.
- Noch kein Schlaf, keine Stufen des Hintergrundlichts und keine
  Abschaltung: Das Dial bleibt an, bis RST gedrückt wird oder die Zelle leer
  ist. Die Laufzeit mit einer Zelle ist nicht gemessen.
