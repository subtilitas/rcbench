---
screen: IMU
title: IMU
order: 66
---

# IMU

IMU ist der Bildschirm der Bluetooth-IMU (Inertial Measurement Unit): ein WitMotion-BWT901CL-Winkelsensor auf einem Ruder, der das Panel über ein HC-05-Bluetooth-Modul an UART1 des Panels erreicht. Er öffnet über die Kachel IMU auf [Setup](setup.md); sein Home-Tag führt dorthin zurück. Der Bildschirm steuert keinen Ausgang an. Seine Einstellungen stehen im IMU-Eintrag von [Sensoren](setup-sensors.md#die-imu). Die Verdrahtung und die Einrichtung des HC-05 beschreibt die Wiki-Seite [Die Bluetooth-IMU](https://github.com/subtilitas/rcbench/wiki/IMU-de).

![IMU, der HC-05 antwortet, kein Sensor gebunden](view:imu)

| Bedienelement | Was es tut |
|---|---|
| SENSOREN SUCHEN | öffnet die Liste der gefundenen Geräte und startet eine Suche; nimmt einen Druck an, solange der HC-05 antwortet und keine Suche, kein Binden und keine Kalibrierung läuft |
| MIT JEDEM SENSOR VERBINDEN | nur, nachdem der HC-05 eine Suche mit `ERROR:(1F)` abgelehnt hat: stellt ihn so ein, dass er sich mit jedem Sensor verbindet, der PIN 1234 annimmt (`AT+CMODE=1`) |
| 2s HALTEN: BESCHLEUNIGUNG KALIBRIEREN | solange verbunden: die Kalibrierung des Beschleunigungssensors. Halte ihn still und waagerecht für die 3s, die die Zeile herunterzählt |
| NULLPUNKT | nimmt die aktuellen Winkel als Nullpunkt; solange ein Messwert da ist. Mit ANGLE SOURCE IMU verwirft RUDDER seinen Nullpunkt und MAX |
| NULLPUNKT WEG | die Winkel wieder ab dem eigenen Nullpunkt des Sensors |
| IMU-EINSTELLUNGEN | öffnet den IMU-Eintrag von [Sensoren](setup-sensors.md#die-imu); dessen Home-Tag führt hierher zurück |

Ein Druck auf die Halten-Taste kürzer als 2s startet nichts, und das Band sagt `Halte 2s, um die Beschleunigung zu kalibrieren.`.

## Die Karte

![Verbunden mit 50Hz, NULLPUNKT gesetzt](view:imu-connected)

| Zeile | Zeigt |
|---|---|
| `SENSOR: HC-06` | den Namen des gebundenen Sensors aus der Suche, `OHNE NAMEN` ohne einen, `KEINER GEBUNDEN` oder `JEDER IN REICHWEITE`; darunter seine Adresse |
| `HC-05: 2.0-20100601` | die Firmware des HC-05, `---`, bis er geantwortet hat |
| Zustand | `IMU AUS IN IMU-EINSTELLUNGEN`, `KEIN HC-05`, `KEIN SENSOR GEBUNDEN`, `SUCHE LÄUFT`, `BINDEN LÄUFT`, `GEBUNDEN`, `VERBUNDEN 50Hz` oder `KEINE FRAMES` |
| X, Y, Z | die Winkel ab dem Nullpunkt, 0,1°, `---` ohne Messwert. Die Achse, die die Servoläufe lesen, steht in der Akzentfarbe |
| RATE, ALTER | die Winkel-Frames der letzten Sekunde und die Zeit, seit der neueste den Bildschirm erreicht hat |
| AKKU | der Akku des Sensors, aus seinem Frame 0x52; über 6,0V ausgeblendet |

Der Messwert ist 300ms nach dem letzten Winkel-Frame veraltet. Ein Servolauf mit WINKELQUELLE IMU geht durch eine Lücke bis 300ms einmal je 10s weiter, wie bei der des AS5600 ([Servo-Optionen](servo-settings.md)). Eine längere Lücke beendet den Lauf als `IMU antwortet nicht`, eine zweite innerhalb von 10s als `IMU 2x in 10s ausgefallen`.

## Suchen und binden

![Drei Geräte gefunden](view:imu-scan)

Eine Suche dauert bis zu 40s: 11,5s Inquiry, dann der Name jedes gefundenen Geräts, bis zu 8. Ein Tippen auf ein Gerät bindet den HC-05 mit PIN 1234 daran und schließt die Liste; der HC-05 startet dann neu und verbindet sich damit. SETUP behält Adresse und Namen und bittet ums Speichern. NEU SUCHEN startet eine weitere Suche; SCHLIESSEN und das Home-Tag schließen die Liste.

| Zeile | Bedeutung |
|---|---|
| `HC-05 ANTWORTET NICHT AUF AT: KABEL UND KEY PRÜFEN` | kein OK bei 115200, 38400 oder 9600 Baud |
| `DER HC-05 LEHNT DIE SUCHE AB: ERROR:(1F)` | Firmware 3.0 lehnt eine Suche laut Berichten ab; MIT JEDEM SENSOR VERBINDEN bindet den einen Sensor in Reichweite |
| `KEIN GERÄT GEFUNDEN: IST DER SENSOR AN?` | die Inquiry endete ohne Gerät |
| `KOPPELN FEHLGESCHLAGEN: SENSOR AN UND IN REICHWEITE?` | `AT+PAIR` antwortete FAIL oder ERROR |

## Verwandte Seiten

- [Sensoren](setup-sensors.md)
- [Servo-Optionen](servo-settings.md)
- [Setup](setup.md)
