---
screen: IMU
title: IMU
order: 66
---

# IMU

Die Seite IMU steuert eine Bluetooth-IMU (Inertial Measurement Unit): einen WitMotion-BWT901CL-Winkelsensor auf einem Ruder. Er erreicht das Panel über Bluetooth LE (Low Energy) durch den ESP32-C6 des Boards; ohne Kabel, ohne Pairing, ohne PIN. Öffne die Seite über die Kachel IMU auf [SETUP](setup.md); das Home-Feld führt dorthin zurück. Die Seite steuert keine Ausgänge an. Ihre Einstellungen findest du im IMU-Eintrag unter [Sensoren](setup-sensors.md#die-imu). Die Wiki-Seite [Die Bluetooth-IMU](https://github.com/subtilitas/rcbench/wiki/IMU-de) beschreibt die Verbindung und die Befehle des Sensors.

![IMU, Bluetooth LE läuft, kein Sensor gebunden](view:imu)

| Bedienelement | Funktion |
|---|---|
| SENSOREN SUCHEN | öffnet die Sensorliste und startet eine Suche von 10s; verfügbar, solange Bluetooth läuft und keine Suche und keine Kalibrierung läuft |
| 2s HALTEN: BESCHLEUNIGUNG KALIBRIEREN | startet bei bestehender Verbindung die Kalibrierung des Beschleunigungssensors; halte ihn während des Countdowns von 3s still und waagerecht |
| NULLPUNKT | übernimmt die aktuellen Winkel als Nullpunkt, wenn ein Messwert vorliegt; mit WINKELQUELLE IMU werden dabei der Nullpunkt und MAX auf RUDER gelöscht |
| NULLPUNKT LÖSCHEN | zeigt die Winkel wieder relativ zum eigenen Nullpunkt des Sensors |
| IMU-OPTIONEN | öffnet den IMU-Eintrag unter [Sensoren](setup-sensors.md#die-imu); dessen Home-Feld führt hierher zurück |

Ein Druck von weniger als 2s startet keine Kalibrierung. Das Band zeigt `Halte 2s, um den Beschleunigungssensor zu kalibrieren.`.

## Die Karte

![Verbunden mit 50Hz, NULLPUNKT gesetzt](view:imu-connected)

| Zeile | Anzeige |
|---|---|
| `SENSOR: HC-06` | der bei der Suche ermittelte Name des gebundenen Sensors, `OHNE NAMEN` ohne Namen, oder `KEINER GEBUNDEN`; darunter stehen Adresse und Typ (`P` öffentlich, `R` zufällig) |
| `C6: 2.13.0` | die Firmware-Version des C6, `---`, wenn der C6 keine meldet |
| Zustand | `IMU AUS IN IMU-OPTIONEN`, `BLUETOOTH STARTET`, `KEIN BLUETOOTH AM C6`, `C6 AUSGEFALLEN: NEUSTART`, `KEIN SENSOR GEBUNDEN`, `SUCHE LÄUFT`, `VERBINDE`, `GEBUNDEN`, `VERBUNDEN 50Hz` oder `KEINE FRAMES` |
| X, Y, Z | die Winkel relativ zum Nullpunkt durch den IMU-Filter, mit einer Auflösung von 0,01°, ohne Messwert `---`; die von Servoläufen verwendete Achse erscheint in der Akzentfarbe |
| RATE, ALTER | die Zahl der Winkel-Frames in der letzten Sekunde und die Zeit seit dem jüngsten Frame auf der Seite |
| AKKU | der Akkuwert des Sensors aus Frame 0x52; über 6,0V ausgeblendet |

Ein Messwert gilt 300ms nach dem letzten Winkel-Frame als veraltet. Mit WINKELQUELLE IMU toleriert ein Servolauf eine Lücke von bis zu 300ms je 10s, wie beim AS5600 ([Servo-Optionen](servo-settings.md)). Eine längere Lücke beendet den Lauf mit `IMU antwortet nicht`, eine zweite innerhalb von 10s mit `IMU 2x in 10s ausgefallen`.

## Suchen und binden

![Zwei Sensoren gelistet, vier andere Geräte ausgeblendet](view:imu-scan)

Eine Suche dauert 10s. Sie listet die Geräte, die den Dienst des WitMotion-Sensors anbieten, das stärkste zuerst, mit Namen, Adresse und Signalstärke; das Funkmodul des Sensors heißt `HC-06`. Andere Geräte werden als ausgeblendet gezählt. Tippe auf einen Sensor, um ihn zu binden und die Liste zu schließen; das Panel verbindet sich sofort mit ihm und nach einer verlorenen Verbindung nach 1s erneut. SETUP behält Adresse und Namen und fordert zum Speichern auf. AKTUALISIEREN wiederholt die Suche; SCHLIESSEN oder das Home-Feld schließt die Liste.

| Zeile | Bedeutung |
|---|---|
| `KEINE VERBINDUNG: SENSOR AN, NAH, KEIN HANDY DRAN?` | ein Verbindungsaufbau, seine Einrichtung oder die Verbindung schlug fehl; das Panel versucht es jede 1s erneut. Ein mit dem Sensor verbundenes Handy sperrt das Panel aus |
| `KEIN WITMOTION-SENSOR: KEIN ISSC-UART-DIENST` | dem gebundenen Gerät fehlt der Dienst des Sensors; kein neuer Versuch bis zum nächsten Binden |
| `DER SENSOR HAT DEN BEFEHL NICHT ANGENOMMEN` | das Schreiben eines Befehls wurde abgelehnt oder nicht binnen 1s beantwortet; der Rest seiner Gruppe entfällt |
| `KEIN SENSOR GEFUNDEN: IST ER AN, KEIN HANDY DRAN?` | die Suche listete keinen Sensor |
| `DIE SUCHE STARTETE NICHT: CODE <n>` | Bluetooth lehnte die Suche mit diesem Code ab |

## Verwandte Seiten

- [Sensoren](setup-sensors.md)
- [Servo-Optionen](servo-settings.md)
- [Setup](setup.md)
