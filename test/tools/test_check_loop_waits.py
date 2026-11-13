"""check_loop_waits.py: busy waits and blocking output in the IOmcu's code.

SPDX-License-Identifier: MIT
"""

import check_loop_waits as clw


def kinds(text):
    """The (function, kind) of every finding in @p text."""
    return [(f, k) for _, f, k, _ in clw.findings(text)]


def test_a_loop_is_named_by_its_keyword_and_condition():
    """main()'s loop is allowed as for(;;), not any loop in main()."""
    text = """
int main(void)
{
    for (;;) {
        now = time_us_32();
        while ( time_us_32() - t < 10u ) { x(); }
    }
}
"""
    found = clw.findings(text)
    assert [(f, k, h) for _, f, k, h in found] == [
        ("main", "clock", "for(;;)"),
        ("main", "clock", "while(time_us_32()-t<10u)")]
    table = (("m.c", "main", "clock", "for(;;)", "the main loop"),)
    assert clw.allowed(table, "m.c", "main", "clock", "for(;;)")
    assert not clw.allowed(table, "m.c", "main", "clock",
                           "while(time_us_32()-t<10u)")


def test_the_tree_passes():
    """The IOmcu's code passes with the allow list as committed."""
    assert clw.check() == []


def test_the_scan_takes_the_modules_the_iomcu_build_adds():
    """firmware/iomcu/src and the shared/ and protocols/ directories."""
    files = {p.relative_to(clw.ROOT).as_posix() for p in clw.sources()}
    assert "firmware/iomcu/src/main.c" in files
    assert "shared/can/can_txq.c" in files
    assert "shared/bench/report_steps.c" in files
    assert "protocols/hitec/rp2350/hitec_line.c" in files
    assert "protocols/i2c_txq/i2c_txq.c" in files
    # The panel's modules are not built into the IOmcu.
    assert not any(f.startswith(("shared/ui/", "shared/gfx/")) for f in files)


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
    assert sorted(set(kinds(text))) == [("can_service", "clock"),
                                        ("can_service", "wait")]


def test_a_wait_in_any_loop_is_found():
    """A while, a do and a for block, and loops without braces."""
    text = """
static void a(void) { while (x) { tight_loop_contents(); } }
static void b(void) { do { tight_loop_contents(); } while (x); }
static void c(void) { while (x) tight_loop_contents(); }
static void d(void) { while (x) { if (y) { tight_loop_contents(); } } }
static void e(void) { for (i = 0; i < n; ++i) { tight_loop_contents(); } }
static void f(void) { for (i = 0; i < n; ++i) tight_loop_contents(); }
static void g(void) { for (;;) if (y) tight_loop_contents(); }
"""
    assert kinds(text) == [(n, "wait") for n in "abcdefg"]


def test_a_wait_outside_a_loop_is_not_found():
    """A plain call and one under an if are not waits."""
    text = """
static void pump(void) { tight_loop_contents(); poll(); }
static void e(void) { if (x) { tight_loop_contents(); } }
"""
    assert kinds(text) == []


def test_an_empty_loop_is_found_and_a_do_loops_tail_is_not():
    """Empty while and for loops spin; the tail of a do loop does not."""
    text = """
static void a(void) { while (!ready()); }
static void b(void) { while (!ready()) { } }
static void c(void) { for (;;); }
static void d(void) { do { step(); } while (busy()); }
static void e(void) { while (busy()) { step(); } }
"""
    assert kinds(text) == [("a", "spin"), ("b", "spin"), ("c", "spin")]


def test_a_loop_that_reads_the_clock_is_found():
    """In the condition, in the body, and in a do loop's tail."""
    text = """
static void a(void) { while (time_us_32() < t) { x(); } }
static void b(void) { while (busy()) { pump(time_us_32()); } }
static void c(void) { do { x(); } while (time_us_64() < t); }
static void d(void) { for (i = 0; i < 8; ++i) { t[i] = get_absolute_time(); } }
static void e(void) { t = time_us_32(); while (busy()) { x(); } }
"""
    assert kinds(text) == [("a", "clock"), ("b", "clock"), ("c", "clock"),
                           ("d", "clock")]


def test_a_sleep_or_busy_wait_call_is_found():
    """busy_wait_*() and sleep_*() anywhere in a function."""
    text = """
static void a(void) { busy_wait_us_32(10u); }
static void b(void) { busy_wait_until(t); }
static void c(void) { sleep_ms(1); }
static void d(void) { sleep_until(t); }
static void e(void) { not_a_sleep_ms(1); }
"""
    assert kinds(text) == [("a", "sleep"), ("b", "sleep"), ("c", "sleep"),
                           ("d", "sleep")]


def test_every_print_and_its_alias_is_found_and_the_rest_is_not():
    """printf, puts, fputs, putchar, a macro alias; not snprintf or text."""
    text = """
#define SAY printf
#define LINE puts
static void f(void)
{
    printf("rcbench-iomcu: one %u\\n", 1u);
    SAY("rcbench-iomcu: two\\n");
    puts("rcbench-iomcu: three");
    LINE("rcbench-iomcu: four");
    fputs("rcbench-iomcu: five\\n", stdout);
    putchar('x');
    snprintf(buf, sizeof(buf), "x");
    /* printf("in a comment"); */
    keep("printf(");
}
"""
    found = clw.findings(text)
    assert [(f, k, lit) for _, f, k, lit in found] == [
        ("f", "print", "rcbench-iomcu: one %u\\n"),
        ("f", "print", "rcbench-iomcu: two\\n"),
        ("f", "print", "rcbench-iomcu: three"),
        ("f", "print", "rcbench-iomcu: four"),
        ("f", "print", "rcbench-iomcu: five\\n"),
        ("f", "print", None)]


def test_an_entry_covers_by_file_function_and_literal_and_stale_fails(
        tmp_path):
    """An entry names a file, a function, a kind, a print's literal."""
    src = tmp_path / "firmware" / "m.c"
    src.parent.mkdir()
    src.write_text("""
int main(void)
{
    printf("rcbench-iomcu: saved %u\\n", 1u);
    printf("rcbench-iomcu: other\\n");
    while (1) { tight_loop_contents(); }
    sleep_ms(1);
}
""")
    table = (
        ("firmware/m.c", "main", "print", "rcbench-iomcu: saved", "store"),
        ("firmware/m.c", "main", "wait", None, "boot"),
        ("firmware/m.c", "gone", "wait", None, "stale"),
    )
    problems = clw.check([src], table, tmp_path)
    assert problems == [
        'firmware/m.c:5: a print "rcbench-iomcu: other\\n" in main()',
        "firmware/m.c:7: a busy_wait_*() or sleep_*() call in main()",
        "ALLOWED: firmware/m.c gone() wait  matches nothing"]
