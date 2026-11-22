"""sign_images.sh: the release workflow's signing and verify steps.

The cases sign a fake release with a throw-away P-256 key made here, and
verify it against the matching public key and against another one.  The
failures under test: a release published with an image that has no
signature or one that does not verify, a signing step that runs without a
key, and a key file left behind.

SPDX-License-Identifier: MIT
"""

import os
import pathlib
import shutil
import subprocess
import zipfile

import pytest

TOOL = pathlib.Path(__file__).resolve().parents[2] / "tools" / "sign_images.sh"

pytestmark = pytest.mark.skipif(
    shutil.which("openssl") is None or shutil.which("unzip") is None,
    reason="needs openssl and unzip")


def keypair(folder, name):
    """Make a P-256 key pair.

    Args:
        folder: Where the two PEM files go.
        name: Their stem.

    Returns:
        (the private key's PEM text, the public key's path).
    """
    prv = folder / f"{name}.pem"
    pub = folder / f"{name}.pub.pem"
    subprocess.run(["openssl", "ecparam", "-name", "prime256v1", "-genkey",
                    "-noout", "-out", str(prv)], check=True)
    subprocess.run(["openssl", "ec", "-in", str(prv), "-pubout", "-out",
                    str(pub)], check=True, capture_output=True)
    return prv.read_text(), pub


def run(*args, key=None, tmp=None):
    """Run the tool.

    Args:
        *args: Its arguments.
        key: The value of SIGNING_KEY, or None to leave it unset.
        tmp: TMPDIR for the run, or None.

    Returns:
        The finished process.
    """
    env = {k: v for k, v in os.environ.items() if k != "SIGNING_KEY"}
    if key is not None:
        env["SIGNING_KEY"] = key
    if tmp is not None:
        env["TMPDIR"] = str(tmp)
    return subprocess.run(["bash", str(TOOL), *args], env=env,
                          capture_output=True, text=True)


def artifacts(root):
    """Write build artifacts as the release's build jobs hand them over.

    Args:
        root: The folder of the artifacts.

    Returns:
        The images written, relative to root.
    """
    files = {
        "panel/rcbench-panel.bin": b"\xe9panel app",
        "panel/rcbench-panel-merged.bin": b"\xe9panel merged",
        "panel/rcbench-panel.elf": b"\x7fELF not an image",
        "iomcu/rcbench-iomcu.bin": b"iomcu bin",
        "iomcu/rcbench-iomcu-install.uf2": b"UF2\nuf2 install",
        "c6/rcbench-c6-2.13.0.bin": b"\xe9c6",
    }
    for name, data in files.items():
        p = root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    return sorted(n for n in files if not n.endswith(".elf"))


def package(root, dist):
    """Package the artifacts as the release's Package step does.

    Args:
        root: The signed artifacts.
        dist: The folder of the release assets.
    """
    dist.mkdir()
    for board in ("panel", "iomcu"):
        with zipfile.ZipFile(dist / f"rcbench-{board}-v0.24.0.zip", "w") as z:
            for p in sorted((root / board).iterdir()):
                z.write(p, p.name)
    for name in ("rcbench-panel.bin", "rcbench-panel.bin.sig"):
        shutil.copy(root / "panel" / name,
                    dist / name.replace("panel", "panel-p4-v0.24.0", 1))
    for name in ("rcbench-c6-2.13.0.bin", "rcbench-c6-2.13.0.bin.sig"):
        shutil.copy(root / "c6" / name, dist / name)


@pytest.fixture
def release(tmp_path):
    """A signed and packaged release and its key pair."""
    prv, pub = keypair(tmp_path, "release")
    root = tmp_path / "artifacts"
    images = artifacts(root)
    tmp = tmp_path / "tmp"
    tmp.mkdir()
    done = run("sign", str(root), key=prv, tmp=tmp)
    assert done.returncode == 0, done.stderr
    assert "signed 5 images" in done.stdout
    # No key file is left behind.
    assert list(tmp.iterdir()) == []
    for name in images:
        sig = (root / (name + ".sig")).read_bytes()
        assert 8 <= len(sig) <= 72 and sig[0] == 0x30
    assert not (root / "panel" / "rcbench-panel.elf.sig").exists()
    dist = tmp_path / "dist"
    package(root, dist)
    return tmp_path, pub, root, dist


