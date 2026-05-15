"""Verify set_attribute_at_time propagates the `dtype=` hint end-to-end.

Phase C.3: when a caller pins dtype, the authored typeName must match,
not fall back to the value-derived default (e.g. float3 instead of
color3f, double[] instead of matrix4d, etc.).
"""
import pytest
import tinyusdz


FORMATS = ["usda", "usdc"]


def _stage(name, samples, dtype):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    for t, v in samples:
        p.set_attribute_at_time(name, t, v, dtype=dtype)
    s.add_root_prim(p)
    return s


def _roundtrip(stage, fmt, tmp_path):
    out = tmp_path / f"x.{fmt}"
    stage.save(str(out))
    return tinyusdz.load(str(out))


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,value", [
    ("color3f", (0.1, 0.2, 0.3)),
    ("point3f", (1.0, 2.0, 3.0)),
    ("normal3f", (0.0, 1.0, 0.0)),
    ("vector3f", (4.0, 5.0, 6.0)),
    ("texCoord2f", (0.5, 0.75)),
])
def test_role_typed_3f_2f_dtype_preserved(tmp_path, fmt, dtype, value):
    s = _stage("v", [(0.0, value), (24.0, value)], dtype)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    # The role-typed dtype should appear verbatim in the authored USDA.
    assert dtype + " v.timeSamples" in txt, (
        f"expected '{dtype} v.timeSamples' in:\n{txt}")


@pytest.mark.parametrize("fmt", FORMATS)
def test_quatf_dtype_preserved(tmp_path, fmt):
    s = _stage("q", [(0.0, (1.0, 0.0, 0.0, 0.0)),
                     (24.0, (0.7071, 0.0, 0.7071, 0.0))], "quatf")
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "quatf q.timeSamples" in txt, txt


@pytest.mark.parametrize("fmt", FORMATS)
def test_matrix4d_dtype_preserved(tmp_path, fmt):
    m = ((1.0, 0.0, 0.0, 0.0),
         (0.0, 1.0, 0.0, 0.0),
         (0.0, 0.0, 1.0, 0.0),
         (0.0, 0.0, 0.0, 1.0))
    s = _stage("xform", [(0.0, m), (24.0, m)], "matrix4d")
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "matrix4d xform.timeSamples" in txt, txt
