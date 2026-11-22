"""hardware/mechanical/_models.py: the folder of each model.

Every stem is in one folder, every file under models/ lies where the
map puts it, every model a spec or a generator writes is in the map,
and a stray file is found.

SPDX-License-Identifier: MIT
"""

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
import _models  # noqa: E402
import gen_models  # noqa: E402


def test_each_stem_is_in_one_folder():
    """3 folders, 42 stems, none twice."""
    assert list(_models.FOLDERS) == ["pcb", "casing", "parts"]
    stems = [stem for group in _models.FOLDERS.values() for stem in group]
    assert len(stems) == len(set(stems)) == 42
    assert [len(group) for group in _models.FOLDERS.values()] == [17, 11, 14]


def test_every_file_lies_in_its_folder():
    """No stray file, and each stem has its STEP file."""
    assert _models.strays() == []
    for stem in _models.FOLDER:
        assert _models.path(stem).is_file()
    assert not [p for p in _models.ROOT.iterdir() if p.is_file()]


def test_every_spec_model_is_sorted():
    """Each model gen_models.py writes has a folder."""
    for path in gen_models.outputs():
        assert path == _models.path(path.stem)


def test_unknown_stem_is_an_error():
    """A model that is not sorted has no path."""
    with pytest.raises(KeyError, match="no folder"):
        _models.path("no_such_model")


def test_stray_file_is_found(tmp_path, monkeypatch):
    """A file in the wrong folder, in the root or of another kind."""
    for name in ("pcb/panel_7b.step", "casing/panel_7b.step",
                 "m5dial.step", "parts/m5dial.txt", "pcb/.panel_7b.tmp"):
        (tmp_path / name).parent.mkdir(exist_ok=True)
        (tmp_path / name).write_text("")
    monkeypatch.setattr(_models, "ROOT", tmp_path)
    assert [str(p.relative_to(tmp_path)) for p in _models.strays()] == [
        "casing/panel_7b.step", "m5dial.step", "parts/m5dial.txt"]


def test_every_render_lies_in_its_folder():
    """A model's picture in the model's folder, all.png beside them."""
    assert _models.stray_renders() == []
    assert (_models.RENDERS / _models.SHEET).is_file()
    for stem, folder in _models.FOLDER.items():
        assert (_models.RENDERS / folder / f"{stem}.png").is_file()
    for name, folder in _models.VIEW.items():
        assert (_models.RENDERS / folder / f"{name}.png").is_file()
    assert not set(_models.VIEW) & set(_models.FOLDER)


def test_render_path_makes_its_folder(tmp_path):
    """A view goes to its folder; an unknown name is an error."""
    assert _models.render(tmp_path, "dial_base_section") == (
        tmp_path / "casing" / "dial_base_section.png")
    assert (tmp_path / "casing").is_dir()
    with pytest.raises(KeyError, match="no model and no view"):
        _models.render(tmp_path, "no_such_view")
