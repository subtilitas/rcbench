"""check_sdkconfig.py: a local sdkconfig against the defaults files.

SPDX-License-Identifier: MIT
"""

import check_sdkconfig as cs

DEFAULTS = """\
# The panel's settings.
CONFIG_IDF_TARGET="esp32p4"
CONFIG_HTTPD_MAX_URI_LEN=1280
CONFIG_PARTITION_TABLE_OFFSET=0xC000
CONFIG_SPIRAM_MEMTEST=n
CONFIG_BT_ENABLED=y
"""

# What ESP-IDF writes from DEFAULTS: every option, n as a comment.
GENERATED = """\
CONFIG_IDF_TARGET="esp32p4"
CONFIG_HTTPD_MAX_URI_LEN=1280
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_PARTITION_TABLE_OFFSET=0xc000
# CONFIG_SPIRAM_MEMTEST is not set
CONFIG_BT_ENABLED=y
"""


def project(tmp_path, defaults=DEFAULTS, config=GENERATED, extra=None):
    """Write a project folder; return the arguments that check it."""
    (tmp_path / "sdkconfig.defaults").write_text(defaults)
    if config is not None:
        (tmp_path / "sdkconfig").write_text(config)
    if extra is not None:
        (tmp_path / "sdkconfig.defaults.esp32p4").write_text(extra)
    return ["--check", "--project", str(tmp_path)]


def test_a_generated_sdkconfig_passes(tmp_path, capsys):
    """Hex by value, n as `is not set`, other options not compared."""
    assert cs.main(project(tmp_path)) == 0
    assert "holds the 5 setting(s)" in capsys.readouterr().out


def test_an_older_value_in_the_local_file_fails(tmp_path, capsys):
    """A default the file holds at another value fails."""
    old = GENERATED.replace("URI_LEN=1280", "URI_LEN=512")
    assert cs.main(project(tmp_path, config=old)) == 1
    out = capsys.readouterr().out
    assert "CONFIG_HTTPD_MAX_URI_LEN: defaults 1280, sdkconfig 512" in out


def test_a_default_missing_from_the_file_fails(tmp_path, capsys):
    """A default the file does not hold at all fails."""
    gone = GENERATED.replace("CONFIG_BT_ENABLED=y\n", "")
    assert cs.main(project(tmp_path, config=gone)) == 1
    assert ("CONFIG_BT_ENABLED: defaults y, not in the sdkconfig"
            in capsys.readouterr().out)


def test_an_n_default_set_in_the_file_fails(tmp_path, capsys):
    """An option the defaults switch off and the file switches on fails."""
    on = GENERATED.replace("# CONFIG_SPIRAM_MEMTEST is not set",
                           "CONFIG_SPIRAM_MEMTEST=y")
    assert cs.main(project(tmp_path, config=on)) == 1
    assert ("CONFIG_SPIRAM_MEMTEST: defaults n, sdkconfig y"
            in capsys.readouterr().out)


def test_an_n_default_left_out_of_the_file_passes(tmp_path):
    """Kconfig leaves out an option whose dependencies are off."""
    out = GENERATED.replace("# CONFIG_SPIRAM_MEMTEST is not set\n", "")
    assert cs.main(project(tmp_path, config=out)) == 0


def test_the_target_file_overrides_the_defaults(tmp_path, capsys):
    """sdkconfig.defaults.<target> is read after sdkconfig.defaults."""
    extra = "CONFIG_HTTPD_MAX_URI_LEN=2048\n"
    assert cs.main(project(tmp_path, extra=extra)) == 1
    assert ("CONFIG_HTTPD_MAX_URI_LEN: defaults 2048, sdkconfig 1280"
            in capsys.readouterr().out)


def test_no_sdkconfig_passes(tmp_path, capsys):
    """A checkout without one builds from the defaults."""
    assert cs.main(project(tmp_path, config=None)) == 0
    assert "the build generates it" in capsys.readouterr().out


def test_another_sdkconfig_can_be_named(tmp_path):
    """A build folder's sdkconfig is compared with the same defaults."""
    args = project(tmp_path, config=None)
    other = tmp_path / "build" / "sdkconfig"
    other.parent.mkdir()
    other.write_text(GENERATED.replace("URI_LEN=1280", "URI_LEN=512"))
    assert cs.main(args + ["--sdkconfig", str(other)]) == 1


def test_a_named_sdkconfig_that_is_missing_fails(tmp_path):
    """CI names the file the build writes; without it nothing is checked."""
    args = project(tmp_path, config=None)
    missing = str(tmp_path / "build" / "sdkconfig")
    assert cs.main(args + ["--sdkconfig", missing]) == 1


def test_the_panels_defaults_read_without_a_problem():
    """Every line of the committed defaults is a setting or a comment."""
    path = cs.DEFAULT_PROJECT / "sdkconfig.defaults"
    lines = [line.strip() for line in path.read_text().splitlines()]
    settings = [line for line in lines if line and not line.startswith("#")]
    assert len(cs.read_config(path)) == len(settings)
