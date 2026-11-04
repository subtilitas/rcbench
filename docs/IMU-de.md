# Die Bluetooth-IMU

<sub>[English](IMU.md) · **Deutsch**</sub>

Ein WitMotion-BWT901CL-Winkelsensor sitzt auf einem Ruder und sendet seinen
Winkel über Bluetooth. Ein HC-05-Bluetooth-Modul an UART1 (Universal
Asynchronous Receiver-Transmitter) des Panels empfängt ihn als SPP-Master
(Serial Port Profile). Das Panel richtet den HC-05 selbst über AT-Befehle
ein: Es findet die Baudrate des Moduls, sucht, bindet den Sensor und startet
das Modul im Datenmodus neu. Der Messwert ist eine Messung, kein Schutz: Er
ist 300ms nach dem letzten Winkel-Frame veraltet, und ein Servolauf geht
durch eine Lücke bis 300ms einmal je 10s weiter, wie bei der des AS5600
([Servoverfahren](Servo-de.md)).

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
| der Bildschirm und der IMU-Eintrag von SETUP | `shared/ui/imu_screen.c`, `shared/ui/sensors_pane.c` |
| UART, KEY-Leitung und Task | `firmware/panel/main/panel_imu.c` |

## Verdrahtung

Die Pins sind `PANEL_IMU_*` in
`firmware/panel/components/board/include/board_pins.h`. Jeder ist ein
Platzhalter, den der Eigentümer bestätigt.

| HC-05 (Breakout ZS-040) | Panel | Define |
| --- | --- | --- |
| VCC (3,6 bis 6V) | 5V des Headers H11, Pin 1 (vom Eigentümer zu bestätigen) | |
| GND | GND des Headers P3 | |
| TXD (3,3V) | IO5 des Headers P3, UART1 RX | `PANEL_IMU_PIN_RX` |
| RXD | IO4 des Headers P3, UART1 TX, 3,3V | `PANEL_IMU_PIN_TX` |
| EN (KEY) | IO2 des Headers P3, high für AT-Befehle | `PANEL_IMU_PIN_KEY` |
| STATE | nicht verdrahtet; das Alter des Messwerts steht dafür | `PANEL_IMU_PIN_STATE` (`GPIO_NUM_NC`) |

`PANEL_IMU_UART_NUM` ist 1: UART0 gehört der Konsole. `PANEL_IMU_BAUD` ist
115200, die feste Baudrate des BWT901CL. Das Breakout des HC-05 regelt 3,3V
auf der Platine, sein TXD hat 3,3V, und sein RXD ist nicht 5V-fest; die
3,3V-Pins des Panels passen ohne Pegelwandler. Sein Strom beträgt bis zu
40mA (Datenblatt). Die 5V sind nicht geschaltet: Das Panel kann das Modul
nicht aus- und einschalten. Das Panel hebt KEY nach dem Einschalten des
Moduls an; das bringt es in den AT-Modus mit seiner Datenbaudrate. Ein Modul,
das AT so ablehnt, wird nicht behandelt. Ist STATE an einen GPIO verdrahtet
und in `PANEL_IMU_PIN_STATE` genannt, schreibt die Konsole seinen Pegel,
wenn er wechselt.

## Die Einrichtung des HC-05

Das Panel hält KEY vom ersten Byte einer Operation bis zu ihrem Ende high,
100ms vor dem ersten Byte. Jeder Befehl wartet 1000ms auf sein OK, AT+INQ
13,52s, AT+RNAME? 3000ms, AT+PAIR 22000ms, AT+RESET 1500ms. Das README des
Moduls in `protocols/hc05` nennt jede Antwort und jeden Fehler.

| Operation | Wann | Befehle |
| --- | --- | --- |
| PROBE | IMU EIN, und alle 10s, solange kein Modul antwortet, auch nach einer Suche oder einem Binden ohne AT | `AT` mit 115.200, 38.400, dann 9.600 Baud; `AT+VERSION?` |
| SCAN | SENSOREN SUCHEN | `AT+INIT` (ERROR:(17) ignoriert), `AT+INQM=1,9,9` (11,52s), `AT+INQ`, `AT+RNAME?<addr>` je Gerät, bis zu 8 |
| BIND | ein Tippen auf ein gefundenes Gerät | `AT+ROLE=1`, `AT+PSWD=1234` (`AT+PSWD="1234"` nach einem ERROR), `AT+PAIR=<addr>,20`, `AT+BIND=<addr>`, `AT+CMODE=0`, `AT+UART=115200,0,0`, `AT+RESET` |
| ANY | MIT JEDEM SENSOR VERBINDEN, nach einer mit ERROR:(1F) abgelehnten Suche | `AT+ROLE=1`, `AT+PSWD=1234`, `AT+CMODE=1`, `AT+UART=115200,0,0`, `AT+RESET` |

Nach AT+RESET startet das Modul mit KEY low im Datenmodus neu. Die
gebundene Adresse und der Name aus der Suche gehen nach SETUP (`imu_addr`,
`imu_name`, 23 Zeichen behalten), und ein Speichern wird erbeten; ANY behält
`*`. Solange keine Operation läuft, sind die Bytes des UART die Frames des
Sensors. IMU AUS während einer Operation hält den HC-05 belegt, bis der
Fence seine späten Antworten abgefangen hat: bis zu 14.520ms nach AT+INQ,
23.000ms nach AT+PAIR. Eine Probe eines stummen Moduls endet nach 5.100ms:
100ms KEY settle, 1000ms bei jeder der 3 Baudraten und 2000ms Fence.

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

Die Befehle gehen 50ms auseinander. Eine Änderung von RATE oder ALGORITHMUS,
während der Sensor nicht verbunden ist, geht, sobald seine Frames kommen.
Eine neue RATE gilt, nachdem der Sensor aus- und wieder eingeschaltet wurde.
NULLPUNKT nimmt die aktuellen Winkel auf dem Panel als Nullpunkt; an den
Sensor geht nichts, und ein Neustart vergisst ihn.

## Grenzen

| Punkt | Wert |
| --- | --- |
| Akku | 250mAh, 4h je Ladung (Datenblatt) |
| Ausgaberate | 0,2 bis 200Hz, ab Werk 10Hz; das Panel bietet 10 bis 200Hz, ab Werk 50Hz |
| Genauigkeit X, Y | 0,05° (Datenblatt), 0,2° (Webseite des Herstellers) |
| Genauigkeit Z | 1° nach der Kalibrierung des Magnetometers, die der Servomotor stört; die Drift von Z ohne es ist nicht angegeben |
| der Winkel eines Laufs | in AS5600-Zählern getragen: 360 / 4096 = 0,088° je Zähler |
| der Puffer des HC-05 auf Firmware 3.0 | 230 Bytes, etwa 105ms bei 200Hz (nicht gemessen) |
| ein Verbindungsabbruch | ob der Sensor seine Frames puffert oder verwirft, ist nicht angegeben; der HC-05 verbindet sich nach einem Reichweitenverlust 30min lang neu |
| das Alter des Messwerts | veraltet nach 300ms |

Kein HC-05 und kein BWT901CL ist mit diesem Code gelaufen. Die Antworten der
Firmware 2.0 und 3.0 stammen aus dem Datenblatt und aus Berichten.
