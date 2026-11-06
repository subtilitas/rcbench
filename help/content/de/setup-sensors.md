---
screen: none
title: Sensoren
order: 62
---

# Sensoren

SENSORS auf [SETUP](setup.md) konfiguriert den Sensorbus des IOmcu, den INA228, bis zu vier Dreikanal-Strommonitore TI INA3221 an 0x40 bis 0x43, den AS5600-Winkelsensor und den Phasenabgriff. Hinzu kommt die Bluetooth-IMU (Inertial Measurement Unit) des Panels. Die Einstellungen gelten und werden gespeichert wie andere SETUP-Werte.

## Die linke Liste

![Zwei Bausteine bestückt, 0x40 online](view:setup-sensors)

Die linke, 220px breite Liste hat 9 Einträge: SENSORBUS, INA228, INA3221 0x40 bis 0x43, AS5600, PHASENABGRIFF und IMU. Jeder zeigt Sensorname und Status. Tippe auf einen Eintrag, um seine Einstellungen im 754px breiten rechten Bereich zu sehen.

- Ausgeschaltete Einträge zeigen nur die Einschaltzeile und den Status `AUS`. EIN blendet die übrigen Einstellungen ein.
- Die Auswahl erfolgt beim Loslassen innerhalb des Eintrags und höchstens 8px vom Druckpunkt entfernt. Längeres Ziehen wählt und scrollt nichts.
- Jeder Eintrag behält seine Scrollposition.
- Beim Eintragswechsel bleiben ungespeicherte Änderungen erhalten. SPEICHERN und die Warnung beim Verlassen funktionieren wie bei anderen Listen.
- Die Rücksetztaste nennt den Eintrag, etwa `INA228 ZURÜCKSETZEN`, und setzt alle seine Werte einschließlich verborgener Zeilen auf die Vorgaben zurück. Ein Wischen, das auf ihr endet, hat keine Wirkung.
- Der Drehknopf hat auf SETUP keine Wirkung.

