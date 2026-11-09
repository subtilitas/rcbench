---
screen: none
title: Ruderausschlag
order: 23
---

# Ruderausschlag

RUDER im Hub von [SERVO](servo.md) steuert ein Servo und zeigt den Ruderausschlag. Die Anzeige nennt Grad ab einem Nullpunkt und bei eingegebener Ruderflächentiefe auch Millimeter an der Hinterkante. WINKELQUELLE wählt den AS5600-Ausgangsencoder oder die IMU: ein Tippen auf die Zeile WINKEL öffnet [ihre Liste](#die-winkelquelle). Wie bei GEMESSEN auf LIVE folgt das Vorzeichen der Richtung des Sollwinkels. Die rechte Karte zeigt die Taste PORT und die Zeilen von BEREIT: PORT, WINKEL, NULLPUNKT, VERSORGUNG und ARM. ARM in der Fußleiste schaltet PORT-PWM und die Versorgung ein, wo sie aus sind, und schaltet dann auf ARMED ([SERVO, ARMED](servo.md#armed)).

![RUDER bei SAFE](view:servo-rudder)

## Die Anzeige

| Teil | Anzeige |
|---|---|
| AUSSCHLAG | Winkel ab NULLPUNKT, -180,0 bis +179,9° vom AS5600, -180,00 bis +180,00° von der IMU, ab 100,00° in kleineren Ziffern. Vor dem Nullsetzen gilt SENSOR-MITTE des AS5600 oder Zählwert 0 der IMU als Bezug; ohne Messwert steht `---`. |
| HINTERKANTE | L x sin(Ausschlag) bei LÄNGE L mm: der vorzeichenbehaftete Wert eines senkrecht an die Hinterkante gehaltenen Lineals. |
| MAX | Größter Ausschlag je Seite seit NULLPUNKT oder MAX LÖSCHEN, in ° und mm. |

Der Millimeterwert ist der Abstand der Hinterkante von der Neutralebene, nicht die zurückgelegte Bogenlänge. Bei 30° und 100mm Ruderflächentiefe beträgt er 50,0mm.

## Die Seitenansicht

Die Seitenansicht steht rechts neben den Statuszeilen, mit der Nasenkante links. Sie zeigt die feste Fläche grau, das Scharnier und das Ruder als cyanfarbenen Keil im angezeigten Winkel. Positiver Ausschlag hebt die Hinterkante an. Striche markieren 0, ±30, ±60 und ±90°; gestrichelte Linien zeigen MAX je Seite. Die Zeichnung endet bei ±90°, die Zahlenanzeige läuft darüber hinaus weiter. Die gezeichnete Ruderlänge bleibt fest. Ohne Messwert liegt das Ruder gedimmt auf der Neutrallinie.

![Die Fläche bei -85,0°](view:servo-rudder-85)

## LÄNGE

LÄNGE ist der Abstand von der Scharnierlinie zur Hinterkante: 0 oder 10 bis 500mm in Schritten von 1mm. Die Einstellung bleibt nach einem Neustart erhalten. Tippe darauf, um das Tastenfeld zu öffnen. Werte von 1 bis 9mm werden mit `0 oder 10 bis 500mm` abgelehnt. Bei 0 zeigt die Anzeige nur Grad und fordert in der mm-Spalte die LÄNGE an.

![LÄNGE nicht gesetzt: nur Grad](view:servo-rudder-nolength)

## Das Servo ansteuern

![RUDER bei ARMED, auf 2000us](view:servo-rudder-armed)

| Bedienelement | SAFE | ARMED |
|---|---|---|
| 1000us, 2000us | gesperrt und gedimmt | gibt diesen Puls vor, begrenzt durch WEG und den Pulsbereich des eingestellten Typs |
| ZENTRIEREN | gesperrt und gedimmt | gibt PULS MITTE plus TRIMMUNG vor |
| Drehknopf | gesperrt; Drehungen werden verworfen | bewegt das Horn ausgehend vom Sollwert, wie auf LIVE; bei laufendem AUTO gesperrt |
| NULLPUNKT | setzt den aktuellen Winkel als Neutrallage und löscht MAX; ohne Messwert grau. Ein Nullpunkt am AS5600 wird gespeichert und kommt nach einem Neustart zurück; ein Nullpunkt an der IMU gilt bis zum Neustart, weil ihre Verbindung nicht authentifiziert ist. Änderungen an WINKELQUELLE, IMU-Achse, IMU-Vorzeichen oder IMU-Algorithmus sowie NULLPUNKT oder NULLPUNKT LÖSCHEN auf der IMU-Seite verwerfen Nullpunkt und MAX. | ebenso; bei laufendem AUTO gesperrt |
| MAX LÖSCHEN | löscht MAX und behält den Nullpunkt | ebenso; bei laufendem AUTO gesperrt |

1000us, ZENTRIEREN und 2000us wirken beim Drücken. LÄNGE, NULLPUNKT und MAX LÖSCHEN wirken, wenn du einen Tipp darauf loslässt. PULS unter den Tasten zeigt den Sollpuls. DISARM, STOP oder das Verlassen der Seite schaltet den Prüfstand auf SAFE. [RUDER AUTO](#ruder-auto) programmiert die Endpunkte auf einer eigenen Seite.

## Die Winkelquelle

Ein Tippen auf die Zeile WINKEL von BEREIT, mit Messwert oder ohne, öffnet bei SAFE die Liste WINKELQUELLE über der linken Karte. Bei ARMED oder während eines Laufs wird das Tippen verweigert, der Grund steht im Band. Die Liste fasst bis zu 10 Einträge:

| Eintrag | Ein Tippen |
|---|---|
| `AS5600` | setzt WINKELQUELLE AS5600 und schließt die Liste |
| `AS5600: AUS IN SETUP` | öffnet den Eintrag AS5600 unter [Sensoren](setup-sensors.md); das Home-Tag kehrt hierher zurück |
| ein Sensor, `HC-06 00:0C:BF:08:19:29 P -50dBm`: Name, seine ganze Adresse, ihr Typ (`P` öffentlich, `R` zufällig), das Signal; in zwei Spalten auf zwei Zeilen | bindet ihn, schaltet die IMU ein und setzt WINKELQUELLE IMU |
| `KEINE IMU GEFUNDEN` | die letzte Suche fand keine; das Tippen nennt die üblichen Ursachen im Band |
| `KEINE ANDERE IMU GEFUNDEN` | die letzte Suche fand keine, während der gebundene Sensor verbunden ist: ein verbundener Sensor sendet keine Advertisements, seine Zeile darüber bleibt die einzige |
| `IMU SUCHEN` | schaltet die IMU ein, falls aus, und sucht 10s, sobald Bluetooth LE läuft; die Liste bleibt offen und zeigt `SUCHE: 10s`, danach die gefundenen Sensoren |

Die gültige Quelle steht in der Akzentfarbe. Bei eingeschalteter IMU ohne gebundenen Sensor startet das Öffnen der Liste von selbst eine Suche.

![Die Liste WINKELQUELLE: AS5600 gültig, zwei Sensoren gefunden](view:servo-rudder-angle)

![Sechs gefundene Sensoren in zwei Spalten, zwei mit gleichem Adressende](view:servo-rudder-angle-many)

![Eine laufende Suche](view:servo-rudder-angle-scan)

![AS5600 aus in SETUP, eine Suche ohne Fund](view:servo-rudder-angle-none)

## Die Statuszeilen

| Zeile | Bedeutung |
|---|---|
| `WINKEL: AS5600`, `WINKEL: IMU X+` | Quelle liefert Messwerte; bei der IMU stehen Achse und Vorzeichen dabei. |
| `WINKEL: AS5600 AUS IN SETUP` | AS5600 unter SETUP > SENSORS ausgeschaltet. |
| `WINKEL: AS5600 ANTWORTET NICHT`, `WINKEL: IMU X+ ANTWORTET NICHT` | Quelle eingeschaltet, aber ohne Messwert; bei der IMU auch ausgeschaltet oder nicht verbunden. |
| `WINKEL: AS5600 SIEHT KEINEN MAGNETEN` | Sensor meldet keinen Magneten. |
| `WINKEL: KEINE VERBINDUNG` | Keine Verbindung zum IOmcu, der RP2350-Platine. |
| `KEIN NULLPUNKT: AB SENSORMITTE` | NULLPUNKT noch nicht gesetzt. |
| `ZUERST ARM` | Prüfstand ist SAFE. |

## RUDER AUTO

RUDER AUTO, eine eigene Kachel im Hub, programmiert ein KST-, Chaservo- oder Hitec-Servo am Programmierport so, dass 1000us und 2000us den je Seite vorgegebenen Ausschlag erreichen. Sein SERVO, KST/CHASERVO, HITEC D-SERIE oder HITEC KLASSISCH, ist die eigene gespeicherte Wahl von RUDER AUTO, getrennt von der des Endlagenlaufs; ein Tippen auf die Zeile SERVO der Karte BEREIT wählt es. Die dritte Zeile nennt sie, etwa `SERVO HITEC D-SERIE`.

Die linke Karte hält Anzeige, Sollwerte, RESERVE und TOLERANZ, NULLPUNKT und MAX LÖSCHEN, die Seitenansicht und die Statuszeilen. Die Seitenansicht zeigt beide Sollwerte als bernsteinfarbene gestrichelte Linien: 1000us auf der negativen, 2000us auf der positiven Seite, oder jeweils auf der vom letzten Lauf gemessenen Seite.

![RUDER AUTO an den Ausgängen des Prüfstands: PORT rot, das Halten grau](view:servo-rudder-auto)

| Zeile | Einstellung |
|---|---|
| 1000us, 2000us | Soll-Ausschlag je Seite, 0,5 bis 90,0°, Vorgabe 20,0°. Bei gesetzter LÄNGE stehen daneben die mm an der Hinterkante. |
| RESERVE | Anteil über dem Soll-Ausschlag, 0 bis 50%, Vorgabe 10%: 20,0° bei 10% ergeben einen Zielwert von 22,0°. |
| TOLERANZ | Zulässige Abweichung vom Zielwert, 0,1 bis 5,0°, Vorgabe 0,5°. |

### BEREIT

Die Karte BEREIT rechts zeigt, was der Start braucht, eine Zeile je Punkt: PORT, SERVO, MESSGERÄT, WINKEL, NULLPUNKT, BEREICH, PROGRAMMER, die Versorgung (PROG 5V auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de), PD MINI, wo das PD mini das Servo speist), PORT-PWM und ARM. PROG 5V und PD MINI zeigen die `5.0V` des Laufs. Ein grüner Punkt ist erfüllt. Ein roter Punkt sperrt das Halten oder zeigt einen über die Zeile aktiven Ausgang. Gelb zeigt `START SCHALTET EIN` oder `START SCHALTET ARMED`: das erledigt das Halten. Grau wartet auf PORT. Ein Tippen auf PORT öffnet die Port-Auswahl, auf SERVO die drei Programmierer über der linken Spalte, auf WINKEL die [Liste WINKELQUELLE](#die-winkelquelle), auf einen roten NULLPUNKT setzt es den Nullpunkt, auf ein rotes MESSGERÄT öffnet es [Sensoren](setup-sensors.md), auf ein rotes PD MINI mit `PD MINI: 5.0V AUSSERHALB SEINER GRENZEN` die Grenzen in der Optionsliste von [SUPPLY](supply.md). Ein Tippen auf eine andere rote Zeile nennt sie im Alarmband. Die Zeile unter den Zeilen ist das Messgerät des Laufs.

![Auf dem rbmod-Carrier, SAFE: PROG 5V, PORT-PWM und ARM gelb, das Halten grün](view:servo-rudder-auto-rbmod)

### ARM + START

Setze zuerst NULLPUNKT bei neutraler Ruderfläche. Halte dann 2s HALTEN: ARM + START in der Fußleiste. Das Halten stellt ein PD mini auf 5,0V, schaltet PORT-PWM ein, dann PROG 5V oder den Ausgang des PD mini. Es wartet bis zu 1000ms auf deren Rückmeldung EIN, das PD mini auf 5,0V, und auf ein danach genommenes Messfenster, schaltet ARM, wartet bis zu 500ms auf ARMED und startet den Lauf. Der Puls läuft erst ab dem ARM. Die Taste zeigt den Schritt in Gelb, die erste Zeile `START, SCHRITT 3 VON 5: RÜCKMELDUNG`. Ein Tippen darauf stoppt die Folge. Während der Schritte nehmen die linke Karte, OPTIONEN und die Karte BEREIT keinen Druck an, und der Lauf startet mit den Einstellungen beim Auslösen des Haltens. Während des Laufs heißt sie LAUF BEENDEN; ein Druck beendet den Lauf sofort.

![Die Folge wartet auf die Rückmeldung von PROG 5V](view:servo-rudder-auto-start-rbmod)

Ein fehlgeschlagener Schritt nennt sich im Alarmband und in der ersten Zeile, etwa `PROG 5V FEHLGESCHLAGEN: KEINE RÜCKMELDUNG IN 1000ms`, und schaltet alle Ausgänge aus: der Prüfstand ist SAFE, PROG 5V und der Ausgang des PD mini sind aus.

![PROG 5V ohne Rückmeldung in 1000ms](view:servo-rudder-auto-failed-rbmod)

Der Lauf liest das Servo aus und setzt bei KST den Pulsbereich auf 1000 bis 2000us. Je Seite fährt er den Endpuls an, wartet auf einen 500ms lang ruhigen Winkel und schreibt einen neuen Endpunkt. Mit der IMU muss nur die IMU-Achse ruhig sein; die anderen Achsen dürfen sich bewegen. Mit der IMU und einem [IMU-Filter](setup-sensors.md#die-imu) über 1 wartet er zusätzlich, bis der gefilterte Winkel eingeschwungen ist, bis zu 3000ms plus das längste Einschwingen des Filters, das der Bericht nennt. Er schreibt höchstens 8-mal je Seite, jeweils in einer eigenen Programmiersitzung mit 5,0V und Aus- und Einschalten des Servos ([Die Versorgung: 5,0V](servo.md#die-versorgung-5-0v)); ein PD mini bleibt nach dem Lauf auf 5,0V. Der Stromschutz des Endlagenlaufs bleibt durchgehend aktiv. Blockiert das Ruder vor dem Zielwert, endet der Lauf mit `Ruder klemmt bei x°`.

Welcher Endpunkt welche Seite bewegt: bei einem Hitec-Servo der D-Serie mit CW ist LEFT END die 1000us-Seite und RIGHT END die 2000us-Seite. Bei jedem anderen Servo findet es das erste Schreiben: zuerst der Endpunkt mit dem Namen der Seite, der andere, wenn sich die Seite nicht bewegt. Eine Seite, die ihr Ziel schon erreicht, bekommt kein Schreiben; das erste Schreiben der anderen Seite findet beide. Zwei Schreibvorgänge hintereinander, die ihre Seite nicht bewegen oder in die falsche Richtung, beenden den Lauf mit `Schreiben bewegt 2000us-Seite nicht` (oder 1000us-Seite). Kein Schreiben bewegt einen Endpunkt mehr als 50% von der ersten Schätzung weg, die der Lauf für ihn macht (sein Wert beim Lesen x Sollwert / erste Messung); eines, das es täte, beendet den Lauf mit `Endpoint zu weit von der Schätzung`. Ein Schreiben zählt, sobald seine erwartete Änderung mindestens TOLERANZ beträgt; es hat die Seite bewegt, wenn sich der Winkel um die Hälfte der erwarteten Änderung oder um TOLERANZ geändert hat, je nachdem was kleiner ist. Bei jedem Ende schaltet der Prüfstand auf SAFE und der Netzteilausgang aus. Während des Laufs sind NULLPUNKT und MAX LÖSCHEN gesperrt.

Die zweite Zeile nennt das Ergebnis; nach BESTANDEN zeigt sie jeden Endpunkt vorher und nachher so, wie die Programmierseite ihn zeigt, mit Einheit: `Zählwerte` bei einem Hitec-Servo der D-Serie (8192 die Mitte, wie WEG auf der HITEC-Seite), `Rohwert` bei KST und einem Hitec-Classic-Servo. Die dritte nennt den Bericht RUDDRnnn.TXT. Hat ein nicht bestandener Lauf Einstellungen geschrieben oder einen Schreibvorgang abgebrochen, steht dort stattdessen `ZURÜCKSPIELEN IN KST`, bei Hitec `HITEC001 IN DATEIEN LADEN`, bis ein Lesen oder Schreiben auf der KST- oder HITEC-Seite die Werte vor dem Lauf zeigt oder Werte, die der Lauf nicht hinterlassen hat.

Mit der IMU als WINKELQUELLE auf Achse Z (Gieren) und IMU-Algorithmus 6-AXIS zeigt die Zeile WINKEL `IMU-GIEREN BENÖTIGT 9-AXIS` in Rot, und das Halten wird verweigert: ohne Magnetometer driftet das Gieren etwa 2,4° in 100s, mehr als TOLERANZ. Ein Tippen auf die Zeile bietet `IMU AUF 9-AXIS SCHALTEN`, `IMU-ACHSE X NEHMEN (ROLLEN)` und `IMU-ACHSE Y NEHMEN (NICKEN)`; eine Wahl setzt und speichert sie. Gieren ist erst frei, wenn der Sensor 9-AXIS übernommen hat, das Schreiben und sein SAVE; bis dahin, oder nach einem abgewiesenen Schreiben, zeigt die Zeile `IMU 9-AXIS NICHT BESTÄTIGT` in Rot, und ein Tippen bietet dieselbe Liste. Eine andere Achse oder ein anderer Algorithmus braucht einen neuen NULLPUNKT.

![Die OPTIONEN von RUDER AUTO](view:servo-rudder-auto-settings)

## Was der Winkel ist

Der AS5600 misst den Winkel der Servoabtriebswelle. Durch ein Gestänge kann der Ruderausschlag vom Wellenwinkel abweichen; diese Differenz wird nicht gemessen. Die IMU misst den Winkel des Teils, an dem sie befestigt ist. Auf der Ruderfläche misst sie deren eigenen Winkel.
