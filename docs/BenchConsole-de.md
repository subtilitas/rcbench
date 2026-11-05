# Die Prüfstandskonsole

<sub>[English](BenchConsole.md) · **Deutsch**</sub>

Die Prüfstandskonsole liest und schreibt die Link-Pages des IOmcu über
Kommandozeilen. Sie ist nur in Debug-Builds verfügbar, nicht in
veröffentlichten Images. Baue den IOmcu mit der Option `IOMCU_CONSOLE`,
um seine serielle USB-Schnittstelle (Universal Serial Bus) dafür zu
nutzen. Eine daran angeschlossene Station kann auf KST, SUPPLY, PROG,
HITEC und die übrigen Pages aus [Der Link](Link-de.md) zugreifen, auch
wenn das Panel keine eigene Seite dafür hat.

Jede Anfrage durchläuft dieselben Page-Handler wie eine Anfrage über den
Link. Es gelten dieselben Ablehnungen: ein Schreiben von ARM bei
gesetzter Arm-Sperre, ein KST- oder HITEC-Kommando ohne Schreibschlüssel
oder mit falscher Prüfsumme, ein Einschalten von SUPPLY ohne Heartbeat
und eine Transaktion nach einem Stopp. Die Konsole hat dieselben Rechte
wie das Panel.

Eine Konsolenanfrage zählt weder als Link-Verkehr noch als Heartbeat. Sie
verhindert deshalb nicht, dass der Failsafe nach 200ms ohne Link-Verkehr
auslöst. Vorgänge, die einen vertrauenswürdigen Heartbeat und einen
aktiven Link brauchen, benötigen auch ein laufendes Panel: das
Einschalten von SUPPLY, eine KST-Session, jedes HITEC-Kommando außer ABORT
und CLOSE sowie ARM. Ohne Panel werden diese Vorgänge abgelehnt oder
gestoppt, ebenso wie beim Ausfall des Panels.

Die Konsole kann schreiben, was das Panel schreiben kann, CONTROL
eingeschlossen. Ein Schreiben von ARM aus der Konsole schaltet die Bank auf
ARMED, wenn jede Regel des Safety Gate es zulässt ([Sicherheit](Safety-de.md)).
Ein CLEAR aus der Konsole löst die Arm-Sperre und den Failsafe wie das des
Panels; der Link wird 200ms nach der letzten Anfrage des Panels wieder
still. Benutze die Konsole wie bei jeder Arbeit am Prüfstand nur ohne
Propeller und mit gesicherten Ausgängen.

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

Gebaut mit ARM GNU 14.2 und pico-sdk 2.3.0 ist das Konsolen-Image 17.444
Byte größer als das Standard-Image und belegt 9.664 Byte mehr RAM
(Random-Access Memory): 2.048 davon der Ausgabepuffer, 960 der USB-Sendepuffer
von 1.024 Byte statt der 64 des SDK, 3.488 der Pin-Treiber der Signalleitung
und 1.536 die Antwort seines Tests. Die tiefste
Aufrufkette von Kern 0 ist die des Standard-Images: die Konsole hält ihren
Arbeitsspeicher in ihrem eigenen Zustand, nicht auf dem Stack.

## Zeilen

Eine Zeile endet mit CR oder LF und enthält höchstens 160 Zeichen.
Längere Zeilen werden vollständig abgelehnt. Tokens sind durch
Leerzeichen getrennt. Zahlen sind dezimal oder mit `0x` hexadezimal. Die
Konsole verarbeitet bis zu 64 Zeichen je Durchlauf der Hauptschleife des
IOmcu. Sie gibt weder einen Prompt noch ein Echo aus.

Eine Antwort besteht aus einer oder mehreren Zeilen. Jede beginnt mit
einem dieser Wörter:

| Wort | Bedeutet |
| --- | --- |
| `data` | die Register eines Lesens, hexadezimal |
| `ack` | ein angenommenes Schreiben; die Register, wie die Page sie gespeichert hat |
| `nack` | ein abgewiesenes Schreiben oder Lesen, mit dem Grund des Links: `BAD_PAGE` (1), `BAD_RANGE` (2), `READ_ONLY` (3), `BAD_VALUE` (4), `NOT_ARMED` (5), `BUSY` (6) |
| `err` | die Konsole hat die Zeile nicht verstanden, oder die Page hat nicht geantwortet |
| `page` | eine Zeile von `pages` |
| `hitec` | ein hitec-Kommando und sein Bericht |

