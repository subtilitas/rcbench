---
screen: none
title: Statusband und STOP
order: 2
---

# Statusband und STOP

Das Statusband belegt die oberen 60px jeder Seite außer Splash, Busfehler-Seite, PANEL UPDATE und Fortschrittsanzeige des IOmcu-Updates. Es zeigt den Seitennamen und den Zustand des Prüfstands und enthält STOP.

## Was das Band zeigt

Von links nach rechts stehen das Home-Feld mit dem Seitennamen, die Status-Chips, ein "?" bei verfügbarer Hilfe und STOP am rechten Rand.

| Chip | Anzeige |
|---|---|
| VERBINDUNG, KEINE VERBINDUNG | ob der IOmcu auf dem CAN-Bus (Controller Area Network) antwortet |
| BENCH, SIM | BENCH, solange der IOmcu antwortet und die Seiten seine Ausgänge ansteuern; SIM, solange das Panel sein eigenes Modell berechnet |
| INJECT | ersetzt BENCH und SIM, solange ein Touch-Injector an der I2C-Adresse 0x14 (Inter-Integrated Circuit) antwortet; das Panel sucht ihn einmal beim Start |
| FEHLER 01 bis FEHLER 7F | ein vom IOmcu gemeldeter Fehlercode; ohne Fehler ist der Chip ausgeblendet |
| SAFE, ARMED MOTOR, ARMED SERVO, ARMED, STOP | welche Ausgänge bei ARMED angesteuert werden; STOP bei eingerastetem Stopp |
| Laufzeituhr | die Laufdauer seit dem Schalten auf ARMED in Minuten und Sekunden; bei SAFE bleibt die Dauer des letzten Laufs stehen |

Das Home-Feld führt zum [Menü](overview.md).

## Der Arm-Chip

| Chip | Ausgänge mit Pulsen |
|---|---|
| SAFE | keine |
| ARMED MOTOR | die Gas-Ausgänge, aktiviert durch ARM auf MOTOR & ESC oder durch einen Stick-Lauf unter PROGRAMMIEREN |
| ARMED SERVO | die Servoausgänge und ein pulsender PROG-PORT, aktiviert auf SERVO |
| ARMED | alle zugeordneten Ausgänge, bei einem IOmcu älter als 0.18.0 |

Erhält ein Kanal 500ms lang keinen Befehl, nimmt er auch bei ARMED seine Ruheposition ein: Gas geht auf 0%, ein Servo auf die Mitte seines Wegs.

## STOP

STOP wirkt auf jeder Seite mit Statusband. Ein Druck schaltet den Prüfstand sofort auf SAFE und schaltet alle Ausgänge, den Netzteilausgang und die 5V der Programmierports aus. Der Stopp rastet wie ein Not-Aus-Taster ein. Ein Seitenwechsel, ein gelöschter Alarm, die Rückkehr der Verbindung oder eine ARM-Anfrage löst ihn nicht.

![Ein eingerastetes STOP und sein Kasten](view:band-stop)

- Bei eingerastetem Stopp bleibt STOP rot mit gelbem Rahmen. Die Taste zeigt `GESTOPPT` über `2s HALTEN ZUM LÖSEN`, der Arm-Chip zeigt STOP. ARM, AUSGANG EIN, PROG 5V EIN, VERBINDEN und der Start eines Tests oder Laufs werden mit `STOP gedrückt: Halte STOP 2s, um ihn zurückzusetzen.` abgelehnt. Diese Meldung verschwindet im Frame nach dem abgeschlossenen Lösen aus dem Band.
- Halte STOP 2s, um den eingerasteten Stopp zu lösen. Die Füllung wechselt dabei von Rot nach Gelb. Das Panel wartet anschließend, bis der IOmcu SAFE und alle Ausgänge als aus meldet, frühestens 500ms nach dem letzten STOP. Dann sendet es wieder den Heartbeat. Der Prüfstand bleibt SAFE. Zum Einschalten jedes Ausgangs musst du dessen Taste erneut halten.
- Antwortet der IOmcu nicht innerhalb von 2s, bleibt der Stopp eingerastet. Das Alarmband meldet `STOP nicht gelöst: Der IOmcu meldete nicht binnen 2s alle Ausgänge aus.` Halte STOP erneut.
- Jeder Druck auf das eingerastete STOP löst ebenfalls STOP aus. Zum Zurücksetzen muss ein Finger die vollen 2s auf STOP bleiben. Loslassen vor 2s zeigt `Halte 2s, um den STOP zu lösen.`. Abgleiten von STOP, ein zweiter Finger auf dem Band, ein Seitenwechsel, verlorene Touch-Ereignisse oder ein 100ms lang nicht antwortender Touch-Controller verhindern das Zurücksetzen.
- Ein Neustart des Panels löst den Stopp. Das Panel startet SAFE mit ausgeschalteten Ausgängen.

