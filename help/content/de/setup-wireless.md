---
screen: none
title: Wireless
order: 62
---

# Wireless

WIRELESS auf [SETUP](setup.md) enthält die Bluetooth-LE-Geräte (Low Energy) des Panels: die IMU (Inertial Measurement Unit) WitMotion BWT901CL und das M5Stack Dial, einen Drehknopf ohne Kabel. Beide erreichen das Panel über den ESP32-C6 des Boards. Das erste IMU EIN oder Dial EIN startet Bluetooth LE bis zum Neustart des Panels. Die Einstellungen gelten und werden gespeichert wie andere SETUP-Werte.

## Die linke Liste

Die linke, 220px breite Liste hat 2 Einträge: IMU und DIAL. Jeder zeigt seinen Namen und seinen Zustand. Tippe auf einen Eintrag, um seine Einstellungen im 754px breiten rechten Bereich zu sehen. Die Liste funktioniert wie unter [Sensoren](setup-sensors.md#die-linke-liste): ein ausgeschalteter Eintrag zeigt nur seine Einschaltzeile und den Zustand `AUS`, jeder Eintrag behält seine Scrollposition, und die Rücksetztaste nennt den Eintrag, `DIAL ZURÜCKSETZEN`.

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

## Verwandte Seiten

- [Setup](setup.md)
- [IMU](imu.md)
- [Sensoren](setup-sensors.md)
- [Motor und ESC](motor.md)
- [Servo](servo.md)
