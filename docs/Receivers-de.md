# Empfängerbusse

<sub>[English](Receivers.md) · **Deutsch**</sub>

Was aus einem RC-Empfänger herauskommt, decodiert. Ein Protokoll nach dem
anderen — jedes ist etwa ein Tagewerk, und jedes fügt etwas hinzu, das der
Prüfstand danach kann.

Gebaut: **S.BUS**.

## S.BUS

Futabas, und das verbreitetste. Sechzehn proportionale Kanäle plus zwei
digitale in fünfundzwanzig Bytes, auf einem **invertierten 8E2-UART mit
100 kBaud** — die Invertierung und die zwei Stoppbits sind der Grund, warum das
eine PIO State Machine will und keinen Hardware-UART.

| | |
| --- | --- |
| Byte 0 | Header, `0x0F` |
| Bytes 1–22 | sechzehn Kanäle zu elf Bit, lückenlos gepackt |
| Byte 23 | Flags |
| Byte 24 | Footer |

### Es hat keine Prüfsumme, und das prägt alles

Es gibt keine CRC, kein Escaping, und `0x0F` — der Header — ist ein ganz
gewöhnlicher Wert innerhalb der Kanaldaten. Der Header allein kann also nicht
sagen, wo ein Frame beginnt, und ein Decoder, der ihm traut, rastet mitten in
einem ein und meldet sechzehn plausibel aussehende Kanäle, die alle falsch
sind. Nichts weiter unten kann das erkennen: die Zahlen liegen im Bereich, sie
bewegen sich, wenn die Knüppel sich bewegen, und es sind die falschen Kanäle.

**Was einen Frame tatsächlich abgrenzt, ist die Lücke.** Frames kommen alle 7 ms
oder 14 ms und brauchen 3 ms zum Senden, also liegen mindestens 4 ms Stille
zwischen ihnen — gegen 120 µs zwischen zwei Bytes innerhalb eines Frames. Der
Decoder bekommt einen Zeitstempel und nutzt diese Lücke; Header und Footer
bestätigen danach, statt die ganze Arbeit allein zu tragen.

`SBUS_GAP_US` ist 2 ms, und das ist hier die einzige interessante Zahl — sie muss
über jeder plausiblen Bytezeit und unter der kleinsten Framelücke liegen, und
ein Test nagelt sie zwischen die beiden. Setzt man sie unter eine Bytezeit,
beginnt der Frame bei jedem Byte neu, was dasselbe ist wie gar kein Framing, und
zehn von elf Fällen fallen um, wenn man es versucht.

### Das Kanal-Packing ist die Stelle, an der ein Decoder leise falsch wird

Sechzehn Werte zu elf Bit, little-endian lückenlos über zweiundzwanzig Bytes
gepackt und an nichts ausgerichtet — Kanal *n* beginnt also bei Bit 11*n* und
landet bei allen außer dem ersten mitten im Byte. Die meisten Umsetzungen tragen
dafür sechzehn von Hand ausgeschriebene Ausdrücke, also sechzehn Gelegenheiten,
einen Shift zu vertauschen — und jeder Fehler erzeugt einen Kanal, der völlig
vernünftig aussieht und die falsche Fläche bewegt.

Hier ist es Arithmetik, und der Test läuft **jedes Bit jedes Kanals** ab und
prüft, dass es in diesem Kanal landet und in keinem anderen. Das sind 176
Aussagen über Bitpositionen statt einer Handvoll Beispielframes — denn
Beispielframes stimmen mit einem Decoder überein, der einen systematischen
Fehler hat.

### Die Flags zählen mehr als die Kanäle

| Bit | |
| --- | --- |
| `0x01` | Kanal 17 (digital) |
| `0x02` | Kanal 18 (digital) |
| `0x04` | Frame lost — *dieser* Frame kam nicht heil an |
| `0x08` | **Failsafe** — der Empfänger hat den Sender verloren |

Ein Empfänger im Failsafe sendet sechzehn einwandfrei geformte Kanäle, die
nichts bedeuten. Alles an diesem Prüfstand, das aus S.BUS ein Servo oder einen
Regler ansteuert, muss dieses Bit als *Stopp* behandeln und nicht als sechzehn
gültige Zahlen — wonach sie genau aussehen.

### Footer, und Großzügigkeit, wo sie nichts kostet

Die Spezifikation sagt, der Footer sei `0x00`. Empfänger im Feld senden auch
`0x04`, `0x14`, `0x24` und `0x34`, bei manchen trägt das untere Nibble einen
Framezähler. Die abzulehnen hieße Hardware abzulehnen, die funktioniert, also
prüft der Test die Bits, die immer null sind. Ein Footer, der keines von beiden
ist, wird getrennt von einem Resync gezählt: ein Frame voller Länge mit falschem
Ende ist ein Empfänger, der eine Variante spricht, oder ein Decoder, der ein
Byte daneben liegt — und beides ist kein Leitungsrauschen.

## Was nicht gebaut ist

Die Bytes. Dem Decoder werden sie gereicht, und ihn interessiert nicht, woher;
der invertierte 8E2-Empfänger ist ein PIO-Programm auf dem Koprozessor, und das
ist nicht geschrieben.

## Als Nächstes

iBUS, SUMD, CRSF, SRXL2 und JETI EX Bus, in der Reihenfolge, in der die
Hardware zum Gegentesten auftaucht. Zwei Regeln übertragen sich von S.BUS und
sind es wert, einmal gesagt zu werden: **framen auf etwas Besserem als einem
magischen Byte**, wo das Protokoll eines hergibt, und **das Failsafe-Flag vor
den Kanälen decodieren** — denn ein Protokoll, das überzeugend lügen kann, wird
es tun.

Die Lizenzbedingung aus [dem Protokoll](../README.md) gilt für jedes einzelne:
aus der Spezifikation geschrieben, nicht aus fremdem Code.
