"""Asset path normalization: outer @...@ delimiters are stripped before
the C constructor sees the string, so Value.to_string doesn't repeat
them as @@@@./img.png@@@@.
"""
import os
import tempfile

import pytest
import tinyusdz


FORMATS = ["usda", "usdc", "usdz"]


def _stage_with_asset(value, dtype="asset"):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", value, dtype=dtype)
    s.add_root_prim(p)
    return s


def _roundtrip(stage, fmt, tmp_path):
    out = tmp_path / f"x.{fmt}"
    stage.save(str(out))
    return tinyusdz.load(str(out))


@pytest.mark.parametrize("input_str", [
    "./img.png",       # bare
    "@./img.png@",     # double-@
    "@@@./img.png@@@",  # triple-@ (escaped form)
])
def test_asset_authoring_normalizes(input_str):
    """Authoring should strip outer delimiters; in-memory value matches."""
    s = _stage_with_asset(input_str)
    p = s.get_prim_at_path("/X")
    a = p.get_attribute("a")
    assert a is not None
    assert a.type_name == "asset"
    rendered = a.value.to_string() if a.value else ""
    # No quadruple-@@@@ regression.
    assert "@@@@" not in rendered, rendered


@pytest.mark.parametrize("fmt", FORMATS)
def test_asset_roundtrip(tmp_path, fmt):
    s = _stage_with_asset("./img.png")
    s2 = _roundtrip(s, fmt, tmp_path)
    a = s2.get_prim_at_path("/X").get_attribute("a")
    assert a is not None
    assert a.type_name == "asset"
    rendered = a.value.to_string() if a.value else ""
    assert "@@@@" not in rendered, rendered
    assert "img.png" in rendered


@pytest.mark.parametrize("fmt", FORMATS)
def test_asset_array_mixed_delimiters(tmp_path, fmt):
    paths = ["./a.png", "@./b.png@", "@@@c.png@@@"]
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("paths", paths, dtype="asset[]")
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    a = s2.get_prim_at_path("/X").get_attribute("paths")
    assert a is not None
    assert a.type_name == "asset[]"
    rendered = a.value.to_string() if a.value else ""
    assert "@@@@" not in rendered, rendered
    for piece in ("a.png", "b.png", "c.png"):
        assert piece in rendered
