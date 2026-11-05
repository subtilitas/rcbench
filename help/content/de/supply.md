---
screen: SUPPLY
title: Netzteil
order: 30
---

# Netzteil

SUPPLY steuert das programmierbare Netzteil PD mini und zeichnet seine Werte auf. Der USB-PD-Trigger (USB Power Delivery) kommuniziert über einen UART (Universal Asynchronous Receiver-Transmitter) an zwei Pins mit dem IOmcu. Öffne SUPPLY im [Menü](overview.md); die Seite beginnt immer auf LIVE.

## Die Leiste

Die obere Leiste wechselt zwischen LIVE und OPTIONEN. LIVE behält beim Zurückkehren die gewählte Ansicht GRAFIK oder TABELLE.

| Aufgabe | Die linke Spalte |
|---|---|
| LIVE | die Grafik oder die Tabelle und die Sollwerte |
| OPTIONEN | die Obergrenzen, die Startwerte, die Abschaltungen und die Rückfragen ([OPTIONEN](#optionen)) |

Beide Aufgaben teilen sich die rechte Spalte mit Messwerten und der Karte STEUERUNG. Unten stehen SPITZENWERTE ZURÜCKSETZEN und AUSGANG EIN, an der Stelle von ARM bei SERVO und MOTOR & ESC. Die Kopfzeile rechts neben der Leiste nennt Netzteil, Status und zulässigen Sollwertbereich. Sie bleibt auch unter Tastenfeld und Rückfrage sichtbar.

Das Home-Feld führt eine Ebene zurück. In einer Rückfrage verwirft es die Änderung wie ABBRECHEN; im Tastenfeld schließt es die Eingabe ohne Wertänderung. Von LIVE oder OPTIONEN ohne offenen Dialog führt es zum Menü. Ein Aufgabenwechsel fügt keine Navigationsebene hinzu.

## Das Netzteilmodell

![SUPPLY mit abgeschaltetem PD mini: SIMULIERT](view:supply)

Ist der PD mini im [Reiter PD MINI](#tab-pd-mini) deaktiviert, verwendet das Panel ein Netzteilmodell. Die antippbare Kopfzeile `SIMULIERT: SCHALTE IN PD MINI EIN` öffnet diesen Reiter; die Menükachel zeigt SIMULIERT. Das Modell besteht aus einer Last von 6Ω hinter 0,05Ω Quellwiderstand, mit einem Stromstoß von 1,4A für 0,6s alle 3s. Die Werte sind berechnet. Am Prüfstand wird nichts mit Strom versorgt.

## Der PD mini

![Der PD mini eingeschaltet, kein IOmcu liest ihn: ANTWORTET NICHT](view:supply-offline)

Bei aktiviertem PD mini nennt die Kopfzeile Modul und Pins aus PD mini TX und PD mini RX, etwa `PD MINI TX GPIO 12 RX GPIO 13`. Fehlt ein Pin oder verwenden beide denselben GPIO, steht dort `PD MINI OHNE PINS`. Das Panel liest die SUPPLY-Page des IOmcu alle 100ms aus.

| Kopf | Bedeutung |
|---|---|
| `ONLINE 38400` | das Modul antwortet; die Zahl ist die verwendete Baudrate |
| `ANTWORTET NICHT` | kein IOmcu ab 0.10.0 antwortet, oder die Messwerte sind älter als 1500ms; ein eingeschalteter Ausgang wird abgeschaltet |

Vor dem ersten Einschalten zeigt die Grafik `Ausgang wurde noch nicht eingeschaltet.`, und die Karten zeigen Striche, bis ein Messwert ankommt.

## Die Messwerte

| Karte | Zeigt |
|---|---|
| SPANN. | die Ausgangsspannung; den Sollwert in Klammern; die niedrigste Spannung des Laufs darunter |
| STROM | den Ausgangsstrom; die Strombegrenzung in Klammern; die Spitze des Laufs darunter |
| LEIST. | die Ausgangsleistung und die Spitze des Laufs |
| MODUS | CV (Constant Voltage) an der eingestellten Spannung, CC (Constant Current) an der Strombegrenzung, ABGESCHALTET nach einer Abschaltung; CC steht in der Warnfarbe |

Die Grafik zeigt Spannung, Strom und Leistung der letzten 30s mit gestrichelten Sollwertlinien. TABELLE enthält dieselben Werte. In Klammern steht der vom Netzteil gemeldete Sollwert; ohne Antwort gilt der auf der Seite gespeicherte Wert.

## Sollwerte

| Sollwert | Modell | PD mini | Schritttasten |
|---|---|---|---|
| SPANNUNG | 3,3 bis 21V, Schritte von 20mV | 1 bis 20V, Schritte von 10mV | 0,1V |
| STROMBEGRENZUNG | 0,5 bis 5A, Schritte von 50mA | 0,05 bis 3A, Schritte von 10mA | 0,1A |

Die PD-mini-Bereiche stammen von der Herstellerseite und wurden nicht gemessen. Die Ausgangsspannung ist auf 0,7V unter der gemeldeten Eingangsspannung begrenzt. Bei 5V Eingang sind höchstens 4,3V möglich. Die Obergrenzen in OPTIONEN können beide Bereiche weiter einschränken. Rechts in der Kopfzeile steht der gültige Bereich.

- Ziehe den Schieberegler oder tippe auf seine Spur, um den Wert an dieser Stelle zu setzen.
- Ändere den Wert schrittweise mit `-0.1` und `+0.1`.
- Tippe auf SPANN., STROM oder den Wert rechts in einer Sollwertzeile, um das Tastenfeld zu öffnen.

![Das Tastenfeld über der linken Spalte](view:supply-keypad)

Das Tastenfeld nennt den Bereich in der Titelzeile. OK übernimmt gültige Werte und rundet auf die Schrittweite des Netzteils. Bei unzulässigen Werten wird die Eingabe abgelehnt und der Bereich in der Warnfarbe hervorgehoben. ABBRECHEN oder OK ohne Eingabe behält den Sollwert.

## Eine Änderung an einem eingeschalteten Ausgang

![Die Rückfrage vor einer Änderung an einem eingeschalteten Ausgang](view:supply-confirm)

Eine Sollwertänderung bei eingeschaltetem Ausgang öffnet eine Rückfrage mit der geplanten Änderung. ÜBERNEHMEN sendet sie an das Netzteil; ABBRECHEN verwirft sie und setzt den Regler zurück. Beim Ziehen erscheint die Frage einmal nach dem Loslassen. Wird das Ziehen vorher unterbrochen, etwa durch einen zweiten Finger auf dem Home-Feld, kehrt der Regler ohne Rückfrage zurück. Bei ausgeschaltetem Ausgang entfällt die Frage. In OPTIONEN lassen sich Rückfragen für Regler und Tastenfeld getrennt abschalten.

## AUSGANG EIN und AUS

Halte AUSGANG EIN 2s. Die Taste zeigt denselben Farbverlauf wie ARM. Früheres Loslassen schaltet nichts und zeigt `Halte 2s, um den Ausgang einzuschalten.` im Hinweisband. AUSGANG AUS wirkt per Tipp. In folgenden Fällen schaltet der Ausgang ab und bleibt bis zum nächsten Halten aus:

- STOP auf jeder Seite;
- Verlassen von SUPPLY, wenn diese Seite den Ausgang eingeschaltet hat;
- ausbleibende Antwort des Netzteils oder 3000ms ohne Messwert;
- Auslösen einer Abschaltbedingung;
- PD-mini-Eingangsspannung bei 2 aufeinanderfolgenden Messungen unter Sollwert plus 0,5V;
- selbstständiges Abschalten des Moduls;
- Änderung an Pins, Baudrate oder Aktivierung des PD mini.

Das Band nennt den Grund der Abschaltung.

Bei ausgelöstem IOmcu-Failsafe wird EIN abgelehnt. Das Band zeigt FEHLER 01 nach 200ms ohne Link-Verkehr, etwa bei einem IOmcu-Neustart, oder FEHLER 10 bei ausgebliebenem Heartbeat. Das Halten endet mit `Netzteil bleibt aus: IOmcu-Failsafe ausgelöst. Halte ARM, dann tippe DISARM.` Nur der CLEAR-Befehl beim ARM hebt den Failsafe auf. Halte dazu ARM auf SERVO oder MOTOR & ESC, tippe auf DISARM und halte anschließend AUSGANG EIN erneut.

Ein von einer anderen Seite eingeschalteter Ausgang bleibt beim Verlassen von SUPPLY unverändert. Nach jedem Neustart ist der Ausgang aus, unabhängig von den Startwerten.

## Auf dem rbmod-Carrier

![SUPPLY auf dem Carrier](view:supply-rbmod)

Auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) versorgt der PD mini die Servoschienen. Die Programmierports haben eine eigene, über GPIO 21 geschaltete 5V-Versorgung. Unter AUSGANG steht deren Status: `PROG-PORTS 5V, GPIO 21: EIN`, `AUS` oder ohne IOmcu-Antwort `UNBEKANNT`. Die KST- und Hitec-Servoprogrammierung schaltet diese Schiene für ihre Sitzungen. Den PD mini kannst du weiterhin selbst bedienen; AUSGANG EIN und Sollwerte bleiben verfügbar.

Auf LIVE schaltet das Feld PROG 5V neben GRAFIK und TABELLE die Programmierschiene von Hand, bei SAFE und ARMED. Halte PROG 5V EIN 2s zum Einschalten und tippe auf PROG 5V AUS zum Ausschalten. Früheres Loslassen zeigt `Halte 2s, um die 5V der Programmierports einzuschalten.` Das Feld nennt den Pinstatus, etwa `PROG-PORTS 5V, GPIO 21: AUS`, und das letzte INA3221-Messfenster von 0x40 CH3, etwa `5.02V  0.120A`.

Belegt eine Servoprogrammierung einen Port, zeigt das Feld `BELEGT VON PROG-PORT n` und nimmt keinen Druck an. Beim Verlassen von SUPPLY schalten die über dieses Feld aktivierten 5V aus, außer ein Port belegt sie. Auch jeder Fehler schaltet sie ab. Ein laufender Schreibvorgang im Servospeicher wird vorher beendet, mit höchstens 10000ms Wartezeit.

Bei eingeschaltetem Feld gelten zwei Messfenster unter 3000mV als Ausfall der 5V-Versorgung: `PROG 5V ausgefallen: INA3221 0x40 CH3 misst 0.12V trotz EIN. Alle Ausgänge aus.`

## OPTIONEN

![Die Optionen des Netzteils](view:supply-settings)

OPTIONEN zeigt seine Einstellungen in der linken Spalte. Tippe auf einen Wert für das Tastenfeld oder auf einen Schalter zum Umschalten. Ein Tipp auf LIVE bei offenem Tastenfeld verwirft die Eingabe. Bei einer Rückfrage zum eingeschalteten Ausgang wartet die Leiste auf ÜBERNEHMEN oder ABBRECHEN.

| Option | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| SPANNUNG MAX | 3,3 bis 21V | 21,00V | die höchste Spannung, die ein Sollwert annimmt |
| STROM MAX | 0,5 bis 5A | 5,00A | die höchste Strombegrenzung, die ein Sollwert annimmt |
| STARTSPANNUNG | bis SPANNUNG MAX | 6,00V | der Spannungssollwert nach einem Neustart |
| STARTSTROM | bis STROM MAX | 2,00A | die Strombegrenzung nach einem Neustart |
| ÜBERSTROM | 0 bis 5A | AUS | Ausgang aus, sobald der Strom ABSCHALTZEIT lang darüber liegt |
| ÜBERSPANNUNG | 0 bis 21V | AUS | Ausgang aus, sobald die Spannung ABSCHALTZEIT lang darüber liegt |
| ABSCHALTZEIT | 0 bis 5000ms | 100ms | wie lange ein Messwert über einer Schwelle liegen muss, bevor sie auslöst |
| REGLER UND STUFEN | EIN, AUS | EIN | Rückfrage, bevor der Schieberegler einen eingeschalteten Ausgang ändert |
| TASTENFELD | EIN, AUS | EIN | Rückfrage, bevor das Tastenfeld einen eingeschalteten Ausgang ändert |

- Eine Schwelle von 0 deaktiviert die Abschaltung.
- Eine Überstromschwelle auf oder über der STROMBEGRENZUNG löst nicht aus, weil das Netzteil den Strom in CC begrenzt.
- Wird eine Obergrenze unter den Sollwert gesenkt, sinkt der Sollwert mit.

Die Optionen werden im NVS (nichtflüchtiger Speicher) gespeichert. Das Schreiben erfolgt im nächsten Frame bei SAFE und ausgeschaltetem Ausgang. Die Fußzeile zeigt `GESPEICHERT`, `SPEICHERN AUSSTEHEND`, `NICHT GESPEICHERT` oder `ÄNDERUNGEN IN SETUP NICHT GESPEICHERT`.

Bei aktiviertem PD mini bietet OPTIONEN zusätzlich PD MINI ZURÜCKSETZEN. Es schaltet den Ausgang aus und startet das Modul neu, etwa bei ERR. Ob ein Neustart jedes ERR löscht, wurde nicht gemessen.

## Tab PD MINI

![Der Tab PD MINI](view:supply-pdmini)

OPTIONEN hat zwei Reiter: GRENZEN und PD MINI. Unter PD MINI stellst du die Modulverdrahtung ein.

| Option | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| PD mini | EIN, AUS | AUS | das programmierbare Netzteil auf SUPPLY und SERVO; AUS rechnet das Netzteilmodell des Panels |
| PD mini TX | -1 bis 47 | -1 | der GPIO des IOmcu zum DM des Moduls |
| PD mini RX | -1 bis 47 | -1 | der GPIO des IOmcu vom DP des Moduls |
| PD mini Baud | 9600, 19200, 38400, 57600, 115200, 230400, 460800, AUTO | AUTO | die UART-Rate (Universal Asynchronous Receiver-Transmitter) des Moduls; AUTO probiert jede der 7 Raten, eine pro Sekunde |

Änderungen gelten sofort und werden wie auf GRENZEN gespeichert. Es gibt keine separate Taste SPEICHERN. Das Zurücksetzen auf GRENZEN verändert diese Einstellungen nicht.

Verdrahtungsänderungen schalten einen aktiven Ausgang aus. Daher sind diese Einstellungen gesperrt, solange der Ausgang eingeschaltet ist, ein EIN-Befehl aussteht, ein Programmierport das Netzteil belegt oder der Prüfstand ARMED ist. Hilfezeile und Band nennen den Grund. Beginnt einer dieser Zustände bei offenem Pin-Tastenfeld, schließt es und verwirft die Eingabe.

Die Tasten `-` und `+` für PD mini TX und RX überspringen den GPIO der jeweils anderen Zeile. Bei RX auf GPIO 18 springt TX beim Verringern von GPIO 19 auf GPIO 17. An der Bereichsgrenze stoppt die Taste: Bei RX auf GPIO 47 endet TX bei GPIO 46. Das Tastenfeld lehnt den anderen Pin mit `GPIO vom anderen PD-mini-Pin belegt` ab.

Eine gespeicherte Konfiguration mit beiden Pins auf demselben GPIO wird unverändert geladen. Der PD mini bleibt aus; die Kopfzeile zeigt `PD MINI OHNE PINS`. Beide Zeilen melden `TX und RX auf einem Pin. PD mini bleibt aus.`, bis du eine änderst.

Der IOmcu lehnt außerdem reservierte Pins sowie durch Ausgänge, Sensorbus, Phasenabgriff oder das andere PD-mini-Signal belegte Pins ab. RX auf GPIO 17 kollidiert beispielsweise mit Sensor-SCL auf GPIO 17. Nach Übernahme eines gültigen Pins verschwindet die Meldung.

![GPIO 9 von RX für PD mini TX abgelehnt](view:supply-pdmini-why)

Auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) stehen PD mini TX auf GPIO 7 und PD mini RX auf GPIO 6. Die Zeilen haben keine Tasten und kein Tastenfeld. Ihre Hilfe lautet `Auf Platine verlötet. Hier nicht änderbar.` Die gespeicherten Pinwerte bleiben erhalten und gelten wieder auf einem manuell verdrahteten Modul.

![PD mini TX und RX verlötet](view:supply-pdmini-rbmod)

## Das Log eines Laufs

Jedes Einschalten des Ausgangs beginnt einen Lauf. Dabei wird die Grafik geleert; beim Ausschalten friert sie ein. Die Titelzeile von STEUERUNG summiert Ladung in mAh und Energie in Wh für diesen Lauf. SPITZENWERTE ZURÜCKSETZEN beginnt die Minima und Maxima beim aktuellen Messwert neu.

Bei Auto-Aufzeichnungen EIN unter [SETUP](setup.md#sd-card) > SD CARD erhält jeder Lauf eine eigene `SUPPLnnn.CSV` in `BENCH/LOGS` auf der SD-Karte (Secure Digital). Alle 50ms wird eine Zeile geschrieben: Sollwert, Spannung, Strombegrenzung, Strom, Leistung, Modus, Ladung, Energie und Quelle (`measured` oder `model`). Öffne die Datei unter [Logs](logs.md).

## Verwandte Seiten

- [Servo](servo.md): dieselben Sollwerte und derselbe Ausgangsschalter auf SERVO.
