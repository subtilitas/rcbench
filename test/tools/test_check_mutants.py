"""check_mutants.py: the weekly run's figures against the counted mutants.

SPDX-License-Identifier: MIT
"""

import pytest

import check_mutants as cm
import mutate as mu

WORKFLOW = """\
# {comment}
      - run: |
          find shared/safety shared/outputs -name '*.c' -o -name '*.h' \\
            | sort > "$RUNNER_TEMP/files"
          xargs -a "$RUNNER_TEMP/files" python3 tools/mutate.py \\
            --shard "$SHARD/6" --max-mutants 0 --budget 18000 --files
"""

# 2 mutants in shared/safety and 3 in shared/outputs, in 3 files; the
# header has none.
SAFETY_C = "int on(int n)\n{\n    return n == 0;\n}\n"
OUTPUTS_C = "int high(int n)\n{\n    return n > 7;\n}\n"
OUTPUTS_H = "int high(int n);\n"


def page(name, want):
    """A file holding the phrases PLACES gives @p name, filled in.

    The German page groups thousands with a point, the others with a
    comma; the budget is written without a separator.
    """
    like = "1.000" if name.endswith("-de.md") else "1,000"
    values = {k: cm.written(v, "1" if k == "budget" else like)
              for k, v in want.items()}
    text = "\n\n".join(t.format(**values) for t in cm.PLACES[name])
    if name == cm.WORKFLOW:
        # Wrapped as a YAML comment: every space may become a line break.
        return WORKFLOW.format(comment=text.replace(" a shard", "\n# a shard")
                               .replace("\n\n", "\n# "))
    return f"# Page\n\nSome text.\n\n{text}\n"


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """A tree with the five files at the counted figures."""
    (tmp_path / "shared" / "safety").mkdir(parents=True)
    (tmp_path / "shared" / "outputs" / "include").mkdir(parents=True)
    (tmp_path / "shared" / "safety" / "on.c").write_text(SAFETY_C)
    (tmp_path / "shared" / "outputs" / "high.c").write_text(OUTPUTS_C)
    (tmp_path / "shared" / "outputs" / "include" / "high.h").write_text(
        OUTPUTS_H)
    (tmp_path / ".github" / "workflows").mkdir(parents=True)
    (tmp_path / "docs").mkdir()
    monkeypatch.setattr(mu, "REPO", tmp_path)
    (tmp_path / cm.WORKFLOW).write_text(WORKFLOW.format(comment=""))
    want = cm.count(tmp_path)
    for name in cm.PLACES:
        (tmp_path / name).write_text(page(name, want))
    return tmp_path, want


def test_the_figures_follow_from_the_counts():
    """The run's reach: 18000s at 42s a mutant, 6 shards."""
    got = cm.figures(85, 699, 6044, 6, 18000)
    assert got == {"files": 85, "mutants": 6743, "safety": 699,
                   "outputs": 6044, "shards": 6, "budget": 18000,
                   "per_shard": 428, "run": 2568, "safety_per_shard": 117,
                   "outputs_run": 1869}


def test_a_figure_keeps_its_separator():
    """6,743, 6.743 and 6743 stay in their style."""
    assert cm.written(1869, "1,871") == "1,869"
    assert cm.written(1869, "1.871") == "1.869"
    assert cm.written(6801, "6743") == "6801"
    assert cm.written(999, "1,001") == "999"
    assert cm.number("6.743") == cm.number("6,743") == 6743


def test_the_tree_is_counted_as_the_weekly_run_counts_it(tree):
    """3 files named, the header without mutants among them."""
    root, want = tree
    assert cm.weekly_files(root) == [
        "shared/outputs/high.c", "shared/outputs/include/high.h",
        "shared/safety/on.c"]
    safety = len(mu.candidates("x", [str(root / "shared/safety/on.c")]))
    outputs = len(mu.candidates("x", [str(root / "shared/outputs/high.c")]))
    assert (want["files"], want["safety"], want["outputs"]) == (
        3, safety, outputs)
    assert want["mutants"] == safety + outputs > 0


def test_figures_that_agree_pass(tree, capsys):
    """--check and --update on agreeing figures: 0, nothing written."""
    root, want = tree
    before = {n: (root / n).read_text() for n in cm.PLACES}
    assert cm.main(["--check"], root) == 0
    assert cm.main(["--update"], root) == 0
    assert {n: (root / n).read_text() for n in cm.PLACES} == before
    assert "wrote" not in capsys.readouterr().out


def test_a_file_added_to_a_folder_makes_the_figures_stale(tree, capsys):
    """The drift the check is for: a new file under shared/safety."""
    root, want = tree
    (root / "shared" / "safety" / "more.c").write_text(
        "int off(int n)\n{\n    return n != 1;\n}\n")
    assert cm.main(["--check"], root) == 1
    err = capsys.readouterr().err
    for name in cm.PLACES:
        assert f"{name}: mutants reads {want['mutants']}," in err
    assert "docs/Building-de.md: files reads 3, the tree gives 4" in err
    assert cm.main(["--update"], root) == 0
    assert cm.main(["--check"], root) == 0
    new = cm.count(root)
    assert new["files"] == 4 and new["safety"] > want["safety"]
    assert f"({new['mutants']} Mutanten in 4 Dateien)" in (
        root / "docs/Building-de.md").read_text()


def test_update_keeps_the_separator_and_the_rest_of_the_page(tree):
    """Only the stale number changes, in the style it was written in."""
    root, want = tree
    path = root / "docs/Building-de.md"
    good = path.read_text()
    stale = good.replace(f"etwa {cm.written(want['outputs_run'], '1.000')} "
                         f"der", "etwa 1.871 der")
    assert stale != good
    path.write_text(stale)
    assert cm.main(["--check"], root) == 1
    assert cm.main(["--update"], root) == 0
    assert path.read_text() == good


def test_a_phrase_that_is_gone_fails_both_modes(tree, capsys):
    """A rewritten sentence is not silently left unchecked."""
    root, want = tree
    path = root / "CONTRIBUTING.md"
    path.write_text(path.read_text().replace("runs in 6 shards.",
                                             "runs in six shards."))
    assert cm.main(["--check"], root) == 1
    assert cm.main(["--update"], root) == 1
    assert 'CONTRIBUTING.md: 0 matches of "runs in {shards} shards."' in (
        capsys.readouterr().err)


def test_a_phrase_twice_is_refused(tree, capsys):
    """Two places for one phrase: the check cannot tell which it holds."""
    root, want = tree
    path = root / "STATUS.md"
    path.write_text(path.read_text() * 2)
    assert cm.main(["--check"], root) == 1
    assert "STATUS.md: 2 matches of" in capsys.readouterr().err


def test_the_workflow_must_name_the_folders_and_the_call():
    """No find of the two folders, no shards or no budget: an error."""
    good = WORKFLOW.format(comment="")
    assert cm.weekly_call(good) == (6, 18000)
    for bad in (good.replace("shared/outputs ", ""),
                good.replace('--shard "$SHARD/6" ', ""),
                good.replace("--budget 18000 ", "")):
        with pytest.raises(ValueError):
            cm.weekly_call(bad)


def test_a_figure_wrapped_across_comment_lines_is_read(tree):
    """A YAML comment that breaks a phrase over lines still matches."""
    root, want = tree
    text = (root / cm.WORKFLOW).read_text()
    assert "\n# a shard" in text
    new, problems = cm.compare(cm.WORKFLOW, text, want)
    assert problems == [] and new == text
