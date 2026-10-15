# SPDX-License-Identifier: MIT
"""tools/patch_rgb_driver.py: every restart of ESP-IDF's RGB panel driver
becomes the start of boot, and a driver the patches do not fit exactly
once stops the build instead of building without the fix."""

import os
import pathlib
import re

import pytest

import patch_rgb_driver as p

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
HEADER = (REPO / "firmware" / "panel" / "components" / "display" / "include"
          / "rgb_panel_rcbench.h")

# The three places the tool patches, as ESP-IDF v5.4's
# components/esp_lcd/rgb/esp_lcd_panel_rgb.c has them, with two long
# lines folded.
DRIVER = """\
static void lcd_rgb_panel_try_restart_transmission(esp_rgb_panel_t *panel)
{
    int bb_size_px = panel->bb_size / (panel->fb_bits_per_pixel / 8);
    bool do_restart = false;
#if CONFIG_LCD_RGB_RESTART_IN_VSYNC
    do_restart = true;
#else
    portENTER_CRITICAL_ISR(&panel->spinlock);
    if (panel->bb_eof_count < panel->expect_eof_count) {
        do_restart = true;
    }
    panel->bb_eof_count = 0;
    portEXIT_CRITICAL_ISR(&panel->spinlock);
#endif // CONFIG_LCD_RGB_RESTART_IN_VSYNC

    if (!do_restart) {
        return;
    }

    gdma_reset(panel->dma_chan);
    lcd_ll_fifo_reset(panel->hal.dev);
#if RGB_LCD_NEEDS_SEPARATE_RESTART_LINK
    // restart the DMA by a special DMA node
    gdma_start(panel->dma_chan,
               gdma_link_get_head_addr(panel->dma_restart_link));
#endif
}

static void lcd_rgb_panel_start_transmission(esp_rgb_panel_t *rgb_panel)
{
    // reset FIFO of DMA and LCD, in case there remains old frame data
    gdma_reset(rgb_panel->dma_chan);
    lcd_ll_stop(rgb_panel->hal.dev);
    lcd_ll_reset(rgb_panel->hal.dev);
    lcd_ll_fifo_reset(rgb_panel->hal.dev);
    esp_rom_delay_us(1);
    // start LCD engine
    lcd_ll_start(rgb_panel->hal.dev);
}
"""


def body(text, name):
    """The body of the C function @p name in @p text."""
    head = text.index(name + "(esp_rgb_panel_t")
    start = text.index("{", head)
    return text[start:text.index("\n}\n", start)]


def test_the_prelude_comes_first_and_the_rest_is_the_driver():
    out = p.patch(DRIVER)
    assert out.startswith(p.PRELUDE)
    restored = out[len(p.PRELUDE):]
    for _, old, new in p.PATCHES:
        assert restored.count(new) == 1
        restored = restored.replace(new, old)
    assert restored == DRIVER


def test_a_restart_is_the_start_of_boot_and_never_the_restart_link():
    restart = body(p.patch(DRIVER), "lcd_rgb_panel_try_restart_transmission")
    check = restart.index("if (!do_restart)")
    clean = restart.index("lcd_rgb_panel_start_transmission(panel);")
    link = restart.index("gdma_start(panel->dma_chan,")
    assert check < clean < link
    # Nothing between the clean start and the restart link but its return.
    between = restart[clean:link]
    assert "return;" in between
    assert "gdma_start" not in between
    assert restart.index("rcbench_lcd_rgb_restarts++") < clean


def test_the_start_drops_the_old_stream_before_it_refills():
    start = body(p.patch(DRIVER), "lcd_rgb_panel_start_transmission")
    reset = start.index("gdma_reset(rgb_panel->dma_chan);")
    drop = start.index("rcbench_gdma_drop_eof(rgb_panel->dma_chan);")
    zero = start.index("rgb_panel->bb_eof_count = 0;")
    stop = start.index("lcd_ll_stop(rgb_panel->hal.dev);")
    assert reset < drop < stop
    assert reset < zero < stop


def test_the_start_clears_a_vblank_its_own_start_raised():
    start = body(p.patch(DRIVER), "lcd_rgb_panel_start_transmission")
    go = start.index("lcd_ll_start(rgb_panel->hal.dev);")
    clear = start.index("LCD_LL_EVENT_VSYNC_END")
    assert go < clear
    assert start.rstrip().endswith("LCD_LL_EVENT_VSYNC_END);")


@pytest.mark.parametrize("name", [n for n, _, _ in p.PATCHES])
def test_a_patch_whose_text_is_missing_stops_the_build(name):
    old = next(o for n, o, _ in p.PATCHES if n == name)
    with pytest.raises(p.PatchError, match=f"'{name}'.* 0 times"):
        p.patch(DRIVER.replace(old, ""))


