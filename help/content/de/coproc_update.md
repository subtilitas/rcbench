---
screen: COPROC_UPDATE
title: IOmcu-Update
order: 100
---

# IOmcu-Update

Jedes Firmware-Image des Panels enthält ein IOmcu-Image. Das Panel bietet es an, wenn auf dem verbundenen IOmcu ein anderer Build läuft. Die Prüfung erfolgt bei jedem Verbindungsaufbau über den CAN-Bus (Controller Area Network). Sie vergleicht SHA-256-Prüfsummen (Secure Hash Algorithm, 256 Bit) statt Versionsnummern. Bei einer Abweichung erscheint der Alarm `IOmcu nutzt einen anderen Build. Aktualisiere ihn auf UPDATES.`.

## Die Frage

![Die Frage auf dem Menü](view:coproc-update)

Die Update-Rückfrage öffnet sich im [Menü](overview.md), mit UPDATE im Band. Dazu muss der Prüfstand SAFE sein; Netzteilausgang und WLAN-Funkmodul müssen ausgeschaltet sein, und es darf kein Panel-Update laufen. Die Rückfrage zeigt die laufende Version des IOmcu und die im Panel enthaltene Version.

| Bedienelement | Funktion |
|---|---|
| 2s HALTEN ZUM STARTEN | startet das Update nach 2s Halten, mit derselben Geste und demselben Übergang wie ARM |
| ABBRECHEN | schließt die Rückfrage; bis zum Neustart fragt das Panel für diese Version nicht erneut. Du kannst das Update weiterhin unter [Updates](updates.md#der-iomcu) starten. |

Das Home-Feld lehnt das Update ebenso wie ABBRECHEN ab. Wird die Rückfrage durch eine andere Seite unterbrochen, gilt das nicht als Ablehnung. Das betrifft die [Fehlerseite](busfault.md) bei einer verlorenen Verbindung und den Download eines Panel-Updates. Beim nächsten Verbindungsaufbau wird das Update erneut angeboten. Die Rückfrage öffnet sich im Menü, sobald dieselben Bedingungen erfüllt sind.

![Ein älteres Image](view:coproc-update-older)

Ein älteres Image wird ebenfalls angeboten. Die Frage zeigt dann `Die Firmware auf diesem Panel ist älter.` in der Warnfarbe.

## Der Fortschritt

![Firmware wird gesendet.](view:coproc-update-progress)

Nach vollständigem Halten übernimmt das Update das Panel. Die Verbindung wird unterbrochen, und die Ausgänge sind ausgeschaltet. Der IOmcu lehnt ARM bis zum Ende des Updates ab. Die Fortschrittsanzeige belegt den ganzen Bildschirm, ohne Statusband und ohne STOP.

| Schritt | Vorgang |
|---|---|
| `Firmware wird vorbereitet.` | Das Panel berechnet die Prüfsumme des Images, 8KiB je Schritt. |
| `Firmware wird gesendet.` | Das Panel sendet Blöcke von je 64 Bytes und zeigt den Fortschritt, etwa `Block 1497 von 2994, 50%`. |
| `IOmcu prüft die Firmware.` | Der IOmcu berechnet die Prüfsumme des beschriebenen Slots. |
| `IOmcu startet die neue Firmware.` | Der IOmcu startet den neuen Slot auf Probe. |
| `Neue Firmware wird dauerhaft übernommen.` | Das Panel liest die Identität des neuen Images aus und übernimmt es dauerhaft. |

Die Übertragungsdauer ist nicht gemessen.

## Wie es endete

Nach dem Update kehrt das Panel ins Menü zurück. Wurde das Update unter [Updates](updates.md#der-iomcu) gestartet, kehrt es dorthin zurück. Das Alarmband meldet das Ergebnis:

| Alarm | Bedeutung |
|---|---|
| `IOmcu auf 0.22.0 aktualisiert.` | Das neue Image läuft und ist dauerhaft übernommen. |
| `IOmcu-Update abgebrochen: keine Antwort. Vorherige Firmware läuft.` | Seit 4s kam keine Antwort. |
| `IOmcu-Update abgebrochen: abgelehnt, Fehler 6. Vorherige Firmware läuft.` | Der IOmcu hat einen Schritt abgelehnt. |
| `IOmcu-Update abgebrochen: Firmware-Prüfung fehlgeschlagen. Vorherige Firmware läuft.` | Die SHA-256-Prüfsumme des Slots stimmt nicht mit der des Images überein. |
| `IOmcu-Update abgebrochen: Flash-Speicher blieb belegt. Vorherige Firmware läuft.` | Der Flash-Speicher blieb 10s belegt. |
| `IOmcu-Update abgebrochen: IOmcu 3-mal neu gestartet. Vorherige Firmware läuft.` | Die Übertragung begann dreimal bei Block 0. |
| `IOmcu-Update abgebrochen: neue Firmware ist nicht gestartet. Vorherige Firmware läuft.` | Das neue Image lieferte innerhalb von 12s keine Identität. |
| `IOmcu kann nicht über CAN aktualisiert werden. Installiere die Firmware einmal über USB.` | Dem IOmcu fehlen die Update-Pages oder die Partitionstabelle. Flashe ihn einmal über USB (Universal Serial Bus). |
| `Neue IOmcu-Firmware dauerhaft übernommen.` | Ein beim Verbindungsaufbau auf Probe laufendes Image wurde dauerhaft übernommen. |

Bei einem Stromausfall während des Updates bleibt das alte IOmcu-Image erhalten. Beim nächsten Verbindungsaufbau wird das Update erneut angeboten.

## Verwandte Seiten

- [Panel-Update](update.md): ein Panel-Image über WLAN installieren.
- [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/blob/main/docs/Link-de.md): die Übertragung, die Flash-Aufteilung und die Fehlernummern.
