# Sicherheit

<sub>[English](Safety.md) · **Deutsch**</sub>

Wie der Prüfstand anhält, welche externe Schaltung der Entwurf voraussetzt,
und welche Verhaltensweisen Absicht sind.

Das Projekt ist in Entwicklung, und seine Funktionen sind nicht auf Hardware
verifiziert.

## Stoppmechanismen

| Mechanismus | Deckt ab | Stand |
| --- | --- | --- |
| Der Heartbeat bleibt aus | das Panel hängt, ist resettet, hat einen Brown-out oder ist abgesteckt; ein gedrücktes STOP | an beiden Enden erzeugt und überwacht; das Monoflop, das er steuert, ist nicht bestückt |
| Stille-Watchdog des IOmcu, 200ms | der Link ist in einer der beiden Richtungen tot | gebaut und getestet |
| STOP-Kommando über den Link | ein bewusster Stopp, quittiert und gemeldet | geschrieben |
| Watchdog der Render-Schleife, 1000ms | die Render-Schleife des Panels bleibt stehen, während der Prüfstand ARMED ist, der Ausgang des Netzteils an ist oder die 5V der Programmierports auf dem Carrier an sind | gebaut; die Entscheidung ist auf dem Host getestet (`test_frame_watch`) |

Ein gedrücktes STOP stoppt den Heartbeat, schaltet das eigene
Ausgangsmodell des Panels auf SAFE und schreibt ARM = 0 auf die Control-Page. Bei
stehendem Link schreibt das Panel ARM und THROTTLE mit jedem Poll alle 50ms;
ein bewusstes Schalten auf ARMED wartet, bis der IOmcu meldet, dass er dem
Heartbeat vertraut, schreibt dann CLEAR (0x5AFE) allein, dann ARM, THROTTLE
und MOTOR_POLES in einem Frame, und ein NACK (negative Quittung) des
IOmcu auf eines von beiden lässt das Panel SAFE.

### Watchdog der Render-Schleife

Der Control-Task liest den Touch, prüft das Rechteck von STOP und erzeugt
den Heartbeat ohne die Render-Schleife. Steht die Render-Schleife, wirkt
STOP weiter und sonst nichts. DISARM, der Gas-Schieberegler, das Zifferblatt
von SERVO und OUTPUT OFF gehören der Render-Schleife, und die Ausgänge behalten ihr
letztes Kommando.

