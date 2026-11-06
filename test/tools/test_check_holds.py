"""check_holds.py: every 2s hold is drawn and stepped for its flash.

SPDX-License-Identifier: MIT
"""

import check_holds as ch


def tree(tmp_path, files):
    """Writes @p files into a directory of screens' sources.

    Args:
        tmp_path: pytest's directory.
        files: Name to text.

    Returns:
        The directory.
    """
    for name, text in files.items():
        (tmp_path / name).write_text(text, encoding="utf-8")
    return tmp_path


BEGIN = "void d(void) { ui_hold_begin(&s.arm, 1); }\n"
DRAW = ("void r(gfx_canvas_t *c) { ui_hold_button(c, r, \"ARM\", a, b,"
        " &s.arm, false, true, f); }\n")
STEP = "void t(float dt) { ui_hold_flash_step(&s.arm, dt); }\n"


def test_the_tree_holds_every_hold_to_its_flash():
    """Every ui_hold_begin() of shared/ui is drawn and stepped."""
    assert ch.check() == []


def test_a_hold_drawn_and_stepped_in_its_module_passes(tmp_path):
    """The begin, the draw and the step may be in three files of one module."""
    ui = tree(tmp_path, {"servo_a.c": BEGIN, "servo_b.c": DRAW,
                         "servo_c.c": STEP})
    assert ch.check(ui) == []


def test_a_hold_never_stepped_fails(tmp_path):
    """A flash nobody steps stays white: named."""
    ui = tree(tmp_path, {"servo_a.c": BEGIN + DRAW})
    assert ch.check(ui) == ["servo_a.c: &s.arm is not stepped by "
                            "ui_hold_flash_step"]


def test_a_hold_drawn_without_the_helpers_fails(tmp_path):
    """A fill of its own shows no flash: named."""
    ui = tree(tmp_path, {"kst_a.c": BEGIN + STEP + (
        "void r(void) { ui_button_font(c, r, l, ui_hold_fill(a, b,"
        " s.arm.held_s), false, true, f); }\n")})
    assert len(ch.check(ui)) == 1
    assert "drawn by none" in ch.check(ui)[0]


def test_another_modules_draw_does_not_count(tmp_path):
    """A draw in a file of another module is not this hold's."""
    ui = tree(tmp_path, {"kst_a.c": BEGIN + STEP, "hitec_b.c": DRAW})
    assert len(ch.check(ui)) == 1


def test_a_wrapper_and_its_wrapper_count(tmp_path):
    """A function that hands the hold on, and one that calls it, count."""
    ui = tree(tmp_path, {"upd_a.c": BEGIN + DRAW + (
        "static void one(ui_hold_t *h, float dt)"
        " { ui_hold_flash_step(h, dt); }\n"
        "static void two(ui_hold_t *g, float dt) { one(g, dt); }\n"
        "void t(float dt) { two(&s.arm, dt); }\n")})
    assert ch.check(ui) == []


def test_a_comment_is_no_call(tmp_path):
    """A call in a comment draws and steps nothing."""
    ui = tree(tmp_path, {"servo_a.c": BEGIN + DRAW
                         + "/* ui_hold_flash_step(&s.arm, dt); */\n"})
    assert len(ch.check(ui)) == 1


def test_main_fails_only_with_check():
    """Without --check it lists; with it, the tree passes."""
    assert ch.main([]) == 0
    assert ch.main(["--check"]) == 0
