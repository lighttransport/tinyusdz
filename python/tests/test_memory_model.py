"""Regression tests for the ownership model.

Before the helpers rewrite, `Prim.get_attribute` returned a borrowed pointer
into a thread-local `Attribute` cache. Two simultaneous handles aliased each
other: the first became corrupt the moment the second call happened.

These tests pin that invariant so it can't silently regress.
"""
from __future__ import annotations

import pytest

import tinyusdz


USDA = """#usda 1.0

def Mesh "M"
{
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
    normal3f[] normals = [(0,0,1), (0,0,1), (0,0,1), (0,0,1)]
    color3f[] primvars:displayColor = [(1,0,0), (0,1,0), (0,0,1), (1,1,0)]
    texCoord2f[] primvars:st = [(0,0), (1,0), (1,1), (0,1)]
    int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]
    int[] faceVertexCounts = [3, 3]
}
"""


@pytest.fixture(scope="module")
def stage():
    return tinyusdz.loads(USDA)


def test_two_attribute_handles_are_independent(stage):
    mesh = stage.root_prims()[0]
    pts = mesh.get_attribute("points")
    nrm = mesh.get_attribute("normals")
    # Read both values after both attributes exist — the earlier one must
    # still be valid and un-aliased by the later one.
    pts_bytes = bytes(memoryview(pts.value))
    nrm_bytes = bytes(memoryview(nrm.value))
    assert pts_bytes != nrm_bytes
    assert len(pts_bytes) == 4 * 3 * 4
    assert len(nrm_bytes) == 4 * 3 * 4


def test_many_concurrent_handles(stage):
    mesh = stage.root_prims()[0]
    refs = [mesh.get_attribute("points") for _ in range(100)]
    expected = bytes(memoryview(refs[0].value))
    for r in refs:
        assert bytes(memoryview(r.value)) == expected


def test_role_type_arrays_are_2d(stage):
    mesh = stage.root_prims()[0]
    for name in ("points", "normals", "primvars:displayColor"):
        v = mesh.get_attribute(name).value
        mv = memoryview(v)
        assert mv.ndim == 2
        assert mv.shape == (4, 3)
        assert mv.format == "f"
        assert mv.itemsize == 4

    uv_mv = memoryview(mesh.get_attribute("primvars:st").value)
    assert uv_mv.shape == (4, 2)


def test_scalar_int_arrays_are_1d(stage):
    mesh = stage.root_prims()[0]
    v = mesh.get_attribute("faceVertexIndices").value
    mv = memoryview(v)
    assert mv.ndim == 1
    assert mv.shape == (6,)
    assert mv.format == "i"
    assert mv.itemsize == 4


def test_numpy_roundtrip(stage):
    np = pytest.importorskip("numpy")
    mesh = stage.root_prims()[0]
    arr = np.asarray(mesh.get_attribute("points").value)
    assert arr.shape == (4, 3)
    assert arr.dtype == np.float32


def test_value_outlives_attribute_via_buffer_refcount(stage):
    mesh = stage.root_prims()[0]
    attr = mesh.get_attribute("points")
    mv = memoryview(attr.value)
    del attr  # buffer protocol should keep the chain alive via INCREF
    assert bytes(mv)[:12] == b"\x00" * 12  # first vertex is (0,0,0)


def test_tydra_list_prims_by_type(stage):
    meshes = tinyusdz.tydra.list_prims_by_type(stage, "Mesh")
    assert len(meshes) == 1
    prim, path, depth = meshes[0]
    assert prim.type_name == "Mesh"


def test_loads_and_load_bytes_equivalent():
    s1 = tinyusdz.loads(USDA)
    s2 = tinyusdz.load_bytes(USDA.encode("utf-8"))
    assert len(s1.root_prims()) == len(s2.root_prims())
