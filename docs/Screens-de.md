# Bildschirme

<sub>[English](Screens.md) · **Deutsch**</sub>

Der Prüfstand sind acht Kacheln hinter einem Menü, dazu ein Splash, der sagt,
was hochgekommen ist. Diese Seite behandelt die Hülle; jeder Prüfstand bekommt
seine eigene Seite, sobald er gebaut ist.

## Warum ein Statusband, wo der Vorgänger bewusst keines hatte

Der Vorgänger gab jedem Bildschirm seine vollen 800×480 und teilte nichts außer
einem Home-Tag, mit dem Argument, dass ein Logviewer, eine Einstellungsliste und
ein Live-Plot sehr Verschiedenes von ihrer Oberkante wollen. Für den *Körper*
eines Bildschirms gilt das weiterhin, und deshalb ist das Band nur 48 px hoch.

Was den Rest umgeworfen hat: **inzwischen kann mehr als ein Bildschirm etwas
scharf schalten.** STOP muss überall an derselben Stelle sein, und ein Band ist
waagerecht — die billige Richtung auf einem Panel, dessen Frame Rate
bandbreitenbegrenzt ist, siehe [Performance](Performance-de.md).

![Das Funktionsmenü](img/overview.png)

### Das Menü sagt, was der Prüfstand kann

Drei Zustände, nicht zwei. **SOON** ist ein Bildschirm, den es noch nicht gibt.
**MODELLED** ist einer, den es gibt und dessen Hardware nicht bestückt ist — er
öffnet, er funktioniert, und jede Zahl darin ist erfunden. Gar nichts heißt, das
Bauteil ist da und die Messwerte sind echt.

Dieser mittlere Zustand war früher vom dritten nicht zu unterscheiden, das Menü
versprach also still Messungen, die der Prüfstand nicht machen konnte. Er kommt
jetzt aus `LINK_ID_CAPABILITIES`, einer Bitmap, die der Koprozessor aus dem
füllt, was tatsächlich aufgelötet ist — heute nichts.

Das ersetzte eine Seite mit Checkboxen zum Abschalten von Funktionen, und der
Grund lohnt sich aufzuheben. An einem Diagnosegerät ist eine Einstellung, die das
Werkzeug *nicht hinsehen* lässt, gefährlich: einen Empfängerbus abwählen, ein
halbes Jahr später einen anstecken, und der Analyser meldet nichts auf der
Leitung, ohne dass sich herausfinden ließe, warum. Es hätte außerdem „das will
ich nicht" und „das ist nicht bestückt" hinter ein Bedienelement gelegt, und die
beiden dürfen nie verwechselt werden — das Zweite ist eine Tatsache, und es
korrigiert sich selbst, sobald das Bauteil daran ist.

Abwesend heißt nicht verboten. Ein Bildschirm, dessen Capability fehlt, öffnet
weiterhin und funktioniert weiterhin aus dem Modell — mit der Kennzeichnung, die
das sagt.

Von rechts nach links, damit kein Element die Breite eines anderen raten muss:
Linkzustand, Ausgangsmodus, scharf, die Laufzeit, und STOP. **STOP ist global und
immer aktiv** — es entschärft von jedem Bildschirm aus und braucht deshalb nie
eine Erklärung.

Eine Konsequenz ist Absicht: **ARM sitzt unten an einem Prüfstand und STOP oben
im Band**, getrennt durch die volle Höhe des Panels, damit der Griff nach dem
einen nicht das andere findet.

### Das Band ist erzwungen, nicht vereinbart

Der Router besitzt die oberen 48 px und reicht jedem Bildschirm eine
**Sub-Canvas** dessen, was übrig bleibt, über `gfx_canvas_sub`. Nicht per
Konvention: physisch. Jeder Bildschirm beginnt damit, seine Canvas zu löschen —
ein Bildschirm, dem man das ganze Panel reicht, würde also STOP im Vorbeigehen
auslöschen, still und aussehend wie ein kosmetischer Fehler statt wie das
Sicherheitsproblem, das es ist. `test_nav` prüft, dass die Pixel des Bandes ein
vollständiges Rendern jedes Bildschirms überleben.

Bildschirme arbeiten außerdem in ihren eigenen Koordinaten. Der Versatz wird
einmal abgezogen, im Router, statt an neun Stellen erinnert.

## Der Splash ist der Selbsttest

![Der Splash](img/splash.png)

