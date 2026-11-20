"""mutate.py: which lines, which mutants, and a run that leaves nothing behind.

SPDX-License-Identifier: MIT
"""

import pathlib
import re
import shlex
import shutil
import subprocess

import pytest

from conftest import skip_or_fail
import mutate as mu

DIFF = """\
diff --git a/shared/a/a.c b/shared/a/a.c
--- a/shared/a/a.c
+++ b/shared/a/a.c
@@ -10,2 +10,3 @@ int f(void)
+one
+two
+three
@@ -40 +42 @@
+changed
@@ -50,2 +51,0 @@
-gone
-gone
diff --git a/shared/a/old.c b/shared/a/old.c
--- a/shared/a/old.c
+++ /dev/null
@@ -1,3 +0,0 @@
-x
diff --git a/shared/a/new.h b/shared/a/new.h
--- /dev/null
+++ b/shared/a/new.h
@@ -0,0 +1,2 @@
+#define A 1
+#define B 2
"""


def test_changed_lines_are_the_new_side_of_every_hunk():
    """Changed lines are the new side of every hunk."""
    assert mu.changed_lines(DIFF) == {
        "shared/a/a.c": {10, 11, 12, 42},
        "shared/a/new.h": {1, 2},
    }


def test_mask_blanks_comments_and_literals_and_keeps_columns():
    """Mask blanks comments and literals and keeps columns."""
    text = 'a < b; /* c < d */ s = "e < f"; // g < h\nc = \'<\'; i > j;\n'
    masked = mu.mask(text)
    assert len(masked) == len(text)
    assert masked.split("\n")[0].rstrip() == (
        "a < b; " + " " * len("/* c < d */") + " s = "
        + " " * len('"e < f"') + ";")
    assert masked.split("\n")[1] == "c =    ; i > j;"


def test_mask_follows_a_comment_over_lines():
    """Mask follows a comment over lines."""
    masked = mu.mask("x = 1; /* a < b\n   c > d */ y >= 2;\n")
    assert [line.rstrip() for line in masked.split("\n")] == [
        "x = 1;", " " * len("   c > d */") + " y >= 2;", ""]


def test_mask_keeps_an_escaped_quote_inside_a_string():
    """Mask keeps an escaped quote inside a string."""
    assert mu.mask(r'p("a\"<b"); q < r;') == (
        "p(" + " " * len(r'"a\"<b"') + "); q < r;")


def kinds(line):
    """The (kind, before, after) of every mutant of @p line."""
    return [(m.kind, m.before, m.after)
            for m in mu.line_mutants("f.c", 1, line, mu.mask(line))]


@pytest.mark.parametrize("op, flipped", list(mu.FLIP.items()))
def test_every_comparison_flips_to_its_neighbour(op, flipped):
    """Every comparison flips to its neighbour."""
    assert ("flip", op, flipped) in kinds(f"    if (a {op} b) {{")


@pytest.mark.parametrize("line", [
    "    p->next = q->next;",          # no comparison in an arrow
    "    x = y << 2;",
    "    x >>= 1;",
    "#include <stdint.h>",
    "#if FOO > 2",
    "    /* a < b */",
    '    puts("a < b");',
    "    int local = 3;",
    "    total += n;",
])
def test_no_mutant_where_there_is_no_comparison_bound_or_store(line):
    """No mutant where there is no comparison bound or store."""
    assert [k for k in kinds(line) if k[0] != "drop"] == []
    if "->" not in line:
        assert kinds(line) == []


def test_an_integer_beside_a_comparison_moves_by_one():
    """An integer beside a comparison moves by one."""
    assert kinds("    if (n > 5u) {") == [
        ("bound", "5u", "4u"), ("bound", "5u", "6u"), ("flip", ">", ">=")]


def test_zero_does_not_go_below_zero_and_hex_keeps_its_width():
    """Zero does not go below zero and hex keeps its width."""
    assert ("bound", "0", "1") in kinds("    if (n == 0) {")
    assert not any(a == "-1" for _, _, a in kinds("    if (n == 0) {"))
    assert ("bound", "0x0F", "0x10") in kinds("    if (0x0F < n) {")
    assert ("bound", "0x0F", "0x0E") in kinds("    if (0x0F < n) {")