- STOP reagiert sofort, wenn der Finger auf der Taste aufsetzt. Hineingleiten vom übrigen Bildschirm löst keinen Stopp aus. Wer auf STOP aufsetzt und danach wegrutscht, hat den Stopp bereits ausgelöst.
- STOP reagiert auch auf einen zweiten Finger, während der erste einen Regler zieht. Die Ziehbewegung endet mit dem Druck auf STOP.
- Bei geöffnetem Hilfefeld eines Chips schließt STOP das Feld und stoppt sofort.

## Hilfe zu den Chips

![Der Kasten unter dem Chip VERBINDUNG](view:band-link)
![Der Kasten unter dem Chip SAFE](view:band-safe)

Tippe auf einen Chip, um sein Hilfefeld zu öffnen. Es erklärt den aktuellen Zustand und was du tun kannst. Das Feld ist 640px breit und erscheint 4px unter dem Band. Ein Tipp auf einen anderen Chip wechselt zu dessen Hilfe. Andere Berührungen schließen das Feld, ohne die darunterliegende Bedienung auszulösen. Ausgenommen sind DISARM, AUSGANG AUS, ABBRECHEN eines Stick-Laufs, TEST BEENDEN und LAUF BEENDEN: Sie wirken mit demselben Druck. Ein Seitenwechsel schließt das Feld ebenfalls. Solange es offen ist, folgt der Text dem aktuellen Zustand.

![Der Kasten unter ARMED MOTOR](view:band-armed)

![VERBINDUNG mit 37 abgelehnten Link-Frames](view:band-link-refused)

Hat der IOmcu seit dem Einschalten Verbindungsframes abgelehnt, zeigt das Hilfefeld von VERBINDUNG am Ende ihre Anzahl, etwa `Abgelehnte Verbindungsframes: 37`.

## FEHLER-Codes

![Der Kasten von FEHLER mit drei gesetzten Bits](view:band-fault)

Das Hilfefeld von FEHLER ist 1008px breit und erstreckt sich über das Panel. Jedes gesetzte Bit hat einen eigenen Absatz:

| Code | Name | In diesem Release gesetzt, wenn |
|---|---|---|
| 01 | LINK_SILENT | der IOmcu 200ms keine Anfrage gehört und auf SAFE geschaltet hat; bleibt bis zum nächsten ARM |
| 02 | OVERCURRENT | nie |
| 04 | OVERTEMP | nie |
| 08 | STALL | nie |
| 10 | HEARTBEAT | die Sicherheitsleitung vom Panel steht, obwohl das Panel sie treibt: bei gedrücktem Not-Aus oder offener Heartbeat-Leitung; ein eingerastetes STOP zeigt stattdessen STOP |
| 20 | VERSION | nie |
| 40 | STORE_OFF | der IOmcu kann in diesem Start nicht in seinen Flash schreiben; Einstellungen, die er bekommt, gelten bis zu seinem Neustart |

Bei einem Bit, das dieses Release nicht setzt, fordert das Hilfefeld dazu auf, beide Platinen mit demselben Release zu flashen.

## Screenshot

Halte die unmarkierte Lücke zwischen Home-Feld und Chips 1s lang. Nach 1s meldet das Band `Screenshot aufgenommen.`; das anschließende Loslassen löst nichts weiter aus. Ein Druck auf einen Chip, das Home-Feld, "?" oder STOP nimmt keinen Screenshot auf. Herausgleiten aus der Lücke, ein zweiter Finger oder ein verlorener Touch bricht die Geste ab.

Der Screenshot zeigt die aktuelle Anzeige samt Band und geöffneten Feldern, Listen oder Tastenfeldern. Die Aufnahme erfolgt auch bei ARMED sofort und beeinflusst keinen Ausgang. STOP und DISARM bleiben aktiv. Im Speicher liegt jeweils ein Bild; eine weitere Aufnahme ersetzt es, ein Neustart verwirft es. Splash, Busfehler-Seite, PANEL UPDATE und Fortschrittsanzeige des IOmcu-Updates haben kein Band und unterstützen diese Geste nicht.

