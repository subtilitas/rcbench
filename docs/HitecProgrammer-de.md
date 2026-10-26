# Servoprogrammierung für Hitec-Servos

<sub>[English](HitecProgrammer.md) · **Deutsch**</sub>

Die Programmierung liest die Einstellungen eines Hitec-Servos über seine
Signalleitung und zeigt sie decodiert und so, wie sie gespeichert sind. Sie
liest nur: sie schreibt nichts ins Servo, führt keinen Vermerk und legt kein
Backup an. Sie öffnet sich aus ihrer Zeile `Hitec-Servos` unter
**PROGRAMMIEREN** der [Seite SERVO](Screens-de.md#servo), eine Ebene tiefer,
und deckt die linke Spalte samt den Reitern der Leiste ab. **SCHLIESSEN**
oder die Home-Tag führt zur Liste von PROGRAMMIEREN zurück, solange die
Programmierung keinen Port hält. Die Zeile ist mit `IOMCU OHNE SERVO PWM`
ausgegraut und öffnet nichts, solange der IOmcu, die RP2350-Platine, kein
Servo-PWM (Pulsweitenmodulation) meldet, also auch, wenn kein IOmcu
antwortet.

Hitec ist ein Name oder eine Marke seines Inhabers. rcbench ist unabhängig:
es ist mit ihm weder verbunden noch von ihm unterstützt oder gefördert. Der
Name sagt, mit welchen Servos die Programmierung arbeitet.
[Rechtliche Hinweise und Haftungsausschluss](../README-de.md#rechtliche-hinweise-und-haftungsausschluss)
enthält den Hinweis des Projekts.

Zwei Serien werden gelesen:

| Serie | Servos | Leitung | Gelesen |
| --- | --- | --- | --- |
| D-SERIE | D485HW und die übrigen Servos der D-Serie | 115.200bit/s, invertiert | 21 Register |
| KLASSISCH | HS-5xxx und HS-7xxx | 19.200bit/s | 100 Zellen: 0x00 bis 0x23, 0x30 bis 0x53, der Name 0x60 bis 0x6F, das Datum 0x70 bis 0x73, die Seriennummer 0x80 bis 0x87, und die Version |

Das Registermodell und seine Codierungen stammen von 1 D485HW (20.10.2026)
und von je 1 HS-5645MG, HS-7955TG und HS-5955TG (18.10. bis 20.10.2026)
([`protocols/hitec`](../protocols/hitec/README.md)). Die Page, auf der sie
läuft, beschreibt
[Der Programmierport für Servos von Hitec](LinkHitec-de.md).

![Die Programmierung, der Tester hält den Port](img/de/servo-hitec.png)

## Voraussetzungen

- Der IOmcu spricht Protokoll 4.23 oder neuer. Mit einem älteren sagt der
  Reiter SERVO `Nicht verfügbar: Der IOmcu hat keinen Hitec-Port.` und
  `IOmcu auf Version 0.21.1 oder neuer aktualisieren.`
- SERVO-TESTER unter SETUP > OUTPUTS steht auf einem Port, PORT 1, PORT 2,
  PORT 3 oder PORT 4, und der Port nennt einen Pin. Mit OUTPUTS lautet die Zeile PORT
  `KEIN PROG-PORT GESETZT`, und VERBINDEN sagt `Zum Programmieren muss das
  Servo am Programmierport angeschlossen sein.`
- Der Prüfstand ist SAFE. VERBINDEN wird abgelehnt bei ARMED, bei einem
  angeforderten Schalten auf ARMED und mit einem Finger auf ARM: `Zuerst
  DISARM tippen. Bei ARMED kann der Port nicht übernommen werden.` Ein
  gesetzter STOP lehnt es ebenfalls ab: `Ein STOP ist gesetzt. Er wird beim
  nächsten Schalten auf ARMED aufgehoben.`
- Ein klassisches Servo geht nur beim Einschalten in den Programmiermodus,
  also schaltet der Prüfstand seine Versorgung aus und ein: VERBINDEN für
  KLASSISCH braucht ein PD mini, das antwortet, sonst `Klassische Servos
  brauchen ein PD mini: Der Prüfstand schaltet die Versorgung.`
- Ein Servo der D-Serie antwortet auf der Leitung so, wie SETUP >
  INTERFACES > Hitec-D-Empfang es einstellt, PULL-UP oder PRECHARGE
  ([Empfangsmodus](LinkHitec-de.md#empfangsmodus)). VERBINDEN wartet, bis
  der IOmcu diesen Modus hält: `Empfangsart im IOmcu noch nicht gesetzt.
  Erneut versuchen.`
- Die Programmierspannung ist die Einstellung, die auch die
  KST-Programmierung nutzt (SET_SERVO_PROG_V, WEITERES >
  PROGRAMMIERSPANNUNG), für eine Hitec-Sitzung 4,8V bis 6,0V: eine höhere
  Einstellung läuft mit 6,0V, und der Reiter SERVO sagt
  `Hitec-Programmierung läuft mit höchstens 6.0V.` Die Strombegrenzung liegt
  bei 50mA bis 3000mA. Beide müssen innerhalb der Grenzen von SUPPLY liegen,
  sonst sagt VERBINDEN `Programmierspannung oder -strom überschreitet die
  SUPPLY-Grenzen.`

## Der Reiter SERVO

| Zeile | Was sie tut |
| --- | --- |
| PORT | Zwei Zeilen: der Port und sein GPIO mit der Serie, `PROG-PORT 1 GPIO 13  D-SERIE`, dann die Empfangsart und die Versorgung, die VERBINDEN einstellt, `RX PRECHARGE  5.0V 1000mA` (`VERSORGUNG UNVERÄNDERT` für ein Servo der D-Serie ohne PD mini). Die Taste lautet VERBINDEN, ABBRECHEN, solange der Port in den Programmiermodus geht oder ein Lesen läuft, WARTEN, VERLASSEN, solange der Port gehalten ist, und PORT FREIGEBEN, solange eine andere Sitzung ihn hält |
| SERIE | D-SERIE oder KLASSISCH, vor VERBINDEN gewählt; bei den Einstellungen von SERVO gehalten (SET_HITEC_SERIES). Nicht angenommen, solange der Port gehalten ist |
| SERVO | Das gelesene Modell, `D485HW` oder `HS-5645MG`; ALLES AUSLESEN liest die Werte der Serie erneut |
| WEITERES | Programmierspannung und Strombegrenzung, jede auf dem Tastenfeld, solange der Tester den Port hat |
| NACH STOP | CLEAR nach einem Schalten auf SAFE am Prüfstand: der IOmcu löscht das Flag STOPPED des Ports, und das Lesen geht weiter, oder der Einstieg von vorn, wo die Versorgung ausging |

Unter den Zeilen sagt die Zustandszeile, was der Port gerade tut, und die
letzte Meldung steht am Fuß der Hinweise. Jedes Bedienelement ist 48px hoch
oder mehr und endet 4px über dem Alert-Band; die Maße sind die der
KST-Programmierung ([Seiten](Screens-de.md#programmierer)).

![Ein D485HW gelesen](img/de/servo-hitec-read.png)
![Die Liste SERIE](img/de/servo-hitec-series.png)
![Ein laufendes Lesen](img/de/servo-hitec-reading.png)

## VERBINDEN

1. OPEN: der IOmcu nimmt den Port aus PWM. 50ms kein Puls, dann gehört die
   Leitung dem Leitungstreiber, losgelassen.
2. Der Einstieg:
   - D-Serie: POWER mit der Programmierspannung und einer Auszeit von 0ms,
     wo ein PD mini antwortet, dann 1000ms, bis das Servo gestartet ist.
     Ohne PD mini läuft das Servo an der Versorgung, wie sie ist.
   - Klassisch: POWER mit 500ms ausgeschalteter Versorgung, dann ein, und
     ein LISTEN auf das `+` des Servos; sein zweites `+` folgt 250ms bis
     267ms später; 0x67 liest dann die Version.
3. ALLES AUSLESEN: die Werte der Serie, jede Adresse mit 3 Versuchen.
4. Das Modell wird erkannt: D-Serie an Register 0x00 (34485 ist der
   D485HW), klassisch an den Namenszellen. Die Meldung nennt es:
   `Servo der D-Serie, Modell 34485, ID 0.` oder
   `Klassisches Servo HS-5645MG, Version 0x16.` Ein Modell, das das
   Registermodell nicht kennt, wird im Aufbau des nächstliegenden gelesen,
   und der Reiter SERVO sagt welches: `Modell unbekannt. Die Felder werden
   wie beim D485HW gelesen.`

Im Host-Modell (`test_hitec_prog`) dauert VERBINDEN bei der D-Serie 2,4s,
davon 1,2s der Einstieg, und bei einem klassischen Servo 4,2s bei 16ms je
Frame und 7,6s bei 33ms: ein klassisches Lesen braucht 2 Frames je Zelle.
Am Prüfstand sind diese Zeiten nicht gemessen.

Solange der Port gehalten ist:

- ARM ist ausgegraut und abgelehnt: `ARMED gesperrt: Die
  Servoprogrammierung belegt einen Port.` DISARM und STOP wirken.
- Sollwerte und AUSGANG EIN der Zeile SOLL und der Seite SUPPLY nehmen
  keinen Druck an; ein Druck darauf sagt `Servoprogrammierung belegt das
  Netzteil: Sollwerte und EIN gesperrt.` AUSGANG AUS wirkt. Ab Protokoll
  4.29 hält der IOmcu seine SUPPLY-Page zusätzlich von OPEN bis CLOSE
  ([Die Versorgung, solange ein Port gehalten ist](LinkHitec-de.md#die-versorgung-solange-ein-port-gehalten-ist)).
- Die Zeile für KST- und Chaservo-Servos und die Zeile ZIEL von MOTOR & ESC
  nehmen keinen Druck an, und ESC STICK startet keinen Lauf.

## Die Wertereiter

BEWEGUNG, WEG, SCHUTZ und INFO zeigen, was das letzte vollständige Lesen
fand, in den Zeilen der Serie. Ein Wert, der zu seiner Codierung passt,
steht decodiert, mit dem gespeicherten Wert in Klammern daneben, `CW (0)`;
einer, der zu keiner bekannten Codierung passt, steht als `RAW 1234` in der
Warnfarbe. Keine Zeile nimmt einen Tipp an.

| Reiter | D-Serie | Klassisch |
| --- | --- | --- |
| BEWEGUNG | TEMPO, DEAD BAND, SOFT START | TEMPO, DEAD BAND, AUFLÖSUNG |
| WEG | NEUTRAL, ENDE LINKS, ENDE RECHTS in Servo-Zählern mit Grad (220° über 16383 Zähler, 8192 die Mitte), DREHRICHTUNG | MITTE in Mikrosekunden, ENDE LINKS, ENDE RECHTS als Rohwerte (Einheit unbekannt), DREHRICHTUNG |
| SCHUTZ | FAILSAFE (AUS, KRAFTLOS oder eine Position in Mikrosekunden), ÜBERLASTSCHUTZ, SMART SENSOR, EMPFINDLICHKEIT (roh) | FAILSAFE, FAILSAFE-POSITION, ÜBERLASTSCHUTZ |
| INFO | MODELL, ID, POSITION und die Register 0x04, 0x22, 0x8A, 0x8C, 0xD4 und 0xD6 roh | MODELL, VERSION, DATUM, SERIENNR., PRÜFSUMME |

Ein klassisches Abbild, dessen Zellen 0x00 bis 0x1C modulo 256 nicht 0
ergeben, zeigt PRÜFSUMME `FALSCH` und den Hinweis `Prüfsumme der Zellen
0x00-0x1C falsch. Das Servo folgt PWM evtl. nicht.`

![WEG eines D485HW](img/de/servo-hitec-travel.png)
![BEWEGUNG eines D485HW](img/de/servo-hitec-motion.png)
![SCHUTZ eines D485HW](img/de/servo-hitec-protect.png)
![INFO eines D485HW](img/de/servo-hitec-info.png)
![Ein HS-5645MG gelesen](img/de/servo-hitec-classic.png)
![WEG eines HS-5645MG](img/de/servo-hitec-classic-travel.png)
![SCHUTZ eines HS-5645MG](img/de/servo-hitec-classic-protect.png)
![INFO eines HS-5645MG](img/de/servo-hitec-classic-info.png)

## VERLASSEN

SCHLIESSEN, die Home-Tag oder VERLASSEN in der Zeile PORT öffnet
PROGRAMMIERMODUS VERLASSEN; 2s Halten auf `2s HALTEN ZUM VERLASSEN`
verlässt ihn:

- D-Serie: ein Neustart des Servos (Register 0x46 = 1), das danach 1s lang
  Signale ignoriert; die Versorgung bleibt unverändert.
- Klassisch: die Versorgung 500ms aus und wieder ein.

Dann CLOSE: der Port ist wieder in PWM, und die Meldung sagt
`Der Port ist wieder im Testermodus.` VERLASSEN wird auch an einem
gestoppten Port, bei ARMED und mit gesetztem STOP angenommen: den Neustart
lehnt ein gestoppter Port ab, das CLOSE wird immer angenommen.

![PROGRAMMIERMODUS VERLASSEN, D-Serie](img/de/servo-hitec-leave.png)
![PROGRAMMIERMODUS VERLASSEN, klassisch](img/de/servo-hitec-classic-leave.png)

## STOP, SAFE und eine verlorene Verbindung

| Ereignis | Der Port | Was der Bediener tut |
| --- | --- | --- |
| STOP, gedrückt oder vom Prüfstand selbst | Der IOmcu bricht die Transaktion nach dem Frame auf der Leitung ab, behält den Port mit losgelassenem Pin und setzt STOPPED. Das Panel setzt den Stopp und hält den Heartbeat zurück | `Gestoppt. Der Pin ist freigegeben. VERLASSEN, dann einmal auf ARMED schalten, um den STOP aufzuheben.` CLEAR und ALLES AUSLESEN werden bei gesetztem STOP abgelehnt: der IOmcu würde das CLEAR als UNSAFE ablehnen. VERLASSEN, dann ARM und DISARM, dann VERBINDEN |
| Ein Schalten auf SAFE am Prüfstand allein | STOPPED, kein gesetzter Stopp | `Gestoppt. Der Pin ist freigegeben. CLEAR zum Fortsetzen oder VERLASSEN.` CLEAR setzt das Lesen fort |
| ABBRECHEN | Der Vorgang endet; der Port bleibt gehalten | ALLES AUSLESEN erneut, oder VERLASSEN |
| Die Verbindung verstummt | Der Failsafe des IOmcu löst aus, und der Port bleibt gehalten; das Modul des Panels zählt ihn nicht mehr als seinen | Sobald die Verbindung zurück ist, sagt die Programmierung `Port von einer anderen Hitec-Sitzung belegt.` und bietet PORT FREIGEBEN an |
| Das Panel startet neu | Wie bei einer verlorenen Verbindung | Wie bei einer verlorenen Verbindung |

![STOP gesetzt](img/de/servo-hitec-stopped.png)

## Ein Port, den eine andere Sitzung hält

Ein Port, den die HITEC-Page hält und den dieses Panel nicht geöffnet hat,
von der Prüfstandskonsole geöffnet oder über eine verlorene Verbindung oder
einen Neustart des Panels gehalten, lässt die Hitec-Zeile aktiv: die
Programmierung öffnet sich mit der Zeile PORT
`PROG-PORT 1 GPIO 13  ANDERWEITIG BELEGT` und `Eine andere Hitec-Sitzung
belegt ihn.` PORT FREIGEBEN öffnet eine Seite; 2s Halten auf
`2s HALTEN ZUM FREIGEBEN` sendet CLOSE, das der IOmcu annimmt, ob er dem
Heartbeat traut oder nicht. Die Versorgung bleibt unverändert. Danach hebt
ein Schalten auf ARMED den ausgelösten Failsafe des IOmcu auf wie nach
jedem Stopp: ARM wird nur abgelehnt, solange der Port gehalten ist.

![Ein Port, den eine andere Sitzung hält](img/de/servo-hitec-other.png)
![PORT FREIGEBEN](img/de/servo-hitec-release.png)
![Der Port freigegeben](img/de/servo-hitec-released.png)

## Meldungen

| Zeile | Wann |
| --- | --- |
| `Kein Servo der D-Serie antwortet. Stecker, Empfangsart und Serie prüfen.` | beim letzten von 3 Versuchen eines Lesens der D-Serie kam kein Byte |
| `Kein klassisches Servo hat nach dem Einschalten geantwortet. Stecker und Serie prüfen.` | innerhalb von 2s nach dem Einschalten kam kein `+` |
| `Die Antwort des Servos auf den Einstieg war falsch. Serie prüfen.` | es kamen Bytes, die nicht zum Einstieg passen |
| `Fehlerhafte Antwort vom Servo (Code 14). Lese erneut.` | ein Versuch schlug fehl; der Code ist der des Decoders (`hitec_rx_t`) |
| `Lesen bei Adresse 0x32 fehlgeschlagen. ALLES AUSLESEN antippen.` | jeder Versuch an einer Adresse schlug fehl |
| `Vom IOmcu abgelehnt: Sein Failsafe ist ausgelöst.` | OPEN oder CLEAR mit UNSAFE abgelehnt |
| `Der IOmcu hat die Sitzung beendet. Der Port ist wieder im Testermodus.` | der Port ging ohne Befehl dieses Panels zurück in PWM |
| `Keine rechtzeitige Antwort vom IOmcu. Der Vorgang ist beendet.` | ein Befehl wurde nicht innerhalb von 2000ms angenommen, oder eine Anfrage hatte nach 30.000ms kein Ergebnis |
| `Die Versorgung folgte der POWER-Anfrage nicht. PD mini prüfen.` | POWER endete mit SUPPLY |
| `Der IOmcu kann die Leitung dieses Ports nicht ansteuern.` | OPEN fand keine freie Leitung |
| `Der IOmcu hat die Anfrage abgelehnt (Code 7).` | eine Ablehnung ohne eigene Meldung |

![Klassisches VERBINDEN ohne PD mini](img/de/servo-hitec-nopd.png)
![WEITERES](img/de/servo-hitec-more.png)

## Bekannte Grenzen

- Nichts wird geschrieben: die Programmierung liest nur.
- Das POWER des Einstiegs der D-Serie mit einer Auszeit von 0ms schaltet
  den Ausgang des PD mini aus und wieder ein: das Servo startet bei jedem
  VERBINDEN neu.
- Ein klassisches Lesen braucht 2 Frames der Anzeige je Zelle; bei 33ms je
  Frame dauern 100 Zellen im Host-Modell 6,6s.
- Die Prüfstandsschritte S1 bis S5 des Plans (ein D485HW bei PRECHARGE und
  PULL-UP, die klassischen HS-5645MG und HS-7955TG, STOP und ein gezogenes
  Verbindungskabel während ALLES AUSLESEN, ARM und SUPPLY bei gehaltenem
  Port) sind nicht gelaufen.