def test_a_constant_beside_a_comparison_moves_by_one():
    """A constant beside a comparison moves by one."""
    out = kinds("    if (gap > HEARTBEAT_MAX_GAP_MS) {")
    assert ("bound", "HEARTBEAT_MAX_GAP_MS",
            "(HEARTBEAT_MAX_GAP_MS + 1)") in out
    assert ("bound", "HEARTBEAT_MAX_GAP_MS",
            "(HEARTBEAT_MAX_GAP_MS - 1)") in out


def test_null_a_call_and_a_float_are_no_bounds():
    """Null a call and a float are no bounds."""
    assert kinds("    if (m == NULL) {") == [("flip", "==", "!=")]
    assert kinds("    if (n < MAX(a)) {") == [("flip", "<", "<=")]
    assert kinds("    if (v < 0.05f) {") == [("flip", "<", "<=")]


def test_a_define_of_an_integer_moves_by_one():
    """A define of an integer moves by one."""
    assert kinds("#define LINK_DEV_SILENCE_MS 200u") == [
        ("bound", "200u", "199u"), ("bound", "200u", "201u")]
    assert kinds("#define NAME other_name") == []
    assert kinds("#define RATIO 0.5f") == []


@pytest.mark.parametrize("line", [
    "    m->alive = false;",
    "    s.pending.kind = MOTOR_CMD_NONE;",
    "    regs[i] = pack(a, b);",
    "    *out = 0;",
])
def test_a_store_that_outlives_the_statement_is_dropped(line):
    """A store that outlives the statement is dropped."""
    out = [m for m in mu.line_mutants("f.c", 1, line, mu.mask(line))
           if m.kind == "drop"]
    assert len(out) == 1
    assert out[0].text == "    (void)0;"


@pytest.mark.parametrize("line", [
    "    local = 3;",
    "    int x[2] = { 1, 2 };",
    "    if (a) b->c = 1;",
    "    m->good_run += 1;",
    "    ok = m->alive == true;",
    "    m->value = f(a,",
])
def test_what_is_no_whole_store_is_not_dropped(line):
    """What is no whole store is not dropped."""
    assert not [k for k in kinds(line) if k[0] == "drop"]


def test_a_mutant_changes_one_place_on_its_line():
    """A mutant changes one place on its line."""
    line = "    if (a < b && c < d) {"
    out = mu.line_mutants("f.c", 7, line, mu.mask(line))
    assert [m.text for m in out] == [
        "    if (a <= b && c < d) {", "    if (a < b && c <= d) {"]
    assert all(m.line == 7 for m in out)


def test_file_mutants_take_only_the_lines_asked_for():
    """File mutants take only the lines asked for."""
    text = "a < b;\n/* c < d */\ne > f;\n"
    assert [m.line for m in mu.file_mutants("f.c", text)] == [1, 3]
    assert [m.line for m in mu.file_mutants("f.c", text, {3})] == [3]
    assert mu.file_mutants("f.c", text, {2}) == []


def test_pick_is_even_and_the_same_every_time():
    """Pick is even and the same every time."""
    all_ = [mu.Mutant("f.c", n, 0, "flip", "<", "<=", "") for n in range(10)]
    assert mu.pick(all_, 0) == all_
    assert mu.pick(all_, 20) == all_
    assert [m.line for m in mu.pick(all_, 4)] == [0, 2, 5, 7]
    assert mu.pick(all_, 4) == mu.pick(all_, 4)


def test_the_summary_lists_the_survivors():
    """The summary lists the survivors."""
    results = [
        {"path": "shared/a.c", "line": 3, "kind": "flip", "before": "<",
         "after": "<=", "result": "survived"},
        {"path": "shared/a.c", "line": 4, "kind": "drop", "before": "x",
         "after": "(void)0;", "result": "killed"},
    ]
    text = mu.summary(results, 5, 12.4)
    assert "2 mutants in 12 s: 1 killed, 1 survived, 0 did not build, " \
           "5 not run." in text
    assert "| `shared/a.c:3` | flip | `<` to `<=` |" in text
    assert "shared/a.c:4" not in text


def _m(path, line):
    """A flip mutant of one line of a file."""
    return mu.Mutant(path, line, 0, "flip", "<", "<=", "")