![Das Popup des Screenshots](view:shot-popup)

Das Speicher-Popup wartet, bis der Prüfstand ruht: SAFE, Netzteilausgang und PROG 5V aus, kein belegter Port, keine Programmiersitzung, kein Sweep, Test, Lauf, Update, FEHLER oder offene Aufzeichnung. Die Verbindung muss bestehen, außer der Prüfstand läuft in der Simulation. Entfällt eine dieser Bedingungen, schließt sich das Popup. Das Bild bleibt gespeichert und wartet, bis alle Bedingungen wieder erfüllt sind.

- Ändere den Namen mit der Tastatur, bis zu 55 Bytes. Die Vorgabe ist `SHOT` mit der nächsten Nummer. Die Endung `.PNG` bleibt fest.
- `SPEICH.` schreibt `screenshots/<Name>.PNG` auf die Karte und legt einen fehlenden Ordner an. Bereits vergebene Namen werden abgelehnt; keine Datei wird überschrieben.
- ABBRECHEN verwirft das Bild. Scheitert das Speichern, etwa bei fehlender oder voller Karte, bleibt das Popup mit dem Bild geöffnet.
- Die Datei ist ein PNG (Portable Network Graphics) mit 1024 x 600px, das du im Browser oder auf einem PC ansehen kannst. Sie erscheint in [FILES](files.md) und auf der WLAN-Seite.

## Die Bildschirmhilfe

![Die Hilfe des Menüs auf Seite 1](view:help-overview)

"?", 8px links von STOP, öffnet die Hilfe zur aktuellen Seite. Das Menü, SUPPLY, SERVO, OUTPUTS, SETUP, LOGS und FILES haben eine Hilfe; die anderen Seiten haben kein "?". Jede Hilfeseite markiert bis zu 5 Bedienelemente mit Nummern und Eckmarkierungen. Ein Feld von 480 x 488px auf einer Bildschirmhälfte nennt ihre Namen und Funktionen, mit höchstens 3 Zeilen pro Bedienelement. Die andere Hälfte läuft weiter. In der letzten Zeile steht die Adresse dieser Hilfe.

- VORIGE und NÄCHSTE blättern durch die Hilfe. Sie ersetzen das Home-Feld im Band und bleiben auf jeder Seite an derselben Stelle. Wiederholtes Tippen auf dieselbe Position blättert weiter. SCHLIESSEN unten im Hilfefeld oder "?" schließt die Hilfe.
- Ein Tipp an eine andere Stelle schließt die Hilfe, ohne die darunterliegende Bedienung auszulösen.
- STOP schließt die Hilfe und stoppt sofort. DISARM, AUSGANG AUS, ABBRECHEN eines Stick-Laufs, TEST BEENDEN und LAUF BEENDEN wirken ebenfalls mit demselben Druck.
- Der Drehknopf hat bei geöffneter Hilfe keine Wirkung.
- Bei ARMED zeigt die Hilfe nur Seiten, die weder ein Bedienelement zum Anhalten eines Ausgangs noch eine ZIEL-Zeile verdecken.
- Auf SUPPLY und SERVO fehlen die Hilfeseiten, deren Hilfefeld AUSGANG AUS verdeckt, solange der Netzteilausgang an ist. Während ein Test, ein Endlagenlauf oder AUTO läuft, bietet SERVO keine Hilfeseite: sein Stopp bleibt sichtbar.

![Die Hilfe von SERVO auf Seite 1 in LIVE](view:help-servo)
![Die Hilfe von SUPPLY auf Seite 1 in LIVE](view:help-supply)

OUTPUTS, SETUP, LOGS und FILES haben eigene Hilfeseiten für jeden Zustand: Kacheln, Liste und SENSORS auf SETUP, Platinenansicht, Eintrag PROG PORT und Rasteransicht auf OUTPUTS, Dateiliste, Importansicht und Grafik auf LOGS, die Spalten auf FILES. Auf Hilfeseite 1 steht das Hilfefeld rechts. Ein Bedienelement, das das Hilfefeld zum Teil verdeckt, wird markiert, wo es sichtbar ist. Diese Seiten steuern keinen Ausgang an, daher bleiben alle ihre Hilfeseiten auch in ARMED sichtbar.

![Die Hilfe von SETUP auf Seite 1](view:help-setup)
![Die Hilfe von OUTPUTS auf Seite 1](view:help-outputs)
![Die Hilfe von LOGS auf Seite 1](view:help-logs)
![Die Hilfe von FILES auf Seite 1](view:help-files)

