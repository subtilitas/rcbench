# Das M5Stack Dial

<sub>[English](Dial.md) · **Deutsch**</sub>

Das M5Stack Dial v1.1 ist ein runder Drehknopf mit einem Display von
240 x 240 Pixeln. In rcbench ist es ein kabelloser Knopf für das Panel über
Bluetooth LE (Low Energy, BLE). Sein Display zeigt, worauf der Knopf wirkt.
Diese Seite beschreibt seine Firmware (`firmware/dial`) und das Protokoll
zwischen ihm und dem Panel (`protocols/dialknob`).

Was es gibt: das Protokollmodul mit seinen Host-Tests und die Firmware des
Dial: die Selbsthaltung, den Encoder, die Tasten, das Touch-Glas, den Klick
pro Rastung, die Faces auf dem Display, das BLE-Peripheral mit seinem
gebundenen Panel und die USB-Konsole; und die Seite des Panels: SETUP >
WIRELESS > DIAL, Suche und Bindung, der Link mit seiner Verlustregel, die
Drehung auf MOTOR & ESC und SERVO LIVE, die Bildschirmtaste, die Navigation
der Menüs und Listen des Panels mit SET eines Werts und der Strom des
Panel-Bildschirms zum Dial ([Das Panel](#das-panel)); und das
Firmware-Update über BLE von SETUP > UPDATES > EINZELNE UPDATES ([Update über Bluetooth
LE](#update-über-bluetooth-le)). Die Bildschirme der Programmer (KST,
Hitec, ESC) und das Pairing folgen in späteren Schritten.

Dieselbe Firmware baut für ein zweites Gerät, den Guition-JC3636K518C-Knopf:
einen ESP32-S3R8 mit rundem Display von 360 x 360 Pixeln, Touch, einem
Encoder ohne Drucktaster und einem Vibrationsmotor. Sein Build
([Flashen über USB](#flashen-über-usb)) startet das BLE-Peripheral, die
Bindung, den Face-Task, die Konsole und das Display. Das Display, ein
ST77916 über QSPI (Quad-SPI) mit 40MHz, zeigt seine eigenen Faces,
gezeichnet mit 360 x 360: jedes Face des M5 aus derselben Szene, MOTOR &
ESC und SERVO mit sechs Messwerten, sechs Ringen, Titel, Pille und
Hinweisen, die anderen Faces mit ihren Textblöcken, den Randstrichen einer
Liste und den Randdekorationen. Ein Textblock steht auf der Zeile des
M5-Face, um die Mitte mit 3/2 genommen, eine Schriftgröße größer, jeder
String innerhalb von Radius 150. Das Start-Face mit dem BLE-Namen und der
Firmware-Version steht bis zum ersten Face. Eine Änderung eines Face
zeichnet und sendet den ganzen Frame von 259.200 Bytes.

Der Encoder des K518 hat zwei Pulsleitungen, keine Quadratur: GPIO8 pulst
je Rastung im Uhrzeigersinn einmal low, GPIO7 je Rastung gegen den
Uhrzeigersinn. Je eine PCNT-Einheit (Pulszähler) zählt die fallenden
Flanken einer Leitung; der Input-Task liest beide alle 5ms, und eine
Leitung, die nach einem 5ms-Schritt ohne Puls pulst, ist eine Rastung.
Zwei Rastungen einer Leitung mit weniger als 10ms Abstand zählen als eine.
Der K518 hat keine Taste: Ein ruhig auf dem Glas liegender Finger ist die
Bildschirmtaste. Vor 1.000ms abgehoben ohne Bewegung über 20px ist das
PRESS; 1.000ms gehalten HOLD; 5s und 10s sind die Warnung zum Ausschalten
und das Vergessen, wie beim M5. Eine Bewegung über 20px vor 1.000ms sendet
nichts. Ein Abheben innerhalb von 500ms ist zusätzlich ein TAP an seinem
Startpunkt, wie ihn das Glas des M5 sendet. Der Touch-Controller ist ein
CST816S auf 0x15 (0x2A, wenn 0x15 nicht antwortet), alle 20ms gelesen;
EVENT trägt seinen Punkt von 0-359 auf 0-239 skaliert. Die Update-Frage
lautet TIPPEN: ANNEHMEN, die Ausschalt-Wahl TIPPEN: WÄHLEN. Das Update
nimmt nur die ruhige Berührung: Ein ruhiges Abheben vor 1.000ms nimmt es
an, eine ruhige Berührung von 1.000ms lehnt es ab, und ein Wischen tut
keins von beiden. Nur eine Berührung, die beginnt, während die Frage
steht, beantwortet sie; eine Berührung von vorher, von vor einem
verlorenen Link oder zu einer Frage, die eine andere ersetzt hat,
beantwortet nichts, bis sie abhebt. Eine Berührung zählt nur, wenn ein
Lesen des Glases, alle 20ms, keinen Finger fand, während die Frage dieses
Angebots schon ganz auf dem Display stand, vom Face-Task gesendet (alle
50ms); ein Tippen in den ersten 20ms der Frage auf dem Glas braucht also
ein zweites Tippen. Jede Frage hat ihre eigene Nummer,
und das Update nimmt die Taste nur für die Frage, in der sie entstand. Statt eines
Summers vibriert ein DRV2605 auf 0x5A mit seinen ROM-Effekten für einen
LRA (Bibliothek 6): eine Rastung "Sharp Tick 3" (26), PRESS "Sharp Click
100%" (4), HOLD und ein angenommenes Update "Strong Click 100%" (1), die
5s-Warnung "Buzz 1 100%" (47), das Vergessen zweimal "Double Click 100%"
(10). SOUND aus stoppt nur das Ticken der Rastung. Antwortet kein DRV2605
auf 0x5A, geht GPIO38 auf high (der Enable der V1.0-Boards) und es folgt
ein zweiter Versuch; ohne Antwort dann vibriert der K518 nicht. Die Zahl
der Rastungen je Umdrehung (24 laut Guitions Demo-Code), die Ausrichtung
der Touch-Achsen und die Stärke der Vibration sind noch nicht auf dem
Gerät gelaufen. 

Der K518 misst seine Versorgung an GPIO1 (ADC1 Kanal 0, 12dB, kalibriert)
über einen 10k/10k-Teiler einmal pro Sekunde: 16 Messungen, die
niedrigste und die höchste verworfen, der Rest gemittelt, mal 2. Der
Teiler liegt an der Schiene hinter dem Power-Pfad, also misst er mit USB
VBUS weniger eine Diode, nicht den Akku. Ein Wert über 4.450mV oder ein
USB-Host am Konsolen-Port ist USB: STATUS setzt sein USB-Flag und nennt den
Akku unbekannt. Ein Wert unter 2.500mV ist kein plausibler Akku und
ebenfalls unbekannt. Sonst trägt STATUS die Akkuspannung. LOW (STATUS
Bit4) setzt nach 3 Messungen in Folge unter 3.500mV und endet nach 3 ab
3.600mV; EMPTY (Bit5) ebenso bei 3.350mV und 3.450mV. EMPTY zeigt 3s lang
AKKU LEER, dann schaltet sich der K518 selbst aus. Der Akkuanteil der
Faces ist linear von 3.300mV (0%) bis 4.200mV (100%). Kein Ladestatus-Pin
ist bekannt, also setzt STATUS nie CHARGING.

Der Schiebeschalter trennt die Versorgung. AUS in der Ausschalt-Wahl und
ein leerer Akku schicken den K518 in den Deep Sleep: der ST77916 aus und
im Schlaf (DISPOFF, dann SLPIN und 120ms), der Pin des Hintergrundlichts
auf low gehalten, Touch-Controller und DRV2605 in ihrem eigenen Schlaf.
Ein Dreh am Knopf (eine Encoder-Leitung low) weckt ihn zu einem neuen
Start, ebenso der Schiebeschalter. Der Strom im Deep Sleep ist nicht
gemessen; der Teiler allein zieht etwa 190µA aus einem Akku mit 3,8V. Die
Genauigkeit des ADC gegen ein Messgerät ist noch nicht auf dem Gerät
gelaufen. Sein INFO nennt Hardware 2 (`DK_HW_K518`); sein Image nennt die
Update-Art 5 (`BLEOTA_KIND_DIAL_K518`), also nimmt keines der beiden Geräte
das Image des anderen.

![Das Start-Face des K518](img/de/dial-k518-boot.png)
![MOTOR & ESC auf dem K518, ARMED](img/de/dial-k518-motor-armed.png)
![MOTOR & ESC auf dem K518, SAFE](img/de/dial-k518-motor-safe.png)
![SERVO LIVE auf dem K518, ARMED](img/de/dial-k518-servo-live.png)
![eine Liste auf dem K518](img/de/dial-k518-navigation.png)
![ein Wert beim Setzen auf dem K518](img/de/dial-k518-value-set.png)
![LINK LOST auf dem K518](img/de/dial-k518-link-lost.png)
![der beendete Link auf dem K518](img/de/dial-k518-link-ended.png)
![warten auf das Panel auf dem K518](img/de/dial-k518-waiting.png)
![kein gebundenes Panel auf dem K518](img/de/dial-k518-bind.png)
![ein Bildschirm ohne Wirkung des Knopfs auf dem K518](img/de/dial-k518-other.png)
![das angebotene Update auf dem K518](img/de/dial-k518-update-ask.png)
![das ankommende Update auf dem K518](img/de/dial-k518-update-send.png)
![die Warnung vor dem Ausschalten auf dem K518](img/de/dial-k518-off-warn.png)
![die Wahl beim Ausschalten auf dem K518](img/de/dial-k518-off-ask.png)
![ein leerer Akku auf dem K518](img/de/dial-k518-batt-empty.png)

M5Stack, Dial und Stamp-S3A sind Namen ihres Inhabers und seiner Produkte,
Guition und JC3636K518C sind Namen ihres Inhabers und seines Produkts,
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
  zeigt das Dial nach 500ms VERBINDUNG VERLOREN und sendet ab dann kein EVENT
  mehr. Das Panel sieht LOST 1.000ms nach dem letzten EVENT, und ein
  Prüfstand im Zustand ARMED geht auf SAFE. Nach weiteren 500ms beendet das
  Dial den Link und sendet nichts mehr, bis es wieder verbunden ist und das
  erste Paket des Panels kam.
- Die Bildschirmtaste im Zustand ARMED: Ein Tippen setzt das Gas auf MOTOR
  auf 0% und auf SERVO auf CENTER; ein Halten von 1s macht DISARM.
- Ein Halten der Bildschirmtaste von 1s beendet einen Servotest, einen
  LIMIT RUN und einen Lauf von RUDDER AUTO in jeder Phase, auch solange der
  Lauf den Prüfstand für seine Programmiersitzung auf SAFE hält, und nimmt ein
  ARM zurück, das unterwegs ist.
- Kein BLE-Scan im Zustand ARMED: Die Funkzeit eines Scans würde dem Link
  fehlen, der den Knopf trägt. Die Regel gilt für den Scan des Dial, der
  Knoten und der IMU. Ein Scan, der beim Schalten auf ARMED läuft, wird
  beendet.
- Navigation nur bei SAFE: Ein SELECT ist ein Tippen von einem Frame, es
  vollendet also kein 2s-Halten, und bei ARMED bewegt das Dial keinen Fokus
  weg von dem Ausgang, den es treibt
  ([Navigation und SET](#navigation-und-set)).

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
     write-flash 0x0 rcbench-dial-merged-0.23.18.bin
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
# das Image des Guition JC3636K518C
idf.py -B build-k518 -D SDKCONFIG=build-k518/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.k518" build
```

Die Platine ist eine Auswahl in `firmware/dial/main/Kconfig.projbuild`;
`sdkconfig.k518` wählt den K518 mit seinen 16MB Flash und 8MB Octal-PSRAM.
CI nennt die Images des K518 `rcbench-dial-k518-<version>.bin` und
`rcbench-dial-k518-merged-<version>.bin`; ein Release enthält sie nicht.

Die Partitionstabelle (`firmware/dial/partitions.csv`) enthält den NVS, zwei
Anwendungsslots von 3.072kB und ihre otadata. Das Image hat
850.864 Bytes, 27,0% eines Slots; die acht Fonts der Faces sind 250.716
Bytes davon und die Ring-Tabellen 25.044. Sein statischer RAM: 216.550 von
341.760 Bytes internem DRAM und IRAM, darunter der Frame von 115.200 Bytes.
Den Heap, den NimBLE zur Laufzeit nimmt, hat niemand gemessen; das Log beim
Start und `status` nennen den freien internen Heap und seinen Tiefstwert
seit dem Start.

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
wieder für jeden. Die HOLD-Geste nach 1s geht dabei an das Panel, und ab 5s
zeigt das Face die Warnung vor dem Ausschalten ([Ausschalten](#ausschalten)).

## Ausschalten

Die Bildschirmtaste ist die WAKE-Taste des Dials: Nur an der Zelle schaltet
ein Druck das Dial ein, und die Firmware hält die Versorgung mit GPIO46 auf
high. Solange die Taste gedrückt ist, hat das Dial Versorgung; sie fällt
erst ab, wenn die Taste oben ist.

![Die Warnung vor dem Ausschalten, ihre rote Hälfte](img/de/dial-off-warn.png)
![Die Wahl beim Ausschalten](img/de/dial-off-ask.png)

| Die Bildschirmtaste | Was folgt |
| --- | --- |
| vor 1.000ms losgelassen | PRESS an das Panel |
| 1.000ms gehalten | HOLD an das Panel, einmal, wie bei jedem Halten |
| 5.000ms gehalten | ein Piepton mit 600Hz für 150ms; das Face blinkt rot, 500ms rot und 500ms dunkel: `JETZT LOSLASSEN: AUSSCHALTEN` und `WEITER HALTEN: PANEL VERGESSEN`, rot mit sechs roten Ringen am Rand, dann weiß über der matten Spur der Ringe |
| ab 5.000ms und vor 10.000ms losgelassen | die Wahl: `AN LASSEN` über `AUS`, der Fokus auf `AN LASSEN` |
| 10.000ms gehalten | das gebundene Panel ist vergessen ([Binden](#binden)), mit einem Piepton mit 1.000Hz für 200ms; die Warnung endet, keine Wahl folgt, das Dial bleibt an |

In der Wahl setzt eine Rastung im Uhrzeigersinn den Fokus auf `AUS`, eine
gegen den Uhrzeigersinn zurück auf `AN LASSEN`. Ein Druck der
Bildschirmtaste nimmt die Antwort im Fokus. Ein Halten von 1.000ms nimmt
`AN LASSEN`, ebenso 10s (10.000ms) ohne Drehung und ohne Druck der
Bildschirmtaste; die zweite Hinweiszeile zählt diese Sekunden herunter.
`AUS` setzt GPIO46 auf low, und an der Zelle fällt die Versorgung ab. An USB
bleibt das Dial nach `AUS` an und zeigt wieder seine Faces. `AN LASSEN`
kehrt zu den Faces zurück.

Solange die Warnung oder die Wahl steht, klickt eine Drehung, lässt aber
den Zähler im EVENT unverändert, und kein PRESS und kein HOLD geht an das
Panel. Ein Druck, der in der Wahl beginnt, gehört der Wahl, bis die Taste
oben ist, auch über 5s und 10s hinaus. RST schaltet das Dial an der Zelle
ebenfalls aus. Geht das Dial mit einem Link aus, nimmt das Panel das als
verlorenen Link.

Protokollversion 1 hat kein Pairing und keine Verschlüsselung: Ein Gerät in
Reichweite, das sich zuerst verbindet oder die gebundene Adresse fälscht,
wird angenommen.

## Update über Bluetooth LE

Das Panel sendet das Anwendungs-Image des Dial über BLE. Das Panel-Image
bringt es nicht mit: Das Halten des Schritts DIAL lädt
`rcbench-dial-<Version>.bin` aus dem Release, das das Panel ausführt, oder
liest es aus `BENCH/UPDATES` der SD-Karte, und prüft es vorher
([Bildschirme: Geräte-Images](Screens-de.md#geräte-images)).
Das Protokoll ist `protocols/bleota`
([README](https://github.com/subtilitas/rcbench/blob/main/protocols/bleota/README.md));
die Seite des Panels ist die Zeile DIAL-FIRMWARE auf der Seite, die der
Eintrag FUNKGERÄTE auf EINZELNE UPDATES öffnet; EINZELNE UPDATES öffnet
über die Zeile Einzelne Updates in den OPTIONEN von UPDATES
([Bildschirme, Einzelne Updates](Screens-de.md#einzelne-updates),
[Bildschirme, Das Dial](Screens-de.md#das-dial)). Auf UPDATES selbst ist
das Dial Schritt 4: Läuft auf einem verbundenen Dial eine andere Version
und sind die Schritte davor erledigt, trägt seine Karte dasselbe Halten
([Bildschirme, Die Schritte](Screens-de.md#die-schritte)).

1. Das Dial ist gebunden und verbunden, der Prüfstand ist SAFE. Die Zeile
   zeigt `Dial: 0.23.17. Release: 0.23.18.` und `2s HALTEN ZUM DIAL-UPDATE`.
2. Das Halten von 2s beendet den Knopf-Link des Dial. Das Panel verbindet
   sich mit dem Update-Dienst des Dial und sendet BEGIN: Art DIAL, Größe,
   Version und den SHA-256 des Images.
3. Das Dial prüft die Art, die Größe gegen seinen Slot von 3.145.728 Bytes
   und die Version. Dann zeigt es 30s lang `UPDATE`, `v0.23.17 AUF
   v0.23.18`, `DRÜCKEN: ANNEHMEN`, die verbleibenden Sekunden und
   `HALTEN: ABLEHNEN`. Vor der Antwort wird nichts gelöscht.
   - Ein Druck der Bildschirmtaste, innerhalb von 1s losgelassen, nimmt an.
   - Ein Halten von 1s lehnt ab.
   - Kein Druck in 30s lehnt ab.
4. Angenommen: Das Dial nimmt die ersten 296 Bytes des Images und prüft,
   dass sie den ESP32-S3 und die Rolle des Dial nennen (`DEVOTA_ROLE`,
   hinter dem Anwendungsdeskriptor von ESP-IDF). Das Image eines anderen
   Geräts wird abgelehnt, bevor etwas geschrieben ist. Dann löscht und
   schreibt das Dial den Slot, aus dem es nicht läuft, und zeigt den
   Fortschritt als Ring mit `DIAL AN LASSEN`.

![Die Frage des Updates am Dial](img/de/dial-update-ask.png)
![Der Fortschritt des Updates am Dial](img/de/dial-update-send.png)

5. Das Panel sendet END. Das Dial vergleicht den SHA-256 jedes
   geschriebenen Bytes mit dem aus BEGIN, lässt ESP-IDF das Image prüfen,
   setzt den Boot-Slot, zeigt `STARTET NEU` und startet 500ms später neu.
6. Das neue Image startet auf Probe. Es markiert sich bei seiner ersten
   Verbindung als gültig, die es nur mit laufendem BLE-Stack annimmt. Der
   Knopf-Link des Panels verbindet sich wieder aus den Werten von SETUP.

Eine Ablehnung, ein verlorener Link, 10s ohne Schreiben oder ein
abweichender SHA-256 beenden die Übertragung: Das Dial schließt den Slot
und läuft mit seinem Image weiter. Eine neue Übertragung beginnt bei Byte
0. Die Ausnahme ist die Installation nach END: Der Boot-Slot kann schon
gesetzt sein, wenn der Link abbricht oder das ABORT des Panels kommt. Das
Dial beendet die Installation dann und startet in das neue Image, wenn es
die Prüfung von ESP-IDF bestanden hat, ohne Link, um es dem Panel zu
sagen. Das Panel liest die Version des Dial, sobald es zurück ist, bis zu
60s lang, und meldet, was es findet
([Bildschirme, Das Dial](Screens-de.md#das-dial)).

Ein neues Image, das vor seiner ersten Verbindung neu startet, auch durch
einen Stromausfall, startet wieder das Image davor
(`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). Probe und Rollback gehören dem
Bootloader: Ein Dial, das über USB mit einem Image vor dieser
Update-Firmware geflasht wurde, hat einen Bootloader ohne beides. Es nimmt
Updates über Funk an, aber ohne Probe und ohne Rollback, bis es einmal über
USB mit einem Release geflasht wird, das das Update trägt.

Das Update braucht kein USB und kein Werkzeug. Der Knopf steuert das Panel
vom Halten an nicht, bis sich der Link wieder verbindet; diese Lücke ist
kein Verlustfehler, denn der Prüfstand ist SAFE, und ARM wird abgelehnt,
solange das Update läuft. Das Dial hält seinen Link, solange eine
Übertragung läuft: Das Trennen nach 500ms ohne Paket des Panels wartet.

Sicherheit: kein Pairing, keine Verschlüsselung und keine Signatur. Die
Unversehrtheit stützt sich auf den SHA-256 in BEGIN. Jedes Central in
Reichweite kann BEGIN senden; der Druck am Dial nimmt es an
([SECURITY.md](../SECURITY.md)).

Die Geräteseite läuft im Task `ota`, Priorität 3, alle 10ms, mit einem
Stack von 8.192 Bytes aus dem Heap beim Start, der Größe der OTA-Beispiele
von ESP-IDF; seine Tiefe ist nicht gemessen. Ihr statischer RAM sind
3.487 Bytes: der Ring des Fensters von 2.048 Bytes, ein Schreibpuffer von
1.024 Bytes und der Zustand.

## Das Panel

SETUP > WIRELESS > DIAL enthält die Zeilen des Dial
([Bildschirme](Screens-de.md#wireless-die-imu-und-das-dial)): Dial EIN oder AUS
(ab Werk AUS), DIAL SUCHEN, GEBUNDENES DIAL, DIAL-MODELL, DIAL-AKKU und die
Schritte einer Rastung, 0.5%, 1%, 2% oder 5% Gas (ab Werk 1%) und 0.5°, 1°,
2° oder 5° Hornwinkel (ab Werk 1°). DIAL-MODELL nennt das Board, das das
INFO des verbundenen Dials nennt, `M5STACK DIAL` (Hardware 1) oder
`GUITION K518` (Hardware 2). DIAL-AKKU liest den STATUS des Dials, nach
INFO und alle 5.000ms, solange es verbunden ist: die Spannung der Zelle,
davor `SCHWACH` (Bit4) oder `LEER` (Bit5), `USB`, solange USB es versorgt,
`NICHT GEMESSEN` bei einem Dial ohne Messung (dem M5Stack Dial). Ein
STATUS-Lesen wartet auf das PARAM-Lesen der Verbindung und hält die Pakete
des Panels an das Dial für seine Laufzeit an; ein fehlgeschlagenes Lesen
behält den letzten STATUS. Ein Dial eines älteren Releases sendet nur die
Flags Bit0 bis Bit3, die wie bisher dekodiert werden. Das Update bietet
jedem Dial das Image seines Boards an: `rcbench-dial-<Version>.bin` dem
M5Stack Dial und einem Dial, dessen INFO noch aussteht,
`rcbench-dial-k518-<Version>.bin` dem K518, und das Panel prüft den Kopf
des Images gegen das Board, bevor das erste Byte geht. Die Annahme des K518
auf UPDATES lautet `AM DIAL TIPPEN ZUM ANNEHMEN`. Das erste Dial EIN startet Bluetooth LE auf dem ESP32-C6 des
Boards, wie das erste IMU EIN; es läuft bis zum Neustart des Panels. Das
Dial, die IMU und die Knoten sind Clients der Bluetooth-LE-Central des
Panels, mit einem Connect oder Scan zur Zeit zwischen ihnen.

### Suchen und Binden

DIAL SUCHEN öffnet SEARCH ([Bildschirme](Screens-de.md#wireless-search))
und sucht 10.000ms nach bis zu 8 Geräten, die den dialknob-Dienst senden,
das stärkste zuerst, mit Namen und RSSI; SEARCH zählt die verbleibenden
Sekunden. Ist der Prüfstand ARMED oder wartet ein Arm, wird die Suche
abgelehnt, und das Band zeigt
`KEINE DIAL-SUCHE BEI ARMED: ERST AUF SAFE SCHALTEN`; bevor Bluetooth LE
läuft, zeigt es `BLUETOOTH NICHT BEREIT: KEINE DIAL-SUCHE`. SEARCH öffnet
in jedem Fall, mit der letzten Liste. Eine bei SAFE gestartete Suche endet
beim Schalten auf ARMED, und SEARCH und DIAL SUCHEN zeigen
`DURCH ARM BEENDET`. GEBUNDENES DIAL öffnet SEARCH mit der Liste der
letzten Suche, ohne Suche, oder mit dem Grund, warum es keins listet:
`NOCH KEINE SUCHE: AKTUALISIEREN ANTIPPEN`, `KEINS GEFUNDEN`.
AKTUALISIEREN auf SEARCH startet eine neue Suche mit denselben
Ablehnungen. Ein Tippen auf ein Dial fragt `DIAL BINDEN?`; BINDEN bindet
es: Das Panel behält Adresse und Namen in SETUP (`dial_ble`, `dial_name`)
und fragt nach dem Speichern, und SEARCH bleibt offen und hebt das Dial
hervor, sobald es gebunden ist. Es ist immer ein Dial gebunden oder keins.
Das Panel verbindet sich über die Adresse mit ihm, ohne Suche, bei Dial
EIN, beim Start und nach jedem Verlust.

BINDEN während einer Suche, und ein Tippen auf ein Dial dann, zeigt
`SUCHE LÄUFT: DAS DIAL NACH DEM ENDE WÄHLEN`; BINDEN, nachdem sich die
Liste unter der Frage änderte, zeigt
`DIAL NICHT GEBUNDEN: LISTE GEÄNDERT, ERNEUT WÄHLEN`. Beides bindet nichts.
Eine Bindung, die der Controller ablehnt, weil zuerst eine Suche startete
oder sich die Liste darunter änderte, zeigt dieselben Texte, und die
Konsole schreibt `dial: bind of n refused`. Ein gebundenes Dial schreibt
`dial: bound <Adresse> <Name>`.
Ein Tippen auf DIAL SUCHEN oder AKTUALISIEREN, während eine Suche läuft,
startet keine; das Band zeigt `SUCHE LÄUFT: SIE ENDET SPÄTESTENS NACH 10s`.

BINDUNG LÖSEN auf SEARCH, 2s gehalten, vergisst das gebundene Dial: Die
Verbindung endet, das Panel behält eine leere Adresse und einen leeren
Namen in SETUP und fordert Speichern an, und es verbindet sich bis zur
nächsten Bindung mit keinem Dial. Die Konsole schreibt
`dial: unbound <Adresse>`. Ein Dial, das beim Lösen UP oder DEGRADED ist,
ist sofort verloren, und bei ARMED ist dieser Verlust ein Fehler: SAFE,
jeder Ausgang aus.

Der Connect nach dem Binden startet sofort, wenn die Central frei ist. Eine
IMU oder ein Knoten, EIN und an ein Gerät außer Reichweite gebunden, hält
den einen Connect 5.000ms pro Versuch. Der Connect nach dem Binden nimmt
den nächsten Zug des Hubs
([bleuart: der Hub](../protocols/bleuart/README.md#the-hub)): Ein
laufender Versuch eines anderen Clients läuft zu Ende, und der Connect des
Dial folgt innerhalb von 20ms, vor den Wiederholungen der anderen. Im
Host-Modell wartet der erste Connect des Dial nach der Wahl bis zu
5.061ms, das Tippen von 80ms und ein Frame eingerechnet, bei einem oder
zwei solchen Clients, und er startet im selben Durchlauf wie das BIND,
wenn kein Versuch läuft; auf dem Panel sind diese Wartezeiten nicht
gemessen. Ein Connect nach einem Verlust, bei Dial EIN oder beim Start
wartet in der Warteschlange des Hubs, wer zuerst kommt, zuerst: Er folgt
nach höchstens einem Versuch jedes anderen Clients, im Host-Modell
höchstens 10.046ms neben zwei solchen Clients und 20.092ms neben vier.

### Der Link

Der Connect fragt nach einem Intervall von 30 bis 50ms, Latenz 0 und einem
Supervision Timeout von 4.000ms, den Werten der IMU. Nach dem Connect sucht
das Panel den Dienst, abonniert EVENT und liest INFO. Ein gebundenes Gerät
ohne den Dienst zeigt `GEBUNDEN: KEIN DIAL` und wird nicht genommen. Ein
Dial, dessen INFO eine andere Protokollversion als 1 nennt, zeigt
`ANDERES PROTOKOLL n`; seine EVENTs werden gezählt und verworfen. Bis INFO
gelesen ist, werden EVENTs ebenfalls verworfen. Ein INFO, das sich nicht
lesen oder dekodieren lässt, ist ein fehlgeschlagener GATT-Schritt: Der
Link endet, und das Panel verbindet sich nach 1.000ms erneut. Rastungen und
PRESS erreichen den Bildschirm nur, solange der Link `VERBUNDEN` oder
`GESTÖRT` ist; ein HOLD, der nur auf SAFE schaltet, in jedem Zustand.

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
den Gas-Schritt, auf SERVO LIVE das Horn um den Servo-Schritt. Die
Bildschirme aus [Navigation und SET](#navigation-und-set) nehmen die Drehung
als Schritt des Fokus; alle anderen nehmen keine an.

| Geste | MOTOR & ESC, ARMED | SERVO LIVE, ARMED | SAFE, oder ein anderer Bildschirm |
| --- | --- | --- | --- |
| PRESS: Bildschirmtaste vor 1.000ms losgelassen | Gas 0% | CENTER | nichts |
| HOLD: Bildschirmtaste 1.000ms gedrückt | DISARM | DISARM; eine laufende Folge ARM + START endet mit allen Ausgängen aus, auch vor ARMED | nichts; BACK auf einem Bildschirm, den das Dial navigiert |

Auf MOTOR & ESC schaltet ein HOLD auch DISARM, solange ein ARM unterwegs
ist, angefragt und noch nicht bestätigt, und beendet einen ESC-STICK-Lauf
wie ABORT, auch in ARMING: Nach einem HOLD schaltet der Prüfstand nie auf ARMED.
Ein PRESS zählt nur in einem Frame mit einer Drehung oder einem Druck, den
das Panel nimmt; ein HOLD immer. Ein Frame ohne Eingabe lässt eine offene
Hilfeblase offen. Die gehaltenen Zustände von Taste und Touch im EVENT werden nie als
Druck oder Berührung gelesen, eine am Dial gehaltene Taste ist also kein
Halten am Panel: ARM und jedes andere 2s-Halten bleiben beim Panel.

### Navigation und SET

Solange das Dial verbunden und der Prüfstand SAFE ist, navigiert es diese
Bildschirme:

| Bildschirm | Einträge, in Drehrichtung |
| --- | --- |
| das Menü | seine 8 Kacheln, Zeile für Zeile |
| die Kacheln von SETUP | die Kacheln, dann SAVE, solange Speichern angeboten ist |
| eine Liste von SETUP | ihre Zeilen, dann RESET und SAVE; auf SENSORS und WIRELESS zuerst die Einträge links; keine, solange die Tastatur einer Zeile offen ist |
| SERVO | die 8 Kacheln, dann SETTINGS; die offene SETTINGS-Liste wie eine Liste von SETUP |
| OUTPUTS, Platinenansicht | das Feld der Protokollliste, CLEAR ALL, solange angeboten, dann je eine Pin-Taste |
| SEARCH | die gefundenen Geräte, dann AKTUALISIEREN; bei offener Bindungsfrage ABBRECHEN und BINDEN, der Fokus auf ABBRECHEN |
| die Warnung beim Verlassen | STAY, DISCARD, SAVE |

| Geste | Navigation | SET |
| --- | --- | --- |
| Drehen | der Fokus geht eine Rastung einen Eintrag weiter und läuft an beiden Enden um; eine Zeile außer Sicht rollt herein | der Wert geht eine Rastung einen Schritt, innerhalb seines Bereichs |
| PRESS | SELECT: ein Tippen auf den Eintrag im Fokus | KEEP |
| HOLD | BACK: was das Home-Tag tut, die Bindungsfrage oder das Ziffernfeld schließt | CANCEL: der Wert wie vorher |

Das Panel zeichnet einen 2px-Rahmen in der Akzentfarbe um den Eintrag im
Fokus. Der Rahmen hebt die Pixel darunter je Framebuffer auf und setzt sie
zurück, wenn er weiterzieht: Eine Rastung zeichnet die zwei Rahmen neu,
nie den Bildschirm. Kein Rahmen erscheint und das Dial nimmt nicht am
Bildschirm teil, solange das Dial OFF oder nicht verbunden ist, der
Prüfstand ARMED ist, die Bildschirmhilfe oder das Screenshot-Popup offen
ist oder die Texttastatur offen ist.

Ein SELECT ist ein Tippen: ein DOWN und ein UP in der Mitte des Eintrags im
selben Frame. Die Zeile im Fokus rollt zuerst ins Bild, auch wenn ein Finger
die Liste seitdem gerollt hat; ein Punkt neben dem Eintrag oder außer Sicht
wird nie getippt, und der Druck tut dann nichts. Ein 2s-Halten am Panel braucht den Kontakt 2s lang über
Frames, also wird vom Dial aus kein Halten vollendet: ARM, Spannung EIN,
WRITE n VALUES und das Löschen bleiben beim Panel. Ein SELECT auf eine
Halte-Taste wie CLEAR ALL auf OUTPUTS zeigt den Hinweis, den das Panel bei
jedem Tippen zeigt. Auf den Kacheln von SERVO beendet ein HOLD zuerst eine
laufende Folge ARM + START, wie auf jedem SERVO-Bildschirm; BACK folgt nur,
wo es nichts gestoppt hat.

SET:

- eine Zahl, die das Ziffernfeld nimmt: SELECT tippt ihren Wert, und das
  Ziffernfeld öffnet sich. Die Drehung schreibt den Wert in die Eingabe, um
  den Schritt der Einstellung und innerhalb ihres Bereichs; eine Drehung an
  einem Ende bleibt dort stehen. Eine Einstellung mit einer AUS-Lücke (etwa
  ein INA3221-Shunt, AUS oder 5.0 bis 1000.0mΩ) nimmt eine Rastung von AUS
  zu ihrem kleinsten Wert und eine zurück, wie viele Rastungen ein Frame
  auch bringt. PRESS ist das OK des Ziffernfelds, mit
  seinen Prüfungen; HOLD ist CANCEL.
- ein Ein/Aus oder eine Liste von Optionen: SELECT startet SET auf der
  Zeile. Jede Rastung tippt ihre Taste `-` oder `+`, höchstens 16 je Frame;
  Ein/Aus und Optionslisten laufen um wie ihre Tasten; das VALUE-Face zeigt
  die Stelle einer Optionsliste von 0 bis zu ihrer letzten Option. PRESS behält den
  Wert; HOLD tippt die andere Taste so oft, wie ein Tippen den Wert geändert
  hat.

Dimmen nach: Eine Drehung, ein PRESS oder ein HOLD des Dial weckt ein
gedimmtes Panel und startet seine Minuten neu, wie eine Berührung. Auf
einem gedimmten Panel mit ruhendem Prüfstand tut die Eingabe, die es
geweckt hat, sonst nichts: Sie öffnet, bewegt und stellt nichts ein. Auf
einem Prüfstand, der nicht ruht, wirkt sie sofort; ein PRESS für 0% oder
CENTER auf einem ARMED-Prüfstand wird also nie verschluckt, und ein HOLD
auf MOTOR & ESC oder einem SERVO-Bildschirm wirkt immer.

Das Dial zeigt das NAV-Face, solange es den Fokus bewegt, mit dem Namen des
Bildschirms, dem Eintrag im Fokus zwischen den Einträgen davor und danach
und seiner Stelle in der Liste. In SET zeigt es das VALUE-Face mit dem
Namen der Einstellung als Titel. Auf dem Menü bleibt das Face HOME und
zeichnet die Kacheln als Liste.

### Der Strom

Das Panel schreibt seinen Bildschirm in Panel-Paketen zum Dial, eines zur
Zeit:

| Paket | Wann |
| --- | --- |
| SCREEN | Face, Titel, Sprache oder Flags geändert, oder 5.000ms nach dem letzten |
| VALUE_META, VALUE | auf MOTOR & ESC und SERVO LIVE und in SET, wenn sich Bereich, Schritt oder Wert ändern |
| NAV | solange das Dial navigiert: wenn sich der Eintrag im Fokus, der davor oder danach oder die Liste ändert; eine Zeile je Paket |
| SCALE | auf MOTOR & ESC und SERVO LIVE: beim Betreten und bei jedem Connect, dann wenn sich ein Vollausschlag ändert, höchstens alle 200ms |
| LIVE_MOTOR, LIVE_SERVO | auf MOTOR & ESC und SERVO LIVE, alle 50ms (20Hz) |
| STATE | ARMED, DEGRADED oder ein Schritt geändert, 200ms ohne Paket, oder 1.000ms nach dem letzten STATE |

Jeder Connect sendet das ganze Modell erneut, SCREEN zuerst. Die Faces sind
MOTOR, SERVO, HOME (das Menü), NAV und VALUE
([Navigation und SET](#navigation-und-set)); jeder andere Bildschirm sendet
seinen Titel und den Zustand des Prüfstands. SCREEN trägt das Flag
NAVIGATES, solange das Dial navigiert. LIVE_MOTOR trägt das Gas und die
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
steht: EVENTs und Rastungen der Sekunde, die Umlaufzeit von einem STATE
bis zum ersten EVENT, das es zurückgibt (die des Links, plus bis zu 200ms
bis zum nächsten EVENT des Dial), die wiederholten, übersprungenen, veralteten und gesprungenen
Pakete des Drehungs-Trackers, EVENTs, die wegen ihrer Form abgelehnt und
vor INFO verworfen wurden, geschriebene Panel-Pakete und Ereignisse, die
die Bluetooth-LE-Queue verworfen hat.

## Die Faces

Das Display zeigt, worauf der Knopf wirkt. `shared/dial` zeichnet jedes Face
aus den Paketen des Panels (SCREEN, NAV, VALUE_META, VALUE, LIVE_MOTOR,
LIVE_SERVO, STATE und SCALE) und aus dem eigenen Zustand des Dial: gebunden
oder nicht, der Link, sein RSSI und seine Versorgung. Es zeichnet mit dem
Rasterizer des Panels (`shared/gfx`), den Fonts des Panels und der dunklen
Palette aus `shared/ui/ui_theme.c` in einen RGB565-Frame von 240 x 240
Pixeln. Die Bilder auf dieser Seite sind das Rendering dieses Codes auf
einem Rechner, aus den festen Modellen in `test/host/dial_models.h`;
`tools/render_dial.py --check` hält sie in der CI beim aktuellen Rendering.

Jedes Face hat dieselbe Ausstattung:

| Element | Wo | Was |
| --- | --- | --- |
| Titel-Tag | mittlere Zeile 42, 20px hoch | der Name des Panel-Bildschirms im Font 8x16 auf dem Akzent, im Font 6x12, wenn 8x16 Radius 101 verlassen würde (12 bis 14 Zellen); gedimmt, solange der Link verloren ist; ein Titel, der auch in 6x12 nicht passt, wird gekürzt |
| Zustandspille | mittlere Zeile 66, 19px hoch | `SAFE`, oder `ARMED` mit der Laufuhr (`0:42`, ab 1h `1h05`) auf MOTOR & ESC; im Font 10x20, im Font 8x16, wenn die Symbole daneben Radius 101 verlassen würden |
| Link-Balken | 6px links der Pille | 4 Balken, 3px breit: 4 ab -60dBm, 3 ab -70dBm, 2 ab -80dBm, darunter 1; gelb, solange das Panel den Link als DEGRADED markiert; keine ohne Verbindung |
| Akku | 6px rechts der Pille | 18 x 9px, gefüllt von 3.300 bis 4.200mV; ein Strich, solange der Messwert unbekannt ist; `USB` an seiner Stelle, solange der Messwert unbekannt ist und ein USB-Host den Port liest, wo die Zeile Platz hat |
| Hinweise | mittlere Zeilen 183 und 197, ab Zeile 176, unter den unteren Zellen | was Drücken und Halten hier tun, im Font 8x16 |

Eine Zeile steht im größten Font ihrer Kette, in dem ihre Tinte auf ihrer
Höhe innerhalb von Radius 100 um die Mitte des Displays liegt, umbrochen auf
die Zeilen, die ihr Block erlaubt. Eine Zeile, die in keinen Font passt,
verliert ihren letzten Teil, Teile stehen zwei Leerzeichen auseinander, bis
der Rest passt; ein Teil mit einer Ziffer fällt ganz weg, so erscheint nie
eine Zahl zum Teil. `test_dial_fit` hält jeden String jedes Face, auf
Englisch und auf Deutsch und mit jedem Feld an seinen Enden, innerhalb von
Radius 100, innerhalb seiner Zelle, innerhalb der Zeilen, die ein
Neuzeichnen für ihn löscht, außerhalb des Löschrechtecks jeder fremden
Zelle und abseits jedes anderen Strings.

### Die Messwerte: MOTOR & ESC und SERVO

![MOTOR & ESC, ARMED](img/de/dial-motor-armed.png)
![MOTOR & ESC, SAFE](img/de/dial-motor-safe.png)
![SERVO, ARMED](img/de/dial-servo-live.png)

Sechs Messwerte in drei Reihen von zwei gleichen Zellen von 78 x 31px, die
Spalten bei x 76 und 164, die Reihen bei y 100, 133 und 166, jede Zelle
innerhalb von Radius 100. Eine Zelle enthält den Namen des Messwerts im Font
8x16, gedimmt, darunter den Wert im Font 10x20 mit seiner Einheit im Font
8x16, beide in der Farbe des Messwerts. Ein Messwert ohne sein Gültig-Bit
zeigt `--`. Solange der Prüfstand SAFE ist, stehen die Werte in halber
Stärke, und die erste Hinweiszeile lautet `ARM AM PANEL` in Gelb.

| Face | Messwerte, in Reihenfolge | Hinweise bei ARMED |
| --- | --- | --- |
| MOTOR & ESC | Gas, Drehzahl, Spannung, Strom, Leistung, ESC-Temperatur | `DRÜCKEN: 0%`, `HALTEN: DISARM` |
| SERVO | Soll-Winkel, gemessener Winkel, Puls, Spannung, Strom und Leistung der Schiene | `DRÜCKEN: MITTE`, `HALTEN: DISARM` |

Um die Zellen läuft je Messwert ein Ring, in der Reihenfolge des Rasters,
der erste außen: Radius 118 nach innen im Abstand von 3px bis 103, 3px
außerhalb des Kreises der Zellen. Ein Ring ist 1px breit und
kantengeglättet: Das Pixel, dessen Mitte im Abstand d von der Mitte des
Displays liegt, nimmt die Farbe des Rings mit der Deckung
clamp(1 - |d - r|, 0, 1). Der Bogen läuft von 12 Uhr im Uhrzeigersinn über
den Anteil des Messwerts an seinem Endwert; der Rest des Kreises ist die
Spur, die Farbe des Messwerts zu 22% auf den Hintergrund gemischt. Die
Endwerte kommen aus dem SCALE-Paket des Panels:

| Messwert | Farbe | Endwert |
| --- | --- | --- |
| Gas | ACCENT | 100% |
| Drehzahl | RPM | SCALE `rpm`: die Spitze des Laufs, sonst der Endwert des Plots |
| Spannung | VOLT | SCALE `mv` |
| Strom | CURR | SCALE `ca`, nach Betrag |
| Leistung | POWER | SCALE `mv` x `ca` |
| ESC-Temperatur | LILAC | 120°C |
| Soll-Winkel | ACCENT | -`travel` bis +`travel`: die Mitte ist ein halber Ring |
| gemessener Winkel | TEAL | -`travel` bis +`travel` |
| Puls | LILAC | `us_min` bis `us_max` |
| Spannung, Strom, Leistung der Schiene | VOLT, CURR, POWER | SCALE `rail_mv`, `rail_ca`, ihr Produkt |

Ein Endwert von 0 oder kein SCALE seit dem Verbinden lässt seinen Ring bei
der Spur; ebenso ein Messwert ohne sein Gültig-Bit.

### Eine Liste und ein Wert

![Eine Liste unter SETUP](img/de/dial-navigation.png)
![Ein Wert wird eingestellt](img/de/dial-value-set.png)

Eine Liste (NAV) zeigt die Zeile vor der fokussierten, die fokussierte Zeile
im Font 20x36 zwischen den Eckklammern des Akzents, die Zeile danach und
`4 / 9`. Bis zu 24 Zeilen erscheinen als Striche am Rand über 270°, der
fokussierte im Akzent; eine längere Liste zeigt ihre Stelle als Ring bei
Radius 112. Hinweise: `DRÜCKEN: ÖFFNEN`, `HALTEN: ZURÜCK`.

Ein Wert (VALUE_META, VALUE) zeigt die Zahl im Ziffern-Font 32x44 mit ihrer
Einheit, den Bereich (`0 BIS 60`), den Schritt und den Wert beim Eintritt
(`SCHRITT 1  VORHER 5`) und einen Ring bei Radius 118 vom Minimum zum
Maximum. Ein Bereich, der zu breit für seine Zeile ist, fällt weg; eine zu
breite Schrittzeile verliert zuerst `VORHER`. Zahl und Bereich werden
gelb, wenn das Panel die letzte Drehung an einer Grenze abgelehnt hat.
Hinweise im SET-Modus: `DRÜCKEN: BEHALTEN`, `HALTEN: ZURÜCK`; davor:
`DRÜCKEN: ÄNDERN`, `HALTEN: ZURÜCK`.

### Der Link und das Binden

![VERBINDUNG VERLOREN, verbunden](img/de/dial-link-lost.png)
![VERBINDUNG VERLOREN, der Link ist beendet](img/de/dial-link-ended.png)
![Warten auf das Panel](img/de/dial-waiting.png)
![Ohne Panel](img/de/dial-bind.png)
![Ein Bildschirm, auf dem der Knopf nichts tut](img/de/dial-other.png)

500ms ohne Paket vom Panel, nachdem auf dieser Verbindung eines kam, sendet
das Dial kein EVENT und kein Keepalive mehr. Die Uhr des Panels zählt ab dem
letzten EVENT: Das Panel sieht DEGRADED 500ms und LOST 1.000ms nach dem
letzten EVENT und schaltet einen Prüfstand im Zustand ARMED bei LOST auf
SAFE. Ab diesen 500ms zeigt das Face `VERBINDUNG VERLOREN`,
`KEINE DREHUNG GESENDET`, `PRÜFSTAND WIRD SAFE` und `ARM AM PANEL` in
einem gestrichelten roten Rand, der Titel gedimmt. Das Dial beendet den
Link 500ms später; dann zeigt das Face `VERBINDUNG VERLOREN`,
`DER PRÜFSTAND IST SAFE`, `ALLE AUSGÄNGE AUS` und `ARM AM PANEL`. Verbunden
ohne Paket vom Panel oder gebunden und nicht verbunden zeigt es
`WARTE AUF DAS PANEL` und `DREHEN BEWEGT NICHTS`, mit dem Namen des Dial auf
dem gedimmten Titel-Tag; bis zum ersten Paket des Panels geht kein EVENT.
Pakete ohne SCREEN zeigen `WARTE AUF DAS PANEL` allein, mit der Pille aus
STATE, wenn STATE kam.
Ohne gebundenes Panel zeigt es `OHNE PANEL`, seinen Namen und seine Adresse,
wo es am Panel gebunden wird (`AM PANEL`, `SETUP > WIRELESS`,
`SCAN, DIAL WÄHLEN`) und `BEREIT ZUM SCAN`. Auf einem Bildschirm, auf dem
der Knopf nichts tut, und auf HOME ohne Listenzeilen zeigt es den Titel,
SAFE oder ARMED und `DREHEN BEWEGT HIER NICHTS`.

Die eigenen Worte der Faces folgen der Sprache des Panels, Englisch oder
Deutsch, aus dem SCREEN-Paket; vor dem ersten SCREEN die letzte Sprache, die
das Panel gab, nach einem Start Englisch. Es sind die Einträge
`UI_DIAL_TEXT` in `shared/text/include/ui_text.def`.

### Was eine Änderung kostet

Die Face-Task zeichnet alle 50ms, was sich seit dem Frame davor geändert
hat: eine Zelle, die Zeile der Pille, den Rumpf einer Liste oder eines
Werts, die Hinweise, und von einem Ring nur die Pixel zwischen seinem alten
und seinem neuen Ende. Jedes Neuzeichnen löscht nur innerhalb von Radius
102, berührt also nie einen Ring, einen Strich oder den Rand; Ringe und
Striche werden auf die Hintergrundfarbe gemalt, ein Neuzeichnen gleicht also
dem ersten. `test_dial_face` hält das Zeichnen der Änderungen über Folgen
jedes Face Pixel für Pixel gleich dem vollen Zeichnen desselben Modells. Ein
Damage-Record sammelt die geschriebenen Rechtecke, höchstens 8, und nur sie
gehen über SPI. Der ganze Frame geht bei einem Wechsel des Face und alle
10.000ms.

| Änderung | Gesendete Pixel | Bei 40MHz (gerechnet, nicht gemessen) |
| --- | ---: | ---: |
| Drehzahl um 300rpm: ihre Zelle und der Bogen ihres Rings | 3.920 | 1,6ms |
| Gas um 1% | 3.968 | 1,6ms |
| Laufuhr um 1s: die Zeile der Pille | 5.696 | 2,3ms |
| fünf Messwerte auf einmal | 19.040 | 7,6ms |
| der Fokus einer Liste um eine Zeile | 23.152 | 9,3ms |
| der ganze Frame | 57.600 | 23,0ms |

Die Ring-Tabellen enthalten jedes Pixel, das ein Ring berührt, nach Winkel
sortiert: 8.344 Pixel für die sechs Ringe, je 3 Bytes, 25.032 Bytes
konstante Daten im Flash. `tools/gen_dial_rings.py` rechnet sie mit der
Arithmetik von `gfx_ring_build()` und schreibt `shared/dial/dial_rings.c`;
CI ruft es mit `--check` auf, und `test_dial_face` hält die Datei Pixel für
Pixel gleich der Ausgabe von `gfx_ring_build()`. Ein volles Neuzeichnen
eines Rings mischt etwa 1.490 Pixel, eine Bewegung um 1% einer Umdrehung bei
Radius 118 etwa 15. Beides ist auf dem Gerät nicht gemessen.

Die Bilder stammen aus der Mathematik-Bibliothek des Rechners. Ring-Tabellen
und Fonts sind auf dem Gerät dieselben Bytes, aber den Endwinkel eines Rings
und den gestrichelten Rand rechnet das Gerät zur Laufzeit mit seiner eigenen
Mathematik-Bibliothek, die anders runden kann: Das Antialiasing auf dem
Gerät kann von den Bildern um eine Deckungsstufe abweichen.

## Die Konsole

Die Konsole ist der USB-Serial/JTAG-Port der USB-C-Buchse, mit dem Prompt
`dial>` und dem Log auf demselben Port.

| Befehl | Was er tut |
| --- | --- |
| `status` | Name, Adresse, Firmware- und Protokollversion, die Platine, das gebundene Panel, der Link und sein RSSI (Received Signal Strength Indication), die EVENT-Zähler, die Klick-Einstellung, der Akkuwert, der freie interne Heap und sein Tiefstwert seit dem Start |
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

- Das Dial navigiert nur die Bildschirme aus
  [Navigation und SET](#navigation-und-set). Auf LOGS, FILES, WI-FI,
  UPDATES, IMU, der Rasteransicht von OUTPUTS, den Programmern (KST, Hitec,
  ESC) und den eigenen Listen und Reitern der Prüfstandsbildschirme bewegt
  es keinen Fokus; ein HOLD tut dort auf einem SAFE-Prüfstand nichts.
- Eine Liste nimmt den Fokus für 64 Einträge, RESET und SAVE darunter; die
  letzten Zeilen einer längeren Liste erreicht das Dial nicht.
- Ein Pixel, das der Bildschirm unter dem Rahmen genau in der Farbe des
  Rahmens malt (der Akzent mit gekipptem untersten Bit), gilt als Rahmen
  und wird zurückgesetzt, wenn der Rahmen weiterzieht.
- Das Wasserzeichen eines simulierten Prüfstands blendet über den Rahmen;
  wo es das tut, bleiben die Pixel des Rahmens stehen, wenn er weiterzieht,
  bis der Bildschirm dort neu zeichnet.
- Das ARMED-Flag des Panels für die Verlustregel und die Ablehnung der Suche
  gehört dem Control-Task und wird vom Bluetooth-Task gelesen; es kann einen
  Control-Durchlauf, etwa 5ms, alt sein.
- LIVE_SERVO trägt keinen Messwert der Position oder des Stroms eines
  Feedback-Servos.
- Eine Servoschiene, die PROG 5V speist, hat in SCALE keinen Vollausschlag:
  0, ihre Ringe zeigen nur ihre Spur.
- Das Panel hat keinen Konsolenbefehl, der eine Drehung oder einen Druck des
  Dial einspeist; das macht der Test-Build des Dial von seiner Seite aus.
- Die Spannung des Akkus ist unbekannt: Keine Quelle nennt den Pin, den sie
  erreicht. STATUS trägt 0 und das Flag "Akku unbekannt". Der Zustand des
  Laders ist ebenfalls unbekannt.
- Der Zähler des Encoders fällt beim Dial v1.1 im Uhrzeigersinn; die
  Firmware dreht ihn um, sodass im Uhrzeigersinn hochgezählt wird, die
  Regel des Protokolls. Wo in seinen 4 Zählschritten der Encoder einrastet,
  ist nicht gemessen. Eine Rastung wechselt 3 Zählschritte
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
  zuerst das Dial am alten Panel lösen (BINDUNG LÖSEN auf SEARCH,
  [Suchen und Binden](#suchen-und-binden)).
- Zeichen außerhalb von ASCII, des Gradzeichens und der deutschen
  Buchstaben erscheinen als `?`.
- Das Senden eines ganzen Frames dauert bei 40MHz etwa 23ms (gerechnet,
  nicht gemessen). Das Neuzeichnen eines Rings ist auf dem Gerät nicht
  gemessen.
- Das Face eines Werts zeigt keine Kategorie: Kein Paket trägt eine.
- Die Latenz von der Drehung zum Ausgang über Funk ist nicht gemessen; die Station misst sie
  mit dem Test-Build, sobald das Dial an ihrem USB hängt.
- Die Rate des Updates über BLE über die
  ESP32-C6-Verbindung des Panels, die Dauer einer Übertragung von 840kB und
  ein Rollback am Dial sind nicht gemessen.
- Das Dial prüft Art, Chip und Segmente des Images, nicht seine Herkunft:
  Ein Central in Reichweite kann jedes Dial-Image anbieten, auch ältere,
  und ein Druck installiert es.
- Kein Schlaf, keine Stufen des Hintergrundlichts und keine automatische
  Abschaltung: Das Dial bleibt an bis `AUS` in der Wahl beim Ausschalten,
  bis RST oder bis die Zelle leer ist. Die Laufzeit mit einer Zelle ist nicht gemessen.
