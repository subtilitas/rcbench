---
screen: SERVO
title: Servo
order: 20
---

# Servo

SERVO steuert ein Servo, misst seinen Strom an der Servoschiene, testet es bei mehreren Versorgungsspannungen und sucht die Weggrenzen im eingebauten Zustand. Öffne die Seite über SERVO im [Menü](overview.md). Sie beginnt auf LIVE.

## Die Werkbank

Die Leiste unter dem Band enthält LIVE, TEST, PROGRAMMIEREN, RUDER und OPTIONEN. Jede Aufgabe behält Seite und Auswahl bis zum Neustart. Reiterwechsel erzeugen keine Navigationsebene: Home führt von jeder Aufgabe zum Menü. Aus Laufoptionen geht es zurück zu TEST, aus einer Servoprogrammierung zur Liste PROGRAMMIEREN. Das Verlassen von SERVO schaltet den Prüfstand auf SAFE.

| Aufgabe | Linke Karte |
|---|---|
| LIVE | Hornwinkel, Zifferblatt und darunter SOLL-PULSBREITE, GEMESSEN und STROM; Laufanzeige links neben dem Zifferblatt. |
| TEST | AUTOMATISCHER TEST und ENDLAGENLAUF mit OPTIONEN, Zusammenfassung, Start-Haltetaste, letztem Ergebnis und Datei; SUPPLY-Grenzen als Anzeige. |
| PROGRAMMIEREN | Liste der [Servoprogrammierungen](servo-programmer.md); eine Zeile öffnet die Sitzung über der linken Spalte. |
| RUDER | Ausschlag in ° und mm, LÄNGE, 1000us, ZENTRIEREN, 2000us, NULLPUNKT und MAX LÖSCHEN. AUTO programmiert KST-, Chaservo- oder Hitec-Endpunkte auf den vorgegebenen [Ruderausschlag](servo-rudder.md). |
| OPTIONEN | [Servo-Optionen](servo-settings.md): AUSGANG, SWEEP und PRÜFLING. |

Rechts bleibt bei jeder Aufgabe dieselbe Ausgangskarte: Typzeile, Messzeile und Grafik, Netzteilzeile, TEMPO, ZIEL, ZENTRIEREN, SWEEP, FREIGEBEN und ARM. Antippbare Zeilen sind mindestens 48px (7,2mm) hoch, ARM 60px. Der Drehknopf bewegt das Horn nur auf LIVE und RUDER.

## In Ruhe

![SERVO bei SAFE](view:servo)

Bei SAFE erscheint das Horn gedimmt, und die linke LIVE-Karte zeigt `ZUERST ARM`. Das Zifferblatt nimmt keine Positionsbefehle an. Der IOmcu schreibt auf jeden Servo-PWM-Pin (Pulsweitenmodulation) eine Pulslänge von null.

Die Messwerte von LIVE, am Fuß der linken Karte:

| Anzeige | Inhalt |
|---|---|
| SOLL-PULSBREITE | von der Seite vorgegebene Pulsbreite in us |
| GEMESSEN | Winkel der gewählten Quelle AS5600 oder IMU, -180,0 bis +179,9°. Der AS5600 muss eingeschaltet sein und seinen Magneten erkennen. Ohne Messwert steht `---`. |
| STROM | Servoschienenstrom des aktiven Messgeräts in Hundertstel Ampere |

Die rechte Karte, von oben:

