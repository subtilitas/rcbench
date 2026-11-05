---
screen: MOTOR
title: Motor und ESC
order: 10
---

# Motor und ESC

MOTOR & ESC steuert einen ESC (Electronic Speed Controller) und zeichnet Spannung, Strom, Leistung und Drehzahl eines Laufs über der Zeit auf. Öffne die Seite über die Kachel MOTOR & ESC im [Menü](overview.md). Sie beginnt immer mit LIVE.

## Die Leiste

Die obere Leiste bietet drei Aufgaben: LIVE, PROGRAMMIEREN und OPTIONEN. Tippe auf eine Aufgabe, um zu wechseln. LIVE behält dabei die gewählte Ansicht, GRAFIK oder TABELLE.

| Aufgabe | Linke Spalte |
|---|---|
| LIVE | Grafik oder Tabelle und Gasregler |
| PROGRAMMIEREN | Liste der ESC-Programmierer ([Programmieren](#programmieren)) |
| OPTIONEN | [Motor-Optionen](motor-settings.md): Akku, Motor und Gasausgang |

Die rechte Spalte ist bei allen drei Aufgaben gleich: Temperaturen, vier Anzeigen und die Karte STEUERUNG. Die Karte enthält Summen in der Titelzeile, ZIEL, SPITZENWERTE ZURÜCKSETZEN und ARM. ARM steht wie bei SERVO am unteren Kartenrand und funktioniert bei jeder Aufgabe. Eine unter PROGRAMMIEREN geöffnete Sitzung verdeckt die Karte.

Das Home-Feld oben links führt jeweils eine Ebene zurück:

| Ausgangspunkt | Ziel des Home-Felds |
|---|---|
| Tastenfeld auf OPTIONEN | OPTIONEN; der Wert bleibt unverändert |
| Sitzung auf PROGRAMMIEREN | eine Ebene höher in der Sitzung, dann zur Liste von PROGRAMMIEREN |
| LIVE, Liste von PROGRAMMIEREN oder OPTIONEN | Menü; der Prüfstand schaltet auf SAFE |

## Programmieren

![Die Liste von PROGRAMMIEREN](view:motor-program)

PROGRAMMIEREN bietet zwei Wege zur ESC-Programmierung. `ESCs per Gasknüppel` unter STICK-MENÜ öffnet [ESC STICK](motor-stick.md) und steuert die als Gas zugeordneten Pins. `AM32, Bluejay, BLHeli_S, ESCape32` unter SIGNALLEITUNG öffnet die [Programmierung über die Signalleitung](motor-wire.md). Diese Zeile bleibt gedimmt, bis der IOmcu die benötigte Fähigkeit meldet.

ESC STICK verdeckt beide Spalten. Anstelle der Reiter erscheint ein einzeiliger Ausgangsstreifen:

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

1. Halte ARM 2s. Die Füllung geht von Grün nach Rot über. Am Ende des Übergangs schaltet der Prüfstand auf ARMED. Lässt du früher los, ändert sich der Zustand nicht, und das Hinweisband zeigt `Halte 2s, um auf ARMED zu schalten.`.
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
| GRAFIK, TABELLE | wechseln die Ansicht von LIVE | wechseln die Ansicht von LIVE |

Beim Verlassen der Seite, auch über ZIEL, schaltet der Prüfstand auf SAFE und setzt Gas auf 0%. Dasselbe geschieht bei Verbindungsverlust oder ausbleibender Antwort des Touch-Controllers.

Solange kein IOmcu antwortet, zeigt das Band SIM, und SIMULATION steht quer über der Seite. Alle Werte stammen dann aus dem Panelmodell. SETUP, PANEL, Demo-Modus EIN blendet das Wasserzeichen SIMULATION aus; SIM bleibt.

## Ein gehaltener Lauf

![Die Grafik hält den Lauf nach dem Schalten auf SAFE](view:motor-held)

Beim Schalten auf SAFE friert die Grafik ein. Ihre Beschriftung lautet `LETZTE TELEMETRIE`, am rechten Rand steht `ENDE`. Das nächste Schalten auf ARMED löscht sie. Mit 600 Spalten bei 20 Messungen pro Sekunde deckt sie 30,0s ab. Bei längeren Läufen zeigt sie die letzten 30,0s; die CSV-Datei (Comma-Separated Values) auf der Karte enthält den gesamten Lauf.

Die Anzeigen, die Ansicht TABELLE, die Summen und der Temperaturstreifen bleiben immer live, ARMED oder nicht.

## Die Abfragerate und die Temperaturen

Auf LIVE stehen GRAFIK und TABELLE unter dem Titel der Telemetriekarte, daneben die Abfragerate und die Fehlerzahl der Verbindung. Hat der IOmcu seit dem Einschalten Link-Frames abgelehnt, erscheint deren Anzahl in der Zeile darunter. Über der rechten Spalte stehen bei jeder Aufgabe drei Temperaturen: ESC und MOT aus der Prüfstands-Page des IOmcu, MCU vom ESP32-P4-Chip im Panel.

![37 abgelehnte Link-Frames in der Zeile](view:motor-refused)

## TABELLE

![Die Ansicht TABELLE mit einem INA228 als Quelle](view:motor-table)

TABELLE zeigt Spannung, Strom, Leistung und Drehzahl mit ihren Spitzenwerten. Ohne gemeldeten Wert steht beim jeweiligen Kanal `--`.

- Ist der INA228 unter SETUP, SENSORS eingeschaltet und antwortet er, liefert er Spannung, Strom und Leistung. Es sind die Mittelwerte des letzten Messfensters von 50ms im IOmcu, ausgelesen mit 20Hz. Die Überschrift lautet `INA228`.
- `ÜBERSTEUERT` folgt der Überschrift, wenn ein Strommesswert die Messbereichsgrenze erreicht hat. Strom und Leistung sind dann Grenzwerte, keine gemessenen Werte.
- `ESC MELDET` zeigt Spannung und Strom aus der ESC-Telemetrie. `Abw.` ist der ESC-Wert minus dem INA228-Wert.
- Ohne INA228 stammen die Werte aus der ESC-Telemetrie; `ESC MELDET` entfällt.
- Die Drehzahl stammt aus der bidirektionalen DShot-Telemetrie. Nicht gemessene Größen bleiben leer und werden nicht simuliert.

## kV, rpm/V und WIRK.

Der Kopf der Grafik zeigt Nenn-kV, gemessene Drehzahl je Volt und WIRK. (geschätzter Wirkungsgrad). WIRK. ist die gemessene Drehzahl je Volt geteilt durch Nenn-kV, begrenzt auf 199%. Stelle `Nenn-kV` unter MOTOR & ESC, OPTIONEN ein: 0 bis 10000rpm/V, Vorgabe 0. Bei 0 bleibt das kV-Feld leer, und WIRK. entfällt. WIRK. berücksichtigt nur Kupferverluste einschließlich der ESC-Verluste. Es ist daher eine obere Grenze, kein Wirkungsgrad an der Welle.

## Das Log eines Laufs

Ist Auto-Aufzeichnungen unter [SETUP](setup.md#sd-card) > SD CARD EIN, beginnt jedes Schalten auf ARMED eine Datei `MOTORnnn.CSV` in `BENCH/LOGS` auf der SD-Karte (Secure Digital) des Panels. Bei bestehender Verbindung erzeugt jede beantwortete Abfrage eine Zeile. An einem Prüfstand wurden etwa 19 Zeilen pro Sekunde gemessen; für andere Prüfstände liegen keine Messungen vor. Beim Schalten auf SAFE schließt die Datei. Öffne sie unter [Logs](logs.md), um den Lauf als Grafik anzuzeigen.

## Verwandte Seiten

- [Motor-Optionen](motor-settings.md): Akku, Nenn-kV, Motorpole, Leerlaufpuls und Vollgaspuls.
- [ESC STICK](motor-stick.md): ESC über sein Gasknüppel-Menü programmieren.
- [Ausgänge](outputs.md): Pins für die Gassteuerung.
- [Setup](setup.md): Drehknopf.
- [Sensoren](setup-sensors.md): INA228.
