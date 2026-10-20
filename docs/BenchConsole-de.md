# Die Prüfstandskonsole

<sub>[English](BenchConsole.md) · **Deutsch**</sub>

Die Prüfstandskonsole ist ein Werkzeug für den Prüfstand. Sie ist in keinem
veröffentlichten Image. Ein IOmcu, der mit der Option `IOMCU_CONSOLE` gebaut
ist, liest Kommandozeilen auf seiner seriellen USB-Schnittstelle (Universal
Serial Bus) und liest und schreibt damit die Link-Pages aus
[Der Link](Link-de.md). Eine Station an dieser Schnittstelle bedient die
Pages des IOmcu ohne einen Bildschirm des Panels dafür: die KST-Page, SUPPLY,
PROG, HITEC und jede andere Page.

Jede Anfrage geht durch die Page-Handler, die der Link benutzt. Eine Page
weist der Konsole ab, was sie dem Panel abweist: ein Schreiben von ARM bei
gesetzter Arm-Sperre, ein KST- oder HITEC-Kommando ohne seinen
Schreibschlüssel oder mit falscher Prüfsumme, ein Einschalten von SUPPLY
ohne Heartbeat, eine Transaktion nach einem Stopp. Die Konsole hat keine
Rechte, die das Panel nicht hat.

Eine Anfrage der Konsole ist kein Link-Verkehr. Sie hält den Failsafe des
stillen Links (200 ms) nicht auf, und sie ist kein Heartbeat. Jede Page, die
einen vertrauenswürdigen Heartbeat und einen nicht stillen Link braucht,
braucht ein laufendes Panel neben der Konsole: das Einschalten von SUPPLY,
eine KST-Session, jedes HITEC-Kommando außer ABORT und CLOSE und ein ARM.
Ohne Panel werden sie abgewiesen oder gestoppt, wie auf einem Prüfstand,
dessen Panel stehen geblieben ist.

Die Konsole kann schreiben, was das Panel schreiben kann, CONTROL
eingeschlossen. Ein Schreiben von ARM aus der Konsole schaltet die Bank auf
ARMED, wenn jede Regel des Safety Gate es zulässt ([Sicherheit](Safety-de.md)).
Ein CLEAR aus der Konsole löst die Arm-Sperre und den Failsafe wie das des
Panels; der Link wird 200 ms nach der letzten Anfrage des Panels wieder
still. Die Konsole nur auf einem Prüfstand ohne Propeller und mit
gesicherten Ausgängen benutzen, wie bei jeder Arbeit am Prüfstand.

