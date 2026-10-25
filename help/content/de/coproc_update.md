---
screen: COPROC_UPDATE
title: IOmcu-Update
order: 100
---

# IOmcu-Update

Ein Firmware-Image des Panels trägt das Image des IOmcu und bietet es an, wenn der IOmcu einen anderen Build fährt. Die Prüfung läuft bei jedem Link-Aufbau über den CAN-Bus (Controller Area Network) und vergleicht SHA-256-Prüfsummen (Secure Hash Algorithm, 256 Bit), keine Versionsnummern. Ein anderer Build löst den Alert `IOmcu nutzt einen anderen Build. Update unter SETUP > UPDATES.` aus.

## Die Frage

![Die Frage auf dem Menü](view:coproc-update)

Die Frage öffnet aus dem [Menü](overview.md), mit UPDATE im Band, solange der Prüfstand SAFE ist, der Netzteilausgang aus ist, das WLAN-Funkmodul aus ist und kein Panel-Update läuft. Sie nennt die Version, die der IOmcu fährt, und die Version, die das Panel mitbringt.

| Bedienelement | Was es tut |
|---|---|
| 2s HALTEN ZUM STARTEN | startet das Update nach einem Halten von 2s, mit der Geste und dem Übergang von ARM |
| ABBRECHEN | schließt die Frage; das Panel fragt für diese Version bis zu seinem Neustart nicht erneut, und [Updates](updates.md#der-iomcu) startet das Update weiterhin |

Ein anderes Verlassen der Frage lehnt sie ab wie ABBRECHEN.

![Ein älteres Image](view:coproc-update-older)

Ein älteres Image wird ebenfalls angeboten. Die Frage zeigt dann `Die Firmware auf diesem Panel ist älter.` in der Warnfarbe.

## Der Fortschritt

![Firmware wird gesendet.](view:coproc-update-progress)

Ist das Halten vollständig, gehört das Panel dem Update. Der Link geht herunter, die Ausgänge sind aus, und der IOmcu lehnt ARM bis zum Ende des Updates ab. Der Fortschritt füllt den Bildschirm, ohne Band und ohne STOP.

| Schritt | Steht da, solange |
|---|---|
| `Firmware wird vorbereitet.` | das Panel die Prüfsumme des Images bildet, 8KiB je Schritt |
| `Firmware wird gesendet.` | die Blöcke hinausgehen, je 64 Bytes, gezählt als `Block 1497 von 2994, 50%` |
| `IOmcu prüft die Firmware.` | der IOmcu die Prüfsumme des geschriebenen Slots bildet |
| `IOmcu startet die neue Firmware.` | er in den neuen Slot neu startet, auf Probe |
| `Neue Firmware wird dauerhaft übernommen.` | das Panel die Identität des neuen Images liest und es behält |

Die Übertragungsdauer ist nicht gemessen.

## Wie es endete

Der Bildschirm kehrt zum Menü zurück, wenn das Update endet, oder zu [Updates](updates.md#der-iomcu), wenn dessen Halten das Update gestartet hat, und das Alert-Band sagt, wie:

| Alert | Bedeutung |
|---|---|
| `IOmcu auf 0.22.0 aktualisiert.` | das neue Image läuft und wird behalten |
| `IOmcu-Update abgebrochen: keine Antwort. Vorherige Firmware läuft.` | 4s antwortete nichts |
| `IOmcu-Update abgebrochen: abgelehnt, Fehler 6. Vorherige Firmware läuft.` | der IOmcu lehnte einen Schritt ab |
| `IOmcu-Update abgebrochen: Firmware-Prüfung fehlgeschlagen. Vorherige Firmware läuft.` | die SHA-256 des Slots ist nicht die des Images |
| `IOmcu-Update abgebrochen: Flash-Speicher blieb belegt. Vorherige Firmware läuft.` | der Flash blieb 10s belegt |
| `IOmcu-Update abgebrochen: IOmcu 3-mal neu gestartet. Vorherige Firmware läuft.` | drei Starts ab Block 0 |
| `IOmcu-Update abgebrochen: neue Firmware ist nicht gestartet. Vorherige Firmware läuft.` | binnen 12s keine Identität vom neuen Image |
| `IOmcu kann nicht über CAN aktualisiert werden. Firmware einmal über USB installieren.` | der IOmcu hat keine Update-Seiten oder keine Partitionstabelle; ihn einmal über USB (Universal Serial Bus) flashen |
| `Neue IOmcu-Firmware dauerhaft übernommen.` | ein Image, das beim Link-Aufbau auf Probe lief, wurde behalten |

Ein Stromausfall während des Updates lässt den IOmcu auf seinem alten Image, und der nächste Link-Aufbau bietet das Update erneut an.

## Verwandte Seiten

- [Panel-Update](update.md): ein Panel-Image über WLAN installieren.
- [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/blob/main/docs/Link-de.md): die Übertragung, die Flash-Aufteilung und die Fehlernummern.
