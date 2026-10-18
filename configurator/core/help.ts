// What a function with a tick does, in one or two sentences per language.
//
// The help bubble of a tick in the chooser opens with this text. The catalogue
// holds the name of a function and no description; a part has its `role`,
// which the bubble of a part reads instead. The parts a function needs and the
// supply of a part are not written here: the page derives them from the
// catalogue and the resolver, so they cannot drift from the data.
//
// Every fact below restates a source the catalogue entry of the function
// names, or a check entry of the catalogue. `tools/check.ts` holds one entry
// per function with a tick, no other, and applies the text rules of the data.

import type { FunctionId, Text } from '../contract.ts';

function t(en: string, de: string): Text {
  return { en, de };
}

export const FUNCTION_HELP: Readonly<Partial<Record<FunctionId, Text>>> = Object.freeze({
  servo: t(
    'Sends a PWM signal to a servo from the SERVO screen: pulse width, trim, travel and reverse, at a frame rate of 50 Hz unless another is set.',
    'Gibt vom SERVO-Bildschirm ein PWM-Signal an ein Servo aus: Pulsbreite, TRIM, WEG und REVERSE, mit einer Frame Rate von 50 Hz, wenn keine andere eingestellt ist.',
  ),
  servotest: t(
    'Measures the current of the servo at rest, moving and at each end, its travel time from end to end, and the voltage below which it stops moving. The PD mini sets the steps 4.8 V and 6.0 V, with HV SERVO on also 7.4 V and 8.4 V.',
    'Misst den Strom des Servos in Ruhe, in Bewegung und an jedem Ende, seine Stellzeit von Ende zu Ende und die Spannung, unter der es sich nicht mehr bewegt. Der PD mini stellt die Stufen 4,8 V und 6,0 V, mit HV SERVO an auch 7,4 V und 8,4 V.',
  ),
  servocur: t(
    'Reads the current of the servo on channel 1 of the INA3221, 1000 samples per second, over the sensor bus at 400 kHz.',
    'Liest den Strom des Servos an Kanal 1 des INA3221, 1000 Abtastwerte pro Sekunde, über den Sensorbus mit 400 kHz.',
  ),
  angle: t(
    'Reads the angle of the servo horn with an AS5600 magnetic sensor on the output shaft: 12 bit, 500 readings per second.',
    'Liest den Winkel des Ruderhorns mit einem AS5600-Magnetsensor an der Abtriebswelle: 12 Bit, 500 Messungen pro Sekunde.',
  ),
  servoprog: t(
    'Reads the 32 registers of a KST or Chaservo servo over its signal wire, shows them as settings and writes changed settings back. The servo signal moves to the programming port, GP13.',
    'Liest die 32 Register eines KST- oder Chaservo-Servos über dessen Signalleitung, zeigt sie als Einstellungen und schreibt geänderte Einstellungen zurück. Das Servosignal wechselt an den Programmierport, GP13.',
  ),
  esc: t(
    'Sends a PWM or DShot throttle signal to an ESC over signal and GND. The ESC takes its power from its own battery (ESC power A) or from the PD mini (ESC power B).',
    'Gibt ein PWM- oder DShot-Gassignal über Signal und GND an einen ESC aus. Der ESC wird aus seinem eigenen Akku (ESC-Versorgung A) oder vom PD mini (ESC-Versorgung B) versorgt.',
  ),
  escpow: t(
    'Reads the supply voltage and the current of the ESC with the INA228, 500 times per second each, and sums the energy.',
    'Liest Versorgungsspannung und Strom des ESC mit dem INA228, je 500-mal pro Sekunde, und summiert die Energie.',
  ),
  stick: t(
    'Sets an ESC through its throttle-stick menu; 24 of the 72 ESC profiles run. The bench moves the throttle to the positions of the ESC profile and counts the beeps of the menu as pulses in the supply current of the PD mini. Load: a resistor load or a motor mounted solid, without propeller.',
    'Stellt einen ESC über das Menü am Gasknüppel ein; 24 der 72 ESC-Profile laufen. Der Prüfstand fährt das Gas auf die Stellungen des ESC-Profils und zählt die Pieptöne des Menüs als Pulse im Versorgungsstrom des PD mini. Last: ein Lastwiderstand oder ein fest montierter Motor ohne Propeller.',
  ),
  tap: t(
    'Reads the pitch and the length of the beeps of the ESC on phase A through the phase tap. The motor simulator stands in for the motor.',
    'Liest Tonhöhe und Länge der ESC-Pieptöne an Phase A über den Phasenabgriff. Der Motorsimulator ersetzt den Motor.',
  ),
  supply: t(
    'The PD mini gives an output set on the panel: 1 to 20 V, 0.05 to 3 A (vendor values, not measured), the voltage at least 0.7 V below the input voltage of the module. The bench reads the module every 102 to 106 ms.',
    'Der PD mini liefert einen am Panel eingestellten Ausgang: 1 bis 20 V, 0,05 bis 3 A (Herstellerangaben, nicht gemessen), die Spannung mindestens 0,7 V unter der Eingangsspannung des Moduls. Der Prüfstand liest das Modul alle 102 bis 106 ms.',
  ),
  logs: t(
    'Writes a log of each run to the microSD card: while the bench is ARMED, and while it is SAFE with the supply output on. The log viewer of the panel reads these files.',
    'Schreibt ein Log jedes Laufs auf die microSD-Karte, solange der Prüfstand ARMED ist oder bei SAFE der Ausgang des Netzteils an ist. Der Log-Viewer des Panels liest diese Dateien.',
  ),
  estop: t(
    'A latching button in the heartbeat wire. Pressed, its NC contact opens the wire; after 150 ms without a heartbeat edge the IOmcu switches the bench to SAFE: its outputs and the output of the PD mini go off. The button breaks no power line: an ESC battery or a servo supply of your own stays connected.',
    'Ein rastender Taster im Heartbeat-Draht. Gedrückt öffnet sein Öffner (NC) den Draht; nach 150 ms ohne Heartbeat-Flanke schaltet der IOmcu den Prüfstand auf SAFE: seine Ausgänge und der Ausgang des PD mini gehen aus. Der Taster trennt keine Versorgungsleitung: ein ESC-Akku oder eine eigene Servoversorgung bleibt angeschlossen.',
  ),
});
