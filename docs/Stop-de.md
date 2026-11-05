# STOP als Not-Aus

<sub>[English](Stop.md) · **Deutsch**</sub>

Die Taste STOP am Panel verhält sich wie ein Not-Aus-Taster. Eine Berührung
stoppt den Prüfstand und verriegelt den Stopp. Halte STOP 2s, um das Lösen
der Sperre einzuleiten. [Sicherheit](Safety-de.md) beschreibt die weiteren
Stoppmechanismen, die Heartbeat-Leitung und die eigene Sperre des IOmcu. Der Code ist
`shared/safety/stop_latch.c`, `arming_held()` in `shared/safety/arming.c`
und `shared/ui/stop_hold.c`, auf dem Host getestet in `test_stop_latch` und
`test_stop_hold`.

- **Berührung.** Ein Druck auf STOP auf einer beliebigen Seite schaltet den
  Prüfstand auf SAFE. Er schaltet alle Ausgänge, den Ausgang des PD mini
  und die 5V der Programmierports aus und hält den Heartbeat an, wie unter
  [Sicherheit](Safety-de.md#stoppmechanismen) beschrieben. Der Stopp rastet
  ein: er bleibt bestehen, wenn der Finger abhebt, die Seite wechselt oder die
  Verbindung wiederhergestellt wird.
- **Eingerastet.** STOP bleibt rot in einem gelben Rahmen von 3px und zeigt
  `GESTOPPT` über `2s HALTEN ZUM LÖSEN`. Das Feld für den ARM-Zustand im Band zeigt STOP
  statt SAFE oder ARMED. ARM, AUSGANG EIN, PROG 5V EIN, der Start eines Tests oder Endlagenlaufs werden
  mit der Meldung `STOP gedrückt: Halte STOP 2s, um ihn zurückzusetzen.` abgelehnt; die
  Programmierungen für KST, Hitec und die ESC-Leitung lehnen VERBINDEN mit
  ihrer Stopp-Zeile ab. Ein ARM hebt diesen Stopp nicht auf.
- **Halten zum Lösen.** Ein Druck auf ein eingerastetes STOP ist ebenfalls
  ein STOP und beginnt das Halten zum Lösen: die Füllung geht in 2s von Rot
  zum Gelb des Rahmens über. Das Halten zählt nur bei einer ununterbrochenen
  Berührung auf STOP über 2s, während der Touch-Controller durchgehend
  antwortet. Folgendes bricht das Halten ab, ohne das Lösen anzufordern: ein Loslassen
  vor 2s, das `Halte 2s, um den STOP zu lösen.` zeigt; ein Abgleiten von
  STOP; eine zweite Berührung auf dem Band; verlorene Touch-Ereignisse; ein
  Seitenwechsel; ein Touch-Controller, der 100ms
  (`STOP_LATCH_FRESH_MS`) nicht antwortet, was der Control-Task als
  Kontaktabbruch zählt. Ein STOP oder ein Abbruch weniger als 1900ms
  (`STOP_LATCH_HOLD_SEEN_MS`) vor dem Ende des Haltens, oder danach, lehnt
  die Anforderung ab.
- **Lösen.** Eine Zustandsmaschine im Control-Task führt das Lösen aus,
  ein Schritt pro Durchlauf:

  | Zustand | Verlässt ihn, wenn | Nach |
  |---|---|---|
  | LATCHED | das abgeschlossene Halten erfüllt die Zeitprüfung für den letzten STOP und Kontaktabbruch | HOLD_DONE |
  | HOLD_DONE | 100ms nach dem Halten: die erste Leseanforderung ist gesendet | ASKING |
  | ASKING | alle Rückmeldungen zeigen ausgeschaltete Ausgänge, 500ms (`STOP_LATCH_FLOOR_MS`) oder mehr nach dem STOP | CONFIRMED |
  | ASKING | keine Bestätigung binnen 2000ms (`STOP_LATCH_CONFIRM_MS`) | LATCHED |
  | CONFIRMED | der Heartbeat erzeugt wieder Flanken | RESUMED |
  | RESUMED | CLEAR einmal geschrieben, Link steht | CLEAR_WRITTEN |
  | CLEAR_WRITTEN | fertig | UNLATCHED |

  Ein STOP führt aus jedem Zustand zurück nach LATCHED. Ein Kontaktabbruch
  oder ein Fehler führt aus HOLD_DONE, ASKING oder CONFIRMED ebenfalls nach
  LATCHED. Wird STOP während einer Leseanforderung der Zustandsmaschine
  gedrückt, wird der Stopp vor dem Leseergebnis berücksichtigt. "Alles aus" wird in ASKING alle 50ms gelesen: STATUS nicht
  ARMED, der SUPPLY-Ausgang aus, wo der IOmcu diese Seite bedient, der
  PROG_SUPPLY-Ausgang aus, wo er diese bedient, und die eigene Bank SAFE
  ohne eingeschaltete Versorgung oder 5V. Der Prüfstand bleibt nach dem
  Lösen SAFE: ARM, AUSGANG EIN und PROG 5V EIN brauchen jeweils ihr eigenes
  Halten. Ein Programmierport mit aktiver Sitzung bleibt auch nach dem Lösen
  belegt. AUFHEBEN oder VERLASSEN auf seiner Seite setzt den Ablauf fort.
- **Keine Bestätigung.** Der Stopp bleibt eingerastet, der Heartbeat bleibt
  angehalten, und die Meldung lautet `STOP nicht gelöst: Der IOmcu meldete
  nicht binnen 2s alle Ausgänge aus.` Das nächste Halten fragt erneut.
- **Kein Link.** Antwortet kein IOmcu, prüft das Panel beim Lösen seine
  eigene Ausgangsbank und sein eigenes Modell. Es sendet kein CLEAR. Ein
  angeschlossener IOmcu, der nicht antwortet, ist wegen der ausbleibenden
  Verbindung in den Failsafe gewechselt. Er braucht das von ARM gesendete
  CLEAR, wie nach jedem Verbindungs-Failsafe.
- **FEHLER 10.** Solange STOP eingerastet ist, solange sein Lösen läuft,
  und 1000ms danach (`STOP_LATCH_GRACE_MS`), lässt das Band
  LINK_FAULT_HEARTBEAT weg:
  das Panel hält die Leitung selbst an, und FEHLER 10 sähe aus wie der
  Not-Aus oder eine offene Leitung. Der Hardware-Not-Aus an der
  Heartbeat-Leitung und eine offene Leitung zeigen FEHLER 10.
- **Neustart.** Die STOP-Sperre wird nicht gespeichert. Nach einem Neustart
  ist das Panel unverriegelt und SAFE; alle Ausgänge sind ausgeschaltet.
- Ein Stopp, den der Prüfstand selbst auslöst, ein bei ARMED verlorener
  Link, ein vom IOmcu abgelehntes Schreiben bei ARMED oder der Watchdog der
  Render-Schleife, ist nicht diese Sperre: Das Band behält SAFE und jedes
  FEHLER-Bit, auch FEHLER 10, denn die Ursache kann der Not-Aus oder eine
  offene Leitung sein, und das nächste Schalten auf ARMED hebt ihn auf. Ein
  Druck auf STOP darüber rastet STOP wie oben ein.
