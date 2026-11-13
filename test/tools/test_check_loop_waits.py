"""check_loop_waits.py: busy waits and printf() in the IOmcu's sources.

SPDX-License-Identifier: MIT
"""

import check_loop_waits as clw


def kinds(text, name="x.c"):
    """The (function, kind) of every finding in @p text."""
    return [(f, k) for _, f, k, _ in clw.findings(name, text)]


def test_the_tree_passes():
    """The IOmcu's sources pass with the allow list as committed."""
    assert clw.check() == []


def test_the_reply_wait_of_can_service_is_found():
    """The per-frame wait can_service() held before the transmit queue."""
    text = """
void can_service(uint32_t now)
{
    for (size_t i = 0; i < n; ++i) {
        const uint32_t since = time_us_32();
        bool sent = xl2515_send(&frames[i]);
        while (!sent && time_us_32() - since < IOMCU_CAN_TX_WAIT_US) {
            tight_loop_contents();
            sent = xl2515_send(&frames[i]);
        }
    }
}
"""
    assert kinds(text) == [("can_service", "wait")]


def test_a_wait_in_a_while_a_do_or_a_braceless_while_is_found():
    """A while block, a do block and a while without braces."""
    text = """
static void a(void) { while (x) { tight_loop_contents(); } }
static void b(void) { do { tight_loop_contents(); } while (x); }
static void c(void) { while (x) tight_loop_contents(); }
static void d(void) { while (x) { if (y) { tight_loop_contents(); } } }
"""
    assert kinds(text) == [("a", "wait"), ("b", "wait"), ("c", "wait"),
                           ("d", "wait")]


def test_a_wait_outside_a_while_is_not_found():
    """A for (;;) after a reboot and a plain call are not waits."""
    text = """
static void restart(void) { reboot(); for (;;) { tight_loop_contents(); } }
static void pump(void) { tight_loop_contents(); poll(); }
static void e(void) { if (x) { tight_loop_contents(); } }
"""
    assert kinds(text) == []


def test_printf_and_its_alias_are_found_and_the_rest_is_not():
    """printf(), a macro defined as printf, not snprintf, comments, text."""
    text = """
#define SAY printf
static void f(void)
{
    printf("rcbench-iomcu: one %u\\n", 1u);
    SAY("rcbench-iomcu: two\\n");
    snprintf(buf, sizeof(buf), "x");
    /* printf("in a comment"); */
    puts("printf(");
}
"""
    found = clw.findings("x.c", text)
    assert [(f, k, lit) for _, f, k, lit in found] == [
        ("f", "printf", "rcbench-iomcu: one %u\\n"),
        ("f", "printf", "rcbench-iomcu: two\\n")]


def test_an_entry_covers_by_function_and_literal_and_a_stale_one_fails(
        tmp_path):
    """An entry names a file, a function and the first literal's start."""
    (tmp_path / "m.c").write_text("""
int main(void)
{
    printf("rcbench-iomcu: saved %u\\n", 1u);
    printf("rcbench-iomcu: other\\n");
    while (1) { tight_loop_contents(); }
}
""")
    table = (("m.c", "main", "printf", "rcbench-iomcu: saved", "store"),
             ("m.c", "main", "wait", None, "boot"),
             ("m.c", "gone", "wait", None, "stale"))
    problems = clw.check(tmp_path, table)
    assert problems == [
        'm.c:5: printf() "rcbench-iomcu: other\\n" in main()',
        "ALLOWED: m.c gone() wait  matches nothing"]
