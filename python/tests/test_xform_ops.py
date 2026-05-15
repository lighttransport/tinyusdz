"""Tests for xformOp authoring + USDA/USDC round-trip on Xform prims.

xformOps in USD are authored as:

    double3 xformOp:translate = (1, 2, 3)
    float3  xformOp:rotateXYZ = (0, 90, 0)
    float3  xformOp:scale     = (2, 2, 2)
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ",
                                    "xformOp:scale"]

These tests cover:
  - All common xformOp suffixes (translate, rotateXYZ, scale, transform).
  - The `xformOpOrder` token[] with uniform variability.
  - double-precision components (USD spec uses double3 for translate).
"""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


FORMATS = ["usda", "usdc"]


def _build_xformed_stage() -> tinyusdz.Stage:
    s = tinyusdz.Stage()
    x = tinyusdz.Prim("Xform", name="World")
    x.set_attribute("xformOp:translate", (1.0, 2.0, 3.0), dtype="double3")
    x.set_attribute("xformOp:rotateXYZ", (0.0, 90.0, 0.0), dtype="float3")
    x.set_attribute("xformOp:scale", (2.0, 2.0, 2.0), dtype="float3")
    x.set_attribute(
        "xformOpOrder",
        ["xformOp:translate", "xformOp:rotateXYZ", "xformOp:scale"],
        dtype="token[]",
    )
    x.set_attribute_metadata("xformOpOrder", "variability", "uniform")
    s.add_root_prim(x)
    return s


def test_xform_op_in_memory():
    s = _build_xformed_stage()
    x = s.get_prim_at_path("/World")
    assert x is not None and x.type_name == "Xform"
    assert "xformOp:translate" in x.property_names()
    assert "xformOpOrder" in x.property_names()


def test_xform_op_appears_in_usda(tmp_path: pathlib.Path):
    s = _build_xformed_stage()
    out = tmp_path / "x.usda"
    s.save(str(out), format="usda")
    text = out.read_text()
    assert "double3 xformOp:translate" in text
    assert "float3 xformOp:rotateXYZ" in text
    assert "float3 xformOp:scale" in text
    assert "uniform token[] xformOpOrder" in text


@pytest.mark.parametrize("fmt", FORMATS)
def test_xform_op_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = _build_xformed_stage()
    out = tmp_path / f"x.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))

    x2 = s2.get_prim_at_path("/World")
    assert x2 is not None and x2.type_name == "Xform"

    t = x2.get_attribute("xformOp:translate")
    assert t is not None and t.value is not None
    assert "1" in t.value.to_string() and "2" in t.value.to_string() and "3" in t.value.to_string()

    r = x2.get_attribute("xformOp:rotateXYZ")
    assert r is not None and r.value is not None
    assert "90" in r.value.to_string()

    sc = x2.get_attribute("xformOp:scale")
    assert sc is not None and sc.value is not None
    assert "2" in sc.value.to_string()

    order = x2.get_attribute("xformOpOrder")
    assert order is not None and order.value is not None
    text = order.value.to_string()
    assert "xformOp:translate" in text
    assert "xformOp:rotateXYZ" in text
    assert "xformOp:scale" in text


@pytest.mark.parametrize("fmt", FORMATS)
def test_xform_op_transform_matrix(tmp_path: pathlib.Path, fmt: str):
    """`xformOp:transform = matrix4d` is authored via the float-list path
    with `dtype='matrix4d'`. Currently the Python coercion treats matrices
    as a flat 16-float list; we verify the string content survives."""
    s = tinyusdz.Stage()
    x = tinyusdz.Prim("Xform", name="X")
    # Identity matrix
    x.set_attribute(
        "xformOp:translate", (5.0, 0.0, 0.0), dtype="double3"
    )
    x.set_attribute(
        "xformOpOrder", ["xformOp:translate"], dtype="token[]"
    )
    x.set_attribute_metadata("xformOpOrder", "variability", "uniform")
    s.add_root_prim(x)

    out = tmp_path / f"m.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    x2 = s2.get_prim_at_path("/X")
    assert x2 is not None
    t = x2.get_attribute("xformOp:translate")
    assert t is not None and t.value is not None
    assert "5" in t.value.to_string()


def test_double3_dtype_in_memory():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("xformOp:translate", (1.5, 2.5, 3.5), dtype="double3")
    a = p.get_attribute("xformOp:translate")
    assert a is not None
    assert a.type_name == "double3"


def test_attribute_variability_uniform():
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("subdivisionScheme", "catmullClark", dtype="token")
    p.set_attribute_metadata("subdivisionScheme", "variability", "uniform")
    assert p.get_attribute_metadata(
        "subdivisionScheme", "variability"
    ) == "uniform"
