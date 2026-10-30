---
screen: none
title: Statusband und STOP
order: 2
---

# Statusband und STOP

Das Statusband sind die oberen 60px jedes Bildschirms außer dem Splash, dem Busfehler-Bildschirm, PANEL UPDATE und dem Fortschritt eines IOmcu-Updates. Es nennt den Bildschirm, zeigt den Zustand des Prüfstands und trägt STOP.

## Was das Band zeigt

Von links: das Home-Tag mit dem Namen des Bildschirms, dann die Chips, dann "?" auf einem Bildschirm mit Hilfe, dann STOP am rechten Rand.

| Chip | Zeigt |
|---|---|
| VERBINDUNG, KEINE VERBINDUNG | ob der IOmcu auf dem CAN-Bus (Controller Area Network) antwortet |
| BENCH, SIM | BENCH, solange der IOmcu antwortet und die Bildschirme seine Ausgänge ansteuern; SIM, solange das Panel sein eigenes Modell rechnet |
| INJECT | an Stelle von BENCH und SIM, solange ein Touch-Injector an der I2C-Adresse 0x14 (Inter-Integrated Circuit) antwortet; das Panel sucht ihn einmal, beim Start |
| FEHLER 01 bis FEHLER 7F | ein Fehlercode, den der IOmcu meldet; fehlt, solange keiner anliegt |
| SAFE, ARMED MOTOR, ARMED SERVO, ARMED | was das Schalten auf ARMED treibt |
| Laufzeituhr | die Dauer des laufenden Laufs ab dem Schalten auf ARMED, in Minuten und Sekunden; bei SAFE hält sie den letzten Lauf |

Das Home-Tag führt zum [Menü](overview.md).

## Der Arm-Chip

| Chip | Ausgänge, die Pulse bekommen |
|---|---|
| SAFE | keine |
| ARMED MOTOR | die Throttle-Ausgänge, von ARM auf MOTOR & ESC oder einem Stick-Lauf auf dessen PROGRAMMIEREN |
| ARMED SERVO | die Servo-Ausgänge und ein pulsender PROG PORT, von SERVO |
| ARMED | jeder gebundene Ausgang, an einem IOmcu älter als 0.18.0 |

Ein Kanal ohne Befehl seit 500ms steht bei ARMED in Ruhe: ein Throttle auf 0%, ein Servo in der Mitte seines Wegs.

## STOP

STOP wirkt auf jedem Bildschirm mit Band. Es schaltet den Prüfstand auf SAFE, schaltet den Ausgang des Netzteils ab und rastet ein: Der Prüfstand bleibt SAFE, bis er erneut auf ARMED geschaltet wird. Ein Bildschirmwechsel, ein ablaufender Alert oder ein zurückkehrender Link hebt einen Stopp nicht auf.

- STOP reagiert auf einen Druck, der auf ihm aufsetzt, beim Aufsetzen. Ein Finger, der vom Bildschirm auf STOP rutscht, stoppt nichts. Ein Finger, der auf STOP aufsetzt und wegrutscht, stoppt trotzdem.
- Ein zweiter Finger auf STOP wird bedient, während der erste zieht, und der Drag endet bei diesem Druck.
- Ist der Kasten eines Chips offen, schließt ein Druck auf STOP den Kasten und stoppt sofort.

## Hilfe zu den Chips

![Der Kasten unter dem Chip VERBINDUNG](view:band-link)
![Der Kasten unter dem Chip SAFE](view:band-safe)

Ein Tipp auf einen Chip öffnet einen Kasten unter dem Band. Er sagt, was der Zustand des Chips für den Prüfstand bedeutet und was zu tun ist. Der Kasten ist 640px breit und öffnet 4px unter dem Band. Ein Tipp auf einen anderen Chip schließt ihn und öffnet den Kasten dieses Chips. Jeder andere Tipp schließt ihn und tut sonst nichts, außer auf DISARM, AUSGANG AUS, ABBRECHEN eines Stick-Laufs, TEST BEENDEN oder LAUF BEENDEN: Diese wirken mit demselben Druck, wie ohne Kasten. Das Verlassen des Bildschirms schließt ihn ebenfalls. Der Text folgt dem Zustand, solange der Kasten offen ist.

![Der Kasten unter ARMED MOTOR](view:band-armed)

![VERBINDUNG mit 37 abgelehnten Link-Frames](view:band-link-refused)

Hat der IOmcu seit seinem Einschalten Link-Frames abgelehnt, endet der Kasten von VERBINDUNG mit ihrer Zahl, zum Beispiel `Abgelehnte Verbindungsframes: 37`.

## FEHLER-Codes

![Der Kasten von FEHLER mit drei gesetzten Bits](view:band-fault)

Der Kasten von FEHLER reicht über das Panel, 1008px, mit einem Absatz je gesetztem Bit:

| Code | Name | In diesem Release gesetzt, wenn |
|---|---|---|
| 01 | LINK_SILENT | der IOmcu 200ms keine Anfrage gehört und auf SAFE geschaltet hat; bleibt bis zum nächsten ARM |
| 02 | OVERCURRENT | nie |
| 04 | OVERTEMP | nie |
| 08 | STALL | nie |
| 10 | HEARTBEAT | die Sicherheitsleitung vom Panel steht: nach jedem STOP bis zum nächsten ARM, bei gedrücktem Not-Aus oder offener Heartbeat-Leitung |
| 20 | VERSION | nie |
| 40 | STORE_OFF | der IOmcu kann in diesem Start nicht in seinen Flash schreiben; Einstellungen, die er bekommt, gelten bis zu seinem Neustart |