def test_a_release_signed_with_the_key_verifies(release):
    """Every image, beside the assets and inside the archives, verifies."""
    _, pub, _, dist = release
    done = run("verify", str(pub), str(dist))
    assert done.returncode == 0, done.stdout
    # Two in each archive, two beside them.
    assert "6 images signed" in done.stdout
    assert "ok rcbench-iomcu-v0.24.0.zip:rcbench-iomcu-install.uf2" \
        in done.stdout


def test_another_public_key_fails(release):
    """A secret whose public half is not the committed key fails."""
    tmp_path, _, _, dist = release
    _, other = keypair(tmp_path, "other")
    done = run("verify", str(other), str(dist))
    assert done.returncode == 1
    assert "does not verify" in done.stdout


def test_an_image_without_its_signature_fails(release):
    """An image without its .sig fails, and so does a .sig without its image.

    The image is an asset or a member of an archive.
    """
    _, pub, root, dist = release
    (dist / "rcbench-panel-p4-v0.24.0.bin.sig").unlink()
    done = run("verify", str(pub), str(dist))
    assert done.returncode == 1
    assert "rcbench-panel-p4-v0.24.0.bin has no signature" in done.stdout
    shutil.copy(root / "panel" / "rcbench-panel.bin.sig",
                dist / "rcbench-panel-p4-v0.24.0.bin.sig")
    assert run("verify", str(pub), str(dist)).returncode == 0
    # An image added to an archive after the signing.
    with zipfile.ZipFile(dist / "rcbench-iomcu-v0.24.0.zip", "a") as z:
        z.writestr("rcbench-iomcu-pt.uf2", b"late")
    done = run("verify", str(pub), str(dist))
    assert done.returncode == 1
    assert "rcbench-iomcu-pt.uf2 has no signature" in done.stdout
    # A signature whose image is gone.
    (dist / "stray.bin.sig").write_bytes(b"0")
    done = run("verify", str(pub), str(dist))
    assert "stray.bin.sig has no image" in done.stdout


def test_a_changed_image_fails(release):
    """One byte changed after the signing fails the verify."""
    _, pub, _, dist = release
    p = dist / "rcbench-c6-2.13.0.bin"
    data = bytearray(p.read_bytes())
    data[0] ^= 1
    p.write_bytes(bytes(data))
    done = run("verify", str(pub), str(dist))
    assert done.returncode == 1
    assert "signature of rcbench-c6-2.13.0.bin does not verify" in done.stdout


def test_signing_without_a_key_fails_and_signs_nothing(tmp_path):
    """An unset or empty secret, and one that is no key, sign nothing."""
    root = tmp_path / "artifacts"
    artifacts(root)
    for key in (None, ""):
        done = run("sign", str(root), key=key)
        assert done.returncode == 1
        assert "RCBENCH_IMAGE_SIGNING is not set" in done.stderr
    tmp = tmp_path / "tmp"
    tmp.mkdir()
    done = run("sign", str(root), key="not a key", tmp=tmp)
    assert done.returncode != 0
    assert list(tmp.iterdir()) == []
    assert not list(root.rglob("*.sig")) or all(
        p.stat().st_size == 0 for p in root.rglob("*.sig"))


def test_usage_and_empty_folders(tmp_path):
    """A wrong call is a usage error; a folder without images fails."""
    assert run().returncode == 2
    assert run("sign").returncode == 2
    assert run("verify", "x").returncode == 2
    assert run("other", "x").returncode == 2
    empty = tmp_path / "empty"
    empty.mkdir()
    prv, pub = keypair(tmp_path, "k")
    assert run("sign", str(empty), key=prv).returncode == 1
    assert run("verify", str(pub), str(empty)).returncode == 1
    assert run("verify", str(tmp_path / "none.pem"), str(empty)).returncode \
        == 1
