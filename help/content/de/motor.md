---
screen: MOTOR
title: Motor und ESC
order: 10
---

# Motor und ESC

MOTOR & ESC steuert einen ESC (Electronic Speed Controller) und zeichnet Spannung, Strom, Leistung und Drehzahl eines Laufs über der Zeit auf. Öffne die Seite über die Kachel MOTOR & ESC im [Menü](overview.md). Sie beginnt immer mit LIVE.

## Der Aufbau

Die obere Leiste bietet zwei Aufgaben: LIVE und PROGRAMMIEREN. Tippe auf eine Aufgabe, um zu wechseln. LIVE behält dabei die gewählte Ansicht, GRAFIK, TABELLE oder WUCHTEN. LOG UMBENENNEN rechts in der Leiste gibt dem letzten MOTOR-Log ein Label ([Das Log eines Laufs](#das-log-eines-laufs)).

| Aufgabe | Linke Spalte |
|---|---|
| LIVE | Grafik oder Tabelle und Gasregler |
| PROGRAMMIEREN | Liste der ESC-Programmierer ([Programmieren](#programmieren)) |

Die rechte Spalte ist bei beiden Aufgaben gleich: die Temperaturen in einer Zeile, vier Anzeigen und die Karte STEUERUNG mit Summen in der Titelzeile, ZIEL und SPITZENWERTE ZURÜCKSETZEN. Eine unter PROGRAMMIEREN geöffnete Sitzung verdeckt beide Spalten und die Fußleiste.

Die Fußleiste unten hat drei Felder:

| Feld | Was es tut |
|---|---|
| MOTOR & ESC | der Name der Seite; darunter Protokoll und Pins des Gases, `KEIN MOTOR-PIN` in der Warnfarbe ohne Pin |
| ARM / DISARM | 2s halten für ARMED, tippen für SAFE ([Ein Lauf](#ein-lauf)) |
| OPTIONEN | öffnet die [Motor-Optionen](motor-settings.md): Akku, Motor und Gasausgang; grau, solange der Prüfstand nicht SAFE ist |

Das Home-Feld oben links führt jeweils eine Ebene zurück:

| Ausgangspunkt | Ziel des Home-Felds |
|---|---|
| Tastenfeld über der Optionsliste | die Liste; der Wert bleibt unverändert |
| die Optionsliste | LIVE oder PROGRAMMIEREN; vorher die Frage nach einer ungespeicherten Änderung |
| Sitzung auf PROGRAMMIEREN | eine Ebene höher in der Sitzung, dann zur Liste von PROGRAMMIEREN |
| Tastatur von LOG UMBENENNEN | LIVE oder Liste von PROGRAMMIEREN; nichts wird umbenannt |
| LIVE oder Liste von PROGRAMMIEREN | Menü; der Prüfstand schaltet auf SAFE |

## Programmieren

![Die Liste von PROGRAMMIEREN](view:motor-program)

PROGRAMMIEREN bietet zwei Wege zur ESC-Programmierung. `ESCs per Gasknüppel` unter STICK-MENÜ öffnet [ESC STICK](motor-stick.md) und steuert die als Gas zugeordneten Pins. `AM32, Bluejay, BLHeli_S, ESCape32` unter SIGNALLEITUNG öffnet die [Programmierung über die Signalleitung](motor-wire.md). Diese Zeile bleibt gedimmt, bis der IOmcu die benötigte Fähigkeit meldet. Solange der Prüfstand keinen Stick-Lauf tragen kann, nennt die Gasknüppel-Zeile den ersten Grund in der Warnfarbe, etwa `START BRAUCHT PD MINI` oder `KEINE PIEPTONQUELLE`, und öffnet ESC STICK trotzdem; die Herstellerliste zeigt dasselbe Kennzeichen in ihrer Kopfzeile.

ESC STICK verdeckt beide Spalten und die Fußleiste. Anstelle der Reiter erscheint ein einzeiliger Ausgangsstreifen:

`ZIEL DSHOT300 GPIO 0 GPIO 1   NETZTEIL 7.60V  1.00A   SAFE`

- ZIEL: Protokoll und Pins des Laufs. Die Warnfarbe erscheint, wenn kein Pin zugeordnet ist oder die Zuordnung noch nicht ausgelesen wurde.
- NETZTEIL: Ausgangsmesswerte des PD mini, `NETZTEIL EIN` ohne Messwerte, `NETZTEIL AUS` oder `NETZTEIL --`, wenn das Netzteil nicht antwortet.
- SAFE oder ARMED: Zustand des Prüfstands.

Solange ESC STICK geöffnet ist, lassen sich ARM und die Reiter nicht drücken. Beim Öffnen schaltet der Prüfstand bei Bedarf auf SAFE. Das Verlassen von MOTOR & ESC beendet den Lauf, schaltet auf SAFE und schließt ESC STICK.

## Vor dem ersten Lauf

![Verbindung steht, noch kein Lauf bei ARMED, kein Motor-Pin zugeordnet](view:motor-idle)

- Bis zum ersten Schalten auf ARMED lautet die Beschriftung der Grafik `TELEMETRIE INAKTIV`; die Grafik zeigt `Noch kein Lauf aufgezeichnet.`.
- Bei SAFE steht unter dem Balken `ZUERST ARM`. Der Balken und die Tasten `-1` und `+1` sind gedimmt und nehmen keine Eingabe an.
- Die Anzeigen zeigen `---`, solange kein Wert gemeldet wurde.
- ZIEL zeigt `KEIN MOTOR-PIN` in der Warnfarbe, wenn die Zuordnung ausgelesen wurde und kein Motor-Pin zugeordnet ist. Ordne unter [Ausgänge](outputs.md) einen Pin zu.

## ZIEL

ZIEL oben auf der Karte STEUERUNG nennt die Gasausgänge nach Protokoll und Pin: `DSHOT300 GPIO 0 GPIO 1`. Es sind die Pins, die unter [Ausgänge](outputs.md) als MOTOR PWM oder mit einem DShot-Protokoll zugeordnet sind. Das Wertefeld fasst 25 Zeichen. Passt die Pin-Liste nicht hinein, ersetzt eine Anzahl die übrigen Pins: `DSHOT300 GPIO 0 GPIO 1 +6`.

| ZIEL zeigt | Bedingung |
|---|---|
| `DSHOT300 GPIO 0 GPIO 1` | Protokoll und Gas-Pins in Pin-Reihenfolge |
| `GEMISCHT GPIO 0 GPIO 4` | Die Gas-Pins nutzen mindestens zwei Protokolle, oder einer nutzt eine Pulsrate, die keinem Protokoll auf OUTPUTS entspricht. |
| `GPIO 0 GPIO 1` | Alle Gas-Pins nutzen Pulsraten, die keinem Protokoll auf OUTPUTS entsprechen. |
| `KEIN MOTOR-PIN` | Zuordnung ausgelesen, kein Motor-Pin zugeordnet; Warnfarbe |
| `PINS NICHT AUSGELESEN` | Verbindung unterbrochen, Pages nicht ausgelesen oder keine Pin-Karte für die Platine vorhanden; Warnfarbe |

Tippe auf ZIEL, um [Ausgänge](outputs.md) beim zugeordneten Gas-Protokoll zu öffnen. Bei mehreren Protokollen oder unbekanntem Protokoll öffnet sich die Übersicht. Wie bei jedem Verlassen von MOTOR & ESC schaltet der Prüfstand auf SAFE. Das Home-Feld auf OUTPUTS führt zu der Aufgabe zurück, von der du gekommen bist.

ZIEL erscheint gesperrt und nimmt keinen Tipp an:

- solange der Prüfstand nicht SAFE ist: Er ist ARMED, oder eine ARM- oder DISARM-Anfrage wartet auf Antwort;
- solange ARM gehalten wird;
- solange die [Servoprogrammierung](servo-programmer.md) einen Port belegt.

ARM schaltet die gesamte Gruppe der Gasausgänge auf ARMED, auch Pins, die hier nicht genannt sind.

## Ein Lauf

![Ein Lauf bei ARMED ohne IOmcu: SIM und das Wasserzeichen SIMULATION](view:motor)

1. Halte ARM 2s. Die Füllung geht von Grün nach Rot über. Am Ende des Übergangs schaltet der Prüfstand auf ARMED. Lässt du früher los, ändert sich der Zustand nicht, und das Alarmband zeigt `Halte 2s, um auf ARMED zu schalten.`.
2. Ziehe den Gasregler. Er folgt dem Weg des Fingers und springt nicht zur Stelle, an der du aufsetzt. `-1` und `+1` ändern den Wert um einen Prozentpunkt.
3. Tippe auf DISARM oder drücke STOP, um den Lauf zu beenden. Beide stoppen den Ausgang sofort, ohne Rampe, und setzen Gas auf 0%.

Bei ARMED lautet die Beschriftung der Grafik `TELEMETRIE LIVE`; am rechten Rand steht `JETZT`. ARM erscheint gesperrt, solange die [Servoprogrammierung](servo-programmer.md) einen Port belegt.

| Bedienelement | SAFE | ARMED |
|---|---|---|
| Balken | gesperrt und gedimmt | bewegt den Gasregler um den Weg des Fingers |
| `-1`, `+1` | gesperrt und gedimmt | ändern den Wert um einen Prozentpunkt |
| Drehknopf | gesperrt; Drehungen werden verworfen | ändert Gas ausgehend vom aktuellen Wert |
| ARM | schaltet nach 2s Halten auf ARMED | DISARM: schaltet bei einem Tipp auf SAFE |
| SPITZENWERTE ZURÜCKSETZEN | löscht die Spitzenmarken | löscht die Spitzenmarken |
| GRAFIK, TABELLE, WUCHTEN | wechseln die Ansicht von LIVE | wechseln die Ansicht von LIVE |

Beim Verlassen der Seite, auch über ZIEL, schaltet der Prüfstand auf SAFE und setzt Gas auf 0%. Dasselbe geschieht bei Verbindungsverlust oder ausbleibender Antwort des Touch-Controllers.

Solange kein IOmcu antwortet, zeigt das Band SIM, und SIMULATION steht quer über der Seite. Alle Werte stammen dann aus dem Panelmodell. SETUP, PANEL, Demo-Modus EIN blendet das Wasserzeichen SIMULATION aus; SIM bleibt.

## Ein gehaltener Lauf

![Die Grafik hält den Lauf nach dem Schalten auf SAFE](view:motor-held)

Beim Schalten auf SAFE friert die Grafik ein. Ihre Beschriftung lautet `LETZTE TELEMETRIE`, am rechten Rand steht `ENDE`. Das nächste Schalten auf ARMED löscht sie. Mit 600 Spalten bei 20 Messungen pro Sekunde deckt sie 30,0s ab. Bei längeren Läufen zeigt sie die letzten 30,0s; die CSV-Datei (Comma-Separated Values) auf der Karte enthält den gesamten Lauf.

Die Anzeigen, die Ansicht TABELLE, die Summen und der Temperaturstreifen bleiben immer live, ARMED oder nicht.

## Die Abfragerate und die Temperaturen

Auf LIVE stehen GRAFIK, TABELLE und WUCHTEN unter dem Titel der Telemetriekarte, daneben die Abfragerate und die Fehlerzahl der Verbindung. Hat der IOmcu seit dem Einschalten Link-Frames abgelehnt, erscheint deren Anzahl in der Zeile darunter. Über der rechten Spalte stehen bei jeder Aufgabe drei Temperaturen: ESC und MOT aus der Prüfstands-Page des IOmcu, MCU vom ESP32-P4-Chip im Panel.

![37 abgelehnte Link-Frames in der Zeile](view:motor-refused)

## TABELLE

![Die Ansicht TABELLE mit einem INA228 als Quelle](view:motor-table)

TABELLE zeigt Spannung, Strom, Leistung und Drehzahl mit ihren Spitzenwerten. Ohne gemeldeten Wert steht beim jeweiligen Kanal `--`.

- Ist der INA228 unter SETUP, SENSORS eingeschaltet und antwortet er, liefert er Spannung, Strom und Leistung. Es sind die Mittelwerte des letzten Messfensters von 50ms im IOmcu, ausgelesen mit 20Hz. Die Überschrift lautet `INA228`.
- `ÜBERSTEUERT` folgt der Überschrift, wenn ein Strommesswert die Messbereichsgrenze erreicht hat. Strom und Leistung sind dann Grenzwerte, keine gemessenen Werte.
- `ESC MELDET` zeigt Spannung und Strom aus der ESC-Telemetrie. `Abw.` ist der ESC-Wert minus dem INA228-Wert.
- Ohne INA228 stammen die Werte aus der ESC-Telemetrie; `ESC MELDET` entfällt.
- Die Drehzahl stammt aus der bidirektionalen DShot-Telemetrie. Nicht gemessene Größen bleiben leer und werden nicht simuliert.

## WUCHTEN

![WUCHTEN mit beiden Läufen der Zwei-Lauf-Methode](view:motor-balance)

WUCHTEN nimmt die Läufe einer Auswuchtung mit dem Auswuchtknoten auf ([Balance](balance.md#der-auswuchtknoten)). Die Tasten wirken beim Drücken und steuern keinen Ausgang, daher gehen sie auch bei ARMED.

- Die obere Zeile zeigt Vibration, Winkel, Drehzahl und Signal des Knotens, rechts die Testmasse. Bei Auswuchtknoten AUS oder ohne gebundenen Knoten steht dort `KEIN AUSWUCHTKNOTEN: IN WIRELESS BINDEN`. Ein Bericht, der älter als 1000ms ist, oder eine abgerissene Verbindung zeigt `--`.
- 2 LÄUFE: REFERENZ ohne Testmasse, dann TEST 0° mit der Testmasse auf Linie mit Blatt 1. Braucht die Indexmarke. 4 LÄUFE: REFERENZ, dann die Testmasse bei 0°, 120° und 240° ab Blatt 1, gegen die Drehrichtung.
- Jede Taste nimmt die nächsten 8 Berichte auf, 2s, bei gleichem Gas. Unter der Taste: `LÄUFT n/8`, das Ergebnis oder `KEIN BERICHT`, `SENSORFEHLER`, `ÜBERSTEUERT`, `DREHZAHLBEREICH`, `INDEXFEHLER`, `MODUS GEÄNDERT` (ein Bericht, den der Knoten als unbrauchbar kennzeichnet, beendet den Lauf sofort), `KEINE UMDREHUNG`, `KEIN INDEX`, `DREHZAHL GEÄNDERT` (mehr als 5%) oder unter einem Testlauf `ANDERE DREHZAHL`: seine mittlere Drehzahl weicht mehr als 5% von der der Referenz ab, und nur Läufe einer Drehzahl ergeben ein Ergebnis. Erneut drücken nimmt den Lauf neu auf.
- LÖSCHEN leert alle Läufe. Ein Wechsel der Methode leert sie ebenfalls.
- Sind alle Läufe aufgenommen: `HINZU 0.35g  WO zwischen 1 und 2` und eine Zeile in `BENCH/BALANCE/BALANCE.CSV`. Die Zeile darunter sagt `Geschrieben` oder `NICHT geschrieben`. Eine neue Testmasse löst die Läufe neu und schreibt eine neue Zeile. `SCHWACH`, `OHNE WIRKUNG`, `WIDERSPRÜCHLICH` oder `KEIN ERGEBNIS` nennen, warum das Ergebnis zweifelhaft ist oder fehlt.

## kV, rpm/V und WIRK.

Der Kopf der Grafik zeigt Nenn-kV, gemessene Drehzahl je Volt und WIRK. (geschätzter Wirkungsgrad). WIRK. ist die gemessene Drehzahl je Volt geteilt durch Nenn-kV, begrenzt auf 199%. Stelle `Nenn-kV` in der Optionsliste von MOTOR & ESC ein: 0 bis 10000rpm/V, Vorgabe 0. Bei 0 bleibt das kV-Feld leer, und WIRK. entfällt. WIRK. berücksichtigt nur Kupferverluste einschließlich der ESC-Verluste. Es ist daher eine obere Grenze, kein Wirkungsgrad an der Welle.

## Das Log eines Laufs

Ist Auto-Aufzeichnungen unter [SETUP](setup.md#sd-card) > SD CARD EIN, beginnt jedes Schalten auf ARMED eine Datei `MOTORnnn.CSV` in `BENCH/LOGS` auf der SD-Karte (Secure Digital) des Panels. Bei bestehender Verbindung erzeugt jede beantwortete Abfrage eine Zeile. An einem Prüfstand wurden etwa 19 Zeilen pro Sekunde gemessen; für andere Prüfstände liegen keine Messungen vor. Beim Schalten auf SAFE schließt die Datei. Öffne sie unter [Logs](logs.md), um den Lauf als Grafik anzuzeigen.

Die Zeile jeder Abtastung hält neben `rpm` und `power` das Gas, das das Panel vorgibt: Spalte `throttle (%)`, 0 bis 100% auf 0,1%, für PWM- und DShot-Ausgänge gleich. Solange der Motor nicht ARMED ist, steht 0.0 darin. Eine Zeile, die nur ein INA3221-Fenster hält, hat die Zelle leer.

![Die Tastatur von LOG UMBENENNEN](view:motor-logname)

LOG UMBENENNEN gibt dem seit dem Start des Panels geschlossenen MOTOR-Log ein Label hinter seiner Nummer: Aus `MOTOR042.CSV` wird `MOTOR042_kv1200.CSV`. Der Lauf behält Nummer und Platz unter [Logs](logs.md).

- LOG UMBENENNEN ist ein Tipp. Es wirkt, solange der Prüfstand SAFE ist, ARM nicht gehalten wird, keine Log-Datei offen ist und die Karte des Logs noch steckt. Sonst ist es gedimmt, und ein Tipp nennt den Grund im Alarmband, etwa `Seit dem Start kein MOTOR-Log.`
- Die Tastatur bearbeitet nur das Label, höchstens 46 Bytes. Sie öffnet mit dem Label der Datei, der Cursor an seinem Ende; `<-`, `->` und ein Tippen in den Text bewegen den Cursor. OK bei leerem Feld entfernt das Label.
- Die Tastatur verdeckt ARM. ABBRECHEN oder das Home-Feld schließt sie ohne Änderung.
- Das Alarmband meldet `Log umbenannt: MOTOR042_kv1200.CSV` 4s lang in Bernsteinfarbe oder den Grund in Rot, etwa `MOTOR042_kv1200.CSV ist dort bereits vorhanden.`
- Nach einem Neustart bleibt der Knopf gedimmt, bis der nächste MOTOR-Lauf endet. Ältere Logs benennst du unter [Files](files.md) um.

## Verwandte Seiten

- [Die Bildschirmhilfe](band.md#die-bildschirmhilfe): "?" im Band öffnet die Hilfe dieser Seite.
- [Motor-Optionen](motor-settings.md): Akku, Nenn-kV, Motorpole, Leerlaufpuls und Vollgaspuls.
- [ESC STICK](motor-stick.md): ESC über sein Gasknüppel-Menü programmieren.
- [Ausgänge](outputs.md): Pins für die Gassteuerung.
- [Setup](setup.md): Drehknopf.
- [Sensoren](setup-sensors.md): INA228.
