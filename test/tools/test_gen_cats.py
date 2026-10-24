# SPDX-License-Identifier: MIT
"""Tests of tools/gen_cats.py.

The checked-in tables are what the generator writes, each table decodes
back to the pixels it was made from, the runs keep to the format
gfx_sprite.h describes, and a pose naming a spot that does not exist is
refused.
"""

import dataclasses
import importlib.util

import pytest

import gen_cats as g
from conftest import skip_or_fail


def need_pillow():
    """Skip locally, fail in CI, when Pillow is not installed."""
    if importlib.util.find_spec("PIL") is None:
        skip_or_fail("Pillow is not installed")


@pytest.fixture(scope="module")
def arts():
    """Every pose, drawn, cut and encoded once."""
    need_pillow()
    return [g.build(p) for p in g.POSES]


def test_the_checked_in_files_are_what_the_generator_writes():
    """--check passes on the tree as committed."""
    need_pillow()
    assert g.main(["--check"]) == 0


def test_each_table_decodes_to_its_pixels(arts):
    """decode() reads every table back to the rows it encodes."""
    for a in arts:
        assert g.decode(a.data, a.w, a.h) == a.rows


def test_the_cut_leaves_no_empty_edge(arts):
    """The first and last row and column each hold a covered pixel."""
    for a in arts:
        assert any(p[0] for p in a.rows[0])
        assert any(p[0] for p in a.rows[-1])
        assert any(r[0][0] for r in a.rows)
        assert any(r[-1][0] for r in a.rows)


def test_premultiplied_colours_stay_within_their_alpha(arts):
    """A partly covered pixel's channels are at most round(max x a / 15).

    So the blend in gfx_sprite.c never carries.
    """
    for a in arts:
        for row in a.rows:
            for al, c in row:
                if 0 < al < 15:
                    f = al / 15.0
                    assert (c >> 11) <= int(31 * f + 0.5)
                    assert ((c >> 5) & 63) <= int(63 * f + 0.5)
                    assert (c & 31) <= int(31 * f + 0.5)
                elif al == 0:
                    assert c == 0


def test_runs_keep_to_the_format():
    """Runs keep to the format.

    64 pixels at most, none across a row; FILL for a repeat of 3 or more,
    COPY otherwise; BLEND alpha packed low nibble first.
    """
    row = [(15, 5)] * 70 + [(0, 0)] * 2 + [(15, 1), (15, 2), (7, 9)]
    data = g.encode_row(row)
    assert data[0] == (g.RUN_FILL << 6) | 63
    assert data[3] == (g.RUN_FILL << 6) | 5
    assert data[6] == (g.RUN_SKIP << 6) | 1
    assert data[7] == (g.RUN_COPY << 6) | 1
    assert data[12] == (g.RUN_BLEND << 6) | 0
    assert data[13] == 7
    assert g.decode(data, len(row), 1) == [row]


def test_a_bad_table_is_refused():
    """A run across its row, a table cut short and bytes past the end."""
    with pytest.raises(ValueError):
        g.decode(bytes([(g.RUN_SKIP << 6) | 6]), 6, 1)
    with pytest.raises(ValueError):
        g.decode(bytes([(g.RUN_FILL << 6) | 1, 0x1F]), 2, 1)
    with pytest.raises(ValueError):
        g.decode(bytes([(g.RUN_SKIP << 6) | 1, 0]), 2, 1)


def test_a_pose_with_a_spot_that_does_not_exist_is_refused():
    """build() names the pose and the spot."""
    need_pillow()
    bad = dataclasses.replace(g.POSES[0], spots=("nowhere",))
    with pytest.raises(ValueError, match="nowhere"):
        g.build(bad)
    with pytest.raises(ValueError, match="no spot"):
        g.build(dataclasses.replace(g.POSES[0], spots=()))


def test_every_spot_is_taken_by_a_pose():
    """No spot in SPOTS is left without a pose."""
    taken = {s for p in g.POSES for s in p.spots}
    assert taken == set(g.SPOTS)
