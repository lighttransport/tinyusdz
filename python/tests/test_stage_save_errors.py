"""Negative tests for `Stage.save` — invalid paths, format mismatches,
unsupported targets.
"""
import os

import pytest

import tinyusdz


def _stage():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    s.add_root_prim(p)
    return s


def test_save_to_nonexistent_directory_raises(tmp_path):
    s = _stage()
    bad = tmp_path / "no" / "such" / "dir" / "x.usda"
    with pytest.raises((tinyusdz.UsdIoError, tinyusdz.UsdError, OSError)):
        s.save(str(bad))


def test_save_with_unsupported_extension_falls_back_or_raises(tmp_path):
    """Saving with an extension tinyusdz can't handle should either
    error or pick a sensible default — never silently produce an empty
    file."""
    s = _stage()
    out = tmp_path / "x.unknown"
    try:
        s.save(str(out))
    except (tinyusdz.UsdIoError, tinyusdz.UsdError, ValueError):
        return
    # If the call succeeded, the output must be non-empty and valid USD.
    assert out.stat().st_size > 0
    s2 = tinyusdz.load(str(out))
    assert s2 is not None


def test_save_with_explicit_format_mismatch(tmp_path):
    """`format='usda'` on an output named `.usdc` — should emit USDA
    despite the extension, or refuse."""
    s = _stage()
    out = tmp_path / "weird.usdc"
    try:
        s.save(str(out), format="usda")
    except (tinyusdz.UsdIoError, tinyusdz.UsdError, ValueError):
        return
    # If accepted, output is USDA (text); first 4 bytes != PXR-USDC magic.
    head = out.read_bytes()[:4]
    assert head != b"PXR-"


def test_save_assets_kwarg_only_for_usdz(tmp_path):
    """`assets={...}` is meaningful only for USDZ — passing it on
    USDA or USDC should error."""
    s = _stage()
    with pytest.raises((tinyusdz.UsdError, ValueError)):
        s.save(str(tmp_path / "x.usda"), assets={"tex.png": b"\x00"})
    with pytest.raises((tinyusdz.UsdError, ValueError)):
        s.save(str(tmp_path / "x.usdc"), assets={"tex.png": b"\x00"})


def test_save_to_existing_file_overwrites(tmp_path):
    """Saving to an existing path should overwrite, not append."""
    s = _stage()
    out = tmp_path / "x.usda"
    out.write_text("garbage that should be replaced")
    s.save(str(out))
    content = out.read_text()
    assert content.startswith("#usda 1.0")
    assert "garbage" not in content


def test_save_empty_stage(tmp_path):
    """A stage with no prims is degenerate but legal."""
    s = tinyusdz.Stage()
    out = tmp_path / "empty.usda"
    s.save(str(out))
    assert out.stat().st_size > 0
    s2 = tinyusdz.load(str(out))
    assert s2.root_prims() == []
