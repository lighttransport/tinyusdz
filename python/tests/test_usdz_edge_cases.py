"""USDZ packing edge cases beyond the basic shape covered in
test_usdz_packing.py.

AOUSD Core Spec 17.2 limits archive entries to a fixed extension
allowlist (usd/usda/usdc + png/jpg/jpeg/exr/avif + m4a/mp3/wav).
Tests below stay inside that allowlist; entries with other extensions
are silently skipped (with a warning), which is intended behavior.
"""
import struct
import zipfile

import pytest

import tinyusdz


PNG_HEADER = bytes.fromhex("89504E470D0A1A0A0000000D49484452")


def _stage_with_asset(name="X"):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name=name)
    p.set_attribute("a", "tex.png", dtype="asset")
    s.add_root_prim(p)
    return s


def test_usdz_with_many_assets(tmp_path):
    """20 assets, all aligned, all readable."""
    s = _stage_with_asset()
    out = tmp_path / "many.usdz"
    assets = {f"tex_{i:02d}.png": PNG_HEADER + bytes([i]) * 32
              for i in range(20)}
    s.save(str(out), assets=assets)
    with zipfile.ZipFile(out, "r") as z:
        names = z.namelist()
        assert names[0] in ("root.usdc", "root.usda")
        for k, v in assets.items():
            assert k in names
            assert z.read(k) == v


def test_usdz_with_large_asset(tmp_path):
    """A single ~1 MB asset survives intact."""
    s = _stage_with_asset()
    out = tmp_path / "big.usdz"
    blob = PNG_HEADER + b"X" * (1024 * 1024 + 17)  # non-aligned size
    s.save(str(out), assets={"big.png": blob})
    with zipfile.ZipFile(out, "r") as z:
        assert z.read("big.png") == blob


def test_usdz_alignment_holds_for_many_assets(tmp_path):
    """64-byte alignment must hold for every entry's data offset
    regardless of how many assets are packed."""
    s = _stage_with_asset()
    out = tmp_path / "many.usdz"
    # Mix of allowed extensions and varying name lengths to stress
    # the alignment-padding computation.
    assets = {
        "a.png": bytes([0] * 10),
        "bb.jpg": bytes([1] * 50),
        "ccc.wav": bytes([2] * 100),
        "dddd.mp3": bytes([3] * 300),
        "eeeee.exr": bytes([4] * 1000),
        "ffffff.usdc": bytes([5] * 2000),
        "ggggggg.png": bytes([6] * 17),
        "hhhhhhhh.jpeg": bytes([7] * 33),
    }
    s.save(str(out), assets=assets)
    data = out.read_bytes()
    offset = 0
    seen = 0
    while offset < len(data) - 30:
        if data[offset:offset+4] != b"PK\x03\x04":
            break
        name_len = struct.unpack_from("<H", data, offset + 26)[0]
        extra_len = struct.unpack_from("<H", data, offset + 28)[0]
        compressed_size = struct.unpack_from("<I", data, offset + 18)[0]
        data_offset = offset + 30 + name_len + extra_len
        assert data_offset % 64 == 0, (
            f"entry {seen} ({name_len}-byte name) data_offset={data_offset} "
            f"not 64-byte aligned")
        seen += 1
        offset = data_offset + compressed_size
    assert seen >= len(assets) + 1


def test_usdz_empty_asset_blob_accepted(tmp_path):
    """A zero-byte asset is a degenerate but legal archive entry."""
    s = _stage_with_asset()
    out = tmp_path / "z.usdz"
    s.save(str(out), assets={"empty.png": b""})
    with zipfile.ZipFile(out, "r") as z:
        assert z.read("empty.png") == b""


def test_usdz_root_layer_position_is_first(tmp_path):
    """Per AOUSD spec the root layer must be the first entry."""
    s = _stage_with_asset()
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={
        "z_late.png": b"end",
        "a_early.png": b"start",
    })
    with zipfile.ZipFile(out, "r") as z:
        names = z.namelist()
        # Root layer first, regardless of dict-iteration order on
        # additional assets.
        assert names[0] in ("root.usdc", "root.usda")
        # Additional assets appear after the root.
        assert "z_late.png" in names[1:]
        assert "a_early.png" in names[1:]


def test_usdz_assets_none_equivalent_to_no_kwarg(tmp_path):
    """Passing assets=None is the same as not passing the kwarg."""
    s = _stage_with_asset()
    out_a = tmp_path / "a.usdz"
    out_b = tmp_path / "b.usdz"
    s.save(str(out_a))
    s.save(str(out_b), assets=None)
    # Same number of entries, root layer present in both.
    with zipfile.ZipFile(out_a, "r") as za, zipfile.ZipFile(out_b, "r") as zb:
        assert za.namelist() == zb.namelist()


def test_usdz_disallowed_extensions_are_skipped(tmp_path):
    """Per AOUSD Core Spec 17.2, only certain extensions are allowed.
    Entries with other extensions are silently skipped (the writer
    emits a warning but does not fail). Fence the spec compliance."""
    s = _stage_with_asset()
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={
        "ok.png": PNG_HEADER,
        "bad.bin": b"should be skipped",
        "another.txt": b"also skipped",
    })
    with zipfile.ZipFile(out, "r") as z:
        names = z.namelist()
        assert "ok.png" in names
        assert "bad.bin" not in names
        assert "another.txt" not in names