Die Ausgabe durchläuft einen Puffer von 2.048 Byte in ganzen Zeilen.
Sie wird so schnell übertragen, wie der USB-Host oder das Panel über den
Link sie annimmt. Zeilen, die nicht hineinpassen, werden verworfen.
Danach folgt `err output full, N bytes dropped`.

## Rohe Pages

| Kommando | Tut |
| --- | --- |
| `help` | die Kommandos |
| `pages` | jede Page: Nummer, Register, `rw` oder `ro` |
| `r <page> <off> [n]` | n Register lesen, 1 bis 32, 1 wenn weggelassen |
| `w <page> <off> <v> [v ...]` | 1 bis 32 Register zu 16 Bit schreiben |

```text
> r 0x00 0 2
data page=0x00 off=0 n=2: 0x0004 0x001B
> w 0x2A 4 1 5000 1000
nack page=0x2A off=4: NOT_ARMED (5)
```

Getippte Zeilen stehen hier nach `> `; die Konsole gibt keinen Prompt aus.
Die erste Zeile liest die Protokollversion, 4.27. Die zweite ist das
Einschalten von SUPPLY mit 5000mV und 1000mA, abgewiesen, weil kein Panel
einen Heartbeat sendet. Mit laufendem Panel wird dieselbe Zeile angenommen:

```text
> w 0x2A 4 1 5000 1000
ack page=0x2A off=4 n=3: 0x0001 0x1388 0x03E8
```

Dieselben Kommandos greifen auf die Programmierports zu und starten eine
KST-Session. CMD 0x0041 ist ENTER (1) auf Port 0 mit gesetztem Bit 6.
Verwende den aktuellen Wert von `SEQ` der Page plus 1:

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

Die `hitec`-Kommandos schreiben auf die HITEC-Page
([Der Programmierport für Servos von Hitec](LinkHitec-de.md)). Jedes belegt
die Register 4 bis 21, berechnet ihre Prüfsumme und liest `SEQ`. Es sendet
das Kommando mit `SEQ` plus 1. Der Schreibschlüssel 0x48A5 wird bei jedem
SEND sowie bei LISTEN oder POWER mit Antwort gesetzt. Eine Ablehnung
durch die Page wird sofort gemeldet:

```text
hitec SEND port=0 refused STOPPED
```

Ein Kommando, das einen Vorgang startet, gibt
`hitec <op> port=<n> started` aus. Der Bericht folgt nach Abschluss des
Vorgangs. Kommandos, die den Port innerhalb ihres Frames freigeben oder
offen lassen, etwa CLOSE oder CLEAR, geben den Bericht sofort aus. Er
enthält diese Zeilen; die letzte wird für jedes empfangene Byte wiederholt:

```text
hitec done state=<state> port=<n> flags=<flags> seq=<n> op=<op> count=<n> result=<result> refusal=<refusal>
hitec line pin=<pin> line=<line> rx=<pu|pre> bit_ns=<ns> lead_ns=<ns> span_us=<us>
hitec cap n=<n> full=<0|1> answered=<0|1> overflow=<0|1> aborted=<0|1> dec=<dec> rx=<rx> id=<id> value=<value> delay_ns=<ns> t_us=<us> window_us=<us>
hitec byte <k> <value> at_ns=<ns> framing=<0|1>
```

