# Warum rcbench keine BLHeli_32-Parameter macht

<sub>[English](BLHeli32.md) · **Deutsch**</sub>

BLHeli_32 legt seine Einstellungen als 256-Byte-Block an einer festen
Flash-Adresse ab, und dieser Block ist XTEA-Chiffrat — 64-Bit-Block,
128-Bit-Key, mit drei Klartext-Bytes in je vier gespeicherten Bytes.

Alles drumherum ist gewöhnliches Protokoll und vollständig aus der
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

Ihn aus der Binary herauszulösen und mitzuliefern ist nichts, was eine
technische Entscheidung legal machen könnte — es ist das Umgehen einer
Schutzmaßnahme an einem kommerziellen Produkt, und das in einem Repository, das
wir unter MIT ausliefern, mit der stehenden Regel, dass jedes Protokoll hier aus
einer Spezifikation geschrieben wird und nicht aus fremdem Code.

Der Blocker ist also nicht der Aufwand und war es nie. Mit der
BLHeli_S-Umsetzung in der Hand kostet der Weg zu einem BLHeli_32-Bootloader eine
CRC-Routine und eine Command-Tabelle. Was fehlt, ist ein Key, den wir uns nehmen
müssten, statt ihn zu bekommen.

## Was wir stattdessen tun

Die Identifikation funktioniert ohne all das — der Prüfstand benennt MCU und
Bootloader-Revision des Reglers aus den vier Bytes, die `cmd_DeviceInitFlash`
zurückgibt.

Drehrichtung, Drehrichtungsumkehr, 3D-Modus, Beacon und Save-Settings sind DShot
Special Commands auf der Signalleitung, offen und standardisiert, und sie decken
das meiste ab, was jemand am Prüfstand ändert. Die Telemetrie ist nicht
betroffen.

Ein BLHeli_32-Regler ist an diesem Prüfstand ein erkannter, ansteuerbarer,
messbarer Regler mit einer kurzen Parameterliste und einem ehrlichen Grund
dafür.

## Was das ändern würde

- Ein veröffentlichter Key oder eine Spezifikation vom Autor.
- Eine Lizenz von dem, der die Rechte inzwischen hält.
- Ein Regler, der auf **AM32** geflasht ist — offen, und von diesem Prüfstand
  vollständig programmierbar.
