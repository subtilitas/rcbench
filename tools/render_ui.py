#!/usr/bin/env python3
"""Render every screen on the host and save it as a PNG (Portable Network
Graphics) image.

The UI (user interface) code has no ESP-IDF (Espressif Internet-of-Things
Development Framework) dependency, so the host build links the rasteriser,
the fonts and the layout code the panel runs.  The output is what the display
shows.

    python3 tools/render_ui.py                   # every screen into docs/img/
    python3 tools/render_ui.py overview -o /tmp/overview.png --lang en
    python3 tools/render_ui.py --check           # the committed images match
    python3 tools/render_ui.py --fit             # every string fits

Every screen is rendered in English into docs/img/ and in German into
docs/img/de/.  --fit builds the renderer with GFX_TEXT_TRACE, draws every
view and every FIT_ONLY view in both languages and fails when a string is
wider than its box, is cut at the edge of the area it is drawn in, runs past
the shape it is printed on, overlaps another string, or is painted over by a
fill drawn after it.
The English overflows in KNOWN_OVERFLOWS are listed and do not fail; one of
them that no longer overflows fails until it is taken off the list.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent

# The flags of the libraries in test/host/CMakeLists.txt, with -Werror: a
# warning in the renderer or in the code it links fails the build.
STRICT = ["-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wconversion",
          "-Wno-sign-conversion"]

SOURCES = [
    "test/host/render_screen.c",
    "shared/gfx/gfx.c",
    "shared/gfx/gfx_text.c",
    "shared/gfx/gfx_seg.c",
    "shared/gfx/gfx_font8x16.c",
    "shared/gfx/gfx_font16x28.c",
    "shared/gfx/gfx_font_num24x30.c",
    "shared/ui/ui_theme.c",
    "shared/ui/ui_widgets.c",
    "shared/ui/ui_icons.c",
    "shared/ui/ui_band.c",
    "shared/ui/ui_plot.c",
    "shared/ui/ui_slider.c",
    "shared/ui/ui_tabs.c",
    "shared/ui/ui_workbench.c",
    "shared/ui/ui_target_row.c",
    "shared/ui/prog_registry.c",
    "shared/ui/ui_watermark.c",
    "shared/ui/ui_router.c",
    "shared/ui/ui_text.c",
    "shared/ui/ui_text_de.c",
    "shared/ui/splash_screen.c",
    "shared/ui/overview_screen.c",
    "shared/ui/motor_screen.c",
    "shared/ui/motor_workbench.c",
    "shared/ui/motor_settings_page.c",
    "shared/ui/motor_program_page.c",
    "shared/ui/supply_screen.c",
    "shared/ui/ui_keypad.c",
    "shared/ui/ui_textkey.c",
    "shared/ui/ui_pinlist.c",
    "shared/ui/servo_screen.c",
    "shared/ui/servo_settings_panel.c",
    "shared/ui/servo_test_panel.c",
    "shared/ui/servo_limit_panel.c",
    "shared/ui/servo_workbench.c",
    # The programmer for KST and Chaservo servos on SERVO, and under it
    # what the views drive it with: the link module, the IOmcu's
    # port and the core.
    "shared/ui/kst_panel.c",
    "shared/bench/kst_prog.c",
    "shared/bench/kst_prog_card.c",
    "shared/bench/kst_prog_file.c",
    "shared/bench/kst_card.c",
    "shared/bench/prog_link.c",
    "shared/logfile/kst_store.c",
    "protocols/kst/kst_file.c",
    "shared/bench/kst_link.c",
    "shared/outputs/kst_port.c",
    "shared/link/link_dev.c",
    "shared/link/link_crc.c",
    "shared/link/link_drawing.c",
    "protocols/kst/kst_reg.c",
    "protocols/kst/kst_limits.c",
    "protocols/kst/kst_plan.c",
    "protocols/kst/kst_session.c",
    "protocols/kst/kst_wire.c",
    "shared/ui/analyser_screen.c",
    "shared/ui/balance_screen.c",
    "shared/ui/battery_screen.c",
    "shared/ui/prog_models.c",
    "shared/ui/esc_stick_pages.c",
    "shared/ui/esc_stick_panel.c",
    "shared/ui/outputs_screen.c",
    "shared/ui/outputs_port.c",
    "shared/ui/picker_screen.c",
    "shared/ui/busfault_screen.c",
    "shared/ui/wifi_screen.c",
    "shared/ui/coproc_update_screen.c",
    "shared/ui/upd_screen.c",
    # The ESC profiles and the stick programmer MOTOR & ESC > PROGRAM's
    # ESC STICK session lists and runs.
    "shared/esc/esc_profiles_gen.c",
    "shared/esc/esc_json.c",
    "shared/esc/esc_json_lex.c",
    "shared/esc/esc_registry.c",
    "shared/esc/esc_stick.c",
    "shared/esc/esc_stick_rules.c",
    "shared/esc/esc_det.c",
    "shared/esc/esc_stick_read.c",
    "shared/esc/esc_sim.c",
    # The board drawing the IOmcu serves: generated data, so the
    # screenshot is the drawing a bench shows.
    "firmware/iomcu/src/board_drawing_rp2350_can.c",
    "shared/ui/board_drawing.c",
    "shared/ui/log_viewer_screen.c",
    "shared/ui/log_select.c",
    "shared/ui/settings_screen.c",
    "shared/settings/settings.c",
    "shared/logfile/log_numbers.c",
    "shared/logfile/log_csv.c",
    "shared/logfile/log_reader.c",
    "shared/logfile/log_fields.c",
    "shared/logfile/log_name.c",
    "shared/bench/bench_state.c",
    "shared/bench/telemetry_sim.c",
    "shared/bench/supply.c",
    "shared/servo/servo_sim.c",
    "shared/servo/servo_sweep.c",
    "shared/servo/servo_move.c",
    "shared/servo/servo_test.c",
    "shared/servo/servo_str.c",
    "shared/servo/servo_report.c",
    "shared/servo/servo_limit.c",
    "shared/servo/servo_limrun.c",
    "shared/servo/servo_limrun_report.c",
    "protocols/sbus/sbus.c",
    "protocols/dshot/dshot_telem.c",
    "shared/outputs/outputs.c",
    "shared/outputs/outputs_pages.c",
    "shared/outputs/out_pwm_map.c",
    "shared/outputs/out_bind.c",
    "shared/outputs/servo_cmd.c",
    "shared/can/can_selftest.c",
    "shared/link/link_can.c",
    "shared/link/link_control.c",
]

# Every screen gets a committed screenshot; CI (continuous integration)
# checks them all.
# name -> (committed file, screen the renderer knows, theme)
SCREENS = {
    "splash":     ("splash.png",     "splash",     "dark"),
    "overview":   ("overview.png",   "overview",   "dark"),
    # The same menu in the other theme, committed so a palette change that
    # breaks one theme is caught.
    "overview-light": ("overview-light.png", "overview", "light"),
    # The help bubbles of the band's chips over the menu: FAULT with the
    # three bits this release sets, LINK, SAFE, and ARMED MOTOR.
    "band-fault": ("band-fault.png", "overview", "dark"),
    "band-link":  ("band-link.png",  "overview", "dark"),
    "band-link-refused": ("band-link-refused.png", "overview", "dark"),
    "band-safe":  ("band-safe.png",  "overview", "dark"),
    "band-armed": ("band-armed.png", "overview", "dark"),
    # An alert across the bottom of the menu, with its x.
    "overview-alert": ("overview-alert.png", "overview", "dark"),
    "motor":      ("motor.png",      "motor",      "dark"),
    "motor-held": ("motor-held.png", "motor-held", "dark"),
    # A linked bench before its first run, no pin bound as a motor.
    "motor-idle": ("motor-idle.png", "motor", "dark"),
    # The TABLE pane with an INA228 as BENCH's source, the ESC's own
    # telemetry beside it.
    "motor-table": ("motor-table.png", "motor", "dark"),
    # An IOmcu that has refused link frames: the count after the
    # error count in the header strip, with the link up.
    "motor-refused": ("motor-refused.png", "motor", "dark"),
    # SETTINGS: ESC / BENCH's rows under PACK, MOTOR and THROTTLE OUTPUT on
    # a SAFE bench after a run, and the keypad of Capacity over them.
    "motor-settings": ("motor-settings.png", "motor", "dark"),
    "motor-settings-keypad": ("motor-settings-keypad.png", "motor", "dark"),
    "servo":      ("servo.png",      "servo",      "dark"),
    # The same bench ARMED: DISARM, the dial live.
    "servo-armed": ("servo-armed.png", "servo", "dark"),
    # The INA3221 as the servo rail's meter: its name over the plot and
    # CH1's windows in the CURRENT row, the line and the plot; and the same
    # with the last window clipped, its current in the warning colour.
    "servo-ina":      ("servo-ina.png",      "servo", "dark"),
    "servo-clipped":  ("servo-clipped.png",  "servo", "dark"),
    # The programming port as the target: the TARGET row in the warning
    # colour.
    "servo-target":   ("servo-target.png",   "servo", "dark"),
    # The panel's model as the supply: the MODELLED pill on the plot.
    "servo-model":    ("servo-model.png",    "servo", "dark"),
    # SETTINGS' OUTPUT and SWEEP pages; TEST's run cards; the AUTOMATIC
    # TEST card's settings and the LIMIT RUN card's, one level down.
    "servo-settings": ("servo-settings.png", "servo", "dark"),
    "servo-sweep-page": ("servo-sweep-page.png", "servo", "dark"),
    "servo-test":     ("servo-test.png",     "servo", "dark"),
    "servo-test-settings": ("servo-test-settings.png", "servo", "dark"),
    "servo-limits":   ("servo-limits.png",   "servo", "dark"),
    "servo-warning":  ("servo-warning.png",  "servo", "dark"),
    "servo-name":     ("servo-name.png",     "servo", "dark"),
    # The DUT page with the output encoder on, and its angle in the
    # MEASURED row.
    "servo-encoder":  ("servo-encoder.png",  "servo", "dark"),
    "servo-hv":       ("servo-hv.png",       "servo", "dark"),
    "servo-run":      ("servo-run.png",      "servo", "dark"),
    "servo-result":   ("servo-result.png",   "servo", "dark"),
    # A run that read the INA3221's CH1 windows: the meter named in the
    # result box, the line and the plot.
    "servo-result-ina": ("servo-result-ina.png", "servo", "dark"),
    "servo-sweep":    ("servo-sweep.png",    "servo", "dark"),
    # The limit run from TEST's LIMIT RUN card (servo_limrun.h) against
    # the simulated KST servo on the programming port: SEARCH+PROG chosen,
    # a run partway, run
    # out to PASS, SEARCH ONLY on the bench's own output, STOP after a write
    # with RESTORE named, RESTORE's list in the programmer after a STOP in a
    # session, and SERVO PULSES at KEEP refused: the servo's 900-2100 us
    # outside the bench's 1000-2000 us.
    "servo-limrun-ready":   ("servo-limrun-ready.png",   "servo", "dark"),
    "servo-limrun":         ("servo-limrun.png",         "servo", "dark"),
    "servo-limrun-pass":    ("servo-limrun-pass.png",    "servo", "dark"),
    "servo-limrun-found":   ("servo-limrun-found.png",   "servo", "dark"),
    "servo-limrun-stopped": ("servo-limrun-stopped.png", "servo", "dark"),
    "servo-limrun-restore": ("servo-limrun-restore.png", "servo", "dark"),
    "servo-limrun-range":   ("servo-limrun-range.png",   "servo", "dark"),
    # The programmer for KST and Chaservo servos over the left card: each
    # tab, each list and each page that asks for a hold, against the
    # simulated servo.
    # SERVO > PROGRAM: the registry's list, the KST and Chaservo row live
    # with the port's pin; with no IOmcu answering, dimmed with its reason.
    "servo-program": ("servo-program.png", "servo", "dark"),
    "servo-program-nocap": ("servo-program-nocap.png", "servo", "sim"),
    "servo-kst": ("servo-kst.png", "servo", "dark"),
    "servo-kst-nopage": ("servo-kst-nopage.png", "servo", "dark"),
    # The tester drives the standard outputs: PROGRAM is locked.
    "servo-kst-target": ("servo-kst-target.png", "servo", "dark"),
    # A PD mini answers: the voltage and limit CONNECT sets, the list
    # that holds them, their keypads and the hold above 6.0 V.
    "servo-kst-supply": ("servo-kst-supply.png", "servo", "dark"),
    "servo-kst-options": ("servo-kst-options.png", "servo", "dark"),
    "servo-kst-voltage": ("servo-kst-voltage.png", "servo", "dark"),
    "servo-kst-hv": ("servo-kst-hv.png", "servo", "dark"),
    "servo-kst-wait": ("servo-kst-wait.png", "servo", "dark"),
    # The same supply with the servo read, and LEAVE tapped.
    "servo-kst-supplied": ("servo-kst-supplied.png", "servo", "dark"),
    "servo-kst-leave-supply": ("servo-kst-leave-supply.png", "servo",
                               "dark"),
    # A port whose pin a slot holds.
    "servo-kst-nopin": ("servo-kst-nopin.png", "servo", "dark"),
    "servo-kst-working": ("servo-kst-working.png", "servo", "dark"),
    "servo-kst-read": ("servo-kst-read.png", "servo", "dark"),
    "servo-kst-readonly": ("servo-kst-readonly.png", "servo", "dark"),
    "servo-kst-locked": ("servo-kst-locked.png", "servo", "dark"),
    "servo-kst-failsafe": ("servo-kst-failsafe.png", "servo", "dark"),
    "servo-kst-enable": ("servo-kst-enable.png", "servo", "dark"),
    "servo-kst-leave": ("servo-kst-leave.png", "servo", "dark"),
    "servo-kst-more": ("servo-kst-more.png", "servo", "dark"),
    "servo-kst-release": ("servo-kst-release.png", "servo", "dark"),
    "servo-kst-keypad": ("servo-kst-keypad.png", "servo", "dark"),
    "servo-kst-stretch": ("servo-kst-stretch.png", "servo", "dark"),
    "servo-kst-soft": ("servo-kst-soft.png", "servo", "dark"),
    "servo-kst-hard": ("servo-kst-hard.png", "servo", "dark"),
    "servo-kst-tune": ("servo-kst-tune.png", "servo", "dark"),
    "servo-kst-pulse": ("servo-kst-pulse.png", "servo", "dark"),
    "servo-kst-files": ("servo-kst-files.png", "servo", "dark"),
    "servo-kst-files-none": ("servo-kst-files-none.png", "servo", "dark"),
    "servo-kst-files-next": ("servo-kst-files-next.png", "servo", "dark"),
    "servo-kst-name": ("servo-kst-name.png", "servo", "dark"),
    "servo-kst-name-exists": ("servo-kst-name-exists.png", "servo", "dark"),
    "servo-kst-name-bad": ("servo-kst-name-bad.png", "servo", "dark"),
    "servo-kst-name-saved": ("servo-kst-name-saved.png", "servo", "dark"),
    "servo-kst-file-loaded": ("servo-kst-file-loaded.png", "servo", "dark"),
    "servo-kst-file-edited": ("servo-kst-file-edited.png", "servo", "dark"),
    "servo-kst-file-untyped": ("servo-kst-file-untyped.png", "servo", "dark"),
    "servo-kst-file-locked": ("servo-kst-file-locked.png", "servo", "dark"),
    "servo-kst-file-type": ("servo-kst-file-type.png", "servo", "dark"),
    "servo-kst-file-v6": ("servo-kst-file-v6.png", "servo", "dark"),
    "servo-kst-file-same": ("servo-kst-file-same.png", "servo", "dark"),
    "servo-kst-file-damaged": ("servo-kst-file-damaged.png", "servo", "dark"),
    "servo-kst-file-bad": ("servo-kst-file-bad.png", "servo", "dark"),
    "servo-kst-file-newer": ("servo-kst-file-newer.png", "servo", "dark"),
    "servo-kst-file-hard": ("servo-kst-file-hard.png", "servo", "dark"),
    "servo-kst-file-soft": ("servo-kst-file-soft.png", "servo", "dark"),
    "servo-kst-card-none": ("servo-kst-card-none.png", "servo", "dark"),
    "servo-kst-card-full": ("servo-kst-card-full.png", "servo", "dark"),
    "servo-kst-changes": ("servo-kst-changes.png", "servo", "dark"),
    "servo-kst-write": ("servo-kst-write.png", "servo", "dark"),
    "servo-kst-writing": ("servo-kst-writing.png", "servo", "dark"),
    "servo-kst-written": ("servo-kst-written.png", "servo", "dark"),
    "servo-kst-write-nocard": ("servo-kst-write-nocard.png", "servo", "dark"),
    "servo-kst-nofile": ("servo-kst-nofile.png", "servo", "dark"),
    "servo-kst-doubt": ("servo-kst-doubt.png", "servo", "dark"),
    "servo-kst-journal": ("servo-kst-journal.png", "servo", "dark"),
    "servo-kst-restore-list": ("servo-kst-restore-list.png", "servo", "dark"),
    "servo-kst-restore": ("servo-kst-restore.png", "servo", "dark"),
    "servo-kst-restore-check": ("servo-kst-restore-check.png", "servo",
                                "dark"),
    "servo-kst-other": ("servo-kst-other.png", "servo", "dark"),
    "servo-kst-swapped": ("servo-kst-swapped.png", "servo", "dark"),
    "servo-paused":   ("servo-paused.png",   "servo", "dark"),
    "analyser":   ("analyser.png",   "analyser",   "dark"),
    # Nothing decoded: what a bench shows, where no firmware feeds it.
    "analyser-silent": ("analyser-silent.png", "analyser", "dark"),
    "logs":       ("logs.png",       "logs",       "dark"),
    # No card mounted.
    "logs-nocard": ("logs-nocard.png", "logs", "dark"),
    # A card of 121 files, more than a page: the second page, PREV and NEXT
    # in the header row.
    "logs-pages": ("logs-pages.png", "logs", "dark"),
    "analyser-failsafe": ("analyser-failsafe.png", "analyser", "dark"),
    # MOTOR & ESC > PROGRAM: the registry's list, its STICK MENU row.
    "motor-program": ("motor-program.png", "motor", "dark"),
    # ESC STICK, opened from that row, full width under the output strip.
    "motor-stick": ("motor-stick.png", "motor", "dark"),
    "motor-stick-items": ("motor-stick-items.png", "motor", "dark"),
    "motor-stick-timing": ("motor-stick-timing.png", "motor", "dark"),
    "motor-stick-warning": ("motor-stick-warning.png", "motor", "dark"),
    "motor-stick-run": ("motor-stick-run.png", "motor", "dark"),
    # The run's page with the phase tap enabled and running, its readout
    # under the current line.
    "motor-stick-tone": ("motor-stick-tone.png", "motor", "dark"),
    "motor-stick-done": ("motor-stick-done.png", "motor", "dark"),
    "motor-stick-aborted": ("motor-stick-aborted.png", "motor", "dark"),
    "motor-stick-failed": ("motor-stick-failed.png", "motor", "dark"),
    "motor-stick-find": ("motor-stick-find.png", "motor", "dark"),
    "motor-stick-found": ("motor-stick-found.png", "motor", "dark"),
    "motor-stick-hand-list": ("motor-stick-hand-list.png", "motor", "dark"),
    "motor-stick-hand-info": ("motor-stick-hand-info.png", "motor", "dark"),
    "motor-stick-hand": ("motor-stick-hand.png", "motor", "dark"),
    "motor-stick-hand-warning": ("motor-stick-hand-warning.png",
                                 "motor", "dark"),
    "motor-stick-hand-prompt": ("motor-stick-hand-prompt.png",
                                "motor", "dark"),
    "motor-stick-hand-after": ("motor-stick-hand-after.png", "motor", "dark"),
    "motor-stick-hand-steps": ("motor-stick-hand-steps.png", "motor", "dark"),
    "motor-stick-hand-end": ("motor-stick-hand-end.png", "motor", "dark"),
    "motor-stick-hand-locked": ("motor-stick-hand-locked.png",
                                "motor", "dark"),
    "logs-import":("logs-import.png","logs",       "dark"),
    "logs-plot":  ("logs-plot.png",  "logs",       "dark"),
    "logs-delete":("logs-delete.png","logs",       "dark"),
    # SETUP's tiles, clean and with a value unsaved; INTERFACES' list
    # across the screen with a value unsaved.
    "setup":      ("setup.png",      "setup",      "dark"),
    "setup-hub-dirty": ("setup-hub-dirty.png", "setup", "dark"),
    "setup-dirty":("setup-dirty.png","setup",      "dark"),
    # PANEL: the language, the theme and the knob.
    "setup-application": ("setup-application.png", "setup", "dark"),
    # INTERFACES with both current monitors enabled, at the top of the list
    # and drawn on to the INA3221's rows and the bus's pins.
    "setup-interfaces": ("setup-interfaces.png", "setup", "dark"),
    "setup-sensors": ("setup-sensors.png", "setup", "dark"),
    # The same rows with the pins the IOmcu has not taken: the mark on
    # Sensor SDA and Sensor SCL, and on the INA3221 that is off for them.
    "setup-unheld": ("setup-unheld.png", "setup", "dark"),
    # The same list past them, to the phase tap's rows, the tap enabled.
    "setup-tap": ("setup-tap.png", "setup", "dark"),
    # The warning on leaving SETUP unsaved: one change, and twelve, of
    # which nine fit beside the count of the rest.
    "setup-leave": ("setup-leave.png", "setup", "dark"),
    "setup-leave-many": ("setup-leave-many.png", "setup", "dark"),
    "outputs":    ("outputs.png",    "outputs",    "dark"),
    "outputs-protocol": ("outputs-protocol.png", "outputs", "dark"),
    "outputs-held": ("outputs-held.png", "outputs", "dark"),
    "outputs-full": ("outputs-full.png", "outputs", "dark"),
    # The longest protocol name, DSHOT600 BIDIR, in the closed list.
    "outputs-bidir": ("outputs-bidir.png", "outputs", "dark"),
    # The overview every visit opens on, OFF: every bound pin names its
    # protocol, the longest among them.
    "outputs-off": ("outputs-off.png", "outputs", "dark"),
    # The binding last read, after a read that failed.
    "outputs-unread": ("outputs-unread.png", "outputs", "dark"),
    # The same after a read of pages no binding describes, with the key
    # that unbinds every pin.
    "outputs-odd": ("outputs-odd.png", "outputs", "dark"),
    # GP13 is the programming port's.
    "outputs-prog": ("outputs-prog.png", "outputs", "dark"),
    # The PROG PORT entry, as SERVO's TARGET row opens it with the port as
    # the tester's target: GP13 lit, SERVO TESTER and PORT PWM.
    "outputs-port": ("outputs-port.png", "outputs", "dark"),
    "picker": ("picker.png", "picker", "dark"),
    "picker-drawn": ("picker-drawn.png", "picker", "dark"),
    # GP13 is the programming port's: held, and named under IN USE.
    "picker-prog": ("picker-prog.png", "picker", "dark"),
    # Eight of eight slots: every free key grey, and the reason why.
    "picker-full": ("picker-full.png", "picker", "dark"),
    # The protocol list open over the board.
    "picker-protocol": ("picker-protocol.png", "picker", "dark"),
    # OFF picked, GP13 the programming port's, 8 pins over 6 protocols:
    # the tallest IN USE table, in its 20 px rows.
    "picker-off": ("picker-off.png", "picker", "dark"),
    "busfault": ("busfault.png", "busfault", "dark"),
    "supply": ("supply.png", "supply", "dark"),
    "supply-settings": ("supply-settings.png", "supply", "dark"),
    "supply-keypad": ("supply-keypad.png", "supply", "dark"),
    "supply-confirm": ("supply-confirm.png", "supply", "dark"),
    # The PD mini enabled and not answering, the output never on.
    "supply-offline": ("supply-offline.png", "supply", "dark"),
    "busfault-silent": ("busfault-silent.png", "busfault", "dark"),
    "busfault-lost": ("busfault-lost.png", "busfault", "dark"),
    # The IOmcu update: the question a link-up poses, the same with
    # an image older than the one running, and the progress half way.
    "coproc-update": ("coproc-update.png", "coproc-update", "dark"),
    "coproc-update-older": ("coproc-update-older.png", "coproc-update",
                            "dark"),
    "coproc-update-progress": ("coproc-update-progress.png",
                               "coproc-update", "dark"),
    "setup-light":("setup-light.png","setup",      "light"),
    "battery":    ("battery.png",    "battery",    "dark"),
    # No cell monitor: what a bench shows, where no firmware feeds it.
    "battery-none": ("battery-none.png", "battery", "dark"),
    "balance":    ("balance.png",    "balance",    "dark"),
    "balance-rig": ("balance-rig.png", "balance", "dark"),
    "balance-aircraft": ("balance-aircraft.png", "balance", "dark"),
    "balance-edf": ("balance-edf.png", "balance", "dark"),
    # WI-FI from LOGS: the radio off with nothing kept; a home
    # network kept whose join failed, the access code off; the gate shut.
    "wifi": ("wifi.png", "wifi", "dark"),
    "wifi-home": ("wifi-home.png", "wifi", "dark"),
    "wifi-blocked": ("wifi-blocked.png", "wifi", "dark"),
    # The access point up and an upload arriving; joining; joined.
    "wifi-ap": ("wifi-ap.png", "wifi", "dark"),
    "wifi-joining": ("wifi-joining.png", "wifi", "dark"),
    "wifi-joined": ("wifi-joined.png", "wifi", "dark"),
    # CHOOSE HOME NETWORK: the scan running, and its list.
    "wifi-scanning": ("wifi-scanning.png", "wifi", "dark"),
    "wifi-scan": ("wifi-scan.png", "wifi", "dark"),
    # The Wi-Fi keyboard: a hidden network's name on each layer, an empty
    # one refused, and a password masked, shown, and refused at 7.
    "wifi-key-lower": ("wifi-key-lower.png", "wifi", "dark"),
    "wifi-key-upper": ("wifi-key-upper.png", "wifi", "dark"),
    "wifi-key-marks": ("wifi-key-marks.png", "wifi", "dark"),
    "wifi-key-ssid": ("wifi-key-ssid.png", "wifi", "dark"),
    "wifi-key-pass": ("wifi-key-pass.png", "wifi", "dark"),
    "wifi-key-shown": ("wifi-key-shown.png", "wifi", "dark"),
    "wifi-key-short": ("wifi-key-short.png", "wifi", "dark"),
    # An uploaded image's question: newer; older with a drawing being
    # kept; the same version rebuilt; the build that runs.
    "wifi-question": ("wifi-question.png", "wifi", "dark"),
    "wifi-older": ("wifi-older.png", "wifi", "dark"),
    "wifi-rebuild": ("wifi-rebuild.png", "wifi", "dark"),
    "wifi-installed": ("wifi-installed.png", "wifi", "dark"),
    # PANEL UPDATE: writing, written, failed.
    "panel-update": ("panel-update.png", "panel-update", "dark"),
    "panel-update-done": ("panel-update-done.png", "panel-update", "dark"),
    "panel-update-failed": ("panel-update-failed.png", "panel-update",
                            "dark"),
    # SETUP > UPDATES: nothing asked yet; a check asking GitHub; a newer
    # release found, with the download's hold; up to date; no answer in
    # time.  The reminder a start-up check opens on OVERVIEW.  The download
    # under way, and stopped by a SHA-256 that differs.
    "updates": ("updates.png", "updates", "dark"),
    "updates-checking": ("updates-checking.png", "updates", "dark"),
    "updates-newer": ("updates-newer.png", "updates", "dark"),
    "updates-current": ("updates-current.png", "updates", "dark"),
    "updates-failed": ("updates-failed.png", "updates", "dark"),
    # RELEASES on TESTING, and the testing release it found.
    "updates-testing": ("updates-testing.png", "updates", "dark"),
    # Its IOmcu section with an IOmcu on another build: the
    # hold that starts the IOmcu's update.
    "updates-coproc": ("updates-coproc.png", "updates", "dark"),
    "upd-remind": ("upd-remind.png", "upd-remind", "dark"),
    "panel-download": ("panel-download.png", "panel-update", "dark"),
    "panel-download-failed": ("panel-download-failed.png", "panel-update",
                              "dark"),
}
INCLUDES = [
    "shared/gfx/include",
    "shared/touch/include",
    "shared/ui/include",
    "shared/bench/include",
    "shared/outputs/include",
    "shared/link/include",
    "shared/can/include",
    "shared/logfile/include",
    "shared/settings/include",
    "shared/servo/include",
    "shared/esc/include",
    "protocols/sbus/include",
    "protocols/pdmini/include",
    "protocols/kst/include",
    "protocols/dshot/include",
]


def build(tmp: pathlib.Path, trace: bool = False) -> pathlib.Path:
    exe = tmp / ("render_trace" if trace else "render_screen")
    # With the touch injector's texts, as the host suite builds: the
    # band-inject view draws INJECT's bubble and --fit measures it.
    cmd = ["cc", "-O2", "-g", *STRICT, "-DRCBENCH_TOUCH_INJECTOR=1",
           "-o", str(exe)]
    if trace:
        cmd += ["-DGFX_TEXT_TRACE"]
    cmd += [str(REPO / s) for s in SOURCES]
    for inc in INCLUDES:
        cmd += ["-I", str(REPO / inc)]
    cmd += ["-lm"]
    subprocess.run(cmd, check=True)
    return exe


def render_one(exe, tmp, name, screen, theme, lang="en", trace=None):
    ppm = tmp / f"{name}-{lang}.ppm"
    # The view name as well as the screen: three log goldens are all the
    # same screen in different states, and the renderer needs to know
    # which state to drive it into.
    cmd = [str(exe), str(ppm), screen, theme, name, lang]
    if trace is not None:
        cmd.append(str(trace))
    subprocess.run(cmd, check=True)
    from PIL import Image
    return Image.open(ppm).convert("RGB")


# ------------------------------------------------------------- the fit check

# A string's cell box carries a little empty space above and below the ink,
# so two lines that touch are not counted as overlapping until they share
# more than this many rows.
SLACK_Y = 2

# A filled shape smaller than this on either side is a mark or a rule, not
# something a label sits inside.
MIN_SHAPE = 8

# Views the fit check draws and no page shows: the band's other bubbles,
# so every string of every bubble is measured where it is drawn, and the
# Wi-Fi screen's failure lines and its install question held by the
# IOmcu's update, and UPDATES joining, refused, without a home
# network and shut by the gate, and the reminder's hold shut.  The same
# shape as SCREENS, with no committed file.
FIT_ONLY = {
    "band-fault-all":   (None, "overview", "dark"),
    # SERVO > PROGRAM with a table of 12 rows: SEARCH, the page keys, two
    # group headings and a row dimmed with its reason.
    "servo-program-many": (None, "servo", "dark"),
    "band-nolink":      (None, "overview", "sim"),
    "band-bench":       (None, "overview", "dark"),
    "band-sim":         (None, "overview", "sim"),
    # The mode chip of a build with RCBENCH_TOUCH_INJECTOR, an injector
    # attached.
    "band-inject":      (None, "overview", "dark"),
    "band-armed-servo": (None, "overview", "dark"),
    "band-armed-all":   (None, "overview", "dark"),
    "band-armed-sim":   (None, "overview", "sim"),
    "band-clock":       (None, "overview", "dark"),
    "band-link-refused-max": (None, "overview", "dark"),
    "wifi-unavailable": (None, "wifi", "dark"),
    "wifi-save-failed": (None, "wifi", "dark"),
    "wifi-coproc":      (None, "wifi", "dark"),
    "updates-joining":  (None, "updates", "dark"),
    "updates-unexpected": (None, "updates", "dark"),
    "updates-nonet":    (None, "updates", "dark"),
    "updates-blocked":  (None, "updates", "dark"),
    # TESTING at GitHub's request limit, and a running version that did
    # not parse.
    "updates-limit":    (None, "updates", "dark"),
    "updates-unversioned": (None, "updates", "dark"),
    # Every other state of UPDATES' IOmcu section.
    "updates-cu-older":    (None, "updates", "dark"),
    "updates-cu-rebuilt":  (None, "updates", "dark"),
    "updates-cu-noimage":  (None, "updates", "dark"),
    "updates-cu-busy":     (None, "updates", "dark"),
    "updates-cu-nolink":   (None, "updates", "dark"),
    "updates-cu-unknown":  (None, "updates", "dark"),
    "updates-cu-checking": (None, "updates", "dark"),
    "updates-cu-failed":   (None, "updates", "dark"),
    "updates-cu-usb":      (None, "updates", "dark"),
    "updates-cu-unsafe":   (None, "updates", "dark"),
    "updates-cu-radio":    (None, "updates", "dark"),
    "updates-cu-install":  (None, "updates", "dark"),
    "upd-remind-blocked": (None, "upd-remind", "dark"),
    # The warning on leaving SETUP with a save refused and the bench ARMED.
    "setup-leave-armed": (None, "setup", "dark"),
    # MOTOR & ESC > SETTINGS ARMED: the pulse rows locked with their reason,
    # a save waiting for a SAFE bench.
    "motor-settings-armed": (None, "motor", "dark"),
    # The bus fault's other two verdicts and its other four controller
    # states, so every meaning and check is measured where it wraps.
    "busfault-lossy":   (None, "busfault", "dark"),
    "busfault-dropped": (None, "busfault", "dark"),
    "busfault-lost-rejoin":  (None, "busfault", "dark"),
    "busfault-lost-stopped": (None, "busfault", "dark"),
    "busfault-lost-running": (None, "busfault", "dark"),
    "busfault-lost-unknown": (None, "busfault", "dark"),
    # BATTERY's two other verdicts, whose reasons wrap in the right card.
    "battery-healthy":  (None, "battery", "dark"),
    "battery-replace":  (None, "battery", "dark"),
    # SUPPLY's MODE card after either trip, its line wrapped in the card.
    "supply-trip-i":    (None, "supply", "dark"),
    "supply-trip-v":    (None, "supply", "dark"),
    # The SERVO PROGRAMMER's notes turned to their second page, where they
    # take more rows than there are above the message.
    "servo-kst-file-locked-p2": (None, "servo", "dark"),
    "servo-kst-journal-p2": (None, "servo", "dark"),
    # The analyser on a frame the receiver flags as lost.
    "analyser-lost":    (None, "analyser", "dark"),
    # The SERVO screen's OUTPUT page with the frame rate refused and with an
    # IOmcu that cannot set it; the HV warning with the output on.
    "servo-rate-refused": (None, "servo", "dark"),
    "servo-rate-unsupported": (None, "servo", "dark"),
    "servo-hv-live":    (None, "servo", "dark"),
    # TEST after a run by movements and after a limit run on PROG PORT:
    # the cards' summaries and LAST lines; the limit run's settings on the
    # port, SEARCH + PROGRAM and SERVO PULSES with their note.
    "servo-test-done":  (None, "servo", "dark"),
    "servo-limrun-card": (None, "servo", "dark"),
    "servo-limrun-settings": (None, "servo", "dark"),
}

# Views that open a drop-down, a chip's help bubble or the alert band over
# the screen: the list, the bubble or the band covers the labels under it by
# design, so a fill drawn over a string is not a finding there.
OVERLAY_VIEWS = {"outputs-protocol", "picker-protocol", "overview-alert"} | {
    name for name in (*SCREENS, *FIT_ONLY) if name.startswith("band-")}

# English overflows the layout has for a time: (view, string, finding), word
# for word as --fit prints them; none today.  Each is a defect of the
# screen, listed here so the check holds every other string; none is a
# decision that the overflow is wanted.  An entry that --fit no longer
# finds fails, so the list cannot outlive its defects.  SETUP shows 65
# cells of a help line.
KNOWN_OVERFLOWS = set()


class Rect:
    def __init__(self, x, y, w, h):
        self.x, self.y, self.w, self.h = x, y, w, h

    @property
    def r(self):
        return self.x + self.w

    @property
    def b(self):
        return self.y + self.h

    def inter(self, o):
        x0, y0 = max(self.x, o.x), max(self.y, o.y)
        x1, y1 = min(self.r, o.r), min(self.b, o.b)
        if x1 <= x0 or y1 <= y0:
            return None
        return Rect(x0, y0, x1 - x0, y1 - y0)

    def covers(self, o):
        return (self.x <= o.x and self.y <= o.y and self.r >= o.r
                and self.b >= o.b)

    def holds(self, px, py):
        return self.x <= px < self.r and self.y <= py < self.b


def read_trace(path: pathlib.Path):
    """The traced frame: its strings and shapes in drawing order, and the
    table IDs looked up while the view was set up and drawn."""
    items, used = [], set()
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("U "):
            used.add(line[2:].strip())
        elif line == "F":
            items = []
        elif line.startswith("B "):
            v = [int(t) for t in line.split()[1:9]]
            items.append(("B", Rect(*v[0:4]), Rect(*v[4:8])))
        elif line.startswith("T "):
            parts = line.split(" ", 13)
            v = [int(t) for t in parts[1:13]]
            box = Rect(*v[8:12]) if v[10] >= 0 else None
            items.append(("T", Rect(*v[0:4]), Rect(*v[4:8]), box,
                          parts[13]))
    return items, used


def ink(t, s):
    """The part of a string's box its characters cover: the box less the
    cells of its leading and trailing spaces, which a padded label carries."""
    n = len(s)
    if n == 0 or not s.strip():
        return None
    cell = t.w / n
    lead = n - len(s.lstrip(" "))
    trail = n - len(s.rstrip(" "))
    x0 = t.x + round(cell * lead)
    x1 = t.r - round(cell * trail)
    return Rect(x0, t.y, x1 - x0, t.h)


def painted_over(items, i, vis, s):
    """The first fill drawn after string i that covers part of its ink, as
    a Rect, unless the same string is drawn again in the same place after
    that fill."""
    t = items[i][1]
    k = ink(t, s)
    if k is None:
        return None
    k = k.inter(vis)
    if k is None or k.h <= 2 * SLACK_Y:
        return None
    core = Rect(k.x, k.y + SLACK_Y, k.w, k.h - 2 * SLACK_Y)
    for j in range(i + 1, len(items)):
        b = items[j]
        if b[0] != "B":
            continue
        hit = core.inter(b[1])
        if hit is None or b[2].inter(hit) is None:
            continue
        redrawn = any(items[m][0] == "T" and items[m][4] == s
                      and items[m][1].x == t.x and items[m][1].y == t.y
                      for m in range(j + 1, len(items)))
        if not redrawn:
            return b[2].inter(hit)
    return None


def fit_problems(items, overlay=False):
    """What overflows, as (string, problem) pairs.  Width only: a language
    changes how long a string is, not how tall.  With @p overlay, a string
    a later fill covers is not a finding."""
    problems = []
    drawn = []          # (index, visible rect, string)
    for i, it in enumerate(items):
        if it[0] != "T":
            continue
        _, t, clip, box, s = it
        if t.y + t.h <= clip.y or t.y >= clip.b:
            continue    # wholly above or below its clip: not drawn at all
        if box is not None and t.w > box.w:
            problems.append((s, "is %d px wide in a %d px box"
                             % (t.w, box.w)))
        elif t.x < clip.x or t.r > clip.r:
            problems.append((s, "is cut at the edge of its area "
                             "(%d to %d, area %d to %d)"
                             % (t.x, t.r, clip.x, clip.r)))
        # The shape it is printed on: the last one drawn under its start.
        px, py = t.x + min(4, max(t.w // 2, 0)), t.y + t.h // 2
        for j in range(i - 1, -1, -1):
            b = items[j]
            if (b[0] == "B" and b[1].w >= MIN_SHAPE and b[1].h >= MIN_SHAPE
                    and b[1].holds(px, py)):
                if t.r > b[1].r and b[1].r <= clip.r:
                    problems.append((s, "runs %d px past the shape it sits "
                                     "on" % (t.r - b[1].r)))
                break
        vis = t.inter(clip)
        if vis is None:
            continue
        over = None if overlay else painted_over(items, i, vis, s)
        if over is not None:
            problems.append((s, "is painted over from x=%d to %d by a fill "
                             "drawn after it" % (over.x, over.r)))
        core = Rect(vis.x, vis.y + SLACK_Y, vis.w, max(vis.h - 2 * SLACK_Y, 0))
        for k, other, os_ in drawn:
            hit = core.inter(other)
            if hit is None:
                continue
            # Painted over between the two is not drawn under the second.
            hidden = any(items[j][0] == "B" and items[j][1].covers(hit)
                         for j in range(k + 1, i))
            if not hidden:
                problems.append((s, "overlaps %r" % os_))
        drawn.append((i, Rect(vis.x, vis.y + SLACK_Y, vis.w,
                              max(vis.h - 2 * SLACK_Y, 0)), s))
    return problems


def judge(findings, known, views):
    """Tag every finding and name the stale waivers.

    @p findings is [(lang, view, string, why)].  A finding fails in either
    language unless @p known holds it word for word: a German finding that
    equals a known English one is the same string in the same place -- a
    number from the data, say -- and is the layout's, not the
    translation's.  Returns ([(tag, lang, view, string, why)], [stale])
    where tag is "FAIL" or "known" and stale is every entry of @p known for
    a view in @p views that English did not produce."""
    english = {(v, s, w) for lang, v, s, w in findings if lang == "en"}
    judged = [("known" if (view, s, why) in known else "FAIL",
               lang, view, s, why) for lang, view, s, why in findings]
    stale = sorted(k for k in known if k[0] in views and k not in english)
    return judged, stale


def fit(views, langs) -> int:
    """Render every view in every language with the trace on and report
    every string that overflows.  An overflow fails in either language,
    except the English ones in KNOWN_OVERFLOWS and their German copies.  A
    table string with no declared width has to be drawn by some view, or
    nothing measures it."""
    failed = False
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = build(tmp, trace=True)
        ids = {}
        out = subprocess.run([str(exe), "--ids"], check=True,
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            _, name, cells = line.split()
            ids[name] = int(cells)
        seen = {lang: set() for lang in langs}
        findings = []
        for lang in sorted(langs, key=lambda x: x != "en"):
            for name in views:
                filename, screen, theme = {**SCREENS, **FIT_ONLY}[name]
                trace = tmp / f"{name}-{lang}.trace"
                render_one(exe, tmp, name, screen, theme, lang, trace)
                items, used = read_trace(trace)
                seen[lang] |= used
                for s, why in fit_problems(items, name in OVERLAY_VIEWS):
                    findings.append((lang, name, s, why))
        judged, stale = judge(findings, KNOWN_OVERFLOWS, set(views))
        for tag, lang, name, s, why in judged:
            print(f"{tag} {lang} {name}: {s!r} {why}")
            failed = failed or tag == "FAIL"
        if "en" in langs:
            for name, s, why in stale:
                print(f"FAIL en {name}: {s!r} is in KNOWN_OVERFLOWS and "
                      f"no longer {why}; take it off the list")
                failed = True
        for lang in langs:
            if lang == "en":
                continue
            unmeasured = sorted(n for n, c in ids.items()
                                if c == 0 and n not in seen[lang])
            for n in unmeasured:
                print(f"FAIL {lang}: {n} has no declared width and no view "
                      "draws it")
                failed = True
    if not failed:
        known = sum(1 for tag, *_ in judged if tag == "known")
        print("every string fits" if not known else
              f"every string fits, except the {known} known overflows above")
    return 1 if failed else 0


# The languages a golden is committed in, and where each set lives.
LANGS = {"en": ".", "de": "de"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("screens", nargs="*",
                    help=f"subset of {', '.join(SCREENS)} (default: all)")
    ap.add_argument("-o", "--output", type=pathlib.Path,
                    help="write a single screen here instead of docs/img/")
    ap.add_argument("--dir", type=pathlib.Path,
                    default=REPO / "docs" / "img")
    ap.add_argument("--lang", choices=sorted(LANGS) + ["all"], default="all",
                    help="the language to render (default: every one; "
                         "German goes to docs/img/de/)")
    ap.add_argument("--check", action="store_true",
                    help="fail if any render differs from the committed image")
    ap.add_argument("--fit", action="store_true",
                    help="fail if a string overflows where it is drawn, in "
                         "either language")
    args = ap.parse_args()

    if not shutil.which("cc"):
        sys.exit("no C compiler on PATH")

    wanted = args.screens or list(SCREENS)
    for name in wanted:
        # A FIT_ONLY view has no committed file: it is measured or written
        # where -o says.
        if name not in SCREENS and not (name in FIT_ONLY
                                         and (args.fit or args.output)):
            sys.exit(f"unknown screen {name!r}; expected one of "
                     f"{', '.join(SCREENS)}")
    langs = list(LANGS) if args.lang == "all" else [args.lang]
    if args.output and (len(wanted) != 1 or len(langs) != 1):
        sys.exit("--output takes exactly one screen and one --lang")

    if args.fit:
        # A subset is the views asked for; the whole run adds FIT_ONLY.
        return fit(wanted if args.screens else wanted + list(FIT_ONLY),
                   langs)

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        sys.exit("Pillow is required: pip install pillow")

    failed = False
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = build(tmp)

        for lang in langs:
            for name in wanted:
                filename, screen, theme = {**SCREENS, **FIT_ONLY}[name]

                out = args.output or (args.dir / LANGS[lang] / filename)
                rendered = render_one(exe, tmp, name, screen, theme, lang)
                shown = f"{LANGS[lang]}/{out.name}".lstrip("./")

                if args.check:
                    # The unit tests cover what a screen decides; the golden
                    # image covers what it looks like.  Compare pixels rather
                    # than encoded bytes: PNG encoders differ between
                    # versions.
                    if not out.exists():
                        print(f"{out} does not exist; run tools/render_ui.py",
                              file=sys.stderr)
                        failed = True
                        continue
                    committed = Image.open(out).convert("RGB")
                    if committed.size != rendered.size:
                        print(f"{out} is {committed.size}, "
                              f"render is {rendered.size}",
                              file=sys.stderr)
                        failed = True
                        continue
                    a = committed.tobytes()
                    b = rendered.tobytes()
                    if a != b:
                        diff = sum(1 for i in range(0, len(a), 3)
                                   if a[i:i + 3] != b[i:i + 3])
                        print(f"{shown} is out of date: {diff:,} pixels "
                              "differ", file=sys.stderr)
                        failed = True
                    else:
                        print(f"{shown} matches")
                else:
                    out.parent.mkdir(parents=True, exist_ok=True)
                    rendered.save(out)
                    print(f"wrote {out}")

    if failed:
        print("\nRun tools/render_ui.py and review the new images.",
              file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
