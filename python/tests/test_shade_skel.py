"""Tests for UsdShade (Material/Shader/NodeGraph + MaterialXConfigAPI)
and UsdSkel (SkelRoot/Skeleton/SkelAnimation/BlendShape) authoring,
USDA + USDC roundtrip, and applied-API-schema metadata.

These tests exercise the binding extensions added on top of the
PhysicsScene work — generic typed-prim construction for shade/skel
schemas plus `Prim.apply_api_schema` / `Prim.api_schemas`.
"""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


FORMATS = ["usda", "usdc"]


# ---------------------------------------------------------------------------
# Material / Shader graph
# ---------------------------------------------------------------------------

def _build_material_graph() -> tinyusdz.Stage:
    s = tinyusdz.Stage()
    mat = tinyusdz.Prim("Material", name="Mat")
    shader = tinyusdz.Prim("Shader", name="PBR")
    shader.set_attribute("info:id", "UsdPreviewSurface", dtype="token")
    shader.set_attribute("inputs:roughness", 0.5)
    shader.set_attribute("inputs:metallic", 1.0)
    shader.set_attribute("inputs:diffuseColor",
                         (0.8, 0.2, 0.1), dtype="color3f")
    mat.add_child(shader)
    s.add_root_prim(mat)
    return s


def test_material_shader_in_memory():
    s = _build_material_graph()
    mat = s.get_prim_at_path("/Mat")
    assert mat is not None and mat.type_name == "Material"

    sh = s.get_prim_at_path("/Mat/PBR")
    assert sh is not None and sh.type_name == "Shader"

    info_id = sh.get_attribute("info:id")
    assert info_id is not None and info_id.value.as_scalar() == "UsdPreviewSurface"

    rough = sh.get_attribute("inputs:roughness")
    assert rough is not None
    assert pytest.approx(rough.value.as_scalar(), rel=1e-5) == 0.5


def test_material_shader_roundtrip_usdc(tmp_path: pathlib.Path):
    """Full attribute-fidelity roundtrip via USDC.

    USDA is covered by test_material_shader_roundtrip_usda_structure_only
    because the existing tinyusdz USDA Shader writer requires a typed
    shader subclass (UsdPreviewSurface, ...) to serialize attribute
    values, and emits a placeholder for generic Shader prims. USDC has
    no such limitation.
    """
    s = _build_material_graph()
    out = tmp_path / "mat.usdc"
    s.save(str(out), format="usdc")
    s2 = tinyusdz.load(str(out))

    mat = s2.get_prim_at_path("/Mat")
    sh = s2.get_prim_at_path("/Mat/PBR")
    assert mat is not None and sh is not None
    assert mat.type_name == "Material"
    assert sh.type_name == "Shader"

    rough = sh.get_attribute("inputs:roughness")
    metal = sh.get_attribute("inputs:metallic")
    assert rough is not None and rough.value is not None
    assert metal is not None and metal.value is not None
    assert pytest.approx(rough.value.as_scalar(), rel=1e-5) == 0.5
    assert pytest.approx(metal.value.as_scalar(), rel=1e-5) == 1.0


def test_material_shader_roundtrip_usda_structure_only(tmp_path: pathlib.Path):
    """USDA writer can save the Material/Shader hierarchy; per the limitation
    described above, attribute-value reload is not asserted here."""
    s = _build_material_graph()
    out = tmp_path / "mat.usda"
    s.save(str(out), format="usda")
    assert out.exists() and out.stat().st_size > 0


def test_nodegraph_construction():
    s = tinyusdz.Stage()
    ng = tinyusdz.Prim("NodeGraph", name="NG")
    ng.set_attribute("inputs:scale", 2.0)
    s.add_root_prim(ng)
    assert s.get_prim_at_path("/NG").type_name == "NodeGraph"
    text = s.export_to_string()
    assert "NodeGraph" in text


# ---------------------------------------------------------------------------
# MaterialXConfigAPI
# ---------------------------------------------------------------------------

def test_apply_materialx_config_api_in_memory():
    mat = tinyusdz.Prim("Material", name="Mat")
    mat.apply_api_schema("MaterialXConfigAPI")
    schemas = mat.api_schemas()
    assert "MaterialXConfigAPI" in schemas


