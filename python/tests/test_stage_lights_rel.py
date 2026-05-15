"""Tests for stage metadata, UsdLux light prims, and Relationship authoring."""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


FORMATS = ["usda", "usdc"]


# ---------------------------------------------------------------------------
# Stage metadata
# ---------------------------------------------------------------------------

def test_stage_metadata_in_memory():
    s = tinyusdz.Stage()
    s.set_metadata("defaultPrim", "World")
    s.set_metadata("upAxis", "Y")
    s.set_metadata("metersPerUnit", 0.01)
    s.set_metadata("timeCodesPerSecond", 24.0)
    s.set_metadata("framesPerSecond", 30.0)
    s.set_metadata("startTimeCode", 0.0)
    s.set_metadata("endTimeCode", 100.0)
    s.set_metadata("doc", "test scene")
    s.set_metadata("comment", "owned by Q/A")
    x = tinyusdz.Prim("Xform", name="World")
    s.add_root_prim(x)

    assert s.get_metadata("defaultPrim") == "World"
    assert s.get_metadata("upAxis") == "Y"
    assert s.get_metadata("metersPerUnit") == pytest.approx(0.01)
    assert s.get_metadata("timeCodesPerSecond") == pytest.approx(24.0)
    assert s.get_metadata("framesPerSecond") == pytest.approx(30.0)
    assert s.get_metadata("startTimeCode") == pytest.approx(0.0)
    assert s.get_metadata("endTimeCode") == pytest.approx(100.0)
    assert s.get_metadata("doc") == "test scene"
    assert s.get_metadata("comment") == "owned by Q/A"


@pytest.mark.parametrize("fmt", FORMATS)
def test_stage_metadata_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = tinyusdz.Stage()
    s.set_metadata("defaultPrim", "World")
    s.set_metadata("upAxis", "Z")
    s.set_metadata("metersPerUnit", 0.01)
    s.set_metadata("timeCodesPerSecond", 60.0)
    x = tinyusdz.Prim("Xform", name="World")
    s.add_root_prim(x)

    out = tmp_path / f"meta.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))

    assert s2.get_metadata("defaultPrim") == "World"
    assert s2.get_metadata("upAxis") == "Z"
    assert s2.get_metadata("metersPerUnit") == pytest.approx(0.01)
    assert s2.get_metadata("timeCodesPerSecond") == pytest.approx(60.0)


def test_stage_metadata_unknown_returns_none():
    s = tinyusdz.Stage()
    assert s.get_metadata("nope") is None


def test_stage_metadata_invalid_upaxis():
    s = tinyusdz.Stage()
    with pytest.raises(ValueError):
        s.set_metadata("upAxis", "Q")


def test_stage_metadata_invalid_key():
    s = tinyusdz.Stage()
    with pytest.raises(ValueError):
        s.set_metadata("notARealKey", "x")


# ---------------------------------------------------------------------------
# UsdLux lights
# ---------------------------------------------------------------------------

LIGHT_TYPES = [
    "SphereLight",
    "RectLight",
    "DiskLight",
    "DistantLight",
    "CylinderLight",
    "DomeLight",
    "DomeLight_1",
    "GeometryLight",
    "PortalLight",
]


@pytest.mark.parametrize("type_name", LIGHT_TYPES)
def test_light_construction(type_name: str):
    p = tinyusdz.Prim(type_name, name="L")
    assert p.type_name == type_name


@pytest.mark.parametrize("type_name", LIGHT_TYPES)
@pytest.mark.parametrize("fmt", FORMATS)
def test_light_typing_roundtrip(
    tmp_path: pathlib.Path, type_name: str, fmt: str
):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim(type_name, name="L")
    s.add_root_prim(p)
    out = tmp_path / f"l.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    assert s2.get_prim_at_path("/L").type_name == type_name


def test_light_attributes_in_memory():
    """Light attributes are authorable; in-memory readback works through
    the props map. Read after USDA/USDC parse routes typed builtins into
    typed fields that the C API does not yet surface for non-Mesh types,
    so this test asserts in-memory only."""
    sl = tinyusdz.Prim("SphereLight", name="Sphere")
    sl.set_attribute("inputs:intensity", 1000.0, dtype="float")
    sl.set_attribute("inputs:radius", 0.25, dtype="float")
    sl.set_attribute(
        "inputs:color", (0.9, 0.8, 0.7), dtype="color3f"
    )
    intensity = sl.get_attribute("inputs:intensity")
    assert intensity is not None and intensity.value is not None
    assert pytest.approx(intensity.value.as_scalar(), rel=1e-5) == 1000.0


@pytest.mark.parametrize("fmt", FORMATS)
def test_light_attributes_persisted_to_file(tmp_path: pathlib.Path, fmt: str):
    """Light attributes survive round-trip in the on-disk file (USDA/USDC),
    even when the C API readback for typed light builtins is not yet wired."""
    s = tinyusdz.Stage()
    sl = tinyusdz.Prim("SphereLight", name="Sphere")
    sl.set_attribute("inputs:intensity", 1000.0, dtype="float")
    sl.set_attribute("inputs:radius", 0.25, dtype="float")
    s.add_root_prim(sl)

    out = tmp_path / f"l.{fmt}"
    s.save(str(out), format=fmt)
    if fmt == "usda":
        text = out.read_text()
        assert "inputs:intensity" in text
        assert "1000" in text
        assert "inputs:radius" in text
    s2 = tinyusdz.load(str(out))
    assert s2.get_prim_at_path("/Sphere").type_name == "SphereLight"


# ---------------------------------------------------------------------------
# Relationships
# ---------------------------------------------------------------------------

def test_relationship_single_target_in_memory():
    s = tinyusdz.Stage()
    mat = tinyusdz.Prim("Material", name="Mat")
    mesh = tinyusdz.Prim("Mesh", name="M")
    mesh.add_relationship("material:binding", "/Mat")
    s.add_root_prim(mat)
    s.add_root_prim(mesh)

    targets = mesh.get_relationship_targets("material:binding")
    assert targets == ["/Mat"]
    text = s.export_to_string()
    assert "rel material:binding = </Mat>" in text


def test_relationship_multi_target_in_memory():
    p = tinyusdz.Prim("Mesh", name="M")
    p.add_relationship("skel:joints", ["/Skel/Root", "/Skel/Hand"])
    targets = p.get_relationship_targets("skel:joints")
    assert targets == ["/Skel/Root", "/Skel/Hand"]


def test_relationship_no_value():
    p = tinyusdz.Prim("Mesh", name="M")
    p.add_relationship("anchor", [])
    # An empty (DefineOnly) rel has no targets to read back.
    assert p.get_relationship_targets("anchor") is None


@pytest.mark.parametrize("fmt", FORMATS)
def test_relationship_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = tinyusdz.Stage()
    mat = tinyusdz.Prim("Material", name="Mat")
    mesh = tinyusdz.Prim("Mesh", name="M")
    mesh.add_relationship("material:binding", "/Mat")
    s.add_root_prim(mat)
    s.add_root_prim(mesh)

    out = tmp_path / f"r.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))

    m2 = s2.get_prim_at_path("/M")
    assert m2 is not None and m2.type_name == "Mesh"
    targets = m2.get_relationship_targets("material:binding")
    assert targets == ["/Mat"]


def test_relationship_invalid_target_type():
    p = tinyusdz.Prim("Mesh", name="M")
    with pytest.raises(TypeError):
        p.add_relationship("bad", [123, 456])
