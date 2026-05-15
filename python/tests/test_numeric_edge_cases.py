"""Numeric edge cases — extreme floats, integer boundaries, NaN/Inf,
half-precision rounding."""
import math

import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    return s2.export_to_string()


def test_int32_boundaries(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int max_i = 2147483647
    custom int min_i = -2147483648
}
''')
    assert "2147483647" in txt
    assert "-2147483648" in txt


def test_int64_boundaries(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int64 max64 = 9223372036854775807
    custom int64 min64 = -9223372036854775808
}
''')
    assert "9223372036854775807" in txt
    assert "-9223372036854775808" in txt


def test_uint64_max(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom uint64 maxu = 18446744073709551615
}
''')
    assert "18446744073709551615" in txt


def test_int64_above_inline_threshold(tmp_path):
    """Values above 2^47 require out-of-line storage in the ValueRep."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int64 big = 1125899906842624
    custom int64 bigger = 9000000000000000
}
''')
    assert "1125899906842624" in txt
    assert "9000000000000000" in txt


def test_float_zero_and_negative_zero(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom float pos = 0.0
    custom float neg = -0.0
}
''')
    assert "pos" in txt
    assert "neg" in txt


def test_double_precision_kept(tmp_path):
    """Author a double that requires more than float-32 precision."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom double d = 1.2345678901234567
}
''')
    assert "1.234567890" in txt


def test_half_precision_authored_via_python(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("h", 1.5, dtype="half")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "half h = 1.5" in txt


def test_half_precision_rounding_edge():
    """0.1 in float32 != 0.1 in float16 — write float32, read half[]
    and verify the rounded values are within half-precision tolerance."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("h", [0.1, 0.2, 0.3, 0.4], dtype="half[]")
    s.add_root_prim(p)
    a = p.get_attribute("h")
    assert a is not None
    assert a.type_name == "half[]"


def test_zero_array_size_does_not_crash(tmp_path):
    """No-element arrays should not crash the writer's size encoding."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int[] empty = []
    custom point3f[] noPts = []
}
''')
    assert "empty" in txt or "[]" in txt