@pytest.mark.parametrize("fmt", FORMATS)
def test_materialx_config_api_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = tinyusdz.Stage()
    mat = tinyusdz.Prim("Material", name="Mat")
    mat.apply_api_schema("MaterialXConfigAPI")
    # MaterialXConfigAPI exposes config:mtlx:* attributes on the Material
    mat.set_attribute("config:mtlx:version", "1.39")
    mat.set_attribute("config:mtlx:colorspace", "lin_rec709")
    s.add_root_prim(mat)
    out = tmp_path / f"matx.{fmt}"
    s.save(str(out), format=fmt)

    s2 = tinyusdz.load(str(out))
    m2 = s2.get_prim_at_path("/Mat")
    assert m2 is not None and m2.type_name == "Material"

    # apiSchemas should round-trip with the MaterialXConfigAPI entry.
    schemas = m2.api_schemas()
    assert any("MaterialXConfigAPI" in sc for sc in schemas)

    ver = m2.get_attribute("config:mtlx:version")
    assert ver is not None and ver.value is not None
    assert ver.value.as_scalar() == "1.39"


def test_apply_multiple_api_schemas():
    p = tinyusdz.Prim("Material", name="Mat")
    p.apply_api_schema("MaterialXConfigAPI")
    p.apply_api_schema("CollectionAPI", instance_name="material")
    schemas = p.api_schemas()
    assert "MaterialXConfigAPI" in schemas
    assert "CollectionAPI:material" in schemas


# ---------------------------------------------------------------------------
# UsdSkel
# ---------------------------------------------------------------------------

def _build_skel_stage() -> tinyusdz.Stage:
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("SkelRoot", name="Model")

    skel = tinyusdz.Prim("Skeleton", name="Skel")
    skel.set_attribute("joints",
                       ["Shoulder", "Shoulder/Elbow", "Shoulder/Elbow/Hand"],
                       dtype="token[]")

    anim = tinyusdz.Prim("SkelAnimation", name="Anim")
    anim.set_attribute("joints", ["Shoulder/Elbow"], dtype="token[]")

    bs = tinyusdz.Prim("BlendShape", name="Wider")
    # offsets is vector3f[], but we stay within the float-component-array
    # path covered by the C API today; encode as float3[] which the writer
    # routes via props for unauthored typed builtins.
    bs.set_attribute("offsets",
                     [(0.0, 1.0, 0.0), (0.0, 0.5, 0.0)],
                     dtype="vector3f[]")

    skel.add_child(anim)
    root.add_child(skel)
    root.add_child(bs)
    s.add_root_prim(root)
    return s


def test_skel_in_memory_construction():
    s = _build_skel_stage()
    assert s.get_prim_at_path("/Model").type_name == "SkelRoot"
    assert s.get_prim_at_path("/Model/Skel").type_name == "Skeleton"
    assert s.get_prim_at_path("/Model/Skel/Anim").type_name == "SkelAnimation"
    assert s.get_prim_at_path("/Model/Wider").type_name == "BlendShape"

    skel = s.get_prim_at_path("/Model/Skel")
    joints = skel.get_attribute("joints")
    assert joints is not None and joints.value is not None
    # Token arrays come back through to_string() with brackets.
    js = joints.value.to_string()
    assert "Shoulder" in js
    assert "Elbow" in js


@pytest.mark.parametrize("fmt", FORMATS)
def test_skel_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = _build_skel_stage()
    out = tmp_path / f"skel.{fmt}"
    s.save(str(out), format=fmt)

    s2 = tinyusdz.load(str(out))
    assert s2.get_prim_at_path("/Model").type_name == "SkelRoot"
    assert s2.get_prim_at_path("/Model/Skel").type_name == "Skeleton"
    assert s2.get_prim_at_path("/Model/Skel/Anim").type_name == "SkelAnimation"
    assert s2.get_prim_at_path("/Model/Wider").type_name == "BlendShape"

    sk = s2.get_prim_at_path("/Model/Skel")
    joints = sk.get_attribute("joints")
    assert joints is not None and joints.value is not None
    js = joints.value.to_string()
    assert "Shoulder/Elbow" in js


def test_apply_skel_binding_api():
    p = tinyusdz.Prim("Mesh", name="Arm")
    p.apply_api_schema("SkelBindingAPI")
    schemas = p.api_schemas()
    assert "SkelBindingAPI" in schemas


# ---------------------------------------------------------------------------
# Reading existing Skel fixture
# ---------------------------------------------------------------------------

def test_read_existing_skel_fixture(usda_fixture_dir: pathlib.Path):
    f = usda_fixture_dir / "usdskel-001.usda"
    if not f.exists():
        pytest.skip("usdskel-001.usda fixture missing")
    s = tinyusdz.load(str(f))
    root = s.get_prim_at_path("/Model")
    assert root is not None and root.type_name == "SkelRoot"
    skel = s.get_prim_at_path("/Model/Skel")
    assert skel is not None and skel.type_name == "Skeleton"
    anim = s.get_prim_at_path("/Model/Skel/Anim")
    assert anim is not None and anim.type_name == "SkelAnimation"
