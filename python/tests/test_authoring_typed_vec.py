"""Regression: typed-vec aliases author with the correct USD type
through C API (color3f/point3f/normal3f/vector3f, double counterparts,
matrix2/3/4d, bool)."""
import os
import tempfile

import pytest
import tinyusdz


def _roundtrip(stage):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "x.usdc")
        stage.save(path)
        return tinyusdz.load(path)


@pytest.mark.parametrize("dtype", ["color3f", "point3f", "normal3f", "vector3f"])
def test_typed_float3_alias_authoring(dtype):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "x")
    p.set_attribute(dtype, (1.0, 2.0, 3.0), dtype=dtype)
    s.add_root_prim(p)
    out = _roundtrip(s).export_to_string()
    assert f"{dtype} {dtype}" in out, out


@pytest.mark.parametrize("dtype", ["color3d", "point3d", "normal3d", "vector3d"])
def test_typed_double3_alias_authoring(dtype):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "x")
    p.set_attribute(dtype, (1.0, 2.0, 3.0), dtype=dtype)
    s.add_root_prim(p)
    out = _roundtrip(s).export_to_string()
    assert f"{dtype} {dtype}" in out, out


@pytest.mark.parametrize("dtype,dim", [("matrix2d", 2), ("matrix3d", 3), ("matrix4d", 4)])
def test_matrix_authoring(dtype, dim):
    rows = tuple(tuple(1.0 if i == j else 0.0 for j in range(dim)) for i in range(dim))
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "x")
    p.set_attribute("m", rows, dtype=dtype)
    s.add_root_prim(p)
    out = _roundtrip(s).export_to_string()
    assert f"{dtype} m" in out, out


def test_bool_authoring_distinct_from_int():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "x")
    p.set_attribute("flag", True)
    p.set_attribute("count", 5)
    s.add_root_prim(p)
    out = _roundtrip(s).export_to_string()
    assert "bool flag" in out, out
    assert "int count" in out, out
