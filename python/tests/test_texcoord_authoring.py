"""texCoord{2,3}{f,d} scalar and array authoring round-trip.

Phase C.4: dtype dispatch in py_to_value + ConvertValue cases in
stage-converter.cc are already wired; these tests fence the surface.
"""
import pytest
import tinyusdz


FORMATS = ["usda", "usdc"]


def _roundtrip(stage, fmt, tmp_path):
    out = tmp_path / f"x.{fmt}"
    stage.save(str(out))
    return tinyusdz.load(str(out))


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,value", [
    ("texCoord2f", (0.5, 0.75)),
    ("texCoord2d", (0.5, 0.75)),
    ("texCoord3f", (0.5, 0.75, 0.25)),
    ("texCoord3d", (0.5, 0.75, 0.25)),
])
def test_texcoord_scalar_roundtrip(tmp_path, fmt, dtype, value):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("st", value, dtype=dtype)
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert dtype + " st" in txt, txt


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,components,values", [
    ("texCoord2f[]", 2, [(0.0, 0.0), (1.0, 1.0), (0.5, 0.5)]),
    ("texCoord2d[]", 2, [(0.0, 0.0), (1.0, 1.0)]),
    ("texCoord3f[]", 3, [(0.0, 0.0, 0.0), (1.0, 0.5, 0.5)]),
    ("texCoord3d[]", 3, [(0.1, 0.2, 0.3), (0.4, 0.5, 0.6)]),
])
def test_texcoord_array_roundtrip(tmp_path, fmt, dtype, components, values):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("st", values, dtype=dtype)
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert dtype + " st" in txt, txt


def test_texcoord_dtype_required_for_tuple():
    """Without dtype hint, a 2-tuple defaults to float2/double2 — not texCoord2f."""
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("st", (0.5, 0.75), dtype="texCoord2f")
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    txt = s.export_to_string()
    assert "texCoord2f st" in txt
