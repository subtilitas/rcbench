# Performance

<sub>[English](Performance.md) · **Deutsch**</sub>

Das Zeichenbudget für alle, die einen Bildschirm hinzufügen oder ändern.

## Die Randbedingung

Die Frame Rate dieses Panels begrenzt die Bandbreite des PSRAM (Pseudo-Static
Random-Access Memory), nicht die CPU (Central Processing Unit). Die
LCD-Einheit (Liquid-Crystal Display) liest ununterbrochen einen Framebuffer
mit etwa 30 MB/s aus dem PSRAM, und die Framebuffer liegen hinter einem
Write-Back-, Write-Allocate-Datencache (64 KB, 8-fach assoziativ,
64-Byte-Lines). Jedes Pixel, das die CPU schreibt, kostet einen
64-Byte-Line-Fill und ein 64-Byte-Write-Back, sofern die Line nicht schon
geladen ist. Die Kosten eines Frames sind deshalb eine Anzahl von
Cache-Line-Fills, und die lässt sich auf dem Host exakt messen.

`tools/frame_cost.py` baut die echten Bildschirme für den Host und lässt sie
unter cachegrind mit der Cache-Geometrie des ESP32-S3 laufen. Es meldet die
Differenz zwischen einem und elf gerenderten Frames, sodass Prozessstart und
das erste Füllen jedes Buffers herausfallen.

<!-- framecost:start -->
```
$ python3 tools/frame_cost.py
panel 39.0 Hz, ~39 MB/s effective -> 976 KiB of traffic per panel frame

mode       lines/frame     traffic   est. ms  est. fps
-------------------------------------------------------
frame           10,881     1360 KiB     35.7      19.5
frame-idle          895      112 KiB      2.9      39.0
sim             11,891     1486 KiB     39.0      19.5
chrome          30,559     3820 KiB    100.3       9.8
overview           889      111 KiB      2.9      39.0
servo           15,544     1943 KiB     51.0      19.5
servo-grip        2,986      373 KiB      9.8      39.0
analyser           835      104 KiB      2.7      39.0
logs               860      108 KiB      2.8      39.0
settings           830      104 KiB      2.7      39.0
battery            836      104 KiB      2.7      39.0
balance            822      103 KiB      2.7      39.0
programmer          815      102 KiB      2.7      39.0
balance-sim        2,408      301 KiB      7.9      39.0
settings-sim        2,417      302 KiB      7.9      39.0
battery-sim        2,417      302 KiB      7.9      39.0
analyser-chrome          850      106 KiB      2.8      39.0
logs-chrome       16,205     2026 KiB     53.2      13.0
settings-chrome       22,607     2826 KiB     74.2      13.0
battery-chrome          851      106 KiB      2.8      39.0
balance-chrome          833      104 KiB      2.7      39.0
programmer-chrome          833      104 KiB      2.7      39.0
clear           12,005     1501 KiB     39.4      19.5
vlines           8,160     1020 KiB     26.8      19.5
hlines               0        0 KiB      0.0      39.0
```
<!-- framecost:end -->

| Modus | Was er misst |
| --- | --- |
| `frame` | der Motorprüfstand auf einem Frame, in dem ein Telemetriesample eintrifft |
| `frame-idle` | der Motorprüfstand auf einem Frame zwischen zwei Samples, ohne Berührung |
| `sim` | wie `frame`, mit dem SIMULATION-Watermark |
| `chrome` | der Motorprüfstand ohne Cache, vollständig neu gezeichnet |
| `overview` | das Menü, Chrome gecacht |
| `servo` | der Servobildschirm mit neu gezeichnetem Arm |
| `servo-grip` | der Servobildschirm, nur der Griff neu gezeichnet |
| `analyser`, `logs`, `settings`, `battery`, `balance`, `programmer` | ein ruhiger Frame dieses Bildschirms, Chrome gecacht |
| `<screen>-sim` | derselbe Bildschirm mit dem SIMULATION-Watermark |
| `<screen>-chrome` | derselbe Bildschirm, auf jedem Frame invalidiert |
| `clear` | ein Löschen des ganzen Bildschirms |
| `vlines` | siebzehn senkrechte Linien über die volle Höhe |
| `hlines` | dieselbe Pixelzahl als waagerechte Linien |

Die absoluten Zahlen verschieben sich zwischen Maschinen um einige Fills, weil
argv und die Umgebungsvariablen denselben Cache belegen wie der Framebuffer.
`frame_cost.py --check-doc` prüft die Tabelle deshalb mit einer Toleranz von
1 %.

