# DShot und die Output-Treiber

<sub>[English](DShot.md) · **Deutsch**</sub>

Der IOmcu hat vier Ausgangstreiber: Servo-PWM (Pulse-Width Modulation),
PPM (Pulse-Position Modulation), DShot und bidirektionales DShot. Diese
Seite beschreibt ihre Ausgangssignale und die empfangenen Antworten.

Die gemeinsamen Regeln stehen in `shared/outputs/`: Weg, Rolle,
Ruhelage, Slew, Begrenzung, Arming und Timeout bei ausbleibendem
Link-Verkehr. Sie gelten für alle vier Ausgangstreiber. Hier geht es
um die Signale auf der Leitung.

## Welche Platine

IOmcu-Platinen haben unterschiedliche Pin-Belegungen. Für die
Inbetriebnahme dient ein von Hand verkabeltes Modul im Pico-Formfaktor.
Bei Platinen mit verlöteten Ausgängen sind die Zuordnungen fest. Ein
Pin-Index kann auf einer anderen Platine einen anderen Pin bezeichnen
oder dort gar nicht vorhanden sein.

Der IOmcu sagt deshalb im Hardware-Register der
[Identity-Page](Link-de.md#page-map), welche Platine er ist, und das Panel
bietet die Pins dieser Platine an und keine anderen. Ein Platinenwechsel
verwirft jede Auswahl, denn ein Pin-Index auf der einen Platine bedeutet auf
der anderen einen anderen Pin.

Für eine bekannte Platine nutzt das Panel den eingebauten Katalog.
Dieser geprüfte Katalog benennt das Signal an jedem reservierten Pin
und bleibt während des Betriebs unverändert.

Eine unbekannte Platine beschreibt ihre Pins auf der
[Catalogue-Page](Link-de.md#page-map): die herausgeführten GPIOs, die
aufgedruckten Pad-Nummern und die Belegung der für Ausgänge gesperrten
Pins. So kann das Panel auch einen IOmcu verwenden, der neuer als sein
eingebauter Katalog ist.

Die [Shape-Page](Link-de.md#page-map) ergänzt den Platinenumriss, das
Pad-Raster und die Ecke mit Pad 1. Mit diesen Angaben kann das Panel
die Platine zeichnen. Antwortet die Shape-Page nicht, verwendet es nur
den Katalog und zeigt keine Zeichnung. Eine geratene Form könnte auf
das falsche Pad der tatsächlichen Platine verweisen.

Was eine Platine nicht kann, ist einen ihrer Pins sicher machen. Der
IOmcu reserviert seinen eigenen Satz an seinem eigenen Ende, was die
Catalogue-Page auch sagt — ein falscher Katalog kostet also einen Pin und
nicht die Safety-Leitung. Eine Platine, die sich weder beschreibt noch bekannt
ist, bietet gar nichts an: eine Pin-Belegung zu raten ist der Weg, auf dem ein
Output auf der Safety-Leitung landet.

Bei einer Platine mit verlöteten Ausgängen konfiguriert der IOmcu die
festen Zuordnungen selbst. Das Panel zeigt die zurückgelesenen Werte
an, erlaubt aber keine Änderungen, die die Platine nicht unterstützt.

## Welcher Pin

Auf einer von Hand verkabelten Platine ist kein Ausgangspin in der Firmware
festgelegt. Ein Slot auf der
[OUTPUTS-Page](Link-de.md#page-map) trägt seine Pinnummer, der Pin ist also eine
Einstellung und kein Build.

Der IOmcu verweigert einen Pin, den er nicht treiben darf: GPIO 3 (die
Safety-Heartbeat-Leitung), GPIO 8 bis GPIO 12 (SPI (Serial Peripheral Interface) und
Interrupt des CAN-Controllers (Controller Area Network)), die vier Pins, die das
Bring-up-Modul belegt und nicht herausführt — GPIO 23 (der MODE-Pin des
Wandlers auf dem Modul), GPIO 24 (VBUS-Messung), GPIO 25 (LED1) und GPIO 29
(VSYS-Messung) — und jede Nummer über dem letzten GPIO (General-Purpose
Input/Output), den das Modul hat: GPIO 30 und aufwärts, 29 ist der höchste Pin des
RP2350A. Die Menge gehört zum Modul und folgt nicht der SDK-Board-Datei, für die
das Image gebaut wird. Die PD-mini-Pins der SUPPLY-Page werden an dieselbe Menge
gebunden. Ein abgelehnter Slot bleibt ohne Zuordnung. Die Page liefert weiterhin
die angeforderten Werte zurück. Keines ihrer Register zeigt, ob die
Zuordnung erfolgreich war. Ein nicht zugeordneter Slot liest sich
deshalb wie ein aktiver, obwohl seine Leitung kein Signal ausgibt.

Ein PIO-Block (Programmable Input/Output) adressiert 32 Pins ab Basis
0 oder 16. Die Basis bleibt fest, solange der Block ein Programm
enthält. Der Treiber wählt den Block deshalb erst bei der Zuordnung
des Slots, wenn der Pin bekannt ist. Erreicht kein freier Block den
Pin, wird die Zuordnung abgelehnt.

## Servo-PWM

Hardware-PWM, kein PIO: das Bauteil hat 12 Slices und 24 Kanäle, und ein
Servopuls ist genau das, was ein Slice erzeugt. Der Zähler läuft mit 1MHz, ein
Puls ist also eine Anzahl Mikrosekunden und die Frame-Dauer 1.000.000 geteilt
durch die Rate.

| | |
| --- | --- |
| Frame Rate | 40 bis 560Hz |
| Puls | 400 bis 2500µs und höchstens die Periode minus 1µs, außerhalb verweigert |
| Auflösung | 1µs |

Ein Puls ist nie so lang wie sein Frame. Der längste, den ein Frame trägt, ist
die Periode minus 1µs, abgerundet: der letzte Zählerstand, der den Pin 1µs
auf Low lässt.

| Rate | Periode | Längster Puls |
| ---: | ---: | ---: |
| 560Hz | 1785µs | 1784µs |
| 500Hz | 2000µs | 1999µs |
| 400Hz | 2500µs | 2499µs |
| 399Hz und darunter | 2506µs und mehr | 2500µs, die Obergrenze |

Ein Schreiben auf CHAN_CFG, OUTPUTS, BIND, SERVO oder PROG, das einem
PWM-Ausgang einen längeren Endpunkt hinterließe, wird mit BAD_VALUE
verweigert, und nichts ändert sich. Beurteilt wird die Rate, mit der der
Ausgang läuft: die von SERVO für eine Surface, solange sie nicht 0 ist, sonst
die eigene des Slots. Ein Standardservo mit 1000 bis 2000µs nimmt also
499Hz und wird bei 500Hz und 560Hz verweigert. Ein gespeicherter Slot,
dessen Endpunkt nicht zu seiner eigenen Rate passt, startet ungebunden. Die
Regel ist `out_pwm_pulse_max()` und `outputs_pulse_check()`; [der
Link](Link-de.md#ein-puls-nicht-länger-als-sein-frame) hat die Seite der Pages.

Ein Slice enthält zwei Kanäle mit einem gemeinsamen Zähler. Beide Pins
verwenden deshalb dieselbe Pulsrate. Eine weitere Zuordnung mit einer
anderen Rate wird auf einem belegten Slice abgelehnt. Die Rate wird
nicht geändert, weil dies den bereits aktiven Ausgang beeinflussen
würde.

Die SERVO-Page setzt eine Frame Rate für ihr Ziel. Mit den Standard-Servoports
als `TARGET` von PROG ist das jeder PWM-Ausgang, dessen erster Kanal die Rolle
surface hat, über die Rate, die die Bindung dem Slot gibt; mit einem
Programmierport als Ziel ist es nur der Pin dieses Ports, und jeder Slot behält
die Rate, die seine Bindung ihm gibt (Protokoll 4.19,
[der Link](Link-de.md#die-frame-rate-des-ziels)). Sie wird
mit BAD_VALUE verweigert, wenn sie eine Surface von der Rate des Ausgangs neben
ihr auf demselben Slice wegbewegen würde, und solange sie eine Rate hält, wird
auch ein Schreiben auf CHAN_CFG oder OUTPUTS verweigert, das dasselbe täte --
eine Surface, die Throttle wird, ein Throttle, der neben eine Surface gebunden
wird. Sie wird nicht gespeichert: Nach einem Neustart läuft jeder Slot mit
seiner eigenen Rate.

GPIO-Nummern falten sich auf die 12 Slices. GPIO 0 bis GPIO 31 nehmen Slice
(Pin / 2) modulo 8, GPIO 32 bis GPIO 47 nehmen Slice 8 + (Pin / 2) modulo 4, und der
Kanal ist in beiden Bereichen das unterste Bit der Pinnummer. Zwei Pins mit
Abstand 16 unterhalb von GPIO 32 sind damit derselbe Kanal desselben Slice,
oberhalb von GPIO 32 ist der Abstand 8. Ein Kanal ist ein Compare-Register und ein
Compare-Register ist eine Pulsweite, der zweite Pin eines solchen Paars wird
also verweigert statt gebunden: Ihn zu binden würde ihn auf die Pulsweite des
ersten Pins legen, und eine Leitung folgte der anderen, ohne dass ein
Bildschirm es sagt.

Sechs dieser Paare sind auf dem Header, den der IOmcu anbietet, beide
frei: GPIO 0 und GPIO 16, GPIO 1 und GPIO 17, GPIO 2 und GPIO 18, GPIO 4 und GPIO 20, GPIO 5 und GPIO 21,
GPIO 6 und GPIO 22. Jeder Pin eines Paars ist benutzbar, beide zusammen nicht.

## PPM

Ein Pin, bis zu acht Kanäle. Ein Frame ist eine Folge von 300-µs-Marks; die Zeit
vom Beginn eines Marks bis zum Beginn des nächsten ist die Pulsweite eines
Kanals. Nach dem letzten Kanal folgen ein abschließender Mark und die Sync-Lücke,
und diese Lücke sagt einem Empfänger, wo der nächste Frame beginnt.

Kanalzahl und Frame Rate sind nicht unabhängig. Acht Kanäle zu 2000µs
verbrauchen bereits 16ms eines 22,5-ms-Frames. Der Prüfstand verweigert einen
Frame, dessen Sync-Lücke unter 3000µs fiele — die kürzeste Lücke, die kein
Empfänger als Kanal lesen kann, denn die Kanalobergrenze liegt bei 2500µs.

Der kürzeste Frame, der n Kanäle bei jedem denkbaren Befehl trägt, ist

    n × 2500µs + 300µs + 3000µs

also 23,3ms für acht Kanäle — acht Kanäle bei 50Hz werden verweigert, acht bei
40Hz nicht. Die Verweigerung erfolgt beim Konfigurieren des Slots, nicht erst,
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
| 0 | 37,5% der Bitdauer |
| 1 | 75% |

Die State Machine läuft mit dem Achtfachen der Bitrate, weil acht die kleinste
Zyklenzahl ist, die beide Anteile ganzzahlig macht. DShot600 sind damit 4,8MHz.

Der Wert ist nicht über seinen ganzen Bereich ein Gasbefehl:

| Wert | Bedeutung |
| ---: | --- |
| 0 | Motor Stop |
| 1 bis 47 | Commands: Beeps, Drehrichtung, 3D-Mode, Save Settings, Extended Telemetry |
| 48 bis 2047 | Gas |

Ein Kommando muss zehnmal wiederholt werden, bevor ein ESC (Electronic
Speed Controller) darauf reagiert. Ein einmal gesendetes Kommando bleibt
ohne Wirkung und kann deshalb wie ein Treiberfehler wirken.

Der Prüfstand bildet den Weg so ab, dass ein Befehl von null Motor Stop ist und
alles darüber in 48 bis 2047 landet. Ein Treiber, der den Weg direkt auf 0 bis
2047 abbildete, sendete auf dem Weg vom Leerlauf nach oben Beeps und
Drehrichtungswechsel. Ein Befehl über der Spanne von 1000 wird auf null, Motor
Stop, abgebildet, nicht auf volles Gas.

`out_dshot_bind()` gibt false zurück, wenn der Pin bereits mit einer
anderen Rate oder einem anderen Modus zugeordnet ist. Der bisherige
Ausgang läuft weiter. Gib den Pin vor einer Zuordnung mit anderen
Einstellungen frei.

Frames gehen mit 1kHz hinaus. Diese Rate steht nicht auf der Leitung: das
Ratenfeld der OUTPUTS-Page ist bei DShot eine Bitrate und keine Frame Rate, der
Prüfstand wählt sie also. 1kHz ist das Zwanzigfache der Poll-Rate des Panels
und liegt weit innerhalb des Signal-Timeouts jedes ESC.

**Während der Prüfstand nicht treibt, wird nichts gesendet.** Keine Frames heißt
keine Flanken, und ein ESC stoppt nach seinem eigenen Timeout auf Stille. Stattdessen ausdrückliche Nullen zu senden hielte ihn armed und wartend, und so soll
ein Prüfstand in SAFE von der ESC-Seite aus nicht aussehen.

## Bidirektionales DShot

Derselbe Frame mit invertierter Leitung und komplementierter Prüfsumme, sodass
ein auf ein Protokoll eingestellter ESC das andere ignoriert, statt darauf zu
reagieren. Es ist eine eigene Treibernummer auf der
[OUTPUTS-Page](Link-de.md#page-map) und kein Flag: die beiden sind verschiedene
Leitungen, nicht eine Leitung mit einer Einstellung.

Nach jedem Frame gibt der IOmcu die Leitung frei, und der ESC antwortet
darauf, etwa 30µs später, mit 21 Bit bei fünf Vierteln der DShot-Rate. Der
Turnaround geschieht im PIO-Block — der Transmitter gibt den Pin frei und setzt
ein Flag, der Receiver startet auf dieses Flag — denn 30µs sind keine Frist,
die eine Schleife einhalten kann.

Die Antwort ist GCR-codiert (Group-Coded Recording). Jedes Nibble eines
16-Bit-Werts wird zu einem Fünf-Bit-Codewort, das häufige Pegelwechsel
erzeugt. Die Codierung ist differenziell: Jedes Bit ergibt sich aus dem
vorigen Bit exklusiv-oder den Daten. `x ^ (x >> 1)` macht diese
Codierung rückgängig. Das erste gesendete Bit ist null. Die fallende
Flanke vom Ruhepegel markiert damit den Beginn des Bursts.

Die 16 Bit sind zwölf Nutzdaten und vier Prüfsumme, und die Prüfsumme ist das
Komplement der eines ausgehenden Frames. Jeder Ein-Bit-Fehler wird gefangen: die
Prüfung ist das Exklusiv-Oder der vier Nibbles, und ein gekipptes Bit verändert
genau eines davon.

Die Nutzdaten sind eine Periode, keine Drehzahl:

    period_us = Mantisse (9 Bit) << Exponent (3 Bit)
    elektrische rpm = 60.000.000 / period_us

Nutzdaten von 0x0FFF heißen, dass der Motor sich nicht dreht.

### Die Invertierung sitzt am Pad, und sie kommt zuletzt

Die Leitung wird vom Output-Override des Pads invertiert und nicht vom
PIO-Programm. Derselbe Pin liest also den wahren Pegel der Leitung, sobald der
Transmitter sie losgelassen hat, und der Pull-up hält dazwischen den Idle-Pegel.

Der Override wird **nach** der Initialisierung der State Machine gesetzt, nie
davor. Diese Initialisierung beginnt mit `pio_gpio_init()`, das
`gpio_set_function()` ist, und das weist das ganze Steuerregister des Pads zu,
statt es zu maskieren. Der Override liegt in diesem Register und geht verloren.
Der Pull-up nicht, denn Pulls liegen im Pad-Block; ein Pin, der seine
Invertierung verloren hat, ruht also weiterhin high und sieht wie ein
Protokollfehler aus und nicht wie ein Pin-Fehler.

Bei der Zuordnung wird das Register zurückgelesen. Ist die benötigte
Polarität nicht gesetzt, wird die Zuordnung abgelehnt. Die Reihenfolge
der Initialisierung ist deshalb entscheidend; kein anderer Teil des
Treibers erkennt eine verlorene Invertierung.

### Die Antwort wird abgetastet, nicht gemessen

Der IOmcu tastet die Leitung mit dem Fünffachen der Antwort-Bitrate ab
und dekodiert die Daten in Software. Der ESC verwendet seinen eigenen
Quarz; seine Bitrate kann ein oder zwei Prozent von der des Prüfstands
abweichen. Über 21 Bit kann diese Abweichung einen festen Abtastpunkt
aus dem Bit verschieben. Der Dekoder setzt seine Phase deshalb bei
jeder Flanke zurück, ähnlich einem UART (Universal Asynchronous
Receiver-Transmitter) am Startbit, und liest jedes Bit in der Mitte
seines Fensters. `test_dshot_telem` prüft dies mit 4,75 und 5,25
Abtastungen je Bit.

### Polzahl

Ein ESC meldet elektrische Perioden und weiß nicht, woran er angeschraubt ist;
mechanische rpm (Revolutions per Minute) brauchen also die Magnetzahl des
Motors. Das ist die eine Zahl, die die Leitung nicht trägt. Das Panel sendet sie
aus der Einstellung `Motorpole`, sobald ein IOmcu zu antworten beginnt,
erneut bei jeder Änderung der Einstellung und erneut vor dem Schreibvorgang,
der auf ARMED schaltet, sofern eine Änderung noch offen ist, auf der
[CONTROL-Page](Link-de.md#page-map). Ein
Schreibvorgang, den niemand beantwortet, bleibt offen und geht beim nächsten
50-ms-Poll erneut hinaus; einen, den der IOmcu ablehnt, wiederholt sie
nicht, er wartet auf die nächste Änderung oder die nächste Link-up-Flanke.

Bis die Polzahl eintrifft, meldet der IOmcu keine Drehzahl. Das Feld
bleibt leer, weil eine geratene Polzahl eine plausible, aber falsche
Drehzahl ergeben könnte.

Das gilt nur für null. Jede gerade Zahl zwischen 2 und 42 wird angenommen und
setzt das Gültig-Bit der Drehzahl. Eine ungerade wird abgelehnt, denn ein
Tippfehler, der bis zum anderen Ende käme, würde eine plausible falsche
Drehzahl auf den Bildschirm bringen. Ein IOmcu mit einer veralteten Zahl
meldet
also die tatsächliche Drehzahl mal die Polzahl des Motors geteilt durch die
gehaltene Zahl. Ein 12-poliger Motor an einem Prüfstand, der noch 14 hält,
liest 14,3% zu wenig; bleibt 12 stehen und der Motor hat 14 Pole, liest er
16,7% zu viel. Die Richtung folgt der Änderung, und die Enden des Bereichs
sind in beide Richtungen ein Faktor 21: ein 42-poliger Motor, der als 2
umgerechnet wird, liest das Einundzwanzigfache seiner Drehzahl, ein 2-poliger
als 42 ein Einundzwanzigstel. Zu beachten ist die gewöhnliche Änderung, denn
einen Faktor 21 sieht man sofort und 14% nicht.

### Extended Telemetry

Ein ESC, dem Command 13 gesendet wurde, schiebt zwischen die Drehzahl-Frames
Frames für Temperatur, Spannung, Strom, Stress und Status, markiert durch das
oberste Nibble der Nutzdaten.

Der Prüfstand fragt mit Command 13 und fragt erneut, bis der ESC antwortet.
Eine Anfrage sind zehn Frames Command 13 hintereinander, 10ms bei 1.000Hz.
Eine Anfrage geht nur anstelle eines Frames mit Throttle null hinaus, sie
verzögert also nie ein Throttle, und Command 13 liegt im Command-Bereich, es
dreht sich also nichts, während sie hinausgeht:

- die erste Anfrage beginnt mit dem ersten Frame mit Throttle null eines
  Laufs;
- die nächste beginnt 500ms nach der letzten Wiederholung der vorigen,
  solange das Throttle auf null bleibt;
- ein Throttle über null geht in seinem eigenen Frame hinaus, und eine
  Anfrage, die es unterbricht, beginnt wieder bei ihrer ersten Wiederholung;
- das Fragen endet mit dem ersten Extended-Frame, der zurückkommt, oder nach
  10 Anfragen ohne einen;
- jeder Lauf fragt neu, denn Extended Telemetry ist eine Laufzeit-Einstellung,
  die ein ESC bei einem Neustart vergisst, und zwischen zwei Läufen kann ein
  anderer ESC angesteckt werden.

Das Wiederholen ist für ESCs mit AM32-Firmware. Ein ESC mit AM32 2.21 nimmt
ein Command nur an, solange er armed ist und der Motor steht, und er armt
sich nach 1s Throttle null selbst, gezählt ab dem ersten Frame, den er
hört (`Src/dshot.c:157`,
`Src/main.c:1353-1356`). Ein Prüfstand in SAFE sendet keine Frames, und
die AM32-Firmware startet nach 0,5s ohne Frame neu, wenn der ESC armed ist,
und nach 2s, wenn nicht. Jeder Lauf beginnt also mit einem ESC, der nicht
armed ist. Eine Anfrage nur in den ersten 10ms eines Laufs kommt an, bevor
der ESC sie annehmen kann. `test_dshot_edt` prüft den Ablauf gegen ein Modell
dieser Regeln: ein ESC mit AM32-Firmware, der aus der Stille bei Throttle null
startet, nimmt die dritte Anfrage an, 1,02s nach Beginn des Laufs. Die
Drehzahl hängt davon nicht ab: ein ESC mit AM32-Firmware beantwortet einen
bidirektionalen Frame mit einer Periode, ob gefragt wurde oder nicht.

Gelesen werden Antworten als Extended Telemetry ab der ersten Wiederholung der
ersten Anfrage. Ein ESC mit AM32-Firmware beantwortet Command 13 einmal, mit
einem Status-Frame
(`0xE00`) nach der sechsten Wiederholung, und als Drehzahl gelesen wäre dieser
Frame eine Periode von null und keine Antwort.

Jeder der zehn Frames trägt das Telemetry-Bit gesetzt. Bei einem Wert von 1
bis 47 markiert dieses Bit den Frame für die BLHeli_S-Firmware-Familie (Bluejay) als
Command: sie verwirft ein Command mit gelöschtem Telemetry-Bit und setzt dabei
ihren Wiederholungszähler zurück, sodass zehn Frames ohne das Bit nie die
sechs Wiederholungen erreichen, die ihr Command-Handler zählt. Die
AM32-Firmware hat diese Sperre nicht und nimmt beide Frames an. An einem bidirektionalen Pin lautet
der Frame mit dem Bit `0x01B5` und ohne es `0x01A4`: andere Nutzdaten und
ein anderes Prüfsummen-Nibble. `test_dshot_frame` hält dieses Wort fest. Die
zweite Bedeutung des Bits ist eine Anforderung auf der separaten seriellen
Telemetry-Leitung, die auf diesem Prüfstand niemand liest.

Aus den Bits allein sind die beiden nicht zu unterscheiden: das Nibble, das
einen Extended-Frame markiert, ist in einem Drehzahl-Frame ein gewöhnlicher
Exponent mit Mantisse, und nur ein ESC mit eingeschalteter Extended Telemetry
garantiert die Normalisierung, die sie trennt. Der Decoder nimmt den Modus
deshalb als Argument entgegen, statt ihn zu erschließen. Ein ESC, der Command
13 ignoriert **und** nicht normalisiert, bekommt deshalb einzelne
Drehzahl-Frames als Temperatur oder Spannung gelesen. Das ist ungemessen, und der
Grund, die ersten Anzeigen an einem neuen ESC zu prüfen.

Diese Telemetrie-Frames sind die einzige Quelle des IOmcu für
Temperatur, Spannung und Strom in der Prüfstandsanzeige; er hat kein
Messfrontend. Jeder Wert gilt 2000ms lang als aktuell, die Drehzahl
200ms. Extended-Frames treffen nur wenige Male je Sekunde ein, während
jeder Frame eine Periode zurückliefert. Die Leistung wird aus Spannung
und Strom berechnet. Ihre Anzeige bleibt leer, bis beide Werte
eingetroffen sind.

## Was auf der Leitung nicht bestätigt ist

Die Implementierung ist aus der veröffentlichten Beschreibung des Protokolls
geschrieben. Alles Folgende wird von der Host-Suite gegen Frames geprüft, die
derselbe Code baut; das belegt die Arithmetik und nicht die Leitung.

Die folgende Beobachtung stammt von einem ESC ohne Messgerät am Pin.
Sie ist ein Hinweis auf die Funktion, keine Messung des Leitungssignals.

- **Bidirektionales DShot600 gegen einen ESC** (#172): ein Skystars 35A mit AM32 2.21, Target `JUPITER_35A_F421` (der
  AM32-Konfigurator zeigt es gekürzt als `JUPITER_35A_F42`), an einem
  4S-Pack, gebunden auf GPIO 0. Version 0.8.2 hat seine Drehzahl dekodiert
  (2593rpm). Version 0.9.1 hat mit der wiederholten Anfrage oben seine
  Extended Telemetry eingeschaltet: bei 0% Gas zeigte MOTOR & ESC 14,75V,
  72,0A und eine ESC-Temperatur von 41°C. Der Tester hat die Spannung als
  richtig gemeldet. Strom und Temperatur wurden dekodiert und gegen nichts
  geprüft: 72,0A im Stand, und 81,00A, die DshotDisplay an einem anderen Tag
  vom selben ESC las, sehen nach einem Eingang ohne Stromsensor dahinter aus
  und nicht nach einer Messung.

Nichts hiervon war an einem Oszilloskop, und jeder bidirektionale Punkt unten
ist über diesen einen ESC hinaus unbestätigt:

- die Antwortrate von fünf Vierteln der DShot-Rate;
- die Konvention für das führende Bit des Group Code;
- die Turnaround-Verzögerung, und ob 30µs das ist, was ein ESC tatsächlich
  wartet;
- die Einheiten der Extended-Telemetry-Frames für Strom (1A je Zählwert)
  und Temperatur (1°C je Zählwert) sowie die Stress-, Status- und
  Debug-Frames; nur die Spannung (0,25V je Zählwert) ist gegen einen Wert
  gelesen, den der Tester kannte;
- ob ESCs mit anderer Firmware als AM32 Command 13 unter der wiederholten
  Anfrage annehmen: ESCs mit Bluejay- und BLHeli_32-Firmware sind ungetestet;
- ob der Hinweis "mindestens 35ms warten" in der Spezifikation zu Command 13
  oder zu Command 12 (Save Settings) gehört. Die Tabelle lässt beide Lesarten
  zu, und der Prüfstand wartet in keinem Fall: auf die zehnte Wiederholung
  einer Anfrage folgt beim nächsten 1-ms-Takt das Throttle oder ein weiterer
  Frame mit Throttle null. Das klärt der Text der Spezifikation und keine
  Platine;
- jedes Bit-Timing, gegen die Toleranz eines echten ESC statt gegen die
  Spezifikation.

## Wo der Code liegt

| | |
| --- | --- |
| Frames, Group Code, Prüfsumme, Drehzahl, Sampler | `protocols/dshot/` |
| PPM-Frame-Layout | `protocols/ppm/` |
| Weg, Rolle, Ruhelage, Slew, Arming, Timeout | `shared/outputs/` |
| Welchen PWM-Slice und -Kanal ein GPIO erreicht | `shared/outputs/out_pwm_map.c` |
| PIO-Programme und ihre Treiber für PPM und DShot | `protocols/ppm/rp2350/`, `protocols/dshot/rp2350/` |
| Backend für Hardware-PWM | `firmware/iomcu/src/out_pwm.c` |
| Bindung der Bank an die Pins | `firmware/iomcu/src/outputs_hw.c` |
