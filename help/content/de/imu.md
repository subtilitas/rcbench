---
screen: IMU
title: IMU
order: 66
---

# IMU

Die Seite IMU steuert eine Bluetooth-IMU (Inertial Measurement Unit): einen WitMotion-BWT901CL-Winkelsensor auf einem Ruder. Ein HC-05-Bluetooth-Modul verbindet ihn mit UART1 (Universal Asynchronous Receiver/Transmitter) des Panels. Öffne die Seite über die Kachel IMU auf [SETUP](setup.md); das Home-Feld führt dorthin zurück. Die Seite steuert keine Ausgänge an. Ihre Einstellungen findest du im IMU-Eintrag unter [Sensoren](setup-sensors.md#die-imu). Die Wiki-Seite [Die Bluetooth-IMU](https://github.com/subtilitas/rcbench/wiki/IMU-de) beschreibt die Verdrahtung und die Einrichtung des HC-05.

![IMU, der HC-05 antwortet, kein Sensor gebunden](view:imu)

| Bedienelement | Funktion |
|---|---|
| SENSOREN SUCHEN | öffnet die Geräteliste und startet eine Suche; verfügbar, solange der HC-05 antwortet und keine Suche, kein Bindevorgang und keine Kalibrierung läuft |
| MIT JEDEM SENSOR VERBINDEN | verfügbar, nachdem der HC-05 eine Suche mit `ERROR:(1F)` abgelehnt hat; stellt ihn so ein, dass er sich mit jedem Sensor verbindet, der PIN 1234 annimmt (`AT+CMODE=1`) |
| 2s HALTEN: BESCHLEUNIGUNG KALIBRIEREN | startet bei bestehender Verbindung die Kalibrierung des Beschleunigungssensors; halte ihn während des Countdowns von 3s still und waagerecht |
| NULLPUNKT | übernimmt die aktuellen Winkel als Nullpunkt, wenn ein Messwert vorliegt; mit WINKELQUELLE IMU werden dabei der Nullpunkt und MAX auf RUDER gelöscht |
| NULLPUNKT LÖSCHEN | zeigt die Winkel wieder relativ zum eigenen Nullpunkt des Sensors |
| IMU-OPTIONEN | öffnet den IMU-Eintrag unter [Sensoren](setup-sensors.md#die-imu); dessen Home-Feld führt hierher zurück |

Ein Druck von weniger als 2s startet keine Kalibrierung. Das Band zeigt `Halte 2s, um den Beschleunigungssensor zu kalibrieren.`.

## Die Karte

![Verbunden mit 50Hz, NULLPUNKT gesetzt](view:imu-connected)

| Zeile | Anzeige |
|---|---|
| `SENSOR: HC-06` | der bei der Suche ermittelte Name des gebundenen Sensors, `OHNE NAMEN` ohne Namen, `KEINER GEBUNDEN` oder `JEDER IN REICHWEITE`; darunter steht die Adresse |
| `HC-05: 2.0-20100601` | die Firmware des HC-05; bis zur Antwort steht dort `---` |
| Zustand | `IMU AUS IN IMU-OPTIONEN`, `KEIN HC-05`, `KEIN SENSOR GEBUNDEN`, `SUCHE LÄUFT`, `BINDEN LÄUFT`, `GEBUNDEN`, `VERBUNDEN 50Hz` oder `KEINE FRAMES` |
| X, Y, Z | die Winkel relativ zum Nullpunkt mit einer Auflösung von 0,1°, ohne Messwert `---`; die von Servoläufen verwendete Achse erscheint in der Akzentfarbe |
| RATE, ALTER | die Zahl der Winkel-Frames in der letzten Sekunde und die Zeit seit dem jüngsten Frame auf der Seite |
| AKKU | der Akkuwert des Sensors aus Frame 0x52; über 6,0V ausgeblendet |

Ein Messwert gilt 300ms nach dem letzten Winkel-Frame als veraltet. Mit WINKELQUELLE IMU toleriert ein Servolauf eine Lücke von bis zu 300ms je 10s, wie beim AS5600 ([Servo-Optionen](servo-settings.md)). Eine längere Lücke beendet den Lauf mit `IMU antwortet nicht`, eine zweite innerhalb von 10s mit `IMU 2x in 10s ausgefallen`.

## Suchen und binden

![Drei Geräte gefunden](view:imu-scan)

Eine Suche dauert bis zu 40s: zunächst 11,5s für die Gerätesuche, danach die Namensabfragen für bis zu 8 Geräte. Tippe auf ein Gerät, um den HC-05 mit PIN 1234 daran zu binden und die Liste zu schließen. Der HC-05 startet anschließend neu und verbindet sich mit dem Gerät. SETUP behält Adresse und Namen und fordert zum Speichern auf. AKTUALISIEREN wiederholt die Suche; SCHLIESSEN oder das Home-Feld schließt die Liste.

| Zeile | Bedeutung |
|---|---|
| `HC-05 ANTWORTET NICHT AUF AT. PRÜFE KABEL UND KEY` | kein OK bei 115200, 38400 oder 9600 Baud |
| `DER HC-05 LEHNT DIE SUCHE AB: ERROR:(1F)` | Firmware 3.0 lehnt eine Suche laut Berichten ab; MIT JEDEM SENSOR VERBINDEN bindet den einen Sensor in Reichweite |
| `KEIN GERÄT GEFUNDEN: IST DER SENSOR EIN?` | die Inquiry endete ohne Gerät |
| `PAIRING FEHLGESCHLAGEN: SENSOR EIN UND IN REICHWEITE?` | `AT+PAIR` antwortete FAIL oder ERROR |

## Verwandte Seiten

- [Sensoren](setup-sensors.md)
- [Servo-Optionen](servo-settings.md)
- [Setup](setup.md)
