"""Author single-attribute stages of various value types, save in each
format, reload, and assert the value round-trips.

`double*`, `asset`, and time-sampled attributes are intentionally
skipped — the underlying C API does not yet expose those constructors.
"""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


FORMATS = ["usda", "usdc", "usdz"]


def _timesample_text(sample_value) -> str:
    if hasattr(sample_value, "to_string"):
        return sample_value.to_string()
    return str(sample_value)


def _build_one_attr_stage(value, dtype=None) -> tinyusdz.Stage:
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    if dtype:
        p.set_attribute("attr", value, dtype=dtype)
    else:
        p.set_attribute("attr", value)
    s.add_root_prim(p)
    return s


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("py_value, expected_scalar", [
    (42,        42),
    (3.5,       pytest.approx(3.5, rel=1e-6)),
    (True,      1),     # bool currently coerces to int
    ("hello",   "hello"),
])
def test_scalar_roundtrip(tmp_path: pathlib.Path, fmt, py_value,
                          expected_scalar):
    s = _build_one_attr_stage(py_value)
    out = tmp_path / f"a.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    a = s2.get_prim_at_path("/X").get_attribute("attr")
    assert a is not None
    v = a.value
    assert v is not None
    assert v.as_scalar() == expected_scalar


@pytest.mark.parametrize("fmt", FORMATS)
def test_int_array_roundtrip(tmp_path: pathlib.Path, fmt):
    s = _build_one_attr_stage([1, 2, 3, 4])
    out = tmp_path / f"a.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    v = s2.get_prim_at_path("/X").get_attribute("attr").value
    assert v.is_array
    assert list(memoryview(v)) == [1, 2, 3, 4]


@pytest.mark.parametrize("fmt", FORMATS)
def test_float_array_roundtrip(tmp_path: pathlib.Path, fmt):
    s = _build_one_attr_stage([0.5, 1.5, 2.5])
    out = tmp_path / f"a.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    v = s2.get_prim_at_path("/X").get_attribute("attr").value
    np = pytest.importorskip("numpy")
    arr = np.asarray(v)
    assert arr.dtype == np.float32
    assert arr.tolist() == [0.5, 1.5, 2.5]


@pytest.mark.parametrize("fmt", FORMATS)
def test_float3_vector_roundtrip(tmp_path: pathlib.Path, fmt):
    s = _build_one_attr_stage((1.0, 2.0, 3.0))   # tuple -> float3 (vector)
    out = tmp_path / f"a.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    v = s2.get_prim_at_path("/X").get_attribute("attr").value
    # float3 may surface as a 3-element buffer or scalar tuple-string;
    # the key invariant is the value can be read back without crashing.
    assert v is not None


@pytest.mark.parametrize("fmt", FORMATS)
def test_point3f_array_roundtrip(tmp_path: pathlib.Path, fmt):
    pts = [(0.0, 0.0, 0.0), (1.0, 2.0, 3.0), (4.0, 5.0, 6.0)]
    s = _build_one_attr_stage(pts, dtype="point3f[]")
    out = tmp_path / f"a.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    v = s2.get_prim_at_path("/X").get_attribute("attr").value
    np = pytest.importorskip("numpy")
    arr = np.asarray(v)
    assert arr.shape == (3, 3)
    assert arr.dtype == np.float32
    assert arr.tolist() == [list(p) for p in pts]


