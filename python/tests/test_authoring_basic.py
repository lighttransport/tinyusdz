"""Programmatic authoring of USD data via the Python API.

Verifies that Prim/Attribute construction, add_child, and add_root_prim
build a stage that exposes the expected paths and attribute values
through the existing read API — without going through any save/load.
"""
from __future__ import annotations

import pytest

import tinyusdz


def test_construct_empty_stage():
    s = tinyusdz.Stage()
    assert s.root_prims() == []


def test_add_root_prim_xform():
    s = tinyusdz.Stage()
    x = tinyusdz.Prim("Xform", name="World")
    s.add_root_prim(x)
    roots = s.root_prims()
    assert len(roots) == 1
    assert roots[0].name == "World"
    assert s.get_prim_at_path("/World") is not None


def test_add_child_builds_hierarchy():
    s = tinyusdz.Stage()
    parent = tinyusdz.Prim("Xform", name="P")
    child = tinyusdz.Prim("Xform", name="C")
    grand = tinyusdz.Prim("Sphere", name="G")
    child.add_child(grand)
    parent.add_child(child)
    s.add_root_prim(parent)

    assert s.get_prim_at_path("/P/C/G") is not None
    p = s.get_prim_at_path("/P")
    assert [k.name for k in p.children()] == ["C"]


def test_set_attribute_int_array():
    s = tinyusdz.Stage()
    m = tinyusdz.Prim("Mesh", name="M")
    m.set_attribute("faceVertexCounts", [3, 4, 5])
    s.add_root_prim(m)

    mesh = s.get_prim_at_path("/M")
    assert mesh is not None
    a = mesh.get_attribute("faceVertexCounts")
    assert a is not None
    val = a.value
    assert val is not None and val.is_array
    assert list(memoryview(val)) == [3, 4, 5]


def test_set_attribute_float_scalar():
    s = tinyusdz.Stage()
    sph = tinyusdz.Prim("Sphere", name="Ball")
    sph.set_attribute("radius", 1.25)
    s.add_root_prim(sph)
    a = s.get_prim_at_path("/Ball").get_attribute("radius")
    assert a is not None
    v = a.value
    assert v is not None
    # scalar float
    assert pytest.approx(v.as_scalar(), rel=1e-6) == 1.25


def test_set_attribute_point3f_array():
    s = tinyusdz.Stage()
    m = tinyusdz.Prim("Mesh", name="M")
    pts = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)]
    m.set_attribute("points", pts, dtype="point3f[]")
    s.add_root_prim(m)

    a = s.get_prim_at_path("/M").get_attribute("points")
    assert a is not None
    v = a.value
    assert v is not None and v.is_array
    np = pytest.importorskip("numpy")
    arr = np.asarray(v)
    assert arr.shape == (3, 3)
    assert arr.dtype == np.float32
    assert arr.tolist() == [[0, 0, 0], [1, 0, 0], [0, 1, 0]]


def test_unsupported_value_type_raises():
    m = tinyusdz.Prim("Mesh", name="M")
    with pytest.raises(TypeError):
        m.set_attribute("bogus", object())


def test_unknown_prim_type_falls_back_to_model():
    # The C API treats unknown type names as a Model with prim_type_name set
    # to the string. A construction with a clearly invalid identifier raises.
    p = tinyusdz.Prim("MyCustomPrim", name="Cust")
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    out = s.export_to_string()
    assert "MyCustomPrim" in out


def test_invalid_identifier_raises():
    with pytest.raises(ValueError):
        tinyusdz.Prim("Bad Name With Spaces")