Keine Deko. Jedes Subsystem meldet beim Initialisieren sein eigenes Ergebnis, ein
Board ohne Karte, ein Touch-Controller, der nicht geantwortet hat, oder ein
Koprozessor, der eine Protokollversion spricht, die dieses Bedienteil nicht
kennt, sagt es also im Vorbeigehen — statt drei Bildschirme später rätselhaft
kaputt auszusehen.

Ein **Fehlschlag gilt trotzdem als gemeldet**: man muss ihn lesen und
weitergehen können und darf hier nicht stranden. Sobald jeder Schritt geantwortet
hat, hält die Liste kurz und übergibt ans Menü; ein Tippen überspringt das
Halten.

## Das Menü sind Dinge, keine Fähigkeiten

Der Katalog umfasst gut sechzig Einträge über messen, treiben, mithören,
programmieren und rechnen. Ein Menü aus *Fähigkeiten* wäre ein Ablagesystem, und
ein Werkstattwerkzeug ist keines. Eine Kachel ist **ein physischer Gegenstand,
den du vor dir hast**, und die Prüfstände kombinieren messen, treiben und
mithören für diesen einen Gegenstand.

Fünf sind lebendig. Drei sind benannt, geroutet und ehrlich:

![Setup](img/setup.png)

Gebaut, und die sieben Bildschirmfälle, die während des Neuschnitts
zurückgehalten wurden, sind mit ihm zurück. Im hellen Theme ebenfalls, denn eine
Palettenänderung, die nur ein Theme kaputt macht, ist die Sorte, die ausgeliefert
wird:

![Setup im hellen Theme](img/setup-light.png)

Beide Themes sind eingecheckt, denn eine Palettenänderung, die nur ein Theme
kaputt macht, ist die Sorte, die ausgeliefert wird:

![Das Menü im hellen Theme](img/overview-light.png)

## Ein Bildschirm, der seine Aufgabe noch nicht erfüllen kann, sagt warum

Ein „demnächst"-Feld lehrt niemanden etwas. Eines, das benennt, was es tun wird
und welche einzelne Entscheidung oder welches Bauteil zuerst ankommen muss, ist
eine To-do-Liste, die jemand beantworten kann.

![Motor und Regler](img/motor.png)

Und der Hinweis wird **grün**, wenn nichts es blockiert — was eine andere und
unbequemere Lage ist als blockiert zu sein:

![Logs](img/logs.png)

Was inzwischen gebaut ist: die Karte durchsehen, ansehen, was die Importansicht
erkannt hat und woran, dann plotten.

![Die Importansicht](img/logs-import.png)
![Der Plot](img/logs-plot.png)

Das Layout ist auf dem Weg hinein um 40 px nach oben gewandert. Es war für einen
Bildschirm gezeichnet, dem alle 480 gehörten und der sein eigenes Home-Tag in die
oberen 40 malte; beides gehört jetzt dem Router, und er reicht ein Fenster von
432 px — ohne die Verschiebung lief die Fußzeile also von 430 bis 472 in einer
Canvas von 432 Höhe, und RESCAN, OPEN und PLOT waren überhaupt nicht drückbar.

Der Servoprüfstand wird an seinem Horn befohlen: irgendwo auf dem Bogen ziehen,
und der Arm zeigt dorthin. Das Gehäuse daneben ist nicht das Stellrad, und die
Mitte der Nabe ist es auch nicht, denn sie ist keine Richtung.

![Servo](img/servo.png)

Der Arm folgt der *gemessenen* Stellung und nicht der befohlenen, ohne eigene
Glättung. Auf eine Messung hin zu glätten hieße, die Trägheit des Prüfstands auf
die des Servos zu addieren, und einmal gezeichnet sind die beiden nicht
unterscheidbar — ein langsames Servo und ein langsamer Bildschirm sehen gleich
aus, und nur eines von beiden steht auf dem Prüfstand. Die befohlene Stellung
wird deshalb blass hinter der gemessenen gezeichnet, und Verzug zeigt sich als
zwei Arme statt als Zahl, die dem Bild widerspricht.

Der Analyser ist zu zwei Dritteln Verlauf und zu einem Drittel Gegenwart. Die
Frage, die ihm gestellt wird, lautet „ich habe daran gedreht — welcher Kanal war
das?", und keine Anordnung aktueller Werte kann sie beantworten, denn die
Information steckt in der Bewegung. Jeder Kanal hält die letzten anderthalb
Sekunden, der Kanal, der sich bewegt hat, ist also der mit der Stufe darin, und
das Auge findet eine Stufe unter fünfzehn flachen Linien, ohne dass man ihm sagt,
wo. Der Balken neben jeder Spur beantwortet die andere Frage — wie weit ist er
jetzt — ohne dass man eine Zahl liest.

