"""Numeric formatting: small floats, scientific notation, special values."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out))


def test_small_float(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom float small = 0.000001
}
''')
    v = s.get_prim_at_path("/X").get_attribute("small").value
    assert abs(v.as_scalar() - 0.000001) < 1e-9


def test_negative_zero(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom double n = -0.0
}
''')
    v = s.get_prim_at_path("/X").get_attribute("n").value
    assert v.as_scalar() == 0.0


def test_double_precision(tmp_path):
    """Double should preserve more than 7 sig figs."""
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom double pi = 3.14159265358979
}
''')
    v = s.get_prim_at_path("/X").get_attribute("pi").value
    assert abs(v.as_scalar() - 3.14159265358979) < 1e-12


def test_int_max_min(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int hi = 2147483647
    custom int lo = -2147483648
}
''')
    p = s.get_prim_at_path("/X")
    assert p.get_attribute("hi").value.as_scalar() == 2147483647
    assert p.get_attribute("lo").value.as_scalar() == -2147483648


def test_uint_large(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom uint big = 4000000000
}
''')
    v = s.get_prim_at_path("/X").get_attribute("big").value
    assert v.as_scalar() == 4000000000


def test_scientific_notation_input(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom float light_speed = 2.998e8
}
''')
    v = s.get_prim_at_path("/X").get_attribute("light_speed").value
    assert abs(v.as_scalar() - 2.998e8) < 1e3


def test_int64_full_range(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int64 hi = 9223372036854775807
    custom int64 lo = -9223372036854775808
}
''')
    p = s.get_prim_at_path("/X")
    assert p.get_attribute("hi").value.as_scalar() == 9223372036854775807
    assert p.get_attribute("lo").value.as_scalar() == -9223372036854775808