## Regeln

**Zeilenweise zeichnen.** Siebzehn senkrechte Linien über die volle Höhe
kosten 8 160 Fills; dieselbe Pixelzahl als waagerechte Linien kostet null,
weil jede Line vom vorigen Pixel noch geladen ist. Die Oberfläche ist in
waagerechte Bänder gegliedert, und eine senkrechte Trennlinie ist eine
bewusste Ausgabe.

**Das Chrome cachen.** Alles neu zu zeichnen kostet etwa das Dreißigfache des
eingeschwungenen Zustands. Jeder Bildschirm führt je Framebuffer eine
Bitmaske dessen, was er schon gezeichnet hat; dafür ist das Argument
`buffer_index` von `render()` da. Das Panel wechselt zwischen zwei Buffern;
ein Bildschirm, der nur den gerade gezeichneten Buffer invalidiert, lässt den
anderen einen Frame zurück, was als Flackern erscheint.

**Ein Stencil, das sich nicht bewegt, wird gecacht.** SIMULATION wird auf
jedem Frame gezeichnet, sobald die Prüfstandswerte `LINK_BN_SIMULATED` tragen,
also auch bei einem Coprozessor, der mit simulierten Werten antwortet.
Gedrehter Text scannt seine gedrehte Bounding-Box, von Ecke zu Ecke also die
ganze Canvas, und rotiert und dividiert je Pixel, um die 3 439 Pixel zu
schreiben, die das Watermark bedeckt: 0,9 % der Canvas. `ui_watermark` nimmt
diese Punkte einmal auf und schreibt sie danach nur noch, mit 144 721
Instruktionen je Frame statt 8 412 078.

**Eine Zahl von Fills ist keine Zahl von Zyklen.** Die Tabelle oben misst
Cache-Line-Fills, und das Watermark kostet davon nur 1 586: es ist Arithmetik
je Pixel, kein Traffic. Es kostete das 58-Fache dessen, was die Tabelle nahelegte,
und die Tabelle konnte das nicht zeigen. Wo die gemessene Frame-Zeit eines Modus
über dem liegt, was seine Fills vorhersagen, erst Instruktionen zählen, dann der
Schätzung trauen.

**Nur auf Frames zeichnen, die etwas zu zeichnen haben.** Samples kommen mit
20 Hz, das Panel zeichnet mit 39 Hz, also hat etwa jeder zweite Frame nichts
Neues. Der Prüfstandsbildschirm führt den Push-Zähler des Plots und eine
Revisionsnummer der Bedienelemente, jeweils je Framebuffer, und zeichnet Plot,
Anzeigen und Bedienelemente nur neu, wenn der zugehörige Zähler sich bewegt
hat. `each_framebuffer_is_updated_independently` in `test_motor` hält die
Zähler je Buffer fest.

## Obergrenzen

Das Panel bewegt 976 KiB je Frame bei 39 Hz. Ein Frame, der das Doppelte
kostet, landet bei 19,5 fps (Frames pro Sekunde), also einem Frame je
20-Hz-Telemetriesample; schneller zu zeichnen würde identische Pixel neu
malen, langsamer würde Samples verlieren. CI (Continuous Integration) hält
jeden Modus an eine Obergrenze:

| Modi | Obergrenze (Fills) | Fängt |
| --- | ---: | --- |
| `frame`, `sim` | 15 600 | einen Prüfstandsframe, der ein Telemetriesample überschreitet |
| `overview` | 2 000 | einen Bildschirm mit gecachtem Chrome, der neu zu zeichnen begonnen hat |
| `servo` | 17 000 | ein Wachsen der Arm- und Griffzeichnung |
| `servo-grip` | 4 000 | ein Atmen, das die ganze Karte neu zeichnet |
| die sechs Bildschirmmodi | 1 200 | einen Bildschirm, der neu zu zeichnen begonnen hat |
| die drei `-sim`-Modi | 2 800 | ein Watermark, das über die volle Canvas hinauswächst |

Braucht ein künftiger Bereich mehr Platz, sind die verbleibenden Hebel vom
gröbsten zum feinsten: die Höhe des Plots, seine Breite, und das
Simulations-Watermark auf den tatsächlich neu gezeichneten Bereich zu
clippen.

Die Logzeile `DRAW … WAIT …` des ESP32-S3, alle 300 Frames ausgegeben, ist
die Prüfung auf der Hardware: DRAW ist die Zeichenzeit, WAIT die Zeit, die
der Buffer-Wechsel blockiert hat. Ein gesunder Frame besteht überwiegend aus
WAIT.