Die Namen sind die der Page: die Zustände, Operationen, Ergebnisse und
Abweisungen aus
[Der Programmierport für Servos von Hitec](LinkHitec-de.md#register) und die
Ergebnisse der Dekoder aus `hitec_rx_t`. `hitec cap` und `hitec byte` kommen
nur, solange die Page einen Mitschnitt hält.

`hitec` und `hitec state` drucken denselben Bericht mit `now` statt
`done`:

```text
hitec now state=OPEN port=0 flags=RAIL ...
```

`flags=PRECHARGE` und `rx=` beschreiben unterschiedliche Zustände.
`PRECHARGE` in `flags` entspricht Register 23: dem Empfangsmodus für das
nächste OPEN einer Leitung der D-Serie. `rx=` in der Zeile `hitec line`
zeigt den Modus des letzten OPEN und bleibt auch nach CLOSE erhalten.
Wird Register 23 bei offener Leitung geschrieben, können beide Angaben
abweichen. Die offene Leitung behält ihren Modus bis CLOSE. Das Panel schreibt Register 23 bei jedem Link-Aufbau und bei jeder
Änderung von Hitec-D-Empfang in der Liste WEITERES des Hitec-Tabs:
jeder auf SETUP gesetzte Wert gilt sofort.

`lead_ns` reicht von der ersten Flanke des Vorlaufs bis zum Loslassen der
Leitung: der Vorlauf und jede gesendete Bitzeit. Der Vorlauf ist der
verlangte oder länger: das PIO-Programm (Programmable Input/Output) sendet
ganze Wörter von 16 Bitzeiten, und die Bitzeiten, die den Anfang des Stroms
auf ein Wort auffüllen, zählen zum Vorlauf. Ein Lesen der D-Serie ergibt
`lead_ns=559886`, bei einer Bitzeit von 8,68us:

| Teil | Bitzeiten | Zeit |
| --- | ---: | ---: |
| Vorlauf: 8 Takte und die 13 Bitzeiten Auffüllung (88us verlangt) | 13,5 | 117,2us |
| 5 Bytes zu 10 Bit | 50 | 434,0us |
| 1 losgelassene Bitzeit, bevor der Empfänger startet | 1 | 8,7us |
| `lead_ns` | 64,5 | 559,9us |

Ein Schreiben der D-Serie ergibt `lead_ns=705283`: 89,0us Vorlauf, 70
Bitzeiten Bytes und 1 losgelassene Bitzeit.

| Kommando | Tut |
| --- | --- |
| `hitec [state]` | die Page, dekodiert, mit dem Mitschnitt |
| `hitec open <port> c\|d [od\|pp] [pu\|pre]` | OPEN auf Port 0 bis 3 (PROG-PORT 1 bis 4; vor Protokoll 4.26 nur 0): classic oder D-Serie, Open Drain (Standard) oder Push-Pull. Für die D-Serie schreibt `pu` oder `pre` zuerst den Empfangsmodus, PULL-UP oder PRECHARGE (Register 23, [Empfangsmodus](LinkHitec-de.md#empfangsmodus)); ohne beides nimmt das OPEN den Modus, den die Page hält. Classic weist beides ab: ein classic-Servo braucht keins davon |
| `hitec close <port>` | CLOSE |
| `hitec abort <port>` | ABORT |
| `hitec clear <port>` | CLEAR, nach einem Stopp |
| `hitec send <port> [lead=<us>] [gap=<bits>] [window=<us>] [rx=<n>] [dec=<dec>] <byte>...` | SEND von 1 bis 8 Bytes |
| `hitec listen <port> window=<us> rx=<n> [answer=<byte>] [dec=<dec>]` | LISTEN |
| `hitec level <port> 0\|1 <us>` | LEVEL |
| `hitec power <port> mv=<mV> ma=<mA> off=<ms> window=<us> rx=<n> [answer=<byte>] [dec=<dec>]` | POWER: die Versorgung aus, aus gehalten, an mit 4800 bis 6000mV, und das LISTEN ab dem Einschalten |
| `hitec c <port> <cmd> <p1> <p2>` | ein classic-Kommando mit seinem Summenbyte, 1 Ruhebit zwischen den Bytes, einem Fenster von 1500us für 2 Bytes und dem classic-Dekoder, wenn das Modul das Kommando kennt |
| `hitec d <port> read <id> <addr>` | ein Lesen der D-Serie: 88us Vorlauf verlangt, 117,2us low getrieben, 5 Bytes, ein Fenster von 25.000us für 8 Bytes und der Dekoder der D-Serie |
| `hitec d <port> write <id> <addr> <value>` | ein Schreiben der D-Serie: 88us Vorlauf verlangt, 89,0us low getrieben, 7 Bytes, nichts empfangen |

`<dec>` ist `none`, `c:<cmd>`, `d:<addr>` oder `l:<byte>`.

`more` als letztes Wort von `send`, `listen`, `level`, `power`, `c` oder `d`
setzt Bit 4 des Kommandos (MORE, Protokoll 4.31): weitere Schritte dieses
Schreibens folgen. Es gehört auf jeden Schritt eines Schreibens in den
Speicher des Servos außer dem letzten, zum Beispiel ein classic-Schreiben
einer Zelle vor ihrer Ausgleichszelle oder ein Schreiben der D-Serie und
sein Speichern vor dem Neustart. Solange es steht, `flags=MORE`, wartet ein
OFF, das das Panel an SUPPLY schreibt, auf den letzten Schritt des
Schreibens, höchstens 10.000ms nach dem OFF
([Ein OFF während eines Schreibens](LinkHitec-de.md#ein-off-während-eines-schreibens)).
Ein Schritt, der 10.000ms oder mehr nach dem OFF eingegeben wird, erreicht
ein Servo ohne Versorgung. OFF wird auch während eines SEND mit
schreibendem Frame und für 150ms danach zurückgehalten, selbst ohne
`more`. Während eines Lesens oder eines LISTEN wird OFF nicht
zurückgehalten.

```text
hitec c 0 0x62 0x10 0x55 more
hitec c 0 0x62 0x1C 0xAA
```

Nicht angegebene Optionen von `send`, `listen` und `power` haben den Wert
0: keine Lücke, kein Fenster, kein empfangenes Byte und kein Dekoder. Die
Ausnahme ist `answer`: Ohne diese Option wird keine Antwort gesendet;
`answer=0` sendet 0x00. Ein
fehlendes `lead=` lässt die Auffüllung der PIO-Wörter vorn stehen: ein SEND
der D-Serie mit 5 Bytes wird mit und ohne `lead=88` 117,2us low getrieben.
`send` fügt kein Summenbyte hinzu. Die rohe Form von
`hitec d 0 read 0 0x32`:

```text
> hitec send 0 lead=88 window=25000 rx=8 dec=d:0x32 0x96 0x00 0x32 0x00 0x32
```

Das letzte Byte ist das untere Byte der Summe aller Bytes nach dem Header
0x96: 0x00 + 0x32 + 0x00 = 0x32. Derselbe Frame mit dem Summenbyte 0xC8,
das den Header mitzählt, bekam von einem D485HW keine Antwort.

Eine Leitung der D-Serie braucht einen Pull-up auf der Platine. Getestet mit
einem D485HW: 2,2kΩ von der Signalleitung zur Servoversorgung, und jedes
Lesen wird beantwortet; mit dem Pull-up des Pads allein beantwortet der
Servo keines (`rx=SILENT`).
[Der Programmierport für Servos von Hitec](LinkHitec-de.md#hinein-und-hinaus)
nennt die noch ungeklärten Punkte.

`hitec c` braucht eine offene classic-Leitung und `hitec d` eine offene
Leitung der D-Serie. Auf einer Leitung der anderen Familie schreibt die
Konsole nichts und sagt es:

```text
err the line is classic; this command needs a D series line
```

Eine manuell gesteuerte classic-Session auf Port 0:

```text
> hitec open 0 c
hitec OPEN port=0 started
hitec done state=OPEN port=0 ...
> hitec c 0 0x67 0 0
> hitec c 0 0x61 0x12 0
> hitec power 0 mv=5000 ma=1000 off=1000 window=2000000 rx=1 dec=l:0x2B
> hitec close 0
```

Ein Lesevorgang der D-Serie an Port 0 ohne Pull-up an der Signalleitung:

```text
> hitec open 0 d pp pre
hitec OPEN port=0 started
hitec done state=OPEN port=0 flags=RAIL,PRECHARGE ...
hitec line pin=13 line=0x11 rx=pre ...
> hitec d 0 read 0 0x32
```

`pre` und `pu` schreiben dasselbe Register, das das Panel aus SETUP >
Hitec-D-Empfang (Liste WEITERES des Hitec-Tabs) schreibt. Das Panel schreibt es wieder bei
seiner nächsten Änderung dieser Zeile und bei jedem Link-Aufbau.

Ein direktes `w` auf die HITEC-Page erzeugt keinen Bericht und fügt keinen
Schlüssel hinzu. Das [Beispiel](LinkHitec-de.md#eine-transaktion-von-hand)
zeigt eine solche Transaktion.

## Der Pin-Treiber der Signalleitung

`ewline <gpio> tx|abort|rx` lässt den Pin-Treiber der Signalleitung eines ESC
(Electronic Speed Controller) (`protocols/escwire/rp2350/ew_line.h`) auf einem
freien Pin laufen, für einen Logic Analyser ohne angeschlossenen ESC. Das ist
keine Page und hat keinen Schlüssel: es sendet feste Bytes, keines davon ein
Throttle-Signal, ein Erase oder ein Program. Es weist einen Pin ab, den Outputs,
Ports, Supply, Sensorbus oder Tone-Capture halten, einen Pin, der treibt, und
jeden Pin, solange die Bank ARMED ist. Ein Programming Port, der den Pin nennt,
hält ihn; dessen PIN geht darum vorher auf 0xFFFF: `w 0x33 4 0xFFFF` für Port 0,
`w 0x33 4 13` gibt GPIO 13 zurück. Während eines Tests ist der Pin
reserviert: die Pages weisen ihn ab. Ein ARM während eines Tests schließt die
Leitung sofort und antwortet `ewline armed`.

| Test | Auf dem Pin, der Reihe nach |
|---|---|
| `tx` | geparkt 20ms; high 50ms; der First Contact (17B bei 52us je Bit) 3-mal, jeder gefolgt von seinen 100ms Wartezeit; Pull-up 20ms; 12B mit 10 Idle-Bitzeiten, 520us, nach dem sechsten; Pull-up 20ms; `info` und LF bei 38400baud und 200ms Wartezeit; Pull-up 20ms; 0x55 bei 52us je Bit in einen Pull-down; Pull-up 20ms; 0x55 bei 38400baud in einen Pull-down; geparkt 20ms; low 300ms; geparkt 20ms |
| `abort` | high 5ms; der First Contact, 3ms nach seinem Start abgeschnitten; geparkt 20ms; high 5ms; 0x55 in einen Pull-down; geparkt 20ms |
| `rx` | high 5ms; 3-mal 0x55 und eine Antwort von 9B, über die Overrides des Pins auf das Pad getrieben: bei 52us je Bit, mit einem Low-Stoppbit am dritten Byte und 2 Idle-Bitzeiten danach, bei 38400baud; jeweils gefolgt von 5ms |

0x55 in einen Pull-down zeigt das Loslassen: die steigende Flanke des Stoppbits,
dann fällt das Pad nach 1 Bitzeit, wenn es loslässt. Der `rx`-Test
blockiert die Hauptschleife für seine 9B, 4,7ms bei 52us je Bit.

```
ewline open gpio=13 bit_a=52000ns bit_text=26042ns
ewline capture n=9 bad_stop=0 aborted=0 overflow=0 match=1
ewline done gpio=13 start_max=216us poll_max=96us level_max=30us abort=53us
```

Die letzte Zeile nennt den längsten Aufruf jeder Art in Mikrosekunden, hier den
längsten auf der Station bei Commit aeea917d
([was zu sehen war](../protocols/escwire/README.md#on-a-chip-no-esc-attached)). Eine
Last auf der Leitung, etwa die Signalleitung eines Servos, liest am Pull-up allein
low, und vor dem ersten Startbit jedes Frames zeigt sich ein High-Puls von 6,5us,
3,26us bei 38400baud. Ein Test auf so einer Leitung kann mit `n=`-Werten enden,
die nicht passen.

## Die ESC-Einstellungs-Page

`esc` greift mit denselben Frames wie das Panel auf
[die Page ESC_WIRE](LinkEsc-de.md) zu. Es gelten dieselben Prüfungen und
Ablehnungen. Wie andere Pages, die einen Heartbeat benötigen, braucht
sie neben der Konsole ein laufendes Panel. Keiner dieser Befehle schreibt
in einen ESC. Die Funktion ist nicht in veröffentlichten Images enthalten.

```
esc
esc open slot <n>
esc open port <n>
esc enter mv=<mV> ma=<mA> [hold=<ms>] [off=<ms>]
esc enter hand [hold=<ms>]
esc done|read|leave|stop|abort|clear|close
```

SWITCHED braucht `mv` und `ma`, ohne Vorgabewert; `hold` und `off` sind ohne Angabe 3000ms. Jeder Befehl
antwortet mit einer Zeile:

```
esc state=2 op=1 result=2 ref=0 flags=0x0008 line=0 gpio=5 ses=0 fw=0 layout=0 why=0 t=52ms
```

`state`, `result`, `ref`, `flags` und `line` sind die Register
[ihrer Page](LinkEsc-de.md#register); `ses` ist das letzte Ergebnis der Session.

## Über den Link, ohne USB am IOmcu

Diese Funktion ist nur in Debug-Builds enthalten. Sie fehlt in den
veröffentlichten Images beider Seiten.

Ein mit `RCBENCH_PANEL_CONSOLE` gebautes Panel leitet seine seriellen
Ports UART0 und USB-Serial-JTAG an einen mit `IOMCU_CONSOLE` gebauten
IOmcu weiter. Dafür nutzt es die CONSOLE-Page (0xE0) des Links. Befehle
werden dort mit demselben Verhalten und denselben Ablehnungen wie über
USB ausgeführt. Das gilt auch für `ewline` und andere Befehle, die
Leitungen treiben. Die USB-Verbindung des IOmcu bleibt nutzbar.

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware/iomcu -B firmware/iomcu/build-console -DIOMCU_CONSOLE=ON
cmake --build firmware/iomcu/build-console
cp firmware/iomcu/build-console/rcbench-iomcu.bin firmware/panel/iomcu-console.bin
idf.py -C firmware/panel -B firmware/panel/build-console \
    -D RCBENCH_PANEL_CONSOLE=ON -D RCBENCH_IOMCU_BIN=iomcu-console.bin build
```

Das Panel trägt dieses Image. Ein IOmcu mit einem anderen Build meldet bei der Build-Prüfung einen Unterschied, also bietet UPDATES das Image an, bei gleicher Versionsnummer: `Gleiche Versionsnummer, aber ein anderer Build.` Nach dem Update findet die Prüfung denselben Build und bietet nichts an. Ein Panel mit einem veröffentlichten Image bietet dieses einem Konsolen-IOmcu ebenso wieder an. Ein rbmod-Carrier braucht ein eigenes Konsolen-Image, gebaut mit `-DIOMCU_BOARD=rbmod`, in `RCBENCH_IOMCU_RBMOD_BIN`. Ein Panel mit der Option und ohne Image bietet nichts an.

| Zeile | Bedeutet |
| --- | --- |
| `console: link up t_ms=<ms>` | der Link kam, in der Zeit des Panels seit seinem Start |
| `console: link down t_ms=<ms>` | der Link ging |
| `console: the IOmcu has no console. Take the update UPDATES offers, or flash a build with IOMCU_CONSOLE=ON` | die Page antwortete `BAD_PAGE`; einmal gesagt, und das Panel schaut alle 5000ms wieder |
| `console: the IOmcu's output restarted at <n>, expected <m>` | die Stelle der Ausgabe sprang: das IOmcu startete neu |
| `console: no pass from t_ms=<a> to t_ms=<b>: the link was not followed` | 1500ms oder länger erreichte kein Control-Durchlauf das Relay, etwa während eines IOmcu-Updates: ein Link-Ausfall oder -Aufbau zwischen beiden Zeiten hat keine Zeile |
| `err no link: line dropped` | eine Zeile bei getrenntem Link |
| `err line lost: the link went down inside it` | der Link ging, während die Zeile unterwegs war |
| `err line lost: the IOmcu restarted inside it` | das IOmcu startete neu, während die Zeile unterwegs war |
| `err no answer: line lost` | ein Stück der Zeile blieb ohne Antwort |
| `err the IOmcu has no console` | eine Zeile, und keine Page dafür |
| `err line longer than 160 characters` | die Zeile wird ganz abgelehnt |
| `err line dropped: 8 lines wait` | eine Zeile, während 8 auf den Link warten |
| `err panel buffer full, <n> bytes dropped` | der eigene Puffer des Panels von 4.096 Byte hatte keinen Platz, etwa wenn ein serieller Host das Lesen einstellt und das IOmcu neu startet |

Die eigenen Zeilen des Relays landen nie in einer IOmcu-Zeile. Ein Fenster endet an beliebiger Stelle, also behält das Panel eine Zeile des IOmcu bis zu ihrem LF, bis zu 256 Byte, und die Zeile des Relays geht zuerst. Eine längere Zeile geht geschnitten weiter, und eine Relay-Zeile nach diesem Schnitt beginnt eine neue Zeile. Eine Zeile, die beim Neustart des IOmcu nicht beendet ist, geht mit einem eigenen LF hinaus, vor der Zeile `restarted`.

Das Panel erkennt die Page durch einen Lesezugriff. Es gibt dafür weder
ein Capability-Bit noch eine eigene Minor-Version des Protokolls. Ein
IOmcu ohne diese Page antwortet mit `BAD_PAGE`, wie bei jeder nicht
unterstützten Page.

### Bluetooth LE: `ble scan`

Eine Zeile, die mit dem Wort `ble` beginnt, bleibt im Panel und geht nicht
zum IOmcu. Dafür hält das Panel jede getippte Zeile bis zu ihrem Ende
zurück. `ble scan` startet Bluetooth LE, wenn es noch nicht läuft ([Bauen,
Bluetooth LE über den ESP32-C6](Building-de.md#bluetooth-le-über-den-esp32-c6)),
wartet auf den Sync des Hosts und macht einen aktiven Scan von 10 000ms mit
dem Duplikatfilter des Controllers: ein Gerät, das alle 256ms sendet,
erscheint einmal im Scan. Eine andere Zeile mit `ble` nennt die
Befehle.

| Zeile | Bedeutung |
| --- | --- |
| `ble: starting Bluetooth LE; the scan follows the sync` | das erste `ble scan` seit dem Start des Panels |
| `ble: scan for 10000ms` | der Scan hat begonnen |
| `ble: adv <address> type <n> rssi <n>dBm evtype <n> data <hex>` | ein Gerät: seine Adresse mit dem höchstwertigen Byte zuerst, sein Adresstyp (0 public, 1 random), die Signalstärke, der Event-Typ des Berichts (0 verbindbares Advertising, 4 Scan Response) und die Advertising-Daten hexadezimal |
| `ble: scan done: <n> reports, reason <n>, <n> events dropped since start-up` | der Scan ist zu Ende; Grund 0 ist sein Ende nach 10 000ms |
| `ble: no scan: no link to the C6` | die Verbindung zum C6 ist ausgefallen |
| `ble: no scan: no sync` | der Host hatte binnen 3 000ms keinen Sync; er versucht es weiter, und ein späteres `ble scan` scannt, sobald er synchron ist |
| `ble: no scan: <error>` | NimBLE hat den Scan abgelehnt |
| `ble: a scan is under way` | ein `ble scan`, während einer wartet oder läuft |
| `ble: commands: ble scan` | jede andere Zeile, die mit `ble` beginnt |

### Raten

Das Relay führt nach dem Polling des Panels höchstens einen Austausch
je Control-Durchlauf aus. Ohne Verzögerung durch andere Arbeit dauert
ein Durchlauf 5ms. Während eines IOmcu-Updates bleibt der Link für das
Update reserviert; das Relay führt dann keinen Austausch aus. Die
Tabelle ist aus den Frames für einen Durchlauf von genau 5ms berechnet.

| Verkehr | Frames | Rate |
| --- | --- | --- |
| Ein Blick in Ruhe | 2 alle 50ms | 40 Frames/s |
| Ein Fenster Ausgabe, 28 Byte in 14 Registern | 5, eine Anfrage und 4 Antworten mit 4 Registern | ein Durchlauf |
| Seine Bestätigung | 2 | ein Durchlauf |
| Ausgabe, dauerhaft | 7 je 28 Byte, 2 Durchläufe | 2.800 Byte/s, 700 Frames/s |
| Eingabe, 6 Byte je Stück | 2 je Stück, ein Durchlauf | 1.200 Byte/s, 135ms für eine Zeile mit 160 Zeichen |

Ein Fenster ist ein Lesen von 16 Registern, das Meiste, was ein Lesen abfragt: 4 Antwortframes passen in den TWAI-Empfangs-FIFO des Panels ([Der Link](Link-de.md)). Die Page bleibt 32 Register breit, und die Register hinter dem Fenster lesen sich als 0; ein Panel, das für Fenster von 60 Byte gebaut ist, liest sie also weiter. Dieses Panel nimmt von einem IOmcu-Konsolen-Build vor Protokoll 4.35 ein Fenster von bis zu 60 Byte und liest es als 32 Register in einem Austausch.

Gemessen an der Station mit einem Fenster von 60 Byte, das dieser Build nicht hat: 2.856 bis 3.510 Byte/s Ausgabe, mit `help` bei 6/s bis 30/s, 17ms bis 21ms je Fenster gegen 10ms berechnet. Die Wartezeit der Steuerung von 5ms kommt nach der Arbeit des Durchlaufs (`vTaskDelay`), ein Durchlauf ist also 5ms plus seine eigenen Austausche: die Polls des Panels und die Frames des Fensters. Der Anteil jedes Teils ist nicht gemessen. Bei denselben 17ms bis 21ms je Fenster ergibt ein Fenster von 28 Byte etwa 1.330 bis 1.650 Byte/s; nicht gemessen. Ein Fenster und seine Bestätigung brauchen 2 Durchläufe, und nach dem letzten Fenster einer Folge kommt der nächste Blick bis zu 50ms später.

Bei 1Mbit/s dauert ein Frame mit 8 Byte etwa 150us, dauerhafte Ausgabe bei der berechneten Rate belegt also etwa 11% des Busses. Bei 1.330 bis 1.650 Byte/s leert sich der Puffer von 2.048 Byte in etwa 1,2s bis 1,5s. Ausgabe schneller als das, länger als dieser Puffer reicht, verliert ganze Zeilen, auch die 3s-Zeilen des IOmcu; die nächste Zeile sagt `err output full, N bytes dropped` wie an USB. Ausgabe wird auch zurückgehalten, wenn der eigene Puffer des Panels von 4.096 Byte unter 412 Byte frei hat, etwa wenn ein serieller Host das Lesen einstellt.

Der IOmcu behält ein Fenster bis zur Bestätigung. Bei einer verlorenen
Antwort ist deshalb ein weiterer Lesevorgang nötig, ohne dass Bytes
verloren gehen. Ein unbeantworteter Austausch blockiert den
Control-Durchlauf bis zu 1000ms, wie andere Austausche des Panels. Der
Heartbeat läuft dabei weiter.

### Was sich nicht ändert

Die Befehle erreichen die Pages über `link_dev_apply()`. Eine Anfrage der Konsole ist kein Link-Verkehr: sie hält den Stille-Failsafe nicht auf. Die Frames der CONSOLE-Page sind Anfragen des Panels, wie seine Polls, und enden, wenn das Panel endet. Die eigenen Module des Panels schreiben weiter: der Supply-Link, `hitec_link`, `kst_link`, `esc_link` und das Schreiben von ARM und THROTTLE alle 50ms können überschreiben, was ein `w` der Konsole in dieselben Register schrieb. Ein Schreiben von ARM aus der Konsole erreicht die Page, und das nächste Schreiben des Panels nach 50ms überschreibt es mit dessen eigenem Arming-Zustand.

Beim Start des IOmcu wartet seine Ausgabe im 2.048-Byte-Puffer auf einen
USB-Host oder ein Panel, bis zur Kapazität des Puffers. Die erste Zeile,
`rcbench-iomcu: reset ...`, nennt die Startursache und wiederholt sich
alle 3s; die Zeile danach, `rcbench-iomcu: boot ...`, nennt den Eintrag des
Boot-ROM zu diesem Boot ([Bringup](Bringup-de.md#warum-der-iomcu-zuletzt-gestartet-ist)). Eine Zeile vom Panel wird vollständig verarbeitet, ohne eine
teilweise eingegebene USB-Zeile zu verändern. Sind USB-Host und Panel
angeschlossen, bestimmt der langsamere Leser das Ausgabetempo. Ein
USB-Host, der den Port offen lässt, aber das Lesen einstellt, hält die
Ausgabe ebenfalls auf.

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
- Die eigenen Zeilen des IOmcu, `rcbench-iomcu: ...` alle 3s und nach einem
  Flash-Schreiben, laufen in diesem Build durch dieselbe Ausgabe: sie kommen
  nach der Zeile davor, nie mitten in ihr, und enden nur mit LF.
- `shared/outputs/page_console.c` hält die Regeln, und
  `test/host/test_page_console.c` testet sie gegen ein Modell des Treibers
  der HITEC-Page.