![Analyser](img/analyser.png)

Es ist außerdem die einzige Anordnung, die die beiden Fehler trennt, die es zu
fangen lohnt: ein Glitch ist eine Spitze in einer Spur, ein Dropout ist eine
Kerbe über alle sechzehn im selben Augenblick.

Der Zustandsblock ist das Größte auf diesem Bildschirm, und das ist der Grund:

![Ein Empfänger im Failsafe](img/analyser-failsafe.png)

Ein Empfänger im Failsafe sendet sechzehn einwandfrei geformte Kanalwerte, die er
sich auf Ansage ausgedacht hat. Jede Spur und jeder Balken wird rot, und der
Bildschirm sagt es in Worten — denn ein Prüfstand, der erfundene Zahlen in
derselben Farbe malt wie gemessene, hilft jemandem dabei, ihnen zu trauen. Vier
Zustände, nicht zwei: still, Failsafe, Frame verloren und live, jeder mit einer
Zeile, die sagt, was er bedeutet.

Der Programmierer ist nicht ein Programmiergerät. BLHeli_S und AM32 sprechen
einen One-Wire-Bootloader mit 19 200; ESCape32 antwortet auf eine CLI; VESC will
framed packets; ein Hitec-Servo der D-Serie hat etwas ganz Eigenes. Sie teilen
sich einen Stecker und sonst nichts.

Er fragt also der Reihe nach: was programmierst du, welches seiner Protokolle,
und erst dann, was geantwortet hat.

![Was programmierst du](img/programmer.png)

Flach saßen die fünf in einer Reihe, und der Bildschirm behauptete still, ein
Servoprotokoll und vier ESC-Protokolle seien dieselbe Art von Wahl. Sind sie
nicht. Die Klasse zu wählen heißt zu wählen, welches Kabel man in der Hand hat;
das Protokoll zu wählen heißt zu wählen, was man hineinspricht — und jede Zeile
benennt ihren eigenen Transport, denn genau dafür gibt es die Liste statt einer
Autoerkennung.

![Die Protokolle einer Klasse](img/programmer-protocols.png)

**BLHeli_32 steht nicht in dieser Liste, und der Grund ist nicht Aufwand.** Seine
Einstellungen sind ein 256-Byte-XTEA-Block, dessen Key nur in einer geschlossenen
Binary existiert — der Prüfstand kann sich also mit einem dieser Regler
verbinden, ihn identifizieren und den Block lesen, ohne einen einzigen Wert darin
benennen zu können. Drehrichtung, 3D-Modus, Beacon und Telemetrie funktionieren
trotzdem, weil das DShot Special Commands sind und keine Parameter. [Die ganze
Begründung](BLHeli32-de.md).

Eine Ebene zurückzugehen trennt die Verbindung. Das Gerät, das einen Bootloader
beantwortet hat, ist nicht das, das eine CLI beantworten wird, und die alte
Identität über einer neuen Liste stehen zu lassen ist die eine Lüge, die dieser
Bildschirm nicht erzählen darf. Back steigt eine Ebene hinauf, statt den
Bildschirm zu verlassen — das tut das Tag im Band.

![Nichts hat geantwortet](img/programmer-idle.png)

## Die Parameter zeichnen sich selbst

Eine Definition sagt, welche Art Größe sie ist — ein Schalter, eine Auswahl, eine
Zahl auf einem Bereich —, und der Zeichencode besitzt ein Bedienelement je Art.
Nichts im Zeichencode weiß, was BLHeli_S ist; er weiß, wie eine begrenzte Zahl
aussieht. Eine Firmware hinzuzufügen ist eine Tabelle, und der einzige Weg, neuen
Zeichencode zu brauchen, wäre eine Art von Einstellung zu erfinden, die keiner
von diesen hat — was in zwanzig Jahren ESCs niemand getan hat.

![Verbunden](img/programmer-params.png)

Das Timing ist der klarste Beleg dafür. BLHeli_S legt es in benannte Stufen, weil
sein eigener Konfigurator das tut; jede Firmware danach hat sich auf Grad
Vorzündung festgelegt, null bis etwa dreißig. Zwei Darstellungen einer
physikalischen Größe, in einer Liste, von demselben Code gezeichnet — die
Definition sagt, welche Art es ist, und das ist der ganze Unterschied:

![Grad statt benannter Stufen](img/programmer-am32.png)

