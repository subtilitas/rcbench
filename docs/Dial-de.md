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
seinem gebundenen Panel und die USB-Konsole; und die Seite des Panels:
SETUP > WIRELESS > DIAL, Suche und Bindung, der Link mit seiner
Verlustregel, die Drehung auf MOTOR & ESC und SERVO LIVE, die
Bildschirmtaste und der Strom des Panel-Bildschirms zum Dial
([Das Panel](#das-panel)). Die Navigation der Listen des Panels vom Dial
aus, die endgültigen Faces, das BLE-Update und das Pairing folgen in
späteren Schritten.

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

Das Dial ist kein Sicherheitsgerät. Diese Regeln gehören dem Panel, und
das Panel wendet sie an ([Das Panel](#das-panel)):

- Ein Ereignis des Dial schaltet nie Spannung oder Leistung ein. ARM,
  SUPPLY ON, PROG 5V und WRITE bleiben die 2s-Holds des Panels. STOP gibt es
  nur am Panel.
- Der Link ist relativ: Jedes EVENT trägt den Rastungszähler des Dial, nie
  einen Wert. Das erste Paket nach einem Connect, ein Paket mehr als 300ms
  nach dem vorigen und ein Sprung um mehr als 16 Rastungen bewegen nichts.
- Ein Dial, das im Zustand ARMED verloren geht, ist ein Fehler: 1.000ms ohne
  Paket oder der Disconnect schalten den Prüfstand SAFE und alle Ausgänge
  ab. 500ms ohne Paket sind DEGRADED, ein Hinweis. Eine Verbindung, die noch
  kein EVENT gesendet hat, während das Panel den Dienst sucht, ist kein
  Verlust.
- Der Verlust gilt in beide Richtungen. Bleiben die Pakete des Panels aus,
  zeigt das Dial nach 500ms LINK VERLOREN, beendet nach weiteren 500ms den
  Link und sendet nichts mehr, bis es wieder verbunden ist. Das Panel sieht
  dann den Disconnect, und ein Prüfstand im Zustand ARMED geht auf SAFE.
- Die Bildschirmtaste im Zustand ARMED: Ein Tippen setzt das Gas auf MOTOR
  auf 0% und auf SERVO auf CENTER; ein Halten von 1s macht DISARM.
- Kein BLE-Scan im Zustand ARMED: Die Funkzeit eines Scans würde dem Link
  fehlen, der den Knopf trägt.

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
601.824 Bytes, 19,1% eines Slots. Sein statischer RAM: 211.478 von 341.760
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

## Das Panel

SETUP > WIRELESS > DIAL enthält die Zeilen des Dial
([Bildschirme](Screens-de.md#wireless-die-imu-und-das-dial)): Dial EIN oder AUS
(ab Werk AUS), DIAL SUCHEN, GEBUNDENES DIAL und die Schritte einer Rastung,
0.5%, 1%, 2% oder 5% Gas (ab Werk 1%) und 0.5°, 1°, 2° oder 5° Hornwinkel (ab
Werk 1°). Das erste Dial EIN startet Bluetooth LE auf dem ESP32-C6 des
Boards, wie das erste IMU EIN; es läuft bis zum Neustart des Panels. Das
Dial und die IMU sind zwei Clients der Bluetooth-LE-Central des Panels, mit
einem Connect oder Scan zur Zeit zwischen ihnen.

### Suchen und Binden

DIAL SUCHEN sucht 10.000ms und listet bis zu 8 Geräte, die den
dialknob-Dienst senden, das stärkste zuerst, mit Namen und RSSI. Ist der
Prüfstand ARMED oder wartet ein Arm, wird die Suche abgelehnt, und das Band
zeigt `KEINE DIAL-SUCHE BEI ARMED: ERST AUF SAFE SCHALTEN`; bevor Bluetooth LE
läuft, zeigt es `BLUETOOTH NICHT BEREIT: KEINE DIAL-SUCHE`. Ein Tippen auf ein
gefundenes Dial bindet es: Das Panel behält Adresse und Namen in SETUP
(`dial_ble`, `dial_name`) und fragt nach dem Speichern. Es ist immer ein
Dial gebunden oder keins. Das Panel verbindet sich über die Adresse mit ihm,
ohne Suche, bei Dial EIN, beim Start und nach jedem Verlust.

### Der Link

Der Connect fragt nach einem Intervall von 30 bis 50ms, Latenz 0 und einem
Supervision Timeout von 4.000ms, den Werten der IMU. Nach dem Connect sucht
das Panel den Dienst, abonniert EVENT und liest INFO. Ein gebundenes Gerät
ohne den Dienst zeigt `GEBUNDEN: KEIN DIAL` und wird nicht genommen. Ein
Dial, dessen INFO eine andere Protokollversion als 1 nennt, zeigt
`ANDERES PROTOKOLL n`; seine EVENTs werden gezählt und verworfen. Bis INFO
gelesen ist, werden EVENTs ebenfalls verworfen.

Die Verlustregel läuft auf der eigenen Uhr des Panels, nicht auf dem
Supervision Timeout:

| Zeit ohne EVENT | Zustand | Bei SAFE | Bei ARMED oder wartendem Arm |
| --- | --- | --- | --- |
| 0 bis 499ms | `VERBUNDEN` | | |
| 500 bis 999ms | `GESTÖRT` | Band: `DIAL: 500ms KEIN EREIGNIS` | Band: `DIAL: 500ms KEIN EREIGNIS` |
| 1.000ms, oder der Disconnect | `DIAL VERLOREN` | Band: `DIAL VERLOREN` | ein Fehler: SAFE, alle Ausgänge aus, Band: `DIAL BEI ARMED VERLOREN: AUF SAFE GESCHALTET, ALLE AUSGÄNGE AUS` |

Die Uhr startet mit dem ersten EVENT einer Verbindung; eine Verbindung,
die noch keins gesendet hat, während das Panel den Dienst sucht, ist also
kein Verlust. Jeder Verlust ist ein Fehler. Dial AUS in SETUP beendet den
Link ohne Fehler.

### Der Knopf und die Taste

Eine Rastung am Dial nimmt den Weg des eigenen Knopfs des Panels, mit
denselben Regeln: relativ, durch denselben Drehungs-Tracker, nichts bewegt
sich, solange der Prüfstand SAFE ist, und Drehungen bei SAFE werden nicht
für das ARM aufgehoben. Auf MOTOR & ESC verstellt eine Rastung das Gas um
den Gas-Schritt, auf SERVO LIVE das Horn um den Servo-Schritt. Andere
Bildschirme nehmen keine Drehung an.

| Geste | MOTOR & ESC, ARMED | SERVO LIVE, ARMED | SAFE, oder ein anderer Bildschirm |
| --- | --- | --- | --- |
| PRESS: Bildschirmtaste vor 1.000ms losgelassen | Gas 0% | CENTER | nichts |
| HOLD: Bildschirmtaste 1.000ms gedrückt | DISARM | DISARM; eine laufende Folge ARM + START endet mit allen Ausgängen aus, auch vor ARMED | nichts |

Ein PRESS zählt nur in einem Frame, dessen Drehung das Panel nimmt; ein HOLD
immer. Die gehaltenen Zustände von Taste und Touch im EVENT werden nie als
Druck oder Berührung gelesen, eine am Dial gehaltene Taste ist also kein
Halten am Panel: ARM und jedes andere 2s-Halten bleiben beim Panel.

### Der Strom

Das Panel schreibt seinen Bildschirm in Panel-Paketen zum Dial, eines zur
Zeit:

| Paket | Wann |
| --- | --- |
| SCREEN | Face, Titel, Sprache oder Flags geändert, oder 5.000ms nach dem letzten |
| VALUE_META, VALUE | auf MOTOR & ESC und SERVO LIVE, wenn sich Bereich, Schritt oder Wert ändern |
| SCALE | auf MOTOR & ESC und SERVO LIVE: beim Betreten und bei jedem Connect, dann wenn sich ein Vollausschlag ändert, höchstens alle 200ms |
| LIVE_MOTOR, LIVE_SERVO | auf MOTOR & ESC und SERVO LIVE, alle 50ms (20Hz) |
| STATE | ARMED, DEGRADED oder ein Schritt geändert, oder 200ms ohne Paket |

Jeder Connect sendet das ganze Modell erneut, SCREEN zuerst. Die Faces sind
MOTOR, SERVO und HOME (das Menü); jeder andere Bildschirm sendet seinen
Titel und den Zustand des Prüfstands. LIVE_MOTOR trägt das Gas und die
Messwerte, die der Prüfstand meldet: Spannung, Strom, Leistung, Drehzahl,
die Temperatur von ESC und Motor. LIVE_SERVO trägt den Puls, den
befohlenen Winkel, den gemessenen Winkel, wenn die Winkelquelle einen
meldet, und Spannung und Strom der Servoschiene aus dem Kanal des Servos.

SCALE gibt den Ringen des Dial ihre Vollausschläge; 0 ist unbekannt, und
dieser Ring zeigt nur seine Spur:

| Vollausschlag | Quelle |
| --- | --- |
| Drehzahl | die Spitze des Laufs; vor der ersten Spitze der RPM-Vollausschlag des MOTOR-Plots |
| Spannung | Spannung max von SUPPLY, solange der Ausgang von SUPPLY an ist; sonst der VOLT-Vollausschlag des MOTOR-Plots |
| Strom | Überstrom von SUPPLY, solange der Ausgang von SUPPLY an ist und die Auslösung gesetzt ist; sonst der CURR-Vollausschlag des MOTOR-Plots |
| Servoweg, Pulsenden | WEG, PULS MIN und PULS MAX des Servoprofils |
| Spannung, Strom der Servoschiene | Spannung max und Überstrom von SUPPLY, solange der Ausgang von SUPPLY an ist; sonst 0 |

### Die Konsole

Das Log des Panels nennt das Dial mit dem Tag `dial`:

```text
I (…) dial: Dial ON: starting Bluetooth LE
I (…) dial: scan for 10000ms
W (…) dial: scan refused: the bench is ARMED (1 refused)
I (…) dial: bound A0:B1:C2:7F:3A:10 R DIAL-3A10
I (…) dial: link 6 -> 10, error 0 code 0x0, protocol 1
I (…) dial: 50 events in 1000ms, 12 detents, rtt 41ms, repeats 0 skipped 0 stale 0 glitches 0, refused 0 unread 0, 20 writes, 0 events dropped
E (…) dial: Dial lost while ARMED: an error, the bench to SAFE (1)
```

Die Link-Zeile nennt die Zustände als Zahl: 0 AUS, 1 STARTET, 2 KEIN
BLUETOOTH, 3 C6 AUSGEFALLEN, 4 KEIN DIAL GEBUNDEN, 5 SUCHE, 6 VERBINDE,
7 KEIN DIAL, 8 WARTET, 9 ANDERES PROTOKOLL, 10 VERBUNDEN, 11 GESTÖRT,
12 VERLOREN. Die Zählerzeile kommt einmal pro Sekunde, solange der Link
steht: EVENTs und Rastungen der Sekunde, die Umlaufzeit aus dem Echo von
STATE, die wiederholten, übersprungenen, veralteten und gesprungenen
Pakete des Drehungs-Trackers, EVENTs, die wegen ihrer Form abgelehnt und
vor INFO verworfen wurden, geschriebene Panel-Pakete und Ereignisse, die
die Bluetooth-LE-Queue verworfen hat.

## Das Face

Das Face dieser Firmware ist ein Platzhalter: ein schlichter Ring, der Titel
des Panel-Bildschirms und der Wert, auf den der Knopf wirkt, als Text (der
Wert einer Einstellung mit Einheit, das Gas in %, der Puls des Servos in us,
die fokussierte Listenzeile), dazu SAFE oder ARMED. Ohne gebundenes Panel
zeigt es `NICHT GEBUNDEN`, seinen Namen und `AM PANEL BINDEN`. 500ms ohne
Paket vom Panel zeigt es `LINK VERLOREN`, `DER PRÜFSTAND IST SAFE`,
`ALLE AUSGÄNGE AUS` und `ARM AM PANEL`: Das Dial beendet den Link 500ms
später, und dann ist der Prüfstand SAFE, entweder weil er es schon war oder
weil das Panel ihn beim Verlust auf SAFE schaltet. Dazwischen, bis zu 500ms
lang, erreicht eine Drehung noch ein Panel, dessen eigener Strom stockt.
Verbunden ohne Paket vom Panel oder nicht verbunden zeigt es
`WARTE AUF DAS PANEL` und `DREHEN BEWEGT NICHTS`. Seine eigenen Worte folgen
der Sprache des Panels, Englisch oder Deutsch.

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

- Das Dial navigiert noch keine Liste am Panel: Kein Bildschirm füllt NAV.
  Das ist der nächste Schritt.
- Das ARMED-Flag des Panels für die Verlustregel und die Ablehnung der Suche
  gehört dem Control-Task und wird vom Bluetooth-Task gelesen; es kann einen
  Control-Durchlauf, etwa 5ms, alt sein.
- LIVE_SERVO trägt keinen Messwert der Position oder des Stroms eines
  Feedback-Servos.
- Eine Servoschiene, die PROG 5V speist, hat in SCALE keinen Vollausschlag:
  0, ihre Ringe zeigen nur ihre Spur.
- Eine IMU-Suche (SETUP > IMU oder IMU SUCHEN auf SERVO) wird bei ARMED nicht
  abgelehnt. Ihre Wirkung auf die Pakete eines verbundenen Dial ist nicht
  gemessen.
- Das Panel hat keinen Konsolenbefehl, der eine Drehung oder einen Druck des
  Dial einspeist; das macht der Test-Build des Dial von seiner Seite aus.
- Die Spannung des Akkus ist unbekannt: Keine Quelle nennt den Pin, den sie
  erreicht. STATUS trägt 0 und das Flag "Akku unbekannt". Der Zustand des
  Laders ist ebenfalls unbekannt.
- Welche Drehrichtung des Encoders hochzählt, ist nicht gemessen, ebenso
  wenig, wo in seinen 4 Zählschritten der Encoder einrastet. Im Uhrzeigersinn
  positiv ist die Regel des Protokolls. Eine Rastung wechselt 3 Zählschritte
  von ihrer Mitte, also klickt ein Encoder, der zwischen zwei Zählschritten
  ruht, nie.
- Die Startzeit des Touch-Controllers nach dem gemeinsamen Reset ist mit
  300ms angenommen, der Wert der FT5x06-Familie; an diesem Teil nicht
  gemessen.
- Ob GPIO46 eine Rolle spielt, solange USB das Dial versorgt, ist nicht
  bestätigt.
- Ein Panic oder ein Software-Reset im Akkubetrieb gibt GPIO46 für den Reset
  wahrscheinlich frei und schaltet das Dial ab; nicht bestätigt.
- `forget` oder das Halten von 10s mit dem gebundenen Panel in Reichweite:
  Das Panel kennt die Adresse des Dial noch, verbindet sich wieder und
  abonniert, und das Dial bindet es erneut. Um ein anderes Panel zu binden,
  zuerst das Dial am alten Panel lösen.
- Das Face ist ein Platzhalter; Zeichen außerhalb von ASCII und der
  deutschen Buchstaben erscheinen als `?`.
- Das Senden eines ganzen Frames dauert bei 40MHz etwa 23ms (gerechnet,
  nicht gemessen).
- Nichts in dieser Firmware oder in der Seite des Panels ist mit einem Dial
  gelaufen: Das Dial ist bestellt, nicht geliefert. Die Latenz von der
  Drehung zum Ausgang über Funk ist nicht gemessen; die Station misst sie
  mit dem Test-Build, sobald das Dial an ihrem USB hängt.
- Noch kein Schlaf, keine Stufen des Hintergrundlichts und keine
  Abschaltung: Das Dial bleibt an, bis RST gedrückt wird oder die Zelle leer
  ist. Die Laufzeit mit einer Zelle ist nicht gemessen.
