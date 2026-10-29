"""frame_cost.py: the profile, the docs and the measurement's repeatability.

The ESP32-P4 profile, the block in the docs and the table of the board run
without cachegrind; the last cases build the bench and run it under
cachegrind, and need cc and valgrind.

SPDX-License-Identifier: MIT
"""

import shutil

import pytest

import frame_cost as fc
from conftest import skip_or_fail


def test_the_l2_defaults_are_esp_idfs():
    """The l2 defaults are esp idfs."""
    assert fc.l2_cache("") == (131072, 64)


def test_the_l2_follows_what_sdkconfig_defaults_sets():
    """The l2 follows what sdkconfig defaults sets."""
    text = "CONFIG_CACHE_L2_CACHE_256KB=y\nCONFIG_CACHE_L2_CACHE_LINE_128B=y\n"
    assert fc.l2_cache(text) == (262144, 128)
    assert fc.l2_cache("# CONFIG_CACHE_L2_CACHE_256KB=y\n") == (131072, 64)


def test_the_panel_rate_is_the_boards_dpi_timing():
    """The panel rate is the boards dpi timing."""
    text = fc.BOARD_PINS_H.read_text(encoding="utf-8")
    # 52MHz over (1024 + 10 + 160 + 160) x (600 + 1 + 23 + 12).
    assert fc.panel_hz(text, fc.CANVAS) == pytest.approx(
        52e6 / (1354 * 636), rel=1e-9)
    assert 60.3 < fc.panel_hz(text, fc.CANVAS) < 60.5


def test_a_missing_timing_define_stops_the_tool():
    """A missing timing define stops the tool."""
    with pytest.raises(SystemExit):
        fc.panel_hz("#define BOARD_LCD_DPI_CLOCK_MHZ 52.0f\n", fc.CANVAS)


def test_the_profile_is_the_canvas_the_renderer_draws():
    """The profile is the canvas the renderer draws."""
    assert fc.panel_size() == fc.CANVAS


def test_the_bandwidth_is_not_measured_until_the_probe_is():
    """The bandwidth is not measured until the probe is."""
    assert fc.effective_bw(None, fc.CANVAS) is None
    # One clear of 1,228,800B in 24.576ms: 2 x 1,228,800B of traffic.
    assert fc.effective_bw(24576.0, fc.CANVAS) == pytest.approx(100e6)


def test_the_table_without_a_bandwidth_has_no_time_columns():
    """The table without a bandwidth has no time columns."""
    lines = fc.table([("frame", 1000), ("hlines", 0)], 64, 60.38, None)
    assert lines[0].startswith("panel 60.4 Hz, 64-byte lines; effective "
                               "bandwidth not measured")
    assert "est. ms" not in lines[1]
    assert lines[-1].endswith("est. ms and est. fps: not measured")
    assert fc.doc_rows("\n".join(lines)) == {"frame": 1000, "hlines": 0}


def test_the_table_with_a_bandwidth_estimates_each_mode():
    """The table with a bandwidth estimates each mode."""
    lines = fc.table([("clear", 19200)], 64, 60.0, 100e6)
    assert "~100 MB/s effective" in lines[0]
    row = lines[-1].split()
    # 19,200 x 128B = 2,457,600B at 100MB/s: 24.6ms, two panel frames.
    assert row[0] == "clear"
    assert float(row[-2]) == pytest.approx(24.6)
    assert float(row[-1]) == pytest.approx(30.0)


def block(lines):
    """The block a page would carry for @p lines."""
    return fc.doc_block(lines)


GOOD = fc.table([("frame", 10000), ("overview", 900)], 64, 60.38, None)


def test_a_block_that_matches_passes():
    """A block that matches passes."""
    assert fc.block_problems("P.md", block(GOOD), block(GOOD)) == []


def test_fills_within_the_tolerance_pass_and_past_it_fail():
    """Fills within the tolerance pass and past it fail.

    The tolerance absorbs a change of layout elsewhere in the sources, not a
    run that differs: test_a_mode_measures_the_same_twice_* holds that.
    """
    near = fc.table([("frame", 10150), ("overview", 990)], 64, 60.38, None)
    assert fc.block_problems("P.md", block(near), block(GOOD)) == []
    far = fc.table([("frame", 10300), ("overview", 900)], 64, 60.38, None)
    assert fc.block_problems("P.md", block(far), block(GOOD)) == [
        "P.md says frame costs 10,300 fills; it costs 10,000"]


def test_a_missing_mode_fails():
    """A missing mode fails."""
    short = fc.table([("frame", 10000)], 64, 60.38, None)
    assert fc.block_problems("P.md", block(short), block(GOOD)) == [
        "P.md has no row for mode 'overview'"]


def test_a_page_that_claims_a_bandwidth_the_profile_lacks_fails():
    """A page that claims a bandwidth the profile lacks fails."""
    claims = fc.table([("frame", 10000), ("overview", 900)], 64, 60.38,
                      100e6)
    out = fc.block_problems("P.md", block(claims), block(GOOD))
    assert len(out) == 1 and "the profile gives" in out[0]