@pytest.mark.parametrize("name", [n for n, _, _ in p.PATCHES])
def test_a_patch_whose_text_occurs_twice_stops_the_build(name):
    old = next(o for n, o, _ in p.PATCHES if n == name)
    with pytest.raises(p.PatchError, match=f"'{name}'.* 2 times"):
        p.patch(DRIVER + "\n" + old)


def test_the_prelude_refuses_an_iram_safe_isr_and_another_target():
    iram = p.PRELUDE.index("#if CONFIG_LCD_RGB_ISR_IRAM_SAFE\n")
    assert p.PRELUDE.index("#error", iram) < p.PRELUDE.index("#endif", iram)
    assert "#if !CONFIG_IDF_TARGET_ESP32S3\n#error" in p.PRELUDE


def test_the_prelude_refuses_a_restart_on_every_vblank():
    # With the clean start on every frame the restart count would rise
    # 39 times a second and the console line with it.
    vsync = p.PRELUDE.index("#if CONFIG_LCD_RGB_RESTART_IN_VSYNC\n")
    assert p.PRELUDE.index("#error", vsync) < p.PRELUDE.index("#endif", vsync)


def test_the_prelude_clears_the_tx_end_of_buffer_interrupt():
    drop = p.PRELUDE[p.PRELUDE.index("static void rcbench_gdma_drop_eof"):]
    drop = drop[:drop.index("\n}\n")]
    call = drop.index("gdma_ll_tx_clear_interrupt_status(&GDMA,")
    assert drop.index("GDMA_LL_EVENT_TX_EOF", call) > call
    for wrong in ("GDMA_LL_EVENT_TX_DONE", "gdma_ll_rx_", "RX_"):
        assert wrong not in drop


def test_the_header_declares_what_the_prelude_defines():
    header = HEADER.read_text(encoding="utf-8")
    assert '#include "rgb_panel_rcbench.h"' in p.PRELUDE
    assert "volatile uint32_t rcbench_lcd_rgb_restarts;" in p.PRELUDE
    assert "extern volatile uint32_t rcbench_lcd_rgb_restarts;" in header
    # Every renamed function gets the rcbench_ prefix in place of esp_.
    names = [line.split()[1:3] for line in header.replace("\\\n", "")
             .splitlines() if line.startswith("#define esp_lcd_")]
    assert len(names) == 7
    for old, new in names:
        assert new == "rcbench_" + old[len("esp_"):]


def test_an_unchanged_output_is_not_written_again(tmp_path):
    out = tmp_path / "sub" / "rgb.c"
    assert p.write_if_changed(out, "a")
    stamp = out.stat().st_mtime_ns
    assert not p.write_if_changed(out, "a")
    assert out.stat().st_mtime_ns == stamp
    assert p.write_if_changed(out, "b")
    assert out.read_text(encoding="utf-8") == "b"


def test_main_writes_the_copy(tmp_path):
    src = tmp_path / "esp_lcd_panel_rgb.c"
    src.write_text(DRIVER, encoding="utf-8")
    out = tmp_path / "out.c"
    assert p.main(["--source", str(src), "--out", str(out)]) == 0
    assert out.read_text(encoding="utf-8") == p.patch(DRIVER)


def test_main_fails_and_writes_nothing_on_a_driver_it_cannot_patch(
        tmp_path, capsys):
    src = tmp_path / "esp_lcd_panel_rgb.c"
    src.write_text("int x;\n", encoding="utf-8")
    out = tmp_path / "out.c"
    assert p.main(["--source", str(src), "--out", str(out)]) == 1
    assert not out.exists()
    assert "patch 'restart'" in capsys.readouterr().err


def test_main_fails_on_a_missing_source(tmp_path, capsys):
    out = tmp_path / "out.c"
    assert p.main(["--source", str(tmp_path / "none.c"),
                   "--out", str(out)]) == 1
    assert not out.exists()
    assert "patch_rgb_driver:" in capsys.readouterr().err


@pytest.mark.skipif(not os.environ.get("IDF_PATH"),
                    reason="IDF_PATH is not set")
def test_the_real_driver_patches_and_every_public_function_is_renamed():
    src = (pathlib.Path(os.environ["IDF_PATH"]) / "components" / "esp_lcd"
           / "rgb" / "esp_lcd_panel_rgb.c")
    text = src.read_text(encoding="utf-8")
    p.patch(text)
    public = set(re.findall(r"^esp_err_t (esp_lcd_\w+)\(", text, re.M))
    header = HEADER.read_text(encoding="utf-8").replace("\\\n", "")
    renamed = {line.split()[1] for line in header.splitlines()
               if line.startswith("#define esp_lcd_")}
    assert public == renamed
