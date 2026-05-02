"""USDZ packing with extra assets, plus the asset-path rewrite helper.

USDZ format requirements (AOUSD Core Spec section 17):
- Uncompressed (Store-only) ZIP entries.
- 64-byte alignment on each file's data offset.
- Root layer is the first archive entry.
- Standard ZIP magic (PK\\x03\\x04).
"""
import os
import struct
import zipfile

import pytest
import tinyusdz


PNG_HEADER = bytes.fromhex("89504E470D0A1A0A0000000D49484452")
JPEG_HEADER = bytes.fromhex("FFD8FFE000104A464946")
WAV_HEADER = b"RIFFxxxxWAVE"


def _stage_with_asset(name="X"):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name=name)
    p.set_attribute("a", "tex.png", dtype="asset")
    s.add_root_prim(p)
    return s


def test_usdz_save_no_assets(tmp_path):
    s = _stage_with_asset()
    out = tmp_path / "x.usdz"
    s.save(str(out))
    assert out.exists() and out.stat().st_size > 0
    # Must be a valid ZIP starting with PK\x03\x04.
    with open(out, "rb") as f:
        magic = f.read(4)
    assert magic == b"PK\x03\x04"


def test_usdz_save_with_assets(tmp_path):
    s = _stage_with_asset()
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={"tex.png": PNG_HEADER, "audio.wav": WAV_HEADER})
    with zipfile.ZipFile(out, "r") as z:
        names = z.namelist()
    # Root layer must be first
    assert names[0] in ("root.usdc", "root.usda")
    # All extras present
    assert "tex.png" in names
    assert "audio.wav" in names


def test_usdz_archive_is_uncompressed(tmp_path):
    s = _stage_with_asset()
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={"tex.png": PNG_HEADER, "audio.wav": WAV_HEADER})
    with zipfile.ZipFile(out, "r") as z:
        for info in z.infolist():
            assert info.compress_type == zipfile.ZIP_STORED, (
                f"{info.filename} is compressed (compress_type="
                f"{info.compress_type}), USDZ requires Store-only")


def test_usdz_asset_payload_round_trips(tmp_path):
    """The exact bytes we packed should come back via zipfile."""
    s = _stage_with_asset()
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={"tex.png": PNG_HEADER, "audio.wav": WAV_HEADER})
    with zipfile.ZipFile(out, "r") as z:
        assert z.read("tex.png") == PNG_HEADER
        assert z.read("audio.wav") == WAV_HEADER


def test_usdz_64_byte_alignment(tmp_path):
    """Each file's local-header data section must start on a 64-byte
    boundary per the AOUSD USDZ spec."""
    s = _stage_with_asset()
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={"tex.png": PNG_HEADER * 4})
    with open(out, "rb") as f:
        data = f.read()
    # Walk local file headers (signature 0x04034b50, little-endian).
    # Local-header layout: 30 bytes fixed + name_len + extra_len, then
    # data. The data offset must be 64-byte aligned.
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
            f"entry {seen} data_offset={data_offset} not 64-byte aligned")
        seen += 1
        offset = data_offset + compressed_size
    assert seen >= 2, f"expected ≥2 entries, saw {seen}"


def test_usdz_reload(tmp_path):
    """tinyusdz must read its own USDZ output back."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", "tex.png", dtype="asset")
    s.add_root_prim(p)
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={"tex.png": PNG_HEADER})
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "asset a = @tex.png@" in txt


def test_assets_kwarg_rejected_for_non_usdz(tmp_path):
    s = _stage_with_asset()
    with pytest.raises(ValueError):
        s.save(str(tmp_path / "x.usda"), assets={"tex.png": PNG_HEADER})


def test_assets_value_must_be_bytes_like(tmp_path):
    s = _stage_with_asset()
    with pytest.raises(TypeError):
        s.save(str(tmp_path / "x.usdz"), assets={"tex.png": "not bytes"})


def test_assets_key_must_be_str(tmp_path):
    s = _stage_with_asset()
    with pytest.raises(TypeError):
        s.save(str(tmp_path / "x.usdz"), assets={42: PNG_HEADER})


def test_assets_value_accepts_bytearray_and_memoryview(tmp_path):
    s = _stage_with_asset()
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={
        "a.png": bytearray(PNG_HEADER),
        "b.png": memoryview(JPEG_HEADER),
    })
    with zipfile.ZipFile(out, "r") as z:
        assert z.read("a.png") == PNG_HEADER
        assert z.read("b.png") == JPEG_HEADER


# --- rewrite_asset_paths -------------------------------------------------


def test_rewrite_asset_paths_scalar():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("diffuse", "./external/diffuse.png", dtype="asset")
    p.set_attribute("normal",  "./external/normal.png",  dtype="asset")
    s.add_root_prim(p)

    n = tinyusdz.rewrite_asset_paths(s, {
        "./external/diffuse.png": "diffuse.png",
        "./external/normal.png":  "normal.png",
    })
    assert n == 2
    txt = s.export_to_string()
    assert "@diffuse.png@" in txt
    assert "@normal.png@" in txt
    assert "external" not in txt


def test_rewrite_asset_paths_no_match_is_noop():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", "./tex.png", dtype="asset")
    s.add_root_prim(p)
    n = tinyusdz.rewrite_asset_paths(s, {"./missing.png": "x.png"})
    assert n == 0
    assert "@./tex.png@" in s.export_to_string()


def test_rewrite_asset_paths_then_pack(tmp_path):
    """The intended workflow: rewrite paths to match the archive layout,
    then save USDZ with those packed assets."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("diffuse", "/abs/path/to/diffuse.png", dtype="asset")
    s.add_root_prim(p)
    tinyusdz.rewrite_asset_paths(s, {
        "/abs/path/to/diffuse.png": "diffuse.png",
    })
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={"diffuse.png": PNG_HEADER})
    with zipfile.ZipFile(out, "r") as z:
        assert "diffuse.png" in z.namelist()
    s2 = tinyusdz.load(str(out))
    assert "asset diffuse = @diffuse.png@" in s2.export_to_string()


def test_rewrite_asset_paths_rejects_non_dict():
    s = tinyusdz.Stage()
    with pytest.raises(TypeError):
        tinyusdz.rewrite_asset_paths(s, [("a", "b")])
