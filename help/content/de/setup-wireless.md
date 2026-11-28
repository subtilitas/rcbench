---
screen: none
title: Wireless
order: 62
---

# Wireless

WIRELESS auf [SETUP](setup.md) enthält die Bluetooth-LE-Geräte (Low Energy) des Panels: die IMU (Inertial Measurement Unit) WitMotion BWT901CL, das M5Stack Dial, einen Drehknopf ohne Kabel, den Leistungsknoten, einen INA228 am XT60 des ESC, den Auswuchtknoten und den Etikettendrucker, einen Supvan T50M Pro. Alle erreichen das Panel über den ESP32-C6 des Boards. Das erste IMU EIN, Dial EIN, Leistungsknoten EIN oder Auswuchtknoten EIN startet Bluetooth LE bis zum Neustart des Panels. Die Einstellungen gelten und werden gespeichert wie andere SETUP-Werte.

## Die linke Liste

Die linke, 220px breite Liste hat 5 Einträge: IMU, DIAL, AUSWUCHTKNOTEN, DRUCKER und LEISTUNGSKNOTEN. Jeder zeigt seinen Namen und seinen Zustand. Tippe auf einen Eintrag, um seine Einstellungen im 754px breiten rechten Bereich zu sehen. Die Liste funktioniert wie unter [Sensoren](setup-sensors.md#die-linke-liste): ein ausgeschalteter Eintrag zeigt nur seine Einschaltzeile und den Zustand `AUS`, jeder Eintrag behält seine Scrollposition, und die Rücksetztaste nennt den Eintrag, `DIAL ZURÜCKSETZEN`.

## Die IMU

![WIRELESS auf dem IMU-Eintrag, verbunden](view:setup-wireless-imu)
![Dasselbe bei Nullpunkt, Nullpunkt löschen und Kalibrieren](view:setup-wireless-imu-cal)

![Die IMU aus: IMU und Winkelquelle](view:setup-wireless-imu-off)

Der WitMotion BWT901CL ist ein Winkelsensor für ein Ruder. Er wird über Bluetooth LE durch den ESP32-C6 des Boards verbunden: ohne Kabel, ohne Pairing, ohne PIN. Dieser Eintrag sucht und bindet ihn, zeigt seine aktuellen Winkel, setzt seinen Nullpunkt, kalibriert ihn und enthält seine Einstellungen. Nichts hier steuert einen Ausgang an. Die Wiki-Seite [Die Bluetooth-IMU](https://github.com/subtilitas/rcbench/wiki/IMU-de) beschreibt die Verbindung und die Befehle des Sensors.

Bei ausgeschalteter IMU zeigt der Eintrag nur IMU und Winkelquelle. Eingeschaltet stehen diese Zeilen zwischen Winkelquelle und IMU-Achse:

| Zeile | Wert | Was sie tut |
|---|---|---|
| Sensor suchen | `SUCHEN`, `BLUETOOTH STARTET`, `SUCHE LÄUFT`, `n GEFUNDEN`, `KEINS GEFUNDEN`, `DURCH ARM BEENDET` | Tippen öffnet [Search](#search) und sucht 10s lang nach Sensoren in Reichweite. Startet Bluetooth LE noch, wartet die Suche darauf, `BLUETOOTH STARTET`. Abgelehnt bei ARMED, `KEINE IMU-SUCHE BEI ARMED: ERST AUF SAFE SCHALTEN`; während einer Suche, `SUCHE LÄUFT: SIE ENDET SPÄTESTENS NACH 10s`; und ohne Bluetooth am C6, `BLUETOOTH NICHT BEREIT: KEINE SENSORSUCHE` |
| Gebundener Sensor | der Name des Sensors, sonst seine Adresse, sonst `KEINS` | Tippen öffnet [Search](#search) mit der Liste der letzten Suche |
| Letzter Fehler | | nur sichtbar, solange die letzte Suche, Verbindung oder ein Schreibzugriff fehlschlug; die Hilfezeile nennt den Fehler (Tabelle unten) |
| Winkel X, Winkel Y, Winkel Z | `+12.30°` | die Winkel ab dem Nullpunkt durch den IMU-Filter, 0,01°, ohne Messwert `---`. Die Achse, die IMU-Achse nennt, heißt `Winkel X: für Läufe` |
| Frames | `RATE 50.0Hz  ALTER 20ms` | die Winkel-Frames der letzten Sekunde und die Zeit seit dem jüngsten auf dem Panel; ohne Messwert `RATE 0.0Hz  KEIN WINKEL` |
| Sensorakku | `3.95V` | der Akku des Sensors aus Frame 0x52, `---` ohne Wert oder über 6,0V |
| C6-Version | `2.13.0` | die Firmware des ESP32-C6, `---`, wenn der C6 keine meldet |
| Nullpunkt | | Tippen übernimmt die aktuellen Winkel als 0°. Mit Winkelquelle IMU löscht es den Nullpunkt und MAX von RUDER. Ohne Messwert gesperrt, `NOCH KEIN WINKEL VOM SENSOR` |
| Nullpunkt löschen | | Tippen zeigt die Winkel wieder ab dem 0° des Sensors; es löscht ebenfalls den Nullpunkt und MAX von RUDER auf der IMU. Gesperrt, solange kein Nullpunkt gesetzt ist, `KEIN NULLPUNKT GESETZT` |
| Kalibrieren | `2s HALTEN: KALIBRIEREN` | 2s Halten auf dem Knopf startet die Kalibrierung des Beschleunigungssensors; halte den Sensor während der 3s still und waagerecht. Vor 2s losgelassen, startet nichts, und das Band zeigt `Halte 2s, um den Beschleunigungssensor zu kalibrieren.`. Gesperrt, solange der Sensor keine Frames sendet, `DER SENSOR SENDET KEINE FRAMES`, und während sie läuft, `HALTE DEN SENSOR 3s STILL UND WAAGERECHT` mit dem Countdown. Ihr Ende zeigt das Band: `BESCHLEUNIGUNGSSENSOR KALIBRIERT UND GESPEICHERT` oder `KALIBRIERUNG OHNE FRAMES BEENDET. VERSUCHE ES ERNEUT` |

Ein Messwert gilt 300ms nach dem letzten Winkel-Frame als veraltet. Mit Winkelquelle IMU toleriert ein Servolauf eine Lücke von bis zu 300ms je 10s, wie beim AS5600 ([Servo-Optionen](servo-settings.md)). Eine längere Lücke beendet den Lauf mit `IMU antwortet nicht`, eine zweite innerhalb von 10s mit `IMU 2x in 10s ausgefallen`.

![Kein Sensor gebunden nach einer Suche ohne Fund](view:setup-wireless-imu-error)

| Letzter Fehler | Bedeutung |
|---|---|
| `KEINE VERBINDUNG: SENSOR AN, NAH, KEIN HANDY DRAN?` | ein Verbindungsaufbau, seine Einrichtung oder die Verbindung schlug fehl; das Panel versucht es jede 1s erneut. Ein mit dem Sensor verbundenes Handy sperrt das Panel aus |
| `KEIN WITMOTION-SENSOR: KEIN ISSC-UART-DIENST` | dem gebundenen Gerät fehlt der Dienst des Sensors; kein neuer Versuch bis zum nächsten Binden |
| `DER SENSOR HAT DEN BEFEHL NICHT ANGENOMMEN` | das Schreiben eines Befehls wurde abgelehnt oder nicht binnen 1s beantwortet; der Rest seiner Gruppe entfällt und geht bei der nächsten Verbindung erneut |
| `KEIN SENSOR GEFUNDEN: IST ER AN, KEIN HANDY DRAN?` | die Suche listete keinen Sensor |
| `KEIN ANDERER SENSOR: DER GEBUNDENE IST VERBUNDEN` | die Suche listete keinen Sensor, während der gebundene verbunden ist; ein verbundener Sensor sendet keine Advertisements |
| `DIE SUCHE STARTETE NICHT: CODE <n>` | Bluetooth lehnte die Suche mit diesem Code ab |

Die Einstellungen:

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| IMU | EIN, AUS | AUS | Bluetooth LE, die Verbindung zum gebundenen Sensor und sein Messwert; das erste EIN startet Bluetooth LE bis zum Neustart des Panels |
| Winkelquelle | AS5600, IMU | AS5600 | woher die Servoläufe ihren Winkel nehmen ([Servo-Optionen](servo-settings.md)); auch bei ausgeschalteter IMU gezeigt, damit die Quelle ohne Einschalten der IMU auf den AS5600 zurückgeht |
| IMU-Achse | X, Y, Z | X | der Winkel, den die Servoläufe und RUDER lesen; `Z: 1° MIT MAGNETOMETER` |
| IMU-Vorzeichen | +, - | + | `-` dreht diesen Winkel um |
| IMU-Rate | 10Hz, 20Hz, 50Hz, 100Hz, 200Hz | 50Hz | die Frames des Sensors je Sekunde; nur bei einer Änderung gesendet, sie gilt, nachdem der Sensor aus- und wieder eingeschaltet wurde; ab Werk läuft der Sensor mit 10Hz, und `VERBUNDEN` zeigt die Rate, mit der er läuft |
| IMU-Algorithmus | 6-AXIS, 9-AXIS | 6-AXIS | 9-AXIS nimmt das Magnetometer für Z |
| IMU-Filter | 1 bis 50 | 1 | der Tiefpass des Panels auf dem Winkel; 1 ist aus |
| IMU-Einschwingzeit | 0,5 bis 10,0s, Schritte von 0,5s | 3,0s | RUDER AUTO an der IMU: so lange muss der Winkel halten, bevor eine Messung ihn nimmt |
| IMU-Genauigkeit | 0,05 bis 1,00°, Schritte von 0,05° | 0,10° | RUDER AUTO an der IMU: die Spanne, die der Winkel in dieser Zeit überstreichen darf |

IMU-Filter glättet den Winkel auf dem Panel, je Frame und Achse: der Winkel rückt um 1/k der Differenz zum neuen Frame vor, mit k dem eingestellten Wert. 63% eines Sprungs erreicht er nach etwa k Frames, bei 10Hz also mit IMU-Filter 10 nach etwa 1s. Die Frames je Sekunde sind die Rate, mit der der Sensor läuft und die `VERBUNDEN` zeigt, nicht die erfragte IMU-Rate. Die Winkelzeilen, RUDER, AUTO und die Servoläufe lesen den gefilterten Winkel. Der erste Frame nach einem Verbinden setzt den Filter auf seinen eigenen Wert, und ein Übergang über 180° nimmt den kurzen Weg. AUTO wartet, bis der gefilterte Winkel eingeschwungen ist, bis zu 3000ms plus die Zeit, die der Wert brauchen kann ([Die Bluetooth-IMU](https://github.com/subtilitas/rcbench/wiki/IMU-de#der-filter)).

IMU-Einschwingzeit und IMU-Genauigkeit sind die Regel, die eine Messung von RUDER AUTO an der IMU einhält: Sie nimmt den Winkel, sobald die Messwerte über die letzte Einschwingzeit höchstens die Genauigkeit überspannen. Die BWT901CL im 6-AXIS kriecht nach einem Servoschritt noch 5s bis 10s; mit den Vorgaben liegt der genommene Winkel höchstens 0,07° von dem Wert, auf dem der Messwert zur Ruhe kommt. Eine längere Einschwingzeit oder eine kleinere Genauigkeit macht jede Messung langsamer und genauer; die Wartezeit der Messung wächst mit der Einschwingzeit ([Die Einschwingregel](https://github.com/subtilitas/rcbench/wiki/IMU-de#die-einschwingregel)).

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
| Dial suchen | `SUCHEN` | | Tippen öffnet [SEARCH](#search) und sucht 10s lang nach den Dials in Reichweite; das Schalten auf ARMED beendet die Suche, `DURCH ARM BEENDET`; bei ARMED abgelehnt, mit `KEINE DIAL-SUCHE BEI ARMED: ERST AUF SAFE SCHALTEN` im Band |
| Gebundenes Dial | `KEINS` oder der Name des Dials | `KEINS` | Tippen öffnet [SEARCH](#search) mit der Liste der letzten Suche. Die Rücksetztaste behält das gebundene Dial |
| Dial-Schritt, Gas | 0.5%, 1%, 2%, 5% | 1% | das Gas, das eine Raste verstellt |
| Dial-Schritt, Servo | 0.5°, 1°, 2°, 5° | 1° | der Hornwinkel, den eine Raste verstellt |

Das Panel verbindet sich über die Adresse mit dem gebundenen Dial, ohne Suche, und nach jedem Verlust erneut. Ein Dial in Reichweite, das nicht gebunden ist, wird nie genommen.

Auf den Prüfstandsbildschirmen:

- Eine Drehung bewegt nichts, solange der Prüfstand SAFE ist, und Drehungen bei SAFE werden nicht für das ARM aufgehoben. Das erste EVENT nach dem Verbinden, ein EVENT mehr als 300ms nach dem letzten und ein Sprung um mehr als 16 Rasten bewegen nichts.
- Ein Druck auf die Bildschirmtaste des Dials bei ARMED setzt das Gas auf MOTOR & ESC auf 0% und das Horn auf SERVO auf CENTER. 1s Halten schaltet DISARM, auch solange ein ARM unterwegs ist, und beendet einen ESC-STICK-Lauf wie ABORT. Auf SERVO beendet es einen Test, einen Lauf von LIMIT RUN und einen Lauf von RUDER AUTO in jeder Phase, auch solange der Lauf den Prüfstand für seine Programmiersitzung auf SAFE hält.
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

Der Leistungsknoten ist ein ESP32-C6 mit eigener Zelle, der ein INA228-Board in der Zuleitung des ESC (Electronic Speed Controller, Motorregler) liest und seine Messwerte über Bluetooth LE sendet. Der Knoten findet den INA228 auf jeder Adresse von 0x40 bis 0x4F. INA228-Quelle legt fest, wessen INA228 MOTOR & ESC zeigt: den des IOmcu, verkabelt, oder den des Knotens.

| Einstellung | Bereich | Vorgabe | Wirkung |
|---|---|---|---|
| Leistungsknoten | EIN, AUS | AUS | Bluetooth LE und die Verbindung zum gebundenen Knoten; AUS beendet die Verbindung ohne Fehler |
| INA228-Quelle | WIRED, NODE | WIRED | WIRED: der INA228 des IOmcu ([Sensoren](setup-sensors.md)); NODE: die Messwerte des Leistungsknotens. Auch bei ausgeschaltetem Knoten sichtbar, damit die Quelle auf WIRED zurück kann |
| Knoten suchen | `SUCHEN` | | Tippen öffnet [SEARCH](#search) und sucht 10s lang nach den Leistungsknoten in Reichweite; das Schalten auf ARMED beendet die Suche, `DURCH ARM BEENDET`; bei ARMED abgelehnt, mit `KEINE KNOTENSUCHE BEI ARMED: ERST AUF SAFE SCHALTEN` im Band |
| Gebundener Knoten | `KEINS` oder der Name des Knotens | `KEINS` | Tippen öffnet [SEARCH](#search) mit der Liste der letzten Suche. Zurücksetzen behält den gebundenen Knoten |
| Knotenname | der Name des Knotens, `ALTE FIRMWARE`, `NICHT VERBUNDEN` | | der im Knoten gespeicherte Name, bis 16 Zeichen A bis Z, 0 bis 9, Leerzeichen, `-`, `_`, `.`. Tippen öffnet die Tastatur; OK schreibt ihn zum Knoten, ein leerer Name löscht ihn. Gebundener Knoten und Search zeigen ihn. Nur bei SAFE und verbunden |
| Knotenakku | `3.92V`, `SCHWACH 3.41V`, `KEINE ZELLE`, `LESEFEHLER`, `NICHT VERBUNDEN` | | die Zelle des Knotens aus seinem Status jede 1s; `SCHWACH` unter 3.5V |
| INA228 am Knoten | `0x40, 500uΩ`, `NICHT GEFUNDEN`, `NICHT VERBUNDEN` | | die Adresse, auf der der Knoten seinen INA228 gefunden hat, und der Shunt, den der Knoten hält, aus seiner INFO nach jedem Verbinden und jeder Shunt-Änderung |
| INA228-Adresse | AUTO, 0x40 bis 0x4F | AUTO | AUTO: der Knoten sucht 0x40 bis 0x4F ab; eine feste Adresse: der Knoten fragt nur diese. Jeder Schritt mit `<` oder `>` schreibt sie zum Knoten. Nur bei SAFE und verbunden |
| Knoten-Shunt | 50 bis 10000µΩ | 200µΩ | der Shunt des INA228-Boards am Knoten; der des MATEK I2C-INA-BM hat 200µΩ. Das Panel schreibt ihn nach jedem Verbinden und jeder Änderung zum Knoten, wenn der Knoten einen anderen hält; der Knoten behält ihn über Neustarts. Der Messbereich ist 40.96mV über dem Shunt: 204.8A bei 200µΩ, 81.92A bei 500µΩ, 4.096A bei 10000µΩ. Jede Änderung löscht die Ladungs- und Energiesummen des Knotens. Das eigene SETUP des Anzeigeknotens setzt den Shunt auch; die letzte Änderung gewinnt, und eine Änderung am Knoten kommt bei der nächsten Verbindung in diese Einstellung. Ändert sich bei ARMED nicht. Ein Knoten, dessen Firmware keinen Shunt nimmt: `LEISTUNGSKNOTEN NIMMT KEINEN SHUNT: FIRMWARE AKTUALISIEREN` im Band |

![WIRELESS auf dem LEISTUNGSKNOTEN-Eintrag, bis INA228 am Knoten, INA228-Adresse und Knoten-Shunt gescrollt](view:setup-wireless-pnode-shunt)

![Knotenname getippt: die Namenstastatur](view:setup-wireless-pnode-name)

Mit INA228-Quelle auf NODE:

- Spannung, Strom, Leistung, Spitzen, Ladung und Energie von MOTOR & ESC sind die des Knotens, solange die IOmcu-Verbindung steht. Die eigene Telemetrie des ESC bleibt daneben. Die Spalte `source` des Logs zeigt `node`.
- Ein Messwert, der älter als 300ms ist, gilt als keiner.
- Mit Leistungsknoten EIN und einem gebundenen Knoten trägt die Kachel MOTOR & ESC im [Menü](overview.md#die-marke-simuliert) keine Marke SIMULIERT, auch bei ausgeschaltetem INA228 des IOmcu und ohne Verbindung zum Knoten.
- Ein ARM von MOTOR & ESC oder ESC STICK wird abgelehnt, solange Leistungsknoten AUS ist oder in den letzten 100ms kein Messwert kam, mit `ARMED gesperrt: Leistungsknoten sendet nicht. INA228-Quelle WIRED oder Knoten verbinden.` im Band. SERVO und OUTPUTS schalten unabhängig vom Knoten auf ARMED.
- In einem Lauf von MOTOR & ESC oder ESC STICK wird eine Lücke ohne Messwert bis 300ms überbrückt, einmal in 10s. Eine längere Lücke oder eine zweite innerhalb von 10s schaltet den Prüfstand auf SAFE mit allen Ausgängen aus, und das Band zeigt `MESSWERTE DES LEISTUNGSKNOTENS BEI ARMED VERLOREN: AUF SAFE GESCHALTET, ALLE AUSGÄNGE AUS`. Ein SERVO- oder OUTPUTS-Lauf endet nicht am Knoten.
- INA228-Quelle, Knoten-Shunt, Knotenname und INA228-Adresse ändern sich bei ARMED nicht: `KEINE KNOTENEINSTELLUNG BEI ARMED: ERST AUF SAFE SCHALTEN`. Ein Knoten, dessen Firmware keinen Namen kennt: `KNOTEN NIMMT KEINEN NAMEN UND KEINE ADRESSE: FIRMWARE AKTUALISIEREN`.
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
| Knoten suchen | `SUCHEN` | | Tippen öffnet [SEARCH](#search) und sucht 10s lang nach den Auswuchtknoten in Reichweite, `rcbench-bal-XXXX`; das Schalten auf ARMED beendet die Suche; bei ARMED abgelehnt, mit `KEINE KNOTENSUCHE BEI ARMED: ERST AUF SAFE SCHALTEN` im Band |
| Gebundener Knoten | `KEINS` oder der Name des Knotens | `KEINS` | Tippen öffnet [SEARCH](#search) mit der Liste der letzten Suche. Zurücksetzen behält den gebundenen Knoten |
| Knotenname | der Name des Knotens, `ALTE FIRMWARE`, `NICHT VERBUNDEN` | | der im Knoten gespeicherte Name, bis 16 Zeichen A bis Z, 0 bis 9, Leerzeichen, `-`, `_`, `.`. Tippen öffnet die Tastatur; OK schreibt ihn zum Knoten, ein leerer Name löscht ihn. Gebundener Knoten und Search zeigen ihn. Nur bei SAFE und verbunden |
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

## Der Etikettendrucker

![WIRELESS auf dem DRUCKER-Eintrag, die Rolle gelesen](view:printer-idle)
![Ein Etikett, beendet mit DECKEL OFFEN](view:printer-error)

Der Drucker druckt die Servo-Etiketten von DRUCKEN im SERVO-ETIKETT ([Servo-Einstellungen](servo-settings.md)). Er hat keinen Schalter: das Panel verbindet sich für eine Anfrage und beendet die Verbindung 30s nach seiner letzten Antwort. Eine Anfrage wird während eines Geräte-Updates abgelehnt, ein Update während einer Anfrage oder einer Druckersuche. Der Drucker zeigt keinen Dienst an: eine Suche listet die Geräte, deren öffentliche Adresse mit `A4:93:40` beginnt.

| Zeile | Was sie zeigt oder tut |
|---|---|
| Drucker suchen | `SUCHEN`, `SUCHE LÄUFT`, `n GEFUNDEN`, `KEINS GEFUNDEN`, `DURCH ARM BEENDET`; Tippen öffnet [Search](#search) und sucht 10s. Abgelehnt bei ARMED, während eines Drucks oder Geräte-Updates und während einer Suche, der Grund im Band |
| Gebundener Drucker | sein Name, sonst seine Adresse, sonst `KEINS`; Tippen öffnet [Search](#search) mit der Liste der letzten Suche |
| Adresse | die gebundene Adresse, `A4:93:40:30:FA:71 P` |
| Zustand | `NICHT GEBUNDEN`, `BEREIT`, `VERBUNDEN`, die Phase der Anfrage (`SENDET 3/5`) oder ihr Ergebnis (`GEDRUCKT, 293 ÜBRIG`, `DECKEL OFFEN`) |
| Rolle | `40x30mm ABST. 3mm`, wie der Drucker sie meldet, `KEIN ETIKETT` |
| Etiketten übrig | der Zähler des Druckers |
| Drucker lesen | liest Rolle und übrige Etiketten |
| Testetikett | druckt das Testetikett des Bench; nur bei SAFE |

Drucker lesen und Testetikett sind ohne gebundenen Drucker, während einer Anfrage und während eines Updates gesperrt; die Hilfezeile nennt den Grund. Öffnen des Eintrags mit gebundenem Drucker liest seine Rolle einmal. Eine Anfrage, die mit einem Fehler endet, zeigt ihren Text im Band. DRUCKER ZURÜCKSETZEN behält die Bindung.

## Search

![SEARCH beim Leistungsknoten, zwei Knoten gefunden](view:setup-wireless-search)
![SEARCH für den Drucker während einer Suche](view:printer-search)

![SEARCH bei der IMU, zwei Sensoren gefunden, vier andere Geräte ausgeblendet](view:setup-wireless-imu-search)

SEARCH findet und bindet die IMU, das Dial, den Leistungsknoten, den Auswuchtknoten oder den Etikettendrucker. Sensor suchen, Dial suchen, Knoten suchen und Drucker suchen öffnen es und suchen 10s lang; Gebundener Sensor, Gebundenes Dial, Gebundener Knoten und Gebundener Drucker öffnen es mit der Liste der letzten Suche. Die Suche der IMU listet die Geräte, die den Dienst des WitMotion-Sensors anbieten; das Funkmodul des Sensors heißt `HC-06`. Bindung und BINDUNG LÖSEN des Druckers werden während eines Drucks oder Geräte-Updates abgelehnt. Eine abgelehnte Suche nennt den Grund im Band, und SEARCH öffnet mit der letzten Liste. Das Home-Feld führt zurück zu WIRELESS auf den Eintrag des Geräts.

- Die Zeile über der Liste: `SUCHE LÄUFT: NOCH 7s`, `2 GEFUNDEN: ZUM BINDEN EINES ANTIPPEN`, `KEINS GEFUNDEN`, `DURCH ARM BEENDET` oder `NOCH KEINE SUCHE: AKTUALISIEREN ANTIPPEN`.
- Die Liste: bis zu 8 Geräte, das stärkste zuerst, jedes mit Name oder Adresse und Signal in dBm. Das gebundene ist hervorgehoben. Die Liste leert sich beim Start einer Suche und füllt sich an ihrem Ende.
- Rechts: die Art des Geräts, seine Verbindung, das gebundene Gerät mit seiner Adresse unter dem Namen (`P` öffentlich, `R` zufällig) und AKTUALISIEREN, eine neue Suche von 10s. AKTUALISIEREN ist grau, solange eine Suche läuft. Bei der IMU die Geräte, die die letzte Suche hörte und nicht listete: `4 ANDERE GERÄTE AUSGEBLENDET`.

![Die Bindungsfrage](view:setup-wireless-search-bind)

Tippe auf ein Gerät, um es zu binden: Die Frage zeigt Name, Adresse und Signal und das Gerät, das es ersetzt. BINDEN bindet es; SETUP behält es und fordert Speichern an, und SEARCH bleibt und hebt es hervor, sobald es gebunden ist. ABBRECHEN oder das Home-Feld binden nichts. BINDEN bindet nichts, solange eine Suche läuft, `SUCHE LÄUFT: DEN KNOTEN NACH DEM ENDE WÄHLEN`, oder wenn eine neue Suche die Liste unter der Frage änderte, `KNOTEN NICHT GEBUNDEN: LISTE GEÄNDERT, ERNEUT WÄHLEN`; die Texte des Dials nennen das Dial, die der IMU den Sensor. BINDEN der IMU bindet nichts, während ihre Kalibrierung läuft oder Bluetooth LE startet, `JETZT KEINE BINDUNG: KALIBRIERUNG LÄUFT ODER BLUETOOTH STARTET`. Nach einem Binden verbindet sich das Panel sofort mit dem Sensor und nach einer verlorenen Verbindung nach 1s erneut.

### Bindung lösen

`2s HALTEN: BINDUNG LÖSEN`, unter AKTUALISIEREN, vergisst das gebundene Gerät. Halte es 2s: Die Verbindung endet, SETUP behält kein Gerät und fordert Speichern an, und die Karte zeigt `KEINS`. Vor 2s losgelassen, löst es nichts. Ist nichts gebunden, ist der Knopf grau, und Tippen zeigt `NICHTS GEBUNDEN: KEINE BINDUNG ZU LÖSEN`. Um das Gerät wieder zu nutzen, suche und binde es.

ARMED lehnt es nicht ab. Ein Lauf, der das Gerät nutzt, trifft es wie eine verlorene Verbindung:

- Dial: sofort verloren; bei ARMED ist das ein Fehler, und jeder Ausgang geht aus.
- Leistungsknoten: Ein Lauf, der seinen INA228 liest, bekommt keinen Messwert und endet nach 300ms als Fehler, jeder Ausgang aus.
- Auswuchtknoten: Seine Messwerte veralten.
- IMU: Ein Lauf, der ihren Winkel liest, trifft eine Lücke über 300ms und endet mit `IMU antwortet nicht`.

## Verwandte Seiten

- [Setup](setup.md)
- [Die Bildschirmhilfe](band.md#die-bildschirmhilfe)
- [Sensoren](setup-sensors.md)
- [Motor und ESC](motor.md)
- [Balance](balance.md)
- [Servo](servo.md)
- [Sensorknoten](https://github.com/subtilitas/rcbench/wiki/Nodes-de#das-panel)
