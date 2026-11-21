"""pio_budget.py: the reading of .pio files, the chip model and the boards.

SPDX-License-Identifier: MIT
"""

import dataclasses
import json
import os
import shutil
import subprocess

import pytest
from conftest import REPO, skip_or_fail

import pio_budget as pb

pytestmark = pytest.mark.usefixtures("no_hang")

PIOASM = os.environ.get("PIOASM") or shutil.which("pioasm")

# Every rule of the reader in one file: comments of all three kinds, a code
# block whose lines look like instructions, labels with an instruction
# after them, `.word`, defines, and a `.lang_opt` whose value holds a `;`.
SAMPLE = """\
; a comment
.define public BASE 4
.program first
.side_set 1 opt
.lang_opt python foo = "a;b"
.origin BASE
    nop             ; one
public top: nop     // two
.wrap_target
*inner: jmp top     /* three */
    /* a comment
       across lines */
    .word 0xa042
.wrap
    nop
% c-sdk {
    nop
    jmp top
%}

.program second
.wrap_target
    set x, 1
    jmp x-- 0
"""


def test_sample_is_read_as_pioasm_counts_it():
    """Labels, comments, code blocks and `.word` are counted as pioasm does.

    The first program has 5 words at origin 4, wrap target 2 (the word
    after `.wrap_target`) and wrap 3 (the word before `.wrap`).  The second
    has no `.wrap`, so it wraps at its last word.
    """
    first, second = pb.parse_pio(SAMPLE, "sample.pio")
    assert (first.name, first.length, first.origin) == ("first", 5, 4)
    assert (first.wrap_target, first.wrap) == (2, 3)
    assert (second.name, second.length, second.origin) == ("second", 2, -1)
    assert (second.wrap_target, second.wrap) == (0, 1)
    assert first.source == "sample.pio"


@pytest.mark.parametrize("origin, want", [
    ("0", 0), ("0x1f", 31), ("0b11", 3), ("(7)", 7), ("N", 9)])
def test_origin_forms(origin, want):
    """`.origin` takes a decimal, hex or binary literal and a defined name."""
    text = f".define N 9\n.program p\n.origin {origin}\nnop\n"
    assert pb.parse_pio(text)[0].origin == want


def test_32_words_fit_and_33_do_not():
    """The limit pioasm holds a program to: 32 words."""
    assert pb.parse_pio(".program p\n" + "nop\n" * 32)[0].length == 32
    with pytest.raises(pb.BudgetError, match="over 32 words"):
        pb.parse_pio(".program p\n" + "nop\n" * 33)


@pytest.mark.parametrize("text, message", [
    (".program p\nnop\n.origin 0\n", "origin after an instruction"),
    (".program p\n.origin 30\nnop\nnop\nnop\n", "leaves no room"),
    (".program p\n.origin X\nnop\n", "cannot read the value"),
    (".program p\n.wrap\nnop\n", "before the first instruction"),
    (".program p\nnop\n.wrap\n.wrap\n", "a second .wrap"),
    (".program p\n.wrap_target\n.wrap_target\nnop\n", "second .wrap_target"),
    (".program p\nnop\n.wrap_target\n", "after the last instruction"),
    (".program p\n.nonsense\nnop\n", "unknown directive"),
    ("nop\n", "before .program"),
    (".side_set 1\n", "before .program"),
    (".program p\n", "has no instruction"),
    (".program p\n/* open\nnop\n", "not closed"),
    (".program p\nnop\n% c-sdk {\nint x;\n", "not closed"),
])
def test_what_pioasm_refuses_is_refused(text, message):
    """Each placement pioasm refuses is an error, as is a value not read."""
    with pytest.raises(pb.BudgetError, match=message):
        pb.parse_pio(text)


def test_the_held_programs_have_the_fixture_lengths():
    """kst_line, hitec_line, ew_line and tone_cap against their word lists.

    The firmware build holds pioasm's words to these lists
    (firmware/iomcu/cmake/pio_words.cmake), so the counts match without
    pioasm on the machine.
    """
    progs = pb.load_programs(REPO)
    for name in ("kst_line", "hitec_line", "ew_line", "tone_cap"):
        words = (REPO / "test/host/fixtures" / f"{name}_words.txt")
        lines = [w for w in words.read_text().split() if w.strip()]
        assert progs[name].length == len(lines), name