def test_the_paths_named_first_lead_the_list_in_the_order_given():
    """--first: its prefixes in order, then the rest, each group sorted."""
    all_ = sorted([_m("shared/outputs/a.c", 1), _m("shared/outputs/a.c", 2),
                   _m("shared/safety/arming.c", 7),
                   _m("shared/safety/heartbeat.c", 3),
                   _m("shared/safety/include/arming.h", 9)])
    first = mu.put_first(all_, ["shared/safety/heartbeat.c",
                                "shared/safety/"])
    assert [(m.path, m.line) for m in first] == [
        ("shared/safety/heartbeat.c", 3),
        ("shared/safety/arming.c", 7),
        ("shared/safety/include/arming.h", 9),
        ("shared/outputs/a.c", 1),
        ("shared/outputs/a.c", 2)]
    assert mu.put_first(all_, []) == all_


def test_the_shards_hold_every_mutant_once_and_a_share_of_each_file():
    """--shard I/N takes every N-th mutant from place I."""
    all_ = ([_m("shared/safety/a.c", n) for n in range(7)]
            + [_m("shared/outputs/b.c", n) for n in range(5)])
    shards = [mu.take_shard(all_, i, 3) for i in range(3)]
    assert sorted(m for shard in shards for m in shard) == sorted(all_)
    assert [len(shard) for shard in shards] == [4, 4, 4]
    for shard in shards:
        # The files named first lead every shard.
        assert shard[0].path == "shared/safety/a.c"
        assert sum(m.path == "shared/safety/a.c" for m in shard) in (2, 3)


@pytest.mark.parametrize("text", ["6/6", "7/6", "a/6", "1", "1/0", "1/6/2"])
def test_a_shard_argument_that_names_no_shard_stops_the_tool(text, capsys):
    """I/N needs 0 <= I < N."""
    with pytest.raises(mu.Stop):
        mu.parse_shard(text)
    assert mu.main(["--files", "tools/mutate.py", "--shard", text,
                    "--list"]) == 2
    assert "--shard" in capsys.readouterr().err


def test_a_shard_argument_is_read():
    """0/6 and 5/6 are the first and the last of 6."""
    assert mu.parse_shard("0/6") == (0, 6)
    assert mu.parse_shard("5/6") == (5, 6)


def test_the_summary_names_the_files_the_budget_left():
    """A run that leaves mutants says so, file by file."""
    results = [{"path": "shared/a.c", "line": 3, "kind": "flip",
                "before": "<", "after": "<=", "result": "killed"}]
    chosen = [_m("shared/a.c", 3), _m("shared/a.c", 4), _m("shared/b.c", 1),
              _m("shared/b.c", 2)]
    left = mu.not_run_files(chosen, 1)
    assert left == {"shared/a.c": 1, "shared/b.c": 2}
    text = mu.summary(results, 3, 5.0, left)
    assert "Not every mutant ran: the budget ended with 3 mutants of " \
           "2 file(s) left." in text
    assert "| `shared/a.c` | 1 | 1 |" in text
    assert "| `shared/b.c` | 0 | 2 |" in text


def test_a_summary_of_a_whole_run_has_no_such_table():
    """Nothing left: no sentence and no table."""
    assert mu.not_run_files([_m("shared/a.c", 3)], 1) == {}
    assert "Not every mutant ran" not in mu.summary([], 0, 1.0, {})
    assert "Not every mutant ran" not in mu.summary([], 4, 1.0)


# The weekly run (.github/workflows/mutation.yml).
SAFETY_FIRST = ["shared/safety/arming.c", "shared/safety/heartbeat.c",
                "shared/safety/stop_latch.c", "shared/safety/touch_loss.c",
                "shared/safety/prog5v_lost.c", "shared/safety/"]
SHARDS = 6
BUDGET_S = 18000
# One mutant on the runner: 103,014s for 2,440 mutants in the run of
# 2026-11-15, rounded up.
MUTANT_S = 43


def weekly_step():
    """The run step of mutation.yml that calls the tool."""
    text = (mu.REPO / ".github" / "workflows" / "mutation.yml").read_text()
    step = text[text.index("python3 tools/mutate.py"):]
    return " ".join(step.replace("\\\n", " ").split())


def test_the_weekly_run_orders_the_safety_files_first():
    """mutation.yml passes the order and the shard this file holds."""
    step = weekly_step()
    assert "--first " + " ".join(SAFETY_FIRST) + " --" in step
    assert f'--shard "$SHARD/{SHARDS}"' in step
    assert f"--budget {BUDGET_S} " in step
    # xargs appends the files: --files is the last argument.
    assert '--summary "$GITHUB_STEP_SUMMARY" --files' in step