EN_HEADER, EN_NOT = fc.BENCH_PAGES["Performance.md"]
DE_HEADER, DE_NOT = fc.BENCH_PAGES["Performance-de.md"]


def bench(header, rows):
    """A page holding the table of the board with @p rows under @p header."""
    sep = "| --- | --- | ---: | ---: | ---: | ---: | ---: |"
    return "\n".join([header, sep, *rows]) + "\n\nAfter.\n"


NOT = " | ".join(["not measured"] * 5)


def test_a_table_of_the_board_not_measured_passes_while_the_probe_is_not():
    """A table of the board not measured passes while the probe is not."""
    text = bench(EN_HEADER, [f"| `clear` | the probe | {NOT} |",
                             f"| `overview` | the menu | {NOT} |"])
    rows = fc.bench_rows(text, EN_HEADER, EN_NOT)
    assert rows["clear"] == [None] * 5
    assert fc.bench_problems("P.md", rows, None) == []


def test_a_measured_probe_row_and_the_profile_agree_or_fail():
    """A measured probe row and the profile agree or fail."""
    row = ("| `clear` | the probe | not measured | 24.58 | not measured "
           "| not measured | not measured |")
    rows = fc.bench_rows(bench(EN_HEADER, [row]), EN_HEADER, EN_NOT)
    assert fc.bench_problems("P.md", rows, 24576.0) == []
    assert fc.bench_problems("P.md", rows, None) == [
        "P.md: the clear row gives 24.58 ms; PROBE_CLEAR_US is not set"]
    assert "is 25.00 ms" in fc.bench_problems("P.md", rows, 25000.0)[0]


def test_a_probe_in_the_profile_needs_its_row_measured():
    """A probe in the profile needs its row measured."""
    page = bench(EN_HEADER, [f"| `clear` | the probe | {NOT} |"])
    rows = fc.bench_rows(page, EN_HEADER, EN_NOT)
    assert fc.bench_problems("P.md", rows, 24576.0) == [
        "P.md: the clear row is not measured; PROBE_CLEAR_US is 24576.0 us"]


def test_the_german_table_reads_a_decimal_comma():
    """The german table reads a decimal comma."""
    row = ("| `servo` | SERVO | 3,8 | 4,1 | 7,2 | 11,6 | 60,4 |")
    rows = fc.bench_rows(bench(DE_HEADER, [row]), DE_HEADER, DE_NOT)
    assert rows["servo"] == [3.8, 4.1, 7.2, 11.6, 60.4]


def test_a_row_of_no_mode_or_an_unknown_mode_fails():
    """A row of no mode or an unknown mode fails."""
    rows = fc.bench_rows(bench(EN_HEADER, [f"| `clear` | p | {NOT} |",
                                           f"| `menu` | m | {NOT} |"]),
                         EN_HEADER, EN_NOT)
    assert fc.bench_problems("P.md", rows, None) == [
        "P.md: the table of the board names mode 'menu', which "
        "frame_cost.py does not draw"]
    with pytest.raises(SystemExit):
        fc.bench_rows(bench(EN_HEADER, [f"| clear | p | {NOT} |"]),
                      EN_HEADER, EN_NOT)
    with pytest.raises(SystemExit):
        fc.bench_rows(bench(EN_HEADER, ["| `clear` | p | soon | 1 | 2 | 3 "
                                        "| 4 |"]), EN_HEADER, EN_NOT)


def test_a_table_without_the_probe_row_or_none_at_all_fails():
    """A table without the probe row or none at all fails."""
    rows = fc.bench_rows(bench(EN_HEADER, [f"| `servo` | s | {NOT} |"]),
                         EN_HEADER, EN_NOT)
    assert "no row for 'clear'" in fc.bench_problems("P.md", rows, None)[0]
    assert fc.bench_problems("P.md", None, None) == [
        "P.md: no table of DRAW and WAIT on the board"]


def test_both_pages_carry_the_block_and_the_table_of_the_board():
    """Both pages carry the block and the table of the board."""
    for doc in fc.DOCS:
        text = doc.read_text(encoding="utf-8")
        assert fc.DOC_START in text and fc.DOC_END in text, doc.name
        header, unmeasured = fc.BENCH_PAGES[doc.name]
        rows = fc.bench_rows(text, header, unmeasured)
        assert fc.bench_problems(doc.name, rows, fc.PROBE_CLEAR_US) == []


