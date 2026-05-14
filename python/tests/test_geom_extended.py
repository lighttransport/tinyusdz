"""Tests for extended UsdGeom prim types (curves, subdivision surface,
GeomSubset, GeomPlane/Cylinder_1/Capsule_1, TetMesh, NurbsPatch,
PointInstancer) — construction, USDA + USDC roundtrip of prim typing,
and core authored attributes.
"""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


FORMATS = ["usda", "usdc"]


EXTENDED_GEOM_TYPES = [
    "BasisCurves",
    "NurbsCurves",
    "HermiteCurves",
    "GeomSubset",
    "Plane",
    "Cylinder_1",
    "Capsule_1",
    "TetMesh",
    "NurbsPatch",
    "PointInstancer",
]


@pytest.mark.parametrize("type_name", EXTENDED_GEOM_TYPES)
def test_extended_geom_construction(type_name: str):
    p = tinyusdz.Prim(type_name, name="X")
    assert p.type_name == type_name


@pytest.mark.parametrize("type_name", EXTENDED_GEOM_TYPES)
@pytest.mark.parametrize("fmt", FORMATS)
def test_extended_geom_typing_roundtrip(
    tmp_path: pathlib.Path, type_name: str, fmt: str
):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim(type_name, name="X")
    s.add_root_prim(p)
    out = tmp_path / f"x.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    got = s2.get_prim_at_path("/X")
    assert got is not None and got.type_name == type_name


# ---------------------------------------------------------------------------
# Curves
# ---------------------------------------------------------------------------

def _build_basis_curves_stage() -> tinyusdz.Stage:
    s = tinyusdz.Stage()
    bc = tinyusdz.Prim("BasisCurves", name="Curves")
    bc.set_attribute("curveVertexCounts", [4, 4], dtype="int[]")
    bc.set_attribute(
        "points",
        [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (2.0, 0.0, 0.0), (3.0, 0.0, 0.0),
         (0.0, 1.0, 0.0), (1.0, 1.0, 0.0), (2.0, 1.0, 0.0), (3.0, 1.0, 0.0)],
        dtype="point3f[]",
    )
    bc.set_attribute("type", "linear", dtype="token")
    bc.set_attribute("basis", "bspline", dtype="token")
    bc.set_attribute("wrap", "periodic", dtype="token")
    bc.set_attribute("widths", [0.1, 0.1], dtype="float[]")
    s.add_root_prim(bc)
    return s


@pytest.mark.parametrize("fmt", FORMATS)
def test_basis_curves_attribute_roundtrip(
    tmp_path: pathlib.Path, fmt: str
):
    s = _build_basis_curves_stage()
    out = tmp_path / f"bc.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    bc = s2.get_prim_at_path("/Curves")
    assert bc is not None and bc.type_name == "BasisCurves"

    for name, expected in (
        ("type", "linear"),
        ("basis", "bspline"),
        ("wrap", "periodic"),
    ):
        a = bc.get_attribute(name)
        assert a is not None and a.value is not None, name
        assert a.value.as_scalar() == expected, name

    # Authored array attributes round-trip through the file (the textual /
    # binary form contains them). Read-side access for non-Mesh typed
    # array builtins requires tydra::GetProperty specializations beyond
    # this test's scope, so verify file content for USDA only.
    if fmt == "usda":
        text = pathlib.Path(out).read_text()
        assert "curveVertexCounts" in text
        assert "[4, 4]" in text
        assert "points" in text
        assert "widths" in text


def test_nurbs_curves_construction_attrs():
    s = tinyusdz.Stage()
    nc = tinyusdz.Prim("NurbsCurves", name="N")
    nc.set_attribute("curveVertexCounts", [3], dtype="int[]")
    nc.set_attribute("order", [3], dtype="int[]")
    nc.set_attribute("knots", [0.0, 0.0, 0.0, 1.0, 1.0, 1.0], dtype="double[]") \
        if False else None  # double[] not supported by C API
    nc.set_attribute(
        "points",
        [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (2.0, 0.0, 0.0)],
        dtype="point3f[]",
    )
    s.add_root_prim(nc)
    text = s.export_to_string()
    assert "NurbsCurves" in text
    assert "curveVertexCounts" in text