def test_every_shard_of_the_weekly_run_reaches_all_of_shared_safety():
    """The mutants of shared/safety fit every shard's budget.

    They lead each shard's list, so a shard that spends its budget has run
    them all.
    """
    files = sorted(str(p.relative_to(mu.REPO)) for d in ("safety", "outputs")
                   for p in (mu.REPO / "shared" / d).rglob("*.[ch]"))
    wanted = [f for f in files if "/rp2350/" not in f]
    every = mu.put_first(mu.candidates("unused", [
        str(mu.REPO / f) for f in wanted]), SAFETY_FIRST)
    for name in SAFETY_FIRST[:5]:
        assert any(m.path == name for m in every), name
    for index in range(SHARDS):
        shard = mu.take_shard(every, index, SHARDS)
        safety = [m for m in shard if m.path.startswith("shared/safety/")]
        # The safety mutants are the head of the list, the five named
        # files before the rest of the folder.
        assert shard[:len(safety)] == safety
        assert shard[0].path == "shared/safety/arming.c"
        assert len(safety) * MUTANT_S <= BUDGET_S, (index, len(safety))


# ------------------------------------------------ a run, end to end --

CMAKE = """\
cmake_minimum_required(VERSION 3.16)
project(tiny C)
enable_testing()
add_executable(test_limit test_limit.c ../../shared/limit/limit.c)
target_compile_options(test_limit PRIVATE -Wall -Wextra -Werror)
add_test(NAME limit COMMAND test_limit)
"""

LIMIT_C = """\
#include "limit.h"

int over(const state_t *s, int n)
{
    if (n > 10) {
        return 1;
    }
    return s->seen == 99;
}

void reset(state_t *s)
{
    s->seen = 0;
    s->spare = 0;
}
"""

LIMIT_H = """\
typedef struct { int seen; int spare; } state_t;
int over(const state_t *s, int n);
void reset(state_t *s);
"""

TEST_C = """\
#include "../../shared/limit/limit.h"

int main(void)
{
    state_t s = { 5, 5 };
    reset(&s);
    /* 10 and 11 are both asked, 99 never is, and spare is never read. */
    return !(over(&s, 10) == 0 && over(&s, 11) == 1 && s.seen == 0);
}
"""


@pytest.fixture
def tiny(tmp_path, monkeypatch):
    """A repository of one source and one test, laid out as this one."""
    for tool in ("git", "cmake", "cc"):
        if shutil.which(tool) is None:
            skip_or_fail(f"{tool} is not on PATH")
    repo = tmp_path / "repo"
    (repo / "shared" / "limit").mkdir(parents=True)
    (repo / "test" / "host").mkdir(parents=True)
    (repo / "firmware" / "iomcu").mkdir(parents=True)
    (repo / "shared" / "limit" / "limit.c").write_text(LIMIT_C)
    (repo / "shared" / "limit" / "limit.h").write_text(LIMIT_H)
    (repo / "test" / "host" / "CMakeLists.txt").write_text(CMAKE)
    (repo / "test" / "host" / "test_limit.c").write_text(TEST_C)

    def git(*args):
        subprocess.run(["git", "-c", "user.name=t", "-c",
                        "user.email=t@example.com", *args], cwd=repo,
                       check=True, capture_output=True)

    git("init", "-q", "-b", "main")
    git("add", "-A")
    git("commit", "-q", "-m", "base")
    monkeypatch.setattr(mu, "REPO", repo)
    monkeypatch.chdir(repo)
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    monkeypatch.setattr(mu.tempfile, "tempdir", str(scratch))
    return repo, scratch, git


