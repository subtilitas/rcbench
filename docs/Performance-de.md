# Performance

<sub>[English](Performance.md) · **Deutsch**</sub>

Das Zeichenbudget für alle, die einen Bildschirm hinzufügen oder ändern.

## Die Randbedingung

Der ESP32-P4 zeichnet in einen von zwei DPI-Framebuffern (Display Pixel
Interface) zu 1 228 800 Bytes im PSRAM (Pseudo-Static Random-Access Memory),
während die DPI den anderen ausgibt: 1024 x 600px in RGB565 mit 60,38Hz,
74MB/s Lesezugriffe. Die CPU (Central Processing Unit) erreicht die Buffer
über zwei Write-Back-Caches: den L1-Datencache (64KB, 2-fach assoziativ,
64-Byte-Lines) und den L2-Cache (128KB, 8-fach assoziativ, 64-Byte-Lines).
Jede Line, die der L2 nicht hält, kostet einen 64-Byte-Fill aus dem PSRAM und,
sobald sie beschrieben ist, ein 64-Byte-Write-Back. Die Tabelle unten zählt
diese Fills, und die lassen sich auf dem Host exakt messen.

Die Zeit eines Frames ist sein PSRAM-Traffic und seine Instruktionen, und
welches von beiden einen Frame auf diesem Board begrenzt, ist nicht gemessen.
Der eine Bildschirm mit Zahlen von beiden Seiten deutet auf die
Instruktionen: SERVO > LIVE während eines Sweeps hat 2 431 Fills in der
Tabelle, 304KiB Traffic, und die Konsole las DRAW 13,8 bis 16,5ms ([Auf dem
Board](#auf-dem-board)); als reiner Traffic wären das 19 bis 23MB/s. Die
Fills halten die Seite des Traffics; `test_frame_work` hält die Seite der
Instruktionen in Pixelbesuchen ([Regeln](#regeln)).

Mit Screen rotation 180° zeichnet ein Bildschirm in einen logischen Buffer
derselben Größe im PSRAM, und der Flip lässt die PPA (Pixel-Processing
Accelerator) die Blöcke, die der Frame geändert hat, gedreht in den
Framebuffer kopieren, bevor er ihn zeigt. Das Modell zählt das Zeichnen,
nicht die Kopie. Die Zeit der Kopie ist TURN auf der Konsole; sie ist Teil
von WAIT und kommt zu jedem Frame hinzu, der sich ändert.

`tools/frame_cost.py` baut die echten Bildschirme für den Host und lässt sie
unter cachegrind mit den Caches des ESP32-P4 laufen: den L1-Daten- und den
L1-Instruktionscache als erste Stufe von cachegrind, den L2 als letzte,
bemessen nach `firmware/panel/sdkconfig.defaults`. Es meldet die Differenz
zwischen einem und elf gerenderten Frames, sodass Prozessstart und das erste
Füllen jedes Buffers herausfallen. Das Modell schickt jeden Datenzugriff durch
den L1-Datencache und den L2, auch die des Stacks. Das Laden von Instruktionen
läuft durch den L1-Instruktionscache (16KB, 4-fach, 64-Byte-Zeilen), und seine
Misses füllen den L2 neben den Daten. Geladen wird der x86-Code des Hosts,
dessen Größe und Anordnung sich vom RISC-V-Code unterscheiden; wie weit das
die Fills verschiebt, ist nicht gemessen.

Was ein Fill an Zeit kostet, ist die effektive Bandbreite der Schreibzugriffe
der CPU in den PSRAM, während die DPI liest. Sie ist nicht gemessen; die
Tabelle gibt deshalb Fills und Traffic und keine Zeit. Das Panel misst sie
beim Start ([Auf dem Board](#auf-dem-board)); `PROBE_CLEAR_US` im Werkzeug
nimmt diese Zahl auf, und die Tabelle gibt dann für jeden Modus eine
geschätzte Zeit und Frame Rate, allein aus dem Traffic.

<!-- framecost:start -->
```
$ python3 tools/frame_cost.py
panel 60.4 Hz, 64-byte lines; effective bandwidth not measured

mode                   lines/frame     traffic
----------------------------------------------
frame                        9,597    1200 KiB
frame-idle                       0       0 KiB
held                         5,221     653 KiB
sim                         13,295    1662 KiB
throttle                    11,404    1426 KiB
chrome                      51,887    6486 KiB
overview                         0       0 KiB
overview-guide                 350      44 KiB
servo                        2,216     277 KiB
servo-grip                     521      65 KiB
servo-current                3,189     399 KiB
supply                       9,871    1234 KiB
supply-chrome               48,048    6006 KiB
analyser                         0       0 KiB
logs                             0       0 KiB
settings                         0       0 KiB
settings-list                    0       0 KiB
battery                          0       0 KiB
balance                          0       0 KiB
motor-stick                  1,713     214 KiB
motor-wire                       0       0 KiB
motor-wire-hold                251      31 KiB
balance-sim                  3,253     407 KiB
settings-sim                 3,304     413 KiB
battery-sim                  3,146     393 KiB
analyser-chrome             61,108    7638 KiB
logs-chrome                 27,356    3420 KiB
settings-chrome             31,866    3983 KiB
settings-list-chrome        37,162    4645 KiB
battery-chrome              58,117    7265 KiB
balance-chrome              63,426    7928 KiB
motor-stick-chrome          41,955    5244 KiB
motor-wire-chrome           40,230    5029 KiB
picker                           0       0 KiB
picker-chrome               41,559    5195 KiB
wifi                             0       0 KiB
wifi-chrome                 22,447    2806 KiB
wifi-keys                   36,970    4621 KiB
clear                       19,206    2401 KiB
vlines                      10,204    1276 KiB
hlines                         180      22 KiB

est. ms and est. fps: not measured
```
<!-- framecost:end -->

| Modus | Was er misst |
| --- | --- |
| `frame` | der Motorprüfstand auf einem Frame, in dem ein Telemetriesample eintrifft |
| `frame-idle` | der Motorprüfstand auf einem Frame zwischen zwei Samples, ohne Berührung |
| `held` | der Motorprüfstand zwischen zwei Läufen: live Anzeigen über einem Plot, der den letzten hält |
| `sim` | wie `frame`, mit dem SIMULATION-Watermark |
| `throttle` | der Motorprüfstand mit einem Finger am Gas, der Drag-Fall |
| `chrome` | der Motorprüfstand ohne Cache, vollständig neu gezeichnet |
| `overview` | das Menü, Chrome gecacht |
| `overview-guide` | das Menü mit offener Bildschirmhilfe auf Seite 1: das Dock gecacht, Abzeichen und Eckwinkel von 4 Kacheln in jedem Frame gezeichnet |
| `servo` | der Servobildschirm mit neu gezeichnetem Arm |
| `servo-grip` | der Servobildschirm, nur der Griff neu gezeichnet |
| `servo-current` | wie `servo-grip`, mit dem INA3221 als Messgerät der Servo-Schiene und einem Fenster in jedem Frame, dessen Strom sich in den gezeigten Ziffern unterscheidet: der Griff, der Wert der Zeile STROM, die Zeile über dem Plot und der Plot neu gezeichnet, jedes auf sich selbst beschnitten |
| `supply` | der Netzteilbildschirm mit eingeschaltetem Ausgang, ein Sample in jedem Frame |
| `motor-wire-hold` | die Sitzung der ESC-Signalleitung auf der Seite VERLASSEN, ein Finger auf ihrem Hold von 2s |
| `analyser`, `logs`, `settings`, `battery`, `balance`, `motor-stick`, `motor-wire`, `picker`, `wifi` | ein ruhiger Frame dieses Bildschirms, Chrome gecacht; `settings` sind die Kacheln von SETUP, `motor-stick` ESC STICK offen auf MOTOR & ESC > PROGRAMMIEREN |
| `settings-list` | SETUP mit offener Liste PANEL, Chrome gecacht |
| `wifi-keys` | WI-FI mit offener WLAN-Tastatur, in jedem Frame eine Taste getippt |
| `<screen>-sim` | derselbe Bildschirm mit dem SIMULATION-Watermark |
| `<screen>-chrome` | derselbe Bildschirm, auf jedem Frame invalidiert |
| `clear` | ein Löschen des ganzen Bildschirms |
| `vlines` | siebzehn senkrechte Linien über die volle Höhe |
| `hlines` | dieselbe Pixelzahl als waagerechte Linien |

Die Zahl wiederholt sich bei jedem Lauf desselben Stands exakt: Das Werkzeug
gibt cachegrind die Geometrie aller drei Caches vor, sodass keiner von der CPU
des Hosts übernommen wird, und der Bench zeichnet von einem Stack mit fester
Lage innerhalb von 64KB, sodass argv, die Umgebungsvariablen und die Pfade sie
nicht verschieben. `test_frame_cost.py` misst fünf Modi zweimal und schlägt
bei einem Fill Unterschied fehl. Compiler, C-Bibliothek und valgrind prägen
die Zahl weiterhin. Die Tabelle enthält die Zahlen des ubuntu-26.04-Runners
der CI (Continuous Integration), und eine Maschine mit anderen Versionen misst
andere Zahlen. Eine Änderung irgendwo in den gemessenen Quellen verschiebt die
Adressen von Code und Daten und damit die Zahl auch von Modi, die sie nicht
berührt: Eine Änderung an Texten und am Hitec-Code verschob 24 der 37 Modi um
bis zu 22 Fills. Fills hängen vom Code-Layout ab: 18 Builds eines Baums, mit anderen Paddings und
Link-Reihenfolgen, ergaben battery-sim 3167 bis 3353 Fills, frame 9466 bis 9810, servo
2236 bis 2693 und settings-list-chrome 36320 bis 37282. `frame_cost.py --check-doc`
prüft die Tabelle deshalb mit einer Toleranz von 3%, mindestens 500 Fills. Keine zwei
der 18 Builds liegen weiter auseinander. Zeilen unter etwa 700 Fills hält diese Prüfung nicht. Es schlägt auch fehl, wenn die
Zeile, die die Rate des Panels und die Bandbreite nennt, vom Profil des
Werkzeugs abweicht.

## Regeln

**Zeilenweise zeichnen.** Siebzehn senkrechte Linien über die volle Höhe
kosten 10 204 Fills; dieselbe Pixelzahl als waagerechte Linien kostet 171,
weil jede Line vom vorigen Pixel noch geladen ist. Die Oberfläche ist in
waagerechte Bänder gegliedert, und eine senkrechte Trennlinie ist eine
bewusste Ausgabe.

**Das Chrome cachen.** Alles neu zu zeichnen kostet 22 000 bis 64 000 Fills,
die `-chrome`-Modi, ein gecachter ruhiger Frame desselben Bildschirms keinen:
60 992 Fills für das vollständige Neuzeichnen von RECEIVER gegen 0 für seinen
ruhigen Frame. Jeder Bildschirm führt je Framebuffer eine
Bitmaske dessen, was er schon gezeichnet hat; dafür ist das Argument
`buffer_index` von `render()` da. Das Panel wechselt zwischen zwei Buffern;
ein Bildschirm, der nur den gerade gezeichneten Buffer invalidiert, lässt den
anderen einen Frame zurück, was als Flackern erscheint.

**Ein Stencil, das sich nicht bewegt, wird gecacht.** SIMULATION wird auf
jedem Frame gezeichnet, sobald die Prüfstandswerte `LINK_BN_SIMULATED` tragen,
also auch bei einem IOmcu, der mit simulierten Werten antwortet.
Gedrehter Text scannt seine gedrehte Bounding-Box, von Ecke zu Ecke also die
ganze Canvas, und rotiert und dividiert je Pixel, um die Pixel zu
schreiben, die das Watermark bedeckt: 3 439 auf einer Canvas von
800 x 480px, 0,9% davon, und 5 377 auf 1024 x 600px. `ui_watermark` nimmt
diese Punkte einmal auf, in eine Tabelle mit 6 144 Plätzen, und schreibt sie
danach nur noch: bei 800 x 480px mit 144 721 Instruktionen je Frame statt
8 412 078. Die Zahl bei 1024 x 600px ist nicht gemessen.

**Eine Zahl von Fills ist keine Zahl von Zyklen.** Die Tabelle oben misst
Cache-Line-Fills, und das Watermark kostet davon nur 3 685, `sim` gegen
`frame`: seine Zeit ist Arithmetik je Pixel, kein Traffic, und die Tabelle
kann sie nicht zeigen. Wo die gemessene Frame-Zeit eines Modus
über dem liegt, was seine Fills vorhersagen, erst Instruktionen zählen, dann der
Schätzung trauen.

**Ein Bedienelement, das sich jeden Frame bewegt, bekommt seinen eigenen
Zähler.** Ein Drag liefert eine Berührung je Frame. Eine Revision, die es mit den
Bedienelementen daneben teilte, würde sie bei jeder Berührung alle neu
zeichnen. Das Gas des Motorbildschirms hat seine eigene und zeichnet die Box
der Anzeige und die bemalte Region des Sliders neu:
Der Modus `throttle` in der Tabelle oben misst das gegen einen Sample-Frame.
Der Flip quantisiert auf ganze Panel-Frames: 60,4, 30,2 oder 20,1fps. Die
Frame-Zeit eines Drags auf dem Board ist nicht gemessen.

Zu löschen ist, was ein Widget bemalt, nicht was es einnimmt. Der Daumen des
Sliders und sein Schatten stehen 5px oben und 7px unten über den Track
hinaus, und die Hero-Ziffern sind 32px hoch mit 3px Schräge auf einer 30px
hohen Zeile. Ein Clear in der Grösse des Tracks oder der Zeile lässt daher an
jeder Position, die der Finger passiert hat, einen Daumen und die Füsse der
Ziffern stehen. `ui_slider_painted_rect()` liefert die Region, statt sie den
Aufrufer herleiten zu lassen.

**Nur auf Frames zeichnen, die etwas zu zeichnen haben.** Samples kommen mit
20Hz, das Panel zeichnet mit 60,38Hz (gemessen, der VSYNC-Wert der Konsole),
also haben zwei von drei Frames nichts Neues. Der DPI-Takt ist 48MHz, nicht
die 52MHz, die `board_pins.h` anfordert; bei 48MHz ergäbe sein Frame von
1354 x 636 Takten allein 55,74Hz. ESP-IDF hält die angeforderte Zeilendauer,
indem es die Front Porch der Bridge auf eine Zeile von 1250 Takten kürzt,
also dauert ein Frame so lange wie 1354 x 636 Takte bei 52MHz: 60,38Hz, die
Rate, die `tools/frame_cost.py` aus `board_pins.h` berechnet. Dass dies die
Rate bestimmt, ist aus dem Quelltext von ESP-IDF gelesen, nicht aus den
Registern. Der Prüfstandsbildschirm führt den Push-Zähler des Plots und eine
Revisionsnummer der Bedienelemente, jeweils je Framebuffer, und zeichnet Plot,
Anzeigen und Bedienelemente nur neu, wenn der zugehörige Zähler sich bewegt
hat. `each_framebuffer_is_updated_independently` in `test_motor_draw` hält die
Zähler je Buffer fest.

**Ein geclippter Repaint läuft nur über das, was in seinem Clip liegt.** Ein
Clip begrenzt, was ein Primitiv schreibt, nicht was es rechnet. Die SERVO-Karte
zeichnet ihren atmenden Griff durch eine Box von 73px Seitenlänge neu und ein
bewegtes Horn durch Boxen von etwa 170px, jeweils indem sie die ganze Karte
unter dem Clip zeichnet. Deshalb überspringt der Rasterizer eine Glyphe oder
eine Siebensegment-Ziffer, deren Zelle ausserhalb des Clips liegt, läuft einen
Bogen nur in den Richtungen ab, in denen der Clip liegt, läuft Kreisscheibe,
Kapsel und Ring mit Antialiasing je Zeile nur über die Pixel, die sie dort
bedecken können, und rechnet den Winkel eines Teilrings je Pixel nur nahe
seinen Enden und seiner Blende. `test_gfx_clip` hält jede Abkürzung Pixel für
Pixel an der Schleife über die ganze Box fest.

**Das Band wird neu gezeichnet, wenn es sich ändert.** Ganz gezeichnet kostet
das Band 102 288 Pixelbesuche SAFE und 114 281 ARMED, mit rotem Chip und
Laufuhr. Der Router merkt sich je Framebuffer `ui_band_key()` des zuletzt
gezeichneten Bands und zeichnet es nur neu, wenn sich der Schlüssel bewegt: die
Laufuhr einmal je Sekunde, solange der Prüfstand ARMED ist, ein Chip, ein Druck
auf STOP oder auf den Home-Tag. Über einem simulierten Prüfstand wird es jeden
Frame gezeichnet, unter dem Watermark. `test_frame_work` zählt die Pixel, die
der Rasterizer je Frame besucht (`gfx_work()`, nur in die Host-Suite gebaut),
und hält ARMED in Ruhe bei SAFE in Ruhe plus dem Band einmal je Sekunde, eine
gehaltene Position bei SAFE plus sechs Lagen der Box des Griffs, ein gesweeptes
oder gezogenes Horn bei 140 000 Besuchen je Frame und MOTOR & ESC LIVE ARMED in
Ruhe bei 300 000.

## Obergrenzen

CI hält jeden Modus unten an eine Obergrenze von
Fills je Frame. Eine Obergrenze liegt 13 bis 29% über der größten Zahl ihrer
Modi in der Tabelle oben, und ein Modus unter 700 Fills wird bei 1 000
gehalten. Eine Obergrenze fängt eine Änderung, die das Modell mehr zeichnen
lässt, nicht einen Frame, der das Panel verpasst:

| Modi | Obergrenze (Fills) | Fängt |
| --- | ---: | --- |
| `overview`, `analyser`, `logs`, `settings`, `settings-list`, `battery`, `balance`, `motor-wire`, `motor-wire-hold`, `picker`, `wifi` | 1 000 | einen Bildschirm mit gecachtem Chrome, der neu zu zeichnen begonnen hat |
| `overview-guide` | 1 000 | Marken der Hilfe, die mehr als ihre Abzeichen und Winkel zeichnen |
| `servo` | 2 800 | ein Wachsen der Arm- und Griffzeichnung |
| `servo-grip` | 1 000 | ein Atmen, das mehr als seine Box neu zeichnet |
| `servo-current` | 4 000 | eine geänderte Ziffer des Stroms, die die ganze Karte neu zeichnet |
| `motor-stick` | 2 000 | ein Wachsen des ruhigen Frames von ESC STICK, 1 720 Fills in der Tabelle oben |
| die drei `-sim`-Modi | 3 600 | ein Watermark, das über die volle Canvas hinauswächst |
| die elf `-chrome`-Modi | 72 000 | ein wachsendes vollständiges Neuzeichnen |
| `wifi-keys` | 42 000 | eine Taste der WLAN-Tastatur, die mehr als den Bildschirm neu zeichnet |

`tools/check_docs.py` hält diese Tabelle an den `--max-lines`-Argumenten in
`.github/workflows/ci.yml`: eine abweichende Obergrenze, ein Modus, den CI
hält und der hier keine Zeile hat, und eine Zeile, die CI nicht ausführt,
schlagen jeweils fehl. `test_frame_work` hält daneben Budgets in
Pixelbesuchen ([Regeln](#regeln)).

Keine Obergrenze hält den Prüfstandsframe (`frame`, `sim`, `supply`) an ein
Telemetriesample. Ein Sample kommt alle 50ms, jeden dritten Panel-Frame, und
ein Frame hält mit den Samples Schritt, solange sein Traffic bei der
effektiven Bandbreite in drei Panel-Frames passt. Diese Bandbreite ist nicht
gemessen, daher ist die Obergrenze nicht gesetzt.

Braucht ein künftiger Bereich mehr Platz, sind die verbleibenden Hebel vom
gröbsten zum feinsten: die Höhe des Plots, seine Breite, und das
Simulations-Watermark auf den tatsächlich neu gezeichneten Bereich zu
clippen.

## Auf dem Board

Die Konsolenzeilen dieses Abschnitts kommen mit der Build-Option
`RCBENCH_PERF_LOG`, standardmäßig ON (`firmware/panel/CMakeLists.txt`):

```sh
idf.py -D RCBENCH_PERF_LOG=OFF build
```

baut ein Panel ohne die Messung beim Start, ohne die Zeilen und ohne den
Code dahinter: die Render-Schleife liest dann keine Uhr für DRAW und ruft
für sie nichts auf. CI baut das, zusammen ohne den Touch-Injector, als den
zusätzlichen Panel-Build.

Release-Images werden mit `RCBENCH_PERF_LOG=OFF` gebaut und geben keine
dieser Zeilen aus. Der CI-Build, ein lokales `idf.py build` und ein Build
mit `-D RCBENCH_PERF_LOG=ON` behalten sie; um auf den Quellen eines
Releases zu messen, ohne das Flag bauen. `tools/check_perf_log.py` hält das
Release-Image an die Einstellung: Es schlägt fehl, wenn das Image den Text
der Messung beim Start, den Text des Stack-Scans oder das Symbol
`scan_stacks` enthält.

Die Konsole ist UART0 mit 115 200 Baud ohne installierten Treiber, neben dem
USB-Serial/JTAG-Port. Eine Zeile, die nicht in den 128-Byte-Sende-FIFO
(First In, First Out) der UART passt, hält die Render-Schleife 87us für jedes
Byte darüber auf, und 128 Bytes sind in 11,1ms gesendet, innerhalb eines
Frames von 16,6ms bei 60,4fps. Das Panel gibt deshalb höchstens eine dieser
Zeilen je Frame aus:

- alle 300 Frames, 5,0s bei 60,4fps und 9,9s bei 30,2fps, die Ratenzeile;
- im Frame danach die Fensterzeile;
- in den Frames dazwischen eine Zeile der Tiefststände, solange eine fällig
  ist: die Zeit des Stack-Scans, der interne Heap, dann ein Stack.

```text
I (…) rcbench: <f> fps  DRAW <n> us  WAIT <n> us  TURN <n> us  VSYNC <f> Hz  FLIP TIMEOUTS <n>  TOUCHLOST <panel>/<driver>
I (…) rcbench: frames <n> on <title> #<id>: DRAW <min>/<mean>/<max> WAIT <min>/<mean>/<max> TURN <min>/<mean>/<max> us
```

Die beiden Zeilen sind 110 bis 135 Bytes lang, je nach den Stellen der Zähler
und dem Titel des Bildschirms; ein Byte über 128 hält die Schleife 87us auf.
DRAW, WAIT und TURN der Ratenzeile sind die ihres Frames; VSYNC ist die
Bildwiederholrate des Panels, wie das Panel sie seit der Ratenzeile davor
zählt; FLIP TIMEOUTS zählt die Buffer-Wechsel seit dem Start, die 200ms
vergeblich auf ein Frame-done-Ereignis gewartet haben. DRAW ist die
Zeichenzeit eines Frames, WAIT die Zeit, die der Buffer-Wechsel danach
blockiert hat, und TURN der Teil von WAIT, den Screen rotation 180° für die
gedrehte Kopie des Frames gebraucht hat, 0 in NORMAL. Die Fensterzeile
umfasst die Frames, die seit der Ratenzeile davor auf dem gezeigten
Bildschirm gezeichnet wurden, weniger als 300, wenn der Bildschirm dazwischen
gewechselt hat; ihre Zahlen sind Minimum, Mittel und Maximum in us. Ein
gesunder Frame besteht überwiegend aus WAIT; steigt DRAW, bis WAIT null
erreicht, ist das Frame-Budget aufgebraucht.

Was die Option je Frame kostet, wenn sie ON ist: den Frame im Fenster
addieren, etwa zehn Vergleiche und drei 64-Bit-Additionen, und eine Prüfung
auf eine fällige Zeile; alle 300 Frames eine Abfrage des internen Heaps;
jede 6. Ratenzeile einen Stack-Scan ([High-Water-Marks auf dem
Board](#high-water-marks-auf-dem-board)). Ihre Zeit auf dem Board ist nicht
gemessen, außer der des Stack-Scans, die das Panel ausgibt.

Einmal beim Start, vor dem ersten Frame des Splash, misst das Panel die Zeit
von vier Löschvorgängen des hinteren Buffers, nach einem, den es nicht misst:

```text
I (…) rcbench: PSRAM probe: 4 clears of 1228800B in <n> us, <n> us each
```

Ein Löschen schreibt den Buffer einmal: 2 457 600 Bytes Traffic im Modell,
ein Fill und ein Write-Back je Line. Die Zeit eines Löschens, gesetzt als
`PROBE_CLEAR_US`, macht aus dem Traffic der Tabelle eine Zeit.

| Modus | Auf dem Panel | DRAW min (ms) | DRAW Mittel (ms) | DRAW max (ms) | WAIT Mittel (ms) | fps |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `clear` | die Messung beim Start; DRAW Mittel ist die Zeit eines Löschens | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `overview` | OVERVIEW, unberührt | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `frame` | MOTOR & ESC > LIVE während eines ESC-Laufs | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `sim` | MOTOR & ESC > LIVE mit den simulierten Werten des IOmcu | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `servo` | SERVO > LIVE während eines Sweeps | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `servo-grip` | SERVO > LIVE mit ruhendem Arm | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `supply` | SUPPLY mit eingeschaltetem Ausgang | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `analyser` | RECEIVER ohne angeschlossenen Empfänger | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `logs` | LOGS, die Dateiliste | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `settings` | die Kacheln von SETUP | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `settings-list` | die Liste von SETUP > PANEL | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `battery` | BATTERY | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `balance` | BALANCE | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `motor-stick` | ESC STICK offen auf MOTOR & ESC > PROGRAMMIEREN | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `motor-wire` | die Sitzung der ESC-Signalleitung in Ruhe an einem gelesenen ESC, MOTOR & ESC > PROGRAMMIEREN | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `motor-wire-hold` | ein Finger auf dem Hold von 2s auf der Seite VERLASSEN der ESC-Signalleitung | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `picker` | OUTPUTS, Platinenansicht | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `wifi` | WI-FI mit eingeschaltetem WLAN-HOTSPOT | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `wifi-keys` | WI-FI > HEIMNETZ WÄHLEN, die Passwort-Tastatur, jede Sekunde eine Taste getippt | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |

`frame_cost.py --check-doc` liest diese Tabelle: ein Modus, den das Werkzeug
nicht zeichnet, schlägt fehl, und DRAW Mittel der Zeile `clear` und
`PROBE_CLEAR_US` sind zusammen gemessen oder gar nicht und stimmen auf
0,05ms überein. Die übrigen Zahlen sind eine Aufzeichnung des Boards, die
keine Prüfung hält; Mittel und Extreme der Frames eines Bildschirms sind das,
woran die Schätzung des Modells für seinen Modus gemessen wird.

Eine Fensterzeile ist auf einem Board noch nicht gelesen. Die Ratenzeile
eines Laufs am Prüfplatz auf SERVO > LIVE, der Prüfstand ARMED, ergab:

| Der Arm | Screen rotation | fps | DRAW (ms) | TURN (ms) |
| --- | --- | ---: | ---: | ---: |
| in Ruhe | NORMAL | 60,4 | nicht gemessen | 0 |
| in Ruhe | 180° | nicht gemessen | nicht gemessen | nicht gemessen |
| im Sweep | NORMAL | 39 bis 45 | nicht gemessen | 0 |
| im Sweep | 180° | 30,2 | nicht gemessen | 3,8 bis 4,8 |

VSYNC las 60,38Hz. Ein zweiter Lauf, mit einem früheren Build desselben
Frame-Pfads, las DRAW 13,8 bis 16,5ms im Sweep in NORMAL, bei 35 bis 43fps.
Der Flip quantisiert auf ganze Panel-Frames, 60,4, 30,2 oder 20,1fps; eine
Rate zwischen zweien davon ist also eine Mischung aus Frames, die einen
Panel-Frame getroffen haben, und Frames, die zwei gebraucht haben. Mit TURN
im Flip sind DRAW und TURN eines Sweeps zusammen 17,6 bis 21,3ms (aus den
beiden Läufen hergeleitet), über den 16,6ms eines Panel-Frames, und jeder
Frame braucht zwei.

## Stacks

Jede Task des Panels läuft auf einem festen Stack, und eine Aufrufkette, die
über sein Ende hinausläuft, startet das Panel neu. `tools/stack_check.py`
liest die tiefste Aufrufkette jeder Task aus der ELF-Datei (Executable and
Linkable Format) des Panels. Der ESP32-P4 führt RISC-V-Code aus, den
`tools/stack_riscv.py` dekodiert: der Frame jeder Funktion ist der tiefste
Punkt, den ihr Stack Pointer erreicht (`addi sp, sp, -N` und seine
komprimierten Formen), und die Tiefe ist die größte Summe der Frames entlang
einer Kette vom Einstiegspunkt der Task. Ein Sprung aus einer Funktion
heraus, die Form eines Tail Calls, zählt als Aufruf. Die Tasks sind jedes
`xTaskCreatePinnedToCore()` und `xTaskCreate()` im eigenen Code des Panels
unter `firmware/panel`, ohne `managed_components/`, und die Main-Task. CI
führt das Werkzeug nach dem Panel-Build aus und schlägt fehl, wenn die Tiefe
einer Task ihren Stack abzüglich 1024 Bytes überschreitet.

| Task | Einstieg | Stack (Bytes) | Tiefste Kette (Bytes) | Reserve unter der Marge (Bytes) |
| --- | --- | ---: | ---: | ---: |
| `main` | `main_task`, ruft `app_main` und betreibt die UI | 8 192 | 4 288 | 2 864 |
| `control` | `control_task` | 6 144 | 3 376 | 1 728 |
| `runlog` | `log_task` | 6 656 | 3 824 | 1 792 |
| `knob` | `knob_task`, der Leser des Drehknopfs | 3 072 | 1 168 | 864 |
| `touch` | `touch_task`, der GT911-Leser (`components/gt911`) | 4 096 | 1 392 | 1 664 |
| `wifi_ctl` | `worker`, startet und stoppt den Funk und den Server der Seite (`components/wifi_access`) | 6 912 | 2 864 | 3 008 |
| `mdns` | `mdns_task`, der mDNS-Responder (Multicast Domain Name System), solange das Panel mit einem Heimnetz verbunden ist | 4 096 | 1 440 | 1 616 |
| `wifi_get` | `fetch_task`, eine Release-Suche oder ein Download von GitHub (`components/wifi_access`) | 8 192 | 1 568 | 5 584 |
| `cardjob` | `files_task`, die Kartenoperationen von FILES (`panel_files.c`) | 6 656 | 2 064 | 3 552 |
| `shot` | `shot_task`, ein Screenshot-Auftrag, dann weg (`panel_shot.c`) | 7 168 | 2 416 | 3 712 |

Der HTTP-Server (Hypertext Transfer Protocol) der Seite läuft in einem Task
mit 8 192 Bytes, der TCP/IP-Stack (Transmission Control Protocol, Internet
Protocol), die Event-Loop und die Verbindung von esp_hosted zum ESP32-C6 in
eigenen Tasks. ESP-IDF und esp_hosted legen sie an, deshalb stehen sie nicht
in der Tabelle: Ihre Tiefe ist nicht gemessen, und die Konsole meldet, was
sie unbenutzt lassen (unten). Die Tiefe von `wifi_get` lässt den
TLS-Handshake (Transport Layer Security) und jedes Lesen darunter aus:
esp_http_client erreicht seinen Transport über einen Funktionszeiger. Sein
Stack ist der des Servers der Seite, der nie neben ihm läuft; die Tiefe des
Handshakes ist nicht gemessen.

Die tiefste Kette von `runlog` bindet die SD-Karte ein: die Liste der
Servodateien, die die Karte bedient (`hitec_card_run()`, `list()`,
`servo_store_preset()`, `io_walk()`, `storage_walk()`), das erneute Einbinden einer Karte, die aufgehört
hat zu antworten (`storage_read_end()`, `mount_locked()`), `f_mkfs()` unter dem Einbinden
und den Zeitstempel, den es schreibt (`get_fattime()`, `localtime_r()`, die
Zeitzonen-Auswertung `_tzset_unlocked()` von picolibc und das `sscanf()`
darunter). Das Werkzeug folgt jedem Aufruf, den das Einbinden machen kann,
darunter `f_mkfs()`, das ein Einbinden ohne Formatieren nie erreicht. Der Start eines Laufs bindet die Karte ebenfalls auf diesem Task
ein (`storage_check()`). Die Kette braucht 3 824 Bytes; der Stack von 6 656
Bytes lässt 1 792 über der Marge von 1 024 Bytes.

Die tiefste Kette von `wifi_ctl` ist der Start des Netzwerk-Stacks in
`worker()`: `esp_netif_init()` in `netif_add()` und `netif_set_addr()` von
lwIP, der TCP-Code darunter bis `ip6_frag()` und `pbuf_alloc()`, und die
Heap-Anforderung und der Log-Aufruf darunter. Sie lässt 3 008 Bytes über der
Marge.

Gemessen auf dem ESP32-P4-Build mit ESP-IDF v6.1 bei -O2. Von der Marge
gehen 560 Bytes außerhalb der Frames auf:

- 128 für die Register, die ein Interrupt auf dem Stack der Task sichert,
  bevor er auf den Interrupt-Stack wechselt;
- 32 für den Kopf des Sicherungsbereichs der Koprozessoren am oberen Ende
  jedes Stacks;
- 384 für die Kontexte, die der Port vom unteren Ende eines Stacks nimmt,
  sobald seine Task die Einheit zum ersten Mal benutzt: die FPU
  (Floating-Point Unit) 132, die Hardware-Schleifen 24, die Vektoreinheit
  216 und bis zu 12 für die Ausrichtung;
- 16 für den Canary von FreeRTOS am unteren Ende des Stacks.

Der Thread-Local Storage am oberen Ende jedes Stacks wird aus der ELF-Datei
gelesen und obendrauf gerechnet; der Build des Panels hat keinen. Die
übrigen 464 Bytes decken ab, was das Werkzeug nicht sieht, und jede Tiefe
oben ist deshalb eine Untergrenze:

- Aufrufe über einen Funktionszeiger, außer den Aufrufen des Routers in einen
  Bildschirm, die das Werkzeug aus der Tabelle jedes Bildschirms liest, und
  den Callbacks des Link-Ports, den Flash-Callbacks des Zeichnungsspeichers,
  denen der Startprüfung nach einem Update, den Callbacks des Links des
  SERVO-Bildschirms in das Panel, den Hooks der Sitzungen, die
  SERVO > PROGRAMMIEREN und MOTOR & ESC > PROGRAMMIEREN öffnen, und den
  Kartenfunktionen, über die die Lauf-Logs, das Fehlerprotokoll und die
  Servodateien geschrieben werden, denen es nach Namen folgt
  (`PORT_CALLS`, `FLASH_CALLS`, `BOOT_CALLS`, `SERVO_CALLS`,
  `PROG_CALLS`, `TAKE_CALLS`, `CARD_CALLS`, `FAULT_CALLS`, `SERVO_STORE_CALLS`):
  295
  solche Aufrufe sind von `main_task` aus erreichbar, die meisten in den
  Speicher- und Display-Treibern von ESP-IDF;
- Sprünge über ein Register: eine Sprungtabelle, die in ihrer Funktion
  bleibt, oder ein Tail Call über einen Zeiger, was die Dekodierung nicht
  unterscheiden kann;
- Aufrufe in das ROM (Read-Only Memory) des ESP32-P4, dessen Frames nicht in
  der ELF-Datei stehen;
- eine Funktion, die ihren Stack Pointer um einen erst zur Laufzeit
  bekannten Betrag verschiebt: ihr Frame ist der Teil, den die Konstanten
  ergeben;
- Rekursion: Eine Funktion, die schon in einer Kette steht, wird nicht
  noch einmal betreten; eine Kette läuft also höchstens einmal durch einen
  Zyklus.

CI führt das Werkzeug mit `--check-doc` aus; das schlägt fehl, wenn die
Tabelle oben oder die Zahl der Aufrufe über einen Zeiger vom Build abweicht.

### High-Water-Marks auf dem Board

Die erste Ratenzeile und jede 6. danach, 30s auseinander bei 60,4fps, wollen
einen Scan des Stacks jeder Task. Der Scan ist `uxTaskGetSystemState()` von
FreeRTOS. Er hält den Kernel-Lock, mit maskierten Interrupts auf dem Kern des
Renderns, während er den unbenutzten Stack jeder Task Byte für Byte liest;
ein Kernel-Aufruf auf dem anderen Kern, auch der der Control-Task, wartet
also auf ihn. Der Scan läuft deshalb nur in einem ruhigen Frame, nach der
Regel von `flash_gate_open()`: der Prüfstand nicht ARMED und kein ARM
wartend, der Ausgang des Netzteils aus und kein EIN eingereiht. Ein Scan,
der fällig wird, während der Prüfstand ARMED ist, läuft bei der ersten
Ratenzeile, nachdem er SAFE ist; die High-Water-Mark behält, was der Lauf
gebraucht hat. Der Scan meldet den Namen jeder Task als Zeiger in den
Kontrollblock der Task, den eine endende Task freigibt: das Panel kopiert die
Namen sofort, höchstens 15 Bytes, und behält den Scan nur, wenn die Zahl der
Tasks nach dem Kopieren die ist, die der Scan gelesen hat; sonst scannt die
nächste Ratenzeile erneut. Das Panel gibt die Zeit des Scans beim ersten Scan aus und bei
jedem Scan, der länger dauert als alle davor, und nach jedem Scan jede Task,
deren High-Water-Mark gefallen ist, eine Zeile je Frame:

```text
I (…) rcbench: stack scan: <n> tasks in <n> us
I (…) rcbench: stack <task>: <n> B never used
```

Der erste Scan listet jede Task, auch die von ESP-IDF und esp_hosted, über
die Frames nach ihm, eine Task je Frame. Die Zeit des Scans auf dem Board ist
nicht gemessen.

Die Zahl ist die High-Water-Mark von FreeRTOS: die Bytes am unteren Ende des
Stacks, die seit dem Start der Task kein Frame beschrieben hat, ohne die
Kontexte der Koprozessoren. Sie umfasst nur die Pfade, die das Panel
durchlaufen hat, und begrenzt deshalb nichts; neben die tiefste Kette oben
gestellt, zeigt sie, was das Werkzeug nicht verfolgt.

| Task | Stack (Bytes) | Auf dem Board nie benutzt (Bytes) |
| --- | ---: | ---: |
| `main` | 8 192 | nicht gemessen |
| `control` | 6 144 | nicht gemessen |
| `runlog` | 6 656 | nicht gemessen |
| `knob` | 3 072 | nicht gemessen |
| `touch` | 4 096 | nicht gemessen |
| `wifi_ctl` | 6 912 | nicht gemessen |
| `mdns` | 4 096 | nicht gemessen |
| `wifi_get` | 8 192 | nicht gemessen |
| `cardjob` | 6 656 | nicht gemessen |
| `shot` | 7 168 | nicht gemessen |

Die Tasks von ESP-IDF und esp_hosted stehen nicht in der Tabelle: ihre Namen
und Zahlen kommen aus der Konsole eines Musters, und keine ist aufgezeichnet.

### Der IOmcu

`tools/stack_check.py --iomcu` liest das RP2350-Image auf dieselbe Weise.
Kern 0 führt `main()` auf dem Main-Stack des pico-sdk aus: 4096 Bytes,
`__StackBottom` bis `__StackTop` in der ELF-Datei, festgelegt durch
`PICO_STACK_SIZE=0x1000` in `firmware/iomcu/CMakeLists.txt`. Kern 1 führt
`core1_main()` auf dem 4096 Bytes großen Array aus, das sein Start übergibt.
Keiner der beiden Stacks hat eine Schutzzone. Ein Interrupt läuft auf dem
Stack des Kerns, den er unterbricht; jedem Kern werden deshalb seine tiefste
Kette und ein Interrupt angerechnet: ein Exception-Frame von 108 Bytes und
der tiefste Handler, den das Image installiert. CI führt das Werkzeug nach
dem IOmcu-Build aus und schlägt fehl, wenn Kette, ein Interrupt und
eine Marge von 256 Bytes den Stack eines Kerns überschreiten.

| Kern | Einstieg | Stack (Bytes) | Tiefste Kette (Bytes) | Ein Interrupt (Bytes) | Reserve unter der Marge (Bytes) |
| --- | --- | ---: | ---: | ---: | ---: |
| Kern 0 | `main` | 4 096 | 1 468 | 528 | 1 844 |
| Kern 1 | `core1_main` | 4 096 | 644 | 528 | 2 668 |

Gemessen am Image, das CI baut, pico-sdk 2.3.0 mit arm-none-eabi-gcc 14.2.1
(Paket `gcc-arm-none-eabi` 15:14.2.rel1 von Ubuntu 26.04, derselbe Compiler
wie ARM GNU 14.2.Rel1). CI führt das Werkzeug mit `--check-doc` aus; das schlägt fehl, wenn die
Zeile von Kern 0 oder die Zahl der Aufrufe über ein Register weiter unten
von diesem Build abweicht. Die Zeile von Kern 1 wird nicht
gehalten: ihre Kette ändert sich mit dem Compiler (arm-none-eabi-gcc 13.2.1
ergibt 640 Bytes), und CI baut das Image mit dem Compiler aus dem Paket des
Runners. Die unten genannten Frames stammen aus demselben Build.

Die tiefste Kette von Kern 0 ist eine Anfrage, die einen Ausgang bindet und
im pico-sdk scheitert: `main` (256 Bytes), `can_service` (344),
`link_dev_dispatch`, `slots_write`, `slots_take`, `ports_apply`,
`slots_rerate`, `outputs_hw_apply_only`, `out_dshot_bind`, die Belegung einer PIO-State-Machine (PIO: Programmable
Input/Output) im pico-sdk und `panic()` mit dem, was es zur Ausgabe
aufruft, 228 Bytes. Der tiefste Handler ist der Worker des USB-Stacks,
`low_priority_worker_irq`, mit 420 Bytes; auch seine Kette endet in
`panic()`.

Ein Schreibkommando an den Programmierport für KST- und Chaservo-Servos erreicht 1 104 Bytes: `main`
(264), `can_service` (344), `link_dev_dispatch` (32), `kst_write` (24),
`kst_port_write` (88), `kst_session_edit` (40) und der Start eines
Planer-Jobs, 312 Bytes ab `kst_plan_edit_begin`: `kst_plan_edit_begin`
(40), die Grenzwertprüfung `kst_limits_edit` (248), `load` (16) und
`kst_field_get` (8). Ein Durchlauf, der plant, erreicht 824 Bytes: `main`,
`kst_port_step` (48), `step_programming` (40), `kst_session_step` (264) und ein Slice des Planers,
208 Bytes ab `kst_plan_step`. Der Planer ruft sich nicht selbst auf. Der
Job des Planers, der Plan und das bereitgestellte Image liegen im Zustand
der Session und des Ports, nicht auf dem Stack.

Der Port plant einen Schreibauftrag in Slices. Das Schreibkommando prüft
den Auftrag und startet den Job des Planers, 64us Planerarbeit. Jeder
Durchlauf der Hauptschleife von Kern 0 fährt dann 1 Slice in
`kst_session_step()`, 100 bis 109 Einheiten Arbeit: höchstens 892us, der
längste gefundene 816us. Das Planen endet nach 216 000 Einheiten,
höchstens 2 161 Durchläufen, mit einem Plan, ohne Plan oder mit dem
Ergebnis "too complex", wenn die Einheiten vorher verbraucht sind. Der
teuerste gefundene Auftrag braucht 120 136 Einheiten, höchstens 1 202
Durchläufe und 397 bis 647ms Planerzeit. Alle Zeiten gelten für 150MHz,
sind in einem Emulator aus den ausgeführten Instruktionen mit ARM GNU 14.2
bei -O3 berechnet und nicht auf Hardware gemessen. Die Zeit eines
Durchlaufs mit Link, Heartbeat und Ausgängen ist nicht gemessen. Die
Heartbeat-Lücke, die alle Ausgänge abschaltet, beträgt 150ms.

Jede Tiefe ist eine Untergrenze:

- Aufrufe über ein Register werden nicht verfolgt, außer dem Lese- und dem
  Schreib-Handler einer Page, die das Werkzeug aus der Page-Tabelle des
  Links liest: 250 solche Aufrufe sind von `main` aus erreichbar, im Image,
  das CI baut (arm-none-eabi-gcc 14.2.1);
- der Frame von 1 088 Bytes von `two_way_long_needle()` der newlib bleibt
  außen vor. `strstr()` ruft sie für ein Suchmuster ab 255 Zeichen auf, und
  die beiden Suchmuster der Firmware sind 3 und 5 Zeichen lang. Das Werkzeug
  schlägt bei einem `strstr()`-Aufruf unter `shared/` oder
  `firmware/iomcu/src` fehl, dessen Suchmuster kein Stringliteral unter 255
  Zeichen ist;
- 17 handgeschriebene Rechenroutinen des pico-sdk tragen in der ELF-Datei
  keine Größe, und ihre Frames werden nicht gelesen;
- gezählt wird ein Interrupt, kein zweiter auf dem ersten;
- Rekursion: Eine Funktion, die schon in einer Kette steht, wird nicht
  noch einmal betreten; eine Kette läuft also höchstens einmal durch einen
  Zyklus.

Der Stack von Kern 0 ist der gesamte RAM-Bereich `SCRATCH_Y`, 0x20081000
bis 0x20082000; das Image legt nichts anderes hinein, und der Link schlägt
fehl, sobald etwas hineingelegt wird. Darunter, in `SCRATCH_X`, liegt das
4096 Bytes große Array, das das pico-sdk als eigenen Stack für Kern 1
reserviert und auf dem dieses Image nicht läuft. Dieselbe Definition
bestimmt die Größe dieses Arrays.

`-v` listet sie alle auf, und die tiefste Kette jeder Task.

Die UI des Panels hält ihre Frames klein, wo das wenig kostet. Der
Zeichner einer Seite ist eine eigene Funktion, sodass nur die angezeigte
Seite ihre Puffer hält: in ESC STICK ruft `esc_stick_panel_render()` die
angezeigte Seite auf, und `mo_render()` von MOTOR & ESC ruft den Weg der
Sitzung und den seines Körpers als zwei Funktionen auf, sodass die Seiten
einer Sitzung nicht auf dem Frame des Körpers liegen. Eine Zeile, die zur
Anzeige kopiert wird, wird ohne `snprintf()` kopiert, das die
Gleitkommawandlung von picolibc erreicht. Die Frame-Größen auf dem ESP32-P4
sind nicht gemessen.

## Interner RAM

Der ESP32-P4 hat kein Funkmodul. Das WLAN läuft auf dem ESP32-C6 des
7-Zoll-Boards, den der P4 über SDIO (Secure Digital Input Output) mit
`esp_wifi_remote` und `esp_hosted` erreicht. Kein Werkzeug schätzt den
internen RAM, der zur Laufzeit frei ist: die Host-Seite des Funks belegt
ihre Puffer und Tasks zur Laufzeit, was die Map des Builds nicht zeigt. Das
Panel gibt die Zahlen stattdessen aus. Beim Start, mit allen Tasks des
Panels und ausgeschaltetem Funk:

```text
I (…) rcbench: internal RAM: <n> B free, largest block <n> B; PSRAM: <n> B free
```

Wenn der Funk läuft, mit dem Modus nummeriert wie in `wa_session.h`
(0 WLAN-HOTSPOT, 1 mit einem Heimnetz verbunden, 2 eine Netzsuche, 3 eine
Release-Suche oder ein Download):

```text
I (…) wifi: radio up, mode <n>; internal RAM <n> B free
```

Und in einem Frame nach einer Ratenzeile, wenn die tiefste freie Zahl seit
dem Start gefallen ist ([Auf dem Board](#auf-dem-board)):

```text
I (…) rcbench: internal RAM: <n> B free at the lowest, <n> B free now, largest block <n> B
```

| Wann | Frei (Bytes) | Tiefster Stand seit dem Start (Bytes) |
| --- | ---: | ---: |
| Start, Funk aus | nicht gemessen | nicht gemessen |
| Modus 0, WLAN-HOTSPOT | nicht gemessen | nicht gemessen |
| Modus 1, mit einem Heimnetz verbunden | nicht gemessen | nicht gemessen |
| Modus 2, eine Netzsuche | nicht gemessen | nicht gemessen |
| Modus 3, eine Release-Suche | nicht gemessen | nicht gemessen |

Freier PSRAM beim Start: nicht gemessen.

Ein WLAN-Start, der fehlschlägt, protokolliert den Schritt, den Namen des
Fehlers und den internen Heap in diesem Moment:

```text
E (…) wifi: esp_wifi_init: ESP_ERR_NO_MEM; internal RAM <n> B free, largest block <n> B
```

Die Schritte sind `esp_netif_init`, `esp_event_loop_create_default`, die
beiden Event-Handler, die Netzwerkschnittstelle, `esp_wifi_init`,
`esp_wifi_set_mode`, `esp_wifi_set_config`, `esp_wifi_start`,
`httpd_start` und das Anlegen der Tasks `wifi_ctl`, `wifi_get` und `mdns`.
Eine Netzwerkschnittstelle, die ESP-IDF nicht belegen kann, hält das Panel
stattdessen mit einer Assertion in ESP-IDF an, und das Panel startet neu.

Die Funk-Task `wifi_ctl` belegt 6.912 Bytes internen RAM für ihren Stack,
beim ersten Funkstart oder bei der ersten Release-Prüfung, und behält sie
bis zum Neustart des Panels. Die statischen Werte des Builds für den
internen RAM (unten) ändern sich dadurch nicht.

Die Kartenaufgabe `cardjob` von FILES belegt 6.656 Bytes internen RAM für
ihren Stack und 16.384 Bytes DMA-fähigen internen RAM für ihren
Kopierpuffer. Beides wird bei der ersten Anfrage von FILES belegt, nicht
beim Start, und das Panel schreibt den freien internen RAM davor und
danach:

```text
I (…) rcbench: files: cardjob started; internal RAM <n>B free before, <n>B after
```

Ohne den RAM lautet die Zeile `W (…) rcbench: files: no internal RAM for
cardjob`, und FILES meldet `Nicht genug Speicher.` Die Aufgabe bleibt bis
zum Neustart des Panels, also ist die Zahl von `wifi: radio up` nach der
ersten Anfrage von FILES bei jedem Funkstart niedriger. Auf einem Panel
fiel sie am 2026-10-29 um 23.456 Bytes, von 318.195 auf 294.739, zwischen
zwei Funkstarts, zwischen denen FILES und die Datei-API der Wi-Fi-Seite
benutzt wurden: Stack, Puffer und der Task-Kontrollblock von 108 Bytes
machen 23.148 Bytes davon aus, die übrigen 308 Bytes sind nicht
zugeordnet. Die Datei-API belegt keinen internen RAM
([Screens, Die Datei-API](Screens-de.md#die-datei-api)). Ihre
Schreibzugriffe nehmen den Body in dasselbe PSRAM und teilen die Sperre
des Karten-Tasks von FILES, einen FreeRTOS-Mutex, beim Start vom Heap
genommen; seine Bytes sind nicht gemessen. Jeder Schreibzugriff schreibt
den danach freien internen RAM und den ungenutzten Stack des Server-Tasks
ins Log:

```text
I (…) wifi: api: put OK, 1048576B in <t>ms; httpd stack <n>B unused, internal RAM <n>B free
```

Der Stack des Servers nach einem Schreibzugriff von 1MiB ist auf einem
Panel nicht gemessen.

`firmware/panel/main/psram_bss.lf` legt die nullinitialisierten Daten (.bss)
von 30 Objekten in den PSRAM (Pseudo-Static Random-Access Memory): den
Zustand der Bildschirme und der Helfer für Karte, Seite, Einstellungen und
Ausgänge. Das Fragment nennt jedes Objekt und die Regeln, die es erfüllt.
`tools/psram_bss.py` liest die Linker-Map des Builds und lässt CI
fehlschlagen, wenn ein genanntes Objekt .bss im internen RAM behält.
Statische Zahlen des P4-Builds mit ESP-IDF v5.5.5 mit 30 Objekten im
Fragment, aus `idf.py size`; auf v6.1 nicht wiederholt:

| Posten | Mit dem Fragment | Ohne es |
| --- | ---: | ---: |
| .bss im internen RAM (DIRAM) | 30 952 | 243 504 |
| .bss im PSRAM (`.ext_ram.bss`) | 212 552 | 0 |
| interner RAM, den das Image belegt: .text, .data und .bss im DIRAM, von 655 360 | 128 624 | 341 176 |
| Anwendungs-Image, gebaut ohne `RCBENCH_IOMCU_BIN` (ohne IOmcu-Image) | 2 439 488 | 2 439 488 |

Alle Zahlen sind Bytes. Die Spalte ohne Fragment zählt das .bss im PSRAM
zu den internen Zahlen; sie ist kein eigener Build. Die PSRAM-Einträge von ESP-IDF für lwIP
(Lightweight IP Stack) und wpa_supplicant nutzen ein Schema, das nur das
Fragment von `esp_wifi` definiert, und der P4-Build bindet es nicht ein;
die 3 774 Bytes .bss von lwIP bleiben im internen RAM.

## WLAN-Downloads

Eine Kartendatei oder die ZIP-Datei geht über eine TCP-Verbindung
(Transmission Control Protocol) vom Panel zum Browser. Der Server der
Seite beantwortet eine Anfrage nach der anderen: Eine Anfrage, die während
eines Downloads kommt, wartet auf dessen Ende. In einem Heimnetz lief ein
Download einer Datei von 16MiB mit 688-757kB/s, Kartenlesen und Funk
zusammen, nicht getrennt, bei einem Ping von im Mittel 14,6ms, und ein
Upload eines Image von 2,56MB über `/update` mit 556-599kB/s. Das Panel
meldete keinen SDIO-Fehler (Secure Digital Input/Output) und zeichnete
während der Downloads 60fps. Was die Rate begrenzt:

| Glied | Einstellung | Grenze |
| --- | --- | --- |
| Power Save | keiner, solange das Panel einem Heimnetz beigetreten ist (`esp_wifi_set_ps(WIFI_PS_NONE)`); ein Access Point schläft nicht | keine |
| Sendepuffer | 11 520 Bytes, 8 Segmente zu 1 440 Bytes (`CONFIG_LWIP_TCP_SND_BUF_DEFAULT`) | 11 520 Bytes pro Umlauf: 1 152 000B/s bei 10ms Umlaufzeit, 576 000B/s bei 20ms |
| Karte | SDMMC (der SD/MMC-Host), 4 Datenleitungen mit 20MHz (`BOARD_SD_FREQ_KHZ`), 10 000 000B/s auf der Leitung | nicht gemessen |
| Funk | der ESP32-C6 über SDIO, 4 Datenleitungen mit 40MHz (`BOARD_C6_FREQ_KHZ`), 20 000 000B/s auf der Leitung, am SDMMC-Host, den auch die Karte nutzt | nicht gemessen |
| Prozessor | die Task des Servers mit Priorität 1 auf Kern 0, neben der UI-Schleife (User Interface) mit Priorität 1 | nicht gemessen |

Der Power Save, den ESP-IDF vorgibt, `WIFI_PS_MIN_MODEM`, weckt das Funkteil
nur zu jedem DTIM-Beacon (Delivery Traffic Indication Message). Bis dahin
hält der Access Point die Frames für das Panel zurück, die Bestätigungen
von TCP darunter, also wartet jeder Sendepuffer bis zu einer DTIM-Periode:
102,4ms beim üblichen Beacon-Intervall und einer DTIM-Periode von 1,
307,2ms bei einer DTIM-Periode von 3. Mit dem Sendepuffer von 5 760 Bytes,
den ESP-IDF vorgibt, sind das 18 750 bis 56 250B/s. Ohne Power Save
braucht das Funkteil mehr Strom, solange es läuft; wie viel mehr am Panel,
ist nicht gemessen.

Der Stückpuffer der Seite, 4 096 Bytes, liegt im PSRAM. ESP-IDF v6.1
liest ein Stück per DMA (Direct Memory Access) hinein, wenn der Puffer auf
eine Cache-Line ausgerichtet ist, sonst über einen eigenen Puffer; welchen
der beiden Wege der Puffer der Seite nimmt, ist nicht gemessen. Bei der
gemessenen Umlaufzeit von 14,6ms begrenzt der Sendepuffer einen Download auf
789kB/s (11 520 Bytes je Umlauf, hergeleitet), über den gemessenen
688-757kB/s. Während ein Download läuft, nimmt die Task des Servers der
UI-Schleife Prozessorzeit; das Panel zeichnete während der gemessenen
Downloads 60fps.

Ein Download endet vorzeitig, und die Verbindung wird zurückgesetzt (Reset),
wenn

- der Empfänger die Verbindung schließt: erkannt innerhalb von 250ms
  (`WA_SEND_SLICE_MS`, `shared/webaccess/include/wa_send.h`);
- die Verbindung fehlschlägt;
- der Empfänger 30s lang nichts annimmt (`WA_SEND_QUIET_MS`);
- das WLAN ausgeht: innerhalb von 250ms;
- das Lesen der Karte fehlschlägt oder die Datei nicht die angekündigte
  Größe hat.

Der Browser hat dann weniger Bytes als angekündigt und meldet den Download
als fehlgeschlagen. Der Reset gibt die Bytes, die noch für die Verbindung
anstehen, sofort frei (`CONFIG_LWIP_SO_LINGER`); der Server nimmt die
nächste Anfrage an. Die Konsole protokolliert jede Datei:

```text
I (…) wifi: file: sent, 16777216 of 16777216 B read in <n> ms
W (…) wifi: file: receiver gone, <n> of 16777216 B read in <n> ms
```

Die anderen Enden lauten `nothing taken for 30 s`, `radio going off` und
`card read failed`.

### Einen Download messen

Auf einem Rechner im selben Heimnetz wie das Panel, mit `curl` und, unter
Linux, `iw`:

1. Eine Datei von 16 777 216 Bytes ins Wurzelverzeichnis der Karte
   kopieren: `head -c 16777216 /dev/urandom > RATE16M.BIN`.
2. Am Panel WI-FI öffnen, dem Heimnetz beitreten und den Zugangscode
   ausschalten. `<panel>` unten ist die Adresse, die WI-FI zeigt.
3. Die eigene Verbindung des Rechners notieren und seinen Power Save
   ausschalten: `iw dev wlan0 link`, `iw dev wlan0 get power_save`,
   `sudo iw dev wlan0 set power_save off`. Die Umlaufzeit notieren:
   `ping -c 20 <panel>`.
4. Die Rate, 3-mal:
   `curl -s -o /dev/null --max-time 600 -w '%{http_code} %{size_download} B %{time_total} s %{speed_download} B/s\n' http://<panel>/card/RATE16M.BIN`.
   Jeder Lauf antwortet `200 16777216 B`, und die Konsole protokolliert
   `file: sent, 16777216 of 16777216 B read in <n> ms`.
5. Ein Download, der mittendrin abbricht, und die nächste Anfrage:
   `curl -s -o /dev/null --max-time 5 http://<panel>/card/RATE16M.BIN; curl -s -o /dev/null -w '%{http_code} %{time_total} s\n' --max-time 10 http://<panel>/`.
   Die zweite antwortet `200`; die Konsole protokolliert
   `file: receiver gone`.
6. Ein Empfänger, der aufhört zu lesen: eine Verbindung, die die Datei
   anfragt und dann nichts liest,
   `python3 -c 'import socket,time; s=socket.create_connection(("<panel>",80)); s.sendall(b"GET /card/RATE16M.BIN HTTP/1.1\r\nHost: p\r\n\r\n"); time.sleep(60)' &`,
   dann nach 35s
   `curl -s -o /dev/null -w '%{http_code} %{time_total} s\n' --max-time 10 http://<panel>/`.
   Die Konsole protokolliert `file: nothing taken for 30 s`; die
   Anfrage antwortet `200`.

## Das Dateisystem der SD-Karte

Das Panel liest FAT12, FAT16, FAT32 und exFAT (extended File Allocation
Table) über FatFs, die Dateisystem-Bibliothek in ESP-IDF. ESP-IDF v6.1 baut
FatFs ohne exFAT, und keine Kconfig-Option schaltet es ein. Der Panel-Build
nimmt eine Kopie der Komponente `fatfs` von ESP-IDF mit `FF_FS_EXFAT` auf 1
(`firmware/panel/fatfs_exfat.cmake`), und `storage_list.c` hält einen Build
an, dessen FatFs kein exFAT hat. Lange Dateinamen bleiben, wie
`sdkconfig.defaults` sie setzt: an, 255 Zeichen, der Arbeitspuffer auf dem
Heap.

Was exFAT kostet, gelesen aus dem P4-Build mit ESP-IDF v5.5.5, auf v6.1
nicht wiederholt, gegen denselben Stand, gebaut ohne das Einbinden von `fatfs_exfat.cmake` und ohne
diesen Halt, beide ohne `RCBENCH_IOMCU_BIN`:

| Posten | Ohne exFAT | Mit exFAT | Wo |
| --- | ---: | ---: | --- |
| Anwendungs-Image | 2 425 520 | 2 434 880 | Flash, `ota_0` fasst 8 126 464 |
| interner RAM, den das Image belegt: .text, .data und .bss im DIRAM, aus `idf.py size` | 128 576 | 128 576 | intern |
| der Kontext des Mounts: `FATFS`, 4 offene Dateien (`FIL`) und der Zustand des VFS (Virtual File System) von ESP-IDF | 25 376 | 25 560 | Heap, zuerst PSRAM |
| der Arbeitspuffer für lange Namen, für einen FatFs-Aufruf belegt | 512 | 1 120 | Heap, zuerst PSRAM |
| der Puffer eines Verzeichnisdurchlaufs (`storage_walk()`) | 328 | 368 | Heap, intern |
| `FILINFO`, ein Eintrag, wie FatFs ihn liest, im Puffer von `storage_stat()` neben dem Pfad, für einen Aufruf | 280 | 288 | Heap, intern |

Alle Werte sind Bytes. Die Heap-Zeilen sind `sizeof()`-Werte, mit GDB aus
den beiden Builds gelesen; der Heap zur Laufzeit ist nicht gemessen. FatFs
legt seinen Puffer für lange Namen auf den Heap, nicht auf den Stack des
aufrufenden Tasks.

Die Größe einer Datei ist von FatFs bis zum Bildschirm 64 Bit breit. stat()
über das VFS liefert die Größe in `st_size`, einem `off_t` von 32 Bit auf
dem Panel; jede Größe, die das Panel verwendet, kommt deshalb aus
`f_stat()` oder `f_readdir()` von FatFs.

`FF_LBA64` (64-Bit-Sektornummern) bleibt 0. Es ist nötig für eine Karte mit
GPT-Partitionierung (GUID Partition Table; GUID: Globally Unique Identifier) und für ein Volume über 2TiB.
Damit wächst das P4-Image um weitere 5 056 Bytes, `FATFS` von 4 160 auf
4 184 Bytes und `FIL` von 4 176 auf 4 192 Bytes.