def test_hermite_curves_construction():
    s = tinyusdz.Stage()
    hc = tinyusdz.Prim("HermiteCurves", name="H")
    hc.set_attribute("curveVertexCounts", [2], dtype="int[]")
    hc.set_attribute(
        "points", [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0)], dtype="point3f[]"
    )
    hc.set_attribute(
        "tangents",
        [(1.0, 0.0, 0.0), (1.0, 0.0, 0.0)],
        dtype="vector3f[]",
    )
    s.add_root_prim(hc)
    text = s.export_to_string()
    assert "HermiteCurves" in text


# ---------------------------------------------------------------------------
# Subdivision surface (GeomMesh subdiv attributes)
# ---------------------------------------------------------------------------

def _build_subdiv_mesh_stage() -> tinyusdz.Stage:
    s = tinyusdz.Stage()
    m = tinyusdz.Prim("Mesh", name="Sub")
    m.set_attribute(
        "points",
        [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0),
         (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)],
        dtype="point3f[]",
    )
    m.set_attribute("faceVertexCounts", [4], dtype="int[]")
    m.set_attribute("faceVertexIndices", [0, 1, 2, 3], dtype="int[]")
    m.set_attribute("subdivisionScheme", "catmullClark", dtype="token")
    m.set_attribute("interpolateBoundary", "edgeAndCorner", dtype="token")
    m.set_attribute(
        "faceVaryingLinearInterpolation", "cornersPlus1", dtype="token"
    )
    m.set_attribute("creaseIndices", [0, 1, 1, 2], dtype="int[]")
    m.set_attribute("creaseLengths", [2, 2], dtype="int[]")
    m.set_attribute("creaseSharpnesses", [10.0, 5.0], dtype="float[]")
    m.set_attribute("cornerIndices", [3], dtype="int[]")
    m.set_attribute("cornerSharpnesses", [3.0], dtype="float[]")
    m.set_attribute("holeIndices", [], dtype="int[]")
    s.add_root_prim(m)
    return s


@pytest.mark.parametrize("fmt", FORMATS)
def test_subdiv_mesh_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = _build_subdiv_mesh_stage()
    out = tmp_path / f"sub.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    m = s2.get_prim_at_path("/Sub")
    assert m is not None and m.type_name == "Mesh"

    scheme = m.get_attribute("subdivisionScheme")
    assert scheme is not None and scheme.value is not None
    assert scheme.value.as_scalar() == "catmullClark"

    crease = m.get_attribute("creaseIndices")
    assert crease is not None and crease.value is not None
    cs = crease.value.to_string()
    for tok in ("0", "1", "2"):
        assert tok in cs

    sharp = m.get_attribute("creaseSharpnesses")
    assert sharp is not None and sharp.value is not None
    assert "10" in sharp.value.to_string()

    corners = m.get_attribute("cornerIndices")
    assert corners is not None and corners.value is not None
    assert "3" in corners.value.to_string()


# ---------------------------------------------------------------------------
# GeomSubset
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("fmt", FORMATS)
def test_geom_subset_under_mesh(tmp_path: pathlib.Path, fmt: str):
    s = tinyusdz.Stage()
    m = tinyusdz.Prim("Mesh", name="M")
    m.set_attribute("faceVertexCounts", [3, 3], dtype="int[]")
    m.set_attribute("faceVertexIndices", [0, 1, 2, 0, 2, 3], dtype="int[]")
    sub = tinyusdz.Prim("GeomSubset", name="Front")
    sub.set_attribute("indices", [0], dtype="int[]")
    sub.set_attribute("elementType", "face", dtype="token")
    sub.set_attribute("familyName", "materialBind", dtype="token")
    m.add_child(sub)
    s.add_root_prim(m)

    out = tmp_path / f"sub.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    gs = s2.get_prim_at_path("/M/Front")
    assert gs is not None and gs.type_name == "GeomSubset"

    if fmt == "usda":
        text = pathlib.Path(out).read_text()
        assert "indices" in text
        assert "elementType" in text
        assert "familyName" in text


# ---------------------------------------------------------------------------
# Plane
# ---------------------------------------------------------------------------

def test_plane_construction():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Plane", name="P")
    p.set_attribute("axis", "Y", dtype="token")
    s.add_root_prim(p)
    text = s.export_to_string()
    assert "Plane" in text