Bei einem Bit, das dieses Release nie setzt, sagt der Kasten, beide Platinen mit demselben Release zu flashen.

## Die Bildschirmhilfe

![Die Hilfe des Menüs auf Seite 1](view:help-overview)

"?", 8px links von STOP, öffnet die Hilfe des Bildschirms, der oben ist. Das Menü hat eine; die anderen Bildschirme zeigen noch kein "?". Die Hilfe nummeriert bis zu 5 Bedienelemente des Bildschirms mit einem Abzeichen und Winkeln an den Ecken und listet sie in einem Dock von 480 x 488px auf einer Hälfte des Bildschirms: den Namen jedes Bedienelements, dann was es tut, in höchstens 3 Zeilen. Die andere Hälfte läuft weiter. Die letzte Zeile des Docks nennt die Adresse dieser Hilfe.

- VORIGE und NÄCHSTE blättern. SCHLIESSEN oder ein Tipp auf "?" schließt die Hilfe.
- Ein Tipp an jede andere Stelle schließt sie und tut sonst nichts.
- STOP wirkt durch die Hilfe: Ein Druck auf STOP schließt sie und stoppt sofort. DISARM, AUSGANG AUS, ABBRECHEN eines Stick-Laufs, TEST BEENDEN und LAUF BEENDEN wirken mit demselben Druck, wie ohne Hilfe.
- Der Knopf bewegt nichts, solange die Hilfe offen ist.
- In ARMED zeigt die Hilfe nur die Seiten, die kein Bedienelement verdecken, das einen Ausgang anhält, und keine ZIEL-Zeile.

## Das Alert-Band

![Ein Alert über den unteren Rand des Menüs](view:overview-alert)

Eine Störung, die der offene Bildschirm nicht zeigt, erscheint in einem roten Band, 44px hoch, über den unteren Rand des Bildschirms. Es verdeckt, was darunter liegt; ARM und die Bedienelemente, die den Prüfstand anhalten, enden darüber.

- Es verschwindet 30s nach seinem Eintreffen. Ein neuer Alert startet die 30s neu. Eine Meldung, der Hinweis eines [Halte-Buttons](#halte-buttons), steht 4s in Bernstein und weicht einem Alert.
- Ein Tipp auf das Band, rechts mit einem `x` markiert, löscht es früher. Der Bildschirm darunter bekommt weder diesen Tipp noch einen zweiten Finger auf dem Band.
- DISARM, AUSGANG AUS, ABBRECHEN eines Stick-Laufs, TEST BEENDEN und LAUF BEENDEN enden über dem Band: Ein Druck darauf gehört dem Bedienelement, und der Alert bleibt. ARM und AUSGANG EIN unter dem Band werden nicht gedrückt.
- "Touchscreen antwortet nicht. ARMED ist gesperrt.", beim Start gezeigt, bleibt bis zum Neustart und hat kein `x`.

## Halte-Buttons

Ein Halte-Button wirkt nach 2s Kontakt. ARM, AUSGANG EIN, START TEST, START LIMIT RUN, ÜBERNEHMEN und jeder Button mit der Beschriftung `2s HALTEN ZUM ...` sind Halte-Buttons. Ein Loslassen vor 2s sendet nichts und zeigt den Hinweis des Buttons 4s lang im Alert-Band, in Bernstein statt Rot, zum Beispiel `Halte 2s, um auf ARMED zu schalten.`

- Der Hinweis erscheint nur bei einem Loslassen, das der Finger auf dem Button gemacht hat. Er erscheint nicht, wenn der Finger vorher vom Button gerutscht ist, wenn der Touch verloren ging, bei dem Kontakt eines zweiten Fingers und auf einem Bildschirm ohne Statusband.
- Ein Loslassen bei 2s oder später hat gewirkt und zeigt keinen Hinweis.
- Ein kurzes Tippen ist keine Störung, also ist der Hinweis eine Meldung und kein Alert: Ein Tipp auf das Band löscht ihn früher, ein stehender Alert behält das Band und kein Hinweis erscheint darüber, und ein Alert, der während des Hinweises kommt, ersetzt ihn.
- Die Hinweise, einer je Aktion, jeweils nach `Halte 2s, um`: `auf ARMED zu schalten.`, `den Ausgang einzuschalten.`, `zu übernehmen.`, `den Test zu starten.`, `den Endlagenlauf zu starten.`, `freizugeben.`, `zu schreiben.`, `zurückzuspielen.`, `fortzufahren.`, `zu verlassen.`, `zu laden.`, `den Port freizugeben.`, `zu reparieren.`, `zu starten.`, `zu installieren.`, `zu laden und zu installieren.`, `den IOmcu zu aktualisieren.` und `alle Pin-Zuordnungen zu löschen.`

## Berührungen nahe am Band

- Eine Berührung, die auf dem Band aufsetzt, bleibt beim Band. Rutscht sie in den Bildschirm, bewegt sie dort nichts.
- Eine Berührung, die auf dem Bildschirm aufsetzt und nach oben ins Band rutscht, wird an ihrer letzten Stelle auf dem Bildschirm losgelassen. Ein Schieberegler oder das SERVO-Zifferblatt behält den Wert von dort. Ein Halten auf ARM oder AUSGANG EIN ist abgebrochen.
- AUSGANG AUS und START TEST, solange ein Servotest läuft, nehmen ein solches Loslassen an, weil sie etwas anhalten. DISARM hat schon beim Aufsetzen gewirkt.
- Ein Druck auf das Band, auf STOP oder auf das Alert-Band beendet einen Drag, den ein anderer Finger auf einem Schieberegler oder dem SERVO-Zifferblatt macht.
