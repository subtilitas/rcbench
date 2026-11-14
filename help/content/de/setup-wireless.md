---
screen: none
title: Wireless
order: 62
---

# Wireless

WIRELESS auf [SETUP](setup.md) enthält die Bluetooth-LE-Geräte (Low Energy) des Panels: die IMU (Inertial Measurement Unit) WitMotion BWT901CL, das M5Stack Dial, einen Drehknopf ohne Kabel, den Leistungsknoten, einen INA228 am XT60 des ESC, und den Auswuchtknoten. Alle erreichen das Panel über den ESP32-C6 des Boards. Das erste IMU EIN, Dial EIN, Leistungsknoten EIN oder Auswuchtknoten EIN startet Bluetooth LE bis zum Neustart des Panels. Die Einstellungen gelten und werden gespeichert wie andere SETUP-Werte.

## Die linke Liste

Die linke, 220px breite Liste hat 4 Einträge: IMU, DIAL, AUSWUCHTKNOTEN und LEISTUNGSKNOTEN. Jeder zeigt seinen Namen und seinen Zustand. Tippe auf einen Eintrag, um seine Einstellungen im 754px breiten rechten Bereich zu sehen. Die Liste funktioniert wie unter [Sensoren](setup-sensors.md#die-linke-liste): ein ausgeschalteter Eintrag zeigt nur seine Einschaltzeile und den Zustand `AUS`, jeder Eintrag behält seine Scrollposition, und die Rücksetztaste nennt den Eintrag, `DIAL ZURÜCKSETZEN`.

## Die IMU

![WIRELESS auf dem IMU-Eintrag, verbunden](view:setup-wireless-imu)

![Die IMU aus: IMU und Winkelquelle](view:setup-wireless-imu-off)

Der Winkelsensor WitMotion BWT901CL wird über Bluetooth LE durch den ESP32-C6 des Boards verbunden. Suche, Zuordnung und Kalibrierung erfolgen über die Kachel [IMU](imu.md) in SETUP. Dieser Eintrag enthält seine Einstellungen.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| IMU | EIN, AUS | AUS | Bluetooth LE, die Verbindung zum gebundenen Sensor und sein Messwert; das erste EIN startet Bluetooth LE bis zum Neustart des Panels |
| Winkelquelle | AS5600, IMU | AS5600 | woher die Servoläufe ihren Winkel nehmen ([Servo-Optionen](servo-settings.md)); auch bei ausgeschalteter IMU gezeigt, damit die Quelle ohne Einschalten der IMU auf den AS5600 zurückgeht |
| IMU-Achse | X, Y, Z | X | der Winkel, den die Servoläufe und RUDER lesen; `Z: 1° MIT MAGNETOMETER` |
| IMU-Vorzeichen | +, - | + | `-` dreht diesen Winkel um |
| IMU-Rate | 10Hz, 20Hz, 50Hz, 100Hz, 200Hz | 50Hz | die Frames des Sensors je Sekunde; nur bei einer Änderung gesendet, sie gilt, nachdem der Sensor aus- und wieder eingeschaltet wurde; ab Werk läuft der Sensor mit 10Hz, und `VERBUNDEN` zeigt die Rate, mit der er läuft |
| IMU-Algorithmus | 6-AXIS, 9-AXIS | 6-AXIS | 9-AXIS nimmt das Magnetometer für Z |
| IMU-Filter | 1 bis 50 | 1 | der Tiefpass des Panels auf dem Winkel; 1 ist aus |

IMU-Filter glättet den Winkel auf dem Panel, je Frame und Achse: der Winkel rückt um 1/k der Differenz zum neuen Frame vor, mit k dem eingestellten Wert. 63% eines Sprungs erreicht er nach etwa k Frames, bei 10Hz also mit IMU-Filter 10 nach etwa 1s. Die Frames je Sekunde sind die Rate, mit der der Sensor läuft und die `VERBUNDEN` zeigt, nicht die erfragte IMU-Rate. Der IMU-Bildschirm, RUDER, AUTO und die Servoläufe lesen den gefilterten Winkel. Der erste Frame nach einem Verbinden setzt den Filter auf seinen eigenen Wert, und ein Übergang über 180° nimmt den kurzen Weg. AUTO wartet, bis der gefilterte Winkel eingeschwungen ist, bis zu 3000ms plus die Zeit, die der Wert brauchen kann ([Die Bluetooth-IMU](https://github.com/subtilitas/rcbench/wiki/IMU-de#der-filter)).

Änderungen an RATE und ALGORITHMUS werden mit dem SAVE-Befehl des Sensors (`FF AA 00 00 00`) gesendet. Bei verbundenem Sensor lautet die Ratenmeldung `RATE GESPEICHERT: SCHALTE DEN SENSOR AUS UND EIN`. Ohne Verbindung erscheint `RATE WIRD BEIM VERBINDEN GESENDET. SCHALTE DEN SENSOR DANACH AUS UND EIN`; das Panel wartet auf Frames. Während der Kalibrierung steht `RATE WIRD NACH DER KALIBRIERUNG GESENDET. SCHALTE DEN SENSOR DANACH AUS UND EIN`. Dann folgt die Änderung erst nach CALSW 0 und SAVE der Kalibrierung.

| Zustand | Wann |
|---|---|
| `AUS` | IMU AUS |
| `BLUETOOTH STARTET` | Bluetooth LE startet: die Verbindung zum C6, der Host, sein Sync |
| `KEIN BLUETOOTH AM C6` | der Host hat keinen Sync mit dem C6; er versucht es weiter |
| `C6 AUSGEFALLEN: NEUSTART` | die Verbindung zum C6 schlug fehl; WLAN und Bluetooth sind bis zum Neustart des Panels aus |
| `KEIN SENSOR GEBUNDEN` | Bluetooth läuft; kein Sensor ist gebunden |
| `SUCHE LÄUFT` | eine Suche läuft |
| `VERBINDE` | das Panel verbindet sich mit dem gebundenen Sensor und richtet seinen Dienst ein |
| `GEBUNDEN` | abonniert, noch kein Winkel-Frame; oder das gebundene Gerät ist kein WitMotion-Sensor |
| `VERBUNDEN 50Hz` | Winkel-Frames in den letzten 300ms, mit dieser Rate |
| `KEINE FRAMES` | abonniert, Frames kamen, keiner in den letzten 300ms |

## Das Dial

![WIRELESS auf dem DIAL-Eintrag, verbunden](view:setup-wireless-dial)

Das M5Stack Dial stellt das Gas auf [MOTOR & ESC](motor.md) und das Horn auf [SERVO](servo.md) LIVE, wie der eigene Drehknopf des Panels. Es schaltet nie etwas ein: ARM, SUPPLY ON, PROG 5V und jedes 2s-Halten bleiben am Panel, ebenso STOP.

| Einstellung | Bereich | Vorgabe | Wirkung |
|---|---|---|---|
| Dial | EIN, AUS | AUS | Bluetooth LE und die Verbindung zum gebundenen Dial; AUS beendet die Verbindung, und ein ausgeschaltetes Dial ist kein Verlust |
| Dial suchen | `SUCHEN` | | Tippen sucht 10s lang und listet die Dials in Reichweite; das Schalten auf ARMED beendet die Suche, `DURCH ARM BEENDET`; bei ARMED abgelehnt, mit `KEINE DIAL-SUCHE BEI ARMED: ERST AUF SAFE SCHALTEN` im Band |
| Gebundenes Dial | `KEINS` oder der Name des Dials | `KEINS` | Tippen listet die Dials der letzten Suche mit ihrem Signal in dBm; Tippen auf eines bindet es und fragt nach dem Speichern. Die Rücksetztaste behält das gebundene Dial |
| Dial-Schritt, Gas | 0.5%, 1%, 2%, 5% | 1% | das Gas, das eine Raste verstellt |
| Dial-Schritt, Servo | 0.5°, 1°, 2°, 5° | 1° | der Hornwinkel, den eine Raste verstellt |

Das Panel verbindet sich über die Adresse mit dem gebundenen Dial, ohne Suche, und nach jedem Verlust erneut. Ein Dial in Reichweite, das nicht gebunden ist, wird nie genommen.

Auf den Prüfstandsbildschirmen:

- Eine Drehung bewegt nichts, solange der Prüfstand SAFE ist, und Drehungen bei SAFE werden nicht für das ARM aufgehoben. Das erste EVENT nach dem Verbinden, ein EVENT mehr als 300ms nach dem letzten und ein Sprung um mehr als 16 Rasten bewegen nichts.
- Ein Druck auf die Bildschirmtaste des Dials bei ARMED setzt das Gas auf MOTOR & ESC auf 0% und das Horn auf SERVO auf CENTER. 1s Halten schaltet DISARM, auch solange ein ARM unterwegs ist, und beendet einen ESC-STICK-Lauf wie ABORT.
- Ein Dial, das bei ARMED verloren geht, ist ein Fehler: 1000ms ohne EVENT oder die Trennung schalten den Prüfstand auf SAFE mit allen Ausgängen aus, und das Band zeigt `DIAL BEI ARMED VERLOREN: AUF SAFE GESCHALTET, ALLE AUSGÄNGE AUS`. 500ms ohne EVENT zeigen `DIAL: 500ms KEIN EREIGNIS`. Die Uhr läuft ab dem ersten EVENT einer Verbindung, ein Verbinden ohne EVENT ist also kein Verlust.
- Bei SAFE navigiert das Dial das Menü, SETUP, die Kacheln von SERVO mit ihren SETTINGS, die Platine von OUTPUTS und jede Auswahlliste: Ein Rahmen in der Akzentfarbe markiert den Eintrag im Fokus, eine Drehung bewegt ihn, ein Druck tippt ihn an und ein Halten von 1s geht zurück wie das Home-Tag. Ein Druck auf eine Zahl öffnet ihr Ziffernfeld; eine Drehung stellt dann den Wert ein, ein Druck ist OK und ein Halten CANCEL. Ein Druck auf eine Zeile mit Ein/Aus oder Optionen stellt sie durch Drehen ein: Ein Druck behält, ein Halten stellt zurück. Kein 2s-Halten am Panel wird vom Dial aus vollendet.
- Andere Bildschirme nehmen keine Drehung des Dials an. Das Dial zeigt ihren Titel und den Zustand des Prüfstands.

| Zustand | Wann |
|---|---|
| `AUS` | Dial AUS |
| `BLUETOOTH STARTET` | Bluetooth LE startet |
| `KEIN BLUETOOTH AM C6` | der Host hat keinen Sync mit dem C6; er versucht es weiter |
| `C6 AUSGEFALLEN: NEUSTART` | die Verbindung zum C6 ist bis zum Neustart des Panels ausgefallen |
| `KEIN DIAL GEBUNDEN` | Bluetooth läuft; kein Dial ist gebunden |
| `SUCHE LÄUFT` | eine Suche läuft |
| `VERBINDE` | das Panel verbindet sich mit dem gebundenen Dial und richtet seinen Dienst ein |
| `GEBUNDEN: KEIN DIAL` | das gebundene Gerät bietet keinen Dial-Dienst |
| `VERBUNDEN, KEIN EREIGNIS` | die Verbindung steht; noch kein EVENT |
| `ANDERES PROTOKOLL 2` | die Firmware des Dials spricht eine andere Protokollversion; nichts, was es sendet, wird genommen |
| `VERBUNDEN` | EVENTs in den letzten 500ms |
| `GESTÖRT` | 500ms kein EVENT |
| `DIAL VERLOREN` | 1000ms kein EVENT, oder die Trennung |

## Der Leistungsknoten

![WIRELESS auf dem LEISTUNGSKNOTEN-Eintrag, verbunden](view:setup-wireless-pnode)

Der Leistungsknoten ist ein ESP32-C6 mit eigener Zelle, der einen INA228 am XT60 des ESC (Electronic Speed Controller, Motorregler) liest und seine Messwerte über Bluetooth LE sendet. INA228-Quelle legt fest, wessen INA228 MOTOR & ESC zeigt: den des IOmcu, verkabelt, oder den des Knotens.

| Einstellung | Bereich | Vorgabe | Wirkung |
|---|---|---|---|
| Leistungsknoten | EIN, AUS | AUS | Bluetooth LE und die Verbindung zum gebundenen Knoten; AUS beendet die Verbindung ohne Fehler |
| INA228-Quelle | WIRED, NODE | WIRED | WIRED: der INA228 des IOmcu ([Sensoren](setup-sensors.md)); NODE: die Messwerte des Leistungsknotens. Auch bei ausgeschaltetem Knoten sichtbar, damit die Quelle auf WIRED zurück kann |
| Knoten suchen | `SUCHEN` | | Tippen sucht 10s lang und listet die Leistungsknoten in Reichweite; das Schalten auf ARMED beendet die Suche, `DURCH ARM BEENDET`; bei ARMED abgelehnt, mit `KEINE KNOTEN-SUCHE BEI ARMED: ERST AUF SAFE SCHALTEN` im Band |
| Gebundener Knoten | `KEINS` oder der Name des Knotens | `KEINS` | Tippen listet die Knoten der letzten Suche mit ihrem Signal in dBm; Tippen auf einen bindet ihn und fragt nach dem Speichern. Zurücksetzen behält den gebundenen Knoten |
| Knotenakku | `3.92V`, `SCHWACH 3.41V`, `KEINE ZELLE`, `LESEFEHLER`, `NICHT VERBUNDEN` | | die Zelle des Knotens aus seinem Status jede 1s; `SCHWACH` unter 3.5V |

Mit INA228-Quelle auf NODE:

- Spannung, Strom, Leistung, Spitzen, Ladung und Energie von MOTOR & ESC sind die des Knotens, solange die IOmcu-Verbindung steht. Die eigene Telemetrie des ESC bleibt daneben. Die Spalte `source` des Logs zeigt `node`.
- Ein Messwert, der älter als 300ms ist, gilt als keiner.
- Ein ARM von MOTOR & ESC oder ESC STICK wird abgelehnt, solange Leistungsknoten AUS ist oder in den letzten 100ms kein Messwert kam, mit `ARMED gesperrt: Leistungsknoten sendet nicht. INA228-Quelle WIRED oder Knoten verbinden.` im Band. SERVO und OUTPUTS schalten unabhängig vom Knoten scharf.
- In einem Lauf von MOTOR & ESC oder ESC STICK wird eine Lücke ohne Messwert bis 300ms überbrückt, einmal in 10s. Eine längere Lücke oder eine zweite innerhalb von 10s schaltet den Prüfstand auf SAFE mit allen Ausgängen aus, und das Band zeigt `MESSWERTE DES LEISTUNGSKNOTENS BEI ARMED VERLOREN: AUF SAFE GESCHALTET, ALLE AUSGÄNGE AUS`. Ein SERVO- oder OUTPUTS-Lauf endet nicht am Knoten.
- INA228-Quelle ändert sich bei ARMED nicht.
- Die Messwerte des Knotens speisen keine Schutzfunktion: Stromgrenzen und Abschaltungen lesen ihre eigenen Sensoren.

| Zustand | Wann |
|---|---|
| `AUS` | Leistungsknoten AUS |
| `BLUETOOTH STARTET` | Bluetooth LE startet |
| `KEIN BLUETOOTH AM C6` | der Host hat keinen Sync mit dem C6; er versucht es weiter |
| `C6 AUSGEFALLEN: NEUSTART` | die Verbindung zum C6 ist bis zum Neustart des Panels ausgefallen |
| `KEIN KNOTEN GEBUNDEN` | Bluetooth läuft; kein Knoten ist gebunden |
| `SUCHE LÄUFT` | eine Suche läuft |
| `VERBINDE` | das Panel verbindet sich mit dem gebundenen Knoten und richtet seinen Dienst ein |
| `KEIN LEISTUNGSKNOTEN` | das gebundene Gerät bietet keinen Knotendienst, oder es ist ein Auswuchtknoten |
| `VERBUNDEN, KEIN MESSWERT` | die Verbindung steht; noch kein Messwert |
| `ANDERES PROTOKOLL 2` | die Firmware des Knotens spricht eine andere Protokollversion; nichts, was er sendet, wird genommen |
| `INA228-SETUP ABGELEHNT` | der Knoten nennt einen Shunt und Bereich, die das Panel nicht umrechnen kann; nichts, was er sendet, wird genommen |
| `KEIN INA228 AM KNOTEN` | der Knoten sendet, aber sein INA228 ist nicht online |
| `VERBUNDEN` | ein Messwert in den letzten 300ms |
| `300ms KEIN MESSWERT` | verbunden, 300ms kein Messwert |
| `KNOTEN VERLOREN` | die Verbindung brach nach einem Messwert ab |

## Der Auswuchtknoten

![WIRELESS auf dem AUSWUCHTKNOTEN-Eintrag, berichtet](view:setup-wireless-bal)

Der Auswuchtknoten misst Vibration und Index eines Rotors für [Balance](balance.md#der-auswuchtknoten). Er steuert nichts, daher ist sein Verlust kein Fehler.

| Einstellung | Bereich | Vorgabe | Wirkung |
|---|---|---|---|
| Auswuchtknoten | EIN, AUS | AUS | Bluetooth LE und die Verbindung zum gebundenen Knoten; AUS beendet die Verbindung |
| Knoten suchen | `SUCHEN` | | Tippen sucht 10s lang und listet die Auswuchtknoten in Reichweite, `rcbench-bal-XXXX`; das Schalten auf ARMED beendet die Suche; bei ARMED abgelehnt, mit `KEINE KNOTENSUCHE BEI ARMED: ERST AUF SAFE SCHALTEN` im Band |
| Gebundener Knoten | `KEINS` oder der Name des Knotens | `KEINS` | Tippen listet die Knoten der letzten Suche; Tippen auf einen bindet ihn und fragt nach dem Speichern. Zurücksetzen behält den gebundenen Knoten |
| Knotenakku | `3.91V`, `3.40V NIEDRIG`, `KEIN WERT`, `--` | | die Zelle des Knotens, aus dem STATUS, den der Knoten neben seinen Berichten sendet; NIEDRIG unter 3.50V |
| Testmasse | 0.01g bis 20.00g | 0.50g | die Masse, die für die Testläufe auf [MOTOR & ESC](motor.md#wuchten) aufgeklebt wird |

| Zustand | Wann |
|---|---|
| `KEIN KNOTEN GEBUNDEN` | Bluetooth läuft; kein Knoten ist gebunden |
| `GEBUNDEN: KEIN KNOTEN` | das gebundene Gerät bietet keinen Knotendienst |
| `VERBUNDEN, LIEST INFO` | die Verbindung steht; INFO wird gelesen |
| `ROLLE 1, PROTOKOLL 1` | der Knoten ist kein Auswuchtknoten oder spricht ein anderes Protokoll; nichts, was er sendet, wird übernommen |
| `VERBUNDEN, KEIN BERICHT` | seit 1000ms kein Bericht |
| `BERICHTET` | ein Bericht in den letzten 1000ms |

`AUS`, `BLUETOOTH STARTET`, `KEIN BLUETOOTH AM C6`, `C6 AUSGEFALLEN: NEUSTART`, `SUCHE LÄUFT` und `VERBINDE` lesen sich wie beim Dial.

## Verwandte Seiten

- [Setup](setup.md)
- [IMU](imu.md)
- [Sensoren](setup-sensors.md)
- [Motor und ESC](motor.md)
- [Balance](balance.md)
- [Servo](servo.md)
- [Sensorknoten](https://github.com/subtilitas/rcbench/wiki/Nodes-de#das-panel)