## Das Alert-Band

![Ein Alarm am unteren Rand des Menüs](view:overview-alert)

Eine Störung, die die geöffnete Seite nicht anzeigt, erscheint in einem roten, 44px hohen Band am unteren Bildschirmrand. Es verdeckt die darunterliegenden Inhalte. ARM und die Bedienelemente zum Anhalten des Prüfstands enden oberhalb des Bands.

- Ein Alarm verschwindet nach 30s. Ein neuer Alarm beginnt diese Zeit erneut. Der Hinweis einer [Halte-Taste](#halte-buttons) erscheint dagegen 4s lang in Bernsteinfarbe und weicht einem Alarm.
- Tippe auf das Band, um den Alarm vorzeitig zu löschen. Am rechten Rand steht ein `x`. Die Seite darunter erhält weder diesen Tipp noch die Berührung eines zweiten Fingers auf dem Band.
- DISARM, AUSGANG AUS, ABBRECHEN eines Stick-Laufs, TEST BEENDEN und LAUF BEENDEN liegen oberhalb des Bands. Ein Druck darauf führt die Aktion aus; der Alarm bleibt. Das Band blockiert Berührungen von ARM und AUSGANG EIN, soweit sie darunterliegen.
- Die beim Start angezeigte Meldung "Touchscreen antwortet nicht. ARMED ist gesperrt." bleibt bis zum Neustart und hat kein `x`.

## Halte-Buttons

Eine Halte-Taste wirkt nach 2s Berührung. ARM, AUSGANG EIN, TEST STARTEN, ENDLAGENLAUF STARTEN, ÜBERNEHMEN und jede Taste mit der Beschriftung `2s HALTEN ZUM ...` verwenden diese Geste. Loslassen vor 2s sendet keinen Befehl. Stattdessen erscheint der Hinweis 4s lang in Bernsteinfarbe im Alarmband, etwa `Halte 2s, um auf ARMED zu schalten.`.

- Der Hinweis erscheint nur, wenn du den Finger auf der Taste loslässt. Er erscheint nicht nach dem Herausrutschen, bei verlorenem Touch, bei einer Berührung mit einem zweiten Finger oder auf einer Seite ohne Statusband.
- Nach 2s oder länger ist die Aktion bereits ausgelöst; beim Loslassen erscheint kein Hinweis.
- Ein kurzer Tipp ist keine Störung. Deshalb ist der Hinweis eine Mitteilung und kein Alarm. Ein Tipp auf das Band löscht ihn vorzeitig. Ein bestehender Alarm verdeckt den Hinweis, und ein neu eintreffender Alarm ersetzt ihn.
- STOP löscht den Hinweis sofort, ebenso ein neuer FEHLER-Code oder ein fehlerbedingter Wechsel von ARMED zu SAFE.
- Die Hinweise stehen jeweils nach `Halte 2s, um`: `auf ARMED zu schalten.`, `den Ausgang einzuschalten.`, `zu übernehmen.`, `den Test zu starten.`, `den Endlagenlauf zu starten.`, `freizugeben.`, `zu schreiben.`, `zurückzuspielen.`, `fortzufahren.`, `zu verlassen.`, `zu laden.`, `den Port freizugeben.`, `zu reparieren.`, `zu starten.`, `zu installieren.`, `zu laden und zu installieren.`, `den IOmcu zu aktualisieren.`, `alle Pin-Zuordnungen zu löschen.` und `den STOP zu lösen.`

## Berührungen nahe am Band

- Eine auf dem Band begonnene Berührung bleibt dem Band zugeordnet. Ziehst du den Finger in die Seite darunter, bewegt er dort nichts.
- Ziehst du eine auf der Seite begonnene Berührung nach oben ins Band, gilt sie an der letzten Position innerhalb der Seite als losgelassen. Ein Schieberegler oder das SERVO-Drehrad behält den dortigen Wert. Eine Haltegeste auf ARM oder AUSGANG EIN wird abgebrochen.
- AUSGANG AUS und TEST STARTEN während eines laufenden Servotests reagieren auf dieses Loslassen, da sie den Ausgang beziehungsweise Test anhalten. DISARM wirkt bereits beim Aufsetzen.
- Ein Druck auf das Statusband, STOP oder Alarmband beendet die Ziehbewegung eines anderen Fingers an einem Schieberegler oder am SERVO-Drehrad.
