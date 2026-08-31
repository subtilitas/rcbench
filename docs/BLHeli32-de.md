# Warum rcbench keine BLHeli_32-Parameter macht

<sub>[English](BLHeli32.md) · **Deutsch**</sub>

BLHeli_32 legt seine Einstellungen als 256-Byte-Block an einer festen
Flash-Adresse ab, und dieser Block ist XTEA-Chiffrat — 64-Bit-Block,
128-Bit-Key, mit drei Klartext-Bytes in je vier gespeicherten Bytes.

Alles darum herum ist gewöhnliches Protokoll und vollständig nach der
Spezifikation umgesetzt: der Bootloader auf der Signalleitung, das Framing des
4-Way Interface, MSP Passthrough, beide CRCs, die MCU-Signaturtabelle. Wir
können uns mit einem BLHeli_32-Regler verbinden, ihn identifizieren und seine
256 Bytes lesen.

**Wir können nicht sagen, was auch nur eines davon bedeutet.**

## Der Key

Der Key existiert an genau einer Stelle: in BLHeliSuite32, einer
Closed-Source-Binary ohne Lizenz, aus einem Projekt, dessen Autor nicht mehr
veröffentlicht und dessen Update-Server verschwunden sind. Es gibt keine Lizenz
zu erwerben, keinen Hersteller zu fragen und keine zweite Quelle.

Ihn aus der Binary zu ziehen und mitzuliefern kann keine technische
Entscheidung legal machen: es wäre das Umgehen einer Schutzmaßnahme an einem
kommerziellen Produkt — in einem Repository, das unter MIT steht und der
festen Regel folgt, dass jedes Protokoll hier nach einer Spezifikation
geschrieben wird, nicht nach fremdem Code.

Am Aufwand scheitert es also nicht, und daran hat es nie gelegen. Mit der
fertigen BLHeli_S-Umsetzung kostet der Weg zum BLHeli_32-Bootloader eine
CRC-Routine und eine Kommandotabelle. Was fehlt, ist ein Schlüssel, den man
sich nehmen müsste, statt ihn zu bekommen.

## Was wir stattdessen tun

Die Identifikation braucht von alledem nichts — der Prüfstand nennt MCU und
Bootloader-Revision des Reglers anhand der vier Bytes, die
`cmd_DeviceInitFlash` zurückgibt.

Drehrichtung, Drehrichtungsumkehr, 3D-Modus, Beacon und Save-Settings sind
DShot Special Commands auf der Signalleitung — offen, standardisiert, und sie
decken das meiste ab, was man an einem Prüfstand ändert. Die Telemetrie ist
ohnehin nicht betroffen.

Ein BLHeli_32-Regler ist an diesem Prüfstand also ein erkannter,
ansteuerbarer, messbarer Regler — mit einer kurzen Parameterliste und einem
ehrlichen Grund dafür.

## Wir haben gefragt, und die Antwort war nein

Im August 2026 wurde der Rechteinhaber direkt gefragt: ob er diesem Projekt
erlauben würde, das Nötige zum Lesen und Schreiben dieser Einstellungen
aufzunehmen — den Schlüssel, oder gleichwertig eine Beschreibung des
Blockaufbaus, zu Bedingungen seiner Wahl. **Er hat abgelehnt.**

Das ist eine klare Antwort, und damit ist die Frage entschieden. Sie steht hier,
damit die Tür nicht jedes Mal neu aufgemacht wird, wenn jemandem auffällt, wie
nah der Rest des Protokolls schon ist: der Grund, warum das fehlt, ist nicht,
dass niemand gefragt hätte.

## Was das ändern würde

Nur noch eines, und das liegt nicht bei uns:

- **Ein Sinneswandel des Rechteinhabers**, und den kann nur er selbst haben.

Was dagegen in deiner Hand liegt:

- **Ein auf AM32 geflashter Regler.** AM32 ist offen, dieser Prüfstand
  programmiert ihn vollständig, und damit erledigt sich das ganze Problem.
