"""mutate.py: which lines, which mutants, and a run that leaves nothing
behind."""

import pathlib
import shutil
import subprocess

import pytest

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
    assert mu.changed_lines(DIFF) == {
        "shared/a/a.c": {10, 11, 12, 42},
        "shared/a/new.h": {1, 2},
    }


def test_mask_blanks_comments_and_literals_and_keeps_columns():
    text = 'a < b; /* c < d */ s = "e < f"; // g < h\nc = \'<\'; i > j;\n'
    masked = mu.mask(text)
    assert len(masked) == len(text)
    assert masked.split("\n")[0].rstrip() == (
        "a < b; " + " " * len("/* c < d */") + " s = "
        + " " * len('"e < f"') + ";")
    assert masked.split("\n")[1] == "c =    ; i > j;"


def test_mask_follows_a_comment_over_lines():
    masked = mu.mask("x = 1; /* a < b\n   c > d */ y >= 2;\n")
    assert [line.rstrip() for line in masked.split("\n")] == [
        "x = 1;", " " * len("   c > d */") + " y >= 2;", ""]


def test_mask_keeps_an_escaped_quote_inside_a_string():
    assert mu.mask(r'p("a\"<b"); q < r;') == (
        "p(" + " " * len(r'"a\"<b"') + "); q < r;")


def kinds(line):
    return [(m.kind, m.before, m.after)
            for m in mu.line_mutants("f.c", 1, line, mu.mask(line))]


@pytest.mark.parametrize("op, flipped", list(mu.FLIP.items()))
def test_every_comparison_flips_to_its_neighbour(op, flipped):
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
    assert [k for k in kinds(line) if k[0] != "drop"] == []
    if "->" not in line:
        assert kinds(line) == []


def test_an_integer_beside_a_comparison_moves_by_one():
    assert kinds("    if (n > 5u) {") == [
        ("bound", "5u", "4u"), ("bound", "5u", "6u"), ("flip", ">", ">=")]


def test_zero_does_not_go_below_zero_and_hex_keeps_its_width():
    assert ("bound", "0", "1") in kinds("    if (n == 0) {")
    assert not any(a == "-1" for _, _, a in kinds("    if (n == 0) {"))
    assert ("bound", "0x0F", "0x10") in kinds("    if (0x0F < n) {")
    assert ("bound", "0x0F", "0x0E") in kinds("    if (0x0F < n) {")


def test_a_constant_beside_a_comparison_moves_by_one():
    out = kinds("    if (gap > HEARTBEAT_MAX_GAP_MS) {")
    assert ("bound", "HEARTBEAT_MAX_GAP_MS",
            "(HEARTBEAT_MAX_GAP_MS + 1)") in out
    assert ("bound", "HEARTBEAT_MAX_GAP_MS",
            "(HEARTBEAT_MAX_GAP_MS - 1)") in out


def test_null_a_call_and_a_float_are_no_bounds():
    assert kinds("    if (m == NULL) {") == [("flip", "==", "!=")]
    assert kinds("    if (n < MAX(a)) {") == [("flip", "<", "<=")]
    assert kinds("    if (v < 0.05f) {") == [("flip", "<", "<=")]


def test_a_define_of_an_integer_moves_by_one():
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
    assert not [k for k in kinds(line) if k[0] == "drop"]


def test_a_mutant_changes_one_place_on_its_line():
    line = "    if (a < b && c < d) {"
    out = mu.line_mutants("f.c", 7, line, mu.mask(line))
    assert [m.text for m in out] == [
        "    if (a <= b && c < d) {", "    if (a < b && c <= d) {"]
    assert all(m.line == 7 for m in out)


def test_file_mutants_take_only_the_lines_asked_for():
    text = "a < b;\n/* c < d */\ne > f;\n"
    assert [m.line for m in mu.file_mutants("f.c", text)] == [1, 3]
    assert [m.line for m in mu.file_mutants("f.c", text, {3})] == [3]
    assert mu.file_mutants("f.c", text, {2}) == []


def test_pick_is_even_and_the_same_every_time():
    all_ = [mu.Mutant("f.c", n, 0, "flip", "<", "<=", "") for n in range(10)]
    assert mu.pick(all_, 0) == all_
    assert mu.pick(all_, 20) == all_
    assert [m.line for m in mu.pick(all_, 4)] == [0, 2, 5, 7]
    assert mu.pick(all_, 4) == mu.pick(all_, 4)


def test_the_summary_lists_the_survivors():
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
            pytest.skip(f"{tool} is not on PATH")
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


def test_a_ceiling_on_survivors_sets_the_exit_code(tiny):
    assert mu.main(["--files", "shared/limit/limit.c",
                    "--max-survivors", "3"]) == 0
    assert mu.main(["--files", "shared/limit/limit.c",
                    "--max-survivors", "2"]) == 1


def test_only_the_lines_a_change_touches_are_mutated(tiny, capsys):
    repo, scratch, git = tiny
    git("checkout", "-q", "-b", "work")
    path = repo / "shared" / "limit" / "limit.c"
    path.write_text(path.read_text().replace("n > 10", "n > 20"))
    assert mu.main(["--base", "main", "--list"]) == 0
    listed = capsys.readouterr().out
    assert "3 mutants in the lines changed since main" in listed
    assert "limit.c:5" in listed and "limit.c:8" not in listed


def test_a_suite_that_fails_unmutated_stops_the_run(tiny, capsys):
    repo, scratch, git = tiny
    path = repo / "test" / "host" / "test_limit.c"
    path.write_text(path.read_text().replace("== 1 &&", "== 7 &&"))
    assert mu.main(["--files", "shared/limit/limit.c"]) == 2
    assert "the unmutated suite does not pass" in capsys.readouterr().err
    assert list(scratch.iterdir()) == []


def test_a_mutant_that_does_not_build_is_neither_killed_nor_survived(tiny):
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
    assert mu.REPO == pathlib.Path(mu.__file__).resolve().parent.parent
    assert set(mu.MUTATED) <= set(mu.COPIED)


def test_a_protocol_core_is_mutated_and_its_pin_driver_is_not():
    core = mu.candidates("unused", [str(mu.REPO / "protocols/ppm/ppm.c")])
    assert core and {m.path for m in core} == {"protocols/ppm/ppm.c"}
    assert mu.candidates(
        "unused", [str(mu.REPO / "protocols/ppm/rp2350/out_ppm.c")]) == []
    assert mu.candidates(
        "unused", [str(mu.REPO / "firmware/iomcu/src/out_pwm.c")]) == []