| Zeile | Inhalt |
|---|---|
| Typzeile | Aktiver Servotyp, Pulsrate und Pulsbereich, etwa `STANDARD PWM  50Hz  1000-2000`. |
| Messzeile und Grafik | Spannung, Strom und Leistung der letzten 17,8s. |
| SOLL | Netzteilspannung, Strombegrenzung und Ausgangsschalter. |
| TEMPO | Maximale Bewegungsgeschwindigkeit des Ausgangs; 100% gibt Befehle unverändert weiter. |
| ZIEL | Angesteuerte Ausgänge als `Protokoll Pins`, etwa `SERVO PWM GPIO 2`, `PROG-PORT 1 GPIO 13`, oder eine Warnung. Ein Tipp öffnet [die Zielwahl](servo-settings.md#die-zeile-ziel) unter Ausgänge. |

## ARMED

![SERVO bei ARMED SERVO](view:servo-armed)

1. Halte ARM 2s. Das Band zeigt danach ARMED SERVO, die Taste heißt DISARM.
2. Ziehe auf LIVE an einer beliebigen Stelle des Bogens, um eine Position vorzugeben. Der kräftige Arm zeigt die gemessene, der blasse die vorgegebene Position.
3. Beim Loslassen bleibt die Position aktiv. Die Seite wiederholt den letzten Befehl; das Servo hält seine Stellung.
4. Tippe auf DISARM, drücke STOP oder verlasse die Seite, um auf SAFE zu schalten.

ARMED SERVO steuert Servoausgänge und einen pulsenden PROG-PORT an. Gasausgänge erhalten keinen Puls.

| Bedienelement | SAFE | ARMED |
|---|---|---|
| Zifferblatt | gesperrt | gibt die Position unter dem Finger vor |
| Drehknopf | gesperrt; Drehungen werden verworfen | bewegt das Horn ausgehend vom Sollwert |
| ZENTRIEREN | gesperrt und gedimmt | gibt PULS MITTE plus TRIMMUNG vor |
| SWEEP, PAUSE, PAUSIERT | gesperrt und gedimmt | startet, pausiert und setzt den Sweep fort |
| FREIGEBEN | sendet die Freigabe | stellt Ruder auf ihre Ruheposition: 1500us bei STANDARD PWM, 760us bei NARROW 760 |
| TEMPO | einstellbar | einstellbar; gehaltene Position wird mit neuer Rate erneut gesendet |
| TEST STARTEN | gesperrt: `ZUERST ARM` | startet nach dem Halten den automatischen Test |

Beim Schalten auf ARMED wird die Mitte zwischen den Kanalendpunkten ohne TRIMMUNG vorgegeben. Das erste Ziehen oder Drehen beginnt dort. SWEEP bleibt grau, wenn kein Ruder zugeordnet ist, die Verbindung fehlt oder der IOmcu älter als 0.10.0 ist.

## Das Messgerät der Servoschiene

Die Zeile über der Grafik nennt das Messgerät der Servoschiene:

| Name | Messgerät |
|---|---|
| `INA 0x41 CH2` | INA3221-Kanal, der unter SETUP > SENSORS das Testerziel überwacht, hier CH2 an 0x41. Angezeigt werden Mittelwerte von Strom und Spannung des letzten Fensters von 50ms, mit 20 Punkten pro Sekunde. |
| `PD MINI` | letzter PD-mini-Messwert oder Netzteilmodell des Panels |

![Der INA3221 als Messgerät der Schiene](view:servo-ina)
![Ein übersteuertes Fenster, in der Warnfarbe](view:servo-clipped)

Der INA3221 wird nach 1000ms erfüllter Voraussetzungen zum Messgerät: Ein bestückter Kanal überwacht das Testerziel, der IOmcu ist mindestens Version 0.16.0, und Fenster mit Strommesswerten treffen ein. Vor IOmcu 0.22.0 wird CH1 des gewählten Bausteins verwendet. Erreicht ein Messwert die Bereichsgrenze, etwa 1,638A bei 0,1Ω Shunt, erscheint das Fenster in der Warnfarbe. Sein Mittelwert ist dann nur eine Grenze, kein verlässlicher Messwert.

![Das Netzteilmodell des Panels als Messgerät, mit der Marke SIMULIERT](view:servo-model)

Ist der PD mini unter SUPPLY > PD MINI deaktiviert, wird das Netzteilmodell des Panels verwendet. Die Grafik zeigt SIMULIERT; die Werte sind berechnet.

Fehlende Messwerte erscheinen als `---` bei STROM auf LIVE, als `--` in der Messzeile und als Lücke in der Grafik.

## Das Netzteil auf SERVO

SOLL enthält dieselben zwei Netzteilsollwerte und denselben Ausgangsschalter wie SUPPLY. Änderungen auf einer Seite gelten auf beiden.

- Tippe auf einen Sollwert, um das Tastenfeld über der linken Karte zu öffnen.
- Halte AUSGANG EIN 2s oder tippe auf AUSGANG AUS.
- Beim Verlassen schaltet SERVO einen von hier eingeschalteten Ausgang aus.
- Lässt VERLASSEN einer Programmiersitzung den Ausgang eingeschaltet, fragt SERVO nach Wiederherstellung der vorherigen Sollwerte ([Servoprogrammierung](servo-programmer.md)). Bei PORT FREIGEBEN, STOP, Fehler, Verbindungsverlust oder Neustart entfällt die Frage. Die Sitzungswerte bleiben bis zum nächsten AUS.

Auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) ersetzt PROG 5V die Grafik unter PD MINI. Das Feld schaltet die eigene 5V-Schiene der Programmierports an GPIO 21, bei SAFE und ARMED. Halte PROG 5V EIN 2s zum Einschalten und tippe auf PROG 5V AUS zum Ausschalten. Die zweite Zeile zeigt das letzte INA3221-Fenster von 0x40 CH3, etwa `5.02V  0.120A`.

