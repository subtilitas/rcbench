# Servoverfahren

<sub>[English](Servo.md) · **Deutsch**</sub>

Diese Seite beschreibt drei Servoverfahren. Das erste misst ein Servo
im eingebauten Zustand: Die Endlagensuche auf SERVO liest den
INA3221-Kanal aus, der dem Ziel des Servotesters zugeordnet ist. Bei
einem KST-, Chaservo- oder unterstützten Hitec-Servo am Programmierport
kann sie auch die eigenen Weggrenzen des Servos verringern. Das zweite
Verfahren, der Abgleich, braucht einen Sensor für den gemeinsamen Strom
zweier Ausgänge. Dieser Sensor ist nicht bestückt; der Abgleich läuft
deshalb nur mit einem simulierten Servo. Ihre Vorgabewerte stammen aus
`servo_limit_defaults()` und `servo_sync_defaults()` in `shared/servo/`. Das
dritte, der [automatische Test](#automatischer-test), liest den Strom des
Netzteils, das das Servo versorgt. RUDER, eine Aufgabe der SERVO-Werkbank,
liest den Ausschlag einer Ruderfläche in ° und mm
([Ruderausschlag](#ruderausschlag)).

## Eingebaute mechanische Endlage

### Zweck

Im Sender gesetzte Endpunkte sind Schätzungen. Ein Servo, das gegen einen mechanischen Anschlag drückt, zieht
Blockierstrom, solange diese Stellung angefordert wird. Wo der Anschlag
liegt, hängt vom Einbau ab. Die beiden Enden des Ruderwegs unterscheiden
sich; miss deshalb beide.

### Verfahren

Die Suche schreitet von der Mitte nach außen, in Schritten von 10µs Pulsweite
(etwa 0,9° bei einem Servo mit 11µs je Grad Weg), bis 600µs von der Mitte
und nie über 500 oder 2500µs hinaus. Ein letzter Schritt unter 10µs erreicht
dieses Ende, so wird das Ende selbst gemessen.
Nach jedem Schritt wartet sie 120ms, bis das Servo steht, und mittelt den
Strom über 80ms. Solange die Fläche frei läuft, ist der Strom flach und
niedrig. Das Gestänge gilt als blockiert, wenn der Strom das 1,8-Fache des
Grundstroms im freien Lauf übersteigt und mindestens 0,15A darüber liegt. Die Suche stoppt beim ersten solchen Schritt. Als Endpunkt meldet sie
die letzte freie Pulsweite, um weitere 25µs zur Mitte zurückgenommen.

Am Prüfstand sucht der Endlagenlauf (`shared/servo/servo_limrun.c`) zuerst
die untere Seite, dann die obere. Wo, hängt vom Ziel ab:

| ZIEL | Zuerst gelesen | Suche |
| --- | --- | --- |
| ein SERVO-PWM-Pin auf OUTPUTS | nichts | PULS MITTE bis PULS MIN und PULS MAX |
| PROG-PORT, KST/CHASERVO | eine Lesesitzung der [Servoprogrammierung](ServoProgrammer-de.md) | NEUTRAL des Servos bis zu seinem eigenen PULSE LOWER und PULSE UPPER, nie darüber hinaus |

Der Abstand zu diesem Ende ersetzt die 600µs. Der Strom wird in Fenstern von 50ms am zugeordneten INA3221-Kanal
gemessen; jede Stellung dauert deshalb 250ms. Vor und nach jeder Suche
bleibt das Servo 1000ms in der Mitte. Der Rückweg von einem 600µs
entfernten Ende dauert bei 0,67µs/ms 900ms und muss vor der Messung des
Grundstroms abgeschlossen sein.

Dauer: der Host-Test begrenzt eine vollständige Suche gegen das modellierte
Servo auf unter 12s. Ein Lauf am Prüfstand ist nicht gemessen.

### Starten

Öffne die Karte ENDLAGENLAUF unter TEST auf SERVO. **OPTIONEN** öffnet
die Einstellungen des Laufs eine Ebene tiefer. Dort wählt **ENDLAGENLAUF** mit einem Tippen NUR SUCHE oder SUCHEN +
PROGRAMMIEREN. **SERVO** erscheint nur, wenn ZIEL auf PROG-PORT steht. Wähle dort mit
einem Tippen KST/CHASERVO oder HITEC; diese Servoprogrammierung übernimmt
die Sitzungen des Laufs ([Hitec-Servos](#hitec-servos)). Es beginnt bei
KST/CHASERVO und übernimmt den Programmierer, der unter PROGRAMMIEREN
zuletzt geöffnet wurde. **SERVOPULSE**, nur mit ZIEL auf PROG-PORT und
SERVO auf KST/CHASERVO angezeigt, wählt mit einem Tippen BEHALTEN oder
1000-2000; es beginnt bei BEHALTEN. Das Home-Feld führt zurück zur Karte. **ENDLAGENLAUF STARTEN** auf der Karte
startet den Lauf nach 2s Halten, wie TEST STARTEN; ein Tippen oder ein
kürzeres Halten startet nichts.

![Der Endlagenlauf, bereit](img/de/servo-limrun-ready.png)

Die Zeile unter der Schaltfläche zeigt während des gesamten Laufs den
angesteuerten Ausgang und den verwendeten Strommesskanal: `AUSGANG PROG-PORT 1 GPIO 13, STROM INA3221 0x41 CH2`, oder, solange der Tester die Standardausgänge treibt, `AUSGANG SERVO PWM GPIO 2, STROM INA3221 0x40 CH1`. Der Kanal ist der, der in SETUP > SENSORS das Ziel des Testers überwacht, der niedrigste mit Shunt auf einem bestückten Baustein; `STROM INA3221`, solange keiner es tut. Während des Laufs zeigt die linke Karte von LIVE die Phase, die Seite
und die angeforderte Pulsweite sowie den Ausgang erneut in der dritten
Zeile. Die Zeile ZIEL der
Ausgangskarte bleibt ebenfalls sichtbar.

![Der Endlagenlauf beim Programmieren](img/de/servo-limrun.png)

Der Lauf liest den INA3221 und sonst nichts. Die Messwerte des PD mini und
das Modell des Panels werden abgelehnt, bevor etwas angesteuert wird: die
Schaltfläche bleibt grau, und die Zeile darunter sagt warum.

| Zeile | Bedingung |
| --- | --- |
| `PROG 5V AUS: IM PROG-5V-FELD EINSCHALTEN` | SAFE auf dem rbmod-Carrier mit ZIEL auf einem Programmierport und dem Feld PROG 5V aus. Vor `ZUERST ARM` genannt: der Puls des Ports braucht PROG 5V und kommt nur bei SAFE ([Carrier](Carrier-de.md#der-programmierport)) |
| `ZUERST ARM` | der Prüfstand ist SAFE |
| `BERICHT WIRD NOCH GESCHRIEBEN` | ein automatischer Test oder ein Endlagenlauf läuft, oder sein Bericht geht noch auf die Karte |
| `KEIN SERVO-PWM-PIN`, `PINS NICHT AUSGELESEN` | ZIEL auf den Ausgängen des Prüfstands, ohne einen SERVO PWM zugeordneten Ausgang oder mit noch nicht ausgelesener Zuordnung: die Worte der Zeile ZIEL. Vor dem Messgerät genannt: auf dem rbmod-Carrier überwacht ein Kanal eine Schiene, und die Ausgänge liegen erst auf einer Schiene, wenn einer zugeordnet ist ([Carrier: Der Strommonitor](Carrier-de.md#der-strommonitor)) |
| `INA3221 NICHT ZUR SERVOSTROMMESSUNG EINGERICHTET` | kein Kanal eines bestückten INA3221 überwacht in SETUP > SENSORS das Ziel des Testers, an einem IOmcu älter als 4.25 überwacht CH2 oder CH3 es, ein IOmcu älter als Protokoll 4.11, oder eine SENSE-Page, die die Einrichtung nicht hält |
| `INA3221 NICHT ERREICHBAR` | kein Link, das Bauteil antwortet nicht, oder sein Reset-Zähler hat sich geändert |
| `NOCH KEINE INA3221-MESSFENSTER` | der INA3221 ist noch nicht das Messgerät der Servoschiene: seine Einrichtung ist seit weniger als 1000ms bestätigt, oder kein Fenster mit Strom in den letzten 200ms |
| `SERVOVERSORGUNG UNTER 3.0V ODER OHNE MESSWERT` | das letzte INA3221-Fenster enthält keine Schienenspannung, oder ihr Mittel liegt unter 3,0V (`SERVO_LIMRUN_RAIL_MIN_MV`). An einem ausgeschalteten Ausgang werden 0V gemessen, und eine Suche dort fände zwei freie Enden an einem Servo, das sich nie bewegt hat. 3,0V liegen unter den 4,8V, für die Servos üblicherweise angegeben sind, so dass die Schiene einer einzelnen Lithiumzelle noch startet; die Spannung, bei der ein bestimmtes Servo stehen bleibt, ist nicht gemessen |
| `GESPERRT: SERVOPROGRAMMIERUNG BELEGT DEN PORT` | ZIEL auf PROG-PORT, und eine Sitzung eines Programmers hält den Port. Vor PORT-PWM genannt: die Sitzung ist der Grund, dass kein Puls läuft, gleich was PORT-PWM sagt |
| `PORT-PWM AUS: DISARM, TIPPE AUF ZIEL, SCHALTE ES EIN` | ZIEL auf PROG-PORT mit PORT-PWM aus, der Prüfstand ARMED. ZIEL nimmt bei ARMED kein Tippen an |
| `PORT-PWM AUS: TIPPE AUF ZIEL UND SCHALTE ES EIN` | dasselbe bei SAFE, wie die Zeile nach einem DISARM lautet. ZIEL öffnet den Eintrag des Ports auf OUTPUTS, wo PORT-PWM liegt |
| `PORT-PWM EIN, WARTET AUF SAFE: DISARM, DANN ARM` | ZIEL auf PROG-PORT mit PORT-PWM ein, kein Programmer hält den Port, und sein Puls ist noch aus. Ein Puls kommt nur bei SAFE, und das Panel des rbmod-Carriers fordert ihn nur mit PROG 5V ein an: ein bei ARMED eingeschaltetes PROG 5V wartet auf das nächste SAFE |
| `MITTE MUSS ZWISCHEN PULS MIN UND PULS MAX LIEGEN` | PULS MITTE liegt nicht zwischen PULS MIN und PULS MAX |

### Die Weggrenzen des Servos programmieren

SUCHEN + PROGRAMMIEREN wird für ein Servo am Programmierport angeboten: ZIEL auf
PROG-PORT, der Port in PWM und eine Versorgung von 4,8V oder mehr. Versorgt ein PD mini den Port, muss sein Ausgang eingeschaltet sein;
geprüft wird sein Sollwert. Ohne diese Versorgung zählt die mittlere
Schienenspannung im letzten INA3221-Fenster. Auf dem rbmod-Carrier muss
zusätzlich PROG 5V eingeschaltet sein. Ein PD mini an den anderen
Servoausgängen versorgt den Programmierport nicht. An den Ausgängen des
Prüfstands sucht der Lauf nur, die Zeile lautet NUR SUCHE, und der Hinweis
darunter in den Einstellungen des Laufs lautet `PROGRAMMIEREN ERFORDERT EIN SERVO AM PROG-PORT`. Am PROG-PORT
beginnt jeder Lauf mit einer Lesesitzung, deshalb halten die übrigen
Bedingungen auch NUR SUCHE an: die Schaltfläche bleibt grau, und die
Zustandszeile der Karte nennt die Bedingung.

| Zeile | Bedingung |
| --- | --- |
| `PROGRAMMIEREN: PORT IST NICHT IM PWM-MODUS` | der Programmierer hält den Port, oder der Port ist nicht für einen Lauf bereit |
| `PROGRAMMIEREN: PD-MINI-AUSGANG AUS` | ein PD mini antwortet mit ausgeschaltetem Ausgang: die Lesesitzung schaltete ihn neben dem Netzteil ein, das die Schiene speist |
| `PROGRAMMIEREN: NETZTEIL AUF UNTER 4.8V EINGESTELLT` | ein PD mini antwortet, und sein Sollwert liegt unter 4,8V |
| `PROGRAMMIEREN: SERVOVERSORGUNG UNTER 4.8V` | kein PD mini antwortet, und die Servoschiene liegt im letzten Fenster des INA3221 unter 4,8V oder ist nicht gelesen |

Ein KST- oder Chaservo-Servo übernimmt geschriebene Werte nach einem
Aus- und Einschalten
([der Versorgungszyklus](ServoProgrammer-de.md#der-versorgungszyklus)).
Der Lauf kann dafür den PD mini oder die 5V-Versorgung der
Programmierports auf dem Carrier schalten. Ohne beides sind SUCHEN +
PROGRAMMIEREN und **SERVOPULSE** auf 1000-2000 nicht verfügbar. Bei ihrer
Auswahl erscheint
`PROGRAMMIEREN: KEIN PD MINI ZUM AUS- UND EINSCHALTEN`; der Lauf liest
das Servo nur aus und sucht. Hitec-Servos der D-Serie starten nach dem
Schreiben selbst neu und brauchen für diesen Schritt keine schaltbare
Versorgung, wie unten beschrieben.

![Kein PD mini: der Lauf schreibt nichts](img/de/servo-limrun-nopd.png)

Versorgt ein PD mini den Port, schaltet jedes VERBINDEN seinen Ausgang
1s aus und wieder ein; VERLASSEN nach einem Schreiben ebenso. Auf dem
Carrier übernimmt die feste 5V-Versorgung der Programmierports diese
Zyklen. Die Suche nach dem Schreiben prüft das Servo deshalb mit den
bereits wirksamen neuen Werten.

#### Zuerst lesen

Jeder Lauf auf PROG-PORT, auch NUR SUCHE, beginnt mit einer Lesesitzung.
Das Servo steht auf PULS MITTE, der Lauf schaltet den Prüfstand auf SAFE,
VERBINDEN liest alle Register, VERLASSEN gibt den Port zurück, und der Lauf
schaltet den Prüfstand auf ARMED. Vorher wird nichts angesteuert.

- Ein Lesen, das scheitert, beendet den Lauf vor jeder Suche: NICHT
  BESTANDEN, `kein KST/Chaservo ausgelesen`, wenn das erste VERBINDEN kein
  Servo findet, das antwortet (ein Servo, das kein KST oder Chaservo ist,
  antwortet nicht), oder das gelesene Servo Werte hält, die die harten
  Regeln nicht annehmen; ABGEBROCHEN, `PROG-PORT-Sitzung gescheitert`, wenn
  das erste VERBINDEN aus einem anderen Grund scheitert oder sein Lesen
  nicht abschließt. Ein Servo, das kein KST oder Chaservo ist, wird mit
  ZIEL auf einem SERVO-PWM-Pin gesucht. Jedes spätere VERBINDEN, ein
  VERLASSEN oder ein Schritt, der nicht abschließt, beendet den Lauf als
  ABGEBROCHEN mit demselben Grund.
- NEUTRAL nicht zwischen PULSE LOWER und PULSE UPPER beendet ihn als NICHT
  BESTANDEN, `NEUTRAL außerhalb LOWER/UPPER`.
- PULSE LOWER bis PULSE UPPER nicht innerhalb von PULS MIN bis PULS MAX
  des Prüfstands beendet ihn als NICHT BESTANDEN, `Servopulse außerhalb
  MIN/MAX`. Der Prüfstand bleibt SAFE, nichts wird angesteuert, und Feld
  und Bericht nennen beide Bereiche, zum Beispiel `900-2100 NICHT IN
  1000-2000us`.

Ein KST-Servo im Lieferzustand hält PULSE LOWER 900,0µs und PULSE UPPER
2100,0µs. PULS MIN und PULS MAX des PWM-Typs STANDARD sind 1000µs und
2000µs, also wird dieses Servo bei BEHALTEN abgelehnt. SERVOPULSE auf
1000-2000, oder ein PWM-Typ, dessen Bereich 900 bis 2100µs enthält, lässt
es laufen.

SERVOPULSE auf 1000-2000 schreibt in der Lesesitzung PULSE LOWER 1000,0µs
und PULSE UPPER 2000,0µs, bevor etwas angesteuert wird, und liest sie
zurück. NEUTRAL wird nicht geschrieben. Ein Servo, das diese Werte schon
hält, bekommt kein Schreiben. NEUTRAL außerhalb des neuen Bereichs beendet
den Lauf als NICHT BESTANDEN, bevor etwas angesteuert wird, mit
ZURÜCKSPIELEN im Angebot. Die Einstellung wirkt mit NUR SUCHE und mit
SUCHEN + PROGRAMMIEREN; mit NUR SUCHE sind die Pulse das einzige Schreiben, und der
Hinweis in den Einstellungen des Laufs sagt `NUR SUCHE: SETZT NUR PULSE LOWER/UPPER`.

#### Die Register und das Modell

LEFT ANGLE und RIGHT ANGLE sind die eigenen Weggrenzen des Servos, je ein
8-Bit-Register, in einer unbekannten Einheit. Der kleinste zulässige Wert ist bei einem KST 1; die harten Grenzen der
Servoprogrammierung schließen 0 aus. Ein Wert unter 50 ist eine weiche Grenze; das Halten von 2s
beim Start ist die Zustimmung dazu.

Der Lauf setzt einen linearen Zusammenhang zwischen Pulsweite und
Servoweg voraus. PULSE LOWER und PULSE UPPER sind die
Eingangspulse, bei denen das Servo den vollen Weg erreicht, den LEFT ANGLE
und RIGHT ANGLE setzen. Zwischen NEUTRAL und diesem Puls ist der Weg linear
im Puls; darüber hinaus hält das Servo den vollen Weg. Der Anteil einer
Seite beim Puls p ist

```text
f = (p - NEUTRAL) / (P_lim - NEUTRAL)
```

mit P_lim gleich PULSE LOWER auf der unteren und PULSE UPPER auf der oberen
Seite, so wie das Servo sie während der Suche hält. Die Einheit von LEFT
ANGLE und RIGHT ANGLE ist unbekannt, und der lineare Zusammenhang ist eine
Annahme, keine Messung. Eine blockierte Seite, deren letzter freier Puls
nicht jenseits von NEUTRAL liegt, beendet den Lauf vor jedem Schreiben als
NICHT BESTANDEN, `freies Ende vor NEUTRAL`.

#### Zwei Schreibvorgänge je blockierter Seite

v0 ist der Wert, den das Register der Seite beim Lesen hält.

1. Der Vorhalt: `v1 = ceil(v0 x fb x 1.03)`, fb der Anteil am ersten
   blockierten Puls der ersten Suche, höchstens v0 und mindestens der
   kleinste Wert. Der erste blockierte Puls liegt am Anschlag oder
   dahinter, also bleibt der Anschlag bei v1 erreichbar und rückt nahe an
   die Pulsgrenze. Die Seite wird erneut gesucht. Ist sie dort frei, endet
   der Lauf als NICHT BESTANDEN, `kein Anschlag nach dem Vorhalt`, ohne
   zweites Schreiben und mit ZURÜCKSPIELEN im Angebot.
2. Der Endwert: `v2 = floor(v1 x f1)`, f1 der Anteil am letzten freien Puls
   der Vorhalt-Suche, mindestens der kleinste Wert. Kein Abstand kommt
   hinzu: der letzte freie Puls liegt einen Schritt von 10µs vor dem
   blockierten. Eine Vorhalt-Suche, die weit innerhalb der Pulsgrenze
   anläuft, ergibt v2 ebenso; kein weiterer Vorhalt folgt.

Durchgerechnet am modellierten Servo des Hosts: NEUTRAL 1500,2µs, PULSE
LOWER 900,0µs, v0 150, der Anschlag bei 1000µs. Die erste Suche ist frei
bei 1000µs und läuft bei 990µs an: fb = 510,2 / 600,2 = 0,850, v1 =
ceil(150 x 0,850 x 1,03) = 132. Bei 132 ist die Suche frei bis 930µs:
f1 = 570,2 / 600,2 = 0,950, v2 = floor(132 x 0,950) = 125. Bei 125 endet
der volle Weg bei 1000,2µs.

Der Vorhalt entfällt, wo v1 nicht unter v0 läge, oder wo das Modell den
Anschlag um weniger als 20µs verschiebt: d0 x (v0 - v1) / v1 unter 20µs,
d0 der Abstand des letzten freien Pulses der ersten Suche von NEUTRAL. Eine Suche könnte eine so kleine Verschiebung nicht von einer
unveränderten Endlage unterscheiden. Die erste
Suche zählt dann als die des Vorhalts, und `v2 = floor(v0 x f0)`, f0 der
Anteil an ihrem letzten freien Puls: ein Schreibvorgang für diese Seite.

#### Welches Register welche Seite begrenzt

Die Protokollnotizen sagen nicht, welche Pulsseite jedes Register begrenzt,
und nicht, wie REVERSION das ändert. Der Lauf findet es beim ersten
Schreiben der ersten blockierten Seite: dieser Wert geht nur an LEFT ANGLE
(an RIGHT ANGLE, wo LEFT ANGLE schon den kleinsten Wert hat), und die Seite
wird gesucht. Das Ende hat sich bewegt, wenn es frei ist oder sein
Endpunkt 20µs weiter außen liegt. Ein Ende, das innerhalb von 20µs blieb,
schickt LEFT ANGLE auf seinen Wert zurück und den aus dem v0 von RIGHT
ANGLE neu berechneten Wert an RIGHT ANGLE; bewegt es sich dann, begrenzt
RIGHT ANGLE diese Seite. Bleibt das Ende bei beiden versuchten Registern unverändert, endet der
Lauf als NICHT BESTANDEN, `LEFT/RIGHT ANGLE: Seite
unklar`. Dasselbe gilt, wenn sich das Ende um 20µs oder mehr nach innen bewegt.
Blockiert eine Seite noch beim kleinsten Registerwert, endet der Lauf
als NICHT BESTANDEN, `blockiert beim kleinsten Winkel`. Ein Register, dessen Seite nie blockierte, bleibt unverändert.

Zuletzt werden beide Enden noch einmal gesucht: BESTANDEN, wenn beide frei
sind, NICHT BESTANDEN `Endlage blockiert weiterhin`, wenn eines anläuft.
Nach NICHT BESTANDEN folgt kein weiteres Schreiben. Mit ZURÜCKSPIELEN
kannst du die Werte vor dem Lauf wiederherstellen.

#### Sitzungen

Der Lauf schreibt in Sitzungen des [Programmierers](ServoProgrammer-de.md):

1. Das Servo geht in die Mitte zurück und wird dort 1000ms gehalten.
2. Der Lauf schaltet den Prüfstand auf SAFE.
3. VERBINDEN nimmt den Port und liest alle Register.
4. Das Schreiben: SCHREIBEN INS SERVO FREIGEBEN an, die Felder des Laufs,
   der Schreibschalter wieder aus. Es ist das gewöhnliche Schreiben des
   Programmierers mit jeder Grenzprüfung, und es liest die Werte zurück.
5. VERLASSEN gibt den Port an PWM zurück.
6. Der Lauf schaltet den Prüfstand auf ARMED und sucht die Seite, für die
   geschrieben wurde.

Die Lesesitzung schreibt nichts außer den Pulsen von SERVOPULSE. Ein
VERLASSEN, das dieser Link oder diese Versorgung nicht abschließt, beendet
den Lauf, bevor sich ein Wert ändert. Das erste Schreiben des Laufs sichert
die Werte des Servos in eine Datei auf der Karte, wie das erste Schreiben
des Programmierers; die späteren Sitzungen sichern nicht. Jede spätere
Sitzung liest das Servo erneut. Ein anderer Typ, umgeschaltete REVERSION,
ein anderer Bereich der Werte, die die harten Regeln annehmen, andere
Pulse, oder LEFT ANGLE und RIGHT ANGLE anders, als der Lauf sie zuletzt
schrieb, beenden den Lauf als ABGEBROCHEN, `Servo hat andere Werte`, bevor
er wieder schreibt.

| Konstante | Wert | Zweck |
| --- | ---: | --- |
| `SERVO_LIMRUN_WRITES_MAX` | 8 | Registerwerte, die ein Lauf höchstens schreibt: PULSE LOWER und PULSE UPPER (2), Vorhalt und Endwert auf jeder Seite (4), und das erste Schreiben von LEFT ANGLE mit seinem Wert zurück, wenn RIGHT ANGLE diese Seite begrenzt (2). Eine Sitzung, deren Werte darüber gingen, wird nicht gesendet: NICHT BESTANDEN `Grenze von 8 Werten erreicht` |
| `SERVO_LIMRUN_OVERSET_PCT` | 3% | der Vorhalt über dem Anteil am ersten blockierten Puls |
| `SERVO_LIMRUN_STD_LOWER_DUS`, `SERVO_LIMRUN_STD_UPPER_DUS` | 1000,0µs, 2000,0µs | was SERVOPULSE auf 1000-2000 schreibt |
| `SERVO_LIMRUN_MOVED_US` | 20µs | ein Ende, das sich bewegt hat oder blieb; die kleinste Verschiebung des Anschlags, für die ein Vorhalt geschrieben wird |
| `SERVO_LIMRUN_RETURN_MS` | 1000ms | in der Mitte vor jeder Suche und vor jedem SAFE |

Ein eigener Protokollschritt zum dauerhaften Speichern ist nicht bekannt;
ebenso wenig ein Schreibbefehl, der nur das RAM ändert. Jeder Schreibvorgang
schreibt ein Register und liest es zur Kontrolle zurück. Ob ein KST-Servo den
Wert bei jedem Schreiben im EEPROM (elektrisch löschbarer programmierbarer
Nur-Lese-Speicher) ablegt und wie viele Schreibvorgänge dieser Speicher
verträgt, ist unbekannt; die Grenze hält einen Lauf bei 8 Werten. Ein Lauf
mit beiden Enden blockiert und SERVOPULSE auf 1000-2000 schrieb am
modellierten Servo des Hosts 6 Werte in 4 Schreibvorgängen und 4
Sitzungen, 85,6s, mit REVERSION AUS, und 8 Werte in 5 Schreibvorgängen
und 5 Sitzungen, 97,7s, mit REVERSION EIN.

Die gefundenen Enden werden nicht auf PULS MIN und PULS MAX des
Prüfstands angewendet.

### Hitec-Servos

Mit SERVO auf HITEC übernimmt die
[Hitec-Servoprogrammierung](HitecProgrammer-de.md#läufe) die Sitzungen des
Laufs. Verwendet wird die unter SERIE gewählte Familie, D-SERIE oder
KLASSISCH. Suche, Vorhalt, Zuordnung
und Prüfung sind die oben beschriebenen.

![Die Optionen des ENDLAGENLAUFS, SERVO auf HITEC](img/de/servo-limrun-hitec.png)

Was anders ist:

| | D-SERIE | KLASSISCH |
| --- | --- | --- |
| LINKS, RECHTS | LEFT END (0xB2), RIGHT END (0xB0) | LEFT END und RIGHT END: Zelle 0x14 und Zelle 0x13 an einem CW-Servo, an einem CCW-Servo vertauscht |
| Einheit | Zählwerte ab NEUTRAL 0xC2: 0xC2 - 0xB2 und 0xB0 - 0xC2; 74,5 je Grad an einem Standardservo | die rohe Zelle, 0 bis 255, Einheit unbekannt |
| Bereich eines Schreibens | 1 Zählwert bis zum kleineren von 0xC2 - 1 und 16383 - 0xC2 | 0 bis 255 |
| Pulse: NEUTRAL, voller Weg | 1500,0us; 850,0us und 2150,0us: das Zielmodell aus `hitec_live.h`, t = (us - 750) x 4, Ziele 400 und 5600 | 1500,0us; 900,0us und 2100,0us, der PWM-Bereich von Hitec |
| Ein Schreiben | die Register im Abstand von 20ms, das Speichern (0x70), 100ms, der Neustart (0x46), 1000ms, das Zurücklesen | jede Zelle mit ihrem Ausgleich in 0x1C, das Zurücklesen von 0x00 bis 0x23 |
| Versorgung | wie sie ist: die D-Serie übernimmt ein Schreiben nach ihrem eigenen Neustart | ein PD mini, oder PROG 5V am rbmod-Carrier, für jede Sitzung, auch das Lesen: Einstieg und VERLASSEN schalten die Versorgung aus und ein |

- Ein Hitec-Servo hat keine Pulsregister. Der Lauf nimmt die Pulse der
  Tabelle, die nicht gemessen sind, und sucht bis zu ihnen: PULS MIN und
  PULS MAX müssen sie enthalten, sonst bleibt ENDLAGENLAUF STARTEN grau mit
  `HITEC: PULS MIN/MAX MUSS 850-2150us ABDECKEN` (KLASSISCH: `900-2100us`).
  Der TYP WEIT, 800 bis 2200us, enthält beide. SERVOPULSE wird nicht
  angezeigt.
- KLASSISCH ohne PD mini oder PROG 5V hält die Taste grau mit
  `Klassische Servos brauchen ein PD mini: Der Prüfstand schaltet die
  Versorgung.`
- Die 2s Halten von ENDLAGENLAUF STARTEN sind die Zustimmung, die ÄNDERUNG
  PRÜFEN verlangt: die Schreibvorgänge des Laufs zeigen keine Seite, und
  jeder schaltet SCHREIBEN INS SERVO FREIGEBEN für sich ein und an seinem
  Ende aus. Ein Wert außerhalb des Bereichs wird abgelehnt, nicht
  begrenzt.
- Ein erstes VERBINDEN, auf das kein Servo antwortet, oder ein Servo, dessen
  Endpoints sich nicht ändern lassen, beendet den Lauf als NICHT BESTANDEN
  mit `kein Hitec-Servo ausgelesen`.
- Die Backup-Datei HITEC001 bis HITEC999 wird vor dem ersten Schreiben
  des Laufs gespeichert. Lade sie zum Zurückspielen unter DATEIEN der
  Hitec-Servoprogrammierung und schreibe sie mit n WERTE SCHREIBEN. Das
  Feld nennt sie, `HITEC001 IN DATEIEN LADEN`, oder `KEINE BACKUP-DATEI`, wenn keine gespeichert ist: ohne Karte, oder der Lauf
  endete, bevor die Sitzung des ersten Schreibens eine speicherte.
- Der Bericht nennt die Register, `LEFT END (0xB2): 4841 vorher, 2341
  nachher`, das Servo als `Hitec D SERIES, DIRECTION CW` und die Pulse als
  Pulsmodell.

Am modellierten D485HW des Hosts, beide Enden blockiert, schrieb ein Lauf
4 Werte in 3 Schreibvorgängen und 4 Sitzungen, 107,9s. Welche Hornstellung
ein Endpoint der D-Serie an einem CCW-Servo ergibt, welche klassische
Zelle welche Seite begrenzt, die Einheit der klassischen Zelle und ob der
Weg beider Serien linear von der Pulsweite abhängt, ist nicht gemessen.
Der Zuordnungsschritt ermittelt das Register jeder Seite wie bei einem
KST-Servo,
und ein NICHT BESTANDEN mit `LEFT/RIGHT ANGLE: Seite unklar` (die
KST-Wörter, auch für LEFT END und RIGHT END) oder `Endlage blockiert weiterhin` beendet einen Lauf, dessen Servo dem Modell nicht folgt.

### Schutzmechanismen

| Schutz | Wert | Wirkung |
| --- | ---: | --- |
| Stromobergrenze | 3,0A | Abbruch, bei jedem 1-ms-Messwert eines Fensters geprüft |
| Der INA3221 an seiner Messgrenze | ein abgeschnittener Messwert | Abbruch |
| Stall-Timeout | Fenstermittel über 1,0A für 400ms | Abbruch |
| Kein Fenster | 200ms, 4 Fenster zu 50ms | Abbruch; `SERVO_TEST_WIN_STALE_MS`, der Wert des automatischen Tests. Ein Fenster ohne Strommesswert zählt nicht als Fenster. Die drei Schutzmechanismen darüber verwenden diese Fenster; für eine Lücke gilt deshalb keine zusätzliche Toleranzzeit |
| Der AS5600, ab dem Start in Gebrauch | länger als 300ms keine Antwort oder eine erneute Lücke innerhalb von weniger als 10s seit Beginn der vorherigen | Abbruch als Fehler, `AS5600 antwortet nicht` oder `AS5600 2x in 10s ausgefallen`, mit WINKELQUELLE IMU `IMU antwortet nicht` oder `IMU 2x in 10s ausgefallen`; bei einer kürzeren, selteneren Lücke läuft der Test weiter, ohne dabei Winkelwerte zu übernehmen, wie im automatischen Test ([Der Ausgangsencoder](#der-ausgangsencoder)). Der Bericht hat die Zeile `AS5600-Aussetzer: N, längster Mms. ...`, wenn N größer als 0 ist |
| Eine Suche | 40s | Abbruch: 1000µs Weg sind 100 Stellungen zu 250ms, 25s |
| Ein Schritt des Programmierers | 150s | Abbruch: VERBINDEN mit seinem Lesen, ein Schreiben oder VERLASSEN |
| Das Umschalten des Prüfstands | 3s | Abbruch, wenn SAFE oder ARMED 3s nach der Anforderung nicht steht |
| Das Netzteil | Sollwerte und Ausgang, ein oder aus, wie bei seiner ersten Messung, ob es antwortet oder nicht, in jeder Phase außer den Sitzungen des Programmierers (VERBINDEN bis VERLASSEN), denen das Netzteil gehört; ein Ausgang, der beim Start aus ist und eingeschaltet wird, oder ein Netzteil, das beim Start nicht antwortet und später antwortet, mit ein- oder ausgeschaltetem Ausgang, beendet den Lauf als `Netzteil oder Spannung geändert`, so wird neben einer Schiene, die anderswo gespeist wird, kein zweites Netzteil eingeschaltet; ein VERBINDEN wird nicht gesendet, solange ein PD mini antwortet, mit dem der Lauf nicht begonnen hat, oder dessen Ausgang aus ist: der Lauf endet, wie eine Messung außerhalb einer Sitzung ihn beendet, mit `Netzteil oder Spannung geändert` für einen PD mini, mit dem der Lauf nicht begonnen hat, und mit `Netzteilausgang abgeschaltet` für seinen eigenen; während einer Suche zudem die Schienenspannung innerhalb 1,0V des ersten Fensters | Abbruch |
| Die Schienenspannung im Fenster einer Suche | in jedem Fenster einer Suche vorhanden, ihr Mittel bei 3,0V oder mehr (`SERVO_LIMRUN_RAIL_MIN_MV`), das erste Fenster eingeschlossen | Abbruch, bevor das Fenster die Suche bewegt: `kein Spannungswert im Fenster` ohne Wert, `Netzteil oder Spannung geändert` unter 3,0V; Bezug des 1,0-V-Bands ist das erste Fenster des Laufs, das die Spannung enthält |
| Schrittweite | 10µs | begrenzt die Änderung der angeforderten Pulsweite je Schritt; wie stark das Horn dabei gegen einen Anschlag drückt, hängt vom Einbau ab |

Sie gelten in jeder Phase. Suche und Schutzmechanismen verwenden den
Betrag des Stroms. Bei umgekehrtem Shunt wird der Strom eines
blockierenden Servos stärker negativ; die Blockierung wird trotzdem
wie bei richtig eingebautem Shunt erkannt. Ein Messwert von -32768mA zählt als
32768mA, über der Grenze von 3,0A. Der Lauf folgt den Regeln für ARMED des
SERVO-Bildschirms und des Programmierports: sein ARM wird abgelehnt,
solange der Port gehalten wird, eine STOP-Sperre oder verlorene
Touch-Ereignisse halten den Prüfstand SAFE, und ein ARM, das nicht ankommt,
beendet den Lauf.

Der Abgleich ist ein Modul in `shared/servo`, auf dem Host gegen ein
modelliertes Servo getestet; keines der beiden Firmware-Images ruft es auf.

### Was einen Endlagenlauf beendet

STOP, LAUF BEENDEN im Feld auf LIVE oder auf der Karte ENDLAGENLAUF, ein Umschalten auf
SAFE durch den Bediener, Linkverlust, das Verlassen des Bildschirms, die manuelle Übernahme des Servos, eine Änderung an den Einstellungen des
Servos, verlorene Touch-Ereignisse, der INA3221, der aufhört, das Messgerät zu sein,
und ein Netzteil, das aufhört zu antworten, auslöst, ausschaltet oder
dessen Sollwerte sich ändern: die Gründe des automatischen Tests, in
[Was einen Lauf beendet](#was-einen-lauf-beendet). SAFE und ARM des Laufs
selbst beenden nichts. Jeder Grund beendet den Lauf als ABGEBROCHEN, mit
ausgeschaltetem Ausgang und freigegebenem Servo. Ein Fehler während einer
Suche, einer Probe oder der Prüfung schaltet den Prüfstand zudem auf SAFE:
ein Wert an der Grenze von 3,0A, ein abgeschnittener Wert, 1,0A für 400ms,
ein Fenster ohne Spannung der Schiene und die Fehler des automatischen Tests,
darunter die beiden des AS5600 ([Sicherheit, Fehler](Safety-de.md#fehler)).
In einer Sitzung des Programmierers ist der Prüfstand schon SAFE, und eine
Lücke des AS5600, die den Lauf dort beendet, fordert kein SAFE an. Ein Lauf, der endet,
während er auf ARMED wartet, fordert zudem SAFE an: ein ARM, das er
anforderte und das der Prüfstand noch nicht übernommen hat, schaltet den
Prüfstand nach dem Ende nicht auf ARMED. Ein Start des Panels löscht den
Lauf ohne Bericht.

Die zuletzt geschriebenen Werte bleiben im Servo. Ein Lauf, der schrieb und
nicht bestand, zeigt in seinem Feld `ZURÜCKSPIELEN IN PROGRAMMIEREN`, an
einem Hitec-Servo `HITEC001 IN DATEIEN LADEN` ([Hitec-Servos](#hitec-servos)). Ein
Schreiben, das nach dem Senden scheitert, ohne Antwort oder mit einer
Antwort, die der Programmierer nicht annimmt, gilt als geschrieben: ein Teil
davon kann angekommen sein. Ebenso ein Schreiben, das dem Programmierer
übergeben und beim Ende des Laufs nicht beantwortet ist, auch nach einem
STOP in seiner Sitzung; die Zeile `Backup:` nennt dann die Datei, die diese
Sitzung davor gespeichert hat. Ein Schreiben, das der Programmierer vor dem
Senden ablehnt, gilt als keines.
ZURÜCKSPIELEN im Programmierer bietet nach VERBINDEN und ALLES AUSLESEN `Werte vor dem Endlagenlauf.` an: das Servo, wie die erste Sitzung des Laufs es las,
zurückgeschrieben als gewöhnliches Schreiben von LEFT ANGLE, RIGHT ANGLE,
PULSE LOWER und PULSE UPPER. Das Angebot steht, solange das gelesene Servo
sich darin in diesen vier Feldern und in keinem anderen unterscheidet. Diese Werte liegen im Arbeitsspeicher des Panels und gehen bei einem
Neustart verloren. Die Backup-Datei auf der Karte bleibt erhalten.
Eine Sitzung nach dem Lauf ist unabhängig davon; ihr erstes Schreiben
legt eine eigene Backup-Datei an.

![Ein Lauf, nach einem Schreiben beendet](img/de/servo-limrun-stopped.png)
![ZURÜCKSPIELEN nach einem Endlagenlauf](img/de/servo-limrun-restore.png)

### Ergebnisse

- Ein Endpunkt wird bereits um 25µs zurückgenommen gemeldet.
- „Keine Endlage gefunden“ (`FREI`) heißt: die Suche hat das Ende ihres
  Wegs gemessen, ohne dass das Gestänge blockiert ist: PULS MIN oder PULS
  MAX an einem SERVO-PWM-Pin, PULSE LOWER oder PULSE UPPER des Servos am
  PROG-PORT.
- Eine Endlage nahe der Mitte kann eine Schwergängigkeit im Gestänge sein,
  etwa ein klemmender Umlenkhebel oder eine streifende Schubstange, statt des
  Endes des Ruderwegs.

| Urteil | Bedeutung |
| --- | --- |
| GEFUNDEN | nur Suche: eine Endlage blockiert; die Endpunkte werden gemeldet |
| FREI | keine Endlage blockiert; nichts wird geschrieben |
| BESTANDEN | programmiert, und beide Enden in der letzten Suche frei |
| NICHT BESTANDEN | der Lauf endete wie geplant und eine Endlage blockiert weiterhin, oder das Servo lässt sich nicht programmieren |
| ABGEBROCHEN | ein Schutz, der Bediener, der Link, das Messgerät, das Netzteil oder der Programmierer hat ihn beendet |

Das Feld auf der linken Karte zeigt bis SCHLIESSEN das Urteil, den Grund
oder die Werte (`LINKS 150>87  RECHTS 150>95`), die Enden oder den Ausgang,
an dem der Lauf begann, und den Dateinamen des Berichts.

![Ein programmiertes Servo](img/de/servo-limrun-pass.png)
![Nur Suche](img/de/servo-limrun-found.png)
![Die Pulse des Servos außerhalb von PULS MIN und PULS MAX](img/de/servo-limrun-range.png)

Der Bericht ist eine eigene `LIMITnnn.TXT` in `USER/LOGS` ohne CSV-Datei
daneben, unter der nächsten freien Nummer dieses Ordners, geschrieben
unabhängig davon, was BERICHT auf PRÜFLING sagt. Mit Testaufzeichnungen AUS unter
SETUP > SD CARD wird kein Bericht geschrieben
([Setup](Screens-de.md#sd-card-welche-logs-geschrieben-werden-und-wohin)). Während eines Laufs schreiben weder die Prüfstands- noch die
Netzteilaufzeichnung eine Zeile. Die Laufzeit und Summen der
Netzteilaufzeichnung werden weitergezählt. Vom Start bis zum Schließen
der Berichtsdatei auf der Karte gilt der Lauf als offener Aufzeichnungslauf: WLAN und ein Update warten
darauf, wie auf die CSV eines Laufs. Ein Lauf am modellierten
Servo des Hosts mit REVERSION AUS und SERVOPULSE auf 1000-2000; die
Anschläge bei 1150µs und 1880µs Hornweg liegen bei 1208µs und 1817µs
Eingangspuls, sobald PULSE LOWER und PULSE UPPER 1000,0µs und 2000,0µs
sind:

```text
RCBENCH SERVO-ENDLAGEN-BERICHT
Ergebnis:        BESTANDEN
Prüfling:        SERVO
Firmware:        rcbench 0.23.9
Modus:           Suche, dann LEFT ANGLE und RIGHT ANGLE programmiert
Ausgang:         PROG-PORT 1 GPIO 13, 50Hz
Pulsbereich:     Mitte 1500us, 1000-2000us
Gesucht:         von 1500us bis 1000us und 2000us
Strom:           INA3221 0x40 CH1, Fenster zu 50ms, Shunt 100.0mΩ
Suchregel:       Schritte zu 10us, 120ms warten, Mittel über 80ms; blockiert über 1.8 x frei und +0.15A
Schutz:          ein Messwert ab 3.0A, über 1.0A für 400ms, 200ms kein Fenster, 40s je Suche
Dauer:           85.6s
AS5600:          nicht eingebaut, keine Winkel

ENDEN (die Suche vor dem Programmieren)
Unteres Ende:    blockiert bei 1200us, Endpunkt 1235us; frei 0.120A, blockiert 0.627A
Oberes Ende:     blockiert bei 1820us, Endpunkt 1785us; frei 0.120A, blockiert 0.322A

PROGRAMMIEREN (LEFT ANGLE und RIGHT ANGLE, Einheit unbekannt)
Servo:           20k_sel 1, REVERSION AUS
Servopulse:      NEUTRAL 1500.2us, PULSE LOWER 900.0us, PULSE UPPER 2100.0us
Pulse schreiben: PULSE LOWER 900.0 -> 1000.0us, PULSE UPPER 2100.0 -> 2000.0us
LEFT ANGLE:      150 vorher, 87 nachher; begrenzt das untere Ende
                 Anteil frei 0.580, blockiert 0.600; Vorhalt 93, Anteil 0.940; Endwert 87
RIGHT ANGLE:     150 vorher, 95 nachher; begrenzt das obere Ende
                 Anteil frei 0.620, blockiert 0.640; Vorhalt 99, Anteil 0.960; Endwert 95
Verfahren:       linear im Puls; Vorhalt +3% auf den Anteil am ersten blockierten Puls, dann der Anteil, den seine Suche frei fand
Schreibvorgänge: Werte geschrieben 6, Schreibvorgänge gesendet 4, Grenze 8 Werte; Sitzungen 4
Backup:          KST001, die Werte vor dem Lauf

ENDEN NACH DEM PROGRAMMIEREN (beide neu gesucht)
Unteres Ende:    frei bis 1000us; frei 0.120A
Oberes Ende:     frei bis 2000us; frei 0.120A

Einheit: Die Einheit von LEFT ANGLE und RIGHT ANGLE ist unbekannt. Der Weg wird als linear zur Pulsbreite von NEUTRAL bis PULSE LOWER und PULSE UPPER angenommen. Das ist nicht gemessen.
Prüfstand: die gefundenen Enden werden nicht auf PULS MIN und PULS MAX angewendet.
```

Mit eingebautem AS5600 hat jedes Ende eine Zeile mit dem Winkel des Horns
an seiner letzten freien Stellung, `AS5600 +38.5° an der letzten freien
Stellung`. Ein Lauf, der schrieb und nicht bestand, hat eine Zeile
`Zurückspielen:`. Ein Lauf, der kein Schreiben sendete, hat keine Zeile
`Backup:`: die Werte vor dem Lauf liegen ab dem ersten Schreiben vor.
Die Zeile `Backup:` nennt die Datei, die das erste Schreiben dieses
Laufs sicherte, auch wenn dieses Schreiben gesendet wurde und verloren ging,
nie die Datei eines früheren Laufs. Die Zeile `Gesucht:` nennt Mitte und Enden der Suche; ein
Lauf, der vor seiner ersten Suche endete, hat keine. Ein Lauf an einem
SERVO-PWM-Pin hat keine Zeile `Servopulse:`.

Der vollständige Endlagenlauf und die Zahlen oben wurden nicht an einem
echten Servo gemessen. Unbekannt bleiben die Einheit von LEFT ANGLE und
RIGHT ANGLE, ihre Zuordnung zu den Pulsseiten an einem echten KST- oder
Chaservo-Servo, Bewegungen beim Schreiben einer Grenze, die Dauer einer
Sitzung an der Hardware und die Zeit von VERLASSEN bis zu einem wirksamen
ARM. Bei den Versuchen mit der Servoprogrammierung an einem Chaservo
überstanden geschriebene Werte einen Versorgungszyklus. Die Persistenz
bei KST-Servos und über wiederholte Zyklen ist nicht gemessen.

## Zwei Servos auf einer Ruderfläche abgleichen

### Zweck

Zwei Servos an einer Fläche (Doppelquerruder, geteiltes Höhenruder) arbeiten
gegeneinander, sobald ihr Weg oder ihre Mitte nicht übereinstimmen, und ziehen
dafür dauerhaft zusätzlichen Strom. Die Fläche zeigt kein sichtbares Zeichen.

### Verfahren

Die beiden hören am Punkt des kleinsten Gesamtstroms auf, gegeneinander zu
arbeiten. Die Suche hält Servo A fest und fährt eine Korrektur an Servo B
durch, in der Mitte und bei ±300µs Ausschlag:

- ein Unterschied in der Mitte ist ein Offsetfehler (Trimmung);
- ein Unterschied an einem Ende ist ein Wegfehler für dieses Ende.

Jede Stufe führt 3 Suchläufe mit je 7 Punkten aus. Der erste Suchlauf deckt
±40µs um den Startpunkt der Stufe ab: 0µs bei der Mittenstufe, der Ausschlag
von ±300µs bei den Endstufen. Jeder spätere Suchlauf ist auf den besten Punkt
des vorigen zentriert und deckt ±13µs, dann ±4µs ab. Jeder Punkt wartet 120ms plus die Fahrzeit bei angenommenen
0,8µs/ms und mittelt den Strom dann über 100ms. Ein Minimum gilt, wenn der
Strom über den Suchlauf um mindestens 0,08A schwankt. Die Stromobergrenze
für das Paar liegt bei 4,0A.

Jedes Ende bekommt seine eigene Korrektur; ein Gestänge mit Horn und
Schubstange ist nicht symmetrisch zur Mitte. Ein Sensor für den gemeinsamen Strom beider Servos genügt.

### Ergebnisse

- Ein Paar, das bereits abgeglichen ist, liefert ein Minimum bei Korrektur
  null.
- „Kein Minimum“ heißt: der Strom schwankt über den breitesten Suchlauf um
  weniger als 0,08A.
- Zwei Servos, die sich einig sind und beide falsch stehen, erzeugen keinen
  Stromunterschied. Dieser Fall braucht den Beschleunigungssensor oder eine
  Sichtprüfung.

## Automatischer Test

### Zweck

Der automatische Test misst den Strom in Ruhe, während der Bewegung
und beim Halten einer Endstellung. Er schätzt die Stellzeit bei den für
das Servo vorgesehenen Spannungen und sucht die Spannung, bei der keine
Bewegung mehr erkennbar ist (Brown-out). Ohne eingeschaltete Winkelquelle
(siehe unten) misst er keine Hornposition. Bewegung wird aus dem Strom
des Netzteils abgeleitet, das das Servo versorgt: am PD mini (WeAct PD
Power Mini V1), wenn der Tab PD MINI auf SUPPLY ihn freigibt, sonst am Netzteilmodell
des Panels. Ein Lauf am Modell ist im Bericht als Simulation gekennzeichnet; seine
Messwerte sind simuliert. Ist der INA3221 bei TEST STARTEN das Messgerät der
Servo-Schiene und das Netzteil der PD mini, liest der Lauf stattdessen den
INA3221-Kanal, der das Ziel des Testers überwacht, in 50-ms-Fenstern: siehe
[Der Strommesser des Laufs](#der-strommesser-des-laufs).

Starte den Test mit TEST STARTEN auf der Karte AUTOMATISCHER TEST unter
TEST auf SERVO. Die [Bildschirmbeschreibung](Screens-de.md#servo) erklärt
die Bedienung. Der Ablauf ist in `shared/servo/servo_test.c` implementiert;
die unten genannten Konstanten stehen in `servo_test.h`.

### Verfahren

Die Spannungsstufen sind STUFE 4.8V und STUFE 6.0V des Laufs (OPTIONEN der Karte), und mit HV-SERVO an STUFE 7.4V und STUFE 8.4V. HV-SERVO ist nach jedem Neustart aus. Ein
Lauf mit einer Stufe über 6,0V startet nur über die Warnung NUR HV-SERVOS,
2s gehalten. Die Strombegrenzung im Lauf ist die der Zeile SOLL.

Jede Stufe durchläuft diese Phasen:

| Phase | Das Servo | Endet |
| --- | --- | --- |
| SETZEN | auf PULS MITTE; die Spannung wird verlangt; die erste Stufe schaltet den Ausgang ein | wenn das Netzteil den Sollwert auf 0,05V genau zurückliest, spätestens nach 3000ms |
| EINSCHWINGEN | auf PULS MITTE | nach EINSCHWINGZEIT des Laufs |
| RUHE | auf PULS MITTE: der Ruhestrom ist der Mittelwert der Messwerte, das Ruherauschen ihre Standardabweichung | nach 1000ms |
| BEWEGEN | ein Sprung auf ein Ende, ohne Rampe: TEMPO gilt nicht | bei der Ankunft oder nach 3000ms plus der Verzögerung des Strommessers (3300ms am PD mini): verspätet mit erkannter Bewegung, unerkannt ohne |
| HOLD | an diesem Ende: der Haltestrom ist der Mittelwert der Messwerte | nach HALTEZEIT, mindestens 600ms |

Die beiden Enden sind die von SWEEP: BEREICH des Wegs zu beiden Seiten von
PULS MITTE, ohne Trim. Mit den Vorgaben (BEREICH 80%, WEG +/-90 Grad,
PULS MIN 1000us, MAX 2000us) sind das 1100 und 1900us. KURVE und TEMPO
gelten nur für SWEEP.

Auch nach einem vollständig abgeschlossenen Lauf wird der Ausgang
ausgeschaltet und das Servo zur Mitte freigegeben. Der Prüfstand bleibt
ARMED. Die angezeigte Sollposition ist dann wie nach FREIGEBEN die
Ruhelage: bei STANDARD PWM 1500us, unabhängig von TRIMMUNG. Die nächste
Ziehbewegung oder Drehknopfbewegung beginnt dort.

Die ersten beiden Bewegungen jeder Stufe gehen von der Mitte zum unteren
und dann zum oberen Ende. Sie messen den Haltestrom an beiden Enden und
zählen nicht zum Ergebnis. Danach folgen Bewegungen von Ende zu Ende:
entweder die unter BEWEGUNGEN gewählte Anzahl oder so lange wie TESTZEIT
vorgibt, abhängig von TESTDAUER NACH. Je Stufe werden höchstens
1000 Bewegungen gezählt.

- **Schwelle:** je Stufe der größere Wert aus 0,020A
  (`SERVO_TEST_MOVE_MIN_A`) und dem 3-fachen (`SERVO_TEST_NOISE_K`)
  Ruherauschen, der Standardabweichung der Messwerte in RUHE. Der Bericht
  nennt sie in der Spalte `Schw.`. Ein Servo, dessen Messwerte in Ruhe um
  0,010A streuen, hat eine Schwelle von 0,030A.
- **Bewegung:** ein Messwert, der mehr als die Schwelle vom Wert vor dem
  Befehl entfernt ist: darüber, oder darunter, wenn das Servo ein Ende
  verlässt, an dem es gegen einen Anschlag gedrückt hat.
- **Ankunft:** Nach erkannter Bewegung muss zunächst ein Messwert um
  mehr als die Schwelle über dem Haltestrom des Zielendes liegen. Als
  Ankunft zählt dann der erste Messwert, der wieder innerhalb von 0,05A
  (`SERVO_TEST_BAND_A`) um diesen Haltestrom liegt. Ein Messwert, der noch
  um mehr als die Schwelle darüber liegt, gilt weiterhin als Bewegung,
  auch wenn er schon im Band von 0,05A liegt. Das breitere Band erkennt
  deshalb keine Ankunft zu früh. Fällt der Strom stattdessen um mehr als
  0,05A unter den Haltestrom, etwa nach einem Stromstoß beim
  Beschleunigen, gilt die folgende Regel. Die Halteströme beider Enden
  können sich um mehr als 0,05A unterscheiden. Ein Messwert noch auf dem
  Niveau des Startendes oder ein steigender Strom beim Durchlaufen des
  Zielniveaus zählt deshalb nicht als Ankunft.
- **Ankunft bei einem höheren Halte- als Bewegungsstrom**, etwa an einem
  mechanischen Anschlag: Der Strom überschreitet das Halteniveau nicht.
  Als Ankunft zählt deshalb der erste von zwei aufeinanderfolgenden
  Messwerten nach erkannter Bewegung, die jeweils höchstens 0,05A vom
  Halteniveau und voneinander abweichen. Ein Strom, der das Zielniveau
  mit weniger als 0,05A Anstieg je Messwert durchläuft, kann dabei
  irrtümlich als Ankunft erkannt werden.
- **Nicht unterscheidbar:** Liegt der Bewegungsstrom innerhalb von 0,05A
  um den Haltestrom des Ziels, lässt sich das bewegte Servo nicht von
  einem bereits angekommenen unterscheiden. Die ersten zwei Messwerte
  nach erkannter Bewegung bestimmen dann die Ankunft.
- **Fenster:** eine Bewegung hat 3000ms (`SERVO_TEST_TRAVEL_TIMEOUT_MS`)
  plus die Verzögerung des Strommessers, um anzukommen: 3300ms am PD mini,
  dessen Messwerte eine Ankunft etwa 300ms später zeigen. Die Verzögerung
  wird addiert, statt die letzten 300ms des Fensters unbeurteilt zu
  lassen: ein Servo, das bei 2900ms ankommt, zeigt das bei etwa 3200ms
  und wird gemessen, und eine Bewegung, die nie ankommt, ist trotzdem
  verspätet, 300ms später. Ein Messwert am Ende des Fensters oder danach,
  3300ms am PD mini, ist nie eine Ankunft: die Bewegung ist verspätet.
- **Unerkannt:** eine gezählte Bewegung ohne Messwert jenseits der Schwelle
  binnen des Fensters. Der Strom unterscheidet sie nicht von einem stillstehenden
  Servo: sie wird weder gemessen noch als verspätet gezählt, und der Bericht
  zählt sie in der Spalte `Unerk.`. Eine Stufe, bei der keine Bewegung
  erkannt wurde, lautet `NICHT MESSBAR`.
- **Stellzeit:** vom Befehl bis zum Messwert der Ankunft.
- **Strom in Bewegung:** der Mittelwert der Messwerte zwischen Befehl und
  Ankunft; der Spitzenwert ist der höchste davon.

Das Ankunftsband bleibt bei 0,05A, während sich die Bewegungsschwelle an
das Ruherauschen anpasst. Beim Nachspielen der beiden unten beschriebenen
Prüfstandsläufe liefern ein Band von 0,05A und ein Band in Höhe der
Schwelle dieselben Stellzeiten, auf die Millisekunde genau. Messwerte um
mehr als die Schwelle über dem Haltestrom gelten in beiden Fällen noch
als Bewegung. Entscheidend ist daher die angepasste Bewegungsschwelle. Eine feste
Bewegungsschwelle von 0,10A, der Wert der Firmware 0.13.0, übersieht den
MG90S, der sich mit 0,04 bis 0,077A über 0,001A Haltestrom bewegt: keine
Bewegung wird erkannt, jede läuft nach 3000ms aus und zählt als verspätet,
und der Lauf lautet NICHT BESTANDEN.

**Einschränkung: die Bewegung zur Mitte kann in RUHE reichen.** Mit
EINSCHWINGZEIT 500ms tragen die Messwerte in RUHE noch das Ende der Bewegung
der Stufe zur Mitte: beim 1102HB bei 5,00V 0,025A, fallend auf 0,003A,
aus der Verzögerung des PD mini von 0,3s und der Bewegung selbst. Das hebt
den Ruhemittelwert und das Rauschen und damit die Schwelle: dort 0,026A
statt 0,020A. Eine längere EINSCHWINGZEIT vermeidet diese Überlagerung.

**Brown-out.** Nach den Stufen, mit BROWN-OUT an: ab 5,00V
(`SERVO_TEST_BROWNOUT_START_V`) oder der geltenden Spannungsgrenze, wenn
die niedriger ist, in Schritten von SCHRITT abwärts bis NIEDRIGSTE
SPANNUNG, der Untergrenze, oder bis zum kleinsten Sollwert des Netzteils,
wenn der höher liegt. SCHRITT ist 0,1 bis 1,0V, vorgegeben 0,20V
(`SERVO_TEST_BROWNOUT_STEP_V`); NIEDRIGSTE SPANNUNG ist 2,0 bis 4,8V,
vorgegeben 3,00V (`SERVO_TEST_BROWNOUT_FLOOR_V`). Beide werden auf der
Seite des Brown-out-Laufs gesetzt, die die Zeile BROWN-OUT der Optionen des
automatischen Tests öffnet, und im NVS (nichtflüchtiger Speicher) gehalten.
Die k-te Spannung ist der Start abzüglich k Schritten, auf 0,01V. Die
geltende Grenze ist die niedrigste aus SPANNUNG MAX, dem Höchstwert des
Netzteils und beim PD mini seiner Eingangsspannung abzüglich 0,7V. Liegt die Untergrenze zwischen zwei regulären Stufen, wird sie als letzte
Stufe angefahren; dieser letzte Schritt ist kürzer als SCHRITT: NIEDRIGSTE SPANNUNG 3,1V in Schritten von 0,20V geht 5,0, 4,8 ...
3,2, 3,1V, und 4,8V in Schritten von 1,0V geht 5,0, 4,8V. Am Modell, dessen
kleinster Sollwert 3,3V ist, geht der vorgegebene Lauf 5,0, 4,8 ... 3,4,
3,3V, und eine NIEDRIGSTE SPANNUNG unter 3,3V endet ebenfalls dort. Liegt NIEDRIGSTE SPANNUNG bei oder über der Startspannung, wird nur die
Startspannung geprüft. Bei einer Grenze von 4,5V und NIEDRIGSTE SPANNUNG
4,8V läuft der Test deshalb nur bei 4,5V. Die
feinste Einstellung, 2,0V in Schritten von 0,1V, verlangt 31 Spannungen
(`SERVO_TEST_BROWNOUT_MAX`). Jede Spannung durchläuft SETZEN, EINSCHWINGEN
und RUHE, dann zwei Bewegungen (`SERVO_TEST_BROWNOUT_MOVES`), von der Mitte
zum oberen Ende und weiter zum unteren, jede 600ms gehalten. Bei einer Spannung wird **keine Bewegung** erkannt, wenn kein Messwert
der beiden Bewegungen um mehr als die zugehörige Schwelle vom Strom vor
dem jeweiligen Befehl abweicht. Die Schwelle stammt aus der RUHE-Messung
bei dieser Spannung. Der Lauf abwärts endet bei der ersten
Spannung ohne Bewegung; der Bericht nennt sie, die niedrigste, bei der sich
das Servo noch bewegt hat, und die Schwelle der zuletzt gelaufenen
Spannung. Keine Bewegung schon bei der ersten Spannung, 5,00V, lautet
`nicht messbar`: ein Servo, an dem nie Bewegung erkannt wurde, kann sich
unter der Schwelle bewegen.

| Konstante | Wert | Bedeutung |
| --- | ---: | --- |
| `SERVO_TEST_MOVE_MIN_A` | 0,020A | die kleinste Schwelle: Bewegung; über dem Wert des Ziels, noch nicht angekommen |
| `SERVO_TEST_NOISE_K` | 3 | die Schwelle in Ruherauschen, wo das größer ist |
| `SERVO_TEST_BAND_A` | 0,05A | am Haltestrom angekommen |
| `SERVO_TEST_PDMINI_LAG_MS` | 300ms | die Verzögerung des PD mini, wie der Bericht sie nennt |
| `SERVO_TEST_IDLE_MS` | 1000ms | die Ruhestrommessung |
| `SERVO_TEST_HOLD_MIN_MS` | 600ms | das kürzeste gemessene Halten |
| `SERVO_TEST_TRAVEL_TIMEOUT_MS` | 3000ms | eine Bewegung, die nicht angekommen ist, ist verspätet, nach diesem Wert plus der Verzögerung des Strommessers |
| `SERVO_TEST_SET_TOL_V` | 0,05V | ein zurückgelesener Sollwert |
| `SERVO_TEST_SET_TIMEOUT_MS` | 3000ms | für den Sollwert und für das Einschalten |
| `SERVO_TEST_STALE_MS` | 1500ms | kein neuer Messwert beendet den Lauf |
| `SERVO_TEST_STALL_ABORT_MS` | 1000ms | so lange über BLOCKIERT AB beendet den Lauf |
| `SERVO_TEST_CC_ABORT_MS` | 1000ms | so lange Netzteil im Konstantstrom beendet den Lauf |
| `SERVO_TEST_WIN_STALE_MS` | 200ms | ein Lauf am INA3221: so lange kein Fenster beendet ihn; 4 Fenster von 50ms. Der Endlagenlauf teilt den Wert |
| `SERVO_TEST_WIN_MS` | 50ms | ein Fenster des INA3221 |
| `SERVO_TEST_NEG_IDLE_A` | 0,020A | ein Ruhestrom unter minus diesem Wert wird als Richtung des Shunts berichtet |
| `SERVO_TEST_BROWNOUT_START_V` | 5,00V | wo der Brown-out-Lauf beginnt |
| `SERVO_TEST_BROWNOUT_STEP_V` | 0,20V | Vorgabe von SCHRITT: jeder Schritt abwärts |
| `SERVO_TEST_BROWNOUT_STEP_MIN_V`, `_MAX_V` | 0,10V, 1,00V | Bereich von SCHRITT |
| `SERVO_TEST_BROWNOUT_FLOOR_V` | 3,00V | Vorgabe von NIEDRIGSTE SPANNUNG: die niedrigste verlangte Spannung |
| `SERVO_TEST_BROWNOUT_FLOOR_MIN_V`, `_MAX_V` | 2,00V, 4,80V | Bereich von NIEDRIGSTE SPANNUNG |
| `SERVO_TEST_BROWNOUT_MAX` | 31 | die meisten Spannungen eines Laufs: (5,0 - 2,0) / 0,1 + 1 |
| `SERVO_TEST_BROWNOUT_MOVES` | 2 | Bewegungen je Brown-out-Spannung |

### Zeitauflösung

Ein Messwert zählt einmal. Beim PD mini ist der Zähler das Register SAMPLES
der SUPPLY-Seite: die Zahl der beantworteten Ausgangsmessungen des Moduls,
vom IOmcu gezählt. Ein Lesen der Seite, bei dem SAMPLES stehen
geblieben ist, bringt keinen neuen Messwert, gleich welche Spannung und
welchen Strom es trägt (ein Display-Lesen auf dem IOmcu, das langsam
war (bis 400ms, `PDMINI_REPLY_MS`) oder fehlschlug, lässt die letzten stehen),
und ein Lauf ohne neuen Messwert über 1,5s endet. Als Zeitstempel erhält der Messwert die Panelzeit der ersten Abfrage,
die diesen Zählerstand liefert. Das Modell zählt seine eigenen Schritte
und versieht sie selbst mit Zeitstempeln. Der IOmcu liest den
Ausgang des PD mini alle 100ms (`PDMINI_DISPLAY_MS`), das Panel die Seite
alle 100ms (`SUPPLY_LINK_READ_MS`); ein neuer Messwert erreicht den Test
also bestenfalls alle 100ms, beim Modell alle 50ms. Springt SAMPLES
zwischen zwei Lesevorgängen um mehr als eins, erreichen die Messwerte
dazwischen den Test nie: der Bericht zählt sie in seiner Zeile `Skipped`, und
die Rate des Netzteils schließt sie ein. Der Bericht nennt beide Raten, im
Lauf gemessen, und den mittleren Abstand zweier Messwerte, die den Test
erreicht haben.

Die Stellzeit endet am ersten Messwert, der wieder am Haltestrom liegt.
Sie kann deshalb um bis zu ein Messintervall zu lang ausfallen. Sie enthält auch den Weg des Befehls
vom Panel zum Pin: Render-Schleife, Control-Task, CAN-Link (Controller Area
Network) und den nächsten PWM-Frame (Pulsweitenmodulation). Diese
Verzögerung ist nicht gemessen. Wie der PD mini einen Messwert mittelt, ist
nicht bekannt.

**Die Messwerte des PD mini sind verzögert und wiederholen sich.** Zwei
Läufe am Prüfstand mit dem PD mini auf 0.13.0, ein MG90S und ein 1102HB,
lasen ihn alle 102 bis 106ms. Vom Befehl bis zum ersten Messwert 0,02A
über dem Wert davor vergehen im Median 0,31s, und ein Strom wiederholt sich
oft über mehrere Messwerte. Jede am PD mini abgelesene Stellzeit ist darum
eine Obergrenze: beim MG90S 771 bis 989ms für ein Servo mit etwa 0,1s je
60 Grad. Der Bericht sagt das in seinen Zeilen `Verzögerung`,
`Wiederholung` und `Stellzeiten`, und STELLZEIT kann einen Lauf am PD mini
nicht scheitern lassen: ihre Zeile nennt die längste Stellzeit und lautet
`Obergrenze, nicht gegen die Grenze geprüft`. Was den Strom misst, wird dem
Lauf beschrieben (`servo_test_meter_t`: ein Name, die Verzögerung, ob sich
Messwerte wiederholen, ob Stellzeiten eine Obergrenze sind); ein schnellerer
Stromsensor setzt seine eigenen Werte und lässt STELLZEIT prüfen. Ein Lauf
am Netzteilmodell des Panels nennt keine eigene Verzögerung und prüft
STELLZEIT wie der PD mini: gar nicht.

Am INA3221 ist ein Messwert ein Fenster, gezählt an der Nummer des Fensters:
springt sie zwischen zwei Fenstern, die der Lauf bekam, um mehr als eins,
zählt die Zeile `Übersprungen` die fehlenden. Das Panel nimmt jedes Fenster
einmal und in Reihenfolge. Die Warteschlange zum Bildschirm fasst 8;
dauert ein Frame länger als 400ms, gehen die ältesten Fenster verloren. Die Zeit eines Fensters ist der
späteste Zeitpunkt, zu dem es geschlossen haben kann: die Panelzeit des
Lesens, das es brachte, abzüglich 50ms für jedes Fenster, das nach ihm
schloss und im selben Lesen kam. Fenster, die zusammen ankommen, liegen im
Lauf und in der CSV also 50ms auseinander, wie die, die einzeln ankommen.

### Der Strommesser des Laufs

Ein Lauf liest von TEST STARTEN bis zu seinem Ende ein Messgerät: das
Messgerät der Servo-Schiene, wie das Panel es beim Start des Laufs nennt
([Link](Link-de.md#der-fenster-ring)).

| | PD mini | der INA3221-Kanal |
| --- | --- | --- |
| Der Lauf liest es, wenn | der INA3221 bei TEST STARTEN nicht das Messgerät der Schiene ist oder das Netzteil das Modell des Panels ist | der INA3221 bei TEST STARTEN das Messgerät der Schiene ist und das Netzteil der PD mini |
| Ein Messwert | ein Messwert des Netzteils, alle 102 bis 106ms | ein 50-ms-Fenster des Kanals, der das Ziel des Testers überwacht, 20 je Sekunde |
| Strom | der Messwert | das Mittel des Fensters. Die Spitze ist der höchste oder der tiefste 1-ms-Messwert des Fensters, je nachdem, welcher weiter von null liegt |
| Spannung | der Messwert am Ausgang des Netzteils | die Busspannung des Kanals an der Lastseite des Shunts: das Mittel des Fensters in `Ist V`, sein tiefster Messwert in `V min` |
| RUHE und HOLD | die Messwerte, die in der Phase genommen wurden | die Fenster, die ganz in der Phase liegen; eines, das vor ihr begann, gehört zu keiner. Am modellierten Prüfstand der Host-Suite liegen in den 1000ms von RUHE 19 bis 20 Fenster und in 600ms Halten 11 bis 12 |
| BEWEGEN | die Messwerte ab dem Befehl | die Fenster ab dem, das beim Befehl offen ist |
| Stellzeit | bis zum Messwert, der die Ankunft zeigt: um bis zu einen Messwert und etwa 300ms Verzögerung zu lang | bis zum Fenster, das die Ankunft zeigt: um bis zu zwei Fenster und den Poll, der sie liest, zu lang, 145ms |
| STELLZEIT | berichtet, nicht geprüft | berichtet, nicht geprüft |
| Eine Bewegung ist verspätet nach | 3300ms | 3050ms |
| Spannung und Strom des Netzteils | jede Zahl und jede Zeile | nicht verwendet und nicht aufgezeichnet |
| Zustand des Netzteils: antwortet, Ausgang, Abschaltung, zurückgelesener Sollwert, Modus | gelesen | gelesen |

Beim INA3221 ermittelt das Panel die Stellzeit aus den Messfenstern. Die
Bewegungsaufzeichnung des IOmcu, die eine Bewegung in 1ms ab dem
PWM-Frame misst, verwendet der Lauf nicht.

- **Bis zum Ende gehalten.** Ein Lauf am INA3221 endet ABGEBROCHEN, wenn der
  INA3221 aufhört, das Messgerät der Schiene zu sein, mit der Bedingung, die
  ausfiel, als Grund (siehe
  [Was einen Lauf beendet](#was-einen-lauf-beendet)), und wenn ihn 200ms
  lang kein Fenster erreicht (`SERVO_TEST_WIN_STALE_MS`). Er läuft nie mit
  dem PD mini weiter: die beiden Messgeräte stimmen nicht überein. In einem
  aufgezeichneten Lauf eines MS24 war der höchste Messwert des PD mini
  0,390A und der höchste 1-ms-Messwert des INA3221 1,637A. Ein Lauf am PD
  mini verwendet ihn weiter, auch wenn der INA3221 währenddessen als
  Messgerät verfügbar wird.
- **Warum nicht der INA3221.** Ein Lauf am PD mini, bei dem ein INA3221
  unter SETUP > SENSORS bestückt ist, hat die Berichtszeile `INA3221: nicht verwendet: <Grund>`:
  `IOmcu-Firmware älter als 0.16.0`, `der IOmcu hält
  seine Einstellung nicht`, `er antwortet nicht`, `kein Fenster mit Strommesswert in den letzten 200ms`, `er hat sich zurückgesetzt`, `er arbeitet seit weniger
  als 1s` oder `das Netzteil ist vom Panel simuliert`.
- **Strom mit Vorzeichen.** Ein negativer Strom ist ein Messwert.
  RUHESTROM, HALTESTROM, BLOCKIERT AB und die Spitze nehmen seinen Betrag,
  CSV und Bericht behalten das Vorzeichen. Ein Lauf, dessen Ruhestrom in
  einer Stufe unter -0,020A liegt (`SERVO_TEST_NEG_IDLE_A`), hat die
  Berichtszeile `Strom in Ruhe negativ: Richtung des Shunts`.
- **Übersteuern.** Der INA3221 misst Shuntspannungen bis 163,8mV:
  1,638A an einem Shunt von 0,1Ω, 3,276A an 0,05Ω. Ein abgeschnittener
  Messwert geht mit dem Wert der Messgrenze in sein Fenster ein. Mittelwert,
  Spitze und Bewertung beruhen weiterhin auf den vom Baustein gelieferten
  Werten. Die CSV-Spalte `clipped` zählt solche Messwerte je Fenster;
  die Berichtszeile `Übersteuert` zählt Fenster mit mindestens einem davon.
  Bei Übersteuerung sind die Stromangaben nur Untergrenzen. Die Anzahl
  abgeschnittener Messwerte entscheidet nicht über das Ergebnis. Ein
  höherer tatsächlicher Strom wird weiterhin als Messgrenze angezeigt.
  Welcher Shunt bestückt ist, bestimmt daher den messbaren Strombereich.
- **Der Spannungsabfall am Shunt.** `Ist V` und `V min` werden hinter dem
  Shunt gemessen. Der Sollwert wird für den Abfall nicht angehoben: am
  Bereichsende fallen am Shunt 0,164V ab, bei jedem Shuntwert, und die
  Berichtszeile `Shunt` sagt das.

### Blockieren und Konstantstrom

| Regel | Bedingung | Wirkung |
| --- | --- | --- |
| BLOCKIERT AB | ein Messwert nach EINSCHWINGZEIT in einer Spannungsstufe, dessen Betrag über BLOCKIERT AB liegt | NICHT BESTANDEN |
| 1s über BLOCKIERT AB | Messwerte über BLOCKIERT AB über 1000ms (`SERVO_TEST_STALL_ABORT_MS`) ohne einen bei oder unter dem Wert, in jeder Phase und im Brown-out-Lauf | der Lauf endet, `1s über BLOCKIERT AB` |
| 1s Konstantstrom | das Netzteil meldet in jedem Messwert Konstantstrom (CC, constant current), 1000ms (`SERVO_TEST_CC_ABORT_MS`) ab dem ersten | der Lauf endet, `1s im Konstantstrom`, gleich wie BLOCKIERT AB steht |
| Konstantstrom unter 1s | | nichts endet und nichts fällt durch; die Berichtszeile `Konstantstrom` nennt die Zahl solcher Messwerte und die längste Folge vom ersten bis zum letzten |
| BLOCKIERT AB bei oder über der Strombegrenzung | bei TEST STARTEN | der Lauf startet. Die Karte AUTOMATISCHER TEST und der Bericht sagen `BLOCKIERT AB 3.00A unerreichbar: Strombegrenzung 2.00A` |
| BLOCKIERT AB bei oder über dem Bereich des INA3221 | bei TEST STARTEN mit dem INA3221 als Messgerät | der Lauf startet. Die Karte AUTOMATISCHER TEST und der Bericht sagen `BLOCKIERT AB 2.00A unerreichbar: INA3221-Bereich 1.638A` |

- Jeder Vergleich eines Stroms mit RUHESTROM, HALTESTROM und BLOCKIERT AB
  geschieht in ganzen mA, im Urteil wie im Bericht: ein Messwert von 0,050A
  gegen eine Grenze von 0,05A besteht, 0,051A nicht.
- Am PD mini zählen die 1000ms ab dem ersten Messwert über BLOCKIERT AB:
  ein Messwert 999ms später lässt den Lauf laufen, einer 1000ms später
  beendet ihn.
- Am INA3221 ist der Messwert das Mittel des Fensters, gleich wie hoch sein
  höchster Messwert ist, und die 1000ms zählen ab dem Beginn des ersten
  Fensters über BLOCKIERT AB: 20 Fenster in Folge beenden den Lauf.
- Ein Netzteil im Konstantstrom hält seine Strombegrenzung; kein Messwert
  liegt also über einem BLOCKIERT AB bei oder über dieser Grenze. BLOCKIERT
  AB und der Startstrom des Netzteils stehen beide ab Werk auf 2,00A. Die
  Konstantstrom-Regel beendet einen solchen Lauf: ein Servo am Anschlag,
  das die Grenze zieht, beendet ihn 1,0 bis 1,1s, nachdem das Netzteil
  Konstantstrom meldet.
- Mit dem INA3221 an seinem 0,1-Ω-Shunt und BLOCKIERT AB auf dem Werkswert
  2,00A ist BLOCKIERT AB unerreichbar. Ein Servo am Anschlag, das weniger
  zieht als die Grenze des Netzteils und mehr als 1,638A, liest sich den
  ganzen Lauf als 1,638A; der Bericht hat dann die Zeile `Übersteuert` und
  die Zeile `unerreichbar`. BLOCKIERT AB auf 1,60A oder tiefer oder ein
  Shunt von 0,05Ω legt BLOCKIERT AB in den Bereich.

Nicht gemessen: was der PD mini mit einem Servo am Anschlag meldet
(Konstantstrom oder Überstrom, und ob er selbst abschaltet) und wie lange er
nach einem Einschaltstrom Konstantstrom hält. Im aufgezeichneten Lauf des
MS24 hielt er ihn zweimal 0,43s lang während unauffälliger Bewegungen, bei 0,004
bis 0,342A.

### Der Ausgangsencoder

Ein magnetischer Winkelsensor AS5600 auf der Ausgangswelle des Servos, in
SETUP, SENSORS unter AS5600 eingestellt (Standard AUS) und vom IOmcu
gelesen ([Link](Link-de.md), SENSE-Register 26 bis 31), fügt einem Lauf den
Winkel des Horns hinzu. Die aus dem Strom ermittelten Ergebnisse bleiben unverändert. Der
Winkel geht nicht in das Urteil ein und hat keinen Grenzwert. Ist AS5600
ausgeschaltet, verhalten sich Lauf, CSV und Bericht wie ohne den Sensor.
Der IOmcu übernimmt die SENSE-Einstellungen nur in SAFE. Ein Lauf nutzt
den Winkel erst, wenn der IOmcu AS5600 als eingeschaltet bestätigt hat.
Schaltest du AS5600 in ARMED ein, fehlen deshalb die Winkelspalten,
bis die Einstellung in SAFE übernommen wurde.

Der Winkel ist der 12-Bit-Zählerstand des Sensors minus der Mitte (AS5600-Mitte
oder SENSOR-MITTE auf OPTIONEN > PRÜFLING, die den aktuellen Zählerstand nimmt,
wenn das Servo in Neutral steht), in Grad von -180 bis knapp unter 180. Der
Sensor zählt in der Richtung hoch, auf die sein DIR-Pin gelegt ist; der Test
nimmt an, dass der Winkel mit der Pulsbreite steigt, eine DIR-Beschaltung in
die andere Richtung zeigt sich also als Winkelfehler vom Doppelten des Wegs.
Die Sollwinkel entsprechen der Anzeige: -90 Grad bei PULS MIN, +90 bei
PULS MAX, mit UMKEHREN und TRIMMUNG. Bei UMKEHREN an wird der gemessene Winkel
genauso negiert, auf der Seite, im Bericht und in der CSV; ein Horn an der angeforderten Endstellung zeigt dann keinen Fehler. Ein Servo, das über diese Spanne weniger als
90 Grad dreht, zeigt den Unterschied als Winkelfehler.

Der Zählerstand springt von 4095 auf 0. Jeder Winkel beschreibt deshalb
eine Position auf einer Umdrehung. Differenzen und Mittelwerte werden
auf dem Kreis berechnet; der Übergang über null wird dabei berücksichtigt. Der Abstand einer Bewegung von ihrem Start wird auf dem kürzeren
Weg genommen, 0 bis 180 Grad: zwei Positionen 1 Schritt beiderseits der halben Umdrehung von
der Mitte liegen 0,18 Grad auseinander, nicht 359,8. Die Endwinkel einer Stufe
an einem Ende werden als Abstände vom ersten von ihnen gemittelt, und der
Mittelwert wird auf den Kreis zurückgelegt; Enden bei +179,9 und -179,9 Grad
ergeben also 180 (angezeigt als -180.00), nicht 0. Auch der Winkelfehler wird
auf -180 bis +180 Grad gefaltet.

Der Sensor unterscheidet nur eine Umdrehung, und das begrenzt, was für ein
Servo berichtet wird, das mehr als 180 Grad dreht:

| Fall | Berichtet |
| --- | --- |
| die Enden mehr als 180 Grad auseinander, jedes innerhalb von 180 Grad um die Mitte (-100 und +100) | beide Endwinkel und beide Fehler, wie sie sind; die beiden Enden werden nie voneinander subtrahiert |
| ein Ende mehr als 180 Grad von der Mitte (+200) | der Endwinkel um 360 Grad versetzt (-160); der Winkelfehler stimmt, solange der befohlene Winkel dieselbe Position nennt (+200), weil er gefaltet wird |
| eine Bewegung, die innerhalb von 2,0 Grad um eine ganze Umdrehung von ihrem Start endet | unbewegt |
| die Richtung einer Bewegung und die Umdrehungen eines Windenservos | nicht beurteilt, nicht gezählt |

Für jede Bewegung, vom Befehl bis zum nächsten Befehl (Konstanten in
`servo_test.h`):

| Begriff | Regel |
| --- | --- |
| bewegt | der Winkel verlässt `SERVO_TEST_ENC_MOVED_DEG`, 2,0 Grad, um den Winkel vor dem Befehl, auf dem kürzeren Weg; dieser Messwert muss jünger sein als `SERVO_TEST_ENC_STALE_MS`, 500ms, sonst wird die Bewegung nicht beurteilt |
| beruhigt | nach der Bewegung ein Messwert, dessen Ruhezeit mindestens `SERVO_TEST_ENC_HOLD_MS`, 100ms, beträgt und deren Ruhe nach dem Befehl begann. Der Winkel ist dann so lange innerhalb von `SERVO_TEST_ENC_TOL_COUNTS`, 12 Schritten oder 1,05 Grad, eines Ankers geblieben |
| Stellzeit (Winkel) | der Beginn dieser Ruhe minus der Befehl: der Moment, in dem der Winkel in die Toleranz um seinen Endwert kam |
| Endwinkel | der Winkel beim letzten Messwert vor dem nächsten Befehl, bei einer beruhigten Bewegung |
| Winkelfehler | der mittlere Endwinkel an einem Ende, auf dem Kreis ab dem ersten Endwinkel der Stufe dort gemittelt, minus der befohlene Winkel dieses Endes, auf -180 bis +180 Grad gefaltet |
| unbewegt | der Winkel verließ die 2,0 Grad nie |
| spät | er bewegte sich und war vor dem nächsten Befehl keine 100ms ruhig |

Die Toleranz ist `SENSE_ENC_STILL_TOL` des IOmcu; `test_as5600` hält
beide gleich. Der IOmcu ermittelt die Ruhezeit mit seinem Messintervall von 2ms,
die Stellzeit hängt also nicht davon ab, wie oft das Panel die Page liest (alle
40ms). Sie beginnt beim Befehl, wie der Test ihn ausgibt, und enthält den
Weg des Befehls zum Pin: die Render-Schleife, den Control-Task, den Link und
den nächsten PWM-Frame, bis zu einem Poll-Intervall und einem Frame, nicht
gemessen. Sie endet, sobald der Winkel höchstens 1,05 Grad vom Endwert abweicht.
Der Arm kann sich während des verbleibenden Wegs noch bewegen: bei einem Servo mit 0,09 Grad je Mikrosekunde und 1,2µs je
Millisekunde etwa 10ms. Berichtet werden nur gezählte Bewegungen; die
Bewegungen, die das Horn zuerst an jedes Ende stellen, nicht. Eine Bewegung
fällt aus den Zählungen des Winkels heraus, weder unbewegt noch spät, wenn der
Winkel eine Lücke hat, während sie offen ist, und sie sich noch nicht beruhigt
hatte (ein als ungültig markierter Messwert oder einer, der auf Messwerte
folgt, die nach einem Stillstand der Anzeige von 320ms oder mehr auf dem Weg
zum Bildschirm verloren gingen, löscht den Winkelverlauf), wenn
sie keinen Startwinkel hat und wenn der Lauf aus einem anderen Grund als dem
Abschluss endet (STOP, Wechsel auf SAFE, Linkverlust, Versorgungsfehler, Blockade),
bevor sie sich beruhigt hat: ihr Fenster wurde abgeschnitten, und die
Ergebnisse aus dem Strom zählen so eine Bewegung auch nicht. Eine Bewegung,
die sich vor der Lücke oder dem Abbruch beruhigt hatte, behält ihr Ergebnis.

Der Winkel ist nur ein Messwert, solange der Sensor seinen Magneten erkennt
(STATUS MD, [Link](Link-de.md)). Meldet er keinen, ist sein Zählerstand keine
Position. Das Panel verwendet ihn nicht als Winkel. Der Lauf erhält
einen ungültigen Messwert mit Begründung, die Winkelspalte der CSV bleibt
leer und eine noch nicht abgeschlossene Bewegung ohne bestätigte Ruhe
wird wie oben aus der Auswertung ausgeschlossen. Der Bericht hat
dann die Zeile `Kein Magnet: AS5600 meldete N-mal keinen Magneten. ...`; N zählt, wie
oft der Sensor im Lauf aus einem anderen Zustand zu "kein Magnet" wechselte,
ein in diesem Zustand gestarteter Lauf eingeschlossen. Ein als zu schwach oder
zu stark gemeldeter Magnet (ML, MH) bei gesetztem MD lässt den Winkel in
Gebrauch: das Datenblatt nennt keine Wirkung auf den Winkel und spezifiziert
sein Rauschen nur für 30 bis 90mT. Der Bericht zählt diese Messwerte: `Feld:
N Messwert(e) bei zu schwachem Magnetfeld, M bei zu starkem. ...`. Keine der
beiden Zeilen steht im Bericht, wenn ihre Zahlen 0 sind.

Der Bericht bekommt eine Kopfzeile (`Encoder:`), eine Tabelle je Stufe (den
mittleren Endwinkel und seinen Fehler an jedem Ende, die mittlere und die
längste Stellzeit, die gezählten, unbewegten und späten Bewegungen) und die
befohlenen Winkel mit den Regeln oben. Die Tabelle des Stroms und seine Spalte
`Stell.` bleiben, die beiden Zeiten stehen also nebeneinander: am PD mini ist
die aus dem Strom eine Obergrenze und hinkt dem Horn um etwa 0,3s nach, die aus dem Winkel
nicht. Die CSV bekommt drei Spalten, `angle (deg)` in jeder Zeile, deren
Winkelmesswert jünger als 500ms ist, `travel angle (ms)` in der ersten
Zeile, die zur Zeit des Messwerts, der die Beruhigung fand, oder danach
genommen wurde, und `angle gap (ms)` in der ersten Zeile zur Zeit des
Messwerts, der eine Lücke des nicht antwortenden Bausteins beendete, oder
danach. Jede Zeile nimmt den neuesten Winkelmesswert, der zur Zeit der
Zeile oder davor genommen wurde, aus den letzten 16 Messwerten (etwa 640ms);
die Reihenfolge, in der das Panel Messwerte und Zeilen abarbeitet, schiebt
also keinen Winkel in die falsche Zeile. Der Bericht vermerkt, dass das Totband nicht
gemessen wird: es braucht Schritte, die kleiner sind als die Bewegungen von
Ende zu Ende, und der Test macht keine. Die Zeile GEMESSEN der Seite SERVO
zeigt den aktuellen Winkel, solange AS5600 an ist. Fällt der Link aus, wird
der Messwert gelöscht: die Zeile zeigt Striche, und SENSOR-MITTE setzt nichts,
bis mit dem Link wieder ein Messwert eintrifft. Dasselbe gilt, solange der
Sensor keinen Magneten meldet.

Bleibt die Antwort eines seit Laufbeginn verwendeten AS5600 aus, beginnt
eine Messlücke des Encoders. Motor- und ESC-Läufe mit hohem Strom stören den
gemeinsamen I2C-Bus, und ein Baustein, der während eines Laufs an den Bus
gesteckt wird, lässt einen anderen daran kurz ausfallen; kein Schutzmechanismus hängt vom Winkel ab, deshalb toleriert der Lauf
eine solche Lücke (Konstanten in
`servo_gap.h`):

| Lücke | Lauf |
| --- | --- |
| bis 300ms (`SERVO_GAP_MAX_MS`), Beginn 10.000ms (`SERVO_GAP_EVERY_MS`) oder mehr nach dem Beginn der Lücke davor | geht weiter: die Winkelspalte der CSV ist darin leer, eine Bewegung, die über die Lücke offen und nicht beruhigt war, fällt wie oben heraus, ab dem nächsten Messwert zählen die Winkel wieder |
| länger als 300ms | endet im ersten Durchlauf nach 300ms, oder mit dem Messwert, der eine Lücke von 301ms beendet: `AS5600 antwortet nicht` |
| Beginn weniger als 10.000ms nach dem Beginn der Lücke davor, 9999ms eingeschlossen | endet mit ihrem Beginn: `AS5600 2x in 10s ausgefallen` |

Beide Abbruchgründe gelten als Fehler: Der Ausgang wird ausgeschaltet
und der Prüfstand auf SAFE gesetzt. Eine Lücke beginnt
mit der Abfrage der Control-Task, die den Baustein nicht antwortend findet
(das SENSE-Lesen alle 40ms), und endet mit dem ersten Lesen eines
antwortenden Bausteins, mit Winkel oder mit "kein Magnet". Ein Lauf, der
startet, während der Baustein nicht antwortet, startet in einer Lücke. Ein
Messwert ohne Magnet ist keine Lücke: der Baustein antwortet. Eine
übergelaufene Warteschlange zum Bildschirm (oben) ist auch keine: am IOmcu
fehlte kein Lesen des Bausteins. Der Bericht hat die Zeile `AS5600-Aussetzer:
N, längster Mms. ...`, wenn N größer als 0 ist, und die Spalte
`angle gap (ms)` der CSV die Länge jeder Lücke, durch die der Lauf fuhr. Ein
Lauf ohne AS5600 in Gebrauch hat keine Lücken. Das Band sagt `AS5600 antwortet nicht unter 0x36.`,
wenn der Baustein verstummt, und nimmt die Zeile mit dem ersten
Lesen zurück, das ihn antwortend findet: ein Baustein, den seine Kontakte
beim Einstecken noch einmal verlieren, hinterlässt kein "antwortet nicht"
über einem Encoder, der antwortet.

Diese Toleranzzeit gilt nur für die Winkelmessung. Ein Messwert, von dem ein Schutz abhängt,
beendet einen Lauf bei seinem ersten Ausfall: die Fenster des INA3221
speisen den Stopp von BLOCKIERT AB ([Was einen Lauf
beendet](#was-einen-lauf-beendet)), die Messwerte des Netzteils seine
Abschaltungen. Ein INA3221, der verstummt, während eine Lücke des AS5600
offen ist, beendet den Lauf sofort mit dem Grund des INA3221.

#### Winkelquelle

WINKELQUELLE unter SETUP > SENSORS > IMU legt fest, woher Servotest,
ENDLAGENLAUF und SERVO-Aufzeichnung den Hornwinkel beziehen: `AS5600`
als Vorgabe oder `IMU`. Bei `IMU` wird die gewählte IMU-Achse mit dem
IMU-Vorzeichen verwendet, bezogen auf den auf der IMU-Seite gesetzten
NULLPUNKT.

Der IMU-Winkel wird in Zählerstände des AS5600 umgerechnet. Dadurch
gelten die oben beschriebenen Regeln auch für diese Quelle:

```
count = round(angle x 4096 / 360) mod 4096     centre count 0
```

Ein Zählschritt entspricht 0,088°. Der Servotest beurteilt und meldet den
Winkel der IMU in dieser Schrittweite. Der Messwert trägt zudem die Grad
der IMU, wie sie kommen: GEMESSEN, RUDER, AUTO, die Winkelzeilen des LIMIT
RUN und das SERVO-Laufprotokoll nehmen diese, und die Bildschirme zeigen
sie auf 0,01°. Der Winkel der IMU durchläuft zuerst ihren IMU-Filter
([Die Bluetooth-IMU](IMU-de.md#der-filter)). Die angegebene Sensorauflösung beträgt 0,05 bis
0,2° auf X und Y sowie 1° auf Z. Z benötigt das Magnetometer; dies ist
am Prüfstand nicht gemessen. AS5600-Mitte gilt nicht für die IMU,
und SENSOR-MITTE übernimmt keinen Zählerstand von ihr.

| | AS5600 | IMU |
| --- | --- | --- |
| ein Lauf liest Winkel, wenn | AS5600 an ist und der IOmcu es an hält | beim Start ein Messwert unter 300ms Alter angezeigt wird |
| Ruhezeit | die des IOmcu, 12 Zählschritte (1,05°) | die eigene der IMU (`IMU_STILL_DEG`, 0,5°, `imu.h`) |
| Lücke | antwortet nicht | 300ms kein Messwert (`IMU_STALE_MS`) |
| Bericht, `Encoder:` | `AS5600 an der Welle des Ruderhorns, Mittenwert N` | `IMU X+, in AS5600-Zählschritten (0.088° je Schritt)` |

Für IMU-Lücken gilt dieselbe Toleranz wie für den AS5600: höchstens
300ms, wobei der Beginn der vorherigen Lücke mindestens 10s zurückliegen
muss. Längere oder häufigere Lücken beenden den Lauf. Beide Berichte tragen die Zeile
`Winkelquelle:    AS5600` oder `Winkelquelle:    IMU X+`. Der Bericht des
ENDLAGENLAUFS nennt die Quelle in der Winkelzeile jeder Endlage. Die CSV des
Servotests hat die Spalte `angle source` zuletzt, nach den Spalten des
Messgeräts, mit `AS5600` oder `IMU X+` in jeder Zeile; eine Datei ohne die
Spalte liest LOGS nach denselben Spaltennamen. `angle (deg)` der SERVO-Aufzeichnung enthält den Winkel dieser Quelle,
wie er unter GEMESSEN angezeigt wird.

Ein Lauf mit der IMU, der an einer Lücke endet, nennt im Abbruchgrund die
IMU, auf der Karte und im Bericht: `IMU antwortet nicht` für eine Lücke über
300ms, `IMU 2x in 10s ausgefallen` für eine zweite Lücke innerhalb von 10s.

### Was einen Lauf beendet

Jeder Abbruch schaltet den Ausgang aus und schreibt trotzdem einen
Bericht mit ABGEBROCHEN und dem Grund. Bleibt der Prüfstand ARMED,
wird die Steuerung des Servos freigegeben und die Sollposition kehrt
zur Ruhelage zurück. Der
Grund steht auch in der zweiten Zeile des Ergebniskastens auf LIVE und in
der letzten Zeile der Karte AUTOMATISCHER TEST (`ZULETZT: ABGEBROCHEN -
STOP`), die dann den Grund statt der Dateien nennt. Ein
Fehler schaltet den Prüfstand statt der Freigabe auf SAFE: ein Netzteil, das
nicht antwortet oder abgeschaltet hat, kein neuer Messwert, ein Ausgang, der
nicht anging, ein nicht bestätigter Sollwert, eine Stufe über der Grenze,
BLOCKIERT AB, Konstantstrom, jeder Grund des INA3221 und beide Gründe des
AS5600 ([Sicherheit, Fehler](Safety-de.md#fehler)). Auf SAFE schalten auch DISARM,
STOP und das Verlassen der Seite. Bei manuellen Eingriffen bleibt der Prüfstand ARMED und die Sollposition
kehrt zur Ruhelage zurück: Berühren der Bedienelemente, Ändern der
Einstellungen und AUSGANG AUS. Dasselbe gilt bei verlorenen
Touch-Ereignissen. Eine vom Lauf angeforderte, aber vom Panel noch nicht gesendete Stufe, wenn der Prüfstand auf SAFE schaltet oder
stoppt, wird nicht gesendet. Nach einem Lauf, den DISARM, STOP oder das
Verlassen der Seite beendet hat, bleiben die Hornanzeige und SOLL-PULSBREITE auf der zuletzt vom Lauf
angeforderten Stellung; nach einem Ende, das den
Prüfstand in ARMED lässt, zeigen sie die Ruhelage, in die die Freigabe die Pins
stellt, 1500us bei STANDARD PWM, gleich welcher TRIMMUNG, und der nächste Drag
oder die nächste Knopfdrehung beginnt dort. Nach dem Lauf werden die vorherigen Sollwerte von SUPPLY
wiederhergestellt, unabhängig von der geöffneten Seite. Das geschieht
erst, wenn der Ausschaltbefehl des Laufs gesendet wurde, eine spätere
Messung den Ausgang als ausgeschaltet bestätigt, kein Einschaltbefehl
unterwegs ist und AUSGANG EIN weder auf SERVO noch auf SUPPLY gehalten
wird. Sollwerte, die du nach dem Lauf geändert hast, bleiben erhalten.

| Grund im Bericht | Ursache |
| --- | --- |
| `STOP` | STOP in der Leiste |
| `Prüfstand SAFE` | DISARM oder alles andere, was den Prüfstand auf SAFE geschaltet hat |
| `Verbindung verloren` | der Link zum IOmcu ging während des Laufs verloren |
| `Seite SERVO verlassen` | eine andere Seite wurde geöffnet |
| `vom Bediener gestoppt` | TEST BEENDEN, ein Finger auf der Skala, ZENTRIEREN, SWEEP, FREIGEBEN, ein Tippen auf einen Sollwert, ein Programmierer, geöffnet aus der Liste von PROGRAMMIEREN |
| `Servo-Optionen geändert` | Typ, Pulsrate, Pulse, Trimmung, Weg, Umkehrung oder TEMPO geändert |
| `Touch-Ereignisse verloren` | zwischen zwei Frames gingen Ereignisse verloren |
| `Netzteil antwortet nicht` | ein Messwert als nicht antwortend markiert |
| `Netzteil abgeschaltet` | der Ausgang ging durch eine Abschaltschwelle aus |
| `Netzteilausgang abgeschaltet` | der Ausgang ging anders aus, AUSGANG AUS eingeschlossen |
| `seit 1.5s kein Messwert` | `SERVO_TEST_STALE_MS` |
| `Netzteilausgang blieb aus` | 3000ms nach dem ON nicht an |
| `Sollwert 3s nicht bestätigt` | das Netzteil hat die Spannung einer Stufe nicht übernommen |
| `Stufe über Spannungsgrenze` | eine Stufe über der geltenden Grenze: SPANNUNG MAX oder die Eingangsspannung des PD mini abzüglich 0,7V |
| `1s über BLOCKIERT AB` | `SERVO_TEST_STALL_ABORT_MS` |
| `1s im Konstantstrom` | das Netzteil meldete `SERVO_TEST_CC_ABORT_MS` lang Konstantstrom |
| `0.2s kein INA3221-Fenster` | ein Lauf am INA3221: `SERVO_TEST_WIN_STALE_MS` ohne ein Fenster mit Strom und Spannung. Ein Durchlauf 199ms nach dem letzten Fenster lässt den Lauf laufen, einer bei 200ms beendet ihn |
| `INA3221 setzte sich zurück` | ein Lauf am INA3221: der Reset-Zähler des Bausteins hat sich geändert |
| `INA3221 antwortet nicht` | ein Lauf am INA3221: kein Lesen von SENSE in 200ms zeigt den Baustein online und erkannt an einem Bus, der nicht hängt |
| `INA3221-Fenster ohne Stromwert` | ein Lauf am INA3221: das neueste Fenster enthält keine Strommesswerte, oder seine Nummer steht seit 200ms |
| `INA3221-Einstellung fehlt` | ein Lauf am INA3221: der IOmcu hört auf, die Einstellung zu halten, oder SETUP hat den INA3221 aus |
| `INA3221 misst nicht mehr` | ein Lauf am INA3221: das Messgerät wechselte, und das Panel nannte die Bedingung nicht |
| `AS5600 antwortet nicht` | ein Lauf mit AS5600 in Gebrauch: der Baustein antwortet länger als `SERVO_GAP_MAX_MS`, 300ms, nicht ([Der Ausgangsencoder](#der-ausgangsencoder)) |
| `AS5600 2x in 10s ausgefallen` | ein Lauf mit AS5600 in Gebrauch: der Baustein verstummte weniger als `SERVO_GAP_EVERY_MS`, 10.000ms, nach Beginn der vorherigen Lücke |
| `IMU antwortet nicht` | ein Lauf mit WINKELQUELLE IMU: länger als `SERVO_GAP_MAX_MS`, 300ms, kein Messwert der IMU ([Winkelquelle](#winkelquelle)) |
| `IMU 2x in 10s ausgefallen` | ein Lauf mit WINKELQUELLE IMU: die IMU verstummte weniger als `SERVO_GAP_EVERY_MS`, 10.000ms, nach Beginn der vorherigen Lücke |

Die sechs Gründe eines Laufs am INA3221 sind die Bedingungen, unter denen der
INA3221 das Messgerät der Schiene ist. Jeder beendet den Lauf in der ersten
Abfrage, die ihn findet: die Fenster des INA3221 speisen den Stopp von
BLOCKIERT AB, der immer aktiv ist (0,1A bis 5,0A, kein AUS). Für fehlende
INA3221-Fenster gibt es deshalb keine zusätzliche Toleranzzeit. Die Gründe
des Netzteils gelten an beiden Messgeräten: sein Zustand wird in jedem Lauf
gelesen.

Eine Stufe wird nie über der Grenze verlangt: ein Lauf, dessen Stufen
außerhalb des Bereichs des Netzteils liegen, wird bei START abgelehnt, und
eine Grenze, die während des Laufs sinkt, beendet ihn vor der Stufe darüber.

### Urteil

Die Spannungsstufen erhalten das Urteil NICHT BESTANDEN, wenn eine der
folgenden Bedingungen erfüllt ist. Der Brown-out-Lauf wird berichtet,
aber nicht gegen diese Grenzen bewertet:

- der höchste Ruhestrom liegt über RUHESTROM;
- der höchste Haltestrom liegt über HALTESTROM;
- die längste Stellzeit liegt über STELLZEIT, wo der Strommesser
  Stellzeiten misst (nicht der PD mini und nicht die Fenster des INA3221);
- ein Messwert nach EINSCHWINGZEIT liegt über BLOCKIERT AB;
- eine gezählte Bewegung war verspätet: Bewegung erkannt, keine Ankunft
  binnen des Fensters, 3000ms plus der Verzögerung des Strommessers.

Sonst NICHT MESSBAR, wenn eine gezählte Bewegung unerkannt blieb oder der
Brown-out-Lauf bei seiner ersten Spannung keine Bewegung erkannte, und
BESTANDEN, wenn keines davon zutrifft. Ein Lauf nur aus dem Brown-out, der
nichts erkennt, lautet NICHT MESSBAR, nicht BESTANDEN. Seine Zeile
`Ergebnis` lautet dann `keine Bewegung erkannt bei 5.00V, der ersten
Spannung des Brown-out`, und `Brown-out-Start` unter den Grenzen nennt
dieselbe Spannung mit NICHT MESSBAR. NICHT MESSBAR sagt, dass der Strom nicht jede
Bewegung zeigen konnte: ein Servo, das sich unter der Schwelle bewegt, und
eines, das stillsteht, lesen sich gleich. Die Zeile `Ergebnis` nennt, bei
wie vielen der gezählten Bewegungen keine Bewegung erkannt wurde, und
`Unerkannt` deren Zahl. Ein Servo, das sich nicht bewegt, lautet NICHT
MESSBAR, nicht NICHT BESTANDEN. Kam keine Bewegung an, gibt es keine
längste Stellzeit, und die Zeile `Stellzeit` lautet `längste --` und
`nicht gemessen, keine Bewegung erreichte das Ziel`, an jedem Strommesser.

Eine Bestehensgrenze von 0 ist ausgeschaltet. BLOCKIERT AB wird dagegen
immer geprüft. Ein
Strom wird mit seinem Betrag und in ganzen mA verglichen: siehe
[Blockieren und Konstantstrom](#blockieren-und-konstantstrom).

### Dateien

Ein Lauf nimmt die nächste Laufnummer des Ordners `USER/LOGS`, und die
eigene Task der SD-Karte schreibt seine Dateien dorthin: `ATESTnnn.CSV`, eine
Zeile je Messwert des Messgeräts des Laufs, und mit BERICHT an (OPTIONEN >
PRÜFLING) `ATESTnnn.TXT`. Die Nummer gehört zu einer Reihe mit den
`LIMITnnn.TXT` der Endlagenläufe in diesem Ordner
([Logs](Screens-de.md#logs)). Mit Testaufzeichnungen AUS unter SETUP > SD CARD,
gelesen bei TEST STARTEN, schreibt der Lauf keine Datei, und das Ergebnis
nennt `TESTAUFZEICHNUNGEN AUS`. Ein Lauf ist eine CSV: das eigene
Laufaufzeichnung des Prüfstands in ARMED schreibt keine Zeile, solange ein Lauf
läuft. Seine Zeitspalte springt über den Lauf, und seine Zeilen beginnen
wieder, wenn der Lauf endet. Eine Nummer, die nur ein
`ATESTnnn.TXT` trägt, dessen CSV am Computer gelöscht wurde, ist trotzdem
vergeben, damit kein Bericht überschrieben wird, und LÖSCHEN in der
Aufzeichnungsansicht löscht mit einem Lauf seinen Bericht. Ein Lauf, den die Karte nicht
annimmt, sagt das in der Leiste und im Ergebnis, `NICHT AUFGEZEICHNET`. Zeilen, für
die die Warteschlange zur Karte keinen Platz hatte, zählt der Bericht. Vom
Start des Laufs, bis die Karte seine CSV und seinen Bericht geschlossen hat,
gilt der Lauf als offener Aufzeichnungslauf, die Zeit zwischen dem OPEN der Datei und
seiner Antwort eingeschlossen: WLAN und ein Update warten darauf, und die
Aufzeichnungsansicht listet und öffnet die Datei, die noch geschrieben wird, nicht.

Die CSV verwendet Semikolons als Trennzeichen und den Dezimalpunkt,
wie jede Aufzeichnung des Prüfstands:

| Spalte | Einheit | Bedeutung |
| --- | --- | --- |
| `time (s)` | s | wann das Panel den Messwert hatte, ab Laufbeginn |
| `test` | | `STEP` oder `BROWN-OUT` |
| `step` | | die Stufe, 1 bis n in der Reihenfolge des Laufs |
| `phase` | | `SET`, `SETTLE`, `IDLE`, `MOVE` oder `HOLD` |
| `command (us)` | us | der befohlene Impuls |
| `position (us)` | us | die gemessene Stellung; leer, weil nichts sie misst |
| `set (V)` | V | die Spannung der Stufe |
| `voltage (V)` | V | die des Messgeräts: am Ausgang des Netzteils, oder die mittlere Busspannung des Kanals im Fenster |
| `limit (A)` | A | die Strombegrenzung |
| `current (A)` | A | der des Messgeräts, mit Vorzeichen: der Messwert des Netzteils, oder das Mittel des Fensters |
| `power (W)` | W | Spannung mal Strom |
| `mode` | | der des Netzteils: `CV`, `CC` oder `OFF`. Am INA3221 der Modus des letzten Messwerts des Netzteils, leer vor dem ersten |
| `travel (ms)` | ms | in der Zeile einer Ankunft: die Stellzeit dieser Bewegung |
| `angle (deg)` | deg | bei verwendeter Winkelquelle: der Winkel des Horns ab der Mitte, aus dem neuesten Messwert, der zur Zeit der Zeile oder davor genommen wurde; leer, wenn dieser älter als 500ms ist oder fehlt |
| `travel angle (ms)` | ms | bei verwendeter Winkelquelle: in der Zeile nach einer beruhigten Bewegung ihre Stellzeit aus dem Winkel |
| `angle gap (ms)` | ms | bei verwendeter Winkelquelle: in der ersten Zeile zur Zeit des Messwerts, der eine Lücke des nicht antwortenden Bausteins beendete, oder danach, die Länge der Lücke; sonst leer |
| `meter` | | `INA3221`, `PDMINI` oder `MODEL`, in jeder Zeile |
| `window` | | am INA3221: die Nummer des Fensters, modulo 65536; ein Schritt von mehr als 1 sind Fenster, die den Lauf nie erreichten |
| `current max (A)` | A | am INA3221: der höchste 1-ms-Messwert des Fensters |
| `current min (A)` | A | am INA3221: sein tiefster |
| `voltage min (V)` | V | am INA3221: der tiefste Messwert der Busspannung im Fenster |
| `clipped` | | am INA3221: Messwerte des Fensters an einem Bereichsende, bei 255 gehalten |
| `angle source` | | bei verwendeter Winkelquelle: ihr Name, `AS5600` oder `IMU X+`, in jeder Zeile |

Die sechs Messgerätespalten stehen ohne Winkeldaten an Position 14 bis
19, mit Winkeldaten an Position 17 bis 22. Bei verwendeter Winkelquelle
folgt ihr Name in Spalte 23. Beim PD mini und beim Modell bleiben die
fünf Spalten nach `meter` leer. Auch ältere Dateien mit weniger Spalten
lassen sich öffnen: 13 ohne AS5600 oder 15 beziehungsweise 21 mit AS5600.
Die Aufzeichnungsansicht erkennt die Spalten an den Namen in der Kopfzeile.

Der Bericht steht in der Sprache, die beim Start seines Laufs gilt; jedes
seiner Wörter ist ein Eintrag in `shared/text/include/ui_text.def`, mit
Englisch und Deutsch ([Sprache der Oberfläche](Language-de.md)).
Die CSV ist in jeder Sprache englisch. Ein englischer Bericht aus der
Wiedergabe eines Laufs eines MG90S am Prüfstand mit dem PD mini steht auf
der [englischen Seite](Servo.md#files); auf Deutsch lauten seine Stufen:

```
Ergebnis:        BESTANDEN
...
ERGEBNISSE JE STUFE (Ströme in mA, Zeiten in ms)
 Soll   Ist     V                             Halten Halten
    V     V   min   Ruhe  Schw. Beweg. Spitze  unten   oben Stell. Längste  Anz. Spät Unerk.
 4.80  4.80  4.80      4     20     37     65      1      1    861     989    41    0      0
 6.00  6.00  5.99      1     20     40     77      1      1    892     978    41    0      0
```

Dieselbe Wiedergabe eines 1102HB, das 0,015 bis 0,029A hält und in
Bewegung bis 0,039 bis 0,044A zieht, lautet `NICHT MESSBAR - bei 25 von 46
gezählten Bewegungen keine Bewegung am Stromverlauf erkannt`: seine Bewegungen zum
oberen Ende verlassen die 0,028A des unteren Endes und überschreiten sie
nie um 0,020A. Mit der festen Schwelle von 0,10A (Firmware 0.13.0) lautet dasselbe Servo
NICHT BESTANDEN, alle 34 gezählten Bewegungen verspätet. Ein abgebrochener Lauf zeigt `Ergebnis: ABGEBROCHEN -
<Grund>`. Unter einer abgeschnittenen Stufe steht eine Zeile `(verkürzt)`,
unter einer Stufe, deren gezählte Bewegungen alle unerkannt blieben, eine
Zeile `NICHT MESSBAR`, bei beidem beides in einer Zeile. Eine nie erreichte
Stufe zeigt `nicht ausgeführt`. Die Zeilen der Stufentabelle sind höchstens
92 Zeichen breit, so breit wie eine Zeile der Aufzeichnungsansicht: Ströme in ganzen
mA nehmen je 6 Zeichen, die Spaltenköpfe zwei Zeilen. `Kann zerstören` nennt das rote Schild, wenn
ein Heli-Typ oder eine Pulsrate über 60Hz gilt.

Ein Lauf am INA3221 hat diese Zeilen anstelle derer des PD mini, hier vom
modellierten Prüfstand der Host-Suite:

```
Aufzeichnung:    .CSV mit derselben Dateinummer; eine Zeile je INA3221-Fenster
Netzteil:        PD mini
Strom:           INA3221 0x40 CH1, Shunt 100.0mΩ, Bereich 1.638A
Spannung:        INA3221 0x40 CH1, Lastseite des Shunts
Shunt:           Spannungsabfall bis 0.164V am Ende des Messbereichs; der Sollwert wird nicht zum Ausgleich erhöht
Messwerte:       20.0/s abgeschlossene INA3221-Fenster, 20.0/s im Test angekommen
Übersprungen:    0 Fenster des INA3221 erreichten den Test nie
Auflösung:       ein Fenster alle 50ms; die Stellzeit kann um bis zu zwei Fenster plus Abfragezeit zu lang sein
Verzögerung:     etwa 50ms von einer Stromänderung bis zum Messwert, der sie zeigt
Stellzeiten:     Obergrenzen; nicht mit der eingestellten Grenze verglichen
...
Position: Die Lage des Ruderhorns wird nicht gemessen. Alle Ergebnisse beruhen auf dem Strom des oben genannten INA3221-Kanals.
Strom zwischen zwei Messwerten dieses Kanals im Abstand von 1ms: Ein Fenster enthält deren Mittel-, Höchst- und Tiefstwert.
```

### Was nicht gemessen ist

Ein Tester hat drei Prüfstandsläufe mit Firmware 0.13.0 und dem PD mini
aufgezeichnet: mit einem MG90S-Mikroservo, einem Digitalservo 1102HB und
einem Digitalservo MS24. Das MS24, in Bewegung 0,16 bis 0,18A, bestand. MG90S
und 1102HB lauteten NICHT BESTANDEN, jede Bewegung verspätet, weil die Firmware 0.13.0
die Bewegungsschwelle auf 0,10A festlegte; beide Servos bewegten sich. Die Host-Suite spielt die gekürzten CSVs von MG90S und 1102HB
(`test/host/fixtures/`) gegen den Testablauf nach. Hinzu kommt ein
MS24-ähnlicher Fall mit den dreifachen Stromwerten des MG90S.
Die Host-Suite prüft den Ablauf außerdem mit `servo_sim` und
`supply_sim`, seine Steuerung durch die Seite SERVO und das erneute
Einlesen der CSV durch den Parser der Aufzeichnungsansicht. Nicht gemessen: die Mittelung des PD mini und die
Verzögerung des Befehls bis zum Pin.

`test_servo_test_win`, `test_servo_test_win_read` und `test_servo_test_win_chain` fahren den Test mit dem INA3221 als Messgerät auf dem Host: der modellierte INA3221 führt den Strom von `servo_sim`,
Zeitplan und Seiten des IOmcu machen daraus Fenster, `sense_link`
nimmt jedes einmal und `servo_source` nennt das Messgerät, neben einem
Modell des PD mini, das alle 104ms gelesen wird. Zwei Aufzeichnungen von
einem Prüfstand auf 0.14.0 werden wiedergegeben: die ganze CSV eines MS24 am
PD mini (`servo-ms24-pdmini.csv`) mit ihren zwei Abschnitten im
Konstantstrom, und die parallel geschriebene Prüfstandsaufzeichnung
(`servo-ms24-windows.csv`), das 1309 der 3817 Fenster enthält, die der
INA3221 schloss, und einen Lauf am INA3221 an seiner ersten Lücke von mehr als
200ms beendet: auf Fenster 19804 folgt 20409, 30,25s später. Der Lauf liest
50 Fenster und zählt die 5 Fensternummern, die vor dieser Lücke fehlen, als
ausgelassen. Nicht am Prüfstand gemessen: wie weit ein Fenster hinter dem Horn
liegt, ob das 50-ms-Mittel die Bewegungen eines Servos zeigt, die der PD mini
nicht zeigt, was der Kanal mit einem Servo am Anschlag liest, und die
Konstantstrom-Regel an einem PD mini.

## Die befohlene Stellung folgt dem Zustand ARMED

Die auf SERVO angezeigte Sollposition ändert sich nur in ARMED, wenn
die Ausgänge den Steuerbefehlen folgen.

| Prüfstand | Befohlene Stellung |
| --- | --- |
| SAFE | die zuletzt gefahrene Stellung; Zifferblatt, Drehknopf, ZENTRIEREN und SWEEP werden abgelehnt, und nichts wird gesendet |
| beim ARM | die Ruhelage der Ruderfläche: die Mitte zwischen den Endpunkten des Kanals, ohne TRIMMUNG |
| ARMED | was Zifferblatt, Knopf, ZENTRIEREN, ein Sweep oder ein Lauf befehlen |

| TYP | Endpunkte | Wert beim ARM |
| --- | --- | ---: |
| STANDARD PWM | 1000 bis 2000us | 1500us |
| NARROW 760 | 660 bis 860us | 760us |
| WIDE | 800 bis 2200us | 1500us |
| HELI CYCLIC | 820 bis 2220us | 1520us |

Die Ruhelage wird durch `outputs_role_rest()` in
`shared/outputs/outputs.c` bestimmt. Diesen Wert gibt der IOmcu in ARMED
auf einem Kanal ohne aktiven Steuerbefehl aus, umgerechnet auf den
Pulsbereich der Bildschirmbefehle. Dieser Bereich ist auf PULS MITTE zentriert; ein PULS MITTE abseits
der Mitte zwischen PULS MIN und MAX ist also der Wert beim ARM. TRIMMUNG verändert die angeforderten Stellungen, lässt die Ruhelage
aber unverändert. Mit TRIMMUNG +20 werden beim Wechsel zu ARMED 1500us
angezeigt; ein Befehl für 0deg entspricht dagegen 1520us.

Ein Sweep endet beim Wechsel auf SAFE, und seine zuletzt gezeichnete Stellung
bleibt auf dem Bildschirm; keine seiner Stellungen wird nach dem Wechsel auf SAFE
gesendet. Die Bedienelemente im Einzelnen:
[Bildschirme](Screens-de.md#servo).

Das Panel zentriert die Ruderflächen, bevor es
auf ARMED schaltet, und der Bildschirm zeigt die Ruhelage, wenn der Prüfstand
ARMED meldet; die Zeit zwischen beidem ist auf einem Prüfstand nicht
gemessen.

## Ruderausschlag

### Zweck

RUDER steuert ein Servo und zeigt, wie weit die Fläche ausschlägt, die es
bewegt: in Grad ab einem Nullpunkt und, mit eingegebener Länge der Fläche,
in mm an der Hinterkante. Es beantwortet „wie viele mm schlägt dieses Ruder
aus“ ohne Lineal am Modell. Die Geometrie steht in
`shared/servo/servo_rudder.c`, die Aufgabe in
`shared/ui/servo_rudder_panel.c`.

### Verfahren

- Der Winkel entspricht GEMESSEN auf LIVE und stammt aus der unter
  WINKELQUELLE gewählten Quelle: AS5600 relativ zu SENSOR-MITTE oder IMU,
  umgerechnet in AS5600-Zählerstände relativ zu 0. UMKEHREN kehrt das
  Vorzeichen um. Die erste Zustandszeile nennt die Quelle, `AS5600` oder
  `IMU X+`, mit der gewählten IMU-Achse und dem IMU-Vorzeichen.
- NULLPUNKT speichert seine Referenz relativ zum rohen Zählwert 0 der
  Quelle. Eine spätere Änderung von SENSOR-MITTE verändert den Ausschlag
  deshalb nicht. Änderst du WINKELQUELLE, IMU-Achse oder IMU-Vorzeichen,
  werden Nullpunkt und MAX verworfen. Dasselbe gilt für NULLPUNKT und
  NULLPUNKT LÖSCHEN auf der IMU-Seite. Die Anzeige entspricht dann wieder
  dem Zustand vor dem Setzen des RUDER-Nullpunkts.
- NULLPUNKT nimmt den aktuellen Winkel als Neutrallage der Fläche. Der
  Ausschlag ist der Winkel ab ihr, auf -180 bis unter 180° gefaltet. Vor
  NULLPUNKT ist der Ausschlag der Winkel selbst.
- LÄNGE L ist der Abstand von der Scharnierlinie bis zur Hinterkante, 0 oder
  10 bis 500mm (Einstellung `rud_len_mm`, 0 = nicht gesetzt). Der Weg ist
  L x sin(Ausschlag): der Abstand der Hinterkante von der Neutralebene, was
  ein senkrecht an die Hinterkante gehaltenes Lineal anzeigt. Bei 30° zeigt
  eine Fläche von 100mm einen Weg von 50,0mm; der Bogen L x Ausschlag
  ergäbe 52,4mm.
- MAX hält den größten Ausschlag je Seite, aus jedem angezeigten Messwert,
  auf jeder Aufgabe. NULLPUNKT und MAX LÖSCHEN löschen ihn.
- Die Seitenansicht rechts der Zustandszeilen zeichnet die Fläche im
  Schnitt, die Nasenkante links: eine graue feste Fläche, das Scharnier
  und das Ruder als cyanfarbener Keil im angezeigten Ausschlag. Positiver
  Ausschlag zeichnet die Hinterkante nach oben, also wie das Vorzeichen
  von HINTERKANTE. Das Ruder ist 62px lang, in einer Fläche von
  190 x 152px; Striche markieren 0, ±30, ±60 und ±90°, gestrichelte
  Linien das MAX jeder Seite und im Kasten von AUTO die beiden Sollwerte
  in Bernstein. Ein Ausschlag über ±90° wird bei ±90° gezeichnet; die
  Zahlen bleiben, wie sie sind. Ohne Messwert liegt das Ruder gedimmt auf
  der Neutrallinie. Die Zeichnung steht in `shared/ui/servo_rudder_flap.c`.
- 1000us und 2000us befehlen diese Pulse als Winkel des eingestellten Typs,
  begrenzt auf WEG und den Pulsbereich des Typs; ZENTRIEREN befiehlt PULS
  MITTE plus TRIMMUNG. Alle drei und der Drehknopf wirken nur, solange der
  Prüfstand ARMED ist. Die Zeile ZIEL der Ausgangskarte nennt den Pin, den
  sie ansteuern.

| Messwert | Auflösung | Bereich |
| --- | --- | --- |
| AUSSCHLAG | AS5600: 0,1° angezeigt, 4096 Schritte je Umdrehung, 0,088°; IMU: 0,01° angezeigt, die Grad, wie sie kommen | AS5600: -180,0 bis +179,9°; IMU: -180,00 bis +180,00° |
| HINTERKANTE | 0,1mm angezeigt | -500,0 bis +500,0mm |

### Grenzen

- Der AS5600 misst die Servoabtriebswelle. Ein Gestänge zwischen Horn und
  Fläche lässt den Ausschlag der Fläche von dem der Welle abweichen; der
  Unterschied ist nicht gemessen. Die IMU misst das, woran sie befestigt
  ist.
- Der Ausschlag ist nicht gefiltert: das Rauschen eines Messwerts zeigt
  sich in seinen Zehnteln und kann MAX darum erhöhen. Das Rauschen des
  AS5600 am Prüfstand ist nicht gemessen.
- UMKEHREN ändert das angezeigte Vorzeichen und erhält den Nullpunkt.
  MAX speichert die Seiten in der ursprünglichen Sensorrichtung; mit
  UMKEHREN tauschen sie deshalb auf dem Bildschirm die Seiten.

## AUTO-Endpoints

### Zweck

AUTO, die dritte Taste in der Zeile NULLPUNKT von RUDER, programmiert die
Endpoints eines KST-, Chaservo- oder Hitec-Servos so, dass 1000us und 2000us den je
Seite eingegebenen Ausschlag erreichen, gemessen an der Winkelquelle. Es
schließt den Regelkreis über den Winkel der Fläche statt über einen nach
Augenmaß eingestellten Weg am Sender. Der Lauf steht in
`shared/servo/servo_rudder_auto.c`, der Kasten in
`shared/ui/servo_rudder_auto_panel.c`. AUTO vertraut der Winkelquelle: Mit
WINKELQUELLE IMU nimmt es die Winkel, die die Bluetooth-Verbindung liefert,
und diese Verbindung ist nicht authentifiziert (`SECURITY.md`, die
Bluetooth-IMU).

### Ablauf

- AUTO zeigt seinen Kasten an der Stelle von LÄNGE, den drei Tasten zum
  Ansteuern und den Zustandszeilen; die Anzeige, die Zeile NULLPUNKT, die
  Seitenansicht und die Ausgangskarte bleiben. Die Seitenansicht zeigt
  zusätzlich das Soll bei 1000us auf der negativen und das bei 2000us auf
  der positiven Seite, jedes auf der Seite, auf der der letzte Lauf es
  gemessen hat. MANUELL an der Stelle von AUTO bringt sie
  zurück. Solange der Lauf läuft, sind MANUELL, NULLPUNKT und MAX LÖSCHEN
  abgelehnt und gedimmt, damit der Lauf ab dem Nullpunkt misst, mit dem er
  begann, und der Drehknopf bewegt nichts.
- Soll bei 1000us und bei 2000us: der Ausschlag, den jede Seite erreichen
  soll, 0,5 bis 90,0° in Schritten von 0,5°, vorgegeben 20,0° (Einstellungen
  `rud_defl_lo`, `rud_defl_hi`). Mit gesetzter LÄNGE zeigt die Zeile die mm
  an der Hinterkante neben den Grad. RESERVE: 0 bis 50%, 10%
  (`rud_margin`). TOLERANZ: 0,1 bis 5,0°, vorgegeben 0,5° (`rud_tol`). Ein Tippen
  öffnet das Tastenfeld.
- Der Zielwert einer Seite ist der eingegebene Ausschlag x (1 + RESERVE):
  20,0° bei 10% ergeben 22,0°.
- Halte 2s HALTEN: START für 2s gedrückt, um den Lauf zu starten. Während
  des Laufs heißt die Taste LAUF BEENDEN; ein Druck beendet ihn sofort.
- Die drei Zeilen darunter: der laufende Schritt, etwa `EINSCHWINGEN
  2000us: SCHREIBEN 1/8, SITZUNGEN 2`, oder warum ein START abgelehnt ist,
  oder die Sollwerte; das Ergebnis, `BESTANDEN: 150>92, 150>92` mit dem Wert
  jedes Endpoints davor und danach, oder `NICHT BESTANDEN: Grund`; das
  Servo, das der Lauf programmiert, `SERVO KST/CHASERVO` oder `SERVO HITEC
  D-SERIE`, während er läuft und vor dem ersten Lauf; danach die Datei des
  Berichts, oder `ZURÜCKSPIELEN IN PROGRAMMIEREN` (an einem Hitec-Servo
  `HITEC001 IN DATEIEN LADEN`) nach einem NICHT BESTANDEN oder einem
  Abbruch, der geschrieben hat oder ein Schreiben unterwegs abbrach, das
  angekommen sein kann.
- Das Servo ist die Zeile SERVO der Einstellungen des ENDLAGENLAUFS:
  KST/CHASERVO, oder HITEC in der Serie des Hitec-Programmierers. Es
  übernimmt den Programmierer, der unter PROGRAMMIEREN zuletzt geöffnet
  wurde. An einem Hitec-Servo sind die Endpoints LEFT END und RIGHT END in
  den Einheiten aus [Hitec-Servos](#hitec-servos), die Lesesitzung schreibt
  keine Pulse, und ein Servo der D-Serie braucht kein PD mini.

Ein START wird abgelehnt, der Grund in der ersten Zeile, solange:

| Bedingung | Zeile |
| --- | --- |
| der Prüfstand SAFE ist, auf dem rbmod-Carrier mit ZIEL auf einem Programmierport und dem Feld PROG 5V aus | `PROG 5V AUS: IM PROG-5V-FELD EINSCHALTEN`, vor `ZUERST ARM`: der Puls des Ports kommt nur mit PROG 5V und nur bei SAFE |
| der Prüfstand SAFE ist | `ZUERST ARM`, wie auf TEST |
| ein anderer Lauf läuft oder ein Bericht noch geschrieben wird | die Zeile des Laufs, wie auf TEST |
| ZIEL die Ausgänge des Prüfstands sind oder der Ausgang sonst nicht der Programmierport ist | `AUTO PROGRAMMIERT EIN SERVO AM PROG-PORT`. ZIEL auf den Ausgängen wird vor dem Messgerät genannt: auf dem rbmod-Carrier überwacht kein Kanal die Ausgänge, bis einer als SERVO PWM zugeordnet ist |
| das Messgerät nicht für den Servostrom eingerichtet ist | die Zeile des Endlagenlaufs |
| ARMED am Port des rbmod-Carriers mit dem Feld PROG 5V aus | `PROG 5V AUS: IM PROG-5V-FELD EINSCHALTEN` |
| eine Sitzung eines Programmers den Port hält | `GESPERRT: SERVOPROGRAMMIERUNG BELEGT DEN PORT`, vor der Zeile zum Puls: die Sitzung ist der Grund, dass kein Puls läuft |
| der Puls des Ports aus ist | `PORT-PWM AUS: DISARM, TIPPE AUF ZIEL, SCHALTE ES EIN` mit PORT-PWM aus, da ZIEL bei ARMED kein Tippen annimmt; `PORT-PWM EIN, WARTET AUF SAFE: DISARM, DANN ARM` mit PORT-PWM ein, etwa nach einem bei ARMED eingeschalteten PROG 5V |
| PULS MIN über 1000us, PULS MAX unter 2000us oder PULS MITTE nicht strikt zwischen 1000us und 2000us liegt | `PULS MIN BIS MAX MUSS 1000 BIS 2000us ABDECKEN` |
| die Servoprogrammierung belegt ist oder bei KST/Chaservo und klassischen Hitec-Servos weder PD mini noch PROG 5V die Versorgung schalten kann | die Zeile des Endlagenlaufs |
| die Winkelquelle keinen Messwert gibt | `WINKELQUELLE: KEIN MESSWERT` |
| NULLPUNKT nicht genommen ist | `SETZE DEN NULLPUNKT MIT DEM RUDER IN NEUTRALLAGE` |

### Verfahren

- Die Lesesitzung hält das Servo 1000ms auf PULS MITTE und fordert SAFE
  an. VERBINDEN liest das Servo aus. Nur bei KST/Chaservo werden
  PULSE LOWER und PULSE UPPER bei Bedarf auf 1000,0us und 2000,0us
  geschrieben. Danach folgen VERLASSEN und die Anforderung von ARMED.
- Für eine Messung wird der Endpuls angefordert. Ab 500ms nach dem
  Befehl zählt der erste Winkel, der seit 500ms ruhig ist. Liegt bis
  3000ms kein solcher Winkel vor, endet der Lauf.
- Die Richtung einer Seite ist das Vorzeichen ihrer ersten Messung. Beide
  Seiten mit einem Vorzeichen beenden den Lauf mit `Endpoint einer Seite fehlt`.
- Eine Seite außerhalb der Toleranz bekommt Wert x Sollwert / gemessen,
  begrenzt auf den Bereich des Registers. Ein Wert, der auf den geltenden
  rundet, geht einen Zählwert zum Sollwert hin. Das erste Schreiben geht an
  LEFT ANGLE; bewegt sich die Seite nicht um die Hälfte der erwarteten
  Änderung, geht der Wert an RIGHT ANGLE und LEFT ANGLE zurück. Das
  gefundene Register bewegt diese Seite, das andere die andere Seite.
- Jedes Schreiben ist eine Sitzung: das Servo 1000ms auf PULS MITTE
  gehalten, SAFE, VERBINDEN, Schreiben und Kontrolllesen, VERLASSEN mit dem für
  diese Servofamilie nötigen Versorgungszyklus, ARMED und erneutes Messen
  der Seite. Hitec-Servos der D-Serie starten bereits beim Schreiben
  selbst neu. Die Datei mit den Werten vor dem Lauf wird vor dem
  ersten Schreiben gespeichert.
- BESTANDEN: beide Seiten innerhalb der Toleranz, jede nach dem letzten
  Schreiben gemessen. Eine Seite außerhalb nach 8 Schreibvorgängen beendet
  den Lauf mit `8 Schreibvorgänge je Seite`.

### Schutz

Der des Endlagenlaufs, in jeder Phase:

| Bedingung | Ende |
| --- | --- |
| ein Messwert im Abstand von 1ms ab 3000mA oder an der Messgrenze abgeschnitten | ABGEBROCHEN |
| Fenstermittel über 1000mA für 400ms vor dem Sollwert minus der Toleranz | NICHT BESTANDEN, `Ruder klemmt bei x°` |
| dasselbe am Sollwert oder darüber | ABGEBROCHEN, Blockieren |
| 200ms kein Fenster | ABGEBROCHEN |
| der Winkel über 300ms verloren oder eine zweite Lücke binnen 10s | ABGEBROCHEN, `Winkelquelle ausgefallen` |
| eine abgelehnte Sitzung oder ein Rücklesen, das vom Schreiben abweicht | ABGEBROCHEN |
| WINKELQUELLE, IMU-Achse oder IMU-Vorzeichen geändert: der Nullpunkt wird verworfen | ABGEBROCHEN |
| STOP, DISARM, SERVO verlassen, Messgerät oder Versorgung geändert | ABGEBROCHEN |

Jedes Ende lässt das Servo los, fordert SAFE an und schaltet den Ausgang
der Versorgung aus; ein Lesen in der ersten Sitzung, das kein Servo
findet, fordert statt des AUS ein LEAVE an, damit der Port zurückkommt.
Die zuletzt geschriebenen Werte bleiben im Servo. Bei KST/Chaservo
bietet ZURÜCKSPIELEN auf der Programmierseite die Werte vor dem Lauf an.
Bei Hitec lädst du die Backup-Datei unter DATEIEN der
Hitec-Servoprogrammierung und schreibst sie mit n WERTE SCHREIBEN.

### Ergebnisse

Der Bericht RUDDRnnn.TXT und die CSV RUDDRnnn.CSV, eine Zeile je Messung,
gehen nach USER/LOGS. RUDDR, nicht RUDDER: ein 8.3-Name fasst 5 Buchstaben
und 3 Ziffern. Der Bericht nennt das Servo, den Ausgang, die Winkelquelle
und das Messgerät, eingegebene Ausschläge, Zielwerte und Toleranz, die Endpoints davor und
danach mit der Seite, die jeder bewegt, die Schreibvorgänge und Sitzungen,
die Backup-Datei und jede Messung mit Registerwert, Winkel und der
Angabe, ob sie innerhalb der Toleranz liegt.

### Grenzen

- Das Modell ist linear: der Ausschlag am Endpuls gilt als proportional zum
  Wert des Endpoints ab dem Nullpunkt der Fläche. Ein nicht lineares Servo kann mehr Schreibvorgänge benötigen oder den
  Zielwert verfehlen. Der Lauf misst nach jedem Schreiben.
- Eingegeben werden Grad; ein Soll in mm an der Hinterkante nicht.
- Ein Servo der D-Serie hält bei 1000us nach dem Zielmodell aus
  `hitec_live.h` bei 76,9% des Wegs zu seinem Endpoint an; das ist nicht
  gemessen, der Lauf misst nach jedem Schreiben. Die Zuordnung der
  D-Serie an einem CCW-Servo ist nicht gemessen.

## Sweep und TEMPO

SWEEP auf der Seite SERVO fährt das Servo die Kurve von OPTIONEN > SWEEP ab.
TEMPO auf der rechten Karte begrenzt, wie schnell sich der Ausgang bewegen
darf. Ist TEMPO langsamer als die schnellste Änderung, die die Kurve
verlangt, bestimmt TEMPO die Bewegung statt der Kurve, und die Zeile von
TEMPO lautet in der Warnfarbe TEMPO BEGRENZT DEN SWEEP
([Bildschirme](Screens-de.md#servo)).

Die schnellste Änderung, mit f der Rate von SWEEP in Hz und A der
Amplitude in Grad (BEREICH des Wegs zu beiden Seiten von PULS MITTE):

| KURVE | Schnellste Änderung | Wo |
| --- | --- | --- |
| Rechteck | ein Sprung | bei jedem Wechsel des Endes |
| Sinus | 2 pi f A | durch die Mitte |
| Dreieck | 4 f A | überall |

TEMPO unter 100% erlaubt 3,6 Grad/s je Prozent: 36 Grad/s bei 10%,
356,4 Grad/s bei 99%. Bei 100% wird der Befehl nicht verlangsamt, und
nichts wird begrenzt. Ein Rechteck ist bei jedem TEMPO unter 100%
begrenzt. HALTEZEIT beeinflusst diese Grenze nicht: Sie verlängert die Pausen an
den Enden, nicht die Bewegung dazwischen. Die Zeile folgt den Einstellungen, vor dem Druck auf SWEEP und
während er läuft. `sweep_slew_limited()` in `shared/servo/servo_sweep.c`
entscheidet das, in den Befehlseinheiten, in denen der IOmcu die Rampe
fährt.

| Seite SWEEP | A | Schnellste Änderung | Verschwindet ab TEMPO |
| --- | ---: | ---: | ---: |
| Sinus, 0,5Hz, BEREICH 80%, WEG +/-90 Grad | 72 Grad | 226 Grad/s | 63% |
| Dreieck, 0,5Hz, BEREICH 80%, WEG +/-90 Grad | 72 Grad | 144 Grad/s | 40% |
| Rechteck, beliebig | beliebig | ein Sprung | 100% |

Die Zeile verschwindet, wenn TEMPO steigt oder TEMPO (die Rate) oder
BEREICH auf der Seite SWEEP sinkt.

Die Warnung vergleicht die angeforderte Kurve mit TEMPO. Ein Servo,
das langsamer als beide Vorgaben läuft, kann den tatsächlichen Sweep
ebenfalls begrenzen. Die Warnung verwendet jedoch keine Messung des
Hornwinkels.

PAUSE, der Sweep-Knopf während ein Sweep läuft, hält den Ausgang dort, wo
er gerade steht, und der Knopf heißt PAUSIERT, gefüllt in der Warnfarbe.
Ein Tippen auf PAUSIERT setzt den Sweep ab der Phase fort, an der er
angehalten wurde, bei einem IOmcu mit Protokoll 4.6; ein älterer
startet die Kurve von vorn. Ein angehaltener Sweep ist der Moment, TEMPO zu
erhöhen: die Pause bleibt, und das Fortsetzen läuft mit der neuen Rate.

## Voraussetzungen

Die Strommessung an der Servoschiene. Die Endlagensuche liest den
INA3221-Kanal, der das Ziel des Testers überwacht, verdrahtet und
bestückt in SETUP > SENSORS, und sonst nichts. Der Abgleich braucht einen Sensor für den gemeinsamen Strom beider
Ausgänge; dieser Sensor ist nicht bestückt. Der automatische Test liest den INA3221 oder den PD mini, oder läuft am
Netzteilmodell des Panels. Das Programmieren von LEFT ANGLE und RIGHT ANGLE
braucht den Programmierport ([Servo-Programmierer](ServoProgrammer-de.md)).

Der PWM-Treiber (Pulsweitenmodulation) des IOmcu erzeugt die Pulse;
[DShot und die Ausgangstreiber](DShot-de.md) beschreibt ihn.
Die geplanten Arbeiten stehen in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