## Bauen

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware/iomcu -B firmware/iomcu/build-console -DIOMCU_CONSOLE=ON
cmake --build firmware/iomcu/build-console
```

Die Option ist standardmäßig aus. Die CI (Continuous Integration) baut das
Image mit ihr, prüft die Stacks beider Kerne und schlägt fehl, wenn das
Standard-Image ein Symbol der Konsole enthält. `IOMCU_CONSOLE` und
`SENSE_TRACE` lesen beide die USB-Konsole; CMake weist einen Build mit
beiden ab.

Gebaut mit ARM GNU 14.2 und pico-sdk 2.3.0 ist das Konsolen-Image 10.860
Byte größer als das Standard-Image und belegt 4.124 Byte mehr RAM
(Random-Access Memory): 2.048 davon der Ausgabepuffer, 960 der USB-Sendepuffer
von 1.024 Byte statt der 64 des SDK. Die tiefste
Aufrufkette von Kern 0 ist die des Standard-Images: die Konsole hält ihren
Arbeitsspeicher in ihrem eigenen Zustand, nicht auf dem Stack.

## Zeilen

Eine Zeile endet mit CR oder LF und hält höchstens 160 Zeichen; eine längere
Zeile wird ganz abgewiesen. Tokens sind durch Leerzeichen getrennt. Zahlen
sind dezimal oder hexadezimal mit `0x`. Die Konsole nimmt bis zu 64 Zeichen
je Durchlauf der Hauptschleife des IOmcu und gibt weder Prompt noch Echo
aus.

Jede Antwort ist eine Zeile oder mehr, jede beginnt mit einem Wort:

| Wort | Bedeutet |
| --- | --- |
| `data` | die Register eines Lesens, hexadezimal |
| `ack` | ein angenommenes Schreiben; die Register, wie die Page sie gespeichert hat |
| `nack` | ein abgewiesenes Schreiben oder Lesen, mit dem Grund des Links: `BAD_PAGE` (1), `BAD_RANGE` (2), `READ_ONLY` (3), `BAD_VALUE` (4), `NOT_ARMED` (5), `BUSY` (6) |
| `err` | die Konsole hat die Zeile nicht verstanden, oder die Page hat nicht geantwortet |
| `page` | eine Zeile von `pages` |
| `hitec` | ein hitec-Kommando und sein Bericht |

Die Ausgabe geht in ganzen Zeilen durch einen Puffer von 2.048 Byte hinaus,
so schnell der USB-Host sie nimmt. Was nicht passt, fällt weg, und eine
Zeile `err output full, N bytes dropped` folgt.

## Rohe Pages

| Kommando | Tut |
| --- | --- |
| `help` | die Kommandos |
| `pages` | jede Page: Nummer, Register, `rw` oder `ro` |
| `r <page> <off> [n]` | n Register lesen, 1 bis 32, 1 wenn weggelassen |
| `w <page> <off> <v> [v ...]` | 1 bis 32 Register zu 16 Bit schreiben |

```text
> r 0x00 0 2
data page=0x00 off=0 n=2: 0x0004 0x0017
> w 0x2A 4 1 5000 1000
nack page=0x2A off=4: NOT_ARMED (5)
```

Getippte Zeilen stehen hier nach `> `; die Konsole gibt keinen Prompt aus.
Die erste Zeile liest die Protokollversion, 4.23. Die zweite ist das
Einschalten von SUPPLY mit 5000 mV und 1000 mA, abgewiesen, weil kein Panel
einen Heartbeat sendet. Mit laufendem Panel wird dieselbe Zeile angenommen:

```text
> w 0x2A 4 1 5000 1000
ack page=0x2A off=4 n=3: 0x0001 0x1388 0x03E8
```

Die Programmierports und eine KST-Session gehen genauso. CMD 0x0041 ist
ENTER (1) auf Port 0 mit gesetztem Bit 6; `SEQ` ist die der Page plus 1:

```text
> r 0x33 0 8
> w 0x32 26 5000 1000
> r 0x32 1 1
data page=0x32 off=1 n=1: 0x0000
> w 0x32 0 0x0041 1 0 0
> r 0x32 0 4
```

[Der Programmierport für Servos von KST und Chaservo](LinkKST-de.md) nennt
die Register, den Schlüssel und die Prüfsumme.

## Hitec

Die `hitec`-Kommandos schreiben die HITEC-Page
([Der Programmierport für Servos von Hitec](LinkHitec-de.md)). Jedes stellt
die Register 4 bis 21 der Page bereit, rechnet ihre Prüfsumme, liest `SEQ`
und sendet das Kommando mit `SEQ` plus 1. Es setzt den Schreibschlüssel
0x48A5 in jedes SEND und in ein LISTEN oder POWER, das antwortet. Ein
Kommando, das die Page abweist, wird sofort gemeldet:

```text
hitec SEND port=0 refused STOPPED
```

Ein Kommando, das etwas startet, druckt `hitec <op> port=<n> started`, und
der Bericht folgt, wenn der Port damit fertig ist. Ein Kommando, das den
Port in seinem eigenen Frame frei oder offen lässt, etwa CLOSE oder CLEAR,
druckt den Bericht sofort. Die Zeilen des Berichts, die letzte für jedes
empfangene Byte:

```text
hitec done state=<state> port=<n> flags=<flags> seq=<n> op=<op> count=<n> result=<result> refusal=<refusal>
hitec line pin=<pin> line=<line> bit_ns=<ns> lead_ns=<ns> span_us=<us>
hitec cap n=<n> full=<0|1> answered=<0|1> overflow=<0|1> aborted=<0|1> dec=<dec> rx=<rx> id=<id> value=<value> delay_ns=<ns> t_us=<us> window_us=<us>
hitec byte <k> <value> at_ns=<ns> framing=<0|1>
```

Die Namen sind die der Page: die Zustände, Operationen, Ergebnisse und
Abweisungen aus
[Der Programmierport für Servos von Hitec](LinkHitec-de.md#register) und die
Ergebnisse der Dekoder aus `hitec_rx_t`. `hitec cap` und `hitec byte` kommen
nur, solange die Page einen Mitschnitt hält.

`lead_ns` reicht von der ersten Flanke des Vorlaufs bis zum Loslassen der
Leitung: der Vorlauf und jede gesendete Bitzeit. Der Vorlauf ist der
verlangte oder länger: das PIO-Programm (Programmable Input/Output) sendet
ganze Wörter von 16 Bitzeiten, und die Bitzeiten, die den Anfang des Stroms
auf ein Wort auffüllen, zählen zum Vorlauf. Ein Lesen der D-Serie ergibt
`lead_ns=559886`, bei einer Bitzeit von 8,68 us:

| Teil | Bitzeiten | Zeit |
| --- | ---: | ---: |
| Vorlauf: 8 Takte und die 13 Bitzeiten Auffüllung (88 us verlangt) | 13,5 | 117,2 us |
| 5 Bytes zu 10 Bit | 50 | 434,0 us |
| 1 losgelassene Bitzeit, bevor der Empfänger startet | 1 | 8,7 us |
| `lead_ns` | 64,5 | 559,9 us |

Ein Schreiben der D-Serie ergibt `lead_ns=705283`: 89,0 us Vorlauf, 70
Bitzeiten Bytes und 1 losgelassene Bitzeit.

| Kommando | Tut |
| --- | --- |
| `hitec [state]` | die Page, dekodiert, mit dem Mitschnitt |
| `hitec open <port> c\|d [od\|pp]` | OPEN: classic oder D-Serie, Open Drain (Standard) oder Push-Pull |
| `hitec close <port>` | CLOSE |
| `hitec abort <port>` | ABORT |
| `hitec clear <port>` | CLEAR, nach einem Stopp |
| `hitec send <port> [lead=<us>] [gap=<bits>] [window=<us>] [rx=<n>] [dec=<dec>] <byte>...` | SEND von 1 bis 8 Bytes |
| `hitec listen <port> window=<us> rx=<n> [answer=<byte>] [dec=<dec>]` | LISTEN |
| `hitec level <port> 0\|1 <us>` | LEVEL |
| `hitec power <port> mv=<mV> ma=<mA> off=<ms> window=<us> rx=<n> [answer=<byte>] [dec=<dec>]` | POWER: die Versorgung aus, aus gehalten, an mit 4800 bis 6000 mV, und das LISTEN ab dem Einschalten |
| `hitec c <port> <cmd> <p1> <p2>` | ein classic-Kommando mit seinem Summenbyte, 1 Ruhebit zwischen den Bytes, einem Fenster von 1500 us für 2 Bytes und dem classic-Dekoder, wenn das Modul das Kommando kennt |
| `hitec d <port> read <id> <addr>` | ein Lesen der D-Serie: 88 us Vorlauf verlangt, 117,2 us low getrieben, 5 Bytes, ein Fenster von 25.000 us für 8 Bytes und der Dekoder der D-Serie |
| `hitec d <port> write <id> <addr> <value>` | ein Schreiben der D-Serie: 88 us Vorlauf verlangt, 89,0 us low getrieben, 7 Bytes, nichts empfangen |

`<dec>` ist `none`, `c:<cmd>`, `d:<addr>` oder `l:<byte>`.

Eine Leitung der D-Serie braucht einen Pull-up auf der Platine. Getestet mit
einem D485HW: 2,2 kOhm von der Signalleitung zur Servoversorgung, und jedes
Lesen wird beantwortet; mit dem Pull-up des Pads allein beantwortet der
Servo keines (`rx=SILENT`).
[Der Programmierport für Servos von Hitec](LinkHitec-de.md#hinein-und-hinaus)
nennt, was offen ist.

`hitec c` braucht eine offene classic-Leitung und `hitec d` eine offene
Leitung der D-Serie. Auf einer Leitung der anderen Familie schreibt die
Konsole nichts und sagt es:

```text
err the line is classic; this command needs a D series line
```

Eine Session von Hand, classic, Port 0:

```text
> hitec open 0 c
hitec OPEN port=0 started
hitec done state=OPEN port=0 ...
> hitec c 0 0x67 0 0
> hitec c 0 0x61 0x12 0
> hitec power 0 mv=5000 ma=1000 off=1000 window=2000000 rx=1 dec=l:0x2B
> hitec close 0
```

Ein rohes `w` der HITEC-Page bekommt keinen Bericht und keinen Schlüssel;
das [Beispiel](LinkHitec-de.md#eine-transaktion-von-hand) schreibt eine
Transaktion so.

## Grenzen

- Die Konsole liest nichts, solange der IOmcu Flash schreibt: eine dann
  getippte Zeile wartet im USB-Puffer.
- Der Ausgabepuffer hält 2.048 Byte. Ein Bericht über 16 empfangene Bytes
  belegt etwa 1.100.
- Ein Durchlauf gibt dem USB-Port so viele Bytes, wie sein Sendepuffer frei
  hat, höchstens 400, und wartet nie auf den Host. Eine längere Zeile geht
  in Stücken über mehrere Durchläufe hinaus; ihr Rest zuerst. Jede Zeile bis
  zur längsten, 200 Byte mit ihrem LF, geht auch mit dem 64-Byte-Puffer des
  SDK hinaus.
- Die eigenen Zeilen des IOmcu, `rcbench-iomcu: ...` alle 3 s und nach einem
  Flash-Schreiben, laufen in diesem Build durch dieselbe Ausgabe: sie kommen
  nach der Zeile davor, nie mitten in ihr, und enden nur mit LF.
- `shared/outputs/page_console.c` hält die Regeln, und
  `test/host/test_page_console.c` testet sie gegen ein Modell des Treibers
  der HITEC-Page.
