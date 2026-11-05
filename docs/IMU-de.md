# Die Bluetooth-IMU

<sub>[English](IMU.md) · **Deutsch**</sub>

Die Bluetooth-IMU (Inertial Measurement Unit, Trägheitsmesseinheit) ist ein
WitMotion-BWT901CL-Winkelsensor auf einem Ruder. Er sendet den Winkel über
Bluetooth LE (Low Energy, BLE) an das Panel. Der ESP32-C6 des Boards
betreibt den BLE-Controller; der ESP32-P4 betreibt den Host, NimBLE, über
die SDIO-Verbindung (Secure Digital Input/Output), die auch das WLAN nutzt.
Der Sensor braucht kein Kabel zum Panel, kein Pairing und keine PIN.

Der Messwert dient der Messung, nicht als Schutzfunktion. Er gilt 300ms
nach dem letzten Winkel-Frame als veraltet. Ein Servolauf toleriert einmal
je 10s eine Lücke von bis zu 300ms, wie beim AS5600
([Servoverfahren](Servo-de.md)). Nach einem Wechsel der WINKELQUELLE erhält
SERVO im nächsten Anzeigebild den Zustand der neuen Quelle: den gültigen
oder ungültigen IMU-Messwert oder zunächst keinen AS5600-Messwert, bis
dieser Sensor wieder einen liefert.

Beendet ein eingehender Winkel-Frame eine Phase mit veraltetem Messwert
noch vor dem nächsten Anzeigebild, erhält SERVO trotzdem zuerst den
veralteten Zustand. Dazu wird vor dem neuen Messwert ein veralteter
Messwert mit einem Zeitstempel 300ms nach dem vorherigen Winkel-Frame
übergeben.