Während einer Programmiersitzung steht dort `BELEGT VON PROG-PORT n`, und die Taste ist gesperrt. Beim Verlassen von SERVO schaltet eine hier aktivierte Schiene aus, außer ein Port belegt sie. SUPPLY bietet dasselbe Feld.

Bei eingeschaltetem Feld bedeuten zwei Fenster unter 3000mV einen Ausfall der 5V-Schiene. Ein möglicher Grund ist Rückspeisung eines bremsenden Servos, durch die M7 wegen Überspannung abschaltet. Der Prüfstand geht auf SAFE, alle Ausgänge schalten aus. Das Band zeigt `PROG 5V ausgefallen: INA3221 0x40 CH3 misst 0.12V trotz EIN. Alle Ausgänge aus.` Mit PROG 5V EIN kannst du erneut einschalten. Ohne überwachenden INA3221-Kanal oder während eine Servoprogrammierung einen Port belegt, ist diese Überwachung inaktiv.

![SERVO auf dem Carrier](view:servo-rbmod)

![NUR HV-SERVOS](view:servo-hv)

Standardservos sind für 4,8 bis 6,0V ausgelegt. Eine Erhöhung von höchstens 6,0V auf über 6,0V öffnet NUR HV-SERVOS. Halte 2s HALTEN ZUM ÜBERNEHMEN, um die Spannung anzuwenden; ABBRECHEN verwirft sie. AUSGANG EIN bei einem Sollwert über 6,0V öffnet dieselbe Warnung.

## SWEEP und PAUSE

SWEEP folgt der KURVE unter OPTIONEN > SWEEP: Rechteck, Sinus oder Dreieck. TEMPO reicht von 0,05 bis 5Hz, BEREICH von 10 bis 100% des Wegs. Der IOmcu taktet die Kurve unabhängig von der Verbindung. Während der Bewegung heißt die Taste PAUSE. Ein Tipp hält die aktuelle Position; die Taste heißt dann PAUSIERT. Tippe erneut, um dort fortzufahren.

![Ein angehaltener Sweep](view:servo-paused)

![TEMPO BEGRENZT DEN SWEEP](view:servo-sweep)

`TEMPO BEGRENZT DEN SWEEP` ersetzt die TEMPO-Beschriftung in der Warnfarbe, wenn TEMPO die schnellste geforderte Kurvenänderung begrenzt. Die Bewegung folgt dann Rampen. Erhöhe TEMPO oder senke TEMPO beziehungsweise BEREICH unter OPTIONEN > SWEEP. Mit den Vorgaben Sinus, 0,5Hz, BEREICH 80% und WEG 90° je Richtung verschwindet die Warnung bei TEMPO 63%.

Berühren des Zifferblatts, ZENTRIEREN, FREIGEBEN, STOP, SAFE oder Verlassen der Seite beendet den Sweep.

## Der automatische Test

Halte TEST STARTEN auf der Karte AUTOMATISCHER TEST unter TEST 2s, um die gewählten Spannungsstufen auszuführen. Jede Stufe misst den Strom in Ruhe, Bewegung und Halten sowie die Stellzeit. Der Brown-out-Lauf sucht die Spannung, bei der die Bewegung aussetzt.

Erforderlich sind ARMED und ein antwortendes Netzteil. Die Statuszeile nennt Ablehnungsgründe: `ZUERST ARM`, `KEINE STUFE GEWÄHLT`, `NETZTEIL ANTWORTET NICHT`, `BEREICH ZU KLEIN`, `STUFE AUSSERHALB DER GRENZEN` oder `BERICHT WIRD NOCH GESCHRIEBEN`. Carrier-Programmierports haben feste 5V; Netzteilstufen erreichen sie nicht.

![TEST mit seinen zwei Laufkarten](view:servo-test)