So machen es die echten Konfiguratoren, und warum sie recht haben, lohnt sich zu
sagen und nicht bloß, dass sie übereinstimmen: ein Bildschirm mit einem
Bedienelement je Einstellung driftet, denn die vierzigste Einstellung schreibt
jemand in Eile. Ein Bildschirm mit drei Bedienelementen kann nicht driften. Der
Konfigurator von BLHeli hat genau drei — Checkbox, Select, Zahl —, und der von
AM32 gruppiert sie; beide wurden gelesen, bevor das hier gebaut wurde.

Zwei weitere Dinge kamen aus dem Lesen. VESC Tool gibt jedem Parameter einen
Knopf, der erklärt, was er tut, und deshalb steht die Hilfe der ausgewählten
Zeile unter der Liste: „Demag compensation" sind vier Silben und keine
Information. Und Hitecs DPC-11 teilt seinen Bildschirm in Connection, File
Operations, Testing und Programming — der Abschnitt *Testing* ist das eine, was
ein PC-Konfigurator verkauft und was dieser Prüfstand richtig könnte, denn er
kann die Endpunkte eines Servos ändern und das Servo anschließend bewegen. Das
ist noch nicht gebaut.

## Geändert ist nicht geschrieben

![Zwei vorgemerkte Änderungen](img/programmer-dirty.png)

Jeder brauchbare Konfigurator trennt, was gelesen wurde, was geändert wurde und
was geschrieben wurde. Ein Wert, der in eine Zeile eingetragen ist, die genauso
aussieht wie ein von der Hardware gelesener, ist der Fehler
gemessen-gegen-erfunden in anderen Kleidern, und dieser Prüfstand hat dazu eine
Meinung. Eine vorgemerkte Änderung trägt also eine Markierung und eine eigene
Farbe, und der Knopf zählt sie.

Stepper klemmen, statt umzulaufen. Ein Parameter, der vom letzten Wert auf den
ersten zurückrollt, wird eines Tages von jemandem ans falsche Ende gesetzt, der
einmal öfter gedrückt hat als beabsichtigt — und an einem Regler ist das falsche
Ende eine Drehrichtung.

## Auswuchten beginnt damit, wo die Sensoren hinkommen

Die Messung ist leicht und die Montage nicht. Eine Auswuchtantwort ist ein Betrag
und ein Winkel — so viel, dorthin —, und beides kommt aus zwei Sensoren, deren
Platzierung entscheidet, ob die Antwort etwas bedeutet. Keine der beiden Arten,
es falsch zu machen, sieht auf dem Bildschirm falsch aus: ein Vibrationssensor
auf einer nachgiebigen Aufnahme liest eine gefilterte Fassung dessen, was das
Lager getan hat, und eine Indexmarke auf einem Blatt liefert einen Impuls je
Blatt und einen Winkel, der um einen ganzen Blattabstand danebenliegt.

Die Antwort ist ein Winkel ab der Indexmarke, und ein Winkel wird erst brauchbar,
wenn man weiß, an welchem Blatt er liegt: „0,35 g bei 265 Grad" ist eine Zahl,
„zwischen Blatt zwei und drei" ist eine Anweisung. Also wird die Blattzahl
abgefragt, und der Bildschirm rechnet.

![Der Rotor und wohin die Masse kommt](img/balance.png)

Ein Impeller ist eine andere Aufgabe und sagt das auch. An ein Blattende im
Kanal kommt man nicht heran, die Korrektur wird also als Winkel an der Nabe
angegeben statt als Blatt zum Bekleben:

![Ein fünfblättriger Impeller](img/balance-edf.png)

![Wohin die Sensoren am Prüfstand kommen](img/balance-rig.png)

Zwei Details darin sind die, die falsch gemacht werden. Die Indexmarke kommt auf
die **Glocke** des Motors und nicht auf einen Spinner: ein Prüfstand läuft oft
ohne einen, und ein auf die Spinnernase gerichteter Strahl muss von vorn kommen,
quer über die Kreisfläche — dieselbe Blickrichtung, die am Ende Blätter zählt.
Die Glocke dreht mit der Welle, ist starr, ist da, welcher Propeller auch
montiert ist, und lässt sich von unten beobachten, wo nichts im Weg ist.

Und die Marke ist ein **Filzstiftstrich**, kein Reflexband. Alles, was auf die
Glocke geklebt wird, ist Masse — an genau dem Teil der Maschine, dessen Masse
gemessen werden soll. Man würde den eigenen Marker auswuchten.

