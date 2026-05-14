"""Tests for attribute connections and attribute-level metadata."""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


# ---------------------------------------------------------------------------
# Attribute connections (`inputs:foo.connect = </Path>`)
# ---------------------------------------------------------------------------

def test_attribute_connection_single_in_memory():
    shader = tinyusdz.Prim("Shader", name="PBR")
    shader.add_attribute_connection(
        "inputs:diffuseColor",
        "/Mat/Tex.outputs:rgb",
        dtype="color3f",
    )
    targets = shader.get_attribute_connections("inputs:diffuseColor")
    assert targets == ["/Mat/Tex.outputs:rgb"]


def test_attribute_connection_multi_in_memory():
    shader = tinyusdz.Prim("Shader", name="PBR")
    shader.add_attribute_connection(
        "inputs:multi",
        ["/A.outputs:o", "/B.outputs:o"],
        dtype="float",
    )
    targets = shader.get_attribute_connections("inputs:multi")
    assert targets == ["/A.outputs:o", "/B.outputs:o"]


def test_attribute_connection_requires_target():
    shader = tinyusdz.Prim("Shader", name="PBR")
    with pytest.raises(ValueError):
        shader.add_attribute_connection("inputs:x", [])


def test_attribute_connection_target_type_check():
    shader = tinyusdz.Prim("Shader", name="PBR")
    with pytest.raises(TypeError):
        shader.add_attribute_connection("inputs:x", [1, 2])


def test_attribute_connection_roundtrip_usdc(tmp_path: pathlib.Path):
    """USDC round-trip preserves attribute connection target paths."""
    s = tinyusdz.Stage()
    mat = tinyusdz.Prim("Material", name="Mat")
    shader = tinyusdz.Prim("Shader", name="PBR")
    shader.set_attribute("info:id", "UsdPreviewSurface", dtype="token")
    shader.add_attribute_connection(
        "inputs:diffuseColor",
        "/Mat/Tex.outputs:rgb",
        dtype="color3f",
    )
    mat.add_child(shader)
    s.add_root_prim(mat)

    out = tmp_path / "c.usdc"
    s.save(str(out), format="usdc")
    s2 = tinyusdz.load(str(out))

    sh = s2.get_prim_at_path("/Mat/PBR")
    assert sh is not None and sh.type_name == "Shader"
    targets = sh.get_attribute_connections("inputs:diffuseColor")
    assert targets == ["/Mat/Tex.outputs:rgb"]


def test_attribute_metadata_usdc_roundtrip(tmp_path: pathlib.Path):
    """Attribute meta (displayName, displayGroup, doc, interpolation)
    round-trips through USDC."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("points", [(0.0, 0.0, 0.0)], dtype="point3f[]")
    p.set_attribute_metadata("points", "displayName", "Pts")
    p.set_attribute_metadata("points", "displayGroup", "Geom")
    p.set_attribute_metadata("points", "doc", "vertex positions")
    p.set_attribute_metadata("points", "interpolation", "vertex")
    s.add_root_prim(p)

    out = tmp_path / "m.usdc"
    s.save(str(out), format="usdc")
    s2 = tinyusdz.load(str(out))
    m2 = s2.get_prim_at_path("/M")
    assert m2.get_attribute_metadata("points", "displayName") == "Pts"
    assert m2.get_attribute_metadata("points", "displayGroup") == "Geom"
    assert m2.get_attribute_metadata("points", "doc") == "vertex positions"
    assert m2.get_attribute_metadata("points", "interpolation") == "vertex"


# ---------------------------------------------------------------------------
# Attribute metadata
# ---------------------------------------------------------------------------

def test_attribute_metadata_in_memory():
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("points", [(0.0, 0.0, 0.0)], dtype="point3f[]")
    p.set_attribute_metadata("points", "displayName", "Points")
    p.set_attribute_metadata("points", "interpolation", "vertex")
    p.set_attribute_metadata("points", "displayGroup", "Geometry")
    p.set_attribute_metadata("points", "doc", "vertex positions")
    p.set_attribute_metadata("points", "hidden", True)
    p.set_attribute_metadata("points", "custom", False)

    assert p.get_attribute_metadata("points", "displayName") == "Points"
    assert p.get_attribute_metadata("points", "interpolation") == "vertex"
    assert p.get_attribute_metadata("points", "displayGroup") == "Geometry"
    assert p.get_attribute_metadata("points", "doc") == "vertex positions"


def test_attribute_metadata_appears_in_usda(tmp_path: pathlib.Path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("points", [(0.0, 0.0, 0.0)], dtype="point3f[]")
    p.set_attribute_metadata("points", "displayName", "Points")
    p.set_attribute_metadata("points", "interpolation", "vertex")
    p.set_attribute_metadata("points", "displayGroup", "Geometry")
    s.add_root_prim(p)

    out = tmp_path / "m.usda"
    s.save(str(out), format="usda")
    text = out.read_text()
    assert "displayName = \"Points\"" in text
    assert "interpolation = \"vertex\"" in text
    assert "displayGroup = \"Geometry\"" in text


def test_attribute_metadata_usda_roundtrip(tmp_path: pathlib.Path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("points", [(0.0, 0.0, 0.0)], dtype="point3f[]")
    p.set_attribute_metadata("points", "displayName", "Points")
    p.set_attribute_metadata("points", "interpolation", "vertex")
    p.set_attribute_metadata("points", "displayGroup", "Geometry")
    s.add_root_prim(p)

    out = tmp_path / "m.usda"
    s.save(str(out), format="usda")
    s2 = tinyusdz.load(str(out))
    m2 = s2.get_prim_at_path("/M")
    assert m2.get_attribute_metadata("points", "displayName") == "Points"
    assert m2.get_attribute_metadata("points", "interpolation") == "vertex"
    assert m2.get_attribute_metadata("points", "displayGroup") == "Geometry"


def test_attribute_metadata_invalid_value_type():
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("points", [(0.0, 0.0, 0.0)], dtype="point3f[]")
    with pytest.raises(TypeError):
        p.set_attribute_metadata("points", "displayName", 42)


def test_attribute_metadata_unknown_attribute_returns_none():
    p = tinyusdz.Prim("Mesh", name="M")
    assert p.get_attribute_metadata("nope", "displayName") is None