def _pioasm_json(path):
    """The programs pioasm assembles from @p path, by name."""
    out = subprocess.run([PIOASM, "-o", "json", str(path)], check=True,
                         capture_output=True, text=True).stdout
    return {p["name"]: p for p in json.loads(out)["programs"]}


def test_every_pio_file_agrees_with_pioasm(tmp_path):
    """Name, length, origin and wrap of every program against pioasm 2.3.0.

    Every .pio file of the tree, and SAMPLE.
    """
    if PIOASM is None:
        skip_or_fail("no pioasm (set PIOASM)")
    sample = tmp_path / "sample.pio"
    sample.write_text(SAMPLE)
    files = sorted(REPO.glob("**/*.pio"))
    files = [f for f in files if ".git" not in f.parts] + [sample]
    assert len(files) > 8
    for path in files:
        want = _pioasm_json(path)
        got = pb.parse_pio(path.read_text(encoding="utf-8"), str(path))
        assert [p.name for p in got] == list(want), path
        for p in got:
            w = want[p.name]
            assert (p.length, p.origin, p.wrap_target, p.wrap) == (
                len(w["instructions"]), w["origin"], w["wrapTarget"],
                w["wrap"]), (path, p.name)


def test_the_precharge_sampler_is_its_most(tmp_path):
    """hitec_pre is HITEC_PRE_PROGRAM_MAX words; a tree without it fails."""
    progs = pb.load_programs(REPO)
    assert progs["hitec_pre"].length == 12
    (tmp_path / "protocols/hitec/include").mkdir(parents=True)
    (tmp_path / "protocols/hitec/include/hitec_pre.h").write_text("")
    with pytest.raises(pb.BudgetError, match="HITEC_PRE_PROGRAM_MAX"):
        pb.load_programs(tmp_path)


def test_a_name_in_two_files_fails(tmp_path):
    """Two .pio files that define one name: the model cannot tell them."""
    for mod in ("a", "b"):
        d = tmp_path / "protocols" / mod / "rp2350"
        d.mkdir(parents=True)
        (d / f"{mod}.pio").write_text(".program same\nnop\n")
    with pytest.raises(pb.BudgetError, match="also in"):
        pb.load_programs(tmp_path)


# -------------------------------------------------------------------- model

def prog(name, words, origin=-1):
    """A program of @p words words."""
    return pb.Program(name, words, origin, 0, words - 1, "test")


def use(label, *names, dma=0):
    """A use that loads each of @p names through the SDK's search."""
    return pb.Use(label, tuple(("sm", n) for n in names), dma)


def board(*mixes, boot=()):
    """A board on GPIO0 to GPIO29 with @p mixes."""
    return pb.Board(9, "test", (0, 29), boot=boot, mixes=mixes)


def test_the_sdk_order_pio2_first_highest_offset():
    """The first program goes to PIO2, state machine 0, at the top."""
    chip = pb.apply(pb.Chip(), use("u", "a"), {"a": prog("a", 4)})
    assert chip.used == [0, 0, 0xF0000000]
    assert chip.sms[2] == ["a", None, None, None]


def test_origin_is_honoured_and_a_clash_moves_to_the_next_block():
    """A fixed origin takes its range or the next block's."""
    progs = {"o": prog("o", 2, origin=0)}
    chip = pb.apply(pb.Chip(), use("u", "o", "o"), progs)
    assert chip.used == [0, 0b11, 0b11]


def test_a_configuration_that_fits():
    """3 programs of 20 words and 3 of 12 fill all 96 words."""
    progs = {"big": prog("big", 20), "small": prog("small", 12)}
    mix = pb.Mix("fits", (use("b", "big"),) * 3 + (use("s", "small"),) * 3,
                 True, "test")
    out = pb.run(board(mix), mix.uses, progs)
    assert out.fits
    assert [out.chip.words(b) for b in range(3)] == [32, 32, 32]


