---
screen: COPROC_UPDATE
title: Koprozessor-Update
order: 100
---

# Koprozessor-Update

Ein Firmware-Image des Panels trägt das Image des Koprozessors und bietet es an, wenn der Koprozessor einen anderen Build fährt. Die Prüfung läuft bei jedem Link-Aufbau über den CAN-Bus (Controller Area Network) und vergleicht SHA-256-Prüfsummen (Secure Hash Algorithm, 256 Bit), keine Versionsnummern. Ein anderer Build löst den Alert `Koprozessor hat einen anderen Build -- das Menü bietet sein Update an` aus.

## Die Frage

![Die Frage auf dem Menü](view:coproc-update)

Die Frage öffnet aus dem [Menü](overview.md), mit UPDATE im Band, solange der Prüfstand SAFE ist, der Netzteilausgang aus ist, das WLAN-Funkmodul aus ist und kein Panel-Update läuft. Sie nennt die Version, die der Koprozessor fährt, und die Version, die das Panel mitbringt.

| Bedienelement | Was es tut |
|---|---|
| 2 s HALTEN ZUM STARTEN | startet das Update nach einem Halten von 2 s, mit der Geste und dem Übergang von ARM |
| ABBRECHEN | schließt die Frage; das Panel fragt für diese Version bis zu seinem Neustart nicht erneut |

Ein anderes Verlassen der Frage lehnt sie ab wie ABBRECHEN.

![Ein älteres Image](view:coproc-update-older)

Ein älteres Image wird ebenfalls angeboten. Die Frage zeigt dann `Das Image dieses Panels ist älter.` in der Warnfarbe.

## Der Fortschritt

![Das Image wird gesendet](view:coproc-update-progress)

Ist das Halten vollständig, gehört das Panel dem Update. Der Link geht herunter, die Ausgänge sind aus, und der Koprozessor lehnt ARM bis zum Ende des Updates ab. Der Fortschritt füllt den Bildschirm, ohne Band und ohne STOP.

| Schritt | Steht da, solange |
|---|---|
| `Image wird vorbereitet` | das Panel die Prüfsumme des Images bildet, 8 KiB je Schritt |
| `Image wird gesendet` | die Blöcke hinausgehen, je 64 Bytes, gezählt als `Block 1497 von 2994, 50 %` |
| `Koprozessor prüft das Image` | der Koprozessor die Prüfsumme des geschriebenen Slots bildet |
| `Koprozessor startet das neue Image` | er in den neuen Slot neu startet, auf Probe |
| `Neues Image wird behalten` | das Panel die Identität des neuen Images liest und es behält |

Die Übertragungsdauer ist nicht gemessen.

## Wie es endete

Der Bildschirm kehrt zum Menü zurück, wenn das Update endet, und das Alert-Band sagt, wie:

| Alert | Bedeutung |
|---|---|
| `Koprozessor auf 0.18.0 aktualisiert` | das neue Image läuft und wird behalten |
| `Koprozessor-Update abgebrochen: keine Antwort -- das alte Image läuft` | 4 s antwortete nichts |
| `Koprozessor-Update abgebrochen: abgelehnt, Fehler 6 -- das alte Image läuft` | der Koprozessor lehnte einen Schritt ab |
| `Koprozessor-Update abgebrochen: Image-Prüfung fehlgeschlagen -- das alte Image läuft` | die SHA-256 des Slots ist nicht die des Images |
| `Koprozessor-Update abgebrochen: Flash blieb belegt -- das alte Image läuft` | der Flash blieb 10 s belegt |
| `Koprozessor-Update abgebrochen: Koprozessor 3-mal neu gestartet -- das alte Image läuft` | drei Starts ab Block 0 |
| `Koprozessor-Update abgebrochen: neues Image startete nicht -- das alte Image läuft` | binnen 12 s keine Identität vom neuen Image |
| `Koprozessor nimmt kein Update über CAN -- einmal über USB laden` | der Koprozessor hat keine Update-Seiten oder keine Partitionstabelle; ihn einmal über USB (Universal Serial Bus) flashen |
| `Neues Image des Koprozessors behalten` | ein Image, das beim Link-Aufbau auf Probe lief, wurde behalten |

Ein Stromausfall während des Updates lässt den Koprozessor auf seinem alten Image, und der nächste Link-Aufbau bietet das Update erneut an.

## Verwandte Seiten

- [Panel-Update](update.md): ein Panel-Image über WLAN installieren.
- [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/blob/main/docs/Link-de.md): die Übertragung, die Flash-Aufteilung und die Fehlernummern.
