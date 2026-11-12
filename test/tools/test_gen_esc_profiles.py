# SPDX-License-Identifier: MIT
"""tools/gen_esc_profiles.py: a malformed profile is refused.

A malformed profile is refused with the
generator's own error, never with a Python exception of another type.
"""

import json
import pathlib

import pytest

import gen_esc_profiles as g

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
BASE = REPO / "shared" / "esc" / "profiles" / "hobbywing-flyfun-v5.json"


def profile(tmp_path, change):
    """A profile of record with @change applied, written under its own name.

    The id is the file's name.
    """
    d = json.loads(BASE.read_text(encoding="utf-8"))
    change(d)
    path = tmp_path / BASE.name
    path.write_text(json.dumps(d), encoding="utf-8")
    return path


def applies_to(value):
    """A change that sets applies_to of the first item to @p value."""
    def change(d):
        d["items"][0]["applies_to"] = value
    return change


def test_the_profile_of_record_is_taken(tmp_path):
    """The profile of record is taken."""
    p = g.check(profile(tmp_path, lambda d: None))
    assert p["id"] == BASE.stem


def test_applies_to_takes_the_profiles_model_names(tmp_path):
    """Applies to takes the profiles model names."""
    d = json.loads(BASE.read_text(encoding="utf-8"))
    name = d["models"][0]["name"]
    p = g.check(profile(tmp_path, applies_to([name])))
    assert p["items"][0]["applies"] == [name]


# A list and an object are not hashable: membership in the set of model
# names raises TypeError for them before it can answer.
@pytest.mark.parametrize("entry", [[], ["x"], {}, {"name": "x"}, [[]]])
def test_an_applies_to_entry_that_is_a_list_or_an_object_is_refused(
        tmp_path, entry):
    """An applies to entry that is a list or an object is refused."""
    with pytest.raises(g.Bad, match=r"items\[0\]\.applies_to"):
        g.check(profile(tmp_path, applies_to([entry])))


@pytest.mark.parametrize("entry", [None, 1, 1.5, True, "", "no such model"])
def test_an_applies_to_entry_that_is_no_model_name_is_refused(
        tmp_path, entry):
    """An applies to entry that is no model name is refused."""
    with pytest.raises(g.Bad, match=r"items\[0\]\.applies_to"):
        g.check(profile(tmp_path, applies_to([entry])))


@pytest.mark.parametrize("value", ["x", 1, True, {}])
def test_an_applies_to_that_is_no_list_is_refused(tmp_path, value):
    """An applies to that is no list is refused."""
    with pytest.raises(g.Bad, match=r"items\[0\]\.applies_to"):
        g.check(profile(tmp_path, applies_to(value)))


# json.loads() gives up near 1,000 levels with RecursionError; the card
# reader refuses the same file at 17.
@pytest.mark.parametrize("depth", [17, 999, 1000, 1001, 3000, 20000])
def test_nesting_past_what_json_loads_follows_is_refused(tmp_path, depth):
    """Nesting past what JSON loads follows is refused."""
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
    """Nesting at the limit is taken."""
    text = BASE.read_text(encoding="utf-8")
    head = '"schema": 1,'
    path = tmp_path / BASE.name
    path.write_text(text.replace(
        head, head + ' "n": ' + "[" * 15 + "1" + "]" * 15 + ",", 1),
        encoding="utf-8")
    assert g.check(path)["id"] == BASE.stem


def test_a_profile_without_family_de_has_an_empty_one(tmp_path):
    """A profile without family de has an empty one."""
    assert g.check(profile(
        tmp_path, lambda d: d.pop("family_de", None)))["family_de"] == ""


def test_family_de_is_taken_with_its_umlauts(tmp_path):
    """Family de is taken with its umlauts."""
    def change(d):
        d["family_de"] = "Zählmenü mit 8 Punkten"
    p = g.check(profile(tmp_path, change))
    assert p["family_de"] == "Zählmenü mit 8 Punkten"
    assert p["family"] != p["family_de"]


def test_a_null_family_de_is_none(tmp_path):
    """A null family de is none."""
    def change(d):
        d["family_de"] = None
    assert g.check(profile(tmp_path, change))["family_de"] == ""


@pytest.mark.parametrize("value", ["", 1, True, ["x"], {}])
def test_a_family_de_that_is_empty_or_no_string_is_refused(tmp_path, value):
    """A family de that is empty or no string is refused."""
    def change(d):
        d["family_de"] = value
    with pytest.raises(g.Bad, match=r"family_de"):
        g.check(profile(tmp_path, change))


@pytest.mark.parametrize("text,ok", [("x" * 120, True), ("x" * 121, False),
                                     ("ü" * 60, True), ("ü" * 61, False)])
def test_family_de_is_at_most_120_bytes(tmp_path, text, ok):
    """Family de is at most 120 bytes."""
    def change(d):
        d["family_de"] = text
    path = profile(tmp_path, change)
    if ok:
        assert g.check(path)["family_de"] == text
    else:
        with pytest.raises(g.Bad, match=r"family_de: longer than 120"):
            g.check(path)


def test_the_generated_table_carries_family_de_beside_the_family():
    """The generated table carries family de beside the family."""
    src = (REPO / "shared" / "esc" / "esc_profiles_gen.c").read_text(
        encoding="utf-8")
    d = json.loads((REPO / "shared" / "esc" / "profiles"
                    / "dualsky-xcontroller.json").read_text(encoding="utf-8"))
    assert d["family_de"]
    assert (g.c_str(d["family"]) + ", " + g.c_str(d["family_de"])) in src


def test_every_family_de_of_record_differs_from_its_family():
    """Every family de of record differs from its family."""
    for path in sorted(g.SRC.glob("*.json")):
        d = json.loads(path.read_text(encoding="utf-8"))
        if "family_de" in d:
            assert d["family_de"] != d["family"], path.name