def test_the_cachegrind_run_gets_one_fixed_environment(monkeypatch, tmp_path):
    """The environment of the run is the same on every machine.

    Its length shifts the stack, and with it the count of single modes.
    """
    seen = {}

    def fake_run(cmd, **kw):
        seen["env"] = kw.get("env")
        seen["cmd"] = cmd

        class Done:
            stderr = "LLd misses: 1,234 ( 1 rd + 2 wr)"
        return Done()

    monkeypatch.setenv("RUNNER_JUNK", "x" * 4000)
    monkeypatch.setattr(fc.subprocess, "run", fake_run)
    monkeypatch.setattr(fc, "MISS_RE",
                        fc.re.compile(r"LLd misses:\s+([\d,]+)"))
    exe = tmp_path / "bench_frame"
    assert fc.misses(exe, 3, "held", "131072,8,64") == 1234
    assert seen["env"] == {"LC_ALL": "C"}
    assert seen["cmd"][-3:] == ["./bench_frame", "003", "held"]


def test_the_1_frame_and_the_11_frame_run_have_one_argv_length(
        monkeypatch, tmp_path):
    """The start-up cancels only while both runs lay out the stack alike.

    argv's strings sit above the stack, so a frame count of one digit and
    one of two would start the two runs from stacks a byte apart.
    """
    cmds = []

    def fake_run(cmd, **kw):
        cmds.append(cmd)

        class Done:
            stderr = "LLd misses: 7 ( 1 rd + 6 wr)"
        return Done()

    monkeypatch.setattr(fc.subprocess, "run", fake_run)
    for frames in (1, 11):
        fc.misses(tmp_path / "bench_frame", frames, "frame", "131072,8,64")
    assert cmds[0][-2] == "001" and cmds[1][-2] == "011"


def test_every_cache_of_the_model_is_given_and_none_is_the_hosts(
        monkeypatch, tmp_path):
    """Every cache is given; none is taken from the host's CPU.

    cachegrind takes a cache it is not given from the host's CPU.  The L1
    instruction cache's misses fill the L2 beside the data's, so a
    runner with another instruction cache measures other fills: --I1, --D1
    and --LL are all on the command line, the ESP32-P4's geometry.
    """
    seen = {}

    def fake_run(cmd, **kw):
        seen["cmd"] = cmd

        class Done:
            stderr = "LLd misses: 7 ( 1 rd + 6 wr)"
        return Done()

    monkeypatch.setattr(fc.subprocess, "run", fake_run)
    fc.misses(tmp_path / "bench_frame", 1, "servo", "131072,8,64")
    given = [a for a in seen["cmd"] if a.startswith(("--I1=", "--D1=",
                                                      "--LL="))]
    # 16KiB, 4-way and 64KiB, 2-way, 64-byte lines: esp32p4/rom/cache.h.
    assert given == ["--I1=16384,4,64", "--D1=65536,2,64",
                     "--LL=131072,8,64"]


# The modes the repeatability cases measure: the servo card's three, the
# motor bench's steady frame and a full repaint.
REPEAT_MODES = ("servo", "servo-grip", "servo-current", "frame",
                "logs-chrome")


@pytest.fixture(scope="module")
def built(tmp_path_factory):
    """The bench built once, for the cases that run it under cachegrind."""
    for tool in ("cc", "valgrind"):
        if not shutil.which(tool):
            skip_or_fail(f"{tool} is not on PATH")
    return fc.build(tmp_path_factory.mktemp("bench"))


def ten_frames(exe, mode):
    """The fills of ten frames of @p mode.

    The tool's figure before it is divided, so a difference of one fill
    shows.
    """
    ll = "131072,8,64"
    return fc.misses(exe, 11, mode, ll) - fc.misses(exe, 1, mode, ll)


@pytest.mark.parametrize("mode", REPEAT_MODES)
def test_a_mode_measures_the_same_twice_from_any_directory(built, tmp_path,
                                                            mode):
    """The same bench measured twice gives the same fills.

    The second run starts from a directory whose path is 120 characters
    longer, which moves where the kernel and valgrind put the stack: the
    count holds only while the bench draws from a stack at a fixed phase.
    """
    deep = tmp_path / ("d" * 120)
    deep.mkdir()
    exe = deep / built.name
    shutil.copy2(built, exe)
    assert ten_frames(built, mode) == ten_frames(exe, mode)


def test_a_full_repaint_measures_the_same_from_paths_of_any_length(
        built, tmp_path_factory):
    """The same bench run from paths of 96 to 111 characters measures the same.

    The start-up's misses move with the length of the executable's path, and
    cancel only while the 1-frame and the 11-frame run lay out their stacks
    alike.  With a frame count of one digit and one of two, lengths 99 and
    111 moved ten frames by one and two fills on x86-64 with glibc 2.39;
    sixteen lengths in a row hold every offset within the stack's 16-byte
    alignment.
    """
    root = tmp_path_factory.mktemp("p")
    counts = set()
    for length in range(96, 112):
        pad = length - len(str(root / built.name)) - 1
        if pad < 1:
            continue
        where = root / ("p" * pad)
        where.mkdir()
        exe = where / built.name
        assert len(str(exe)) == length
        shutil.copy2(built, exe)
        counts.add(ten_frames(exe, "logs-chrome"))
    assert len(counts) == 1, counts
