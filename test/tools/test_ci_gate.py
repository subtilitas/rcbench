"""ci_gate.py: a publishing workflow goes on only after a CI run passed on
its commit."""

import pytest

import ci_gate as gate

PASSED = {"status": "completed", "conclusion": "success"}
FAILED = {"status": "completed", "conclusion": "failure"}
CANCELLED = {"status": "completed", "conclusion": "cancelled"}
RUNNING = {"status": "in_progress", "conclusion": None}
QUEUED = {"status": "queued", "conclusion": None}


def state(runs, waited=0.0):
    return gate.verdict(runs, waited)[0]


def test_a_passed_run_lets_the_workflow_go_on():
    assert state([PASSED]) == "pass"


def test_a_pass_counts_beside_a_failed_or_a_running_run():
    assert state([FAILED, PASSED]) == "pass"
    assert state([RUNNING, PASSED]) == "pass"


def test_a_failed_run_stops_the_workflow_after_the_grace_period():
    assert gate.verdict([FAILED], gate.APPEAR_S) == (
        "fail", "CI ended on this commit without a pass: failure")


def test_an_older_failed_run_does_not_stop_the_workflow_at_once():
    """The run this push started may not be listed yet."""
    assert state([FAILED], 0) == "wait"
    assert state([FAILED], gate.APPEAR_S - 1) == "wait"
    assert state([FAILED, QUEUED], gate.APPEAR_S) == "wait"
    assert state([FAILED, PASSED], 5) == "pass"


def test_a_cancelled_run_stops_the_workflow():
    assert state([CANCELLED], gate.APPEAR_S) == "fail"
    assert state([CANCELLED, FAILED], gate.APPEAR_S) == "fail"


@pytest.mark.parametrize("run", [RUNNING, QUEUED])
def test_a_run_that_has_not_ended_is_waited_for(run):
    assert state([run]) == "wait"
    assert state([FAILED, run]) == "wait"      # a re-run of a failed one
    assert state([run], gate.TIMEOUT_S - 1) == "wait"


def test_a_run_that_never_ends_stops_the_workflow_at_the_limit():
    assert state([RUNNING], gate.TIMEOUT_S) == "fail"


def test_a_commit_without_a_run_is_waited_for_and_then_refused():
    assert state([]) == "wait"
    assert state([], gate.APPEAR_S - 1) == "wait"
    assert gate.verdict([], gate.APPEAR_S) == (
        "fail", "no CI run exists for this commit after 600 s")


def test_a_pull_request_run_does_not_count():
    listed = [
        {"event": "pull_request", "status": "completed",
         "conclusion": "success", "id": 1},
        {"event": "push", "status": "completed", "conclusion": "failure",
         "id": 2},
        {"event": "workflow_dispatch", "status": "queued",
         "conclusion": None, "id": 3},
    ]
    assert gate.on_this_tree(listed) == [FAILED, QUEUED]
    assert state(gate.on_this_tree(listed[:1])) == "wait"


def test_what_is_not_a_commit_id_or_a_repository_is_refused():
    with pytest.raises(SystemExit):
        gate.main(["--repo", "o/r", "--sha", "v1.0.0"])
    with pytest.raises(SystemExit):
        gate.main(["--repo", "o/r?x=1", "--sha", "a" * 40])


def test_the_gate_polls_until_ci_ends(monkeypatch):
    answers = [[], [QUEUED], [RUNNING], [PASSED]]
    asked = []

    def runs(repo, sha, workflow):
        asked.append((repo, sha, workflow))
        return answers[len(asked) - 1]

    monkeypatch.setattr(gate, "ci_runs", runs)
    monkeypatch.setattr(gate.time, "sleep", lambda s: None)
    assert gate.main(["--repo", "o/r", "--sha", "a" * 40]) == 0
    assert asked == [("o/r", "a" * 40, "ci.yml")] * 4


def test_the_gate_fails_when_ci_fails(monkeypatch, capsys):
    monkeypatch.setattr(gate, "ci_runs", lambda *a: [FAILED])
    monkeypatch.setattr(gate, "APPEAR_S", 0)
    monkeypatch.setattr(gate.verdict, "__defaults__", (0, gate.TIMEOUT_S))
    assert gate.main(["--repo", "o/r", "--sha", "b" * 40]) == 1
    assert "::error::CI ended on this commit without a pass" in (
        capsys.readouterr().out)