@pytest.mark.parametrize("uses, refused", [
    # Words: a fourth 20-word program finds no block with 20 free.
    ((use("b", "big"),) * 4, "b"),
    # State machines: the 13th 1-word program.
    ((use("one", "w1"),) * 13, "one"),
    # DMA: 17 channels.
    ((use("d", dma=8),) * 2 + (use("x", dma=1),), "x"),
])
def test_a_configuration_that_overflows(uses, refused):
    """Words, state machines and DMA channels each run out."""
    progs = {"big": prog("big", 20), "w1": prog("w1", 1)}
    out = pb.run(board(), uses, progs)
    assert out.refused == [refused]


def test_fragmented_words_do_not_fit():
    """24 free words in two holes do not hold a 24-word program."""
    chip = pb.Chip()
    for b in range(3):
        chip.used[b] = 0x0000FF00   # 8 taken in the middle of each block
        chip.sms[b][0] = "x"
    assert pb.apply(chip, use("k", "k"), {"k": prog("k", 24)}) is None
    assert pb.apply(chip, use("k", "k"), {"k": prog("k", 16)}) is not None


def test_a_pair_needs_adjacent_state_machines():
    """State machines 0 and 2 taken leave 1 and 3: not adjacent."""
    chip = pb.Chip()
    for b in range(3):
        chip.sms[b][0] = chip.sms[b][2] = "x"
    progs = {"tx": prog("tx", 11), "rx": prog("rx", 5)}
    pair = pb.Use("bidir", (("pair", "tx", "rx"),))
    assert pb.apply(chip, pair, progs) is None
    chip.sms[1][2] = None          # 1, 2 and 3 of PIO1 now free
    nxt = pb.apply(chip, pair, progs)
    assert nxt.sms[1] == ["x", "tx", "rx", None]


def test_a_pair_wraps_from_3_to_0():
    """The pair (3, 0) is adjacent modulo 4."""
    chip = pb.Chip()
    for b in range(3):
        chip.sms[b][1] = chip.sms[b][2] = "x"
    progs = {"tx": prog("tx", 11), "rx": prog("rx", 5)}
    nxt = pb.apply(chip, pb.Use("bidir", (("pair", "tx", "rx"),)), progs)
    assert nxt.sms[2] == ["rx", "x", "x", "tx"]


def test_a_refused_use_leaves_the_chip_as_it_was():
    """The Hitec line is let go when its sampler does not fit."""
    progs = {"line": prog("line", 30), "pre": prog("pre", 12)}
    chip = pb.Chip()
    for b in range(3):
        chip.used[b] = (1 << 22) - 1   # 10 free words in each block
    assert pb.apply(chip, use("h", "line", "pre", dma=3), progs) is None
    assert chip.used == [(1 << 22) - 1] * 3


def test_fixed_goes_to_its_block_or_nowhere():
    """A program placed in PIO0 does not move to a free block."""
    progs = {"f": prog("f", 20)}
    fixed = pb.Use("f", (("fixed", "f", 0),))
    chip = pb.apply(pb.Chip(), fixed, progs)
    assert [chip.words(b) for b in range(3)] == [20, 0, 0]
    assert pb.apply(chip, fixed, progs) is None


def test_an_unknown_program_fails():
    """A use naming no known program is a table error."""
    with pytest.raises(pb.BudgetError, match="no program"):
        pb.apply(pb.Chip(), use("u", "nothing"), {})


@pytest.mark.parametrize("gpio, base", [
    ((0, 29), 0), ((0, 31), 0), ((16, 35), 16), ((16, 47), 16)])
def test_the_window(gpio, base):
    """Pins below GPIO32 run at base 0, pins in GPIO16 to GPIO47 at 16."""
    assert pb.Board(9, "t", gpio).base() == base


@pytest.mark.parametrize("gpio", [(10, 40), (16, 48), (0, 47)])
def test_pins_in_no_window_fail(gpio):
    """One block cannot reach a pin below 16 and one above 31."""
    with pytest.raises(pb.BudgetError, match="no 32-pin PIO window"):
        pb.Board(9, "t", gpio).base()


# ------------------------------------------------------------------- boards