def test_a_run_kills_reports_and_leaves_no_trace(tiny, capsys):
    """A run kills reports and leaves no trace."""
    repo, scratch, git = tiny
    before = (repo / "shared" / "limit" / "limit.c").read_bytes()
    out = repo.parent / "out.json"
    code = mu.main(["--files", "shared/limit/limit.c", "--json", str(out),
                    "--summary", str(repo.parent / "summary.md")])
    assert code == 0
    import json
    results = {(r["line"], r["kind"], r["after"]): r["result"]
               for r in json.loads(out.read_text())["results"]}
    assert results == {
        (5, "flip", ">="): "killed",          # 10 is asked
        (5, "bound", "9"): "killed",
        (5, "bound", "11"): "killed",         # 11 is asked
        (8, "flip", "!="): "killed",
        (8, "bound", "98"): "survived",       # 99 is never asked
        (8, "bound", "100"): "survived",
        (13, "drop", "(void)0;"): "killed",
        (14, "drop", "(void)0;"): "survived",  # spare is never read
    }
    # The working tree is as it was, and the copy is gone.
    assert (repo / "shared" / "limit" / "limit.c").read_bytes() == before
    status = subprocess.run(["git", "status", "--porcelain"], cwd=repo,
                            capture_output=True, text=True).stdout
    assert status == ""
    assert list(scratch.iterdir()) == []
    assert "3 survived" in capsys.readouterr().out
    assert "| `shared/limit/limit.c:14` | drop |" in (
        repo.parent / "summary.md").read_text()


def test_a_run_the_budget_ends_says_what_it_left(tiny, capsys):
    """Mutants left by the budget are in the output, JSON and summary."""
    repo, scratch, git = tiny
    out = repo.parent / "out.json"
    note = repo.parent / "summary.md"
    assert mu.main(["--files", "shared/limit/limit.c", "--budget", "-1",
                    "--json", str(out), "--summary", str(note)]) == 0
    import json
    data = json.loads(out.read_text())
    assert data["not_run"] == 8 and data["results"] == []
    assert data["not_run_files"] == {"shared/limit/limit.c": 8}
    printed = capsys.readouterr().out
    assert "0 killed, 0 survived, 0 did not build, 8 not run" in printed
    assert ("::warning::the budget of -1s ended with 8 mutants of 1 file(s) "
            "not run, the first of them in shared/limit/limit.c") in printed
    assert "| `shared/limit/limit.c` | 0 | 8 |" in note.read_text()


def test_a_whole_run_prints_no_warning(tiny, capsys):
    """Every mutant ran: no annotation."""
    assert mu.main(["--files", "shared/limit/limit.c"]) == 0
    assert "::warning::" not in capsys.readouterr().out


def test_a_shard_of_a_run_takes_its_share(tiny, capsys):
    """--shard 1/2 lists the 2nd, 4th, 6th and 8th mutant."""
    assert mu.main(["--files", "shared/limit/limit.c", "--shard", "1/2",
                    "--list"]) == 0
    listed = capsys.readouterr().out
    assert "4 mutants in 1 file(s), shard 1/2" in listed
    assert listed.count("limit.c:") == 4


def test_a_run_lists_the_paths_named_first_at_its_head(tiny, capsys):
    """main() orders the list by --first before it lists or shards it."""
    repo, scratch, git = tiny
    (repo / "shared" / "zone").mkdir()
    (repo / "shared" / "zone" / "zone.c").write_text(
        "int inside(int n)\n{\n    return n < 4;\n}\n")
    assert mu.main(["--files", "shared/limit/limit.c",
                    "shared/zone/zone.c", "--first", "shared/zone/",
                    "--list"]) == 0
    listed = [line.split()[0] for line in
              capsys.readouterr().out.splitlines()[1:]]
    # The 3 mutants of zone.c, then the 8 of limit.c, which sorts first.
    assert listed == ["shared/zone/zone.c:3"] * 3 + [
        "shared/limit/limit.c:5"] * 3 + ["shared/limit/limit.c:8"] * 3 + [
        "shared/limit/limit.c:13", "shared/limit/limit.c:14"]


def test_the_weekly_call_lists_shared_safety_first_in_every_shard(
        monkeypatch, capsys):
    """mutation.yml's own arguments, through main(), give the safety order.

    The call is taken from the workflow as written, with --list put before
    the files, for the first and the last shard.
    """
    step = weekly_step()
    call = step[:step.index(" --files") + len(" --files")]
    argv = shlex.split(call)[2:]
    assert argv[-1] == "--files"
    monkeypatch.chdir(mu.REPO)
    files = sorted(p.relative_to(mu.REPO).as_posix()
                   for d in ("safety", "outputs")
                   for p in (mu.REPO / "shared" / d).rglob("*.[ch]"))
    for shard in (0, SHARDS - 1):
        args = [a.replace("$SHARD", str(shard)) for a in argv[:-1]]
        assert mu.main([*args, "--list", "--files", *files]) == 0
        paths = [line.split(":")[0].strip() for line in
                 capsys.readouterr().out.splitlines()[1:]]
        safety = [p for p in paths if p.startswith("shared/safety/")]
        # Every safety mutant of the shard comes before the first other
        # one, and the five named files lead in the order given.
        assert safety and paths[:len(safety)] == safety
        assert list(dict.fromkeys(safety))[:5] == SAFETY_FIRST[:5]