Der Prüfstand ist der leichte Fall, denn die Platzierung liegt in der eigenen
Hand. Am Modell tut sie das fast nirgends:

![Wohin die Sensoren am Modell kommen](img/balance-aircraft.png)

Die **Motorhaube ist nicht der Spant**. Sie ist eine Verkleidung, an einen Spant
geschraubt und oft in Gummi, frei beweglich gegenüber dem, dessen Vibration man
haben will. Der Motorspant ist die eine starre Fläche an diesem Ende des Modells,
und der Beschleunigungssensor kommt flach dagegen — was zu sagen sich lohnt, denn
ein flach montiertes Dreiachsteil legt zwei seiner Achsen in die Ebene des
Spants, quer zur Welle. Eine davon nehmen und die dritte ignorieren.

Die Marke kommt weiterhin auf die Glocke, und hier gibt es dafür einen zweiten
Grund: **ein Spinner geht ab**. Jedes Mal, wenn er zum Transport abgenommen wird,
kommt er in einem neuen Winkel wieder dran, und die Phasenreferenz, gegen die die
letzte Auswuchtung gemessen wurde, geht mit ihm. Die Glocke ist Teil des Rotors
und bewegt sich nie. An sehr vielen Elektromodellen steht der Außenläufer ohnehin
vor der Haube, und genau das macht dieselbe Anordnung wie am Prüfstand möglich —
Sensor darunter, Blick gerade nach oben, nichts kreuzend.

Und das ganze Flugzeug muss festgezurrt sein. Eine Maschine, die frei schaukeln
kann, ist eine Feder, die niemand gewählt hat, in Reihe mit allem, was man messen
will.

Zwei Dinge zur Verzögerung, denn die Sensorwahl hängt daran. Ohne Indeximpuls gibt
es überhaupt keine Phasenreferenz, ausgewuchtet wird dann mit der Vier-Lauf-Methode
— Grundlauf, dann eine Probemasse bei null, hundertzwanzig und zweihundertvierzig
Grad —, die nie eine Phase misst und der eine konstante Verzögerung deshalb nichts
anhaben kann. Was sie stattdessen braucht, ist *Bandbreite*: bei 10 000 min⁻¹ liegt
die Grundfrequenz bei 167 Hz, und eine fertige IMU, die fusionierte Lage mit 100 Hz
streamt, sieht davon gar nichts. Sie ist nicht zu spät, sie ist blind.

Mit Indeximpuls halbieren sich die Läufe, und dann zählt die Verzögerung — aber nur
ihre Schwankung. Eine konstante Verzögerung kürzt sich im Einflusskoeffizienten
heraus, denn der Probelauf misst die ganze Kette einschließlich des Sensors. Ein
analoges Bauteil in den eigenen Wandler des Koprozessors sind etwa fünfzehn
Mikrosekunden, also unter einem Grad bei 10 000 min⁻¹; eine fusionierte IMU sind
fünf Millisekunden, weder konstant noch veröffentlicht, also dreihundert Grad und
eine mit großer Präzision an der falschen Stelle angebrachte Masse.

## Der Akku, als Urteil angeordnet

Eine Spalte Zellenspannungen ist leicht zu zeichnen und schwer zu lesen: sechs
Zahlen, die auf zwei Nachkommastellen übereinstimmen, von denen eine still um
vierzig Millivolt danebenliegt. Die Größe, auf die es ankommt, ist die
**Spreizung**, die Zellen werden also als Abweichungen von ihrem eigenen
Mittelwert gezeichnet, und die Zahl, die das Urteil entscheidet, ist der größte
Abstand zwischen zwei beliebigen von ihnen.

![Zellenabweichung](img/battery.png)

Spreizung ist zwischen Zellen, nie gegen einen Nennwert. Ein Akku, der leer aber
gleichmäßig ist, ist ein entladener Akku; ein Akku, der voll aber ungleichmäßig
ist, ist ein kaputter — und nur der zweite geht diesen Bildschirm etwas an.

Die Skala folgt dem Akku, mit einer Untergrenze. Eine feste Skala von hundert
Millivolt zeichnet einen gesunden Akku als sechs flache Linien und einen kranken
als fünf flache Linien und einen Stummel, was dasselbe Bild ist — der Bereich ist
deshalb die schlimmste Abweichung des Akkus selbst mit etwas Luft, und die Skala
steht daneben, denn ein Balken, der den Plot füllt, könnte sonst vier Millivolt
sein oder vierzig.

Und gemessen wird unter Last. In Ruhe sieht eine müde Zelle aus wie jede andere.
