"""Tests of tools/video_tap.py and of the phase tap storyboard.

SPDX-License-Identifier: MIT
"""

import json
import shutil
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import make_video  # noqa: E402
import video_audio  # noqa: E402
import video_tap  # noqa: E402

FIGURE = REPO / "tools" / "video" / "plots" / "phase-tap.json"
BOARD = REPO / "tools" / "video" / "storyboards" / "phase-tap.json"


def _figure() -> dict:
    return json.loads(FIGURE.read_text(encoding="utf-8"))


def test_a_beep_is_bursts_of_carrier_pulses_at_the_tones_rate():
    """A beep is bursts of carrier pulses at the tone's rate."""
    pulses = video_tap.beep_pulses(0.0, 127.9, 2090.0)
    period, carrier = 1e9 / 2090.0, 1e9 / 12000.0
    assert len(pulses) == 267 * 3
    assert pulses[1][0] - pulses[0][0] == pytest.approx(carrier)
    assert pulses[3][0] - pulses[0][0] == pytest.approx(period)
    assert pulses[0][1] - pulses[0][0] == pytest.approx(carrier / 4)


def test_a_group_sounds_a_long_beep_for_five():
    """A group sounds a long beep for five."""
    beeps, end = video_tap.group(7, 0.0)
    assert [(length, tone) for _, length, tone in beeps] == [
        video_tap.LONG, video_tap.SHORT, video_tap.SHORT]
    assert end == pytest.approx((383.7 + 148 + 127.9 + 148 + 127.9) * 1e6)


def test_crossings_gives_each_edge_its_time_and_level():
    """Crossings gives each edge its time and level."""
    trace = [(0.0, 0.0), (10.0, 2.0), (20.0, 2.0), (30.0, 0.0)]
    assert video_tap.crossings(trace, 1.0) == [(5.0, 1), (25.0, 0)]


def test_beeps_closer_than_the_group_gap_count_as_one_group():
    """Beeps closer than the group gap count as one group."""
    tick = video_tap.TICK_HZ / 1000
    beeps = [{"start": int(a * tick), "end": int(b * tick)}
             for a, b in ((0, 128), (276, 404), (1200, 1584), (1732, 1860))]
    assert [g[0] for g in video_tap.counted(beeps)] == [2, 6]


def test_the_figure_holds_what_the_detector_reported():
    """The figure holds what the detector reported."""
    drawn = _figure()
    assert video_tap.faults(drawn) == []
    assert drawn["model"] == video_tap.MODEL
    assert drawn["detector"]["kept"] == drawn["detector"]["taken"]
    drawn["detector"]["beeps"].pop()
    assert video_tap.faults(drawn)


@pytest.mark.skipif(shutil.which("cc") is None,
                    reason="no C compiler is installed")
def test_the_detector_reports_the_beeps_of_a_group():
    """The detector reports the beeps of a group."""
    beeps, end = video_tap.group(6, 20e6)
    pulses = [p for at, length, tone in beeps
              for p in video_tap.beep_pulses(at, length, tone)]
    edges = sorted([(rise, 1) for rise, _ in pulses]
                   + [(fall, 0) for _, fall in pulses])
    found = video_tap.detect(edges, end + 1e9)
    assert [round(b["hz"]) for b in found["beeps"]] == [1866, 2090]
    assert [g[0] for g in video_tap.counted(found["beeps"])] == [6]
    assert round(found["beeps"][0]["carrier_hz"], -2) == 12000


def test_the_phase_tap_storyboard_passes_the_check():
    """The phase tap storyboard passes the check."""
    board = json.loads(BOARD.read_text(encoding="utf-8"))
    assert make_video.check(board) == []


def test_the_cello_bed_holds_41_bars_of_sixteenths_and_is_a_known_bed():
    """The cello bed holds 41 bars of sixteenths and is a known bed."""
    assert len(video_audio.CELLO) == 41
    assert all(len(row) == 16 and row[0] for row in video_audio.CELLO)
    assert video_audio.CELLO[0][:8] == (43, 50, 59, 57, 59, 50, 59, 50)
    assert "bach-cello" in video_audio.BEDS
    assert video_audio.BEDS == make_video.BEDS
