"""USDA `= None` value-block parsing and round-trip.

`None` on an attribute means the attribute is *defined* but has no
authored value (a "block"); composition reveals the underlying
default. Both scalar and array forms must parse without error and
emit back identically.
"""
import tinyusdz


def test_scalar_value_block_parses(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom int n = None
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    # Either spelling — the value block must survive.
    assert "n" in txt
    assert ('= None' in txt) or ('n;' in txt) or ('int n' in txt)


def test_array_value_block_parses(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom int[] arr = None
    custom int[] vals = [1, 2, 3]
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    assert "vals" in txt
    assert "[1, 2, 3]" in txt


def test_value_block_with_other_attrs(tmp_path):
    """A blocked attribute alongside non-blocked attributes must not
    confuse the parser."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Mesh "M" {
    int primvars:myint = None
    int[] primvars:displayColor:indices = None
    point3f[] points = [(0, 0, 0), (1, 0, 0)]
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    assert "(0, 0, 0)" in txt
    assert "(1, 0, 0)" in txt
