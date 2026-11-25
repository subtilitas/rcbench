"""The literal-only LZMA encoder of protocols/supvan against liblzma.

test/host/lzlit_cli.c and protocols/supvan/lzlit.c are compiled here and
run on fixed and seeded random inputs.  Each stream is decoded with
Python's lzma (liblzma, an independent decoder) and compared with
tools/lzlit.py's output byte for byte.  The fixtures of the C suites are
held to tools/gen_supvan_fixtures.py.

SPDX-License-Identifier: MIT
"""

import lzma
import pathlib
import random
import shutil
import subprocess

import pytest

import gen_supvan_fixtures
import lzlit
from conftest import skip_or_fail

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
SUPVAN = REPO / "protocols" / "supvan"


def _inputs():
    """Return the inputs by name: edge cases, a bitmap and seeded random.

    Returns:
        A dict of name to bytes.
    """
    rng = random.Random(1)
    return {
        "empty": b"",
        "one": b"\x00",
        "ff": b"\xff" * 5000,
        "zeros12288": bytes(12288),
        "pattern": bytes(range(256)) * 16 + bytes(4096),
        "random20000": bytes(rng.getrandbits(8) for _ in range(20000)),
        "random65536": bytes(rng.getrandbits(8) for _ in range(65536)),
        "bitmap": bytes(0xFF if (i // 48) % 9 < 4 else 0x00
                        for i in range(12288)),
        "label": gen_supvan_fixtures.fixtures()["label-bufs.bin"],
    }


@pytest.fixture(scope="module")
def cli(tmp_path_factory):
    """Build lzlit_cli with the C compiler.

    Args:
        tmp_path_factory: pytest's factory of temporary folders.

    Returns:
        The program's path.
    """
    cc = shutil.which("cc") or shutil.which("gcc")
    if cc is None:
        skip_or_fail("cc is not on PATH")
    exe = tmp_path_factory.mktemp("lzlit") / "lzlit_cli"
    subprocess.run(
        [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-I", str(SUPVAN / "include"),
         str(REPO / "test" / "host" / "lzlit_cli.c"),
         str(SUPVAN / "lzlit.c"), "-o", str(exe)],
        check=True)
    return exe


@pytest.mark.parametrize("name", sorted(_inputs()))
def test_the_c_stream_is_lzlit_py_and_liblzma_decodes_it(cli, tmp_path,
                                                         name):
    """The C stream equals lzlit.py's, stays in its bound and decodes."""
    raw = _inputs()[name]
    src = tmp_path / "in.bin"
    dst = tmp_path / "out.lz"
    src.write_bytes(raw)
    subprocess.run([str(cli), str(src), str(dst)], check=True)
    z = dst.read_bytes()
    assert z == lzlit.encode(raw)
    assert len(z) <= 13 + len(raw) + len(raw) // 8 + 16
    assert lzma.decompress(z, format=lzma.FORMAT_ALONE) == raw


def test_the_header_is_props_dictionary_and_exact_length():
    """Props 0x5D, dictionary 8,192, the length in 64 bits."""
    z = lzlit.encode(bytes(300))
    assert z[:13] == bytes([0x5D, 0x00, 0x20, 0x00, 0x00,
                            0x2C, 0x01, 0, 0, 0, 0, 0, 0])


def test_the_fixtures_are_what_the_generator_writes():
    """test/host/fixtures/supvan holds the generator's bytes."""
    for name, body in gen_supvan_fixtures.fixtures().items():
        assert (gen_supvan_fixtures.OUT / name).read_bytes() == body, name
