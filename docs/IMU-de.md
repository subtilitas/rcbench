# Die Bluetooth-IMU

<sub>[English](IMU.md) · **Deutsch**</sub>

Die Bluetooth-IMU (Inertial Measurement Unit, Trägheitsmesseinheit) ist ein
WitMotion-BWT901CL-Winkelsensor auf einem Ruder. Er sendet den Winkel über
Bluetooth an ein HC-05-Modul an UART1 (Universal Asynchronous
Receiver-Transmitter) des Panels. Der HC-05 arbeitet als SPP-Master (Serial
Port Profile). Das Panel richtet ihn über AT-Befehle ein: Es erkennt die
Baudrate, sucht nach Sensoren, bindet den gewählten Sensor und startet das
Modul im Datenmodus neu.

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

WitMotion, BWT901CL, HC-05 und HC-06 sind Namen von Produkten ihrer
Hersteller, und Bluetooth ist eine Marke ihres Inhabers. rcbench ist
unabhängig: nicht mit ihnen verbunden, nicht von ihnen unterstützt oder
gesponsert. Die Namen sagen, mit welchen Geräten das Panel spricht.
[Rechtliche Hinweise und Haftungsausschluss](../README-de.md#rechtliche-hinweise-und-haftungsausschluss)
enthält den Hinweis des Projekts.

| Teil | Wo |
| --- | --- |
| die AT-Einrichtung des HC-05 | `protocols/hc05`, kein Pin-Treiber |
| Frames und Befehle des Sensors | `protocols/witmotion`, kein Pin-Treiber |
| der Messwert: Winkel, Nullpunkt, Ruhezeit, Rate, Akku, Alter | `shared/bench/imu.c` |
| die Steuerung: die Operationen des HC-05, die Befehle des Sensors, der Verbindungszustand | `shared/ui/imu_ctl.c` |
| die Seite IMU und ihr Eintrag unter SETUP | `shared/ui/imu_screen.c`, `shared/ui/sensors_pane.c` |
| UART, KEY-Leitung und Task | `firmware/panel/main/panel_imu.c` |

## Verdrahtung

Die Pins sind `PANEL_IMU_*` in
`firmware/panel/components/board/include/board_pins.h`. Die Signale gehen auf
den Header P3, KEY auf IO29 neben IO28 des Heartbeats. 5V und GND kommen von
H9, dem RS485-Header, den der Prüfstand sonst nicht nutzt: sein Pin 1 ist
Core_5V, dasselbe Netz wie Pin 1 des CAN-Headers H11, das CAN-Kabel bleibt
also, wie es ist. IO4 und IO5 sind die Pad-JTAG-Pins des ESP32-P4 (MTMS,
MTDO); mit Debugging über USB-JTAG sind sie gewöhnliche GPIOs.

| HC-05 (Breakout ZS-040) | Panel | Define |
| --- | --- | --- |
| VCC (3,6 bis 6V) | Core_5V des Headers H9 (RS485), Pin 1 | |
| GND | GND des Headers H9 (RS485), Pin 2 | |
| TXD (3,3V) | IO5 des Headers P3, UART1 RX | `PANEL_IMU_PIN_RX` |
| RXD | IO4 des Headers P3, UART1 TX, 3,3V | `PANEL_IMU_PIN_TX` |
| EN (KEY) | IO29 des Headers P3, high für AT-Befehle | `PANEL_IMU_PIN_KEY` |
| STATE | nicht verdrahtet; stattdessen wird das Alter des Messwerts ausgewertet | `PANEL_IMU_PIN_STATE` (`GPIO_NUM_NC`) |

`PANEL_IMU_UART_NUM` ist 1: UART0 gehört der Konsole. `PANEL_IMU_BAUD` ist
115200, die feste Baudrate des BWT901CL. Das Breakout des HC-05 regelt 3,3V
auf der Platine, sein TXD hat 3,3V, und sein RXD ist nicht 5V-fest; die
3,3V-Pins des Panels passen ohne Pegelwandler. Sein Strom beträgt bis zu
40mA (Datenblatt). Die 5V sind nicht geschaltet: Das Panel kann das Modul
nicht aus- und einschalten. Das Panel hebt KEY nach dem Einschalten des
Moduls an; das bringt es in den AT-Modus mit seiner Datenbaudrate. Ein Modul,
das auf diesem Weg keine AT-Befehle akzeptiert, wird nicht unterstützt. Ist STATE an einen GPIO verdrahtet
und in `PANEL_IMU_PIN_STATE` genannt, protokolliert die Konsole seinen Pegel
bei jeder Änderung.

## Die Einrichtung des HC-05

Das Panel setzt KEY 100ms vor dem ersten Byte einer Operation auf high
und hält den Pegel bis zu ihrem Ende. Befehle warten 1000ms auf OK, mit
folgenden Ausnahmen: AT+INQ wartet 13,52s, AT+RNAME? 3000ms, AT+PAIR 22000ms
und AT+RESET 1500ms. Das README des
Moduls in `protocols/hc05` nennt jede Antwort und jeden Fehler.

| Operation | Wann | Befehle |
| --- | --- | --- |
| PROBE | IMU EIN, und alle 10s, solange kein Modul antwortet, auch nach einer Suche oder einem Binden ohne AT | `AT` mit 115.200, 38.400, dann 9.600 Baud; `AT+VERSION?` |
| SCAN | SENSOREN SUCHEN, nicht während eine Kalibrierung läuft oder ein Sensorbefehl wartet | `AT+INIT` (ERROR:(17) ignoriert), `AT+INQM=1,9,9` (11,52s), `AT+INQ`, `AT+RNAME?<addr>` je Gerät, bis zu 8 |
| BIND | ein Tippen auf ein gefundenes Gerät | `AT+ROLE=1`, `AT+PSWD=1234` (`AT+PSWD="1234"` nach einem ERROR), `AT+PAIR=<addr>,20`, `AT+BIND=<addr>`, `AT+CMODE=0`, `AT+UART=115200,0,0`, `AT+RESET` |
| ANY | MIT JEDEM SENSOR VERBINDEN, nach einer mit ERROR:(1F) abgelehnten Suche | `AT+ROLE=1`, `AT+PSWD=1234`, `AT+CMODE=1`, `AT+UART=115200,0,0`, `AT+RESET` |

Nach AT+RESET startet das Modul mit KEY low im Datenmodus neu. Das Panel
trägt die gebundene Adresse und den gefundenen Namen unter SETUP ein
(`imu_addr`, `imu_name`, auf 23 Zeichen begrenzt) und fordert das Speichern
an. ANY speichert `*`. Solange keine Operation läuft, werden die UART-Bytes
als Sensor-Frames ausgewertet.

Wird IMU während einer Operation ausgeschaltet, bleibt der HC-05 bis zum
Ende der Wartephase für verspätete Antworten (Fence) belegt: bis zu 14.520ms
nach AT+INQ und 23.000ms nach AT+PAIR. Die Erkennung eines nicht antwortenden
Moduls endet nach 5.100ms: 100ms Wartezeit nach dem Setzen von KEY, 1000ms
bei jeder der 3 Baudraten und 2000ms Fence.

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

Die Befehle werden im Abstand von 50ms gesendet. Eine Änderung von RATE
oder ALGORITHMUS bei getrenntem Sensor wartet, bis dessen Frames eintreffen.
Eine Änderung während der Kalibrierung wartet bis nach CALSW 0 und SAVE.
Unlock, die Schreibbefehle und SAVE werden erst gemeinsam eingereiht, wenn
alle in die 8 Befehlsplätze passen. Bis dahin wartet die Änderung; gesendet
wird der dann aktuelle Wert aus SETUP. Eine neue RATE gilt erst, nachdem
der Sensor aus- und wieder eingeschaltet wurde.

NULLPUNKT übernimmt die aktuellen Winkel auf dem Panel als Nullpunkt. Dabei
wird nichts an den Sensor gesendet. Der Nullpunkt geht beim Neustart
verloren.

## Grenzen

| Punkt | Wert |
| --- | --- |
| Akku | 250mAh, 4h je Ladung (Datenblatt) |
| Ausgaberate | 0,2 bis 200Hz, ab Werk 10Hz; das Panel bietet 10 bis 200Hz, Vorgabe 50Hz |
| Genauigkeit X, Y | 0,05° (Datenblatt), 0,2° (Webseite des Herstellers) |
| Genauigkeit Z | 1° nach der Kalibrierung des Magnetometers, die der Servomotor stört; die Drift von Z ohne diese Kalibrierung ist nicht angegeben |
| der Winkel eines Laufs | als AS5600-Zählerstand dargestellt: 360 / 4096 = 0,088° je Zähler |
| der Puffer des HC-05 auf Firmware 3.0 | 230 Bytes, etwa 105ms bei 200Hz (nicht gemessen) |
| ein Verbindungsabbruch | ob der Sensor seine Frames puffert oder verwirft, ist nicht angegeben; der HC-05 verbindet sich nach einem Reichweitenverlust 30min lang neu |
| das Alter des Messwerts | veraltet nach 300ms |

Dieser Code wurde weder mit einem HC-05 noch mit einem BWT901CL auf Hardware erprobt. Die Antworten der
Firmware 2.0 und 3.0 stammen aus dem Datenblatt und aus Berichten.