| Eintrag | Zustand im Betrieb |
|---|---|
| SENSORBUS | `GPIO 16 / 17`; `KEINE PINS GESETZT`; `VOM IOMCU ABGELEHNT`; auf dem Carrier `GPIO 16 / 17 VERLÖTET` |
| INA228, INA3221 0x40 bis 0x43 | [Was ein Baustein tut](#was-ein-baustein-tut) |
| AS5600, PHASENABGRIFF | `EIN`; `VOM IOMCU ABGELEHNT` |
| IMU | [Die IMU](#die-imu) |

## SENSORBUS und INA228

![Die Zeilen des INA228](view:setup-sensors-ina228)

Der IOmcu liest einen TI INA228 im Strompfad des ESC und bis zu vier TI INA3221 am selben I2C-Bus (Inter-Integrated Circuit).

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| INA228 | EIN, AUS | AUS | der Monitor im Strompfad des ESC |
| INA228-Adresse | 0x40 bis 0x4F | 0x45 | das MATEK I2C-INA-BM ab Werk |
| INA228-Shunt | 50 bis 20000uΩ | 200 | der Shunt, über dem er misst |
| INA228-Höchststrom | 1,0 bis 300,0A, Schritte von 0,1A | 204,8 | der Strom, für den der Messbereich gesetzt ist |

Shuntwiderstand in uΩ mal Höchststrom in A darf 163840 nicht überschreiten. Das entspricht 163,84mV über dem Shunt. Jede Zeile nennt die zur anderen Einstellung passende Grenze, etwa `Bis 300.0A an 200uΩ (163.84mV)`. Für jeden zulässigen Shunt gibt es einen gültigen Strombereich: bis 546uΩ die vollen 300,0A, bei 1000uΩ 163,8A, bei 20000uΩ 8,1A. Bis 40,96mV beträgt die Auflösung 78,125nV, darüber 312,5nV.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Sensor-SDA | -1, 0, 4, 6, 14, 16, 18, 20, 26 | 16 | der GPIO (General-Purpose Input/Output) des IOmcu für SDA; -1 setzt keinen Bus |
| Sensor-SCL | SDA + 1, oder -1 | 17 | folgt Sensor-SDA; die Zeile hat keine Tasten |

- Der Bus läuft fest mit 400kHz.
- Bei einem unzulässigen Sensor-SDA-Pin zeigt das Tastenfeld die gültigen Pins statt des Bereichs. Die Eingabe GPIO 5 führt zu `Zulässige Bus-Pins: 0 4 6 14 16 18 20 26`.
- Die Adresse stoppt an den Bereichsgrenzen: `-` bei 0x40 und `+` bei 0x4F haben keine Wirkung.
- Unbeschaltete Pins führen zu nicht antwortenden Monitoren, nicht zur Meldung eines blockierten Busses.
- Der IOmcu übernimmt Konfigurationsänderungen nur bei SAFE.
- Ist der INA228 eingeschaltet und antwortet er, entfällt SIMULIERT auf der Kachel MOTOR & ESC. [MOTOR & ESC](motor.md#tabelle) zeigt dann seine Spannung und seinen Strom.
- Überwacht ein INA3221-Kanal das Ziel des Servotesters, verwendet [SERVO](servo.md#das-messgerät-der-servoschiene) seinen Strommesswert für die Servoschiene.
- Ein Messwert am oberen Bereichsende wird als übersteuert gekennzeichnet und nicht als gültiger Messwert behandelt: beim INA3221 sind dies 163,8mV über dem Shunt.
- Eine ausbleibende Monitorantwort schaltet den Prüfstand nicht auf SAFE.

![GPIO 5 für Sensor-SDA abgelehnt](view:setup-sda-why)

Pins erscheinen mit dem Präfix GPIO, etwa `GPIO 1`: im Tastenfeld, in der Einstellungszeile und in der Warnung vor ungespeicherten SETUP-Werten. Auch der Bereich im Tastenfeld nutzt diese Form, etwa `GPIO -1 bis 47`.

![Das Tastenfeld von Sensor-SDA, GPIO 1 getippt](view:setup-sda-keypad)

Das Band zeigt jeweils eine Fehlermeldung, höchstens alle 5s. Beispiele sind `SDA oder SCL der Sensoren nicht festgelegt. Prüfe SENSORS.`, `INA228 und INA3221 nutzen beide 0x40. Prüfe SENSORS.`, `INA228 antwortet nicht unter 0x45.` und `INA3221 0x40 CH1 bei 1.64A übersteuert. Anzeige zeigt nur den Grenzwert.` Konfigurationsmeldungen zu fehlenden Pins, Adresskonflikten oder abgelehnten Werten verschwinden, sobald die korrigierte Einstellung im IOmcu aktiv ist.

Der INA228 verwendet die [Statusmeldungen der Bausteine](#was-ein-baustein-tut). `VOM IOMCU ABGELEHNT` gilt auch bei abgelehnten Pins oder fehlender SENSE-Page. `ADRESSE WIE INA3221` bedeutet, dass ein bestückter INA3221 dieselbe Adresse nutzt; dann wird keiner der beiden Sensoren ausgelesen.

## Die INA3221-Bausteine

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| 0x40 bestückt bis 0x43 bestückt | EIN, AUS | AUS | ein INA3221 an dieser Adresse; das DAOKAI-Modul nimmt 0x40 mit A0 nach GND gebrückt |

Ein als bestückt aktivierter Baustein zeigt für jeden seiner drei Kanäle drei weitere Einstellungen. Bei nicht bestückten Bausteinen bleiben diese Zeilen verborgen.

| Einstellung | Bereich | Vorgabe | Wirkung |
|---|---|---|---|
| 0x40 CH1 misst | KEINE, AUSGANG 1 bis AUSGANG 8, PORT 1 bis 4, NETZTEIL | AUSGANG 1 für 0x40 CH1, sonst KEINE | Strompfad des Shunts: Ausgangskanal, Programmierport oder Versorgungsschiene. |
| 0x40 CH1 Shunt | AUS, 5,0 bis 1000,0mΩ, Schritte von 0,1mΩ | 100,0 | AUS deaktiviert die Messung. Mit 100mΩ reicht der Messbereich bis 1,64A. |
| 0x40 CH1 Grenze | AUS, 10 bis 32767mA, Pfeiltasten in Schritten von 10mA | AUS | Überschreitet der Spitzenstrom eines Fensters von 50ms die Grenze, meldet das Band dies. Der Prüfstand schaltet auf SAFE und den Netzteilausgang aus. |

- `+` schaltet den Shunt von AUS auf 5,0mΩ; `-` bei 5,0mΩ zurück auf AUS.
- Tippe auf den Shuntwert, um das Tastenfeld zu öffnen. Für 44,4mΩ statt der Vorgabe 100,0mΩ gib `4`, `4`, `.`, `4` und OK ein. Mit `-` wären 556 Schritte nötig. 0 bedeutet AUS. Werte von 0,1 bis 4,9 werden abgelehnt; das Tastenfeld bleibt mit `0 oder 5.0 bis 1000.0mΩ` bis zur nächsten Eingabe offen.
- [SERVO](servo.md#das-messgerät-der-servoschiene) und der Servotest verwenden den jede Millisekunde gemessenen Kanal. Gewählt wird der niedrigste Kanal mit eingeschaltetem Shunt auf einem bestückten Baustein, der das Testerziel überwacht: ein Ausgang bei OUTPUTS oder PORT n bei Programmierport n.
- Konfigurationsänderungen werden 500ms nach der letzten Eingabe nur bei SAFE an den IOmcu gesendet. Grenzen werden 500ms nach ihrer letzten Änderung in jedem Zustand an IOmcu 0.22.0 oder neuer gesendet.

## Auf dem rbmod-Carrier

![SENSORS auf dem Carrier, 0x40 verlötet](view:setup-sensors-rbmod)

Auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) sind Sensor-SDA und Sensor-SCL mit GPIO 16 und GPIO 17, der Abgriff-Pin mit GPIO 22 fest verlötet. Ein INA3221 ist unter 0x40 bestückt. Diese Zeilen haben weder Tasten noch Tastenfeld und zeigen `Auf Platine verlötet. Hier nicht änderbar.` Gespeicherte Pin-Einstellungen bleiben erhalten und gelten wieder auf einem von Hand verdrahteten Modul:

| Zeile | Zeigt | Shunt zwischen |
|---|---|---|
| 0x40 bestückt | EIN | |
| 0x40 CH1 misst | SCHIENE B | dem PD mini und Servoschiene B: PWM03, PWM02 |
| 0x40 CH2 misst | SCHIENE A | dem PD mini und Servoschiene A: PWM01, PWM04 |
| 0x40 CH3 misst | PROG PORTS 5V | den 5V von GPIO 21 und den Programmierports |

- Shunts und Grenzen bleiben mit den oben genannten Bereichen einstellbar.
- 0x41 bis 0x43 sind für Bausteine am Sensorbus-Stecker J5 frei.
- Jede Millisekunde wird die Schiene des niedrigsten als SERVO PWM angesteuerten GPIO gemessen: CH2 für PWM01 (GPIO 2) oder PWM04 (GPIO 1), CH1 für PWM02 (GPIO 4) oder PWM03 (GPIO 5). Bei einem Programmierport als Ziel wird CH3 gemessen.
- SERVO benennt das Messgerät nach der Schiene, etwa `INA SCHIENE A`. Eine Grenzmeldung lautet `INA3221 0x40 CH2 (SCHIENE A) über der Grenze von 0.80A.`.

## Grenzen

![Eine Grenze bei einem IOmcu älter als 0.22.0](view:setup-sensors-limit)

Bei gesetzter Grenze wertet der IOmcu jedes Messfenster von 50ms des Kanals aus. Das Panel liest Überschreitungen alle 200ms. Im Band erscheint höchstens alle 5s eine Meldung; eine anhaltende Überschreitung desselben Kanals wird alle 30s wiederholt. Gleichzeitige Überschreitungen werden reihum gemeldet. Sind alle 12 Kanäle betroffen, wird jeder innerhalb von 60s genannt.

| Band | Bedeutung |
|---|---|
| `INA3221 0x41 CH2 (PORT 1) über der Grenze von 0.80A.` | Ein Messfenster überschritt die Kanalgrenze. In Klammern steht der überwachte Strompfad, danach die bei der Prüfung geltende Grenze. |
| `Ein INA3221-Kanal überschritt seine Grenze. Die Kanalnummer ging bei der Übertragung verloren.` | Die Antwort mit der Kanalnummer ging verloren. Die Meldung folgt beim nächsten Verbindungsaufbau, nicht nach einem IOmcu-Neustart. |
| `INA3221-Grenzen abgelehnt. Prüfe SENSORS.` | Der IOmcu hat die Grenzen abgelehnt und behält seine Werte, oder er hat die Page abgelehnt. |

Eine noch nicht vom IOmcu angewendete Grenze erscheint in der Warnfarbe: bei ausstehendem Schreibvorgang, nach Ablehnung oder mit IOmcu-Versionen vor 0.22.0 (Protokoll 4.27). Diese prüfen keine Grenzen und zeigen `IOmcu vor 0.22.0: keine Grenze geprüft.`.

![Das Tastenfeld von 0x40 CH1 Shunt, 44,4 getippt](view:setup-sensors-keypad)

![3mΩ abgelehnt, der Grund statt des Bereichs](view:setup-sensors-why)

## Was ein Baustein tut

Unter dem Bausteinnamen steht sein Status:

| Zustand | Bedeutung |
|---|---|
| `ONLINE` | der IOmcu liest den Baustein |
| `SUCHT DEN BAUSTEIN` | die Einstellung ist angenommen; die ersten 2,5s |
| `ANTWORTET NICHT` | an der Adresse antwortet nichts |
| `ANDERE ID ANTWORTET` | dort antwortet etwas anderes |
| `NOCH NICHT GESCHRIEBEN` | eine Änderung wartet auf ihr Schreiben |
| `NOCH NICHT AUSGELESEN` | noch antwortet kein IOmcu |
| `VOM IOMCU ABGELEHNT` | der IOmcu hat die Einstellung abgelehnt |
| `ALLE KANÄLE AUS` | alle drei Shunts stehen auf AUS; nichts wird geschrieben |
| `INA228 AN DIESER ADRESSE` | der INA228 ist auf dieser Adresse eingeschaltet; keiner wird gelesen |
| `UNGELESEN, IOMCU < 0.22.0` | ein älterer IOmcu liest nur einen Baustein |
| `KEINE BUS-PINS` | SENSORBUS hat keinen Pin für SDA oder SCL; nichts wird gelesen |

![Ein IOmcu älter als 0.22.0](view:setup-sensors-old)

Nicht vom IOmcu ausgelesene Bausteine erscheinen mit ihren Zeilen in der Warnfarbe; die Hilfezeile nennt den Grund. Ein IOmcu vor 0.22.0 (Protokoll 4.25) liest nur einen INA3221: den Baustein, der das Servotesterziel überwacht, sonst den niedrigsten bestückten.

Beim Update von 0.21 werden die bisherigen INA3221-Einstellungen übernommen: Die alte Adresse gilt als bestückt, CH1 erhält den bisherigen Shunt und überwacht AUSGANG 1 oder PORT 1, falls der Servotester den Programmierport als Ziel hatte. Alle Grenzen beginnen bei AUS. Das nächste SPEICHERN sichert diese Werte.

## Eine Zeile, die der IOmcu nicht hält

![Zeilen, die der IOmcu nicht übernommen hat](view:setup-unheld)

Weicht ein Einstellwert vom aktiven IOmcu-Wert ab, erscheinen Rand und Wert in der Warnfarbe. Die Hilfezeile lautet `Vom IOmcu nicht übernommen.` Das gilt:

- nach einer Änderung bis zur Übernahme: 500ms nach der letzten Eingabe, bei Strommonitoren erst bei SAFE;
- nach einer Ablehnung bis zur Änderung auf einen zulässigen Wert;
- bei einem aktivierten Baustein, den der IOmcu ausgeschaltet hält, etwa wegen Sensor-SDA -1, eines INA228-Adresskonflikts mit einem bestückten INA3221 oder einer zu alten IOmcu-Version.

Solange kein IOmcu antwortet, ist keine Zeile markiert. AS5600-Mitte ist ein eigener Wert des Panels und wird nie markiert.

Bei Sensor-SDA, AS5600 und den Phasenabgriff-Einstellungen setzt diese Markierung auch den Eintragsstatus auf `VOM IOMCU ABGELEHNT`.

## Der Winkelsensor

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| AS5600 | EIN, AUS | AUS | ein magnetischer Winkelsensor AS5600 an der Abtriebswelle des Servos, auf seiner festen Adresse 0x36 am Sensorbus |
| AS5600-Mitte | 0 bis 4095 | 0 | der Zählwert mit dem Servo in Neutral; 4096 Zählwerte je Umdrehung, 0,0879° je Zählwert |

Der AS5600 benötigt IOmcu 0.15.0 oder neuer. SENSOR-MITTE in den [Servo-Optionen](servo-settings.md#prüfling) übernimmt den aktuellen Messwert als Mitte. Bei fehlendem Magneten zeigt das Band `AS5600 erkennt keinen Magneten. Prüfe den Magneten an der Servoabtriebswelle.` Auch ein zu schwacher oder zu starker Magnet wird gemeldet. Jede Meldung erscheint einmal, solange der Magnetstatus unverändert bleibt. Eine andere Einstellung löst keine Wiederholung aus; Aus- und Einschalten des Sensors schon.

## Der Phasenabgriff

![Die Zeilen des Phasenabgriffs](view:setup-tap)

Der Phasenabgriff erkennt ESC-Pieptöne an einer Motorphase. Ein IOmcu-GPIO liest die Phase über Vorwiderstand und Zener-Klemme. Als Eingang steuert er keinen Ausgang. Änderungen übernimmt der IOmcu sowohl bei SAFE als auch bei ARMED.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Phasenabgriff | EIN, AUS | AUS | die Erfassung läuft auf dem IOmcu |
| Abgriff-Pin | 0, 1, 2, 4 bis 7, 13 bis 22 | 22 | der GPIO des IOmcu; GPIO 22 ist Pad 29. Die Pins, die das RP2350-CAN herausführt und nicht belegt, ohne die ADC-Pins GPIO 26 bis GPIO 29 |
| Tiefster Ton | 50 bis 2000Hz, Schritte von 10Hz | 400 | ein tieferer Ton ist kein Piepton |
| Höchster Ton | 100 bis 6900Hz, Schritte von 50Hz | 6500 | über dem tiefsten Ton |
| Tonsprung | 0 bis 50% | 8 | ein so großer Tonhöhensprung beginnt einen neuen Piepton; 0 trennt nur an Stille |
| Abgriff-Pause | 1 bis 100ms | 3 | die Stille, die einen Piepton beendet |
| Min. Perioden | 1 bis 64 | 3 | die Tonperioden, die einen Piepton ergeben |

Der IOmcu lehnt ADC-Pins (Analog-Digital-Umsetzer), zugeordnete Ausgangspins sowie vom Sensorbus oder PD mini belegte Pins ab. Abgriff-Pause muss mindestens eine Periode von Tiefster Ton abdecken. Bei der Vorgabe 400Hz dauert sie 2,5ms, daher sind 3ms nötig. Die Meldung zu einem abgelehnten Wert verschwindet nach Übernahme eines gültigen Werts. Die Laufseite von [ESC STICK](motor-stick.md) zeigt die erkannten Pieptöne.

Vibration und Optischer Tacho besitzen gespeicherte Schlüssel, aber keinen Eintrag. In diesem Release verwendet weder eine Seite noch der IOmcu diese Werte.

## Die IMU

![SENSORS auf dem IMU-Eintrag, verbunden](view:setup-sensors-imu)

Der Winkelsensor WitMotion BWT901CL wird über Bluetooth LE durch den ESP32-C6 des Boards verbunden. Suche, Zuordnung und Kalibrierung erfolgen über die Kachel [IMU](imu.md) in SETUP. Dieser Eintrag enthält seine Einstellungen.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| IMU | EIN, AUS | AUS | Bluetooth LE, die Verbindung zum gebundenen Sensor und sein Messwert; das erste EIN startet Bluetooth LE bis zum Neustart des Panels |
| IMU-Achse | X, Y, Z | X | der Winkel, den die Servoläufe und RUDER lesen; `Z: 1° MIT MAGNETOMETER` |
| IMU-Vorzeichen | +, - | + | `-` dreht diesen Winkel um |
| IMU-Rate | 10Hz, 20Hz, 50Hz, 100Hz, 200Hz | 50Hz | die Frames des Sensors je Sekunde; nur bei einer Änderung gesendet, sie gilt, nachdem der Sensor aus- und wieder eingeschaltet wurde; ab Werk läuft der Sensor mit 10Hz, und `VERBUNDEN` zeigt die Rate, mit der er läuft |
| IMU-Algorithmus | 6-AXIS, 9-AXIS | 6-AXIS | 9-AXIS nimmt das Magnetometer für Z |
| IMU-Filter | 1 bis 50 | 1 | der Tiefpass des Panels auf dem Winkel; 1 ist aus |
| Winkelquelle | AS5600, IMU | AS5600 | woher die Servoläufe ihren Winkel nehmen ([Servo-Optionen](servo-settings.md)) |

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

## Verwandte Seiten

- [Setup](setup.md)
- [Netzteil](supply.md): die Verdrahtung des PD mini, im Tab PD MINI.
- [Servo](servo.md)
- [IMU](imu.md)
