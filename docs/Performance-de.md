# Performance

<sub>[English](Performance.md) · **Deutsch**</sub>

> Für die Arbeit am Zeichencode, nicht für die Benutzung des Prüfstands. Wer
> einen Bildschirm hinzufügt oder ändert, findet hier das Budget, in das er
> passen muss.

Die Frame Rate dieses Panels entscheidet nicht die CPU und nicht der Flip. Sie
entscheidet die **PSRAM-Bandbreite**.

Die LCD-Peripherie scannt einen Framebuffer fortlaufend mit rund 30 MB/s aus dem
PSRAM heraus, und der Framebuffer liegt hinter einem Write-Back-,
Write-Allocate-Cache — jedes Pixel, das die CPU schreibt, kostet also 128 Byte
Bustraffic, sofern seine 64-Byte-Line nicht schon da ist. Was ein Frame kostet,
ist damit eine Anzahl Cache-Line-Fills und keine Uhrzeit, und das lässt sich
exakt messen statt ungefähr stoppen.

`tools/frame_cost.py` tut genau das: es baut die echten Bildschirme für den Host
und lässt sie unter cachegrind laufen, mit dem Datencache des ESP32-S3
modelliert (64 KiB, 8-fach assoziativ, 64-Byte-Lines). Gemessen wird die
*Differenz* zwischen null gerenderten Frames und vielen, damit
Prozessstart, der erste Anstrich in jeden Buffer und cachegrinds eigener
Overhead sich herauskürzen.

<!-- framecost:start -->
```
$ python3 tools/frame_cost.py
panel 39.0 Hz, ~39 MB/s effective -> 976 KiB of traffic per panel frame

mode       lines/frame     traffic   est. ms  est. fps
-------------------------------------------------------
frame           10,904     1363 KiB     35.8      19.5
frame-idle          944      118 KiB      3.1      39.0
sim             11,716     1464 KiB     38.5      19.5
chrome          30,426     3803 KiB     99.9       9.8
overview           915      114 KiB      3.0      39.0
servo           15,448     1931 KiB     50.7      19.5
servo-grip        2,978      372 KiB      9.8      39.0
clear           12,006     1501 KiB     39.4      19.5
vlines           8,160     1020 KiB     26.8      19.5
hlines               0        0 KiB      0.0      39.0
```
<!-- framecost:end -->

## Die zwei Befunde, die jeden Bildschirm prägen

**Zeilenweise zeichnen.** Siebzehn senkrechte Linien über die volle Höhe kosten
8 160 Cache-Line-Fills. Dieselbe Pixelzahl als waagerechte Linien kostet
**null** — jeder Fill ist vom Pixel davor schon da. Deshalb schichtet sich die
Hülle in waagerechte Bänder statt in Spalten, und deshalb ist eine senkrechte
Linie eine bewusste Ausgabe und kein kostenloser Trenner.

**Das Chrome cachen.** Alles in jedem Frame neu zu malen kostet 24 896 Fills
gegen **740** im eingeschwungenen Zustand — das Dreißigfache an Traffic für
Pixel, die sich nicht geändert haben. Jeder Bildschirm hält je Framebuffer eine
Bitmaske dessen, was er schon gemalt hat; dafür ist das Argument `buffer_index`
von `render()` da. Das Panel wechselt zwischen zwei Buffern, ein Bildschirm, der
nur den gerade gezeichneten invalidiert, lässt den anderen also einen Frame
zurückfallen — und bei wechselnden Buffern liest sich das als *Flackern* und
nicht als offensichtlich altes Pixel.

## Was der Prüfstand kostet, und warum 19,5 fps das Ziel sind

| | Fills/Frame | fps |
| --- | ---: | ---: |
| Das Menü, und jeder Bildschirm mit gecachtem Chrome | **821** | 39,0 |
| Der Motorprüfstand, zwischen zwei Samples | **740** | 39,0 |
| Der Motorprüfstand, im Frame, in dem ein Sample landet | **10 264** | 19,5 |
| Derselbe, mit dem Simulations-Watermark | **11 825** | 19,5 |
| Gar nichts gecacht | 24 896 | 9,8 |

Der Prüfstandsbildschirm läuft mit der halben Panelrate, und das ist der
Auslegungspunkt und kein Defizit. Das Panel bewegt 976 KiB je Frame bei 39 Hz;
ein Render, das doppelt so viel kostet, landet bei **19,5 fps — genau einem
Frame je Telemetriesample mit 20 Hz**. Schneller zu zeichnen als die Zahlen
ankommen hieße, identische Pixel neu zu malen; langsamer hieße, Samples fallen
zu lassen.

Die Obergrenze ist damit **15 600 Fills** für die Prüfstandsmodi, und das ist
diese Schwelle und keine Vorliebe: darüber kann das Panel kein Frame je Sample
mehr liefern, und genau diese Regression ist es wert, gefangen zu werden.
Bildschirme mit gecachtem Chrome sind auf **2 000** festgenagelt, was ein Menü
fängt, das aufgehört hat zu cachen.

**Nur in den Frames malen, in denen es etwas zu malen gibt.** Samples kommen mit
20 Hz, das Panel frischt mit 39 auf — ungefähr jeder zweite Frame hat also
nichts Neues zu zeigen. Der Prüfstandsbildschirm hält den Push-Zähler des Plots
und eine Control-Revision, jeweils je Framebuffer, und malt Plot, Anzeigen und
Bedienelemente nur neu, wenn der zugehörige Zähler sich bewegt hat. Ein Frame
zwischen zwei Samples, ohne dass jemand den Bildschirm berührt, kostet **740
Fills** — das gecachte Chrome und sonst nichts.

Das ist die Änderung, die den Spielraum gebracht hat, den die Tabelle jetzt
zeigt: der schlechteste Frame in der Simulation ging von 15 603 auf 11 825 gegen
die Grenze von 15 600, und der typische Frame von 14 042 auf 740. Es wiegt
schwerer, als die Mittelwerte nahelegen, denn der vermiedene Traffic ist nicht
bloß verschwendet — er konkurriert mit dem Scan-out des LCD um dasselbe PSRAM,
und genau das hat den ersten Hardwarestart reißen lassen.

Die Zähler liegen je Framebuffer, aus dem oben genannten Grund: das Panel
wechselt zwischen zweien, ein Buffer, dessen letzter Anstrich ein Sample her
ist, braucht also einen — auch wenn der andere aktuell ist.
`each_framebuffer_is_updated_independently` in `test_motor` nagelt das fest, und
einen der beiden Zähler auf einen einzelnen Slot zusammenzulegen bringt den Test
zu Fall.

Wenn ein künftiger Bereich noch mehr Platz braucht, sind die verbleibenden Hebel
in der Reihenfolge ihrer Grobheit: die Höhe des Plots, seine Breite, und das
Watermark der Simulation auf den tatsächlich neu gemalten Bereich zu clippen.

Die Tabelle oben prüft `frame_cost.py --check-doc`, mit einer Toleranz statt Byte
für Byte: die absoluten Fills verschieben sich zwischen Maschinen um ein paar,
weil argv und die Umgebung in demselben Cache landen, um den der Framebuffer
konkurriert.
