"""Authoring of half/uint/int64/quat dtypes via Python and Stage.set_default_prim."""
import os
import tempfile

import pytest
import tinyusdz


def _roundtrip(stage):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "x.usdc")
        stage.save(path)
        return tinyusdz.load(path)


def test_set_default_prim():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "World")
    s.add_root_prim(p)
    s.set_default_prim("World")
    assert s.get_default_prim() == "World"
    assert 'defaultPrim = "World"' in _roundtrip(s).export_to_string()


@pytest.mark.parametrize("dtype", ["half", "half2", "half3", "half4"])
def test_half_authoring(dtype):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "x")
    if dtype == "half":
        p.set_attribute("v", 1.5, dtype="half")
    else:
        n = int(dtype[-1])
        p.set_attribute("v", tuple(0.5 * i for i in range(n)), dtype=dtype)
    s.add_root_prim(p)
    out = _roundtrip(s).export_to_string()
    assert f"{dtype} v" in out, out


def test_uint_authoring():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "x")
    p.set_attribute("u", 42, dtype="uint")
    s.add_root_prim(p)
    out = _roundtrip(s).export_to_string()
    assert "uint u = 42" in out


def test_int64_large_value():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "x")
    p.set_attribute("v", -9876543210, dtype="int64")
    s.add_root_prim(p)
    out = _roundtrip(s).export_to_string()
    assert "int64 v = -9876543210" in out, out


def test_uint64_large_value():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "x")
    p.set_attribute("v", 12345678901234, dtype="uint64")
    s.add_root_prim(p)
    out = _roundtrip(s).export_to_string()
    assert "uint64 v = 12345678901234" in out, out


@pytest.mark.parametrize("dtype", ["quath", "quatf", "quatd"])
def test_quat_roundtrip(dtype):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", "x")
    # USDA spelling is (w, x, y, z) — same as tuple order.
    p.set_attribute("q", (0.5, 0.6, 0.7, 0.8), dtype=dtype)
    s.add_root_prim(p)
    out = _roundtrip(s).export_to_string()
    assert f"{dtype} q" in out, out
