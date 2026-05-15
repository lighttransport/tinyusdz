"""Empty arrays and degenerate-but-valid edge cases.

USDA accepts `attr[] = []` (an empty array, distinct from no value
and from a value-block). USDC must round-trip the empty form.
"""
import tinyusdz


def _rt_usdc(tmp_path, usda_text):
    src = tmp_path / "x.usda"
    src.write_text(usda_text)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    return s2.export_to_string()


def test_empty_int_array(tmp_path):
    txt = _rt_usdc(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int[] empty = []
}
''')
    assert "int[] empty = []" in txt


def test_empty_float_array(tmp_path):
    txt = _rt_usdc(tmp_path, '''#usda 1.0
def Xform "X" {
    custom float[] empty = []
}
''')
    assert "float[] empty = []" in txt


def test_empty_token_array(tmp_path):
    txt = _rt_usdc(tmp_path, '''#usda 1.0
def Xform "X" {
    custom token[] empty = []
}
''')
    assert "token[] empty = []" in txt


def test_empty_string_array(tmp_path):
    txt = _rt_usdc(tmp_path, '''#usda 1.0
def Xform "X" {
    custom string[] empty = []
}
''')
    assert "string[] empty = []" in txt


def test_empty_point3f_array(tmp_path):
    txt = _rt_usdc(tmp_path, '''#usda 1.0
def Mesh "M" {
    point3f[] points = []
}
''')
    assert "point3f[] points = []" in txt


def test_single_element_arrays(tmp_path):
    """1-element arrays: not empty, but smallest non-trivial size."""
    txt = _rt_usdc(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int[] one_int = [42]
    custom float[] one_float = [3.14]
    custom point3f[] one_point = [(1, 2, 3)]
}
''')
    assert "[42]" in txt
    assert "3.14" in txt
    assert "(1, 2, 3)" in txt


def test_authored_via_python_empty_arrays(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("ints", [], dtype="int[]")
    p.set_attribute("floats", [], dtype="float[]")
    p.set_attribute("toks", [], dtype="token[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "[]" in txt