Die Render-Schleife zählt einen Zähler pro Durchlauf weiter. Der
Control-Task liest ihn in jedem Durchlauf alle 5ms und in der Wartezeit
jedes Link-Austauschs. Ist der Prüfstand ARMED, der Ausgang des
Netzteils an oder zählen auf dem rbmod-Carrier die 5V der
Programmierports für die Sperren als an
([Die 5V der Programmierports auf dem Carrier](#die-5v-der-programmierports-auf-dem-carrier))
und hat sich der Zähler 1000ms lang nicht bewegt
(`FRAME_WATCH_MS`, `shared/safety/include/frame_watch.h`), ist das ein
Stopp des Prüfstands selbst, auf dem Weg, den STOP nimmt:

- der Heartbeat fällt im selben Durchlauf weg, und der IOmcu geht
  innerhalb von 150ms in den sicheren Zustand;
- der Ausgang des Netzteils schaltet im selben Durchlauf ab;
- der Hinweis `Bildschirm 1s ohne Aktualisierung. Auf SAFE geschaltet,
  Ausgang aus.` erscheint, sobald die Render-Schleife wieder zeichnet. Eine
  Schleife, die nie wieder zeichnet, zeigt nichts Neues.

Der Stopp rastet ein wie STOP. Eine Schleife, die wieder läuft, schaltet
nichts auf ARMED und nichts ein; erst das nächste Schalten auf ARMED löst
die Sperre. Ein Stillstand ergibt einen Stopp, und der nächste Stillstand
beginnt mit der nächsten Bewegung des Zählers.

Die Zeit läuft ab der letzten Bewegung des Zählers, nicht ab dem Schalten
auf ARMED. Eine Schleife, die stehen blieb, nachdem sie ARM oder ON
abgeschickt hatte, stoppt den Prüfstand im ersten Durchlauf, der ihn ARMED
oder den Ausgang an findet. Ein Prüfstand in SAFE mit ausgeschaltetem
Netzteil wird nie gestoppt: die Installation eines Panel-Updates, ein Log,
das LOGS lädt, und das Speichern der Einstellungen halten die Schleife auf
einem Prüfstand in SAFE an. Die Installation beginnt nur in SAFE mit
ausgeschaltetem Netzteil und hält den Heartbeat beim Schreiben absichtlich
an. Beim Start ist der Prüfstand SAFE und das Netzteil aus.

Die Zeit ist 1000ms wegen der längsten Durchläufe, die die Render-Schleife
auf einem Bildschirm macht, der einen Ausgang treibt:

| Aufruf im Durchlauf | Höchstens |
| --- | --- |
| Kopie der PPA (Pixel-Processing Accelerator) für `Bilddrehung` 180° | 50ms Budget (`GFX_TURN_BUDGET_US`), nach einer Zeitüberschreitung dann eine Kopie des Bildes mit 1 228 800 Bytes, nicht gemessen |
| Flip: das Warten auf das Frame-done-Ereignis | 200ms (`FLIP_WAIT_MS`) |
| Zeichnen, ein ganzes Neuzeichnen eingeschlossen (ein Tastenfeld öffnet, ein Bildschirmwechsel) | auf dem ESP32-P4 nicht gemessen |
| Ein Kommando an den Control-Task in eine volle Queue | 5ms pro Kommando |
| Die Konsolenzeile alle 300 Frames | etwa 11ms bei 115200 Baud, nicht gemessen |
| Speichern der Einstellungen | nie, während der Prüfstand ARMED ist, ein Schalten auf ARMED wartet, das Netzteil an ist oder ein ON wartet (`flash_gate_open()`) |
| Der Blick des Servo-Programmierers auf die Karte (SERVO > PROGRAMMIEREN) | ein Statuskommando an die Karte oder ein Mount, dann das Lesen seines Datensatzes (`KST.TXT`); jedes Kartenkommando begrenzt der Treiber auf 1000ms, nicht gemessen |
| LOGS: die Liste, das Öffnen eines Logs und sein Plot | als Fortschritt gezählt, pro Verzeichniseintrag und pro Lesen (`panel_card.c`); ein Lesen begrenzt der Kartentreiber, nicht gemessen |

Der begrenzte Teil, 50ms + 200ms + die Kopie + das Zeichnen, ist etwa
300ms. 1000ms ist etwa das Dreifache davon und das Doppelte der 500ms, nach
denen ein schweigender Touch-Controller den Prüfstand auf SAFE schaltet.

Bekannte Einschränkungen:

- Sendet das Display keine Frame-done-Ereignisse mehr, wartet jeder Flip
  200ms. Die Schleife läuft dann etwa 4-mal pro Sekunde, und der Watchdog
  löst nicht aus: DISARM und die anderen Bedienelemente wirken weiter, auf
  einem Bild, das sich nicht ändert.
- Der Servo-Programmierer sieht auf der Render-Schleife nach der Karte, und
  während einer Session ist der Ausgang des Netzteils an. Braucht ein
  Statuskommando oder ein Mount 1000ms oder länger, zum Beispiel hinter dem
  Schreiben eines anderen Tasks auf dieselbe Karte, stoppt das den
  Prüfstand und beendet die Session.
- Wer SERVO verlässt, während ein Programmierport das Netzteil hält, lässt
  den Ausgang an. Ein einzelnes Lesen von der Karte, das auf LOGS 1000ms oder
  länger braucht, stoppt dann den Prüfstand und beendet die Session des
  Ports.

## Fehler

Die Regel: ein Fehler schaltet jeden Ausgang aus und den Prüfstand auf SAFE.
Ein Fehler ist eine Störung, die beendet, was der Prüfstand treibt. Das
eigene AUS, DISARM und die Änderungen der bedienenden Person sind keine
Fehler.

Die Ausgänge sind die Servo-PWM-Slots und DShot-Throttle-Kanäle der Bank,
die vier Programmierports (Servo-PWM, DShot, die KST-Leitung, die
Hitec-Leitung), der Ausgang des PD (Power Delivery) mini und auf dem
rbmod-Carrier die eigenen 5V der Programmierports an GPIO 21
([Die 5V der Programmierports auf dem Carrier](#die-5v-der-programmierports-auf-dem-carrier)). Der Phase Tap
auf der Page TONE ist ein Eingang und treibt nichts. Das Panel hat keinen
Draht zu einem Ausgang: es handelt über den Link und den Heartbeat, und der
IOmcu handelt selbst, wenn einer der beiden ausfällt.

| Störung | Servo-PWM und DShot, an der Bank und an den Ports | KST- und Hitec-Sitzungen | Ausgang des PD mini | Wer handelt |
| --- | --- | --- | --- | --- |
| Link 200ms still, der IOmcu läuft | kein Puls ab demselben Durchlauf | abgebrochen, Leitung freigegeben | im selben Durchlauf aus angefordert, auch ein für ein Schreiben zurückgehaltenes OFF | der IOmcu (`safety_gate_step()`); das Panel rastet in ARMED einen Stopp ein |
| Der IOmcu im Reset oder in BOOTSEL gehalten | Pins sind Eingänge | Pins sind Eingänge | bleibt, wie er war | niemand: [Grenzen der Hardware](#grenzen-der-hardware) |
| Der IOmcu neu gestartet | SAFE mit Latch bis CLEAR und ARM | keine offen | gelesen und ausgeschaltet vor dem Rest des Starts | der IOmcu (`supply_boot_off()`) |
| Heartbeat am IOmcu verloren | kein Puls binnen 150ms | abgebrochen | aus angefordert, auch ein für ein Schreiben zurückgehaltenes OFF | der IOmcu |
| Das Panel neu gestartet, hängend oder abgesteckt | kein Puls binnen 150ms | abgebrochen | aus angefordert | der IOmcu, über den Heartbeat |
| Die Render-Schleife des Panels 1000ms still | kein Puls binnen weiterer 150ms | abgebrochen | aus | der Control-Task des Panels (`frame_watch.h`), dann der IOmcu |
| STOP | kein Puls binnen 150ms | abgebrochen | aus | das Panel, dann der IOmcu |
| Messgerät oder Netzteil eines Laufs fällt aus (`0.2s kein INA3221-Fenster` und die anderen Fehler in [Servo, Was einen Lauf beendet](Servo-de.md#was-einen-lauf-beendet)) | SAFE | keine offen: ein Lauf ist ARMED | aus | der SERVO-Bildschirm |
| Eine Sitzung, die der Link oder der Heartbeat abbricht | wie in den Zeilen oben | Transaktion abgebrochen | aus, nach einem Frame auf der Leitung, höchstens 12ms | der IOmcu |
| Ein Schreiben auf die Karte scheitert während eines Laufs, oder das letzte Festschreiben oder Schließen des Lauf-Logs oder der CSV-Datei (comma-separated values) des automatischen Tests danach, einmal je Lauf | SAFE | die Versorgung des Ports aus, nach einem laufenden Schreiben ([unten](#die-versorgung-eines-programmierports)) | aus; die eines Ports nach seinem Schreiben, höchstens 10000ms | das Panel (`card_faults_follow()`), der IOmcu für das Schreiben |
| Die Abschaltschwelle des Panels für das Netzteil (ÜBERSTROM oder ÜBERSPANNUNG auf SUPPLY) | SAFE | die Versorgung des Ports aus, nach einem laufenden Schreiben | aus; die eines Ports nach seinem Schreiben, höchstens 10000ms | das Panel, der IOmcu für das Schreiben |
| Ein INA3221-Kanal über seiner Grenze | SAFE | die Versorgung des Ports aus, nach einem laufenden Schreiben | aus; die eines Ports nach seinem Schreiben, höchstens 10000ms | das Panel, beim Lesen von `OVER` alle 200ms; der IOmcu für das Schreiben |
| Eine Stromgrenze eines Laufs: BLOCKIERT AB, Konstantstrom, 3,0A, 1,0A für 400ms | SAFE | keine offen: eine Suche ist ARMED | aus | der SERVO-Bildschirm |
| Der eigene Schutz des PD mini, sein absinkender Eingang, keine Antwort, kein Messwert, ein abgewiesenes oder losgelassenes ON, ein Ausgang oder Sollwert, der nicht übernommen wird, ein ON über verlorene Touch-Ereignisse hinweg | SAFE | die Versorgung des Ports aus, nach einem laufenden Schreiben | aus, wo er sich schalten lässt: der Schutz des Moduls und ein absinkender Eingang sofort, auch während eines Schreibens; der Rest nach dem Schreiben eines Ports, höchstens 10000ms | der Treiber des IOmcu für den Ausgang, das Panel für SAFE |
| Ein KST- oder Hitec-Vorgang endet in einer Störung des Servos oder der Leitung (`kst_link_outcome_fault()`, `hitec_prog_ses_fault()`) | schon SAFE: eine Sitzung läuft auf einem Prüfstand in SAFE | die Versorgung des Ports aus; der Vorgang ist zu Ende | aus | das Panel (`prog_faults_follow()`) |

SAFE heißt: das Panel fordert DISARM an. Solange die Anforderung steht, hält
es den Heartbeat zurück, sodass der IOmcu binnen 150ms sicher abschaltet,
auch wenn der Control-Task des Panels in einem Austausch wartet. Ein
Prüfstand, der schon SAFE ist, wird nicht noch einmal auf SAFE geschaltet:
in SAFE würde der zurückgehaltene Heartbeat die Sitzung eines
Programmierports beenden, ohne dass dieser einen Ausgang treibt. Die Regel
ist `error_stop()` in `shared/safety/error_stop.c`, getestet in
`test_error_stop`. Der Control-Task wendet sie dort, wo er einen Fehler
findet, über `shared/safety/error_sites.c` an, getestet in
`test_error_sites`.

Die Zeilen oben gelten für einen IOmcu ab Link 4.31. Mit einem älteren
IOmcu behält ein Programmierport, der das Netzteil hält, es bei einem
Fehler, den das Panel findet: das Panel fordert DISARM an und sendet kein
OFF, weil dieser IOmcu ein Schreiben in den Speicher des Servos mittendrin
abbrechen würde (`error_sites_keeps()`). Das gilt für jeden Fehler oben,
den das Panel findet, darunter ein Netzteil, das nicht antwortet oder
keinen Messwert liefert, ein ON über verlorene Touch-Ereignisse hinweg und
ein ON, das die Page SUPPLY abweist, während das ON eines Ports auf ihr
steht. Weiterhin aus, gleich wer das Netzteil hält:

- ein verlorener Heartbeat oder Link: der IOmcu schaltet ab;
- die eigenen Abschaltschwellen des Panels für das Netzteil, ÜBERSTROM und
  ÜBERSPANNUNG: das Panel schaltet den Ausgang zuerst aus
  (`error_sites_cut()`);
- der eigene Schutz des PD mini und ein absinkender Eingang: das Modul oder
  der IOmcu hat den Ausgang schon ausgeschaltet, und das Panel folgt
  (`error_sites_page_keeps()`). Einem ON, das die Page losgelassen hat,
  folgt es ebenso; die Page lässt ein ON nur los, wenn kein Port sie hält.

Ein Port hält nur den Ausgang des PD mini. Auf dem Modell des Panels liegt
kein Port, und ein Fehler schaltet es aus. Ein Netzteil, das nicht
antwortet oder keinen Messwert liefert und das kein Port hält, wird
ausgeschaltet, damit es nicht von selbst wieder angeht, wenn es wieder
antwortet. Behält ein Port es, wird die Störung einmal behandelt und
genannt, solange sie besteht, wieder, wenn sie in die andere Störung
übergeht, und noch einmal, wenn der Port loslässt, was den Ausgang
ausschaltet. Ein ON, das die Page wegen einer Verdrahtung abweist, die die
Einstellungen nennen und die die Page abgewiesen hat, gehört wieder dem
Port, solange er die Page hält (`supply_link_keep()`); lässt der Port los,
wird das ON abgewiesen und ausgeschaltet.

### Die Versorgung eines Programmierports

Eine KST- oder Hitec-Sitzung hält den Ausgang des PD mini, solange sie einen
Port hat. Bei einem Fehler schreibt das Panel das OFF der Versorgung an den
IOmcu, gleich wer sie hält und ob es den Ausgang an liest oder nicht
(`supply_link_cut()`). Der IOmcu entscheidet aus seinem eigenen Zustand, ob
ein Schreiben in den Speicher des Servos läuft (`supply_page.h`):

- Kein Schreiben läuft: das OFF erreicht den PD mini im nächsten Durchlauf
  des IOmcu.
- Ein Schreiben läuft: OUTPUT liest sofort 0; der PD mini und die Sicht des
  Ports auf ihn bleiben an, bis das Schreiben endet, und das OFF erreicht
  den PD mini im Durchlauf danach, spätestens 10000ms nach dem OFF
  (`SUPPLY_PAGE_WRITE_WAIT_MS`). Der Port bricht sich nicht mitten im
  Schreiben selbst ab.
- Ein KST-Schreiben ist ein WRITE, ein RESTORE oder ein RELEASE der Sitzung,
  von der Pause vor seinem ersten Schreib-Frame bis zum Ende der
  Rücklesungen seines letzten, und 150ms danach (`KST_PORT_WRITE_TAIL_MS`,
  `kst_port_writing()`). Sein Planen und die vollen Lesevorgänge vor dem
  ersten und nach dem letzten Schreib-Frame senden kein Schreiben und
  werden sofort abgebrochen. Ein ENTER, ein READ_ALL, ein VERIFY, ein
  Versorgungszyklus und ein LEAVE schreiben nichts.
- Ein Hitec-Schreiben läuft, solange ein SEND läuft, dessen Frame
  schreibt, und 150ms danach (`HITEC_PORT_WRITE_TAIL_MS`), oder solange das
  letzte Kommando der HITEC-Page seinen Schritt mit MORE gestartet hat
  (`CMD` Bit 4, `hitec_port_writing()`). Der IOmcu erkennt einen
  schreibenden Frame an seinen Bytes: ein classic-WRITE_EEPROM oder
  -WRITE_RAM, eine Schreibanfrage der D-Serie, Speichern und Neustart
  eingeschlossen, mit oder ohne MORE. Ein Lesen und ein LISTEN schreiben
  nichts, ein OFF währenddessen wird nicht zurückgehalten. Wie lange ein
  Servo nach einem schreibenden Frame Versorgung braucht, ist nicht
  gemessen; die 150ms liegen über der längsten Pause, die ein
  Programmiergerät des Herstellers danach lässt, 128ms
  ([Ein OFF während eines Schreibens](LinkHitec-de.md#ein-off-während-eines-schreibens)).
  Der Programmierer des Panels markiert jeden Schritt eines Schreibens außer dem letzten
  (`hitec_ses_step_t.more`): einen Planschritt vor dem letzten und einen
  classic-Versuch, der wiederholt werden kann. Die Prüfstandskonsole
  markiert einen Schritt mit `more`
  ([Die Prüfstandskonsole](BenchConsole-de.md)).
- Ein verlorener Heartbeat oder ein stiller Link wird nicht zurückgehalten:
  sofort aus, nach einem Frame auf der Leitung, höchstens 12ms.

Vom OFF des Panels, bis der Port die Versorgung loslässt, weist der IOmcu
das eigene ON des Ports ab: ein KST-Versorgungszyklus in seiner Aus-Zeit
endet in PWM mit Ergebnis 32 (SUPPLY) und ausgeschalteter Versorgung;
ein Hitec-POWER schaltet sie aus, sein ON wird abgewiesen (RESULT 3), und
der Port bleibt ohne Versorgung gehalten bis CLOSE. Ein KST-Port, der den
Ausgang nach einem Schreiben aus findet, hält ihn 1000ms aus und kehrt nach
PWM zurück.

Die längsten Schreibvorgänge, die der Code zulässt, enden innerhalb von
10000ms, berechnet und nicht gemessen: ein KST-Schreiben mit 33 Schritten
dauert 1,93s auf der Leitung nach seinem Planen; ein classic-Hitec-Schreiben
mit 56 Schritten und dem Zurücklesen von 36 Zellen 2,9s und ein Schreiben
der D-Serie mit 21 Registern, Speichern, 1000ms Warten nach dem Neustart
und Zurücklesen 3,5s, bei 32ms je Schritt auf der Host-Schleife des Panels.

Eine Störung der Sitzung ist ein Vorgang, der an dem endet, was das Servo
oder die Leitung getan hat. KST: kein Servo, ein Register ohne 3 gleiche
Lesungen, ein Servo, das sich unter dem Plan geändert hat, ein Schreiben,
das scheiterte, zurückgerollt wurde, zerrissen ist oder ein anderes
Register änderte, eine Prüfung mit Abweichung, ein Fehler des Treibers,
eine Versorgung, die nicht schaltete. Hitec: kein Servo, eine falsche
Antwort beim Einstieg, ein gescheitertes Lesen, ein classic-Schreiben ohne
Antwort, ein Zurücklesen mit Abweichung, ein Schritt, den der Decoder
abwies. Keine Störung: eine Ablehnung, ein Plan, der nichts sendet, ABORT
und ein Schritt, den ein Stopp oder eine verlorene Verbindung abbricht; die
beenden die Sitzung selbst.

#### Die 5V der Programmierports auf dem Carrier

Auf dem [rbmod-Carrier](Carrier-de.md#versorgung) laufen die
Programmierports am 5V-Abwärtswandler M7, geschaltet über GPIO 21 (Link
4.32, die Page PROG_SUPPLY). Der PD mini speist nur die Servoschienen. Die
Programmer für KST und Hitec schalten die 5V für eine Sitzung ein und aus;
das Panel bietet keine Taste dafür. Die Programmer lesen auf dem Carrier
nichts vom PD mini: sein Sollwert lehnt weder ein VERBINDEN unter 4.8V
ab, noch setzt er die Spannung eines Power-Cycle, und die Grenzen der
Ansicht SUPPLY begrenzen nur die Servoschienen, kein VERBINDEN. Sind die
5V als ein gelesen, schalten VERBINDEN und VERLASSEN nach einem Schreiben
sie aus und wieder ein, wie den Ausgang des PD mini; sind sie aus,
schaltet VERBINDEN das Servo ein. Die Page folgt den Regeln
der Page SUPPLY für einen Port:

- Ein Fehler schreibt das OFF der Page, wer immer sie hält und was immer
  das Panel von ihr liest (`error_sites.c`, `prog_supply_link_cut()`). Ein
  Schreiben in den Speicher des Servos hält sie an, bis es endet,
  höchstens 10000ms; vom OFF an, bis der Port loslässt, wird das eigene ON
  des Ports abgelehnt.
- Ein verlorener Heartbeat oder ein stummer Link schaltet sie sofort aus,
  nach einem Frame auf der Leitung, höchstens 12ms.
- Bei jedem Start des IOmcu ist sie aus, ab der ersten Anweisung von
  `main()` low getrieben; der Pull-down an EN von M7 hält sie aus, solange
  der IOmcu GPIO 21 nicht treibt.
- Ein ON vom Panel wird ohne lebenden Heartbeat mit NOT_ARMED und mit
  BAD_VALUE abgelehnt, solange ein Port die Versorgung hält.
- Ein Port schaltet sie nur ein, nachdem das Panel seit dem letzten
  Verlust des Heartbeats das Register 4 der Page (PANEL) geschrieben hat.
  Ein Panel vor Link 4.32, das weder diese Versorgung noch ihre Sperren
  kennt, schreibt es nie, also bekommen seine Programmer die 5V nie.
- Eine Firmware-Installation, ein Update des IOmcu, ein Schreiben in den
  Flash des Panels und das WLAN des Panels warten, solange die 5V
  an sind, seit dem Aufbau des Links oder seit einem Lesen ohne Antwort
  nicht gelesen wurden oder ein Port eines Programmers sie hält, so wie sie
  auf den Ausgang des PD mini warten (`prog_supply_link_live()`).

SUPPLY zeigt ihren Zustand unter OUTPUT: `PROG-PORTS 5V, GPIO 21: EIN`,
`AUS` oder `UNBEKANNT`, wenn das letzte Lesen ohne Antwort blieb. Der
Kern ist `shared/outputs/prog_supply.c` unter `test_prog_supply`; das Ende
des Panels ist `shared/bench/prog_supply_link.c` unter
`test_prog_supply_link`.

Aktuelle Einschränkungen:

- Ein Fehler während eines Schreibens hält das Servo versorgt, bis das
  Schreiben endet, höchstens 10000ms. Ein Schreiben, das dann noch läuft,
  mit Wiederholungen, über einen langsamen Link oder Schritt für Schritt an
  der Prüfstandskonsole getippt, wird mittendrin getrennt.
- Jedes OFF, das das Panel schreibt, wird für ein Schreiben genauso
  zurückgehalten: das OFF des Bedieners auf SUPPLY und die eigenen
  Abschaltschwellen des Panels (ÜBERSTROM, ÜBERSPANNUNG) ebenso wie das
  eines Fehlers. Der eigene Schutz des PD mini und die Abschaltung des IOmcu
  für einen absinkenden Eingang wirken sofort, auch während eines
  Schreibens.
- Ein Hitec-Schreiben, das nach einem Schritt mit MORE in einer Störung
  endet, hält das OFF zurück bis zum nächsten Kommando der HITEC-Page oder
  10000ms.
- Eine Störung der Sitzung beendet die Sitzung mit der Versorgung. Nach
  einem gescheiterten KST-Schreiben endet das eigene Backup der Sitzung
  mit ihr; der Vermerk vor dem Schreiben bleibt, und das Lesen nach
  VERBINDEN vergleicht das Servo damit
  ([Ein unterbrochenes Schreiben](ServoProgrammer-de.md#ein-unterbrochenes-schreiben)).
- Die Fehlerbits des IOmcu auf STATUS für Überstrom, Übertemperatur und
  Stall (`LINK_FAULT_OVERCURRENT`, `LINK_FAULT_OVERTEMP`,
  `LINK_FAULT_STALL`) sind definiert und werden von nichts gesetzt.
- Die Meldungen nennen den Fehler und sagen nicht, dass der Prüfstand auf
  SAFE geschaltet hat; die Leiste zeigt SAFE.
- Mit einem IOmcu älter als 4.31 bleibt ein Netzteil, das ein Port über
  eine fehlende Antwort oder einen fehlenden Messwert hinweg behält, an,
  bis der Port loslässt. Das Panel hat währenddessen keinen gültigen
  Messwert, daher greifen ÜBERSTROM und ÜBERSPANNUNG nicht; der eigene
  Schutz des PD mini und die Abschaltung des IOmcu für einen absinkenden
  Eingang greifen.

### Grenzen der Hardware

Firmware kann ein Board, das nicht läuft, nicht abdecken:

- Der IOmcu im Reset oder in BOOTSEL gehalten. Seine Pins sind Eingänge,
  die Bank und die Ports senden also keinen Puls, aber ein offener Eingang
  eines Servos oder ESC (Electronic Speed Controller) kann einen Pegel
  lesen. Der PD mini hat seinen UART nur am IOmcu und behält seinen Ausgang,
  bis der IOmcu wieder läuft. Das Monoflop unten deckt das nicht ab: das
  Panel sendet seinen Heartbeat weiter. Ein Pull-down an jeder
  Signalleitung deckt die Pulse ab; ein zweites Monoflop, das die
  Hauptschleife des IOmcu selbst nachtriggert und dieselben Schalter und
  einen Schalter hinter dem PD mini steuert, deckte den Rest ab. Keines von
  beiden ist spezifiziert.
- Panel und IOmcu beide weg. Nichts schaltet den PD mini oder den
  Leistungspfad. Abgedeckt durch das Monoflop.
- Der PD mini antwortet auf seinem UART nicht. Der IOmcu sendet ein blindes
  OFF nur an ein Modul, das geantwortet hat und dessen OFF-Argument bekannt
  ist. Das Monoflop deckt das nicht ab, solange beide Boards laufen. Ein
  Lastschalter hinter dem Ausgang des PD mini, offen, solange ihn kein Pin
  des IOmcu geschlossen hält, deckte es ab; er ist nicht spezifiziert.

Am Bring-up-Prüfstand blieb der Ausgang des PD mini während eines
Ausfalls des IOmcu von 15s in BOOTSEL an, und ein Power-on-Reset trennt das
USB des IOmcu für 14,5 bis 15s. Wie lange ein echter Reset den PD mini vor
dem OFF des Starts anlässt, ist nicht gemessen.

## Vorausgesetzte externe Schaltung: das Monoflop

Die Sicherheitsleitung ist GPIO28 (General-Purpose Input/Output) des Panels,
ein freier Pin der 7-Zoll-ESP32-P4-Displayplatine: IO28 des 12-poligen
Headers P3 (`PANEL_HEARTBEAT_PIN` in `board_pins.h`). Sie trägt Flanken,
keinen Pegel. Das Output Enable des IOmcu und der Leistungspfad für Servos
und ESC (Electronic Speed Controller, Motorregler) müssen hinter einem
retriggerbaren Monoflop liegen, das nur angezogen bleibt, solange Flanken
eintreffen. Absturz, hängender Task, Reset, Brown-out und abgestecktes Kabel
führen dann zum selben Ergebnis: keine Flanken, kein Ausgang, unabhängig von
der Firmware an beiden Enden.

Fenster des Monoflops: etwa 150ms. Der Heartbeat kommt aus dem Control-Task
des Panels, der alle 5ms auf dem Kern läuft, der nicht zeichnet, und dessen
Periode damit nicht davon abhängt, was ein Frame kostet. Das Fenster bleibt
bei 150ms und liegt innerhalb des 200-ms-Link-Failsafes des IOmcu,
statt auf die neue Periode zu schrumpfen: die Reserve ist das, was einen
verspäteten Task überlebt.

Das Monoflop ist auf keiner Platine. Auf dem Aufbau-Prüfstand erreichen die
Flanken über eine direkte Leitung vom Heartbeat-Pin des Panels den GPIO 3 des IOmcu, weshalb
dieser Prüfstand auf ARMED schalten kann.

Das Monoflop wird von den Flanken des Panels nachgetriggert und nimmt die
Ausgänge weg, wenn diese ausbleiben. Was es gegenüber der direkten Leitung
hinzufügt: Es tut das, ohne dass die Firmware des IOmcu etwas tun muss.
Drei Fälle, und der mittlere ist die Lücke:

| | Direkte Leitung und Firmware | Mit Monoflop |
|---|---|---|
| Das Panel hört auf zu schlagen, der IOmcu ist gesund | schaltet auf SAFE nach HEARTBEAT_MAX_GAP_MS | dasselbe, nach seiner eigenen Zeit |
| Das Panel hört auf zu schlagen und der IOmcu kann nicht handeln (hängt, oder bedient seinen Monitor nicht) | nichts schaltet auf SAFE, die Bank treibt weiter | die Ausgänge fallen trotzdem weg |
| Das Panel ist gesund und der IOmcu verhält sich falsch | es bleiben STOP am Panel und der Link-Watchdog | **keine Hilfe**: das Panel schlägt weiter, das Monoflop bleibt bestromt |

Die dritte Zeile ist kein Fall, für den das Monoflop da ist, und keine
Hardware in diesem Entwurf deckt sie ab. Solange das Bauteil fehlt, ist auch
die zweite Zeile unabgedeckt, und die Firmware an beiden Enden ist die gesamte
Verriegelung.

## Heartbeat-Überwachung

Der IOmcu prüft den Heartbeat zusätzlich in der Firmware, weil ein
Monoflop Heartbeat und Rauschen nicht unterscheiden kann. Die Konstanten aus
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | Wert | |
| --- | ---: | --- |
| Flankenabstand des Panels | 20ms | `HEARTBEAT_PERIOD_MS`, eine Flanke alle 20ms, also alle 4 Perioden des Control-Tasks zu 5ms, nicht je Frame |
| Akzeptierter Abstand | 4–150ms | kürzer ist Rauschen; länger heisst, die Schleife, die STOP besitzt, steht |
| Gute Abstände, bevor der Leitung vertraut wird | 4 | 80ms bei der Rate des Panels |

Die Prüfung ist asymmetrisch: vier gute Abstände, bevor der Leitung vertraut
wird; ein schlechter Abstand oder ein stilles Fenster, und das Vertrauen ist
weg. Der IOmcu verweigert das Schalten auf ARMED, solange der Leitung nicht
vertraut wird, und schaltet seine Ausgänge auf SAFE und setzt seinen Arm-Latch,
sobald sie ausbleibt.

Der Heartbeat wird vom Control-Task erzeugt, der STOP besitzt und alle 5ms
auf Kern 1 läuft, dem Kern, der nicht zeichnet. Er kommt nicht von einem
Timer oder einer Peripherie und nicht aus der Schleife, die zeichnet: ein
Panel, das aufgehört hat zu zeichnen, lässt sich noch stoppen. Den Touch-Controller
liest eine eigene Task, Priorität 5 auf Kern 0, dem Kern, der zeichnet. Der
Control-Task nimmt deren Ereignisse aus einer Queue und beurteilt den
Controller nach der Zeit seit dessen letzter Antwort: 500ms ohne Antwort
schalten den Prüfstand auf SAFE und stoppen den Heartbeat. Der Eingang des
IOmcu hat einen Pull-down, sodass ein unversorgtes oder abgestecktes
Panel als Leitung ohne Flanken gelesen wird.

## Arm-Latch

Der IOmcu hält einen Arm-Latch. Solange er gesetzt ist, wird ein
Schreiben von ARM mit NOT_ARMED abgewiesen, und die Bank bleibt SAFE,
was immer im Register ARM steht.

| Setzt den Latch | Bedingung |
| --- | --- |
| Ein Start des IOmcu | Einschalten, ein Reset, der RUN-Pin, ein Neuflashen |
| Stille auf dem Link | 200ms ohne Anfrage, zusammen mit dem Link-Failsafe |
| Dem Heartbeat wird nicht vertraut | ein Abstand unter 4ms oder über 150ms, oder 150ms ohne Flanke |
| Ein abgewiesenes ARM | ein ARM, das mit NOT_ARMED beantwortet wurde; ein CLEAR mit anschließend abgewiesenem ARM lässt den Latch also gesetzt |

Eines löscht ihn: das Schreiben von 0x5AFE in CLEAR auf der Control-Page.
Das Panel schreibt CLEAR an einer Stelle, beim Schalten auf ARMED, das eine Person
durch 2s Halten von ARM vollendet. Das ARM, das das Panel bei jedem
Poll alle 50ms einer Bank in ARMED schreibt, trägt kein CLEAR und bringt
einen IOmcu mit gesetztem Latch daher nie zurück.

Der Latch ist vom Link-Failsafe getrennt. Das ON der Versorgung und die
STATUS-Page lesen das Failsafe und den Heartbeat und nicht den Latch: nach
einem Start meldet STATUS Idle ohne Fault-Bit, und ein ON der Versorgung wird
vor dem ersten Schalten auf ARMED angenommen. Der Latch steht nicht auf STATUS. Ein
IOmcu mit gesetztem Latch zeigt sich als abgewiesenes ARM, das das
Panel als `IOmcu hat auf SAFE geschaltet. ARM erneut halten.` meldet.

Das Panel hat für dieselben Ereignisse eine eigene Regel. Antwortet der
IOmcu nicht, während der Prüfstand ARMED ist oder ein Schalten auf ARMED
auf die Leitung wartet, rastet am Panel bei diesem Poll ein Stopp ein: die
eigene Bank wird auf SAFE geschaltet, das Gas geht auf null, eine gehaltene
Servo-Position wird losgelassen, und der Heartbeat stoppt. Eine ausbleibende
Antwort wird nach dem Timeout von 1000ms bemerkt, oder sofort, wenn der
CAN-Controller (Controller Area Network) des Panels bus-off ist. Jeder
Austausch zählt, nicht nur das Lesen der Bench-Page im Poll: das Schreiben
von ARM und THROTTLE, das darauf folgt, ein Page-Dienst, ein Servo-Refresh,
das Lesen von STATUS. Endet einer ohne Antwort, gilt der Link beim nächsten
Durchlauf des Control-Tasks als ausgefallen, und bis dahin sendet das Panel
nichts mehr darauf. Solange der Prüfstand ARMED ist oder ein Schalten auf ARMED
wartet, rastet der Stopp ein, sobald dieser Austausch endet, ohne auf den
Durchlauf zu warten. Eine Bank,
die ohne angeschlossenen IOmcu ARMED ist, der simulierte Prüfstand,
wird auf dieselbe Weise bei dem Poll gestoppt, bei dem ein IOmcu zum
ersten Mal antwortet, bevor ein ARM an ihn geschrieben wird.

### Neustart des IOmcu während eines Laufs

Der IOmcu stellt seine Ausgangsbindung aus dem Flash wieder her und
startet mit gesetztem Latch und ohne Kommando. Die Bank des Panels ist noch
bis zu 1000ms ARMED. Zwei Fälle:

- Das Panel bemerkt den Ausfall. Bei ihm rastet ein Stopp ein, und es
  schreibt von da an ARM = 0.
- Das Panel bemerkt ihn nicht, weil die Anfrage wiederholt wurde, bis der neu
  gestartete IOmcu antwortete. Sein nächstes Schreiben von ARM wird
  mit NOT_ARMED abgewiesen, und bei dieser Antwort rastet am Panel ein Stopp
  ein.

In beiden bleiben die Ausgänge aus, bis eine Person ARM hält. Auf dem Host
getestet in `test_safety_gate` für Ausfälle von 5, 30, 50, 60, 300, 1000 und
5000ms bei je 55 Reset-Phasen und drei Modellen der Leitung. Wie lange der IOmcu vom Reset bis zu seinem ersten
beantworteten Frame braucht, ist nicht gemessen.

Der Ausgang des PD mini wird zuerst ausgeschaltet. Gleich nachdem die
Verdrahtung wiederhergestellt ist, und vor dem Sensorbus, dem Löschen im
Speicher, Core 1 und dem CAN-Controller, liest der IOmcu das Modul und
schreibt OFF an einen Ausgang, der an gelesen wird (`supply_boot_off()`).
WHO_AM_I geht erneut, sobald eines scheitert (`pdmini_hurry()`), 415ms je
Versuch, sodass unter AUTO alle sieben Raten in 2,9s versucht sind. Das
Warten endet, wenn das Modul aus gelesen wird, oder nach 3500ms
(`SUPPLY_PAGE_BOOT_OFF_MS`); ein Modul, das nicht antwortet, verzögert den
Link bei jedem Start um so viel. Auf dem Host getestet in
`test_supply_boot`: ein bei fester Rate angelassenes Modul ist binnen 100ms
nach Beginn des Wartens aus, und bei jeder AUTO-Rate binnen 3500ms.

### Das erste Schalten auf ARMED nach einem Stopp

Der Heartbeat wird zurückgehalten, solange ein Stopp eingerastet ist. Ein
Schalten auf ARMED löst den Stopp am Panel, der Heartbeat läuft wieder, und der Monitor
des IOmcu braucht fünf Flanken, bevor er der Leitung vertraut: 100 bis
125ms bei den 5ms des Control-Tasks. Das Panel wartet 100ms, liest dann
einmal je Durchlauf das Fault-Register von STATUS, bis das Heartbeat-Bit
gelöscht ist, und schreibt erst dann CLEAR und den Frame, der auf ARMED schaltet. Ist
das Bit 300ms nach dem vollendeten Halten nicht gelöscht, gibt das Panel
das Schalten auf ARMED mit `Der IOmcu hat den Wechsel zu ARMED abgelehnt.` auf und schreibt nichts, der
Latch des IOmcu bleibt also gesetzt. Eine Antwort, die später als
diese 300ms zurückkommt, wird nicht verwendet, was immer sie sagt.

Dieselben 300ms gelten durch die Schreibvorgänge, die folgen. Das Panel
fragt die Frist vor dem CLEAR und vor dem Frame, der auf ARMED schaltet, erneut ab,
weil das Freigeben des Servos davor und das CLEAR selbst spät beantwortet
werden können. Ist die Frist vor dem CLEAR abgelaufen, wird das Schalten auf ARMED wie
oben aufgegeben, und nichts wird geschrieben. Ist sie nach einem quittierten
CLEAR abgelaufen, wird kein Frame geschrieben, und am Panel rastet ein Stopp
ein: das CLEAR hat den Latch des IOmcu gelöst, und kein Register
setzt ihn wieder, also hält der Stopp den Heartbeat zurück, und der
IOmcu setzt seinen Latch, sobald er der Leitung nicht vertraut,
innerhalb von 150ms. Die Grenze der Frist ist der Frame, der auf ARMED schaltet,
selbst. Er wird zur Frist oder davor geschrieben, und seine Quittung kann
bis zum Timeout von 1000ms (`LINK_HOST_TIMEOUT_MS`) später eintreffen; der
IOmcu hat dann die angefragte Rolle auf ARMED geschaltet, in Ruhe: Gas 0
für MOTOR & ESC und dessen Stick-Läufe, Surfaces zentriert für SERVO. Das Panel macht das nicht rückgängig. Bleibt ein Austausch des Schaltens auf ARMED
unbeantwortet (das Lesen von STATUS, das Freigeben des Servos, CLEAR oder
der Frame, der auf ARMED schaltet), hat er den Timeout von 1000ms abgewartet
oder die Leitung nicht erreicht: das ist der Link, der unter einem Schalten auf ARMED
ausfällt, und am Panel rastet ein Stopp ein wie oben. Eine Ablehnung ist
eine Antwort und lässt das Panel SAFE und ohne Stopp. Ohne
angeschlossenen IOmcu schaltet das Panel seine eigene Bank nach den
100ms auf ARMED.

### Ein Pulsbereich unterwegs

Der Frame, der auf ARMED schaltet, trägt ARM, THROTTLE und die Polzahl,
aber kein CHAN_CFG. Leerlaufpuls und Vollpuls des Gases erreichen den IOmcu
auf einem eigenen Weg: eine Änderung, eine auf OUTPUTS oder PICK A PIN
geschriebene Zuordnung, eine Link-up-Flanke oder der Start des Panels
schuldet dem IOmcu das Paar. Ein Neustart oder ein Update des IOmcu kommt
als Link-up-Flanke zurück. Das Panel begleicht die Schuld bei einem Poll im
50-ms-Takt, dessen Schreiben von ARM = 0 quittiert wurde, sobald 300ms
(`ENDPOINTS_SETTLE_MS`) seit der letzten Änderung vergangen sind. Es liest
CHAN_CFG, setzt das Paar auf die Gaskanäle und schreibt die Seite, wenn sie
abweicht. Der IOmcu übernimmt ein Schreiben von CHAN_CFG in seine Ausgänge,
bevor er es quittiert, und die Quittung trägt, was er gespeichert hat.

| Begleichen | Die Schuld | Ein Schalten auf ARMED bei bestehendem Link |
| --- | --- | --- |
| die Seite hält das Paar schon | beglichen, nichts geschrieben | geht weiter |
| das Schreiben wird quittiert, und jeder Gaskanal hat das Paar gespeichert | beglichen | geht weiter |
| das Lesen oder das Schreiben bleibt unbeantwortet | offen, beim nächsten Poll erneut gesendet | `Pulsbereich noch nicht an den IOmcu gesendet. ARM erneut halten.` |
| das Lesen wird abgelehnt | beendet, `Der IOmcu hat seinen Pulsbereich nicht gemeldet.` | `Kein Pulsbereich vom IOmcu. IOmcu-Firmware aktualisieren, dann ARM erneut halten.` |
| der Leerlaufpuls liegt weniger als 100us unter dem Vollpuls | beendet, nichts gesendet, `Leerlaufpuls muss mindestens 100us unter Vollpuls liegen. Nicht gesendet.` | `Leerlaufpuls liegt nicht 100us oder mehr unter Vollpuls. Einen ändern, dann ARM erneut halten.` |
| das Schreiben wird abgelehnt | beendet, `Der IOmcu hat den Pulsbereich abgelehnt.` | `Der IOmcu hat den Pulsbereich abgelehnt. Leerlaufpuls, Vollpuls oder OUTPUTS ändern.` |
| das Schreiben wird quittiert, und ein Gaskanal hat ein anderes Paar gespeichert | beendet | `Der IOmcu hält einen anderen Pulsbereich. Leerlaufpuls oder Vollpuls ändern.` |

Der Alert eines abgelehnten oder vertauschten Paars verlässt das Band vor
Ablauf seiner 30s, sobald eine spätere Zahlung die Page mit dem
verlangten Paar findet oder ein Schreiben davon quittiert wird. Dasselbe gilt
für `Der IOmcu hat die Polzahl abgelehnt.`: er verlässt das Band, sobald
eine spätere Polzahl übernommen wird.

Das Panel fragt vor dem CLEAR, nach dem Freigeben des Servos und der
Pulsrate, und schreibt nichts, auch kein CLEAR; der Prüfstand bleibt SAFE mit
den Ausgängen in Ruhe. Es fragt nach einem quittierten CLEAR erneut, für
eine Änderung, die eintraf, während das CLEAR unterwegs war, und schreibt
dann keinen Frame, der auf ARMED schaltet. Es fragt ein drittes Mal, sobald
der Frame, der auf ARMED schaltet, quittiert ist, für eine Änderung, die
eintraf, während dieser Frame unterwegs war, und schreibt dann ARM = 0 im
nächsten Austausch: der IOmcu ist für diesen einen Austausch in Ruhe ARMED
auf dem Bereich, den er hielt (Dauer nicht gemessen), und die Bank des
Panels schaltet nicht auf ARMED. Die Bildschirme des Panels können dort
keine Änderung veröffentlichen: die beiden Zeilen auf OPTIONEN sind
gesperrt, solange ARM gehalten wird und solange ein ARM auf seine Antwort
wartet. VERWERFEN auf SETUP, das einen ungespeicherten Wert zurücksetzen
kann, liegt auf einem anderen Bildschirm und nimmt keinen Tipp an, solange
der Prüfstand ARMED ist, und das Verlassen von MOTOR & ESC oder SERVO
schaltet den Prüfstand auf SAFE.

Die Schuld wird auf dem Control-Task beglichen, dem Task, der das Schalten
schreibt, also läuft das Schalten nie, während ein Begleichen unterwegs ist.
Ein abgelehntes Schalten wird nicht von selbst wiederholt: ARM wird erneut
gehalten. STOP ist davon nicht berührt. Eine Ablehnung oder ein anders
gespeichertes Paar wird auch nicht von selbst erneut gesendet: dieselbe
einmal abgelehnte Anfrage wird jedes Mal abgelehnt. Die Sperre endet beim
ersten Begleichen nach einer Änderung, einer Zuordnung oder einem Link-up,
das das Paar in Kraft findet, und nicht vorher; ein Begleichen, das erneut
abgelehnt wird, sperrt erneut mit seinem Grund.

Wer einen IOmcu angeschlossen hat, der die Seite ablehnt, liest beim
Link-up `Der IOmcu hat seinen Pulsbereich nicht gemeldet.` und bei jedem
Halten von ARM `Kein Pulsbereich vom IOmcu. IOmcu-Firmware aktualisieren,
dann ARM erneut halten.`. Mit diesem IOmcu schaltet der Prüfstand nicht auf
ARMED. Das Panel bietet beim Link-up das IOmcu-Image an, das es mitbringt,
nach seinem eigenen Halten von 2s; der IOmcu startet nach dem Update neu,
und der folgende Link-up schuldet das Paar erneut. Ohne IOmcu schaltet die
eigene Bank des Panels auf ARMED.

Eine Änderung zur Zeit 0 wird beim ersten Poll ab 300ms beglichen: ein Poll
bei 299ms lässt sie offen, ein Poll bei 300ms begleicht sie. Die Zeit wird
modulo 2^32ms genommen, also läuft die Ruhezeit über den Überlauf der
Millisekundenuhr hinweg. Die Zeilen der beiden Werte auf OPTIONEN sind
gesperrt, solange ARM gehalten wird, und das Halten dauert 2s, also ist eine
Änderung vor dem Druck in der Regel innerhalb des Haltens beglichen.

Ohne Link schaltet die eigene Bank des Panels auf ARMED, und nichts wird
geschrieben. Ein Link, der zurückkommt, schuldet das Paar erneut und hält
den IOmcu SAFE bis zu einer Antwort; eine Ablehnung von vor dem Link-up
dauert also bis zum ersten Poll danach, und nur dann weiter, wenn dieser
Poll erneut abgelehnt wird. Auf dem Host getestet in `test_endpoints_link`
und `test_arming`. Der Control-Task des Panels, der den Zustand liest und
den Hinweis zeigt, ist nicht in der Coverage-Zahl.

## Verhaltensweisen, die Absicht sind

- STOP rastet ein. Der Prüfstand bleibt SAFE, bis er erneut auf ARMED
  geschaltet wird.
- STOP schließt die Rückfrage AUSGANG IST EINGESCHALTET auf SUPPLY und SERVO. Jeder
  Bildschirm zählt die Stopps, die ihm gemeldet werden, und eine Rückfrage
  gilt nur unter dem Zählerstand, mit dem sie gestellt wurde. Ein ÜBERNEHMEN
  im selben Frame wie STOP ändert also keinen Sollwert, auch wenn das
  Netzteil den Ausgang erst eine Messung später als aus meldet.
- Die Verdrahtung des PD mini (SETUP INTERFACES) ändert sich, sobald das
  Modul einmal geantwortet hat, nur auf eine nach der Änderung gesendete
  Zustandslesung, die den Ausgang aus zeigt. Ein Modul, das sich in den
  500ms zwischen zwei Lesungen selbst eingeschaltet hat, behält seine Pins
  und damit seinen Weg zum OFF; der IOmcu schaltet es ab.
- Einen eingeschalteten Ausgang des PD mini, dessen Eingangsspannung bei 2 Lesungen
  hintereinander unter dem Sollwert plus 0,5V liegt, schaltet der
  IOmcu ab und meldet es dem Panel danach. Eine vorsichtige Regel,
  ohne Messung gewählt: sie kann einen Lauf abbrechen, der durchgehalten
  hätte. Sollwerte sind auf 0,7V unter der Eingangsspannung begrenzt, 0,2V
  über dieser Linie: sinkt die Eingangsspannung unter Last um weniger als
  0,2V, schaltet nichts ab. Ein ON, verlangt während die Eingangsspannung
  unter 1,5V liegt, wird nicht geschrieben.
- Das Schalten auf ARMED ist ein zwei Sekunden langes Halten auf ARM, und das
  Kommando geht ab, wenn das Halten durchgelaufen ist, nicht wenn der Finger
  abhebt. Das Schalten auf SAFE ist ein Druck.
- Das Panel schreibt seinen eigenen Flash nur bei einem Prüfstand in SAFE,
  ohne wartendes Schalten auf ARMED, mit ausgeschaltetem
  Netzteil-Ausgang und ohne eingereihtes ON. Flash-Auto-Suspend ist aus,
  also hält jedes Löschen und Schreiben beide Kerne des Panels an, den
  Control-Task mit Heartbeat, STOP, OUTPUT OFF und den Abschaltungen des
  Netzteils eingeschlossen. Die Regel ist `flash_gate_open()` in
  `shared/safety/flash_gate.c`, unter `test_flash_gate`. Sie hält das
  Speichern der Einstellungen und das Ablegen der Zeichnung der Platine zurück.
  Das Schalten auf ARMED und das Netzteil werden für einen Schreibvorgang nie
  verweigert: der Schreibvorgang wartet. Das Ablegen (2 Löschvorgänge zu 4kB
  und 2 Schreibvorgänge für die 1 560 Bytes große Zeichnung des RP2350-CAN)
  läuft einen Sektor pro 5-ms-Durchlauf des Control-Tasks und hält zwischen
  zwei Sektoren an, wenn der Prüfstand ARMED ist oder das Netzteil
  einschaltet. Es macht beim ersten sicheren Durchlauf mit demselben Sektor
  weiter. Sein Header wird zuerst gelöscht und zuletzt geschrieben; ein
  angehaltenes oder durch Stromausfall abgebrochenes Ablegen hinterlässt also
  keine auffindbare Zeichnung für die Platine, und der nächste Link-Aufbau
  holt sie neu. Die Dauer eines Löschvorgangs auf dem Flash des Panels
  ist nicht gemessen.
- Ein Stick-Lauf auf MOTOR & ESC > PROGRAMMIEREN schaltet über dieselbe Policy auf ARMED, nachdem
  seine Warnung (PROPELLER AB? MOTOR GESICHERT?) 2s gehalten ist. Danach
  setzt er das Gas auf die Einstiegsstellung des Profils, bevor er das
  Netzteil einschaltet; bei den meisten ESCs ist das der Knüppel auf MAX: so
  wird ihr Menü betreten. Der ESC gibt dann sein Programmiermenü aus und
  treibt nicht, aber in seltenen Fällen kann ein Motor anlaufen und drehen;
  deshalb verlangt die Warnung einen Lastwiderstand anstelle des Motors oder
  einen fest montierten Motor ohne Propeller. Das ARM
  des Haltens wartet einen Frame im Bildschirm; ein STOP oder ein verlorenes
  Touch-Ereignis in diesem Frame nimmt es zurück und beendet den Lauf, damit
  es den Stopp nicht aufheben kann. STOP, ein Schalten auf SAFE, ein verlorener
  Link, ABBRECHEN und das Verlassen des Bildschirms beenden den Lauf mit dem Gas
  auf Minimum, dem Netzteil aus und dem Prüfstand auf SAFE, in einem
  Schritt. Ein Lauf, der wie geplant endet, schaltet zuerst das Netzteil aus
  und bewegt den Knüppel, wenn das Netzteil selbst den Ausgang aus meldet.
  [Stick-Programmierung](StickProgramming-de.md).
- Einem Halten werden höchstens 250ms pro Frame gutgeschrieben, es erstreckt
  sich also über mindestens acht Frames mit stehendem Druck. Die Dauer eines
  Frames wird an seinem Anfang gemessen und an seinem Ende angewendet; ohne
  die Obergrenze schreibt ein einzelner verspäteter Frame einem Halten Zeit
  gut, das entstand, während derselbe Frame seine Touch-Events zustellte. Das
  Halten zum Schalten auf ARMED und die Quittierung des Bus-Fault nehmen beide
  diese Obergrenze.
- Ein Frame, der Touch-Events verloren hat, bricht die laufende Geste ab.
  Eine volle Touch-Queue verwirft ihren ältesten Eintrag, um den neuesten
  aufzunehmen, und keine Wahl dort ist für sich sicher: Ein Release, das nie
  ankommt, lässt eine Seite einen Druck halten, ein Druck, der nie ankommt,
  macht das Release danach elternlos, und die Bewegung, mit der ein Finger
  eine Taste verlässt, ist das, was das Halten aufgibt. Der Render-Task
  leert die Queue vom anderen Kern, das Prüfen eines Eintrags entscheidet
  also nicht, welcher entfernt wird. Stattdessen nummeriert der eine
  Erzeuger jeder Queue jedes Event, das er anbietet, ob die Queue es behält
  oder nicht, und der Verbraucher findet einen Verlust als Lücke in den
  Nummern: am ersten Event danach, bevor es behandelt wird, oder am Ende
  eines Leerens gegen die Nummer, die der Erzeuger vor dem Leeren
  veröffentlicht hat. Kein Zähler, der auf einem Kern steigt, muss rechtzeitig
  auf dem anderen gelesen werden. Der Render-Task sagt dann jeder Seite, dass
  ihr Bild vom Glas veraltet ist, nicht nur der obersten, weil ein Event,
  das den Verlust überlebt hat, von der Seite mit dem Druck weg navigiert haben
  kann; jede Seite verwirft jede laufende Geste. Das kommandiert nichts,
  genau wie ein frühes Loslassen, mit der einen Ausnahme weiter unten. Beide
  Queues sind nummeriert: die Event-Queue des Treibers verwirft ihren
  ältesten Eintrag aus demselben Grund, und eine Lücke, die der Control-Task
  dort findet, reist als eigener Eintrag zum Render-Task, vor den Events, die
  ihr folgten. Der Render-Task leert die Queue unmittelbar vor dem Tick des
  Frames noch einmal, damit ein Event, das während des Durchlaufs verloren
  geht, in diesem Frame beantwortet wird und nicht erst im nächsten. Das
  Frame-Log führt die in jeder Queue fehlenden Events als
  `TOUCHLOST <Panel>/<Treiber>`. Der Control-Task verwirft aus demselben
  Grund seinen eigenen Vermerk eines STOP-Drucks bei einem Verlust im
  Treiber: Er besitzt diesen Druck unabhängig von den Seiten.
- Jedes Bedienelement, das zwischen Druck und Release Zustand hält, bricht
  ab. MOTOR & ESC, SERVO und CAN BUS FAULT haben eine Geste, die auf einem
  Timer fertig wird, ein verlorenes Release schaltet dort also von selbst
  auf ARMED oder quittiert; die Kacheln der Übersicht, die Zellen des Outputs-
  und des Picker-Bildschirms, die Tasten des Einstellungs-Bildschirms, die
  Tasten, Zeilen und die LÖSCHEN-Rückfrage des Log-Viewers und die Tab-Zeilen
  von MOTOR & ESC, RECEIVER und BALANCE wirken stattdessen auf das Release,
  und ein gehaltener Druck besitzt eine Track-ID, die der Controller
  wiederverwendet; ein späterer Kontakt, der woanders begann, wird dann für
  das fehlende Release gehalten. Die LÖSCHEN-Rückfrage bleibt bei einem Abbruch
  offen und nimmt den nächsten frischen Druck an. HOME und STOP sind die eigene
  Geste des Routers, und er bricht sie selbst ab.
- Ein Kontakt hat vom Druck bis zum Release einen Besitzer, und der Router
  entscheidet ihn. Eine Seite bekommt ein Move oder ein Release nur für einen
  Kontakt, dessen Druck sie bekommen hat. Ein Kontakt, der auf dem Statusband
  (den oberen 48px) oder auf dem Alert-Band aufsetzt, gehört dem Router, bis
  er abhebt, wohin er auch wandert. Die Ausnahme ist ein Druck auf ein
  Bedienelement, das anhält, was der Prüfstand treibt (DISARM, AUSGANG AUS,
  ABBRECHEN eines Stick-Laufs, TEST BEENDEN, LAUF BEENDEN), das die Seite
  dem Router nennt (`safe_at` in ui_screen.h): Unter dem Alert-Band oder bei
  offenem Hilfe-Kasten eines Chips gehört er der Seite, und Band oder Kasten
  schließen sich. ARM wird nie genannt, also startet ein Druck auf ARM unter
  beiden kein Halten. Ein Kontakt, der beim Bildschirmwechsel
  oder beim Verlust von Touch-Events auf dem Glas lag, gehört niemandem, bis
  er abhebt und neu aufsetzt. Ein Kontakt der Seite, der das Statusband
  erreicht, wird für die Seite an der letzten Stelle losgelassen, die sie
  bekommen hat, und gehört von da an dem Router: Ein Drag auf einem
  Schieberegler oder dem Zifferblatt endet mit dem Wert von dieser Stelle,
  ein Halten ist abgebrochen, und der Weg auf dem Band wird nicht angewendet.
  Ohne dieses Release bleibt der Drag an seiner Track-ID hängen, und ein
  späterer Kontakt mit derselben ID, der vom Band in den Bildschirm wandert,
  bewegt den Wert um seinen eigenen Weg vom Ursprung des ersten Drags aus.
  Das Release trägt eine Marke (`TOUCH_FLAG_NO_TAP`), ebenso das Release, das
  der Tracker für einen Kontakt erzeugt, der mehr als 120px von der Stelle
  des vorigen Reports entfernt liegt. Ein markiertes Release beendet einen
  Druck oder einen Drag und löst kein Bedienelement aus: Der Finger hat dort
  nicht abgehoben. Zwei Bedienelemente nehmen es trotzdem an, weil sie etwas
  anhalten: AUSGANG AUS und START TEST, solange ein Servotest läuft. STOP
  und DISARM auf MOTOR & ESC und auf SERVO haben schon beim Aufsetzen
  gewirkt, und ihr Release tut nichts.
  STOP und das Home-Tag reagieren auf einen Druck, der auf ihnen aufsetzt:
  Ein Kontakt, der vom Bildschirm auf STOP rutscht, drückt nichts. Ein Druck,
  den der Router der Seite vorenthält (auf dem Band, auf STOP, dem Home-Tag,
  einem Chip oder dem Alert-Band), lässt die Kontakte der Seite ebenso los,
  an ihrer letzten Stelle im Bildschirm und markiert: Ein laufender Drag auf
  dem Zifferblatt oder einem Schieberegler endet bei diesem Druck, und nichts,
  was er getrieben hat, wird danach kommandiert. Die
  Tabelle fasst 5 Kontakte, so viele meldet der GT911; ein sechster erreicht
  keine Seite.
- Ein Bedienelement, das eine Einstellung ändert, wirkt auf ein Tippen und
  nicht auf das Aufsetzen. Auf SETUP stellt eine Taste `-` oder `+`, wenn der
  Finger, der sie gedrückt hat, innerhalb von ihr abhebt, in x und in y
  höchstens 8px vom Aufsetzpunkt entfernt; die Liste scrollt von überall,
  die Tasten eingeschlossen, und ein Scrollen ändert keinen Wert. Eine
  gehaltene Taste stellt ab 0,45s nach dem Aufsetzen. Die Bedienelemente von
  ESC STICK folgen derselben Regel, VORGABEN und die Tasten des Stick-Timings
  eingeschlossen. ABBRECHEN eines Stick-Laufs wirkt beim Aufsetzen, ebenso
  jedes Anhalten auf den Prüfstandsbildschirmen, STOP und DISARM
  eingeschlossen: Anhalten wartet nie auf ein Abheben, das ausbleiben kann.
  Ein Druck auf DISARM startet kein Halten, also schaltet ein Finger, der auf
  der Taste bleibt, nichts auf ARMED, wenn sie wieder ARM zeigt; ARM bleibt
  ein Halten von 2s.
- Die Seiten verlassen sich dafür nicht auf den Router. Jeder Druck beendet
  einen Drag auf einem Schieberegler oder dem SERVO-Zifferblatt, der noch
  hängt, wo auch immer der Druck landet, und das Verlassen von SERVO beendet
  einen Drag auf dem Zifferblatt. SUPPLY hält einen Druck zur Zeit; ein Druck
  mit der Track-ID des gehaltenen sagt, dass dieser Kontakt weg ist, und der
  gehaltene Druck wird verworfen, wie ein Touch-Verlust ihn verwirft. Nicht
  am Panel gemessen: ob der GT911 einem einzelnen Finger bei
  aufeinanderfolgenden Berührungen dieselbe Track-ID gibt.
- Der Control-Task prüft das Rechteck von STOP bei jedem Aufsetzen selbst,
  unabhängig von den Seiten und vom Frame, und stoppt den Prüfstand beim
  Aufsetzen: Der Heartbeat fällt im selben Durchlauf weg, und der IOmcu
  geht innerhalb von 150ms in den sicheren Zustand. Ein Druck, der auf STOP
  aufsetzt und vor dem Abheben davon weggeführt wird, stoppt trotzdem. Bricht
  der Touch-Strom ab, während STOP gehalten wird, kommandiert das nichts
  weiter: Der Druck hat den Prüfstand schon gestoppt.
- Ein Druck auf DISARM schaltet beim Aufsetzen auf SAFE, also verliert ein
  Release, das eine volle Queue verschluckt, nichts. Ein Abbruch in ARMED,
  während ein Druck auf ARM noch liegt, dessen Halten nicht ausgelöst hat,
  schaltet ebenfalls auf SAFE: Die Taste unter diesem Finger zeigt DISARM.
  Das Schalten auf ARMED hat sein Kommando schon abgesetzt, wenn der Finger abhebt,
  ein halb abgebrochenes Schalten auf ARMED kommandiert also nichts, und das ist
  richtig so. Ein Abbruch verwirft außerdem ein Schalten auf ARMED, das die
  Seite bereits abgesetzt hat und die Anwendung noch nicht abgeholt hat --
  ein Kommando wird im Frame nach dem absetzenden weitergereicht, und der
  Frame, der einen Verlust bemerkt, bricht vor diesem Weiterreichen ab. Ein
  Schalten auf ARMED, das schon beim Control-Task liegt, trägt, wie oft der
  Render-Task Gesten verworfen hatte, und die Nummer des letzten Events, das
  er genommen hatte. Der Control-Task leert zuerst die Queue des Treibers und
  verwirft dann ein Schalten auf ARMED, wenn der Render-Task seither Gesten
  verworfen hat oder einen Verlust-Eintrag noch nicht genommen hatte. Ist
  das Schalten auf ARMED ausgeführt, macht jeder Verlust (der Render-Task
  verwirft Gesten, oder im Strom des Treibers fehlt eine Nummer) es
  rückgängig, bis der Render-Task ARMED bestätigt; das tut
  ein Frame nur, wenn er in ARMED begann und den Strom vollständig fand. Der
  Control-Task sieht in jeder Wartezeit einer Link-Transaktion nach, nicht
  nur einmal pro Durchlauf, und stoppt von dort wie bei STOP: der Heartbeat
  fällt sofort ab, und der IOmcu geht innerhalb von 150ms auf
  SAFE, wie lang der Rückstau hinter dem Schalten auf ARMED auch
  ist. Ein Frame, der
  vor dem Veröffentlichen begann, bricht Seiten ab, die den Prüfstand noch
  für SAFE halten, und die setzen kein DISARM ab; also muss der
  Control-Task es tun. Nach der Bestätigung sehen die Seiten einen Verlust
  gegen einen Prüfstand in ARMED. Über einen Verlust hinweg schaltet also
  nichts auf ARMED. Ein abgesetztes DISARM bleibt.
- Das Gas bewegt sich um die Strecke, die ein Finger zurücklegt, nicht auf die
  Stelle, an der er landet. Ein Druck auf den Track kommandiert nichts, eine
  Berührung am Ende fordert also nichts an; ein Drag über den ganzen Track
  fordert den ganzen Weg an, und einer, der innerhalb eines 50-ms-Polls
  abgeschlossen ist, ist ein Sprung von 0 auf 100% am Pin. Slider, die
  nichts Gefährliches kommandieren, etwa die Sweep-Geschwindigkeit des
  Servobildschirms, behalten Tap-to-set.
- Ein Ausgangswert ändert sich nur auf einem Prüfstand in ARMED, auf
  MOTOR & ESC wie auf SERVO. Solange der Prüfstand nicht ARMED ist, ändert
  kein Bedienelement der beiden Bildschirme den Wert, den es kommandiert:
  Nichts wird gepostet, der Wert und seine Anzeige bleiben, das
  Bedienelement wird gedimmt gezeichnet, und der Bildschirm zeigt
  `ZUERST ARM`. Eine Drehung des Drehknopfs in dieser Zeit wird nicht
  aufgehoben. Die Ablehnung reicht vom DISARM, bis der Prüfstand ARMED
  meldet, umfasst also die Zeit zwischen einem durchgelaufenen Halten auf
  ARM und der Antwort des Prüfstands. Von einem DISARM an, das ein Bildschirm
  anfordert, oder einem STOP auf einem Prüfstand in ARMED, bis der Prüfstand
  SAFE meldet, werden die Bedienelemente
  ebenfalls abgelehnt und gedimmt gezeichnet, solange das DISARM auf das
  Abholen wartet und danach. Auf SERVO enden ein Sweep, eine Pause, eine
  gehaltene Stellung und ein Lauf des automatischen Tests dort, wo das DISARM
  oder das STOP angefordert wird, und ein
  FREIGEBEN in dieser Zeit wird gesendet und bewegt keinen Wert. Ein
  laufender Drag endet bei einem ARM, einem DISARM und einem STOP, und ein
  Finger, der auf dem Bedienelement liegt, bewegt nichts, bis er abhebt und
  neu drückt. Ein Wert, den der Bildschirm
  gepostet und die Anwendung bei einem DISARM oder STOP noch nicht abgeholt
  hat, wird verworfen. Die einzige Ausnahme ist ein Hitec-Servo in seiner
  Programmiersitzung ([Ein Hitec-Servo bewegt sich bei SAFE](#ein-hitec-servo-bewegt-sich-bei-safe)).
- Jedes ARM beginnt bei der Ruhelage des Kanals, nicht dort, wo der letzte
  Lauf das Bedienelement gelassen hat.
  - MOTOR & ESC: Der Gas-Schieberegler, seine Schritte `-1` und `+1` und der
    Drehknopf sind die Bedienelemente. Ein DISARM setzt das Gas auf 0%, und
    es bleibt dort bis zum nächsten ARM; der erste Schritt nach einem ARM
    postet also 1,0%. Das Halten auf ARM setzt den Schieberegler auf 0%,
    wenn es anfordert, und ebenso der Wechsel auf ARMED bei einem ARM, das
    dieses Halten nicht angefordert hat, wie bei einem Knüppel-Lauf auf
    PROGRAMMIEREN.
  - SERVO: Das Zifferblatt, der Drehknopf, ZENTRIEREN und SWEEP sind die
    Bedienelemente. In SAFE bleiben das Horn und die gezeigte Pulsbreite
    auf der zuletzt gefahrenen Stellung. Bei einem ARM wird der Wert zur
    Ruhelage der Ruderfläche, die die Pins treiben, sobald das ARM seine
    Freigabe bezahlt hat: die Mitte zwischen den Endpunkten des Kanals,
    1500us über 1000 bis 2000us und 760us über 660 bis 860us. Die erste
    Eingabe nach einem ARM beginnt dort.
- FREIGEBEN auf SERVO ist keine Stellung und wird auf einem Prüfstand
  in SAFE angenommen, wo es keinen Wert bewegt. Auf einem Prüfstand
  in ARMED stellt es die Pins in die Ruhelage, und der gezeigte Wert wird
  zu dieser Ruhelage, wie bei einem ARM: Ein laufender Drag endet, und die
  erste Eingabe danach beginnt bei der Ruhelage. Die Freigabe, mit der ein
  Lauf des automatischen Tests endet, tut dasselbe.
- Das Verlassen eines Prüfstandsbildschirms schaltet auf SAFE.
- Der Drehknopf (ein Winkelsensor MT6701 unter 0x06; SETUP, PANEL, `Drehknopf`,
  standardmäßig AUS) bewegt das Gas auf MOTOR & ESC und das Horn auf SERVO um
  die Strecke, die er gedreht wird, wie der Slider, und es gelten dieselben
  Regeln. Er schaltet nie auf ARMED: Das einzige Schalten auf ARMED ist der ARM-Hold. Auf beiden
  Bildschirmen bewegt eine Drehung auf einem Prüfstand in SAFE nichts
  und wird nicht aufgehoben; nach einem ARM bewegt der Knopf auf MOTOR & ESC
  von 0% aus weiter und auf SERVO von der Ruhelage aus. Das Verlassen des
  Bildschirms schaltet auf SAFE. Die
  erste Messung nach dem
  Einschalten, eine Messung, die nicht eintrifft (ein NACK oder ein
  Timeout von 5ms), und ein
  Sprung über 90deg zwischen zwei Messungen setzen jeweils die Referenz neu
  und bewegen nichts; ein Knopf, der aufhört zu antworten, trägt also nichts
  bei, und kein letzter Wert bleibt als Kommando stehen. Ein Finger auf
  Track oder Zifferblatt besitzt den Wert gegenüber dem Knopf für den ganzen
  Frame, in dem er das Bedienelement berührt hat, und auf SERVO besitzen ein
  Sweep, ein Testlauf und das Einstellungsfeld das Horn. Ein Frame, der
  Touch-Ereignisse verloren hat, oder in dem der Router überhaupt navigiert
  hat, verwirft die Bewegung des Knopfs. Das Kommando des Knopfs geht mit dem
  nächsten Frame hinaus; findet dieser Frame zuerst verlorene
  Touch-Ereignisse, wird das Kommando zurückgenommen, und der Wert kehrt auf
  den Stand vor dem Knopf zurück. Der Knopf ersetzt nie ein
  anderes wartendes Kommando als ein Gas oder eine Horn-Position: Ein
  vollendetes Schalten auf ARMED, ein Schalten auf SAFE, ein Freigeben und ein Zurücksetzen
  der Spitzen bleiben stehen, und seine Bewegung wird verworfen.
  Der Knopf wird von einer eigenen Task auf dem Kern gelesen, den der Renderer
  benutzt, nie von der Control-Task, mit 5ms Bus-Timeout für jede
  Transaktion eines Polls: eine Adressprobe, solange er keine Referenz hat, dann drei
  Lesezugriffe. Eine fehlgeschlagene Transaktion kostet dazu die Erholung des
  Treibers, einen Reset des Controllers und ein Freiräumen des Busses, das bis
  zu 50ms länger wartet, solange SDA low gehalten wird. Der Knopf hängt an einem
  eigenen I2C-Controller (I2C_NUM_1, IO30 und IO31 des Headers P3), und
  nichts sonst liegt an diesem Bus; ein Sensor, der SDA low hält,
  verzögert also nur die Knopf-Task. Der Touch-Controller hängt am anderen
  Controller (I2C_NUM_0, GPIO7 und GPIO8), und ein Touch-Controller, der
  500ms schweigt, schaltet den Prüfstand auf SAFE (`ARMING_TOUCH_DEAD_MS`).
  Das Lesen des Touch liegt nicht auf dem Weg des Heartbeats: die
  Control-Task erzeugt den Heartbeat und benutzt keinen der beiden I2C-Busse.
  Der Touch-Bus trägt den GT911 bei 0x5D und den Touch-Injektor eines Builds
  mit ihm bei 0x14. Das Panel legt weder den Knopf noch ein anderes selbst
  angeschlossenes Gerät auf diese beiden Adressen oder auf die, die die
  I2C-Spezifikation reserviert, 0x00 bis 0x03 und 0x78 bis 0x7F
  (`shared/bench/include/panel_i2c.h`). Dass auf dem Bus des Knopfs nichts
  auf seiner Adresse 0x06 antwortet, beruht darauf, dass der Bus allein dem
  Knopf gehört; was auf der Platine antwortet, ist nicht gemessen.
- Antwortet der Touch-Controller 500ms lang nicht, schaltet der Prüfstand auf SAFE
  und verweigert das Schalten auf ARMED. Das Panel ist der einzige Ort mit einem
  STOP-Knopf.
- Nach einem Link-Failsafe schaltet der IOmcu nicht wieder auf ARMED, wenn
  Verkehr zurückkehrt. Das Failsafe wird durch das Schreiben eines definierten
  Werts (0x5AFE) auf die Control-Page verlassen. Dasselbe gilt nach einem
  Start des IOmcu und nach einem Heartbeat, dem nicht vertraut
  wurde: siehe [Arm-Latch](#arm-latch).
- Ein Link, der bei einem Prüfstand in ARMED ausfällt, ist am Panel ein Stopp. Er
  rastet ein wie STOP und wird vom nächsten Schalten auf ARMED gelöst.
- Solange der Prüfstand ARMED ist, weist der IOmcu mit BAD_VALUE ein
  Schreiben auf OUTPUTS ab, das einen Slot ändert, und ein Schreiben auf
  CHAN_CFG, das die Rolle eines Kanals oder irgendetwas an einem
  Throttle-Kanal ändert. Slew und Pulsbereich einer Surface werden in ARMED
  angenommen: der SERVO-Bildschirm schreibt sie mit jeder Position. Das Panel
  schreibt eine Bindung und den Pulsbereich des Throttle nur in SAFE.
- Bei totem Link, verlorenem Heartbeat und absinkendem Eingang des PD mini
  handelt der IOmcu aus eigener Befugnis und meldet den Fehler beim
  nächsten Poll. Seine Fehlerbits für Überstrom, Übertemperatur und Stall
  setzt nichts ([Fehler](#fehler)).
- Ein ARM nennt die Rollen, die es treibt ([Link, Ein Arm für eine
  Rolle](Link-de.md#ein-arm-für-eine-rolle)). ARM auf MOTOR & ESC und ein
  Stick-Lauf auf dessen PROGRAMMIEREN schalten die Throttle-Ausgänge auf ARMED, ARM
  0x0002. SERVO schaltet die Surfaces und den Pin des PROG PORT auf ARMED, ARM
  0x0004. Ein Pin einer Rolle, die das ARM nicht nennt, sendet keinen Puls,
  wie bei einem Prüfstand in SAFE. Ein IOmcu älter als Protokoll
  4.18 nimmt jedes ARM ungleich 0 als ganze Bank; an ihm treibt also jeder
  gebundene Pin.
- Ein ARM für die andere Rolle geht über ein Schalten auf SAFE. Das Panel schreibt
  ARM = 0, wartet mindestens 100ms und schreibt dann die neuen Rollen; den
  Wechsel von einem ARM ungleich 0 zu einem anderen weist der IOmcu
  mit BAD_VALUE ab.
- STOP, das Ausbleiben des Heartbeat, Stille auf dem Link und ein
  Schalten auf SAFE setzen jeweils das ganze ARM auf 0, gleich welche Rollen es
  nannte, und jeder Ausgang hört auf.
- Ein Prüfstand in ARMED treibt jeden gebundenen Pin der Rollen in ARMED, ob
  ihn etwas kommandiert oder nicht. Ein Kanal, der 500ms lang kein Kommando bekommt,
  wird auf der Ruhelage seiner Rolle ausgegeben: gestoppt bei throttle,
  zentriert bei surface. Zentriert ist die Mitte der Endpunkte dieses Kanals
  — 1500us über den voreingestellten 1000 bis 2000us, 760us über den 660
  bis 860us eines schmalen Servos. Das Timeout legt den Kanal auf diese
  Ruhelage und lässt den Pin weiter treiben; beendet werden die Flanken durch
  das Schalten auf SAFE.
- 1500us an einem Empfängerausgang sind etwa halbes Gas. Was ein ESC tut,
  der noch keinen Puls gesehen hat und dann 1500us bekommt, ist an diesem
  Prüfstand nicht gemessen: er kann mit etwa halbem Gas laufen, und er kann
  das Schalten auf ARMED verweigern, bis er einen Stopp gesehen hat. Ein Pin, der
  als Servoausgang gebunden ist, kann daher einen laufenden Motor führen.
- Das Gas erreicht einen Kanal mit der Rolle surface nicht, weil es die Pins
  kommandiert, die die Bindung als Motoren führt. Ein Schreiben auf die
  CHANNELS-Page erreicht ihn, weil diese Page Kanäle über den Index adressiert
  und nicht über die Rolle.
- In den ersten 500ms nach dem Schalten auf ARMED steht ein Kanal nicht auf
  seiner Ruhelage. Das Schalten auf ARMED stempelt die Uhr jedes Kanals, sodass
  ein Kommando, das in SAFE gegeben wurde, nicht überfällig
  ist und ausgegeben wird, bis es überfällig wird. Was ausgegeben wird, hängt
  vom Slew des Kanals ab: ohne Slew ist der erste Schritt die ganze Strecke,
  der Kanal steht also auf diesem Kommando; mit Slew hat das Schalten auf SAFE ihn
  bereits auf die Ruhelage gestellt und er rampt von dort, höchstens
  `slew_per_s * 500 / 1000` weit, bevor der Timeout ihn zurückholt. In beiden
  Fällen treibt er, und beendet wird das durch ein Schalten auf SAFE.
- Die Einstellung `Rampengrenze`, 5 bis 300%/s, gilt für die Gas-Bank des
  Panels. Diese Bank ist der modellierte Prüfstand: ihr geslewter Wert wird
  vom Telemetrie-Simulator gelesen und sonst von nichts, und nur solange der
  Link unten ist. Einem antwortenden IOmcu wird stattdessen das rohe
  Kommando geschickt, und `outbind_to_chan_cfg()` schreibt für keinen Kanal
  einen Slew, ein als Throttle gebundener Pin springt also im nächsten
  1-ms-Durchlauf darauf. Das physische Gas wird nicht gerampt, per
  Entscheidung: die Bank rampt ein Throttle nur aufwärts, ein Ramp auf dem
  Draht würde also nur den Anstieg verlangsamen und sonst nichts. [STATUS.md,
  Not planned](https://github.com/subtilitas/rcbench/blob/main/STATUS.md#not-planned).

### Ein Hitec-Servo bewegt sich bei SAFE

Ein Hitec-Servo darf sich bewegen, während der Prüfstand SAFE ist, und nur
in einer gehaltenen Programmiersitzung. Das ist die einzige Ausnahme von der
Regel, dass sich ein Ausgangswert nur bei ARMED ändert. Der Prüfstand
schaltet nicht auf ARMED, solange die HITEC-Seite einen Port hält
([LinkHitec, STOP](LinkHitec-de.md#stop-schalten-auf-safe-heartbeat-verlust-und-link-verlust)), und ein Hitec-Servo nimmt sein Ziel über
den Programmierdraht, nicht als Pulsbreite.

Die Regel:

- Eine Bewegung braucht eine betretene Sitzung (`hitec_session_move()`;
  sonst abgelehnt mit `HITEC_SES_ERR_NOT_IN_MODE`). D series: ein Ziel von 0
  bis 6000 in Register 0x1E geschrieben, dann Register 0x0C, die Position,
  zurückgelesen. Classic: `66` mit einem Ziel von 3600 bis 8400 (900 bis
  2100us), dann `65`. Ein Ziel außerhalb des Bereichs wird abgelehnt
  (`HITEC_SES_ERR_ARG`).
- Eine Bewegung schreibt nur ins RAM des Servos: keine EEPROM-Zelle, kein
  Speichern (0x70), kein Neustart. Kein Schritt einer Bewegung trägt MORE,
  ein OFF der Versorgung während einer Bewegung wird also nicht für ein
  Schreiben gehalten ([Die Versorgung eines Programmierports](#die-versorgung-eines-programmierports)).
  Ein Wert erreicht den Speicher des Servos nur über ein Schreiben mit
  Backup, Protokoll und Rücklesen.
- Die kommandierte Position bewegt sich höchstens 150 Zähler (2.0deg) je
  Durchlauf und bleibt innerhalb der Reichweite der gespeicherten Endpunkte
  des Servos: der Positionen der Ziele 0 und 6000, 2606 bis 13743 bei einem
  D485HW im Werkszustand (`hitec_live_d_command()`).
- Jeder Durchlauf wird beurteilt (`hitec_live_judge()`). Er gilt als
  blockiert, wenn die gelesene Position bei einem Kommando, das steht oder
  nach außen geht, in 2 Durchläufen hintereinander mehr als 150 Zähler vom
  Kommando entfernt liegt, wenn der Strom der Versorgung 400ms lang bei oder
  über 80% der Stromgrenze der Sitzung liegt (800mA bei den
  voreingestellten 1000mA), oder wenn der AS5600-Winkel, sofern verbaut,
  sich um weniger als die Hälfte einer kommandierten Änderung von 3deg oder
  mehr bewegt hat. Eine Blockierung setzt das Kommando auf die letzte freie
  Position zurück, 75 Zähler (1.0deg) zur Neutralstellung hin.
- Die Abbildung nimmt nur ein auf CW gestelltes Servo
  (`hitec_live_d_ends()`).
- Bewegungen werden mit einem 2s langen Halten eingeschaltet, und nur mit
  einem antwortenden PD mini mit eingeschaltetem Ausgang: STOP schaltet den
  PD mini aus und damit das Servo. Die eigene Stromgrenze des PD mini ist
  die feste Obergrenze.

Aktuelle Einschränkungen:

- Das Panel hat kein Bedienelement, das Bewegungen einschaltet (MOVE SERVO
  auf dem Reiter WEG): nichts ruft `hitec_session_move()` auf.
- Jede Schwelle der Beurteilung ist gewählt, nicht mit einem Servohebel an
  einem Anschlag gemessen. Der Blockierstrom eines D485HW ist nicht bekannt.
- Ob das Servo der Ziellinie unter 400 und über 5600 folgt oder an seinen
  Endpunkten stehen bleibt, ist nicht gemessen; die Reichweite nimmt an,
  dass die Linie weitergeht.
- Ob ein Servo der D series ohne Frames sein letztes Ziel hält, ist nicht
  gemessen. Ein Schalten auf SAFE am Prüfstand allein stoppt den Port und
  lässt die Versorgung an ([LinkHitec, STOP](LinkHitec-de.md#stop-schalten-auf-safe-heartbeat-verlust-und-link-verlust)).
- Ob ein Neustart einen nicht gespeicherten RAM-Wert verwirft, ist nicht
  gemessen.
- Die Abbildung eines auf CCW gestellten Servos ist nicht gemessen.

## Heartbeat statt Enable-Pegel

Ein statischer Enable-Pegel versagt, wenn die Firmware mit gesetztem Pin
hängen bleibt. Flanken laufen von selbst ab. Die Periodenprüfung in der
Firmware weist eine kurzgeschlossene oder klingelnde Leitung ab, die ein
Monoflop allein annehmen würde.

Das Panel hat keine Leitung zu irgendeinem Ausgang.