def test_a_ceiling_on_survivors_sets_the_exit_code(tiny):
    """A ceiling on survivors sets the exit code."""
    assert mu.main(["--files", "shared/limit/limit.c",
                    "--max-survivors", "3"]) == 0
    assert mu.main(["--files", "shared/limit/limit.c",
                    "--max-survivors", "2"]) == 1


def test_only_the_lines_a_change_touches_are_mutated(tiny, capsys):
    """Only the lines a change touches are mutated."""
    repo, scratch, git = tiny
    git("checkout", "-q", "-b", "work")
    path = repo / "shared" / "limit" / "limit.c"
    path.write_text(path.read_text().replace("n > 10", "n > 20"))
    assert mu.main(["--base", "main", "--list"]) == 0
    listed = capsys.readouterr().out
    assert "3 mutants in the lines changed since main" in listed
    assert "limit.c:5" in listed and "limit.c:8" not in listed


def test_a_suite_that_fails_unmutated_stops_the_run(tiny, capsys):
    """A suite that fails unmutated stops the run."""
    repo, scratch, git = tiny
    path = repo / "test" / "host" / "test_limit.c"
    path.write_text(path.read_text().replace("== 1 &&", "== 7 &&"))
    assert mu.main(["--files", "shared/limit/limit.c"]) == 2
    assert "the unmutated suite does not pass" in capsys.readouterr().err
    assert list(scratch.iterdir()) == []


def test_a_mutant_that_does_not_build_is_neither_killed_nor_survived(tiny):
    """A mutant that does not build is neither killed nor survived."""
    repo, scratch, git = tiny
    bench = mu.Bench(scratch / "copy", 2, 120)
    bench.copy()
    bench.configure()
    assert bench.compile()[0] == 0
    path = "shared/limit/limit.c"
    broken = mu.Mutant(path, 5, 0, "flip", ">", ">=", "    if (n >) {")
    assert bench.judge(broken) == ("nobuild", "does not compile")
    # The copy holds the original again.
    assert (scratch / "copy" / path).read_text() == LIMIT_C
    hang = mu.Mutant(path, 5, 0, "drop", "", "",
                     "    for (;;) { } if (n > 10) {")
    bench.timeout = 3
    assert bench.judge(hang) == (
        "killed", "the suite was stopped at the time limit")
    assert (scratch / "copy" / path).read_text() == LIMIT_C


def test_the_tools_own_tree_is_never_the_copy():
    """The tool's own tree is never the copy."""
    assert mu.REPO == pathlib.Path(mu.__file__).resolve().parent.parent
    assert set(mu.MUTATED) <= set(mu.COPIED)


def test_the_copy_holds_every_source_the_host_suite_reads():
    """The copy holds every source the host suite reads.

    A path test/host/CMakeLists.txt reads only behind an if(EXISTS ...)
    may stay out, as tools/sense_trace.py does.
    """
    text = (mu.REPO / "test/host/CMakeLists.txt").read_text()
    paths = set(re.findall(r"\.\./\.\./([A-Za-z0-9_./-]+)", text))
    guarded = set(re.findall(
        r"EXISTS\s+\"\$\{CMAKE_CURRENT_SOURCE_DIR\}/\.\./\.\./"
        r"([A-Za-z0-9_./-]+)\"", text))
    paths -= guarded
    assert paths
    missing = sorted(
        p for p in paths
        if not any(p == c or p.startswith(c.rstrip("/") + "/")
                   for c in mu.COPIED))
    assert not missing, f"not in mutate.COPIED: {missing}"


def test_a_protocol_core_is_mutated_and_its_pin_driver_is_not():
    """A protocol core is mutated and its pin driver is not."""
    core = mu.candidates("unused", [str(mu.REPO / "protocols/ppm/ppm.c")])
    assert core and {m.path for m in core} == {"protocols/ppm/ppm.c"}
    assert mu.candidates(
        "unused", [str(mu.REPO / "protocols/ppm/rp2350/out_ppm.c")]) == []
    assert mu.candidates(
        "unused", [str(mu.REPO / "firmware/iomcu/src/out_pwm.c")]) == []