@pytest.fixture(name="progs", scope="module")
def fixture_progs():
    """The programs of the tree."""
    return pb.load_programs(REPO)


@pytest.mark.parametrize("brd", pb.BOARDS, ids=lambda b: str(b.number))
def test_every_board_passes_with_the_real_sources(brd, progs):
    """Boards 1 and 2 with today's .pio files, and board 3 as planned."""
    lines, failures = pb.report(brd, progs, pb.ppm_events(REPO))
    assert failures == []
    assert lines[0].startswith(f"Board {brd.number},")


def test_main_passes_on_the_tree(capsys):
    """The tool exits 0 and prints the three boards."""
    assert pb.main([]) == 0
    out = capsys.readouterr().out
    assert all(f"Board {n}," in out for n in (1, 2, 3))
    assert pb.main(["--check"]) == 0
    assert capsys.readouterr().out == ""


def test_today_s_lengths(progs):
    """The lengths docs/Link.md quotes, and the rest of today's programs."""
    got = {n: progs[n].length for n in (
        "dshot_tx", "dshot_bidir_tx", "dshot_rx", "ppm_tx", "pd_uart_tx",
        "pd_uart_rx", "tone_cap", "kst_line", "ew_line", "hitec_line")}
    assert got == {"dshot_tx": 4, "dshot_bidir_tx": 11, "dshot_rx": 5,
                   "ppm_tx": 4, "pd_uart_tx": 4, "pd_uart_rx": 8,
                   "tone_cap": 22, "kst_line": 24, "ew_line": 25,
                   "hitec_line": 30}


def test_board_3_boot_fills_pio0(progs):
    """PIO0: MIRROR 1, CAPTURE 6, UART 4 and 8: 19 words, 4 machines."""
    bp = pb.board_programs(pb.BOARD_RSN, progs)
    out = pb.run(pb.BOARD_RSN, (), bp)
    assert out.fits
    assert (out.chip.words(0), out.chip.sms_used(0)) == (19, 4)
    assert out.chip.dma == 3


def test_a_planned_program_in_a_file_is_taken_from_the_file(progs):
    """A .pio that defines pass_capture replaces the planned 6 words."""
    real = dict(progs, pass_capture=prog("pass_capture", 9))
    assert pb.board_programs(pb.BOARD_RSN, real)["pass_capture"].length == 9
    assert pb.board_programs(pb.BOARD_RSN, progs)["pass_capture"].length == 6


def test_a_table_that_allows_what_does_not_fit_fails(progs, capsys,
                                                     monkeypatch):
    """The tool exits 1 when a mix the table allows is refused."""
    bad = dataclasses.replace(pb.BOARD_RBMOD, mixes=(pb.Mix(
        "tap beside 4 bidirectional", pb._x(pb.BIDIR, 4)
        + (pb.PD_UART, pb.TAP), True, "test"),))
    _, failures = pb.report(bad, progs, 0)
    assert len(failures) == 1 and "the table says fits" in failures[0]
    monkeypatch.setattr(pb, "BOARDS", (bad,))
    assert pb.main(["--check"]) == 1
    assert "tap beside 4 bidirectional" in capsys.readouterr().out


def test_a_table_that_refuses_what_fits_fails(progs):
    """A refusal the chip does not need is a table error as well."""
    bad = dataclasses.replace(pb.BOARD_RBMOD, mixes=(pb.Mix(
        "one plain", (pb.DSHOT,), False, "test"),))
    _, failures = pb.report(bad, progs, 0)
    assert len(failures) == 1 and "the table says refused" in failures[0]


