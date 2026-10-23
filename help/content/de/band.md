---
screen: none
title: Statusband und STOP
order: 2
---

# Statusband und STOP

Das Statusband sind die oberen 60 px jedes Bildschirms außer dem Splash, dem Busfehler-Bildschirm, PANEL UPDATE und dem Fortschritt eines IOmcu-Updates. Es nennt den Bildschirm, zeigt den Zustand des Prüfstands und trägt STOP.

## Was das Band zeigt

Von links: das Home-Tag mit dem Namen des Bildschirms, dann die Chips, dann STOP am rechten Rand.

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
| ARMED | jeder gebundene Ausgang, an einem IOmcu älter als Protokoll 4.18 |

Ein Kanal ohne Befehl seit 500 ms steht bei ARMED in Ruhe: ein Throttle auf 0 %, ein Servo in der Mitte seines Wegs.

## STOP

STOP wirkt auf jedem Bildschirm mit Band. Es schaltet den Prüfstand auf SAFE, schaltet den Ausgang des Netzteils ab und rastet ein: Der Prüfstand bleibt SAFE, bis er erneut auf ARMED geschaltet wird. Ein Bildschirmwechsel, ein ablaufender Alert oder ein zurückkehrender Link hebt einen Stopp nicht auf.

- STOP reagiert auf einen Druck, der auf ihm aufsetzt, beim Aufsetzen. Ein Finger, der vom Bildschirm auf STOP rutscht, stoppt nichts. Ein Finger, der auf STOP aufsetzt und wegrutscht, stoppt trotzdem.
- Ein zweiter Finger auf STOP wird bedient, während der erste zieht, und der Drag endet bei diesem Druck.
- Ist der Kasten eines Chips offen, schließt ein Druck auf STOP den Kasten und stoppt sofort.

## Hilfe zu den Chips

![Der Kasten unter dem Chip VERBINDUNG](view:band-link)
![Der Kasten unter dem Chip SAFE](view:band-safe)

Ein Tipp auf einen Chip öffnet einen Kasten unter dem Band. Er sagt, was der Zustand des Chips für den Prüfstand bedeutet und was zu tun ist. Der Kasten ist 640 px breit und öffnet 4 px unter dem Band. Ein Tipp auf einen anderen Chip schließt ihn und öffnet den Kasten dieses Chips. Jeder andere Tipp schließt ihn und tut sonst nichts, außer auf DISARM, AUSGANG AUS, ABBRECHEN eines Stick-Laufs, TEST BEENDEN oder LAUF BEENDEN: Diese wirken mit demselben Druck, wie ohne Kasten. Das Verlassen des Bildschirms schließt ihn ebenfalls. Der Text folgt dem Zustand, solange der Kasten offen ist.

![Der Kasten unter ARMED MOTOR](view:band-armed)

![VERBINDUNG mit 37 abgelehnten Link-Frames](view:band-link-refused)

Hat der IOmcu seit seinem Einschalten Link-Frames abgelehnt, endet der Kasten von VERBINDUNG mit ihrer Zahl, zum Beispiel `Abgelehnte Verbindungsframes: 37`.

## FEHLER-Codes

![Der Kasten von FEHLER mit drei gesetzten Bits](view:band-fault)

Der Kasten von FEHLER reicht über das Panel, 1008 px, mit einem Absatz je gesetztem Bit:

| Code | Name | In diesem Release gesetzt, wenn |
|---|---|---|
| 01 | LINK_SILENT | der IOmcu 200 ms keine Anfrage gehört und auf SAFE geschaltet hat; bleibt bis zum nächsten ARM |
| 02 | OVERCURRENT | nie |
| 04 | OVERTEMP | nie |
| 08 | STALL | nie |
| 10 | HEARTBEAT | die Sicherheitsleitung vom Panel steht: nach jedem STOP bis zum nächsten ARM, bei gedrücktem Not-Aus oder offener Heartbeat-Leitung |
| 20 | VERSION | nie |
| 40 | STORE_OFF | der IOmcu kann in diesem Start nicht in seinen Flash schreiben; Einstellungen, die er bekommt, gelten bis zu seinem Neustart |

Bei einem Bit, das dieses Release nie setzt, sagt der Kasten, beide Platinen mit demselben Release zu flashen.

## Das Alert-Band

![Ein Alert über den unteren Rand des Menüs](view:overview-alert)

Eine Störung, die der offene Bildschirm nicht zeigt, erscheint in einem roten Band, 44 px hoch, über den unteren Rand des Bildschirms. Es verdeckt, was darunter liegt; ARM und die Bedienelemente, die den Prüfstand anhalten, enden darüber.

- Es verschwindet 30 s nach seinem Eintreffen. Ein neuer Alert startet die 30 s neu.
- Ein Tipp auf das Band, rechts mit einem `x` markiert, löscht es früher. Der Bildschirm darunter bekommt weder diesen Tipp noch einen zweiten Finger auf dem Band.
- DISARM, AUSGANG AUS, ABBRECHEN eines Stick-Laufs, TEST BEENDEN und LAUF BEENDEN enden über dem Band: Ein Druck darauf gehört dem Bedienelement, und der Alert bleibt. ARM und AUSGANG EIN unter dem Band werden nicht gedrückt.
- "Touchscreen antwortet nicht. ARMED ist gesperrt.", beim Start gezeigt, bleibt bis zum Neustart und hat kein `x`.

## Berührungen nahe am Band

- Eine Berührung, die auf dem Band aufsetzt, bleibt beim Band. Rutscht sie in den Bildschirm, bewegt sie dort nichts.
- Eine Berührung, die auf dem Bildschirm aufsetzt und nach oben ins Band rutscht, wird an ihrer letzten Stelle auf dem Bildschirm losgelassen. Ein Schieberegler oder das SERVO-Zifferblatt behält den Wert von dort. Ein Halten auf ARM oder AUSGANG EIN ist abgebrochen.
- AUSGANG AUS und START TEST, solange ein Servotest läuft, nehmen ein solches Loslassen an, weil sie etwas anhalten. DISARM hat schon beim Aufsetzen gewirkt.
- Ein Druck auf das Band, auf STOP oder auf das Alert-Band beendet einen Drag, den ein anderer Finger auf einem Schieberegler oder dem SERVO-Zifferblatt macht.
