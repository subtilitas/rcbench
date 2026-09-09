# DShot und die Output-Treiber

<sub>[English](DShot.md) · **Deutsch**</sub>

Was der Coprozessor auf einen Ausgangspin legt und was zurückkommt. Vier
Treiber: Servo-PWM (Pulse-Width Modulation), PPM (Pulse-Position Modulation),
DShot und bidirektionales DShot.

Die Regeln, denen jeder Output folgt — Weg, Rolle, Ruhelage, Slew, Begrenzung,
Arming und das Silence-Timeout — stehen in `shared/outputs/` und gelten für alle
vier gleich. Diese Seite handelt vom Signal auf der Leitung.

## Welche Platine

Es wird mehr als einen Koprozessor geben. Der, auf dem das Bring-up läuft, ist
ein Modul im Pico-Formfaktor, von Hand verkabelt; eine spätere Platine trägt
ihre Outputs auf Pins, die gelötet und nicht gewählt sind. Sie haben nicht
dieselben Pins, und ein Pin-Index auf der einen bedeutet auf der anderen einen
anderen Pin — oder keinen.

Der Koprozessor sagt deshalb im Hardware-Register der
[Identity-Page](Link-de.md#page-map), welche Platine er ist, und das Panel
bietet die Pins dieser Platine an und keine anderen. Ein Platinenwechsel
verwirft jede Auswahl, denn ein Pin-Index auf der einen Platine bedeutet auf
der anderen einen anderen Pin.

Eine Platine, für die das Panel einen Katalog hat, benutzt diesen Katalog. Ihn
hat jemand gelesen, er benennt das genaue Signal, das jeden reservierten Pin
hält, und er ändert sich nicht unter einem laufenden Prüfstand.

Eine Platine, von der das Panel noch nie gehört hat, beschreibt ihre eigenen
Pins auf der [Catalogue-Page](Link-de.md#page-map): welche GPIOs sie
herausführt, die daneben aufgedruckte Pad-Nummer und was die hält, die ein
Output nicht haben darf. Ein Koprozessor, der neuer ist als das Panel, ist so
benutzbar statt leer.

Eine Platine, die auch die [Shape-Page](Link-de.md#page-map) bedient, sagt, wo
ihre Pads liegen — der Umriss, das Raster und die Ecke, an der Pad 1 sitzt —,
sodass das Panel sie zeichnen und nicht nur auflisten kann. Nur ein Bild
braucht das: eine Platine, die nicht antwortet, wird aus ihrem Katalog benutzt
und einfach nicht gezeichnet. Ein aus einer geratenen Form gezeichnetes Bild
zeigte mit derselben Überzeugung auf den falschen wie auf den richtigen Pad —
auf einem Bildschirm, dessen Aufgabe „finde diesen Pad auf der Platine vor
dir“ ist, ist das der Fehler, den es zu vermeiden gilt.

Was eine Platine nicht kann, ist einen ihrer Pins sicher machen. Der
Koprozessor reserviert seinen eigenen Satz an seinem eigenen Ende, was die
Catalogue-Page auch sagt — ein falscher Katalog kostet also einen Pin und
nicht die Safety-Leitung. Eine Platine, die sich weder beschreibt noch bekannt
ist, bietet gar nichts an: eine Pin-Belegung zu raten ist der Weg, auf dem ein
Output auf der Safety-Leitung landet.

Eine Platine mit gelöteten Outputs wird gezeigt, nicht angeboten. Der
Koprozessor konfiguriert sich selbst, und das Panel zeigt, was es zurückliest,
ohne es bearbeiten zu lassen: ein Bildschirm, der Pins neu bindet, die an
keinem Stecker liegen, böte eine Wahl an, die die Platine nicht einlösen kann.

## Welcher Pin

Auf einer von Hand verkabelten Platine ist kein Ausgangspin in der Firmware
festgelegt. Ein Slot auf der
[OUTPUTS-Page](Link-de.md#page-map) trägt seine Pinnummer, der Pin ist also eine
Einstellung und kein Build.

Der Coprozessor verweigert einen Pin, den er nicht treiben darf: GP3 (die
Safety-Heartbeat-Leitung), GP8 bis GP12 (SPI (Serial Peripheral Interface) und
Interrupt des CAN-Controllers (Controller Area Network)) und jede Nummer über
dem letzten GPIO (General-Purpose Input/Output), den das Bauteil hat — 29 beim
RP2350A des Bring-up-Moduls, 47 beim RP2350B, den die endgültige Platine
braucht. Ein verweigerter Slot bleibt ungebunden. Die Page liest weiterhin
zurück, was gefordert wurde, und kein Register auf ihr sagt, ob ein Slot
gebunden ist: ein ungebundener Slot liest sich genau wie ein treibender. Beim
Bediener kommt eine Leitung an, die sich nicht bewegt.

Ein PIO-Block (Programmable Input/Output) adressiert 32 Pins ab Basis 0 oder 16,
festgelegt, solange der Block ein Programm hält. Welcher Block einen bestimmten
Pin erreicht, steht daher erst fest, wenn der Pin eintrifft, und der Block wird
beim Binden des Slots gewählt. Ein Pin, den kein freier Block erreicht, wird wie
jeder andere verweigert.

## Servo-PWM

Hardware-PWM, kein PIO: das Bauteil hat 12 Slices und 24 Kanäle, und ein
Servopuls ist genau das, was ein Slice erzeugt. Der Zähler läuft mit 1 MHz, ein
Puls ist also eine Anzahl Mikrosekunden und die Frame-Dauer 1.000.000 geteilt
durch die Rate.

| | |
| --- | --- |
| Frame Rate | 40 bis 400 Hz |
| Puls | 500 bis 2500 µs, außerhalb verweigert |
| Auflösung | 1 µs |

Ein Slice sind zwei Kanäle an einem Zähler, zwei Pins auf demselben Slice laufen
also mit derselben Frame Rate. Eine zweite Bindung, die auf einem belegten Slice
eine andere Rate verlangt, wird verweigert statt umgestellt: Umstellen würde
einen Output verschieben, den niemand angefasst hat.

GPIO-Nummern falten sich auf die 12 Slices. GP0 bis GP31 nehmen Slice
(Pin / 2) modulo 8, GP32 bis GP47 nehmen Slice 8 + (Pin / 2) modulo 4, und der
Kanal ist in beiden Bereichen das unterste Bit der Pinnummer. Zwei Pins mit
Abstand 16 unterhalb von GP32 sind damit derselbe Kanal desselben Slice,
oberhalb von GP32 ist der Abstand 8. Ein Kanal ist ein Compare-Register und ein
Compare-Register ist eine Pulsweite, der zweite Pin eines solchen Paars wird
also verweigert statt gebunden: Ihn zu binden würde ihn auf die Pulsweite des
ersten Pins legen, und eine Leitung folgte der anderen, ohne dass ein
Bildschirm es sagt.

Sechs dieser Paare sind auf dem Header, den der Koprozessor anbietet, beide
frei: GP0 und GP16, GP1 und GP17, GP2 und GP18, GP4 und GP20, GP5 und GP21,
GP6 und GP22. Jeder Pin eines Paars ist benutzbar, beide zusammen nicht.

## PPM

Ein Pin, bis zu acht Kanäle. Ein Frame ist eine Folge von 300-µs-Marks; die Zeit
vom Beginn eines Marks bis zum Beginn des nächsten ist die Pulsweite eines
Kanals. Nach dem letzten Kanal folgen ein abschließender Mark und die Sync-Lücke,
und diese Lücke sagt einem Empfänger, wo der nächste Frame beginnt.

Kanalzahl und Frame Rate sind nicht unabhängig. Acht Kanäle zu 2000 µs
verbrauchen bereits 16 ms eines 22,5-ms-Frames. Der Prüfstand verweigert einen
Frame, dessen Sync-Lücke unter 3000 µs fiele — die kürzeste Lücke, die kein
Empfänger als Kanal lesen kann, denn die Kanalobergrenze liegt bei 2500 µs.

Der kürzeste Frame, der n Kanäle bei jedem denkbaren Befehl trägt, ist

    n × 2500 µs + 300 µs + 3000 µs

also 23,3 ms für acht Kanäle — acht Kanäle bei 50 Hz werden verweigert, acht bei
40 Hz nicht. Die Verweigerung erfolgt beim Konfigurieren des Slots, nicht erst,
wenn die Knüppel das Ende ihres Wegs erreichen.

Die Polarität ist eine Pad-Invertierung, kein anderes Programm.

Der Frame wird von einem Paar DMA-Kanälen (Direct Memory Access) abgespielt, die
sich gegenseitig neu auslösen; der Prozessor liegt damit nicht im Zeitpfad. Der
Frame-Puffer wird während des Abspielens an Ort und Stelle überschrieben: ein
mitten im Frame geänderter Kanal wirkt im nächsten Frame, der Rest dieses Frames
trägt die vorherigen Werte.

## DShot

Sechzehn Bit: elf Wert, eines fordert Telemetrie auf der separaten seriellen
Leitung an, vier Prüfsumme. Höchstwertiges Bit zuerst. Es gibt kein Startbit und
kein Idle-Muster — die Bitdauer ist die gesamte Synchronisation, beide Seiten
einigen sich also per Konfiguration auf die Rate.

| Bit | High für |
| --- | --- |
| 0 | 37,5 % der Bitdauer |
| 1 | 75 % |

Die State Machine läuft mit dem Achtfachen der Bitrate, weil acht die kleinste
Zyklenzahl ist, die beide Anteile ganzzahlig macht. DShot600 sind damit 4,8 MHz.

Der Wert ist nicht über seinen ganzen Bereich ein Gasbefehl:

| Wert | Bedeutung |
| ---: | --- |
| 0 | Motor Stop |
| 1 bis 47 | Commands: Beeps, Drehrichtung, 3D-Mode, Save Settings, Extended Telemetry |
| 48 bis 2047 | Gas |

Ein Command muss zehnmal wiederholt werden, bevor ein ESC (Electronic Speed
Controller) darauf reagiert; einmal gesendet bewirkt es gar nichts, was genau
wie ein nicht funktionierender Treiber aussieht.

Der Prüfstand bildet den Weg so ab, dass ein Befehl von null Motor Stop ist und
alles darüber in 48 bis 2047 landet. Ein Treiber, der den Weg direkt auf 0 bis
2047 abbildete, sendete auf dem Weg vom Leerlauf nach oben Beeps und
Drehrichtungswechsel.

Frames gehen mit 1 kHz hinaus. Diese Rate steht nicht auf der Leitung: das
Ratenfeld der OUTPUTS-Page ist bei DShot eine Bitrate und keine Frame Rate, der
Prüfstand wählt sie also. 1 kHz ist das Zwanzigfache der Poll-Rate des Panels
und liegt weit innerhalb des Signal-Timeouts jedes ESC.

**Während der Prüfstand nicht treibt, wird nichts gesendet.** Keine Frames heißt
keine Flanken, und ein ESC stoppt nach seinem eigenen Timeout auf Stille. Statt
dessen ausdrückliche Nullen zu senden hielte ihn armed und wartend, und so soll
ein disarmter Prüfstand von der ESC-Seite aus nicht aussehen.

## Bidirektionales DShot

Derselbe Frame mit invertierter Leitung und komplementierter Prüfsumme, sodass
ein auf ein Protokoll eingestellter ESC das andere ignoriert, statt darauf zu
reagieren. Es ist eine eigene Treibernummer auf der
[OUTPUTS-Page](Link-de.md#page-map) und kein Flag: die beiden sind verschiedene
Leitungen, nicht eine Leitung mit einer Einstellung.

Nach jedem Frame gibt der Coprozessor die Leitung frei, und der ESC antwortet
darauf, etwa 30 µs später, mit 21 Bit bei fünf Vierteln der DShot-Rate. Der
Turnaround geschieht im PIO-Block — der Transmitter gibt den Pin frei und setzt
ein Flag, der Receiver startet auf dieses Flag — denn 30 µs sind keine Frist,
die eine Schleife einhalten kann.

Die Antwort ist GCR-codiert (Group-Coded Recording). Jedes Nibble eines 16-Bit-
Werts wird zu einem Fünf-Bit-Codewort, gewählt so, dass die Leitung häufig den
Pegel wechselt, und die Leitung ist differentiell: jedes Bit ist das vorige
exklusiv-oder die Daten. Rückgängig macht das `x ^ (x >> 1)`. Das zuerst
gesendete Bit ist eine Null, der Burst beginnt also mit einer fallenden Flanke
aus dem Idle-Pegel und ist überhaupt auffindbar.

Die 16 Bit sind zwölf Nutzdaten und vier Prüfsumme, und die Prüfsumme ist das
Komplement der eines ausgehenden Frames. Jeder Ein-Bit-Fehler wird gefangen: die
Prüfung ist das Exklusiv-Oder der vier Nibbles, und ein gekipptes Bit verändert
genau eines davon.

Die Nutzdaten sind eine Periode, keine Drehzahl:

    period_us = Mantisse (9 Bit) << Exponent (3 Bit)
    elektrische rpm = 60.000.000 / period_us

Nutzdaten von 0x0FFF heißen, dass der Motor sich nicht dreht.

### Die Antwort wird abgetastet, nicht gemessen

Der Coprozessor tastet die Leitung mit dem Fünffachen der Bitrate der Antwort ab
und decodiert in Software. Die Bitrate des ESC stammt von seinem eigenen Quarz,
ein oder zwei Prozent neben der des Prüfstands, und 21 Bit reichen aus, damit
das einen festen Abtastpunkt aus einem Bit hinausschiebt — der Decoder setzt
seine Phase deshalb bei jeder Flanke zurück, so wie ein UART (Universal
Asynchronous Receiver-Transmitter) auf einem Startbit resynchronisiert, und
liest jedes Bit aus der Mitte seines Fensters. `test_dshot_telem` prüft das
gegen eine Antwort mit 4,75 und 5,25 Abtastungen je Bit.

### Polzahl

Ein ESC meldet elektrische Perioden und weiß nicht, woran er angeschraubt ist;
mechanische rpm (Revolutions per Minute) brauchen also die Magnetzahl des
Motors. Das ist die eine Zahl, die die Leitung nicht trägt. Das Panel sendet sie
aus der Einstellung `Motor poles`, sobald ein Coprozessor zu antworten beginnt,
erneut bei jeder Änderung der Einstellung und erneut vor dem Schreibvorgang,
der scharfschaltet, sofern eine Änderung noch offen ist, auf der
[CONTROL-Page](Link-de.md#page-map). Ein
Schreibvorgang, den niemand beantwortet, bleibt offen und geht beim nächsten
50-ms-Poll erneut hinaus; einen, den der Coprozessor ablehnt, wiederholt sie
nicht, er wartet auf die nächste Änderung oder die nächste Link-up-Flanke.

Bis sie eintrifft, meldet der Coprozessor überhaupt keine Drehzahl. Eine aus
einer geratenen Polzahl abgeleitete Drehzahl ist eine plausible Zahl ohne
Kennzeichnung, dass sie falsch ist, und das ist schlimmer als ein leeres Feld.

Das gilt nur für null. Jede gerade Zahl zwischen 2 und 42 wird angenommen und
setzt das Gültig-Bit der Drehzahl -- eine ungerade wird abgelehnt, denn ein
Tippfehler, der bis zum anderen Ende käme, würde eine plausible falsche
Drehzahl auf den Bildschirm bringen. Ein Coprozessor mit einer veralteten Zahl
meldet
also die tatsächliche Drehzahl mal die Polzahl des Motors geteilt durch die
gehaltene Zahl. Ein 12-poliger Motor an einem Prüfstand, der noch 14 hält,
liest 14,3 % zu wenig; bleibt 12 stehen und der Motor hat 14 Pole, liest er
16,7 % zu viel. Die Richtung folgt der Änderung, und die Enden des Bereichs
sind in beide Richtungen ein Faktor 21: ein 42-poliger Motor, der als 2
umgerechnet wird, liest das Einundzwanzigfache seiner Drehzahl, ein 2-poliger
als 42 ein Einundzwanzigstel. Zu beachten ist die gewöhnliche Änderung, denn
einen Faktor 21 sieht man sofort und 14 % nicht.

### Extended Telemetry

Ein ESC, dem Command 13 gesendet wurde, schiebt zwischen die Drehzahl-Frames
Frames für Temperatur, Spannung, Strom, Stress und Status, markiert durch das
oberste Nibble der Nutzdaten.

Der Prüfstand sendet dieses Command bei jeder Flanke ins Treiben, zehn Frames
bei 1.000 Hz, also in den ersten 10 ms eines Scharfschaltens. Quittiert wird es
nicht, danach werden Antworten als Extended Telemetry gelesen, ob der ESC
zugestimmt hat oder nicht.

Jeder der zehn Frames trägt das Telemetry-Bit gesetzt. Bei einem Wert von 1
bis 47 markiert dieses Bit den Frame für die BLHeli_S-Familie (Bluejay) als
Command: sie verwirft ein Command mit gelöschtem Telemetry-Bit und setzt dabei
ihren Wiederholungszähler zurück, sodass zehn Frames ohne das Bit nie die
sechs Wiederholungen erreichen, die ihr Command-Handler zählt. AM32 hat diese
Sperre nicht und nimmt beide Frames an. An einem bidirektionalen Pin lautet
der Frame mit dem Bit `0x01B5` und ohne es `0x01A4` -- andere Nutzdaten und
ein anderes Prüfsummen-Nibble. `test_dshot_frame` hält dieses Wort fest. Die
zweite Bedeutung des Bits ist eine Anforderung auf der separaten seriellen
Telemetry-Leitung, die auf diesem Prüfstand niemand liest.

Aus den Bits allein sind die beiden nicht zu unterscheiden: das Nibble, das
einen Extended-Frame markiert, ist in einem Drehzahl-Frame ein gewöhnlicher
Exponent mit Mantisse, und nur ein ESC mit eingeschalteter Extended Telemetry
garantiert die Normalisierung, die sie trennt. Der Decoder nimmt den Modus
deshalb als Argument entgegen, statt ihn zu erschließen. Ein ESC, der Command
13 ignoriert **und** nicht normalisiert, bekommt deshalb einzelne
Drehzahl-Frames als Temperatur oder Spannung gelesen -- ungemessen, und der
Grund, die ersten Anzeigen an einem neuen ESC zu prüfen.

## Was auf der Leitung nicht bestätigt ist

Die Implementierung ist aus der veröffentlichten Beschreibung des Protokolls
geschrieben. Alles Folgende wird von der Host-Suite gegen Frames geprüft, die
derselbe Code baut; das belegt die Arithmetik und nicht die Leitung.

Eines steht nicht auf dieser Liste. **Einfaches DShot hat auf dem
Aufbau-Prüfstand einen Motor vom Panel aus laufen lassen**, damit liegt sein
Bit-Timing innerhalb der Toleranz dieses einen ESC -- gegen einen ESC, ohne
Messgerät am Pin, also ein Indiz und keine Messung. Nichts hiervon war an
einem Oszilloskop, und jeder bidirektionale Punkt unten ist in jeder Hinsicht
unbestätigt:

- die Antwortrate von fünf Vierteln der DShot-Rate;
- die Konvention für das führende Bit des Group Code;
- die Turnaround-Verzögerung, und ob 30 µs das ist, was ein ESC tatsächlich
  wartet;
- die Frame-Typen der Extended Telemetry und ihre Einheiten;
- ob der Hinweis "mindestens 35 ms warten" in der Spezifikation zu Command 13
  oder zu Command 12 (Save Settings) gehört. Die Tabelle lässt beide Lesarten
  zu, und der Prüfstand wartet in keinem Fall: das Throttle folgt der zehnten
  Wiederholung beim nächsten 1-ms-Takt. Das klärt der Text der Spezifikation
  und keine Platine;
- jedes Bit-Timing, gegen die Toleranz eines echten ESC statt gegen die
  Spezifikation.

## Wo der Code liegt

| | |
| --- | --- |
| Frames, Group Code, Prüfsumme, Drehzahl, Sampler | `shared/dshot/` |
| PPM-Frame-Layout | `shared/ppm/` |
| Weg, Rolle, Ruhelage, Slew, Arming, Timeout | `shared/outputs/` |
| Welchen PWM-Slice und -Kanal ein GPIO erreicht | `shared/outputs/out_pwm_map.c` |
| PIO-Programme | `firmware/iomcu/src/ppm.pio`, `dshot.pio` |
| Backends für Hardware-PWM, PPM und DShot | `firmware/iomcu/src/out_*.c` |
| Bindung der Bank an die Pins | `firmware/iomcu/src/outputs_hw.c` |
