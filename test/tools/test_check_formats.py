"""check_formats.py: sources that need mbedTLS are skipped without its headers.

SPDX-License-Identifier: MIT
"""

import check_formats as cf


def test_a_source_including_mbedtls_is_skipped_without_the_headers(tmp_path):
    (tmp_path / "a.c").write_text('#include "mbedtls/pk.h"\nint a;\n')
    (tmp_path / "b.c").write_text("#include <mbedtls/ecp.h>\n")
    (tmp_path / "c.c").write_text("#include <stdio.h>\nint c;\n")
    srcs = [tmp_path / n for n in ("a.c", "b.c", "c.c")]
    kept, skipped = cf.without_mbedtls(srcs)
    assert kept == [tmp_path / "c.c"]
    assert skipped == [tmp_path / "a.c", tmp_path / "b.c"]


def test_no_mbedtls_directory_is_none(tmp_path):
    assert cf.mbedtls_include(tmp_path, {}) is None
    assert cf.mbedtls_include(
        tmp_path, {"RCBENCH_MBEDTLS_DIR": str(tmp_path / "x")}) is None


def test_the_named_directory_is_used_when_it_has_the_headers(tmp_path):
    pk = tmp_path / "m" / "include" / "mbedtls"
    pk.mkdir(parents=True)
    (pk / "pk.h").write_text("")
    env = {"RCBENCH_MBEDTLS_DIR": str(tmp_path / "m")}
    assert cf.mbedtls_include(tmp_path, env) == tmp_path / "m" / "include"


def test_a_fetched_tree_is_found_without_the_variable(tmp_path):
    pk = (tmp_path / "test" / "host" / "build" / "_deps" / "mbedtls-src"
          / "include" / "mbedtls")
    pk.mkdir(parents=True)
    (pk / "pk.h").write_text("")
    inc = cf.mbedtls_include(tmp_path, {})
    assert inc is not None and inc.name == "include"
