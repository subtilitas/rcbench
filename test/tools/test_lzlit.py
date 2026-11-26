"""The literal-only LZMA encoder of protocols/supvan against liblzma.

test/host/lzlit_cli.c and protocols/supvan/lzlit.c are compiled here and
run on fixed and seeded random inputs.  Each stream is decoded with
Python's lzma (liblzma, an independent decoder) and compared with
tools/lzlit.py's output byte for byte.  The generated fixtures of the C
suites are held to tools/gen_supvan_fixtures.py, and the station's
captures (H0) to the generator's rules: the full LZMA stream decodes to the
print buffers, the buffers and data frames rebuild from the label's image
and stream, and every captured write is a command or a frame fragment.

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
        "label": (gen_supvan_fixtures.OUT / "label-bufs.bin").read_bytes(),
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


def _captured(name):
    """Return a capture of test/host/fixtures/supvan.

    Args:
        name: the file name.

    Returns:
        Its bytes.
    """
    return (gen_supvan_fixtures.OUT / name).read_bytes()


def test_the_captured_stream_decodes_to_the_captured_buffers():
    """label.lz is full LZMA; decoded it is label-bufs.bin."""
    z = _captured("label.lz")
    assert z[:13] == bytes([0x5D, 0x00, 0x20, 0x00, 0x00,
                            0x00, 0x30, 0, 0, 0, 0, 0, 0])
    raw = lzma.decompress(z, format=lzma.FORMAT_ALONE)
    assert raw == _captured("label-bufs.bin")


def test_the_captured_buffers_rebuild_from_the_labels_image():
    """print_buffer() of the image gives label-bufs.bin, header and sum."""
    g = gen_supvan_fixtures
    bits = g.captured_bits()
    rebuilt = b"".join(g.print_buffer(bits, g.LABEL_COLS, i)
                       for i in range(g.LABEL_BUFS))
    assert rebuilt == _captured("label-bufs.bin")


def test_the_captured_frames_rebuild_from_the_stream():
    """data_frames() of label.lz gives label-dataframes.bin."""
    frames = gen_supvan_fixtures.data_frames(_captured("label.lz"))
    assert frames == _captured("label-dataframes.bin")


def test_every_captured_write_is_a_command_or_a_fragment():
    """label-frames.bin: 29 records, 16-byte commands with their sum and
    128-byte fragments that join to the data frames."""
    f = _captured("label-frames.bin")
    recs = []
    o = 0
    while o < len(f):
        n = int.from_bytes(f[o:o + 2], "little")
        recs.append(f[o + 2:o + 2 + n])
        o += 2 + n
    assert o == len(f)
    assert len(recs) == 29
    frags = b"".join(r for r in recs if len(r) == 128)
    assert frags == _captured("label-dataframes.bin")
    cmds = [r for r in recs if len(r) != 128]
    assert len(cmds) == 21
    for r in cmds:
        assert len(r) == 16
        assert r[:7] == bytes([0x7E, 0x5A, 0x0C, 0x00, 0x10, 0x01, 0xAA])
        assert int.from_bytes(r[8:10], "little") == sum(r[10:16])