WitMotion, BWT901CL und HC-06 sind Namen von Produkten ihrer Hersteller,
ISSC und Microchip sind Namen ihrer Inhaber, und Bluetooth ist eine Marke
ihres Inhabers. rcbench ist unabhängig: nicht mit ihnen verbunden, nicht
von ihnen unterstützt oder gesponsert. Die Namen sagen, mit welchen Geräten
und welchem Dienst das Panel spricht.
[Rechtliche Hinweise und Haftungsausschluss](../README-de.md#rechtliche-hinweise-und-haftungsausschluss)
enthält den Hinweis des Projekts.

| Teil | Wo |
| --- | --- |
| die BLE-Central: Suchliste, Verbinden, GATT-Erkundung (Generic Attribute Profile), Abonnieren, Schreiben, Wiederholungen | `protocols/bleuart`, kein Funkcode |
| Frames und Befehle des Sensors | `protocols/witmotion`, kein Pin-Treiber |
| der Messwert: Winkel, Nullpunkt, Ruhezeit, Rate, Akku, Alter | `shared/bench/imu.c` |
| die Seite des Sensors: der Messwert, seine Befehle, die Kalibrierung, die Lücken, die Übergabe an SERVO; ohne Funk | `shared/ui/imu_sensor.c` |
| die BLE-Verbindung: die Central für die IMU, ihre Geräte, Fehler und die Version des C6 in den Typen der Ansicht | `shared/ui/imu_ble.c` |
| die Steuerung: verbindet die Verbindung mit der Seite des Sensors, der Verbindungszustand, die Ansicht | `shared/ui/imu_ctl.c` |
| die Seite IMU und ihr Eintrag unter SETUP | `shared/ui/imu_screen.c`, `shared/ui/sensors_pane.c` |
| der IMU-Task: Events hinein, Aktionen hinaus, alle 5ms | `firmware/panel/main/panel_imu.c` |
| der Start von NimBLE, seine Aufrufe und seine Callbacks | `firmware/panel/main/panel_ble.c` |

## Start

Das erste IMU EIN (SETUP > SENSOREN > IMU, auch beim Einschalten mit
gespeichertem IMU EIN) startet Bluetooth LE: die Verbindung zum C6, den
Host-Task `nimble_host` und das Warten von 3 000ms auf seinen Sync
([Bauen, Bluetooth LE über den
ESP32-C6](Building-de.md#bluetooth-le-über-den-esp32-c6)). Bluetooth LE
bleibt bis zum Neustart des Panels; IMU AUS beendet nur die Suche und die
Verbindung. Die Verbindung zum C6 dauert bei einem C6, der nicht antwortet,
bis zu etwa 25s. Eine gescheiterte Verbindung hält den C6 bis zum Neustart
des Panels im Reset: WLAN und Bluetooth sind aus.

| Zustand | Bedeutung |
| --- | --- |
| BLUETOOTH STARTET | der Start läuft |
| KEIN BLUETOOTH AM C6 | der Host hat keinen Sync mit dem C6; er versucht es weiter, ein später Sync hebt den Zustand auf |
| C6 AUSGEFALLEN: NEUSTART | die Verbindung zum C6 schlug fehl |
| KEIN SENSOR GEBUNDEN | Bluetooth läuft, kein Sensor ist gebunden |
| SUCHE LÄUFT | eine Suche läuft |
| VERBINDE | ein Verbindungsaufbau oder seine GATT-Schritte laufen, auch nach einem Abbruch |
| GEBUNDEN | abonniert und noch kein Winkel-Frame; oder das gebundene Gerät ist kein WitMotion-Sensor |
| VERBUNDEN 50Hz | Winkel-Frames binnen 300ms, mit der gezeigten Rate |
| KEINE FRAMES | abonniert, die Frames blieben aus |

## Suchen und binden

SENSOREN SUCHEN startet eine aktive Suche von 10 000ms mit dem
Duplikatfilter des Controllers. Ein Gerät wird gelistet, wenn seine
Advertising-Daten die 16-Bit-UUID 0x18F0 oder die 128-Bit-UUID des
ISSC-Transparent-UART-Dienstes enthalten; jedes andere Gerät wird als
ausgeblendet gezählt. 0x18F0 ist keine Vergabe der Bluetooth SIG, deshalb
prüft das Verbinden den Dienst. Die Liste fasst 8 Geräte, das stärkste
zuerst: Name, Adresse und RSSI (Received Signal Strength Indication), zum
Beispiel `HC-06  00:0C:BF:08:19:29  -50dBm`. Das Funkmodul des Sensors
meldet den Namen `HC-06`.

Ein Tippen auf eine Zeile bindet diesen Sensor sofort. SETUP behält seine
Adresse mit dem Adresstyp, `00:0C:BF:08:19:29 P` (öffentlich) oder `... R`
(zufällig), als `imu_ble` und seinen Namen als `imu_name`, 23 Zeichen, und
fordert das Speichern an. Ein neu gebundener Sensor ersetzt den vorigen.
Eine Suche bei verbundenem Sensor behält die Verbindung; eine Suche während
eines laufenden Verbindungsaufbaus bricht diesen ab, und die Versuche gehen
nach der Suche weiter.

## Die Verbindung

| Schritt | Zeitgrenze | Scheitert als |
| --- | ---: | --- |
| Verbinden mit der gebundenen Adresse, Intervall 30 bis 50ms, Latenz 0, Supervision Timeout 4 000ms, Connect-Scan 30ms je 60ms | 5 000ms | KEINE VERBINDUNG, erneut nach 7 000ms bei einem Fehler, den der Host meldet (darunter HCI 0x3E), nach 1 000ms bei der Zeitgrenze |
| den ISSC-Dienst 49535343-FE7D-4AE5-8FA9-9FAFD205E455 finden | 5 000ms | KEIN WITMOTION-SENSOR, kein neuer Versuch bis zum nächsten Binden oder IMU AUS und EIN |
| seine Characteristics: 1E4D mit NOTIFY, 8841 mit WRITE oder WRITE NO RESPONSE | 5 000ms | KEIN WITMOTION-SENSOR |
| der CCCD von 1E4D (Client Characteristic Configuration Descriptor) | 5 000ms | KEIN WITMOTION-SENSOR |
| MTU-Austausch (Maximum Transmission Unit), 247 erfragt | 5 000ms | nur protokolliert |
| Abonnieren: 0x0001 in den CCCD | 5 000ms | KEINE VERBINDUNG, erneut nach 1 000ms |
| 6DAA einmal lesen, protokolliert | 5 000ms | nur protokolliert |

Jede Notification auf 1E4D trägt den UART-Strom des Sensors in Stücken von
bis zu 20 Bytes, nicht an Frame-Grenzen geschnitten. Der WitMotion-Parser
nimmt jede Stückgröße; eine verlorene Notification zerstört einen Frame,
den er als fehlerhaft zählt und neu synchronisiert. Nach einem Abbruch der
Verbindung (Grund in der Konsolenzeile) verbindet sich das Panel nach
1 000ms neu. Eine Änderung der Verbindungsparameter, die der Sensor
verlangt, wird angenommen und protokolliert. Die Characteristics ACA3 und
6DAA werden nie beschrieben.

Die Verbindungswerte sind so gewählt, wie eine Handy-Central sie
erfragt, nicht gegen andere gemessen; sie stehen zusammen in
`protocols/bleuart/include/bleuart.h` (`BLEU_ITVL_*`, `BLEU_LATENCY`,
`BLEU_SUPERVISION`, `BLEU_CONN_SCAN_*`). Nach jedem gescheiterten Verbinden
nimmt das Panel die nächste von vier Varianten und behält die, die
verbindet:

| Variante | Parameter | Eigene Adresse |
| ---: | --- | --- |
| 0 | die der Tabelle | der Typ, den der Sync ermittelt hat (`ble_hs_id_infer_auto()`) |
| 1 | NimBLEs Vorgaben: Intervall 30 bis 50ms, Latenz 0, Supervision Timeout 2 560ms, Connect-Scan 10ms je 10ms | der ermittelte Typ |
| 2 | die der Tabelle | der andere Typ; random ist eine statische Zufallsadresse, die das Panel einmal setzt |
| 3 | NimBLEs Vorgaben | der andere Typ |

KEINE VERBINDUNG lautet `KEINE VERBINDUNG: SENSOR AN, NAH, KEIN HANDY
DRAN?`: Ein mit dem Sensor verbundenes Handy sperrt das Panel aus, weil ein
verbundenes Peripheriegerät das Werben einstellt. Jede Central in Reichweite
kann sich mit dem Sensor verbinden; er hat kein Pairing.

## Der Sensor

Frames: 0x55, der Typ, vier int16-Wörter Little-Endian, das untere Byte der
Summe. Das Panel liest 0x53 (die Winkel, 180° bei 32768, und die
Firmware-Version), 0x52 (den Akku, das vierte Wort ohne Vorzeichen, V x 100)
und 0x51 (die Temperatur, °C x 100). Nur ein Winkel-Frame macht den Messwert
gültig.

| Befehl | Bytes | Gesendet |
| --- | --- | --- |
| Entsperren | `FF AA 69 88 B5` | vor jedem anderen Befehl; auf der älteren Firmware harmlos (an diesem Sensor nicht gemessen) |
| CALSW 1, 3s, CALSW 0, SAVE | `FF AA 01 01 00`, `FF AA 01 00 00`, `FF AA 00 00 00` | `2s HALTEN: BESCHLEUNIGUNG KALIBRIEREN`, verbunden |
| RATE, SAVE | `FF AA 03 <code> 00`, `FF AA 00 00 00` | eine Änderung von IMU-Rate: 0x06 10Hz, 0x07 20Hz, 0x08 50Hz, 0x09 100Hz, 0x0B 200Hz |
| AXIS6, SAVE | `FF AA 24 01 00` (6-AXIS) oder `FF AA 24 00 00` (9-AXIS), `FF AA 00 00 00` | eine Änderung von IMU-Algorithmus |

Die Befehle gehen auf 8841, im Abstand von 50ms, einer zur Zeit, jeder
binnen 1 000ms beantwortet. Hat 8841 WRITE, schreibt das Panel mit
Antwort; hat es nur WRITE NO RESPONSE, ohne. Ein abgelehntes oder
unbeantwortetes Schreiben verwirft den Rest seiner Gruppe und zeigt `DER
SENSOR HAT DEN BEFEHL NICHT ANGENOMMEN`. Eine Änderung von RATE oder
ALGORITHMUS bei nicht verbundenem Sensor wartet, bis dessen Frames
eintreffen. Eine Änderung während der Kalibrierung wartet bis nach CALSW 0
und SAVE. Unlock, die Schreibbefehle und SAVE werden erst gemeinsam
eingereiht, wenn alle in die 8 Befehlsplätze passen. Bis dahin wartet die
Änderung; gesendet wird der dann aktuelle Wert aus SETUP. Eine neue RATE
gilt erst, nachdem der Sensor aus- und wieder eingeschaltet wurde. IMU AUS
während einer Kalibrierung beendet sie mit FAILED; ihr CALSW 0 und SAVE
gehen beim nächsten Verbinden.

NULLPUNKT übernimmt die aktuellen Winkel auf dem Panel als Nullpunkt. Dabei
wird nichts an den Sensor gesendet. Der Nullpunkt geht beim Neustart
verloren.

## Konsolenzeilen

Die Zeilen tragen die Tags `imu` und `ble`:

```text
I (…) imu: IMU ON: starting Bluetooth LE
I (…) ble: synced in <n>ms, own address type <t>
I (…) ble: scan for 10000ms
I (…) ble: connect to 00:0C:BF:08:19:29 type 0 for 5000ms
I (…) ble: connect: variant <v>, own address public (type 0), bleuart's parameters
I (…) ble: connect: interval 24-40 x 1.25ms, latency 0, timeout 400 x 10ms, scan 96/48 x 0.625ms
I (…) ble: connect: last report <r>dBm, <a>ms ago
I (…) ble: connect: status 0x0, handle <h>, token <k>
I (…) ble: connect: record: handle <h>, peer 00:0C:BF:08:19:29 type 0 (over the air type 0), own type 0, role 0, interval <i> x 1.25ms, latency 0, timeout <s> x 10ms, clock accuracy <c>
I (…) ble: MTU exchange: status 0, MTU <m>
I (…) ble: 6DAA: 8 bytes 00 0C 00 0C 00 00 00 02
I (…) imu: link <a> -> <b>, error <e> code 0x<c>
I (…) imu: <n> frames in 1000ms, rate <r>Hz, roll <x> pitch <y> yaw <z> deg, bad <k>, battery <v>V, <d> events dropped
I (…) imu: command FF AA 69 88 B5
I (…) ble: disconnect: reason 0x<r>, handle <h>, token <k>
I (…) ble: disconnect: record: handle <h>, peer …
I (…) ble: connect failed with 0x<r>: the next try is variant <v>
```

Grund 0x23E ist HCI 0x3E, Verbindung nicht aufgebaut, wie NimBLE ihn
meldet (0x200 + HCI-Code). `ble controller` im Konsolen-Build schickt die
Controller-Init- und Enable-Anfragen des C6 erneut
([BenchConsole-de.md](BenchConsole-de.md)).

`<n> frames` zählt Frames aller Typen; die Rate zählt Winkel-Frames.

## Grenzen

| Punkt | Wert |
| --- | --- |
| Akku | 250mAh, 4h je Ladung (Datenblatt) |
| Ausgaberate | 0,2 bis 200Hz, ab Werk 10Hz; das Panel bietet 10 bis 200Hz, Vorgabe 50Hz |
| BLE-Durchsatz | 20 Bytes je Notification; bei 30 bis 50ms und einer Notification je Verbindungsereignis 400 bis 667B/s, unter den 2 200B/s von 50Hz mit allen Frames; die Notifications je Ereignis und die passenden Raten sind nicht gemessen |
| Verbinden | an einem Panel nicht bestätigt: mit Intervall 15ms, Supervision Timeout 1 000ms und einem Connect-Scan 10ms/10ms endeten 35 Versuche bei -59dBm mit HCI 0x3E (Station, 2026-11-05); die vier Varianten oben sind ungetestet |
| Genauigkeit X, Y | 0,05° (Datenblatt), 0,2° (Webseite des Herstellers) |
| Genauigkeit Z | 1° nach der Kalibrierung des Magnetometers, die der Servomotor stört; die Drift von Z ohne diese Kalibrierung ist nicht angegeben |
| der Winkel eines Laufs | als AS5600-Zählerstand dargestellt: 360 / 4096 = 0,088° je Zähler |
| ein Verbindungsabbruch | ob das Funkmodul des Sensors Bytes, die es nicht senden kann, puffert oder verwirft, ist nicht bekannt; das Panel verbindet sich bei IMU EIN nach 1 000ms neu |
| WLAN neben BLE | der C6 teilt seinen Funk zeitlich; die Wirkung auf beide ist nicht gemessen |
| Schreiben | ob ein Schreiben auf 8841 den Sensor erreicht, ist nicht gemessen |
| das Alter des Messwerts | veraltet nach 300ms |