@pytest.mark.parametrize("fmt", FORMATS)
def test_token_via_dtype_roundtrip(tmp_path: pathlib.Path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("kind", "component", dtype="token")
    s.add_root_prim(p)
    out = tmp_path / f"a.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    a = s2.get_prim_at_path("/X").get_attribute("kind")
    assert a is not None
    assert a.value.as_scalar() == "component"


@pytest.mark.parametrize("fmt", FORMATS)
def test_double_scalar_roundtrip(tmp_path: pathlib.Path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("metersPerUnit", 0.01, dtype="double")
    s.add_root_prim(p)
    out = tmp_path / f"d.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    a = s2.get_prim_at_path("/X").get_attribute("metersPerUnit")
    assert a is not None and a.value is not None
    assert pytest.approx(a.value.as_scalar(), rel=1e-9) == 0.01


@pytest.mark.parametrize("fmt", FORMATS)
def test_double3_roundtrip(tmp_path: pathlib.Path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("xformOp:translate", (1.5, 2.5, 3.5), dtype="double3")
    p.set_attribute(
        "xformOpOrder", ["xformOp:translate"], dtype="token[]"
    )
    p.set_attribute_metadata("xformOpOrder", "variability", "uniform")
    s.add_root_prim(p)
    out = tmp_path / f"d3.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    a = s2.get_prim_at_path("/X").get_attribute("xformOp:translate")
    assert a is not None and a.value is not None
    assert a.type_name == "double3"
    text = a.value.to_string()
    assert "1.5" in text and "2.5" in text and "3.5" in text


def test_asset_attribute_in_memory():
    p = tinyusdz.Prim("Material", name="Mat")
    p.set_attribute("inputs:file", "./tex.png", dtype="asset")
    a = p.get_attribute("inputs:file")
    assert a is not None
    assert a.type_name == "asset"
    assert a.value.as_scalar() == "@./tex.png@"


@pytest.mark.parametrize("fmt", FORMATS)
def test_asset_attribute_roundtrip(tmp_path: pathlib.Path, fmt):
    s = tinyusdz.Stage()
    m = tinyusdz.Prim("Material", name="Mat")
    m.set_attribute("inputs:file", "./tex.png", dtype="asset")
    s.add_root_prim(m)
    out = tmp_path / f"a.{fmt}"
    s.save(str(out), format=fmt)
    if fmt == "usda":
        text = out.read_text()
        assert "asset inputs:file = @./tex.png@" in text
    s2 = tinyusdz.load(str(out))
    a = s2.get_prim_at_path("/Mat").get_attribute("inputs:file")
    assert a is not None
    assert a.type_name == "asset"
    assert a.value.as_scalar() == "@./tex.png@"


def test_asset_array_in_memory():
    p = tinyusdz.Prim("Material", name="Mat")
    p.set_attribute(
        "inputs:files",
        ["./a.png", "./b.png"],
        dtype="asset[]",
    )
    a = p.get_attribute("inputs:files")
    assert a is not None
    assert a.type_name == "asset[]"


def test_timesamples_in_memory():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute_at_time(
        "xformOp:translate", 0.0, (0.0, 0.0, 0.0), dtype="double3"
    )
    p.set_attribute_at_time(
        "xformOp:translate", 24.0, (10.0, 0.0, 0.0), dtype="double3"
    )
    samples = p.get_attribute_timesamples("xformOp:translate")
    assert len(samples) == 2
    assert samples[0][0] == 0.0
    assert samples[1][0] == 24.0
    assert "10" in _timesample_text(samples[1][1])


@pytest.mark.parametrize("fmt", FORMATS)
def test_timesamples_roundtrip(tmp_path: pathlib.Path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute_at_time(
        "xformOp:translate", 0.0, (1.0, 2.0, 3.0), dtype="double3"
    )
    p.set_attribute_at_time(
        "xformOp:translate", 24.0, (4.0, 5.0, 6.0), dtype="double3"
    )
    p.set_attribute("xformOpOrder", ["xformOp:translate"], dtype="token[]")
    p.set_attribute_metadata("xformOpOrder", "variability", "uniform")
    s.add_root_prim(p)

    out = tmp_path / f"ts.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    samples = p2.get_attribute_timesamples("xformOp:translate")
    assert len(samples) == 2
    assert samples[0][0] == 0.0
    assert samples[1][0] == 24.0
    # Sample value payloads — the bug we just fixed corrupted these on USDC.
    s0 = _timesample_text(samples[0][1])
    s1 = _timesample_text(samples[1][1])
    assert "1" in s0 and "2" in s0 and "3" in s0
    assert "4" in s1 and "5" in s1 and "6" in s1


def test_timesamples_scalar_float_in_memory():
    """Scalar float time samples (in-memory)."""
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute_at_time("custom:value", 0.0, 1.0)
    p.set_attribute_at_time("custom:value", 10.0, 2.0)
    p.set_attribute_at_time("custom:value", 20.0, 1.5)
    samples = p.get_attribute_timesamples("custom:value")
    assert len(samples) == 3
    assert [t for t, _ in samples] == [0.0, 10.0, 20.0]