Die Zusammenfassung nennt Stufen und Dauer, etwa `4.8V, 6.0V, BROWN-OUT, JE 60s`. Unter TEST STARTEN folgen Status, letztes Ergebnis und Dateien, etwa `ZULETZT: BESTANDEN ATEST004.CSV + .TXT`. Bei Abbruch steht dort stattdessen der Grund, etwa `ZULETZT: ABGEBROCHEN - Verbindung verloren`.

![Ein Testlauf](view:servo-run)

Beim Laufstart bleibt TEST sichtbar; die Statuszeile nennt die aktuelle Stufe. Auf LIVE zeigt ein Feld links neben dem Zifferblatt unter `ZUERST ARM` Stufe und Phase. Neben TEST BEENDEN steht das Messgerät: `PD mini`, ein INA-Kanal wie `INA 0x41 CH2` oder `SIMULATION`.

TEST BEENDEN, STOP, SAFE, Verbindungsverlust, Verlassen der Seite, Berühren des Zifferblatts, ZENTRIEREN, SWEEP, FREIGEBEN, Tippen auf einen Sollwert oder Ändern der Servo-Optionen beendet den Lauf vorzeitig. Dabei schaltet der Ausgang aus und das Servo wird freigegeben. Ein INA3221-Lauf endet außerdem beim Wechsel des Messgeräts oder nach 200ms ohne Fenster mit Strom und Spannung (4 Fenster zu 50ms): `0.2s kein INA3221-Fenster`. Diese Fenster versorgen die Schutzfunktion BLOCKIERT AB; bei dieser Lücke kann der Lauf nicht fortgesetzt werden.

Wird der AS5600 von Beginn an genutzt, toleriert der Lauf einmal innerhalb von 10s einen Ausfall bis 300ms. Hohe Motor- oder ESC-Ströme sowie das Einstecken eines Bausteins können den Sensorbus stören. Während der Lücke werden keine Winkel bewertet. Der Bericht nennt sie, etwa `AS5600-Aussetzer: 1, längster 120ms. ...`; die CSV-Spalte `angle gap (ms)` enthält die Dauer.

Ein längerer Ausfall beendet den Lauf mit `AS5600 antwortet nicht`, ein zweiter innerhalb von 10s mit `AS5600 2x in 10s ausgefallen`. Beide Fehler schalten den Ausgang aus und den Prüfstand auf SAFE. Bei WINKELQUELLE IMU lauten die entsprechenden Gründe `IMU antwortet nicht` und `IMU 2x in 10s ausgefallen`.

![Ein Ergebnis](view:servo-result)
![Ein Ergebnis, vom INA3221 gemessen](view:servo-result-ina)