@pytest.mark.parametrize("session, singles, pairs, ppm, allowed", [
    (None, 8, 0, 0, True), (None, 9, 0, 0, False), (None, 0, 4, 0, True),
    (None, 2, 3, 0, True), (None, 3, 3, 0, False), (None, 6, 0, 6, True),
    (None, 7, 0, 7, False),
    (pb.KST, 4, 0, 0, True), (pb.KST, 5, 0, 0, False),
    (pb.KST, 0, 2, 0, True), (pb.KST, 2, 1, 0, True),
    (pb.KST, 3, 1, 0, False), (pb.ESCWIRE, 4, 0, 4, True),
    (pb.HITEC, 0, 3, 0, False),
    (pb.HITEC_PRE, 3, 0, 0, True), (pb.HITEC_PRE, 1, 1, 0, True),
    (pb.HITEC_PRE, 4, 0, 0, False), (pb.HITEC_PRE, 0, 2, 0, False),
    (pb.HITEC_PRE, 2, 1, 0, False),
])
def test_board_3_table(session, singles, pairs, ppm, allowed):
    """8 plain or 4 bidirectional slots; a session leaves one block.

    The Hitec D precharge sampler takes one state machine of that block.
    """
    assert pb.rsn_allows(session, singles, pairs, ppm) is allowed


def test_the_sweep_finds_a_looser_table(progs, monkeypatch):
    """A table that gives a Hitec D session a whole block fails the sweep.

    The precharge sampler takes one of that block's state machines, so 4
    plain DShot slots and the session do not fit.
    """
    bp = pb.board_programs(pb.BOARD_RSN, progs)
    assert pb.sweep(pb.BOARD_RSN, bp, 0).failures == []

    def loose(session, singles, pairs, ppm):
        if session is pb.HITEC_PRE:
            session = pb.HITEC
        return real(session, singles, pairs, ppm)

    real = pb.rsn_allows
    monkeypatch.setattr(pb, "rsn_allows", loose)
    failures = pb.sweep(pb.BOARD_RSN, bp, 0).failures
    assert failures
    assert any(f.startswith("4 plain, 0 bidirectional, 0 PPM")
               and "Hitec D" in f for f in failures)


def test_the_sweep_finds_the_dma_limit(progs, monkeypatch):
    """7 PPM slots need 17 DMA channels with the 3 of the boot."""
    bp = pb.board_programs(pb.BOARD_RSN, progs)
    monkeypatch.setattr(pb, "RSN_PPM_MAX", 7)
    failures = pb.sweep(pb.BOARD_RSN, bp, 0).failures
    assert failures and all(", 7 PPM" in f for f in failures)


def test_ppm_must_place_as_plain_dshot(progs):
    """The sweep stops when ppm_tx no longer has dshot_tx's length."""
    bp = dict(pb.board_programs(pb.BOARD_RSN, progs),
              ppm_tx=prog("ppm_tx", 5))
    with pytest.raises(pb.BudgetError, match="ppm_tx and dshot_tx differ"):
        pb.sweep(pb.BOARD_RSN, bp, 0)


def test_capture_figures():
    """20,160/s and 406ms; the worst case 227,680/s and 36ms; 195,047/s."""
    assert pb.capture_rate(0, 0, 0, 0) == 20160
    assert round(pb.ring_ms(20160)) == 406
    assert pb.capture_rate(0, 4, 0, 0) == 227680
    assert round(pb.ring_ms(227680)) == 36
    assert pb.RING_EVENTS * 1000 // pb.FLASH_WINDOW_MS == 195047
    assert pb.ring_ms(0) == float("inf")


def test_the_sweep_reports_the_worst_capture(progs):
    """Only 4 bidirectional DShot outputs can lap the ring."""
    bp = pb.board_programs(pb.BOARD_RSN, progs)
    sw = pb.sweep(pb.BOARD_RSN, bp, pb.ppm_events(REPO))
    assert sw.worst_rate == 227680
    assert sw.lapping == ["4 bidirectional DShot, 0 plain DShot, 0 PPM, "
                          "5 PWM"]
    assert sw.allowed > 0 and sw.runs > sw.allowed


def test_ppm_events_from_ppm_h(tmp_path):
    """18 edges in 8 x 400us + 3000us: 2,903 a second; a gap fails."""
    assert pb.ppm_events(REPO) == 2903
    (tmp_path / "protocols/ppm/include").mkdir(parents=True)
    (tmp_path / "protocols/ppm/include/ppm.h").write_text(
        "#define PPM_MAX_CHANNELS 8u\n")
    with pytest.raises(pb.BudgetError, match="PPM_CHANNEL_MIN_US"):
        pb.ppm_events(tmp_path)
