"""`over` prim semantics: adding/overriding attributes on existing prims."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_over_adds_attribute(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
over "X" {
    custom int newField = 42
}
''')
    assert "newField = 42" in txt


def test_over_with_typename(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
over Xform "X" {
    custom string label = "overridden"
}
''')
    assert '"overridden"' in txt


def test_over_nested_under_def(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Root" {
    over "Existing" {
        custom int n = 1
    }
}
''')
    assert "Existing" in txt


def test_over_with_relationship(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "T" {}
over "X" {
    rel link = </T>
}
''')
    assert "rel link" in txt
    assert "</T>" in txt


def test_over_with_xform_ops(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
over "X" {
    double3 xformOp:translate = (5, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
''')
    assert "xformOp:translate" in txt
    assert "(5, 0, 0)" in txt