LIVE zeigt das Ergebnis bis SCHLIESSEN, die Zeile ZULETZT auf TEST bis zum nächsten Lauf. Enthalten sind BESTANDEN, NICHT BESTANDEN oder ABGEBROCHEN mit Grund, längste Stellzeit, höchster Haltestrom und gespeicherte Dateien. Diese heißen `ATESTnnn.CSV` und bei geschriebenem Bericht `+ .TXT` in `USER/LOGS`. Bei Testaufzeichnungen AUS unter [SETUP](setup.md#sd-card) > SD CARD steht `TESTAUFZEICHNUNGEN AUS`; es wird keine Datei geschrieben.

## Der Endlagenlauf

Der Endlagenlauf sucht mechanische Anschläge an beiden Enden des eingebauten Servowegs. Danach kann er LEFT ANGLE und RIGHT ANGLE im Servo verringern, sodass es vor dem Anschlag stoppt. Öffne die Karte ENDLAGENLAUF unter TEST und dort OPTIONEN für die [Laufeinstellungen](servo-settings.md#die-optionen-des-endlagenlaufs).

![Der Endlagenlauf, bereit](view:servo-limrun-ready)

1. Tippe auf OPTIONEN und wähle unter ENDLAGENLAUF NUR SUCHE oder SUCHEN + PROGRAMMIEREN.
2. Wähle bei ZIEL PROG-PORT unter SERVO KST/CHASERVO oder HITEC. Die Vorgabe folgt der zuletzt unter PROGRAMMIEREN geöffneten Servoprogrammierung. Bei KST/CHASERVO kannst du unter SERVOPULSE BEHALTEN oder 1000-2000 wählen; die Vorgabe ist BEHALTEN.
3. Tippe auf das Home-Feld, um zu den Karten zurückzukehren.
4. Halte ENDLAGENLAUF STARTEN bei ARMED 2s. Ein Tipp oder kürzeres Halten startet nichts und zeigt `Halte 2s, um den Endlagenlauf zu starten.` im Hinweisband.
5. TEST bleibt sichtbar. Das Feld auf der linken LIVE-Karte zeigt Phase, Seite, Puls und angesteuerten Ausgang.

Die Zeile unter der Taste nennt die ganze Zeit Ausgang und Messgerät: `AUSGANG PROG-PORT 1 GPIO 13, STROM INA3221 0x41 CH2` oder `AUSGANG SERVO PWM GPIO 2, STROM INA3221 0x40 CH1`. Die Zeile ZIEL bleibt ebenfalls sichtbar.

Der Lauf verwendet ausschließlich den INA3221-Kanal für das Testerziel. Mit PD mini oder Modell als Messgerät bleibt ENDLAGENLAUF STARTEN grau; die Statuszeile nennt den Grund:

| Zeile | Bedingung |
|---|---|
| `ZUERST ARM` | der Prüfstand ist SAFE |
| `KEIN SERVO-PWM-PIN`, `PINS NICHT AUSGELESEN` | ZIEL sind die Ausgänge des Prüfstands, und kein Ausgang ist als SERVO PWM zugeordnet, oder die Zuordnung ist noch nicht ausgelesen |
| `INA3221 NICHT ZUR SERVOSTROMMESSUNG EINGERICHTET` | kein Kanal eines bestückten INA3221 überwacht in SETUP > SENSORS das Ziel des Testers |
| `INA3221 NICHT ERREICHBAR` | keine Verbindung, oder der INA3221 antwortet nicht |
| `NOCH KEINE INA3221-MESSFENSTER` | der INA3221 ist noch nicht das Messgerät |
| `SERVOVERSORGUNG UNTER 3.0V ODER OHNE MESSWERT` | das letzte INA3221-Fenster hat keine Schienenspannung oder weniger als 3,0V: ein ausgeschalteter Ausgang liest 0V |
| `PORT-PWM AUS. KEINE PULSE AN DIESEM PORT.` | ZIEL ist PROG-PORT und PORT-PWM ist aus |
| `MITTE MUSS ZWISCHEN PULS MIN UND PULS MAX LIEGEN` | der Pulsbereich enthält PULS MITTE nicht |
| `BERICHT WIRD NOCH GESCHRIEBEN` | ein Test oder ein Lauf ist im Gang, oder sein Bericht geht noch auf die Karte |

### Die Suche

Die Suche beginnt in der Mitte und geht zuerst zur unteren Pulsseite, in Schritten von 10us. Jede Position dauert 250ms: 120ms Einschwingen, danach Mittelung der folgenden Fenster. Ein Anschlag gilt als erkannt, wenn der Strom sowohl über dem 1,8-Fachen des freien Stroms als auch mindestens 0,15A darüber liegt. Der gemeldete Endpunkt liegt 25us vor dieser Position. PULS MIN und PULS MAX des Prüfstands bleiben unverändert.

| ZIEL | Suche |
|---|---|
| SERVO-PWM-Pin | Von PULS MITTE bis PULS MIN und PULS MAX, ohne Servoregister zu lesen oder zu schreiben. |
| PROG-PORT | Zuerst Auslesesitzung, danach von NEUTRAL des Servos bis PULSE LOWER und PULSE UPPER, nie darüber hinaus. |

Bei PROG-PORT hält die Auslesesitzung PULS MITTE, schaltet auf SAFE, liest mit VERBINDEN aus, gibt den Port mit VERLASSEN zurück und schaltet wieder auf ARMED. Vor dieser Sitzung erfolgt keine Suchbewegung. Bei KST/Chaservo endet der Lauf vor der Suche als NICHT BESTANDEN, wenn:

- kein KST- oder Chaservo-Servo ausgelesen wird (`kein KST/Chaservo ausgelesen`). Suche bei anderen Servos an einem SERVO-PWM-Pin;
- NEUTRAL außerhalb von PULSE LOWER und PULSE UPPER liegt (`NEUTRAL außerhalb LOWER/UPPER`);
- der Servopulsbereich nicht innerhalb von PULS MIN und PULS MAX liegt (`Servopulse außerhalb MIN/MAX`). Der Prüfstand bleibt SAFE, und die Anzeige nennt beide Bereiche.

Scheitert das erste VERBINDEN aus anderem Grund oder wird das Auslesen nicht abgeschlossen, endet der Lauf als ABGEBROCHEN mit `PROG-PORT-Sitzung gescheitert`.

![Die Pulse des Servos außerhalb von PULS MIN und PULS MAX](view:servo-limrun-range)

Ein KST-Servo verwendet ab Werk PULSE LOWER 900,0us und PULSE UPPER 2100,0us. STANDARD PWM reicht nur von 1000 bis 2000us; mit BEHALTEN wird dieses Servo daher abgelehnt. Wähle SERVOPULSE 1000-2000 oder einen PWM-Typ, der 900 bis 2100us abdeckt.

SERVOPULSE 1000-2000 schreibt während der Auslesesitzung PULSE LOWER 1000,0us und PULSE UPPER 2000,0us und liest sie zurück, bevor die Suche beginnt. NEUTRAL bleibt unverändert. Stimmen beide Werte bereits, entfällt das Schreiben. Bei NUR SUCHE sind dies die einzigen geschriebenen Werte.

### LEFT ANGLE und RIGHT ANGLE programmieren

Für KST/Chaservo benötigt SUCHEN + PROGRAMMIEREN ZIEL PROG-PORT mit aktivem PWM und eine schaltbare Servoversorgung. Bei manueller Verdrahtung ist dies ein PD mini mit eingeschaltetem Ausgang und mindestens 4,8V Sollwert. Geschriebene Werte gelten erst nach Aus- und Einschalten.

Ohne PD mini sind SUCHEN + PROGRAMMIEREN und SERVOPULSE 1000-2000 gesperrt. Die Karte zeigt `PROGRAMMIEREN: KEIN PD MINI ZUM AUS- UND EINSCHALTEN`. NUR SUCHE ist bei mindestens 4,8V im letzten INA3221-Fenster möglich. Ist ein PD mini vorhanden, muss sein Ausgang eingeschaltet sein. Ein Port ohne PWM oder eine Versorgung unter 4,8V sperrt auch NUR SUCHE.

Auf dem rbmod-Carrier übernimmt PROG 5V die Portversorgung und die Versorgungszyklen. Bei eingeschalteter Schiene sind SUCHEN + PROGRAMMIEREN und SERVOPULSE verfügbar. Andernfalls zeigt die Karte `PROG 5V AUS: SCHALTE ES IM FELD PROG 5V EIN`. Der PD mini an den Servoschienen wird für diesen Portlauf weder verwendet noch überwacht.

An den normalen Prüfstandsausgängen gilt NUR SUCHE; die Optionen nennen den Grund. Jeder KST/Chaservo-Lauf am PROG-PORT, einschließlich NUR SUCHE, liest zuerst das Servo aus. Fehlt ein passendes Servo, endet er mit `kein KST/Chaservo ausgelesen`. Für Hitec gelten die unten beschriebenen Voraussetzungen.

![Kein PD mini: der Lauf schreibt nichts](view:servo-limrun-nopd)

![Der Endlagenlauf beim Programmieren](view:servo-limrun)

Geschrieben wird in Sitzungen der [Servoprogrammierung](servo-programmer.md):

1. Das Servo kehrt für 1000ms in die Mitte zurück.
2. Der Lauf schaltet den Prüfstand auf SAFE.
3. VERBINDEN liest das Servo aus.
4. Ein Schreibvorgang erfolgt mit allen Grenzprüfungen der Servoprogrammierung.
5. VERLASSEN gibt den Port an PWM zurück.
6. Der Lauf schaltet auf ARMED und sucht dieselbe Seite erneut.

Vor dem ersten Schreiben werden die Servowerte auf der Karte gesichert. Die Berechnung nimmt einen linearen Weg an: Bei PULSE LOWER und PULSE UPPER erreicht das Servo den durch LEFT ANGLE und RIGHT ANGLE festgelegten vollen Weg. Der Weganteil bei Puls p lautet `(p - NEUTRAL) / (PULSE LOWER oder UPPER - NEUTRAL)`. Die Einheit der Winkelregister ist unbekannt; der lineare Zusammenhang wurde nicht gemessen.

Jede Seite mit erkanntem Anschlag erhält normalerweise zwei Schreibvorgänge. v0 ist dabei der ursprüngliche Registerwert:

1. Der Vorhalt: v0 x der Anteil am ersten blockierten Puls x 1,03, aufgerundet. Der Anschlag bleibt erreichbar, nahe an der Pulsgrenze. Der Lauf sucht diese Seite erneut. Ist sie dort frei, endet der Lauf als NICHT BESTANDEN, `kein Anschlag nach dem Vorhalt`.
2. Der Endwert: der Vorhalt x der Anteil am letzten freien Puls dieser Suche, abgerundet.

Beispiel: NEUTRAL 1500,2us, PULSE LOWER 900,0us, v0 150, die erste Suche frei bei 1000us und blockiert bei 990us. Der Vorhalt ist 150 x 0,850 x 1,03 = 132. Bei 132 ist die Suche frei bis 930us; der Endwert ist 132 x 0,950 = 125.

Würde der Vorhalt den Anschlag nahe der Pulsgrenze um weniger als 20us verschieben, schreibt der Lauf sofort den Endwert: v0 mal Weganteil beim letzten freien Puls der ersten Suche. Das erste Schreiben auf der ersten blockierten Seite ermittelt außerdem das zuständige Register.

Zum Schluss werden beide Enden erneut geprüft. Sind beide frei, lautet das Ergebnis BESTANDEN; bei einem verbleibenden Anschlag NICHT BESTANDEN. Höchstens 8 Werte werden geschrieben: zwei Pulsgrenzen, zwei je Seite und zwei zur Registerbestimmung. Werte unter 50 überschreiten eine weiche Grenze; das Halten beim Start erteilt die Zustimmung.

### Hitec-Servos

![Die Optionen des ENDLAGENLAUFS, SERVO auf HITEC](view:servo-limrun-hitec)

Bei SERVO HITEC verwendet der Lauf die [Hitec-Servoprogrammierung](servo-programmer.md) mit ihrer Auswahl SERIE. LEFT END und RIGHT END entsprechen bei D-Serie 0xB2 und 0xB0 in Zählschritten relativ zu NEUTRAL 0xC2. Classic-Servos verwenden die rohen Zellen 0x14 und 0x13, bei CCW vertauscht.

Hitec hat keine Pulsregister; SERVOPULSE entfällt. Der Lauf verwendet ein nicht gemessenes Pulsmodell: NEUTRAL 1500,0us, voller Weg bei 850,0us und 2150,0us für D-Serie beziehungsweise 900,0us und 2100,0us für Classic. PULS MIN und PULS MAX müssen diesen Bereich enthalten. Sonst erscheint `HITEC: PULS MIN/MAX MUSS 850-2150us ABDECKEN`. TYP WIDE mit 800 bis 2200us deckt beide ab.

- Bei D-Serie speichert der Schreibvorgang die Werte und startet das Servo innerhalb der Sitzung neu; ein PD mini ist nicht nötig.
- Classic-Servos benötigen bei jeder Sitzung, auch zum Auslesen, einen PD mini oder PROG 5V auf dem rbmod-Carrier: `Klassische Servos brauchen ein PD mini: Der Prüfstand schaltet die Versorgung.`
- Das Halten von 2s beim Start bestätigt die in ÄNDERUNG PRÜFEN genannten Risiken. Jeder Schreibvorgang aktiviert SCHREIBEN INS SERVO FREIGEBEN für sich und deaktiviert es danach.
- Antwortet beim ersten VERBINDEN kein Servo, endet der Lauf mit `kein Hitec-Servo ausgelesen`.
- Vor dem ersten Schreiben entsteht die Sicherungsdatei HITEC001 bis HITEC999. Lade und schreibe sie zum Wiederherstellen unter DATEIEN der Hitec-Servoprogrammierung. Die Anzeige fordert dazu mit `HITEC001 IN DATEIEN LADEN` auf.

### Was einen Endlagenlauf beendet

Jeder dieser Gründe beendet den Lauf als ABGEBROCHEN, mit abgeschaltetem Ausgang und freigegebenem Servo:

- LAUF BEENDEN, STOP, SAFE, Verbindungsverlust, Verlassen der Seite oder Änderung der Servo-Optionen;
- Wechsel weg vom INA3221 als Messgerät oder 200ms ohne INA3221-Fenster (`0.2s kein INA3221-Fenster`);
- bei AS5600 seit Laufbeginn ein Ausfall über 300ms (`AS5600 antwortet nicht`) oder ein zweiter innerhalb von 10s (`AS5600 2x in 10s ausgefallen`). Bei WINKELQUELLE IMU gelten dieselben Grenzen mit `IMU antwortet nicht` und `IMU 2x in 10s ausgefallen`. Kürzere, seltenere Lücken werden toleriert und im Bericht gezählt;
- während der Suche ein Fenster ohne Schienenspannung (`kein Spannungswert im Fenster`) oder mit Mittelwert unter 3,0V, einschließlich des ersten Fensters (`Netzteil oder Spannung geändert`);
- ein Einzelwert ab 3,0A, ein Wert an der INA3221-Bereichsgrenze oder Fenstermittelwerte über 1,0A für 400ms;
- Suchdauer über 40s, fehlender Zustandswechsel des Prüfstands innerhalb von 3s oder ein Programmierschritt über 150s;
- außerhalb der Programmiersitzungen eine Änderung von Netzteilsollwerten oder Ausgang, auch durch einen erst nach Laufbeginn antwortenden PD mini, unabhängig von dessen Ausgangszustand. Während der Suche gilt auch eine Abweichung der Schienenspannung um mehr als 1,0V vom ersten Fenster als Abbruchgrund. Bei Carrier-Programmierports wird stattdessen PROG 5V überwacht;
- bei einer späteren Sitzung ein anderes Servo oder geänderte REVERSION, Pulse oder Wertebereiche (`Servo hat andere Werte`).

Ein Lauf, der endet, während er auf ARMED wartet, schaltet den Prüfstand zudem auf SAFE.

![Ein nach einem Schreiben beendeter Lauf](view:servo-limrun-stopped)

Nach dem Lauf bleiben die zuletzt geschriebenen Werte im Servo. Die Anzeige fordert `ZURÜCKSPIELEN IN PROGRAMMIEREN`. Bei KST/Chaservo wähle VERBINDEN und ALLES AUSLESEN, danach unter ZURÜCKSPIELEN `Werte vor dem Endlagenlauf.` Eine spätere Sitzung legt beim ersten Schreiben ein eigenes Backup an.

![ZURÜCKSPIELEN nach einem Endlagenlauf](view:servo-limrun-restore)

### Das Ergebnis

Die LIVE-Ergebnisanzeige bleibt bis SCHLIESSEN, die Zeile ZULETZT bis zum nächsten Lauf. Angezeigt werden Ergebnis, Grund oder Werte und Berichtdateiname.

| Ergebnis | Bedeutung |
|---|---|
| GEFUNDEN | NUR SUCHE: Anschlag erkannt, Endpunkte werden gemeldet. |
| FREI | Kein Anschlag erkannt; nichts geschrieben. |
| BESTANDEN | Programmiert und beide Enden bei der letzten Suche frei, etwa `LINKS 150>87  RECHTS 150>95`. |
| NICHT BESTANDEN | Weiterhin ein Anschlag oder Servo nicht programmierbar. |
| ABGEBROCHEN | Schutzfunktion, Bedienung, Verbindung, Messgerät, Netzteil oder Servoprogrammierung hat den Lauf beendet. |

![Ein programmiertes Servo](view:servo-limrun-pass)
![Nur Suche](view:servo-limrun-found)

Der Lauf schreibt unabhängig von BERICHT eine `LIMITnnn.TXT` in `USER/LOGS`, außer bei Testaufzeichnungen AUS. Sie enthält Suchausgang, Endpunkte beider Suchen, Servopulse und mögliche Pulsänderungen sowie LEFT ANGLE und RIGHT ANGLE vorher und nachher. Hinzu kommen Weganteile, Vorhalt und Endwert, geschriebene Werte, Anzahl der Schreibvorgänge und Sitzungen, Messgerät, bei vorhandenem AS5600 die Winkel jeder letzten freien Position sowie Ergebnis oder Abbruchgrund. WLAN und Updates warten, bis die Berichtdatei geschlossen ist.

## Aktuelle Einschränkungen

- Beim Netzteilmodell entsprechen Testströme der Modelllast, nicht dem angeschlossenen Servo.
- Die Einheit von LEFT ANGLE und RIGHT ANGLE ist unbekannt. Der Endlagenlauf nimmt einen linearen Zusammenhang zwischen Puls und Weg an; dieser wurde nicht gemessen.
- Bis zum ersten Positionsbefehl laufen die Pins mit der im IOmcu aktiven Rate, nach einem Neustart mit 50Hz.

## Verwandte Seiten

- [Servo-Optionen](servo-settings.md): Typ, Pulsrate, Pulse, Sweep, Laufeinstellungen und Ziel.
- [Servoprogrammierung](servo-programmer.md): KST- und Chaservo-Zeile unter PROGRAMMIEREN.
- [Netzteil](supply.md): PD mini.
- [Sensoren](setup-sensors.md): INA3221 und AS5600.
