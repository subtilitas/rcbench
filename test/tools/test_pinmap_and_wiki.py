"""pinmap_check.py and wiki_links.py: the checks one map and one page
exercise."""

import pinmap_check as pm
import wiki_links as wl

# What io_bank0.h gives the two pins the cases use.
FUNCTIONS = {2: {"SPI0_SCLK", "UART0_CTS", "I2C1_SDA"},
             3: {"SPI0_TX", "UART0_RTS", "I2C1_SCL"}}


def chip(*pins):
    return {"chip": "main", "pins": list(pins)}


def pin(gpio, function, **more):
    return {"gpio": gpio, "signal": f"signal {gpio}", "function": function,
            **more}


def test_a_map_the_silicon_can_serve_has_no_failure():
    fails, used = pm.check_chip(chip(
        pin(2, "I2C1_SDA", bus="sensors"),
        pin(3, "I2C1_SCL", bus="sensors"),
        pin(40, "ADC"), pin(10, "SIO")), FUNCTIONS)
    assert fails == [] and used == 4


def test_a_function_the_pin_does_not_have_fails():
    fails, _ = pm.check_chip(chip(pin(2, "UART1_TX", bus="u")), FUNCTIONS)
    assert fails == ["main GPIO2 (signal 2): UART1_TX is not a function of "
                     "GPIO2"]


def test_a_pin_used_twice_fails():
    fails, used = pm.check_chip(chip(pin(10, "SIO"), pin(10, "SIO")),
                                FUNCTIONS)
    assert fails == ["main GPIO10 (signal 10): also used by signal 10"]
    assert used == 1


def test_adc_outside_its_pins_and_an_external_signal_past_39_fail():
    fails, _ = pm.check_chip(chip(pin(5, "ADC"),
                                  pin(41, "SIO", external=True)), FUNCTIONS)
    assert fails == [
        "main GPIO5 (signal 5): ADC needs GPIO 40 to 47",
        "main GPIO41 (signal 41): an external signal needs a "
        "fault-tolerant pin, GPIO 0 to 39"]


def test_an_i2c_bus_of_one_pin_and_a_bus_of_two_instances_fail():
    fails, _ = pm.check_chip(chip(pin(2, "I2C1_SDA", bus="a")), FUNCTIONS)
    assert fails == ["main bus a: an I2C bus has 2 pins, not 1"]
    fails, _ = pm.check_chip(chip(pin(2, "SPI0_SCLK", bus="b"),
                                  pin(3, "UART0_RTS", bus="b")), FUNCTIONS)
    assert fails == ["main bus b: mixes SPI0, UART0"]


def test_a_pio_pin_names_its_protocol_and_a_block_fits_one_window():
    fails, _ = pm.check_chip(chip(pin(4, "PIO0"),
                                  pin(40, "PIO0", protocol="dshot")),
                             FUNCTIONS)
    assert fails == [
        "main GPIO4 (signal 4): a PIO pin names no protocol",
        "main PIO0: GPIO 4 to 40 do not fit one window, 0 to 31 or 16 to 47"]


def test_a_page_link_loses_its_extension_and_keeps_its_anchor():
    assert wl.rewrite("[a](Screens.md) [b](Link-de.md#protokoll)") == (
        "[a](Screens) [b](Link-de#protokoll)")


def test_urls_images_and_absolute_paths_are_left_alone():
    text = "[a](https://example.com/x.md) ![i](img/a.png) [r](/abs/x.md)"
    assert wl.rewrite(text) == text


def test_the_rewrite_changes_only_the_copy_it_is_given(tmp_path):
    page = tmp_path / "Home.md"
    page.write_text("[a](Screens.md)\n", encoding="utf-8")
    assert wl.main(["wiki_links.py", "--check", str(tmp_path)]) == 0
    assert page.read_text(encoding="utf-8") == "[a](Screens.md)\n"
    assert wl.main(["wiki_links.py", str(tmp_path)]) == 0
    assert page.read_text(encoding="utf-8") == "[a](Screens)\n"
