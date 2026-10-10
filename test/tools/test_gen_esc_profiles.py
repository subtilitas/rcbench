# SPDX-License-Identifier: MIT
"""tools/gen_esc_profiles.py: a malformed profile is refused with the
generator's own error, never with a Python exception of another type."""

import json
import pathlib

import pytest

import gen_esc_profiles as g

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
BASE = REPO / "shared" / "esc" / "profiles" / "hobbywing-flyfun-v5.json"


def profile(tmp_path, change):
    """A profile of record with @change applied, written under its own
    name: the id is the file's name."""
    d = json.loads(BASE.read_text(encoding="utf-8"))
    change(d)
    path = tmp_path / BASE.name
    path.write_text(json.dumps(d), encoding="utf-8")
    return path


def applies_to(value):
    def change(d):
        d["items"][0]["applies_to"] = value
    return change


def test_the_profile_of_record_is_taken(tmp_path):
    p = g.check(profile(tmp_path, lambda d: None))
    assert p["id"] == BASE.stem


def test_applies_to_takes_the_profiles_model_names(tmp_path):
    d = json.loads(BASE.read_text(encoding="utf-8"))
    name = d["models"][0]["name"]
    p = g.check(profile(tmp_path, applies_to([name])))
    assert p["items"][0]["applies"] == [name]


# A list and an object are not hashable: membership in the set of model
# names raises TypeError for them before it can answer.
@pytest.mark.parametrize("entry", [[], ["x"], {}, {"name": "x"}, [[]]])
def test_an_applies_to_entry_that_is_a_list_or_an_object_is_refused(
        tmp_path, entry):
    with pytest.raises(g.Bad, match=r"items\[0\]\.applies_to"):
        g.check(profile(tmp_path, applies_to([entry])))


@pytest.mark.parametrize("entry", [None, 1, 1.5, True, "", "no such model"])
def test_an_applies_to_entry_that_is_no_model_name_is_refused(
        tmp_path, entry):
    with pytest.raises(g.Bad, match=r"items\[0\]\.applies_to"):
        g.check(profile(tmp_path, applies_to([entry])))


@pytest.mark.parametrize("value", ["x", 1, True, {}])
def test_an_applies_to_that_is_no_list_is_refused(tmp_path, value):
    with pytest.raises(g.Bad, match=r"items\[0\]\.applies_to"):
        g.check(profile(tmp_path, applies_to(value)))


# json.loads() gives up near 1,000 levels with RecursionError; the card
# reader refuses the same file at 17.
@pytest.mark.parametrize("depth", [17, 999, 1000, 1001, 3000, 20000])
def test_nesting_past_what_json_loads_follows_is_refused(tmp_path, depth):
    text = BASE.read_text(encoding="utf-8")
    head = '"schema": 1,'
    assert head in text
    path = tmp_path / BASE.name
    path.write_text(text.replace(
        head, head + ' "n": ' + "[" * depth + "1" + "]" * depth + ",", 1),
        encoding="utf-8")
    with pytest.raises(g.Bad, match="nested deeper than 16"):
        g.check(path)


def test_nesting_at_the_limit_is_taken(tmp_path):
    text = BASE.read_text(encoding="utf-8")
    head = '"schema": 1,'
    path = tmp_path / BASE.name
    path.write_text(text.replace(
        head, head + ' "n": ' + "[" * 15 + "1" + "]" * 15 + ",", 1),
        encoding="utf-8")
    assert g.check(path)["id"] == BASE.stem
